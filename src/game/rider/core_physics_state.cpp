// The skater's core physics component: made, reset and destroyed, and the timers its update counts down.
//
// Float arithmetic follows the original's x87 code at 53-bit precision: intermediate results are doubles, rounded
// to float only where the original stores them.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/maths/shape_tests.h"
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
#include "game/rider/thaw_bmx.h"
#include "game/rider/physics_control.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/objects/node_components.h"
#include "game/rider/core_physics.h"
#include "game/rider/skater_sound.h"
#include "engine/objects/movable_contact.h"
#include "game/rider/skater_state.h"
#include "game/rider/tagging.h"
#include "engine/core/owner.h"

namespace skater {

using namespace physics;

namespace {

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
const char* const kMessageFormat = "M:%s:%i: %s\n";                         // "M:%s:%i: %s\n"
const char* const kSourceFile = "mponents\\SkaterCorePhysicsComponent.cpp"; // (the component's source file)
const char* const kFreeRevertText = "You earned a free revert!!!!";         // "You earned a free revert!!!!"

// The fields used here.

// The game's constants.
constexpr float kMillisecondsPerSecond = 1000.0f; // 0x006453C0
constexpr float kModelRotationEnd = 20.0f;        // 0x006486AC
constexpr float kNeverLands = 0.00001f;           // 0x006453B8 (as a float: 0x3727C5AC)
constexpr float kCacheReach = 180.0f;             // 0x0064B278: the collision cache's box, along x and z
constexpr float kCacheBelow = 204.0f;             // 0x0064BCD0
constexpr float kCacheAbove = 96.0f;              // 0x0064B808

objects::GameEntity* skater_object(SkaterPhysicsModule* component)
{
    return static_cast<objects::GameEntity*>(object_of(component));
}

} // namespace

// 0x00505440
memory::owner<void*> SkaterPhysicsModule::fabricate() { return new SkaterPhysicsModule; }

// 0x00500CA0
SkaterPhysicsModule::SkaterPhysicsModule()
{
    // (the feelers made first by their constructors; the cache, below, by its own - in declaration order)
    skitch_target_.preceding_ = nullptr;
    skitch_target_.following_ = nullptr;
    skitch_target_.owner_object_ = nullptr;
    // (+0x3F0: its constructor 0x005902E0 only returns it.)
    input_ = nullptr;
    trigger_ = nullptr;
    sound_ = nullptr;
    trick_ = nullptr;
    rotate_ = nullptr;
    score_ = nullptr;
    balance_ = nullptr;
    state_ = nullptr;
    ride_ = nullptr;
    physics_control_ = nullptr;
    walk_ = nullptr;
    tagging_component_ = nullptr;
    focus_on_ = 0;
    powerslide_ = 0;
    flip_spin_started_ = 0;
    on_vehicle_ = 0;
    manual_turnaround_ = 0;
    kind_tag_ = kSkaterCorePhysics;
    projectile_input_ = -1;
    slomo_speed_ = 1.0f;
    // Not original - a component made where an old one was starts off THAW's bike (game/rider/thaw_bmx.h).
    if (thaw_bmx::is_switched_on())
        thaw_bmx::get_forget(this);
}

// 0x00506BE0
void SkaterPhysicsModule::get_reset()
{
    auto* game_net = online::g_online_hub;
    if (!game_net->in_net_game()) {
        // (Off line: the old position, and the one kept at +0x98, are where the skater is now.)
        entity_->old_pos_ = entity_->at_point_;
        safe_pos_ = entity_->at_point_;
    }
    lip_pos_ = {0.0f, 0.0f, 0.0f, 1.0f};
    state_changed_at_ = get_now();
    entity_->vel_ = {0.0f, 0.0f, 0.0f, 1.0f};
    physics::change_phase(this, 0);
    old_state_ = 0;
    // Every timed flag (23 down to 0) set, then cleared; flag 7 tells +0x1514's +0x1E each time.
    for (int flag = 0x17; flag >= 0; --flag) {
        set_timed_flag(this, static_cast<PoseFlag>(flag), 1);
        if (flag == 7)
            sound_->sliding_ = 1;
        set_timed_flag(this, static_cast<PoseFlag>(flag), 0);
        if (flag == 7)
            sound_->sliding_ = 0;
    }
    set_timed_flag(this, PoseFlag::kFlipped, !static_cast<objects::Rider*>(entity_)->goofy_);
    // The ride let go of: its object kept at +0x6C, the smart pointer freed, the time noted.
    objects::MovingSurfaceModule* ride = ride_;
    auto* rider = reinterpret_cast<objects::SharedRef<objects::SharedBase>*>(ride->ride_);
    if (rider) {
        ride->last_object_ = reinterpret_cast<objects::GameEntity*>(rider->owner_object_);
        rider->get_release();
        memory::Hub::get_pull()->dealloc(rider);
        ride->ride_ = nullptr;
        ride->stop_time_ = get_now();
    }
    floor_feeler_.hit_.face_flag40_ = 0;
    side_reach_ = 0.0f;
    jump_charge_ms_ = 0;
    wall_ride_start_ = 0;
    wallplant_end_ = 0;
    no_spin_ = 0;
    bail_on_ = 0;
    no_kick_ = 0;
    can_brake_ = 1;
    state_->lip_straight_up_ = 0;
    rail_node_ = nullptr;
    if (bits_as<uint32_t>(limits_time_) != 0xC0000000u) // (-2: kept)
        limits_time_ = 0.0f;
    wallplant_time_ = 0;
    last_push_off_ = 0;
    leap_time_ = 0;
    special_friction_time_ = 0.0f;
    vert_wallplanted_ = 0;
    speed_override_ = 1.0f;
}

// 0x00506BA0
void SkaterPhysicsModule::remember_floor_probe(const void* feeler)
{
    floor_feeler_.get_assign(*static_cast<const render::ProbeRay*>(feeler));
    sound_->last_ground_ = floor_feeler_.hit_.terrain;
}

// 0x00506BD0
int SkaterPhysicsModule::cancel_rotate_displayand_update_matrix(script::Struct*, script::Script*)
{
    return apply_model_rotations() ? 1 : 0;
}

// 0x00500670
void SkaterPhysicsModule::refresh_hit_cache()
{
    const maths::Vec4& position = entity_->at_point_;
    maths::BBox box;
    box.min = {position.x - kCacheReach, position.y - kCacheBelow, position.z - kCacheReach, position.w};
    box.max = {position.x + kCacheReach, position.y + kCacheAbove, position.z + kCacheReach, position.w};
    render::HitMeshCache& cache = hit_cache_;
    cache.tick_frame(box);
    render::g_current_cache = &cache; // the feelers' current cache
}

// 0x00500710
void SkaterPhysicsModule::update_free_revert()
{
    if (!special_friction_index_)
        return;
    auto start = static_cast<uint32_t>(special_friction_start_);
    uint32_t wait = ftol(static_cast<double>(script::fetch_float(kTimeBeforeFreeRevert)) * kMillisecondsPerSecond);
    if (get_since(start) <= wait)
        return;
    --special_friction_index_;
    debug::printf(kMessageFormat, kSourceFile, 0x293F, kFreeRevertText);
    if (special_friction_index_)
        special_friction_start_ = get_now();
}

// 0x005007A0
void SkaterPhysicsModule::update_focus_button()
{
    objects::ControlButton* button = &input_->pad_primary_.stick_click_left_;
    if (focus_held_) {
        if (!button->held_down_)
            focus_held_ = 0;
        return;
    }
    if (!button->held_down_)
        return;
    int wait = script::fetch_int_value(kFocusButtonTime);
    if (time_since_press(button) > wait) {
        focus_held_ = 1;
        script::launch_script(kToggleBulletTime, nullptr, nullptr, false, nullptr);
    }
}

// 0x00500820
void SkaterPhysicsModule::update_projectiles()
{
    if (throwing_projectile_ || !projectiles_on_ || state_->flag(PoseFlag::kBailing).on)
        return;
    objects::TaggingControlComponent* other = tagging_component_;
    if (other->pending_ || other->countdown_ || !(other->cooldown_ == 0.0f))
        return;
    int& wait = throw_wait_;
    if (wait) {
        --wait;
        return;
    }
    if (ammo_ <= 0)
        return;
    if (!projectile_button_pressed() || wait)
        return;
    wait = 30;
    // (0x004FC420: walking.)
    bool walking = static_cast<objects::RiderBoardPhysics*>(physics_control_)->is_on_foot();
    script::launch_script(walking ? kWalkThrow : kSkateThrow, nullptr, skater_object(this), false, nullptr);
}

// 0x00500900
void SkaterPhysicsModule::update_model_rotation()
{
    if (!model_rotating_)
        return;
    auto* model = static_cast<objects::MeshInstanceModule*>(skater_object(this)->fetch_module(kModel));
    double left = static_cast<double>(model_rotation_left_) - slomo_speed_;
    model_rotation_left_ = static_cast<float>(left); // (compared below unrounded)
    auto reset_rotations = [model] { model->cancel_rotate_display(); };
    if (state_->cur_phase_ != 1) {
        reset_rotations();
        model_rotating_ = 0;
        no_rail_tricks_ = 0;
        model_rotation_left_ = 0.0f;
        flip_spin_started_ = 0;
        return;
    }
    if (left <= kModelRotationEnd)
        no_rail_tricks_ = 0;
    if (left <= 0.0) {
        model_rotating_ = 0;
        model_rotation_left_ = 0.0f;
        if (model_flipped_) {
            static_cast<objects::RiderSpinAndFlip*>(skater_object(this)->fetch_module(kSkaterFlipAndRotate))
                ->get_flip();
        }
        reset_rotations();
    }
}

// 0x00500BC0
void SkaterPhysicsModule::update_special_time()
{
    if (!focus_on_)
        return;
    auto* time = static_cast<float*>(static_cast<objects::Rider*>(object_of(this))->get_points_total()); // (the score)
    if (!time)
        return;
    if (*time > 0.0f) {
        *time = *time - frame_length_;
        return;
    }
    script::Struct* params = script::spawn_param_bag();
    params = params ? new (params) script::Struct : nullptr;
    params->put_name_key(kTimeLimit, 1);
    script::launch_script(kKillSpecial, params, skater_object(this), false, nullptr);
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
}

// 0x00500DE0
int SkaterPhysicsModule::get_air_time_left(script::Struct*, script::Script* script)
{
    objects::GameEntity* object = object_of(this);
    float y = object->at_point_.y;
    auto drop = static_cast<float>(y - (static_cast<double>(y) - state_->height_above_ground_));
    float upward = object->vel_.y;
    double down = -current_gravity(this);
    // Solving y + upward * t - down * t^2 / 2 = the take-off height.
    double discriminant = static_cast<double>(upward) * upward + 2.0 * (down * drop);
    float left = 0.0f;
    if (discriminant >= 0.0 || discriminant != discriminant) {
        double t = (std::sqrt(discriminant) + upward) / down;
        left = t < 0.0 ? 0.0f : static_cast<float>(t);
    } else {
        left = kNeverLands;
    }
    script->args_->push_float(kAirTimeLeft, left);
    return 1;
}

// 0x00500E80
int SkaterPhysicsModule::can_kick(script::Struct*, script::Script*) { return can_push_off() ? 1 : 0; }

// 0x00500E90
int SkaterPhysicsModule::orient_to_normal(script::Struct*, script::Script*)
{
    drawn_normal_ = entity_->basis_frame_.row[1];
    floor_normal_ = entity_->basis_frame_.row[1];
    turn_to_normal(feeler_.surface_dir_);
    return 1;
}

// 0x005004B0
void SkaterPhysicsModule::start_grind_trick(uint32_t rail_type, bool trick1, bool trick2, bool trick3, bool trick4)
{
    objects::GameEntity* object = skater_object(this);
    if (!object->own_script_) {
        skater_object(this)->own_script_ = script::Script::create();
    }
    if (motoskateboard_) {
        object = skater_object(this);
        object->own_script_->bind_script(kMotoskateboardGrind, nullptr, object);
    } else {
        // GrindTrickList[the rail's type (0-8; anything else 0)][the four trick buttons as bits 0-3].
        uint32_t index = rail_type <= 8 ? rail_type : 0;
        auto* tricks = reinterpret_cast<script::Array*>(script::fetch_list(kGrindTrickList)->fetch_part(index));
        uint32_t buttons = trick1 ? 1 : 0;
        if (trick2)
            buttons |= 2;
        if (trick3)
            buttons |= 4;
        if (trick4)
            buttons |= 8;
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
        static_cast<objects::RiderSpinAndFlip*>(skater_object(this)->fetch_module(kSkaterFlipAndRotate))
            ->do_after_anim();
        object = skater_object(this);
        object->own_script_->bind_script(tricks->fetch_part(buttons), nullptr, object);
    }
    skater_object(this)->own_script_->tick_frame();
    state_->in_trick_move_ = 1;
}

} // namespace skater

namespace skater {
namespace {} // namespace
} // namespace skater

namespace skater {} // namespace skater
