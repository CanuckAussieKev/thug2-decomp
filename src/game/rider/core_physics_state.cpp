// The skater's core physics component: made, reset and destroyed, and the timers its update counts down.
//
// Float arithmetic follows the original's x87 code at 53-bit precision: intermediate results are doubles, rounded
// to float only where the original stores them.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <new>

#include "base/core/dbg.h"
#include "base/core/layout_32bit.h"
#include "engine/sys/stopwatch.h"
#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "base/maths/shape_tests.h"
#include "game/online/gamenet_manager.h"
#include "engine/render/coll_cache.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/model_component.h"
#include "engine/objects/entity.h"
#include "engine/objects/smtptr.h"
#include "game/rider/walk_hang.h"
#include "engine/script/cscript.h"
#include "engine/script/host.h"
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
#include "game/online/net_game_state.h"

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

// The original's layout: every field at its offset, then the component's size (the pool and the heaps report).
void SkaterPhysicsModule::check_layout()
{
    struct FieldAt {
        std::size_t now;
        std::size_t original;
    };
    constexpr std::array kLayout{
        FieldAt{.now = offsetof(SkaterPhysicsModule, ground_.display_matrix), .original = 0x18},
        FieldAt{.now = offsetof(SkaterPhysicsModule, ground_.drawn_normal), .original = 0x58},
        FieldAt{.now = offsetof(SkaterPhysicsModule, ground_.floor_normal), .original = 0x68},
        FieldAt{.now = offsetof(SkaterPhysicsModule, ground_.old_normal), .original = 0x78},
        FieldAt{.now = offsetof(SkaterPhysicsModule, ground_.normal_blend), .original = 0x88},
        FieldAt{.now = offsetof(SkaterPhysicsModule, frame_.length), .original = 0x8C},
        FieldAt{.now = offsetof(SkaterPhysicsModule, frame_.state_changed_at), .original = 0x90},
        FieldAt{.now = offsetof(SkaterPhysicsModule, frame_.old_state), .original = 0x94},
        FieldAt{.now = offsetof(SkaterPhysicsModule, frame_.safe_pos), .original = 0x98},
        FieldAt{.now = offsetof(SkaterPhysicsModule, feelers_.feeler), .original = 0xA8},
        FieldAt{.now = offsetof(SkaterPhysicsModule, feelers_.floor_feeler), .original = 0x158},
        FieldAt{.now = offsetof(SkaterPhysicsModule, feelers_.probe_start), .original = 0x208},
        FieldAt{.now = offsetof(SkaterPhysicsModule, feelers_.probe_end), .original = 0x218},
        FieldAt{.now = offsetof(SkaterPhysicsModule, feelers_.hit_skatable), .original = 0x228},
        FieldAt{.now = offsetof(SkaterPhysicsModule, feelers_.hit_vert), .original = 0x229},
        FieldAt{.now = offsetof(SkaterPhysicsModule, feelers_.hit_wall), .original = 0x22A},
        FieldAt{.now = offsetof(SkaterPhysicsModule, feelers_.pad183), .original = 0x22B},
        FieldAt{.now = offsetof(SkaterPhysicsModule, rail_.node), .original = 0x22C},
        FieldAt{.now = offsetof(SkaterPhysicsModule, rail_.manager), .original = 0x230},
        FieldAt{.now = offsetof(SkaterPhysicsModule, rail_.time), .original = 0x234},
        FieldAt{.now = offsetof(SkaterPhysicsModule, rail_.lip_time), .original = 0x238},
        FieldAt{.now = offsetof(SkaterPhysicsModule, rail_.wait), .original = 0x23C},
        FieldAt{.now = offsetof(SkaterPhysicsModule, rail_.script_node), .original = 0x240},
        FieldAt{.now = offsetof(SkaterPhysicsModule, rail_.last_script), .original = 0x244},
        FieldAt{.now = offsetof(SkaterPhysicsModule, on_ledge_), .original = 0x248},
        FieldAt{.now = offsetof(SkaterPhysicsModule, bad_ledge_), .original = 0x249},
        FieldAt{.now = offsetof(SkaterPhysicsModule, skitch_moving_), .original = 0x24A},
        FieldAt{.now = offsetof(SkaterPhysicsModule, pad24b_), .original = 0x24B},
        FieldAt{.now = offsetof(SkaterPhysicsModule, skitch_.point), .original = 0x24C},
        FieldAt{.now = offsetof(SkaterPhysicsModule, skitch_.direction), .original = 0x250},
        FieldAt{.now = offsetof(SkaterPhysicsModule, frame_flags_.landed), .original = 0x254},
        FieldAt{.now = offsetof(SkaterPhysicsModule, frame_flags_.began_in_drop), .original = 0x255},
        FieldAt{.now = offsetof(SkaterPhysicsModule, frame_flags_.began_in_lip), .original = 0x256},
        FieldAt{.now = offsetof(SkaterPhysicsModule, frame_flags_.pad3), .original = 0x257},
        FieldAt{.now = offsetof(SkaterPhysicsModule, lip_pos_), .original = 0x258},
        FieldAt{.now = offsetof(SkaterPhysicsModule, vert_.takeoff), .original = 0x268},
        FieldAt{.now = offsetof(SkaterPhysicsModule, vert_.wall_normal), .original = 0x278},
        FieldAt{.now = offsetof(SkaterPhysicsModule, vert_.facing), .original = 0x288},
        FieldAt{.now = offsetof(SkaterPhysicsModule, vert_.step), .original = 0x298},
        FieldAt{.now = offsetof(SkaterPhysicsModule, unknown29c_), .original = 0x29C},
        FieldAt{.now = offsetof(SkaterPhysicsModule, side_.normal_sum), .original = 0x2AC},
        FieldAt{.now = offsetof(SkaterPhysicsModule, side_.nearest), .original = 0x2BC},
        FieldAt{.now = offsetof(SkaterPhysicsModule, side_.reach), .original = 0x2C0},
        FieldAt{.now = offsetof(SkaterPhysicsModule, side_.push), .original = 0x2C4},
        FieldAt{.now = offsetof(SkaterPhysicsModule, side_.turn), .original = 0x2C8},
        FieldAt{.now = offsetof(SkaterPhysicsModule, unknown2cc_), .original = 0x2CC},
        FieldAt{.now = offsetof(SkaterPhysicsModule, transfer_blocked_), .original = 0x2D0},
        FieldAt{.now = offsetof(SkaterPhysicsModule, pad2d1_), .original = 0x2D1},
        FieldAt{.now = offsetof(SkaterPhysicsModule, nearest_wall_side_), .original = 0x2D4},
        FieldAt{.now = offsetof(SkaterPhysicsModule, powerslide_), .original = 0x2D8},
        FieldAt{.now = offsetof(SkaterPhysicsModule, auto_kick_), .original = 0x2D9},
        FieldAt{.now = offsetof(SkaterPhysicsModule, tap_spin_), .original = 0x2DA},
        FieldAt{.now = offsetof(SkaterPhysicsModule, model_rotating_), .original = 0x2DB},
        FieldAt{.now = offsetof(SkaterPhysicsModule, model_rotation_.flipped), .original = 0x2DC},
        FieldAt{.now = offsetof(SkaterPhysicsModule, model_rotation_.pad1), .original = 0x2DD},
        FieldAt{.now = offsetof(SkaterPhysicsModule, model_rotation_.frames_left), .original = 0x2E0},
        FieldAt{.now = offsetof(SkaterPhysicsModule, model_rotation_.flip_spin_started), .original = 0x2E4},
        FieldAt{.now = offsetof(SkaterPhysicsModule, model_rotation_.pad9), .original = 0x2E5},
        FieldAt{.now = offsetof(SkaterPhysicsModule, model_rotation_.slomo_speed), .original = 0x2E8},
        FieldAt{.now = offsetof(SkaterPhysicsModule, point_rail_spin_), .original = 0x2EC},
        FieldAt{.now = offsetof(SkaterPhysicsModule, on_vehicle_), .original = 0x2ED},
        FieldAt{.now = offsetof(SkaterPhysicsModule, pad2ee_), .original = 0x2EE},
        FieldAt{.now = offsetof(SkaterPhysicsModule, vehicle_), .original = 0x2F0},
        FieldAt{.now = offsetof(SkaterPhysicsModule, projectiles_.on), .original = 0x2F4},
        FieldAt{.now = offsetof(SkaterPhysicsModule, projectiles_.pad1), .original = 0x2F5},
        FieldAt{.now = offsetof(SkaterPhysicsModule, projectiles_.ammo), .original = 0x2F8},
        FieldAt{.now = offsetof(SkaterPhysicsModule, projectiles_.ammo_type), .original = 0x2FC},
        FieldAt{.now = offsetof(SkaterPhysicsModule, in_revert_), .original = 0x300},
        FieldAt{.now = offsetof(SkaterPhysicsModule, focus_on_), .original = 0x301},
        FieldAt{.now = offsetof(SkaterPhysicsModule, motoskateboard_), .original = 0x302},
        FieldAt{.now = offsetof(SkaterPhysicsModule, crouch_stats_), .original = 0x303},
        FieldAt{.now = offsetof(SkaterPhysicsModule, manual_turnaround_), .original = 0x304},
        FieldAt{.now = offsetof(SkaterPhysicsModule, pad305_), .original = 0x305},
        FieldAt{.now = offsetof(SkaterPhysicsModule, projectile_input_), .original = 0x308},
        FieldAt{.now = offsetof(SkaterPhysicsModule, queued_spin_), .original = 0x30C},
        FieldAt{.now = offsetof(SkaterPhysicsModule, no_kick_), .original = 0x310},
        FieldAt{.now = offsetof(SkaterPhysicsModule, can_brake_), .original = 0x311},
        FieldAt{.now = offsetof(SkaterPhysicsModule, pad312_), .original = 0x312},
        FieldAt{.now = offsetof(SkaterPhysicsModule, friction_.rolling), .original = 0x314},
        FieldAt{.now = offsetof(SkaterPhysicsModule, friction_.special_index), .original = 0x318},
        FieldAt{.now = offsetof(SkaterPhysicsModule, friction_.special_start), .original = 0x31C},
        FieldAt{.now = offsetof(SkaterPhysicsModule, friction_.special_time), .original = 0x320},
        FieldAt{.now = offsetof(SkaterPhysicsModule, friction_.braking), .original = 0x324},
        FieldAt{.now = offsetof(SkaterPhysicsModule, friction_.pad11), .original = 0x325},
        FieldAt{.now = offsetof(SkaterPhysicsModule, side_hit_times_), .original = 0x328},
        FieldAt{.now = offsetof(SkaterPhysicsModule, last_ground_.pos), .original = 0x338},
        FieldAt{.now = offsetof(SkaterPhysicsModule, last_ground_.heading), .original = 0x348},
        FieldAt{.now = offsetof(SkaterPhysicsModule, rail_backwards_), .original = 0x34C},
        FieldAt{.now = offsetof(SkaterPhysicsModule, pad34d_), .original = 0x34D},
        FieldAt{.now = offsetof(SkaterPhysicsModule, limits_.time), .original = 0x350},
        FieldAt{.now = offsetof(SkaterPhysicsModule, limits_.max), .original = 0x354},
        FieldAt{.now = offsetof(SkaterPhysicsModule, limits_.max_max), .original = 0x358},
        FieldAt{.now = offsetof(SkaterPhysicsModule, limits_.friction), .original = 0x35C},
        FieldAt{.now = offsetof(SkaterPhysicsModule, limits_.gravity), .original = 0x360},
        FieldAt{.now = offsetof(SkaterPhysicsModule, skitch_target_), .original = 0x364},
        FieldAt{.now = offsetof(SkaterPhysicsModule, air_flags_.no_spin), .original = 0x370},
        FieldAt{.now = offsetof(SkaterPhysicsModule, air_flags_.bail_on), .original = 0x371},
        FieldAt{.now = offsetof(SkaterPhysicsModule, air_flags_.came_down_off_spine), .original = 0x372},
        FieldAt{.now = offsetof(SkaterPhysicsModule, air_flags_.landed_from_vert), .original = 0x373},
        FieldAt{.now = offsetof(SkaterPhysicsModule, air_flags_.landed_from_air_vert), .original = 0x374},
        FieldAt{.now = offsetof(SkaterPhysicsModule, air_flags_.allow_lip_no_grind), .original = 0x375},
        FieldAt{.now = offsetof(SkaterPhysicsModule, air_flags_.pad6), .original = 0x376},
        FieldAt{.now = offsetof(SkaterPhysicsModule, jump_charge_ms_), .original = 0x378},
        FieldAt{.now = offsetof(SkaterPhysicsModule, drop_target_height_), .original = 0x37C},
        FieldAt{.now = offsetof(SkaterPhysicsModule, air_time_.start), .original = 0x380},
        FieldAt{.now = offsetof(SkaterPhysicsModule, air_time_.end), .original = 0x384},
        FieldAt{.now = offsetof(SkaterPhysicsModule, lock_velocity_), .original = 0x388},
        FieldAt{.now = offsetof(SkaterPhysicsModule, pad389_), .original = 0x389},
        FieldAt{.now = offsetof(SkaterPhysicsModule, wall_.normal), .original = 0x38C},
        FieldAt{.now = offsetof(SkaterPhysicsModule, wall_.ride_start), .original = 0x39C},
        FieldAt{.now = offsetof(SkaterPhysicsModule, wall_.plant_end), .original = 0x3A0},
        FieldAt{.now = offsetof(SkaterPhysicsModule, no_rail_tricks_), .original = 0x3A4},
        FieldAt{.now = offsetof(SkaterPhysicsModule, last_spin_backside_), .original = 0x3A5},
        FieldAt{.now = offsetof(SkaterPhysicsModule, pad3a6_), .original = 0x3A6},
        FieldAt{.now = offsetof(SkaterPhysicsModule, rail_adjust_), .original = 0x3A8},
        FieldAt{.now = offsetof(SkaterPhysicsModule, speed_override_), .original = 0x3AC},
        FieldAt{.now = offsetof(SkaterPhysicsModule, drop_.matrix), .original = 0x3B0},
        FieldAt{.now = offsetof(SkaterPhysicsModule, drop_.slerp), .original = 0x3F0},
        FieldAt{.now = offsetof(SkaterPhysicsModule, drop_.time), .original = 0x488},
        FieldAt{.now = offsetof(SkaterPhysicsModule, drop_.duration), .original = 0x48C},
        FieldAt{.now = offsetof(SkaterPhysicsModule, drop_.heading), .original = 0x490},
        FieldAt{.now = offsetof(SkaterPhysicsModule, drop_.last_matrix), .original = 0x4A0},
        FieldAt{.now = offsetof(SkaterPhysicsModule, wallplant_.time), .original = 0x4E0},
        FieldAt{.now = offsetof(SkaterPhysicsModule, wallplant_.last_push_off), .original = 0x4E4},
        FieldAt{.now = offsetof(SkaterPhysicsModule, wallplant_.vert_planted), .original = 0x4E8},
        FieldAt{.now = offsetof(SkaterPhysicsModule, wallplant_.pad9), .original = 0x4E9},
        FieldAt{.now = offsetof(SkaterPhysicsModule, leap_time_), .original = 0x4EC},
        FieldAt{.now = offsetof(SkaterPhysicsModule, vert_air_announced_), .original = 0x4F0},
        FieldAt{.now = offsetof(SkaterPhysicsModule, pad4f1_), .original = 0x4F1},
        FieldAt{.now = offsetof(SkaterPhysicsModule, hit_cache_), .original = 0x4F4},
        FieldAt{.now = offsetof(SkaterPhysicsModule, drop_object_hit_), .original = 0x14C0},
        FieldAt{.now = offsetof(SkaterPhysicsModule, focus_held_), .original = 0x14C1},
        FieldAt{.now = offsetof(SkaterPhysicsModule, pad14c2_), .original = 0x14C2},
        FieldAt{.now = offsetof(SkaterPhysicsModule, throwing_.wait), .original = 0x14C4},
        FieldAt{.now = offsetof(SkaterPhysicsModule, throwing_.on), .original = 0x14C8},
        FieldAt{.now = offsetof(SkaterPhysicsModule, throwing_.pad5), .original = 0x14C9},
        FieldAt{.now = offsetof(SkaterPhysicsModule, late_wallplant_.frames), .original = 0x14CC},
        FieldAt{.now = offsetof(SkaterPhysicsModule, late_wallplant_.velocity), .original = 0x14D0},
        FieldAt{.now = offsetof(SkaterPhysicsModule, late_wallplant_.pos), .original = 0x14E0},
        FieldAt{.now = offsetof(SkaterPhysicsModule, late_wallplant_.wall_normal), .original = 0x14F0},
        FieldAt{.now = offsetof(SkaterPhysicsModule, natas_.counter_clockwise), .original = 0x1500},
        FieldAt{.now = offsetof(SkaterPhysicsModule, natas_.pad1), .original = 0x1501},
        FieldAt{.now = offsetof(SkaterPhysicsModule, natas_.time), .original = 0x1504},
        FieldAt{.now = offsetof(SkaterPhysicsModule, tantrum_), .original = 0x1508},
        FieldAt{.now = offsetof(SkaterPhysicsModule, tagging_), .original = 0x1509},
        FieldAt{.now = offsetof(SkaterPhysicsModule, pad150a_), .original = 0x150A},
        FieldAt{.now = offsetof(SkaterPhysicsModule, input_), .original = 0x150C},
        FieldAt{.now = offsetof(SkaterPhysicsModule, trigger_), .original = 0x1510},
        FieldAt{.now = offsetof(SkaterPhysicsModule, sound_), .original = 0x1514},
        FieldAt{.now = offsetof(SkaterPhysicsModule, trick_), .original = 0x1518},
        FieldAt{.now = offsetof(SkaterPhysicsModule, rotate_), .original = 0x151C},
        FieldAt{.now = offsetof(SkaterPhysicsModule, score_), .original = 0x1520},
        FieldAt{.now = offsetof(SkaterPhysicsModule, balance_), .original = 0x1524},
        FieldAt{.now = offsetof(SkaterPhysicsModule, state_), .original = 0x1528},
        FieldAt{.now = offsetof(SkaterPhysicsModule, ride_), .original = 0x152C},
        FieldAt{.now = offsetof(SkaterPhysicsModule, physics_control_), .original = 0x1530},
        FieldAt{.now = offsetof(SkaterPhysicsModule, walk_), .original = 0x1534},
        FieldAt{.now = offsetof(SkaterPhysicsModule, tagging_component_), .original = 0x1538},
        FieldAt{.now = sizeof(SkaterPhysicsModule), .original = 0x153C},
    };
    static_assert(core::layout_32bit(std::ranges::all_of(kLayout, [](const FieldAt& field) { return field.now == field.original; })),
                  "layout must match the original");
}

// 0x00506BE0
void SkaterPhysicsModule::get_reset()
{
    auto* game_net = online::net_game_state().online_hub();
    if (!game_net->in_net_game()) {
        // (Off line: the old position, and the one kept at +0x98, are where the skater is now.)
        entity()->old_pos() = entity()->at_point();
        frame_.safe_pos = entity()->at_point();
    }
    lip_pos_ = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    frame_.state_changed_at = get_now();
    entity()->vel() = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    physics::change_phase(this, 0);
    frame_.old_state = 0;
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
    set_timed_flag(this, PoseFlag::kFlipped,
                   static_cast<uint8_t>(!core::down_cast<objects::Rider>(entity())->traits().goofy()));
    // The ride let go of: its object kept at +0x6C, the smart pointer freed, the time noted.
    objects::MovingSurfaceModule* ride = ride_;
    if (objects::RideTracker* rider = ride->ride().get()) {
        ride->set_last_object(rider->entity().get_pull());
        rider->entity().get_release();
        memory::Hub::dealloc(rider);
        ride->ride() = nullptr;
        ride->set_stop_time(get_now());
    }
    feelers_.floor_feeler.hit().face_flag40_ = 0;
    side_.reach = 0.0F;
    jump_charge_ms_ = 0;
    wall_.ride_start = 0;
    wall_.plant_end = 0;
    air_flags_.no_spin = 0;
    air_flags_.bail_on = 0;
    no_kick_ = 0;
    can_brake_ = 1;
    state_->set_lip_straight_up(0);
    rail_.node = nullptr;
    if (limits_.time != -kTimerForLevel) { // (-2: kept)
        limits_.time = 0.0F;
    }
    wallplant_.time = 0;
    wallplant_.last_push_off = 0;
    leap_time_ = 0;
    friction_.special_time = 0.0F;
    wallplant_.vert_planted = 0;
    speed_override_ = 1.0F;
}

// 0x00506BA0
void SkaterPhysicsModule::remember_floor_probe(const void* feeler)
{
    feelers_.floor_feeler.get_assign(*static_cast<const render::ProbeRay*>(feeler));
    sound_->set_last_ground(feelers_.floor_feeler.hit().terrain);
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
void SkaterPhysicsModule::Friction::update_free_revert()
{
    if (special_index == 0) {
        return;
    }
    auto start = static_cast<uint32_t>(special_start);
    const uint32_t wait =
        ftol(static_cast<double>(script::fetch_float(kTimeBeforeFreeRevert)) * kMillisecondsPerSecond);
    if (get_since(start) <= wait) {
        return;
    }
    --special_index;
    debug::printf(kMessageFormat, kSourceFile, kFreeRevertLine, kFreeRevertText);
    if (special_index != 0) {
        special_start = get_now();
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
    if ((throwing_.on != 0U) || (projectiles_.on == 0U) || (state_->flag(PoseFlag::kBailing).on != 0U)) {
        return;
    }
    const objects::TaggingControlComponent* other = tagging_component_;
    if (other->pending() || (other->countdown() != 0) || !(other->cooldown() == 0.0F)) {
        return;
    }
    int& wait = throwing_.wait;
    if (wait != 0) {
        wait -= frame_clock::grid_frames(); // (not original: 1/60 s frames - the 60 Hz grid; --wait a tick at 60)
        return;
    }
    if (projectiles_.ammo <= 0) {
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
    // (not original: frames of 1/60 s - the 60 Hz grid, frame_clock::grid_frames; at 60 the original's one a tick)
    const double left = static_cast<double>(model_rotation_.frames_left) -
                        (model_rotation_.slomo_speed * static_cast<double>(frame_clock::grid_frames()));
    model_rotation_.frames_left = static_cast<float>(left); // (compared below unrounded)
    Expects(model != nullptr);
    auto reset_rotations = [model] { model->cancel_rotate_display(); };
    if (state_->cur_phase() != 1) {
        reset_rotations();
        model_rotating_ = 0;
        no_rail_tricks_ = 0;
        model_rotation_.frames_left = 0.0F;
        model_rotation_.flip_spin_started = 0;
        return;
    }
    if (left <= kModelRotationEnd) {
        no_rail_tricks_ = 0;
    }
    if (left <= 0.0) {
        model_rotating_ = 0;
        model_rotation_.frames_left = 0.0F;
        if (model_rotation_.flipped != 0U) {
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
        score->set_focus_time_limit(score->focus_time_limit() - frame_.length);
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
    ground_.drawn_normal = core::at(entity()->basis_frame().row, 1);
    ground_.floor_normal = core::at(entity()->basis_frame().row, 1);
    turn_to_normal(feelers_.feeler.surface_dir());
    return 1;
}

// 0x005004B0
void SkaterPhysicsModule::start_grind_trick(uint32_t rail_type, bool trick1, bool trick2, bool trick3, bool trick4)
{
    objects::GameEntity* object = object_of(this);
    if (!object->own_script()) {
        object_of(this)->set_own_script(script::host().create());
    }
    if (motoskateboard_ != 0U) {
        object = object_of(this);
        script::host().bind_script(object->own_script(), kMotoskateboardGrind, nullptr, object);
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
        script::host().bind_script(object->own_script(), tricks->fetch_part(buttons), nullptr, object);
    }
    script::host().tick_frame(object_of(this)->own_script());
    state_->set_in_trick_move(1);
}

} // namespace skater

