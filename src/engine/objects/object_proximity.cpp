// The object proximity component (GameObjects::ObjectProximityComponent): see object_proximity.h. From the disassembly of
// 0x004EC220-0x004EC557 (the member functions 0x004EC250-0x004EC364 and 0x004EC480 aren't Ghidra functions).

#include "engine/objects/object_proximity.h"

#include <bit>
#include <cstdint>
#include <gsl/pointers>
#include <array>

#include "engine/core/mem.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "game/skate/skate_module.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/camera_lookaround.h"
#include "game/rider/vehicle_sound.h"
#include "game/rider/rider.h"

namespace objects {
namespace {
constexpr uint32_t kObjSetInnerRadius = 0x82A0A873; // Obj_SetInnerRadius
constexpr uint32_t kObjectproximity = 0x1D5B1AE6; // objectproximity
constexpr uint32_t kObjSetOuterRadius = 0x8CB5D0C0; // Obj_SetOuterRadius
constexpr uint32_t kObjSetInnerAvoidRadius = 0x50F28D36; // Obj_SetInnerAvoidRadius
constexpr uint32_t kObjSetOuterAvoidRadius = 0x2D703B94; // Obj_SetOuterAvoidRadius
constexpr uint32_t kObjSetAttentionRadius = 0x13C0D8DC; // Obj_SetAttentionRadius
constexpr uint32_t kObjObjectInAttentionRadius = 0x9522F906; // Obj_ObjectInAttentionRadius
constexpr uint32_t kColObjId = 0xF79C90AE; // colObjId
constexpr uint32_t kIntId = 0xDB1E28CD; // intId
constexpr uint32_t kObjectInRadius = 0x7048029A; // ObjectInRadius
constexpr uint32_t kAnyObjectInRadius = 0xF5B2A042; // AnyObjectInRadius
constexpr uint32_t kHash873A40D0 = 0x873A40D0; // (text unknown)
constexpr uint32_t kOutIntId = 0x5F3602F5; // outIntId
constexpr uint32_t kObjectOutofRadius = 0x91BCAD1D; // ObjectOutofRadius
constexpr uint32_t kObjectInAvoidRadius = 0xD69BD3DE; // ObjectInAvoidRadius
constexpr uint32_t kObjectOutofAvoidRadius = 0x35FDAFAE; // ObjectOutofAvoidRadius
constexpr uint32_t kObjectInAttentionRadius = 0xA06C9031; // ObjectInAttentionRadius
constexpr uint32_t kHash9C1CDE7A = 0x9C1CDE7A; // (text unknown)
constexpr uint32_t kIfZero = 0xFD2EACAD; // ifZero
} // namespace

std::array<script::MemberFunctionEntry, 7>& object_proximity_member_functions()
{
    static std::array<script::MemberFunctionEntry, 7> object_proximity_member_functions{{
        {.callsign_ = kObjSetInnerRadius, .owner_kind_ = kObjectproximity}, {.callsign_ = kObjSetOuterRadius, .owner_kind_ = kObjectproximity}, {.callsign_ = kObjSetInnerAvoidRadius, .owner_kind_ = kObjectproximity}, {.callsign_ = kObjSetOuterAvoidRadius, .owner_kind_ = kObjectproximity},
        {.callsign_ = kObjSetAttentionRadius, .owner_kind_ = kObjectproximity}, {.callsign_ = kObjObjectInAttentionRadius, .owner_kind_ = kObjectproximity}, {.callsign_ = 0, .owner_kind_ = kObjectproximity}}};
    return object_proximity_member_functions;
}

namespace {

constexpr uint32_t kObjectProximity = 0x1D5B1AE6;
constexpr float kTwelveInchScale = 12.0f; // 0x0064546C

// 0x0064B6F8 (bits 0x55CE224C, about 2.8e13): "no skater".
float far_away()
{
    const uint32_t bits = 0x55CE224C;
    auto value = std::bit_cast<float>(bits);
    return value;
}

// The unnamed float, if given, as (12 x it) squared (rounded once, as the original's x87 product).
bool set_squared_radius(script::Struct* params, float& radius)
{
    if (params->fetch_float(0u, &radius, 0)) {
        const double inches = static_cast<double>(radius) * kTwelveInchScale;
        radius = static_cast<float>(inches * inches);
    }
    return true;
}

double get_distance_squared(const GameEntity* a, const GameEntity* b)
{
    const double dx = static_cast<double>(a->at_point().x) - b->at_point().x;
    const double dy = static_cast<double>(a->at_point().y) - b->at_point().y;
    const double dz = static_cast<double>(a->at_point().z) - b->at_point().z;
    return (dx * dx + dz * dz) + dy * dy;
}

} // namespace

// 0x004EC420
gsl::owner<ObjectModule*> ObjectProximityComponent::fabricate() { return new ObjectProximityComponent; }

// 0x004EC220
void ObjectProximityComponent::publish_method_table()
{
    script::register_member_function_table(object_proximity_member_functions());
}

// 0x004EC370
ObjectProximityComponent::ObjectProximityComponent() { set_kind_tag(kObjectProximity); }

// 0x004EC240 (0x004EC390 the deleting one)
ObjectProximityComponent::~ObjectProximityComponent() = default;

// 0x004EC3C0
double ObjectProximityComponent::local_skater_distance_squared(uint32_t* skater_id)
{
    auto* skater = modules::Skate::sole()->get_home_rider();
    if (!skater || skater == entity())
        return far_away();
    *skater_id = skater->ident();
    return get_distance_squared(skater, entity());
}

// 0x004EC4B0
float ObjectProximityComponent::nearest_skater_distance_squared(uint32_t* skater_id)
{
    float nearest = far_away();
    modules::Skate* skate = modules::Skate::sole();
    const int count = skate->count_riders();
    for (int i = 0; i < count; ++i) {
        const GameEntity* skater = modules::Skate::sole()->get_rider_at(i);
        if (!skater || skater == entity())
            continue;
        const double distance = get_distance_squared(skater, entity());
        if (distance < nearest) {
            nearest = static_cast<float>(distance);
            *skater_id = skater->ident();
        }
    }
    return nearest;
}

// 0x004EC560
void ObjectProximityComponent::tick_frame()
{
    // (the skater's id: the original's is a stack slot only written when a skater is found - left as it was
    // otherwise; ours starts at 0)
    uint32_t id = 0;
    float distance = 0.0f;
    bool dead = false;
    auto event = [this](uint32_t type, script::Struct* data) { entity()->post_own_event(type, data); };
    auto new_data = []() {
        gsl::owner<script::Struct*> data = script::spawn_param_bag();
        return data ? memory::construct_in<script::Struct>(data) : nullptr;
    };
    auto delete_data = [](script::Struct* data) {
        if (data) {
            data->get_clear();
            script::delete_struct(data);
        }
    };
    if (inner_radius_squared_ != 0.0f) {
        const double local = local_skater_distance_squared(&id);
        distance = static_cast<float>(local);
        if (local <= inner_radius_squared_) {
            // The local skater inside: the enter event, and the any-skater one.
            gsl::owner<script::Struct*> data = new_data();
            data->put_name_key(checksums::Name{kColObjId}, id);
            data->put_int(checksums::Name{kIntId}, static_cast<int>(id));
            event(kObjectInRadius, data);
            if (!(entity()->object_flags() & kObjectFlagDead))
                event(kAnyObjectInRadius, data);
            delete_data(data);
        } else {
            // Another skater inside: the any-skater event.
            if (nearest_skater_distance_squared(&id) <= inner_radius_squared_) {
                gsl::owner<script::Struct*> data = new_data();
                data->put_name_key(checksums::Name{kColObjId}, id);
                data->put_int(checksums::Name{kIntId}, static_cast<int>(id));
                event(kAnyObjectInRadius, data);
                delete_data(data);
            }
            distance = 0.0f;
        }
    }
    if (outer_radius_squared_ != 0.0f) {
        if (distance == 0.0f)
            distance = nearest_skater_distance_squared(&id);
        if (distance >= outer_radius_squared_ && !(entity()->object_flags() & kObjectFlagDead)) {
            gsl::owner<script::Struct*> data = new_data();
            data->put_name_key(checksums::Name{kHash873A40D0}, id);
            data->put_int(checksums::Name{kOutIntId}, static_cast<int>(id));
            event(kObjectOutofRadius, data);
            delete_data(data);
        }
    }
    if (inner_avoid_radius_squared_ != 0.0f) {
        if (distance == 0.0f)
            distance = static_cast<float>(local_skater_distance_squared(&id));
        if (distance <= inner_avoid_radius_squared_) {
            gsl::owner<script::Struct*> data = new_data();
            data->put_name_key(checksums::Name{kColObjId}, id);
            if (!(entity()->object_flags() & kObjectFlagDead))
                event(kObjectInAvoidRadius, data);
            delete_data(data);
        }
    }
    if (outer_avoid_radius_squared_ != 0.0f) {
        if (distance == 0.0f)
            distance = static_cast<float>(local_skater_distance_squared(&id));
        if (distance >= outer_avoid_radius_squared_ && !(entity()->object_flags() & kObjectFlagDead))
            event(kObjectOutofAvoidRadius, nullptr);
    }
    if (attention_radius_squared_ != 0.0f) {
        if (distance == 0.0f)
            distance = static_cast<float>(local_skater_distance_squared(&id));
        dead = (entity()->object_flags() & kObjectFlagDead) != 0;
        if (!dead)
            event(distance <= attention_radius_squared_ ? kObjectInAttentionRadius : kHash9C1CDE7A, nullptr);
    }
}

// 0x004EC250
bool ObjectProximityComponent::obj_set_inner_radius(script::Struct* params, script::Script* /*script*/)
{
    return set_squared_radius(params, inner_radius_squared_);
}

// 0x004EC280
bool ObjectProximityComponent::obj_set_outer_radius(script::Struct* params, script::Script* /*script*/)
{
    return set_squared_radius(params, outer_radius_squared_);
}

// 0x004EC2B0
bool ObjectProximityComponent::obj_set_inner_avoid_radius(script::Struct* params, script::Script* /*script*/)
{
    return set_squared_radius(params, inner_avoid_radius_squared_);
}

// 0x004EC2E0
bool ObjectProximityComponent::obj_set_outer_avoid_radius(script::Struct* params, script::Script* /*script*/)
{
    return set_squared_radius(params, outer_avoid_radius_squared_);
}

// 0x004EC310
bool ObjectProximityComponent::obj_set_attention_radius(script::Struct* params, script::Script* /*script*/)
{
    // (with the flag, only a radius still 0 is set; not a number counts as set)
    if (params->has_bare_flag(kIfZero) && !(attention_radius_squared_ == 0.0))
        return true;
    return set_squared_radius(params, attention_radius_squared_);
}

// 0x004EC480
bool ObjectProximityComponent::obj_object_in_attention_radius(script::Struct* /*params*/, script::Script* /*script*/)
{
    uint32_t skater = 0;
    return local_skater_distance_squared(&skater) <= attention_radius_squared_;
}

} // namespace objects
