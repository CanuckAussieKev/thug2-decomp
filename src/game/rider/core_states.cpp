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
#include "game/rider/balance_trick.h"
#include "game/rider/walk_physics.h"

namespace skater {

using namespace core_fields;

namespace {

constexpr uint32_t kLipKind = 0xA549B57B;     // Lip (the balance trick's kind)
constexpr uint32_t kPerfectRail = 0xCD09E062; // CHEAT_PERFECT_RAIL
constexpr uint32_t kName = 0xA1DC81F9;

void change_state(SkaterPhysicsModule* component, int state)
{
    static_cast<SkaterPhysicsModule*>(component)->set_physics_state(state);
}

} // namespace

// 0x0050BF50
void SkaterPhysicsModule::step_coping_move()
{
    set_flag(this, PoseFlag::kAcidDrop, 0);
    set_flag(this, PoseFlag::kFromAcidDrop, 0);
    set_flag(this, PoseFlag::kRecoverInAir, 0);
    flip_spin_started_ = 0;
    // Carried by what the skater rides on: the position, and the start of the lip (+0x258) - 0x00427A00 works it out.
    if (objects::RideTracker* tracker = get_tracker(this)) {
        if (!tracker->entity_.owner_object_ || !tracker->tick_frame(&entity_->at_point_)) {
            stop_riding(this);
        } else {
            // (0x00427A00 returns `where`; ours is void: the point is read from `where`)
            auto* contact = ride_;
            maths::Vec4 where;
            objects::GameEntity* object = object_of(this);
            contact->get_contact_point(&where);
            object->at_point_ = where;
            ride_->get_contact_point(&where);
            lip_pos_ = where;
            if (get_tracker(this)->turned_) {
                auto multiply = d3dx::matrix_multiply;
                maths::Mat4& matrix = entity_->basis_frame_;
                multiply(&matrix, &matrix, &get_tracker(this)->turn_);
                multiply(&display_matrix_, &display_matrix_, &get_tracker(this)->turn_);
            }
        }
    }
    // The lip's balance (the Perfect Rail cheat: its global flag, and no lean).
    if (balance_->trick_ == kLipKind) {
        auto career = [] { return modules::g_skate_module->career_; };
        if (career()->is_code_on(kPerfectRail)) {
            career()->set_global_flag(script::fetch_int_value(kPerfectRail));
            balance_->lip_.lean_ = 0.0f;
            balance_->lip_.lean_speed_ = 0.0f;
            reinterpret_cast<uint8_t&>(online::g_cheat_flag) = 1;
        }
        balance_->lip_.tick_frame();
    }
    if (!bit_mark(this, PoseFlag::kCrouched) && input_->pad_primary_.pos_x_.held_down_)
        set_flag(this, PoseFlag::kCrouched, 1);
    // Off the lip (0x004FFB10): the rail's TriggerScript, twice.
    if (jump_released()) {
        set_flag(this, PoseFlag::kJumped, 1);
        trigger_rail_script(kLipOffEvents);
        trigger_rail_script(kOffLipEvents);
    }
    auto* node = rail_node_;
    if (node && rail_manager_->node_array_) {
        script::Struct* script = level_nodes::fetch_vertex(node->label_, true);
        script->read_hash_value(kName, &rail_script_node_, 1);
        static_cast<objects::MoveExecutor*>(trick_)->pass_to_trick_objects(rail_script_node_);
    }
    lip_time_ = get_now();
}

// 0x0050C670
void SkaterPhysicsModule::resume_skating(bool on_ground, const objects::FootMovement* grind_from,
                                         const AcidDrop* acid_drop)
{
    clear_flags();
    if (on_ground) {
        change_state(this, kGround);
        old_state_ = kGround;
    } else {
        change_state(this, kAir);
        old_state_ = kAir;
        set_flag(this, PoseFlag::kNoSecondSpin, 1);
        if (static_cast<uint8_t>(walk_->off_vert_))
            set_flag(this, PoseFlag::kNoAcidDrop, 1);
    }
    state_changed_at_ = get_now();
    display_matrix_ = entity_->basis_frame_;
    // The ground normals from the up (+0x68, +0x78, +0x58), the position kept as the last.
    floor_normal_ = entity_->basis_frame_.row[1];
    old_normal_ = floor_normal_;
    drawn_normal_ = old_normal_;
    safe_pos_ = entity_->at_point_;
    entity_->old_pos_ = safe_pos_;
    // The jump / trick state cleared.
    floor_feeler_.hit_.face_flag40_ = 0;
    side_reach_ = 0.0f;
    jump_charge_ms_ = 0;
    wall_ride_start_ = 0;
    wallplant_end_ = 0;
    no_spin_ = 0;
    no_kick_ = 0;
    can_brake_ = 1;
    state_->lip_straight_up_ = 0; // (without its time)
    if (bits_as<uint32_t>(limits_time_) != 0xC0000000)
        limits_time_ = 0.0f;
    speed_override_ = 1.0f;
    wallplant_time_ = 0;
    last_push_off_ = 0;
    leap_time_ = 0;
    vert_wallplanted_ = 0;
    if (grind_from) {
        // Onto a rail: the old ride let go (its object kept at +0x6C), the rail's object ridden.
        objects::MovingSurfaceModule* ride = ride_;
        if (objects::RideTracker* tracker = ride->ride_) {
            ride->last_object_ = tracker->entity_.owner_object_;
            reinterpret_cast<objects::SharedRef<objects::SharedBase>*>(&tracker->entity_)->get_release();
            memory::Hub::get_pull()->dealloc(tracker);
            ride->ride_ = nullptr;
            ride->stop_time_ = get_now();
        }
        if (objects::GameEntity* object = grind_from->grind_object_)
            reinterpret_cast<walk::MovableContact*>(ride_)->start_riding(object);
        start_grind(&grind_from->grind_point_, grind_from->grind_node_, grind_from->grind_rails_, true, true);
        safe_pos_ = entity_->at_point_;
        entity_->old_pos_ = safe_pos_;
    } else if (acid_drop) {
        start_acid_drop(acid_drop);
    }
}

} // namespace skater
