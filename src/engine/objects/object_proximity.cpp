// The object proximity component (GameObjects::ObjectProximityComponent): see object_proximity.h. From the disassembly of
// 0x004EC220-0x004EC557 (the member functions 0x004EC250-0x004EC364 and 0x004EC480 aren't Ghidra functions).

#include "engine/objects/object_proximity.h"

#include <bit>
#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "game/skate/skate_module.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/camera_lookaround.h"
#include "game/rider/vehicle_sound.h"

namespace objects {

uint32_t g_object_proximity_member_functions[42]{
    0x82A0A873, 0x1D5B1AE6, 0, 0, 0, 0, 0x8CB5D0C0, 0x1D5B1AE6, 0, 0, 0, 0, 0x50F28D36, 0x1D5B1AE6, 0, 0, 0, 0,
    0x2D703B94, 0x1D5B1AE6, 0, 0, 0, 0, 0x13C0D8DC, 0x1D5B1AE6, 0, 0, 0, 0, 0x9522F906, 0x1D5B1AE6, 0, 0, 0, 0,
    0,          0x1D5B1AE6, 0, 0, 0, 0};

namespace {

constexpr uint32_t kObjectProximity = 0x1D5B1AE6;
constexpr float kTwelveInchScale = 12.0f; // 0x0064546C

// 0x0064B6F8 (bits 0x55CE224C, about 2.8e13): "no skater".
float far_away()
{
    uint32_t bits = 0x55CE224C;
    auto value = std::bit_cast<float>(bits);
    return value;
}

// The unnamed float, if given, as (12 x it) squared (rounded once, as the original's x87 product).
bool set_squared_radius(script::Struct* params, float& radius)
{
    if (params->fetch_float(0u, &radius, 0)) {
        double inches = static_cast<double>(radius) * kTwelveInchScale;
        radius = static_cast<float>(inches * inches);
    }
    return true;
}

double get_distance_squared(const GameEntity* a, const GameEntity* b)
{
    double dx = static_cast<double>(a->at_point_.x) - b->at_point_.x;
    double dy = static_cast<double>(a->at_point_.y) - b->at_point_.y;
    double dz = static_cast<double>(a->at_point_.z) - b->at_point_.z;
    return (dx * dx + dz * dz) + dy * dy;
}

} // namespace

// 0x004EC420
memory::owner<ObjectModule*> ObjectProximityComponent::fabricate() { return new ObjectProximityComponent; }

// 0x004EC220
void ObjectProximityComponent::publish_method_table()
{
    script::register_member_function_table(reinterpret_cast<void*>(kObjectProximity),
                                           g_object_proximity_member_functions, 0x18);
}

// 0x004EC370
ObjectProximityComponent::ObjectProximityComponent() { kind_tag_ = kObjectProximity; }

// 0x004EC240 (0x004EC390 the deleting one)
ObjectProximityComponent::~ObjectProximityComponent() = default;

// 0x004EC3C0
double ObjectProximityComponent::local_skater_distance_squared(uint32_t* skater_id)
{
    auto* skater = reinterpret_cast<GameEntity*>(modules::Skate::sole()->get_home_rider());
    if (!skater || skater == entity_)
        return far_away();
    *skater_id = skater->ident_;
    return get_distance_squared(skater, entity_);
}

// 0x004EC4B0
float ObjectProximityComponent::nearest_skater_distance_squared(uint32_t* skater_id)
{
    float nearest = far_away();
    modules::Skate* skate = modules::Skate::sole();
    int count = skate->count_riders();
    for (int i = 0; i < count; ++i) {
        GameEntity* skater = modules::Skate::sole()->get_rider_at(i);
        if (!skater || skater == entity_)
            continue;
        double distance = get_distance_squared(skater, entity_);
        if (distance < nearest) {
            nearest = static_cast<float>(distance);
            *skater_id = skater->ident_;
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
    auto event = [this](uint32_t type, script::Struct* data) { entity_->post_own_event(type, data); };
    auto new_data = []() {
        script::Struct* data = script::spawn_param_bag();
        return data ? new (data) script::Struct : nullptr;
    };
    auto delete_data = [](script::Struct* data) {
        if (data) {
            data->get_clear();
            script::delete_struct(data);
        }
    };
    if (inner_radius_squared_ != 0.0f) {
        double local = local_skater_distance_squared(&id);
        distance = static_cast<float>(local);
        if (local <= inner_radius_squared_) {
            // The local skater inside: the enter event, and the any-skater one.
            script::Struct* data = new_data();
            data->put_name_key(0xF79C90AE, id);
            data->put_int(0xDB1E28CD, id);
            event(0x7048029A, data);
            if (!(entity_->object_flags_ & kObjectFlagDead))
                event(0xF5B2A042, data);
            delete_data(data);
        } else {
            // Another skater inside: the any-skater event.
            if (nearest_skater_distance_squared(&id) <= inner_radius_squared_) {
                script::Struct* data = new_data();
                data->put_name_key(0xF79C90AE, id);
                data->put_int(0xDB1E28CD, id);
                event(0xF5B2A042, data);
                delete_data(data);
            }
            distance = 0.0f;
        }
    }
    if (outer_radius_squared_ != 0.0f) {
        if (distance == 0.0f)
            distance = nearest_skater_distance_squared(&id);
        if (distance >= outer_radius_squared_ && !(entity_->object_flags_ & kObjectFlagDead)) {
            script::Struct* data = new_data();
            data->put_name_key(0x873A40D0, id);
            data->put_int(0x5F3602F5, id);
            event(0x91BCAD1D, data);
            delete_data(data);
        }
    }
    if (inner_avoid_radius_squared_ != 0.0f) {
        if (distance == 0.0f)
            distance = static_cast<float>(local_skater_distance_squared(&id));
        if (distance <= inner_avoid_radius_squared_) {
            script::Struct* data = new_data();
            data->put_name_key(0xF79C90AE, id);
            if (!(entity_->object_flags_ & kObjectFlagDead))
                event(0xD69BD3DE, data);
            delete_data(data);
        }
    }
    if (outer_avoid_radius_squared_ != 0.0f) {
        if (distance == 0.0f)
            distance = static_cast<float>(local_skater_distance_squared(&id));
        if (distance >= outer_avoid_radius_squared_ && !(entity_->object_flags_ & kObjectFlagDead))
            event(0x35FDAFAE, nullptr);
    }
    if (attention_radius_squared_ != 0.0f) {
        if (distance == 0.0f)
            distance = static_cast<float>(local_skater_distance_squared(&id));
        dead = (entity_->object_flags_ & kObjectFlagDead) != 0;
        if (!dead)
            event(distance <= attention_radius_squared_ ? 0xA06C9031 : 0x9C1CDE7A, nullptr);
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
    if (params->has_bare_flag(0xFD2EACAD) && !(attention_radius_squared_ == 0.0))
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
