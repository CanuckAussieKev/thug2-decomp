// The trigger component (engine/objects/trigger_component.h).

#include "engine/objects/trigger_component.h"

#include <cstdint>
#include <set>
#include <gsl/pointers>
#include <array>

#include "engine/render/coll_rect.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/entity.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "game/skate/skate_module.h"
#include "platform/run_log.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/trigger_events.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/core/ints.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"

// The event lists (trigger_events.h).
const std::array<int, 3> kWheelLeavesEvents{{1, 4, 0}};
const std::array<int, 3> kWheelLandsEvents{{1, 3, 0}};
const std::array<int, 3> kCrossingEvents{{9, 0, 0}};
const std::array<int, 3> kPullUpEvents{{1, 3, 0}};
const std::array<int, 3> kHangEvents{{10, 3, 0}};
const std::array<int, 3> kExitHangEvents{{10, 4, 0}};
const std::array<int, 3> kLadderOffBottomEvents{{1, 3, 0}};
const std::array<int, 3> kLadderOffTopEvents{{14, 3, 0}};
const std::array<int, 3> kOntoLadderEvents{{10, 3, 0}};
const std::array<int, 3> kExitLadderEvents{{10, 4, 0}};
const std::array<int, 3> kProjectileTouchEvents{{14, 0, 0}};
const std::array<int, 3> kOtherFeelerLeaveEvents{{1, 5, 0}};
const std::array<int, 3> kJumpTriggerEvents{{2, 4, 0}};
const std::array<int, 3> kSkaterEvents0064bB98{{8, 3, 0}};
const std::array<int, 3> kKickEvents{{1, 4, 0}};
const std::array<int, 3> kSkaterEvents0064bBb0{{1, 3, 0}};
const std::array<int, 3> kBonkEvents{{9, 0, 0}};
const std::array<int, 3> kLipEvents{{7, 3, 0}};
const std::array<int, 3> kOffLipEvents{{7, 4, 0}};
const std::array<int, 3> kLipOffEvents{{7, 2, 0}};
const std::array<int, 3> kSkaterEvents0064bBec{{6, 3, 0}};
const std::array<int, 3> kWallplantEvents{{6, 4, 0}};
const std::array<int, 3> kSpinEvents{{12, 3, 0}};
const std::array<int, 3> kJumpEvents{{12, 2, 0}};
const std::array<int, 3> kGrindOffEvents{{13, 4, 0}};
const std::array<int, 3> kGrindEvents{{13, 3, 0}};
const std::array<int, 3> kTaggingEvents{{11, 3, 0}};
const std::array<int, 3> kWalkEvents0064c630{{1, 5, 0}};
const std::array<int, 3> kWalkEvents0064c63c{{2, 4, 0}};
const std::array<int, 3> kWalkEvents0064c648{{8, 3, 0}};
const std::array<int, 3> kWalkEvents0064c654{{14, 3, 0}};
const std::array<int, 3> kWalkEvents0064c660{{14, 4, 0}};

namespace objects {

namespace {

constexpr uint32_t kTriggerScript = 0x2CA8A299; // TriggerScript
constexpr uint32_t kOnlineFlag = 0x20209C31;    // online_mode_flag
constexpr uint32_t kPermanent = 0x23627FD7;     // Permanent
constexpr uint32_t kParams = 0x5180A4DB;        // (a parameters structure on the other object's node)
constexpr uint32_t kTriggerType = 0x3A717382;   // TriggerType
constexpr uint32_t kNodeArray = 0xC472ECC5;     // nodearray (the component holding an object's own nodes)

// Faces the update's feeler looks for (the trigger flag 0x40 and 0x10).
constexpr uint16_t kTriggerFaces = 0x50;

// No events (0x0064663C: three zeros).
const std::array<int, 3> kNoEvents = {{0, 0, 0}};

// The node array of `object`'s nodearray component (the original's slot 13), or null without one.
script::Array* node_array_of(GameEntity* object)
{
    ObjectModule* nodes = object->fetch_module(kNodeArray);
    if (!nodes)
        return nullptr;
    return core::down_cast<WaypointListModule>(nodes)->get_node_array();
}

} // namespace

// 0x00434B90
gsl::owner<ObjectModule*> ActivationZoneModule::fabricate() { return new ActivationZoneModule; }

// (the constructor: inlined in fabricate in the original)
ActivationZoneModule::ActivationZoneModule() { set_kind_tag(kType); }

// 0x00434900 (0x00434910 the deleting one)
ActivationZoneModule::~ActivationZoneModule() = default;

// 0x00434940
void ActivationZoneModule::configure_from_data(script::Struct* /*params*/) { last_pos_ = entity()->at_point(); }

// 0x00434D90
void ActivationZoneModule::tick_frame()
{
    const GameEntity* object = entity();
    const maths::Vec4& pos = object->at_point();
    // (not a number counts as moved, as the original's FUCOMPP)
    if (last_pos_.x == pos.x && last_pos_.y == pos.y && last_pos_.z == pos.z && last_pos_.w == pos.w)
        return;
    if (object->composite_flags() & kCompositeFlagTeleported) {
        last_pos_ = pos;
        return;
    }
    render::ProbeRay feeler;
    feeler.line().start = last_pos_;
    feeler.line().end = entity()->at_point();
    feeler.set_excluded(0, kTriggerFaces);
    feeler.set_callback([](render::ProbeRay* hit) { (void)feeler_hit(hit); });
    feeler.set_user_data(this);
    feeler.get_hit_shape(true, false);
    last_pos_ = entity()->at_point();
}

// 0x00434970
int ActivationZoneModule::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* /*script*/)
{
    if (name != kTriggerType)
        return 2;
    // (the array is read into the name's own slot, cleared first)
    script::Array* array = nullptr;
    params->fetch_list(0u, &array, 0);
    // One integer array, or an array of them - any one of which may match.
    script::Array* arrays = nullptr;
    uint32_t count = 1;
    if (array->kind_tag() == script::kQbvalArray) {
        count = array->extent();
        arrays = array;
    }
    for (uint32_t i = 0; i < count; ++i) {
        if (arrays)
            array = arrays->array_at(i);
        if (array->kind_tag() != script::kQbvalInteger)
            return 0;
        uint32_t wanted = 0;
        for (int k = 0; k < ints::as_signed(array->extent()); ++k) {
            if (static_cast<int>(array->fetch_part(static_cast<uint32_t>(k))) >= 0)
                wanted += 1u << (array->fetch_part(static_cast<uint32_t>(k)) & 31);
        }
        if ((events_ | wanted) == events_)
            return 1;
    }
    return 0;
}

// 0x00434A60
bool ActivationZoneModule::get_trigger_node(const std::array<int, 3>& events, uint32_t name, script::Array* nodes, const Entity* other)
{
    script::Struct* node = nullptr;
    if (!nodes) {
        node = level_nodes::fetch_vertex(name, false);
    } else {
        // (from the last; none named so leaves the first one checked - element 0 -, as the original)
        for (uint32_t i = nodes->extent(); i > 0;) {
            node = nodes->struct_at(--i);
            if (level_nodes::get_node_name(node) == name)
                break;
        }
    }
    script::Struct* params = nullptr;
    if (other) {
        if (script::Struct* other_node = level_nodes::fetch_vertex(other->ident(), false))
            other_node->read_sub_struct(kParams, &params, 0);
    }
    // Not original - a trigger face whose object has no node (a level's collision with another level's node array:
    // the front end's SKATE NOW in THPS4's shop had its halfpipe's gap triggers - the user, 2026-10-19) is no
    // trigger, logged once per name.
    // Original behaviour: the node's TriggerScript read at once, through the null when there's no node (a crash
    // reading address 4):
    //     uint32_t script = 0;
    //     node->read_hash_value(kTriggerScript, &script, 0);
    if (!node) {
        static std::set<uint32_t> s_logged;
        if (s_logged.insert(name).second)
            get_log("trigger: no node named %08X for a trigger face (its collision object) - nothing run", name);
        return false;
    }
    uint32_t script = 0;
    node->read_hash_value(kTriggerScript, &script, 0);
    if (!script)
        return false;
    if (modules::Skate::should_be_absent(node))
        return false;
    uint32_t& recorded = events_;
    recorded = 0;
    for (const int event : events) { // (the mask of the events, up to three)
        if (event)
            recorded += 1u << (event & 31);
    }
    Entity* object = entity();
    const bool permanent = node->has_bare_flag(kPermanent);
    const bool replicate_flag = node->has_bare_flag(kOnlineFlag);
    object->start_script_task(script, static_cast<int>(name), replicate_flag, permanent, params);
    return true;
}

// 0x00434BF0
bool ActivationZoneModule::probe_touches_zone(const std::array<int, 3>& events, render::ProbeRay* feeler)
{
    if (!feeler->hit().face_flag40_ || !feeler->hit().object_checksum_)
        return false;
    if (feeler->get_tested_owner()) {
        // A movable object's face, while it's being tested: through its own nodes.
        auto* owner = feeler->get_tested_owner();
        script::Array* nodes = node_array_of(owner);
        const uint32_t name = feeler->hit().object_checksum_;
        return get_trigger_node(events, name, nodes, owner);
    }
    if (!feeler->hit_object_alive()) // (which clears the name when the object is gone)
        return get_trigger_node(events, feeler->hit().object_checksum_, nullptr, nullptr);
    if (!feeler->get_hit_object())
        return false;
    script::Array* nodes = node_array_of(feeler->get_hit_object());
    const uint32_t name = feeler->hit().object_checksum_;
    return get_trigger_node(events, name, nodes, feeler->get_hit_object());
}

// 0x00434CE0
void ActivationZoneModule::trigger_hits(const std::array<int, 3>& events, const render::BoxProbe* feeler)
{
    const render::RectHits& hits = feeler->hits();
    const int count = hits.count;
    for (int i = count - 1; i >= 0; --i) {
        if (!core::at(hits.hits, i).face_flag40_)
            continue;
        const uint32_t name = core::at(hits.hits, i).checksum;
        // (the count is read again: get_trigger_node may run scripts)
        const int total = hits.count;
        int later = i + 1;
        while (later < total && name != core::at(hits.hits, later).checksum)
            ++later;
        if (later == total)
            get_trigger_node(events, name, nullptr, nullptr);
    }
}

// 0x00434D70
bool ActivationZoneModule::feeler_hit(render::ProbeRay* feeler)
{
    auto* component = static_cast<ActivationZoneModule*>(feeler->user_data());
    return component->probe_touches_zone(kNoEvents, feeler);
}

} // namespace objects
