// The skater's core physics component: the lip state's update, and getting back on the board.

#include <cstdint>

#include "engine/core/manager.h"
#include "engine/gfx/d3dx.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/objects/movable_contact.h"
#include "engine/objects/entity.h"
#include "engine/objects/rail_manager.h"
#include "engine/objects/ride_tracker.h"
#include "engine/objects/smtptr.h"
#include "engine/objects/walk_hang.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "game/skate/career.h"
#include "game/rider/core_fields.h"
#include "game/rider/core_physics.h"
#include "game/rider/manual_balance.h"
#include "game/rider/rider.h"
#include "game/rider/trick_component.h"
#include "game/skate/skate_module.h"
#include "engine/net/gamenet_manager.h"
#include "engine/objects/trigger_events.h"

namespace skater {

using namespace core_fields;

namespace {

constexpr uint32_t kLipKind = 0xA549B57B;     // Lip (the balance trick's kind)
constexpr uint32_t kPerfectRail = 0xCD09E062; // CHEAT_PERFECT_RAIL
constexpr uint32_t kName = 0xA1DC81F9;

void change_state(void* component, int state)
{
    static_cast<SkaterPhysicsModule*>(component)->set_physics_state(state);
}

} // namespace

// 0x0050BF50
void SkaterPhysicsModule::step_coping_move()
{
    set_flag(this, 0x78, 0);
    set_flag(this, 0xA8, 0);
    set_flag(this, 0x80, 0);
    get_at<uint8_t>(this, 0x2E4) = 0;
    // Carried by what the skater rides on: the position, and the start of the lip (+0x258) - 0x00427A00 works it out.
    if (objects::RideTracker* tracker = get_tracker(this)) {
        if (!tracker->entity_.owner_object_ || !tracker->tick_frame(&vector_at(object_of(this), kPosition))) {
            stop_riding(this);
        } else {
            // (0x00427A00 returns `where`; ours is void: the point is read from `where`)
            auto* contact = get_at<objects::MovingSurfaceModule*>(this, kRide);
            maths::Vec4 where;
            void* object = object_of(this);
            contact->get_contact_point(&where);
            vector_at(object, kPosition) = where;
            get_at<objects::MovingSurfaceModule*>(this, kRide)->get_contact_point(&where);
            vector_at(this, 0x258) = where;
            if (get_tracker(this)->turned_) {
                auto multiply = d3dx::matrix_multiply;
                maths::Mat4& matrix = matrix_at(object_of(this), kMatrix);
                multiply(&matrix, &matrix, &get_tracker(this)->turn_);
                multiply(&matrix_at(this, kDisplayMatrix), &matrix_at(this, kDisplayMatrix), &get_tracker(this)->turn_);
            }
        }
    }
    // The lip's balance (the Perfect Rail cheat: its global flag, and no lean).
    if (get_at<uint32_t>(get_at<void*>(this, kBalanceTrick), 0x18) == kLipKind) {
        auto career = [] {
            return get_at<objects::RiderStoryProgress*>(reinterpret_cast<uint8_t*>(modules::g_skate_module), 0x20);
        };
        if (career()->is_code_on(kPerfectRail)) {
            career()->set_global_flag(script::fetch_int_value(kPerfectRail));
            get_at<uint32_t>(get_at<void*>(this, kBalanceTrick), 0x104) = 0;
            get_at<uint32_t>(get_at<void*>(this, kBalanceTrick), 0x108) = 0;
            reinterpret_cast<uint8_t&>(online::g_cheat_flag) = 1;
        }
        reinterpret_cast<objects::Balance*>(get_at<uint8_t*>(this, kBalanceTrick) + 0xD4)->tick_frame();
    }
    if (!bit_mark(this, 0x20) && input_of(this)[0x1C0])
        set_flag(this, 0x20, 1);
    // Off the lip (0x004FFB10): the rail's TriggerScript, twice.
    if (jump_released()) {
        set_flag(this, 0xD8, 1);
        trigger_rail_script(kLipOffEvents);
        trigger_rail_script(kOffLipEvents);
    }
    auto* node = get_at<objects::GrindPoint*>(this, kRailNode);
    if (node && get_at<objects::GrindRailHub*>(this, kRailManager)->node_array_) {
        script::Struct* script = level_nodes::fetch_vertex(node->label_, true);
        script->read_hash_value(kName, &get_at<uint32_t>(this, kRailScriptNode), 1);
        static_cast<objects::MoveExecutor*>(get_at<void*>(this, kSkaterComponent))
            ->pass_to_trick_objects(get_at<uint32_t>(this, kRailScriptNode));
    }
    get_at<int>(this, 0x238) = get_now();
}

// 0x0050C670
void SkaterPhysicsModule::resume_skating(bool on_ground, const uint8_t* grind, void* acid_drop)
{
    clear_flags();
    if (on_ground) {
        change_state(this, kGround);
        get_at<int>(this, 0x94) = kGround;
    } else {
        change_state(this, kAir);
        get_at<int>(this, 0x94) = kAir;
        set_flag(this, 0xC0, 1);
        if (get_at<uint8_t>(get_at<void*>(this, 0x1534), 0x9B))
            set_flag(this, 0xB0, 1);
    }
    get_at<int>(this, 0x90) = get_now();
    matrix_at(this, kDisplayMatrix) = matrix_at(object_of(this), kMatrix);
    // The ground normals from the up (+0x68, +0x78, +0x58), the position kept as the last.
    vector_at(this, kFloorUpVector) = vector_at(object_of(this), kUp);
    vector_at(this, 0x78) = vector_at(this, kFloorUpVector);
    vector_at(this, 0x58) = vector_at(this, 0x78);
    vector_at(this, 0x98) = vector_at(object_of(this), kPosition);
    vector_at(object_of(this), kOldPosition) = vector_at(this, 0x98);
    // The jump / trick state cleared.
    get_at<uint8_t>(this, 0x1C5) = 0;
    get_at<int>(this, 0x2C0) = 0;
    get_at<int>(this, kJumpChargeMs) = 0;
    get_at<int>(this, 0x39C) = 0;
    get_at<int>(this, 0x3A0) = 0;
    get_at<uint8_t>(this, 0x370) = 0;
    get_at<uint8_t>(this, 0x310) = 0;
    get_at<uint8_t>(this, 0x311) = 1;
    get_at<uint8_t>(flags_of(this), 0xF4) = 0; // (without its time)
    if (get_at<uint32_t>(this, 0x350) != 0xC0000000)
        get_at<uint32_t>(this, 0x350) = 0;
    get_at<float>(this, 0x3AC) = 1.0f;
    get_at<int>(this, 0x4E0) = 0;
    get_at<int>(this, 0x4E4) = 0;
    get_at<int>(this, kLeapDuration) = 0;
    get_at<uint8_t>(this, 0x4E8) = 0;
    if (grind) {
        auto* rail = const_cast<uint8_t*>(grind);
        // Onto a rail: the old ride let go (its object kept at +0x6C), the rail's object ridden.
        void* ride = get_at<void*>(this, kRide);
        if (objects::RideTracker* tracker = get_at<objects::RideTracker*>(ride, 0x18)) {
            get_at<void*>(ride, 0x6C) = tracker->entity_.owner_object_;
            reinterpret_cast<objects::SharedRef<objects::SharedBase>*>(&tracker->entity_)->get_release();
            memory::Hub::get_pull()->dealloc(tracker);
            get_at<objects::RideTracker*>(ride, 0x18) = nullptr;
            get_at<int>(ride, 0x70) = get_now();
        }
        if (void* object = get_at<void*>(rail, 0x18))
            get_at<walk::MovableContact*>(this, kRide)->start_riding(static_cast<objects::GameEntity*>(object));
        start_grind(reinterpret_cast<const maths::Vec4*>(grind), get_at<objects::GrindPoint*>(rail, 0x14),
                    get_at<objects::GrindRailHub*>(rail, 0x10), true, true);
        vector_at(this, 0x98) = vector_at(object_of(this), kPosition);
        vector_at(object_of(this), kOldPosition) = vector_at(this, 0x98);
    } else if (acid_drop) {
        start_acid_drop(static_cast<const AcidDrop*>(acid_drop));
    }
}

} // namespace skater
