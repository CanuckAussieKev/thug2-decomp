// The skater's core physics component: made, reset and destroyed, and the timers its update counts down.
//
// Float arithmetic follows the original's x87 code at 53-bit precision: intermediate results are doubles, rounded
// to float only where the original stores them.

#include <cmath>
#include <cstdint>
#include <new>

#include "base/core/dbg.h"
#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "base/maths/shape_tests.h"
#include "engine/net/gamenet_manager.h"
#include "engine/render/coll_cache.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/model_component.h"
#include "engine/objects/entity.h"
#include "engine/objects/smtptr.h"
#include "engine/objects/walk_hang.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/rider/core_physics.h"
#include "game/rider/core_physics_fields.h"
#include "game/rider/flip_and_rotate.h"
#include "game/rider/rider.h"
#include "game/skate/point_tally.h"
#include "game/rider/thaw_bmx.h"
#include "game/rider/physics_control.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/objects/node_components.h"
#include "game/rider/skater_sound.h"
#include "engine/objects/movable_contact.h"
#include "game/rider/skater_state.h"
#include "game/rider/tagging.h"
#include <gsl/pointers>
#include <gsl/assert>
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "engine/objects/ride_tracker.h"
#include "engine/net/net_game_state.h"

namespace skater {

using namespace physics;

namespace {
constexpr int kLastPoseFlag = 0x17;
constexpr int kSlideFlag = static_cast<int>(PoseFlag::kSlide);
constexpr int kFreeRevertLine = 0x293F;             // the source line the original reports a free revert from
constexpr int kProjectileWaitFrames = 30;
constexpr uint32_t kTrickButton4Bit = 8;

constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29;    // SkaterCorePhysics (the component type)
constexpr uint32_t kModel = 0x286A8D26;                // Model (a component)
constexpr uint32_t kSkaterFlipAndRotate = 0x488AF07A;  // SkaterFlipAndRotate (a component)
constexpr uint32_t kTimeBeforeFreeRevert = 0xF4813AD5; // Physics_Time_Before_Free_Revert
constexpr uint32_t kFocusButtonTime = 0xD7E8D8DE;      // focus_button_pressed_time
constexpr uint32_t kToggleBulletTime = 0xD7196D01;     // toggle_bullettime (a script)
constexpr uint32_t kWalkThrow = 0x0CE5B783;            // walk_maybe_throw_projectile (a script)
constexpr uint32_t kSkateThrow = 0x3F356C63;           // skate_maybe_throw_projectile (a script)
constexpr uint32_t kTimeLimit = 0x70F16EA9;            // TimeLimit
constexpr uint32_t kKillSpecial = 0x266B570C;          // KillSpecial (a script)
constexpr uint32_t kAirTimeLeft = 0x7E2A8993;          // AirTimeLeft
constexpr uint32_t kGrindTrickList = 0x2AB3341D;       // GrindTrickList
constexpr uint32_t kMotoskateboardGrind = 0xF7CA3F78;  // Trick_Motoskateboard_Grind (a script)

// The free revert's message: the game's own format, file name and text.
constexpr const char* kMessageFormat = "M:%s:%i: %s\n";                     // "M:%s:%i: %s\n"
const char* const kSourceFile = "mponents\\SkaterCorePhysicsComponent.cpp"; // (the component's source file)
const char* const kFreeRevertText = "You earned a free revert!!!!";         // "You earned a free revert!!!!"

// The fields used here.

// The game's constants.
constexpr float kMillisecondsPerSecond = 1000.0F; // 0x006453C0
constexpr float kModelRotationEnd = 20.0F;        // 0x006486AC
constexpr float kNeverLands = 0.00001F;           // 0x006453B8 (as a float: 0x3727C5AC)
constexpr float kCacheReach = 180.0F;             // 0x0064B278: the collision cache's box, along x and z
constexpr float kCacheBelow = 204.0F;             // 0x0064BCD0
constexpr float kCacheAbove = 96.0F;              // 0x0064B808

} // namespace

// 0x00505440
gsl::owner<void*> SkaterPhysicsModule::fabricate() { return new SkaterPhysicsModule; }

// 0x00500CA0
SkaterPhysicsModule::SkaterPhysicsModule()
{
    // (the feelers made first by their constructors; the cache, below, by its own - in declaration order)
    skitch_target_.preceding_ = nullptr;
    skitch_target_.following_ = nullptr;
    skitch_target_.target_ = nullptr;
    // (+0x3F0: its constructor 0x005902E0 only returns it.)
    set_kind_tag(kSkaterCorePhysics);
    // Not original - a component made where an old one was starts off THAW's bike (game/rider/thaw_bmx.h).
    if (thaw_bmx::is_switched_on()) {
        thaw_bmx::get_forget(this);
    }
}

// 0x00506BE0
void SkaterPhysicsModule::get_reset()
{
    auto* game_net = online::net_game_state().online_hub();
    if (!game_net->in_net_game()) {
        // (Off line: the old position, and the one kept at +0x98, are where the skater is now.)
        entity()->old_pos() = entity()->at_point();
        safe_pos_ = entity()->at_point();
    }
    lip_pos_ = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    state_changed_at_ = get_now();
    entity()->vel() = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    physics::change_phase(this, 0);
    old_state_ = 0;
    // Every timed flag (23 down to 0) set, then cleared; flag 7 tells +0x1514's +0x1E each time.
    for (int flag = kLastPoseFlag; flag >= 0; --flag) {
        set_timed_flag(this, static_cast<PoseFlag>(flag), 1);
        if (flag == kSlideFlag) {
            sound_->set_sliding(1);
        }
        set_timed_flag(this, static_cast<PoseFlag>(flag), 0);
        if (flag == kSlideFlag) {
            sound_->set_sliding(0);
        }
    }
    set_timed_flag(this, PoseFlag::kFlipped, static_cast<uint8_t>(!core::down_cast<objects::Rider>(entity())->goofy()));
    // The ride let go of: its object kept at +0x6C, the smart pointer freed, the time noted.
    objects::MovingSurfaceModule* ride = ride_;
    if (objects::RideTracker* rider = ride->ride().get()) {
        ride->set_last_object(rider->entity().get_pull());
        rider->entity().get_release();
        memory::Hub::dealloc(rider);
        ride->ride() = nullptr;
        ride->set_stop_time(get_now());
    }
    floor_feeler_.hit().face_flag40_ = 0;
    side_reach_ = 0.0F;
    jump_charge_ms_ = 0;
    wall_ride_start_ = 0;
    wallplant_end_ = 0;
    no_spin_ = 0;
    bail_on_ = 0;
    no_kick_ = 0;
    can_brake_ = 1;
    state_->set_lip_straight_up(0);
    rail_node_ = nullptr;
    if (limits_time_ != -kTimerForLevel) { // (-2: kept)
        limits_time_ = 0.0F;
    }
    wallplant_time_ = 0;
    last_push_off_ = 0;
    leap_time_ = 0;
    special_friction_time_ = 0.0F;
    vert_wallplanted_ = 0;
    speed_override_ = 1.0F;
}

// 0x00506BA0
void SkaterPhysicsModule::remember_floor_probe(const void* feeler)
{
    floor_feeler_.get_assign(*static_cast<const render::ProbeRay*>(feeler));
    sound_->set_last_ground(floor_feeler_.hit().terrain);
}

// 0x00506BD0
int SkaterPhysicsModule::cancel_rotate_displayand_update_matrix(script::Struct* /*params*/, script::Script* /*script*/)
{
    return apply_model_rotations() ? 1 : 0;
}

// 0x00500670
void SkaterPhysicsModule::refresh_hit_cache()
{
    const maths::Vec4& position = entity()->at_point();
    maths::BBox box;
    box.min = {.x = position.x - kCacheReach, .y = position.y - kCacheBelow, .z = position.z - kCacheReach, .w = position.w};
    box.max = {.x = position.x + kCacheReach, .y = position.y + kCacheAbove, .z = position.z + kCacheReach, .w = position.w};
    render::HitMeshCache& cache = hit_cache_;
    cache.tick_frame(box);
    render::set_current_cache(&cache); // the feelers' current cache
}

// 0x00500710
void SkaterPhysicsModule::update_free_revert()
{
    if (special_friction_index_ == 0) {
        return;
    }
    auto start = static_cast<uint32_t>(special_friction_start_);
    const uint32_t wait =
        ftol(static_cast<double>(script::fetch_float(kTimeBeforeFreeRevert)) * kMillisecondsPerSecond);
    if (get_since(start) <= wait) {
        return;
    }
    --special_friction_index_;
    debug::printf(kMessageFormat, kSourceFile, kFreeRevertLine, kFreeRevertText);
    if (special_friction_index_ != 0) {
        special_friction_start_ = get_now();
    }
}

// 0x005007A0
void SkaterPhysicsModule::update_focus_button()
{
    objects::ControlButton* button = &input_->pad_primary().stick_click_left();
    if (focus_held_ != 0U) {
        if (button->held_down() == 0U) {
            focus_held_ = 0;
        }
        return;
    }
    if (button->held_down() == 0U) {
        return;
    }
    const int wait = script::fetch_int_value(kFocusButtonTime);
    if (time_since_press(button) > wait) {
        focus_held_ = 1;
        script::launch_script(kToggleBulletTime, nullptr, nullptr, false, nullptr);
    }
}

// 0x00500820
void SkaterPhysicsModule::update_projectiles()
{
    if ((throwing_projectile_ != 0U) || (projectiles_on_ == 0U) || (state_->flag(PoseFlag::kBailing).on != 0U)) {
        return;
    }
    const objects::TaggingControlComponent* other = tagging_component_;
    if (other->pending() || (other->countdown() != 0) || !(other->cooldown() == 0.0F)) {
        return;
    }
    int& wait = throw_wait_;
    if (wait != 0) {
        --wait;
        return;
    }
    if (ammo_ <= 0) {
        return;
    }
    if (!projectile_button_pressed() || (wait != 0)) {
        return;
    }
    wait = kProjectileWaitFrames;
    // (0x004FC420: walking.)
    const bool walking = static_cast<objects::RiderBoardPhysics*>(physics_control_)->is_on_foot();
    script::launch_script(walking ? kWalkThrow : kSkateThrow, nullptr, object_of(this), false, nullptr);
}

// 0x00500900
void SkaterPhysicsModule::update_model_rotation()
{
    if (model_rotating_ == 0U) {
        return;
    }
    auto* model = core::down_cast<objects::MeshInstanceModule>(object_of(this)->fetch_module(kModel));
    const double left = static_cast<double>(model_rotation_left_) - slomo_speed_;
    model_rotation_left_ = static_cast<float>(left); // (compared below unrounded)
    Expects(model != nullptr);
    auto reset_rotations = [model] { model->cancel_rotate_display(); };
    if (state_->cur_phase() != 1) {
        reset_rotations();
        model_rotating_ = 0;
        no_rail_tricks_ = 0;
        model_rotation_left_ = 0.0F;
        flip_spin_started_ = 0;
        return;
    }
    if (left <= kModelRotationEnd) {
        no_rail_tricks_ = 0;
    }
    if (left <= 0.0) {
        model_rotating_ = 0;
        model_rotation_left_ = 0.0F;
        if (model_flipped_ != 0U) {
            core::down_cast<objects::RiderSpinAndFlip>(object_of(this)->fetch_module(kSkaterFlipAndRotate))
                ->get_flip();
        }
        reset_rotations();
    }
}

// 0x00500BC0
void SkaterPhysicsModule::update_special_time() const
{
    if (focus_on_ == 0U) {
        return;
    }
    modules::PointTally* score = core::down_cast<objects::Rider>(object_of(this))->get_points_total();
    if (!score) {
        return;
    }
    if (score->focus_time_limit() > 0.0F) {
        score->set_focus_time_limit(score->focus_time_limit() - frame_length_);
        return;
    }
    gsl::owner<script::Struct*> params = script::new_struct();
    params->put_name_key(checksums::Name{kTimeLimit}, 1);
    script::launch_script(kKillSpecial, params, object_of(this), false, nullptr);
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
}

// 0x00500DE0
int SkaterPhysicsModule::get_air_time_left(script::Struct* /*params*/, script::Script* script)
{
    const objects::GameEntity* object = object_of(this);
    const float y = object->at_point().y;
    auto drop = static_cast<float>(y - (static_cast<double>(y) - state_->height_above_ground()));
    const float upward = object->vel().y;
    const double down = -current_gravity(this);
    // Solving y + upward * t - down * t^2 / 2 = the take-off height.
    const double discriminant = (static_cast<double>(upward) * upward) + (2.0 * (down * drop));
    float left = 0.0F;
    if (discriminant >= 0.0 || discriminant != discriminant) {
        const double time_to_land = (std::sqrt(discriminant) + upward) / down;
        left = time_to_land < 0.0 ? 0.0F : static_cast<float>(time_to_land);
    } else {
        left = kNeverLands;
    }
    script->args()->push_float(checksums::Name{kAirTimeLeft}, left);
    return 1;
}

// 0x00500E80
int SkaterPhysicsModule::can_kick(script::Struct* /*params*/, script::Script* /*script*/)
{
    return can_push_off() ? 1 : 0;
}

// 0x00500E90
int SkaterPhysicsModule::orient_to_normal(script::Struct* /*params*/, script::Script* /*script*/)
{
    drawn_normal_ = core::at(entity()->basis_frame().row, 1);
    floor_normal_ = core::at(entity()->basis_frame().row, 1);
    turn_to_normal(feeler_.surface_dir());
    return 1;
}

// 0x005004B0
void SkaterPhysicsModule::start_grind_trick(uint32_t rail_type, bool trick1, bool trick2, bool trick3, bool trick4)
{
    objects::GameEntity* object = object_of(this);
    if (!object->own_script()) {
        object_of(this)->set_own_script(script::Script::create());
    }
    if (motoskateboard_ != 0U) {
        object = object_of(this);
        object->own_script()->bind_script(kMotoskateboardGrind, nullptr, object);
    } else {
        // GrindTrickList[the rail's type (0-8; anything else 0)][the four trick buttons as bits 0-3].
        const uint32_t index = rail_type <= 8 ? rail_type : 0;
        auto* tricks = script::fetch_list(kGrindTrickList)->array_at(index);
        uint32_t buttons = trick1 ? 1 : 0;
        if (trick2) {
            buttons |= 2;
        }
        if (trick3) {
            buttons |= 4;
        }
        if (trick4) {
            buttons |= kTrickButton4Bit;
        }
        if (thaw_bmx::live_now(this)) {
            // Not original: THAW 0x005CF2F0 - on the bike BikeGrindTrickList by the stick's pitch (when the overlay
            // has it), the over-grind state.
            uint32_t column = 0;
            if (script::Array* row = thaw_bmx::grind_trick_row(this, trick1, trick2, trick3, &column)) {
                tricks = row;
                buttons = column;
            }
        }
        rail_adjust_ = 0;
        core::down_cast<objects::RiderSpinAndFlip>(object_of(this)->fetch_module(kSkaterFlipAndRotate))
            ->do_after_anim();
        object = object_of(this);
        object->own_script()->bind_script(tricks->fetch_part(buttons), nullptr, object);
    }
    object_of(this)->own_script()->tick_frame();
    state_->set_in_trick_move(1);
}

} // namespace skater

