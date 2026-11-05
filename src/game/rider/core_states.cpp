// The skater's core physics component: the lip state's update, and getting back on the board.

#include <cstdint>

#include "engine/memory/manager.h"
#include "base/maths/mat4.h"
#include "base/maths/vec4.h"
#include "engine/objects/movable_contact.h"
#include "engine/objects/entity.h"
#include "engine/objects/rail_manager.h"
#include "engine/objects/ride_tracker.h"
#include "engine/objects/smtptr.h"
#include "game/rider/walk_hang.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "game/skate/career.h"
#include "game/rider/core_fields.h"
#include "game/rider/core_physics.h"
#include "game/rider/manual_balance.h"
#include "game/rider/rider.h"
#include "game/rider/trick_component.h"
#include "game/skate/skate_module.h"
#include "engine/objects/trigger_events.h"
#include "game/rider/balance_trick.h"
#include "game/rider/walk_physics.h"
#include "base/core/checked_index.h"
#include "game/online/net_game_state.h"

namespace skater {

using namespace core_fields;

namespace {

constexpr uint32_t kLipKind = 0xA549B57B;     // Lip (the balance trick's kind)
constexpr uint32_t kPerfectRail = 0xCD09E062; // CHEAT_PERFECT_RAIL
constexpr uint32_t kName = 0xA1DC81F9;


} // namespace

// 0x0050BF50
void SkaterPhysicsModule::step_coping_move()
{
    set_flag(this, PoseFlag::kAcidDrop, 0);
    set_flag(this, PoseFlag::kFromAcidDrop, 0);
    set_flag(this, PoseFlag::kRecoverInAir, 0);
    model_rotation_.flip_spin_started = 0;
    // Carried by what the skater rides on: the position, and the start of the lip (+0x258) - 0x00427A00 works it out.
    if (objects::RideTracker* tracker = get_tracker(this)) {
        if (!tracker->entity().get_pull() || !tracker->tick_frame(&entity()->at_point())) {
            stop_riding(this);
        } else {
            // (0x00427A00 returns `where`; ours is void: the point is read from `where`)
            auto* contact = siblings_.ride;
            maths::Vec4 where;
            objects::GameEntity* object = object_of(this);
            contact->get_contact_point(&where);
            object->at_point() = where;
            siblings_.ride->get_contact_point(&where);
            lip_pos_ = where;
            if (get_tracker(this)->turned()) {
                maths::Mat4& matrix = entity()->basis_frame();
                maths::matrix_multiply(&matrix, &matrix, &get_tracker(this)->turn());
                maths::matrix_multiply(&ground_.display_matrix, &ground_.display_matrix, &get_tracker(this)->turn());
            }
        }
    }
    // The lip's balance (the Perfect Rail cheat: its global flag, and no lean).
    if (siblings_.balance->trick() == kLipKind) {
        auto career = [] { return modules::Skate::sole()->career().get(); };
        if (career()->is_code_on(kPerfectRail)) {
            career()->set_global_flag(script::fetch_int_value(kPerfectRail));
            siblings_.balance->lip().set_lean(0.0F);
            siblings_.balance->lip().set_lean_speed(0.0F);
            online::net_game_state().set_cheat_flag(true);
        }
        siblings_.balance->lip().tick_frame();
    }
    if ((bit_mark(this, PoseFlag::kCrouched) == 0U) && (siblings_.input->pad_primary().pos_x().held_down() != 0U)) {
        set_flag(this, PoseFlag::kCrouched, 1);
    }
    // Off the lip (0x004FFB10): the rail's TriggerScript, twice.
    if (jump_released()) {
        set_flag(this, PoseFlag::kJumped, 1);
        trigger_rail_script(kLipOffEvents);
        trigger_rail_script(kOffLipEvents);
    }
    auto* node = rail_.node;
    if (node && rail_.manager->node_array()) {
        script::Struct* script = level_nodes::fetch_vertex(node->label(), true);
        script->read_hash_value(kName, &rail_.script_node, 1);
        static_cast<objects::MoveExecutor*>(siblings_.trick)->pass_to_trick_objects(rail_.script_node);
    }
    rail_.lip_time = get_now();
}

// 0x0050C670
void SkaterPhysicsModule::resume_skating(bool on_ground, const objects::FootMovement* grind_from,
                                         const AcidDrop* acid_drop)
{
    clear_flags();
    if (on_ground) {
        set_physics_state(kGround);
        frame_.old_state = kGround;
    } else {
        set_physics_state(kAir);
        frame_.old_state = kAir;
        set_flag(this, PoseFlag::kNoSecondSpin, 1);
        if (static_cast<uint8_t>(siblings_.walk->off_vert()) != 0U) {
            set_flag(this, PoseFlag::kNoAcidDrop, 1);
        }
    }
    frame_.state_changed_at = get_now();
    ground_.display_matrix = entity()->basis_frame();
    // The ground normals from the up (+0x68, +0x78, +0x58), the position kept as the last.
    ground_.floor_normal = core::at(entity()->basis_frame().row, 1);
    ground_.old_normal = ground_.floor_normal;
    ground_.drawn_normal = ground_.old_normal;
    frame_.safe_pos = entity()->at_point();
    entity()->old_pos() = frame_.safe_pos;
    // The jump / trick state cleared.
    feelers_.floor_feeler.hit().face_flag40_ = 0;
    side_.reach = 0.0F;
    jump_charge_ms_ = 0;
    wall_.ride_start = 0;
    wall_.plant_end = 0;
    air_flags_.no_spin = 0;
    no_kick_ = 0;
    can_brake_ = 1;
    siblings_.state->set_lip_straight_up(0); // (without its time)
    if (limits_.time != -kTimerForLevel) {
        limits_.time = 0.0F;
    }
    speed_override_ = 1.0F;
    wallplant_.time = 0;
    wallplant_.last_push_off = 0;
    leap_time_ = 0;
    wallplant_.vert_planted = 0;
    if (grind_from) {
        // Onto a rail: the old ride let go (its object kept at +0x6C), the rail's object ridden.
        objects::MovingSurfaceModule* ride = siblings_.ride;
        if (objects::RideTracker* tracker = ride->ride().get()) {
            ride->set_last_object(tracker->entity().get_pull());
            tracker->entity().get_release();
            memory::Hub::dealloc(tracker);
            ride->ride() = nullptr;
            ride->set_stop_time(get_now());
        }
        if (objects::GameEntity* object = grind_from->grind_object()) {
            siblings_.ride->ride_on(object);
        }
        start_grind(&grind_from->grind_point(), grind_from->grind_node(), grind_from->grind_rails(), true, true);
        frame_.safe_pos = entity()->at_point();
        entity()->old_pos() = frame_.safe_pos;
    } else if (acid_drop) {
        start_acid_drop(acid_drop);
    }
}

} // namespace skater
