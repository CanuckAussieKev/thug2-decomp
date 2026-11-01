// The Walk component's update and moves (GameObjects::FootMovement, 0x00529F80-0x0052D6A0): see walk_physics.h. From the
// disassembly: the x87 arithmetic is followed where it keeps values unrounded (doubles here) and where it stores
// them (floats).

#include "game/rider/walk_physics.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/maths/shape_tests.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/net/gamenet_manager.h"
#include "engine/render/coll_cache.h"
#include "engine/objects/movable_contact.h"
#include "engine/render/coll_line.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/anim_blender.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/entity.h"
#include "engine/objects/walk_hang.h"
#include "engine/objects/ride_tracker.h"
#include "engine/objects/smtptr.h"
#include "engine/script/param_struct.h"
#include "platform/game_clock.h"
#include "platform/stopwatch.h"
#include "game/rider/controls.h"
#include "engine/objects/trigger_component.h"
#include "game/rider/core_physics.h"
#include "game/rider/skater_state.h"
#include "game/rider/walk_camera.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/objects/node_components.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/trigger_events.h"
#include "game/rider/physics_control.h"
#include "engine/objects/input_component.h"
#include "game/rider/tagging.h"

namespace objects {

namespace {

// The component's fields (offsets).

// Animation states.
constexpr uint32_t kAnimWait = 0x4FE6069C;       // ANIMWAIT
constexpr uint32_t kHangMoveRight = 0x279B1F0B;  // HangMoveRight
constexpr uint32_t kHangMoveLeft = 0x2D9815C3;   // HangMoveLeft
constexpr uint32_t kHang = 0x4194ECCA;           // HANG
constexpr uint32_t kLadderMoveUp = 0xAF5ABC82;   // LadderMoveUp
constexpr uint32_t kLadder = 0xC84243DA;         // Ladder
constexpr uint32_t kLadderMoveDown = 0xFEC9DDED; // LadderMoveDown
constexpr uint32_t kSkid = 0x1D537EFF;           // Skid
constexpr uint32_t kStand = 0x9B46E749;          // Stand
constexpr uint32_t kLand = 0x57FF2A27;           // Land
constexpr uint32_t kWalkOffEdge = 0xABF1F6AC;    // WalkOffEdge

// Names.
constexpr uint32_t kWalkParameters = 0x6775E538;        // walk_parameters
constexpr uint32_t kHangMoveAnimSpeed = 0x1A47FFAB;     // hang_move_animation_speed
constexpr uint32_t kLadderMoveSpeed = 0x0AB2DB54;       // ladder_move_speed
constexpr uint32_t kSpeedName = 0xF0D90109;             // speed
constexpr uint32_t kSetAnimSpeed = 0xBD4EDD44;          // (the animation component's member function given the speed)
constexpr uint32_t kAir = 0x439F4704;                   // AIR (an event; also the air's animation state)
constexpr uint32_t kPeggedDurationForSkid = 0x8E1CF27A; // pegged_duration_for_skid
constexpr uint32_t kCurbFloatLerpDownRate = 0x9B3388FA; // curb_float_lerp_down_rate
constexpr uint32_t kRehangDelay = 0xE6E0C0A4;           // rehang_delay (a skater physics value)
constexpr uint32_t kStickyLandThresholdSpeed = 0x530DCF34; // sticky_land_threshold_speed (the same)
constexpr uint32_t kSnapUpHeight = 0xCEE3A3E1;             // snap_up_height
constexpr uint32_t kSnapDownHeight = 0xAF3E4251;           // snap_down_height
constexpr uint32_t kMaxUnnoticedGroundSnap = 0xD3193D8E;   // max_unnoticed_ground_snap
constexpr uint32_t kInitialVertVelBoost = 0x17B37748;      // initial_vert_vel_boost
constexpr uint32_t kSteppedUp = 0x93FCF3ED;                // (events for a big step up / down; names not recovered)
constexpr uint32_t kSteppedDown = 0x56E21153;
constexpr uint32_t kSkaterOffEdge = 0xD96F01F1;             // SkaterOffEdge (an event)
constexpr uint32_t kHoldTimeForMaxJump = 0x12333EBD;        // hold_time_for_max_jump
constexpr uint32_t kMinJumpFactor = 0x246D0BF3;             // min_jump_factor
constexpr uint32_t kHangJumpFactor = 0xF2FA5845;            // hang_jump_factor
constexpr uint32_t kJumpVelocity = 0x63D62A21;              // jump_velocity
constexpr uint32_t kJumpHorizSpeed = 0x839FE542;            // jump_horiz_speed (a skater physics value)
constexpr uint32_t kBarrierJumpHighestBarrier = 0xDA85F5AE; // barrier_jump_highest_barrier
constexpr uint32_t kBarrierJumpMaxAngle = 0x78E6A5EC;       // barrier_jump_max_angle
constexpr uint32_t kBarrierJumpMinClearance = 0x72660978;   // barrier_jump_min_clearance
constexpr uint32_t kBarrierJumpDelay = 0x38D36700;          // barrier_jump_delay
constexpr uint32_t kFeelerLength = 0x99978D2B;              // feeler_length
constexpr uint32_t kProbeElevation = 0x6DA7F696;            // feeler_height
constexpr uint32_t kPushFeelerLength = 0xA20C43B7;          // push_feeler_length
constexpr uint32_t kPushStrength = 0x4D16F37D;              // push_strength
constexpr uint32_t kMinAnimRunSpeed = 0x0BD6A05D;           // min_anim_run_speed
constexpr uint32_t kSkaterJump = 0x8687163A;                // SkaterJump (an event)
constexpr uint32_t kJump = 0x584CF9E9;                      // Jump (the animation state)
constexpr uint32_t kJumpRequested = 0x6B9CA247;             // JumpRequested (an event handler type)

constexpr uint16_t kFaceWall = 0x2;                         // a face flag: not stood on
constexpr float kWallGap = 0.1f;                            // 0x00645D74
constexpr float kSlideGap = 0.05f;                          // 0x00645D6C
constexpr float kBigStepDown = -3.0f;                       // 0x0064C798
constexpr float kNudge = 0.001f;                            // 0x006461D8
constexpr double kMinHeadingLength = 0.0010000000474974513; // (0.001f)
constexpr double kSeventhTurn = 0.8975979089736938;         // 0x00646968 (2 pi / 7, as a float)
constexpr double kDegrees = 0.01745329238474369;            // 0x00645464 (pi / 180, as a float)
constexpr double kNinth = 0.1111111119389534;               // 0x0064C79C (1 / 9, as a float)
constexpr double kSteepGround = 0.5;                        // 0x006453BC
constexpr double kFloorBelow = 0.7070000171661377;          // 0x006467BC
constexpr double kMovingSqr = 0.009999999776482582;         // 0x0064544C (0.01f)
constexpr double kStillLength = 0.0001;                     // 0x006484A0
constexpr double kStillSqr = 1e-08;                         // 0x0064C7A0
constexpr double kMostlyStopped = 0.25;                     // 0x006467AC

constexpr float kCameraRate = 0.05f;            // 0x3D4CCCCD
constexpr double kHangTurn = 0.949999988079071; // 0x0064C790 (radians)
constexpr float kHangTurnBack = -0.95f;         // 0xBF733333

// walk_parameters' float `name` (asserted).
float walk_parameter(uint32_t name)
{
    float value;
    script::read_sub_struct(kWalkParameters)->fetch_float(name, &value, 1);
    return value;
}

// The game clock as the skater code reads it (0x0044ECE0 plus the offset at 0x00696D60).
int get_now() { return frame_clock::game_clock_elapsed() + g_clock_offset; }

// The walk halted (the trigger component's +0x26: what it does this frame is over).
bool halted(const FootMovement* walk) { return static_cast<bool>(walk->physics_control_->flag26_); }

// Nx's current collision cache (0x00691974), cleared after each state's update.
void clear_current_cache() { reinterpret_cast<void*&>(render::g_current_cache) = nullptr; }

// The ride component's moving object containing the segment from `from` to `to` (0x00427B30), or null.
GameEntity* moving_object_around(void* ride, maths::Vec4* to, maths::Vec4* from)
{
    return static_cast<MovingSurfaceModule*>(ride)->find_contact_ahead(to, from);
}

// The debug line the original prints when it finds itself inside a moving object.
void report_inside(int line, const char* where)
{
    debug::printf("M:%s:%i: %s\n", "mponents\\WalkComponent.cpp", line, where);
}

// A local feeler going away: the object hit let go of (the original's inlined smart pointer destructor; cleared, so
// the feeler's own destructor has nothing left to do).
void release_hit(render::ProbeRay* feeler) { feeler->entity_.get_release(); }

// `position` put `gap` off the feeler's hit along its normal (x, y and z's offsets rounded first, w's not).
void put_off(maths::Vec4* position, const render::ProbeRay* feeler, float gap)
{
    const maths::Vec4& point = feeler->contact_pos_;
    const maths::Vec4& normal = feeler->surface_dir_;
    double scale = gap;
    position->x = point.x + static_cast<float>(normal.x * scale);
    position->y = point.y + static_cast<float>(normal.y * scale);
    position->z = point.z + static_cast<float>(normal.z * scale);
    position->w = static_cast<float>(normal.w * scale + point.w);
}

// The skater component's state flags (its +0x1528).

// One of the eight contacts (+0xA0, 0xCC each): what feeler 0-6 (around it, a seventh of a turn apart from the facing)
// or 7 (along the fall) found.
using Contact = WalkContact;
Contact& contact_at(const FootMovement* walk, int index) { return const_cast<FootMovement*>(walk)->contacts_[index]; }

// Where it hangs along the hang's facing (+0x18B4): (the position plus the ride check's offset) . that facing.
float hang_plane(const FootMovement* walk)
{
    const maths::Vec4& position = walk->pos_;
    const maths::Vec4& offset = walk->critical_point_offset_;
    const maths::Vec4& facing = walk->push_normal_;
    double x = static_cast<double>(position.x) + offset.x;
    double y = static_cast<double>(position.y) + offset.y;
    double z = static_cast<double>(position.z) + offset.z;
    return static_cast<float>((z * facing.z + y * facing.y) + x * facing.x);
}

// Stopped by what it walks into: no velocity, the run animation's speed down to min_anim_run_speed, blocked.
void halt(FootMovement* walk)
{
    walk->vel_ = {0.0f, 0.0f, 0.0f, 1.0f};
    float minimum = walk_parameter(kMinAnimRunSpeed);
    float& speed = walk->speed_;
    speed = minimum < speed ? minimum : speed;
    walk->blocked_ = true;
}

// a + b in the x87's double precision, stored as a float.
float append_one(float a, float b) { return static_cast<float>(static_cast<double>(a) + b); }

// The walk camera's override (0x00525F70: change_forced_heading - the walk camera component, not rewritten here).
void set_camera_override(void* camera, const maths::Vec4* facing, float rate)
{
    static_cast<FootView*>(camera)->change_forced_heading(facing, rate);
}

} // namespace

// 0x0052A5C0
memory::owner<ObjectModule*> FootMovement::fabricate() { return new FootMovement; }

// 0x0052A190
FootMovement::FootMovement()
{
    // (the contacts, the cache and the feeler made first by their constructors)
    input_ = nullptr;
    anim_blender_ = nullptr;
    model_ = nullptr;
    trigger_ = nullptr;
    physics_control_ = nullptr;
    ride_ = nullptr;
    skater_state_ = nullptr;
    core_physics_ = nullptr;
    kind_tag_ = 0x726E85AA; // Walk
    stick_heading_ = {0.0f, 0.0f, 0.0f, 1.0f};
    jump_velocity_ = {0.0f, 0.0f, 0.0f, 1.0f};
    upright_time_ = 0.0f;
    jump_unsteered_ = false;
}

// 0x00529F80
void FootMovement::update_camera_override()
{
    FootView* camera = walk_camera_;
    const maths::Vec4& facing = facing_;
    switch (anim_state_) {
    case kAnimWait: {
        camera->override_ = true;
        // (while hang-moving, the hang move's facing)
        camera->forced_heading_ = anim_waiting_ ? transition_facing_ : facing;
        camera->override_rate_ = kCameraRate;
        return;
    }
    case kHangMoveRight: {
        // The facing turned 0.95 radians about y (sin / cos stored as floats, the sums rounded once).
        auto sine = static_cast<float>(std::sin(kHangTurn));
        auto cosine = static_cast<float>(std::cos(kHangTurn));
        camera->override_ = true;
        maths::Vec4& target = camera->forced_heading_;
        float x = facing.x;
        float z = facing.z;
        target.x = static_cast<float>(static_cast<double>(z) * sine + static_cast<double>(cosine) * x);
        target.y = facing.y;
        target.z = static_cast<float>(-(static_cast<double>(x) * sine) + static_cast<double>(z) * cosine);
        target.w = facing.w;
        camera->override_rate_ = kCameraRate;
        return;
    }
    case kHangMoveLeft: {
        maths::Vec4 turned = facing;
        turned.rotate_about_y(kHangTurnBack);
        set_camera_override(camera, &turned, kCameraRate);
        return;
    }
    case kHang:
        set_camera_override(camera, &facing, kCameraRate);
        return;
    case kLadderMoveUp:
    case kLadder:
    case kLadderMoveDown:
        camera->override_ = true;
        camera->forced_heading_ = facing;
        camera->override_rate_ = kCameraRate;
        return;
    default:
        camera->override_ = false;
        return;
    }
}

// 0x0052A2C0
void FootMovement::update_anim_speed()
{
    void* animation = anim_blender_;
    // 0x00419B90: the animation's first channel's speed (and whether to send it over the network).
    auto set_speed = [animation](float speed, int send) {
        // (the other three are passed as integers: their bits)
        static_cast<AnimBlenderComponent*>(animation)->first_channel_47e_850(speed, std::bit_cast<float>(send), 0.0f,
                                                                             0.0f);
    };
    // The animation component's member function 0xBD4EDD44 with speed = `speed`.
    auto tell = [animation](float speed) {
        script::Struct* params = script::spawn_param_bag();
        params = params ? new (params) script::Struct : nullptr;
        params->push_float(kSpeedName, speed);
        reinterpret_cast<ObjectModule*>(animation)->invoke_method_by_name(kSetAnimSpeed, params, nullptr);
        if (params) {
            params->get_clear();
            script::delete_struct(params);
        }
    };
    switch (anim_speed_scaling_) {
    case 1: { // running
        if (!(standard_speed_ > 0.0f))
            return;
        set_speed(speed_ / standard_speed_, 0);
        if (!physics_control_->board_missing_)
            return;
        tell(speed_ / standard_speed_);
        return;
    }
    case 2: { // hanging
        float speed = speed_ / walk_parameter(kHangMoveAnimSpeed);
        tell(speed);
        set_speed(speed, 0);
        return;
    }
    case 3: { // on a ladder: also sent when it changed by 0.1 or more
        float speed = speed_ / walk_parameter(kLadderMoveSpeed);
        tell(speed);
        auto* net = online::g_online_hub;
        if (net->in_net_game() && !(std::fabs(static_cast<double>(speed) - ladder_sign_) < 0.10000000149011612)) {
            ladder_sign_ = speed;
            set_speed(speed, 1);
            return;
        }
        set_speed(speed, 0);
        return;
    }
    default:
        return;
    }
}

// 0x0052A620
void FootMovement::update_ride()
{
    objects::MovingSurfaceModule* ride = ride_;
    auto* tracker = ride->ride_;
    if (!tracker)
        return;
    maths::Vec4& position = pos_;
    if (!tracker->entity_.get_pull() || !tracker->tick_frame(&position)) {
        static_cast<MovingSurfaceModule*>(ride)->stop_riding(); // (what it stood on is gone)
        return;
    }
    tracker = ride->ride_;
    position.x = append_one(tracker->carried_.x, position.x);
    position.y = append_one(tracker->carried_.y, position.y);
    position.z = append_one(tracker->carried_.z, position.z);
    position.w = append_one(tracker->carried_.w, position.w);
    if (!ride->ride_->turned_)
        return;
    maths::Vec4& facing = facing_;
    maths::Vec4 turned;
    facing = ride->ride_->turn_.rotate_vector(turned, facing);
    if (!(facing.y == 0.0f)) { // kept level
        facing.y = 0.0f;
        facing.make_unit();
    }
}

// 0x0052AB90
void FootMovement::leave_ride(maths::Vec4* velocity, float* vertical)
{
    objects::MovingSurfaceModule* ride = ride_;
    auto* tracker = ride->ride_;
    maths::Vec4& carried = jump_base_vel_;
    if (tracker && tracker->entity_.get_pull()) {
        carried = tracker->entity_.get_pull()->vel_; // its velocity
        double heading = static_cast<double>(carried.z) * velocity->z + static_cast<double>(carried.y) * velocity->y +
                         static_cast<double>(carried.x) * velocity->x;
        if (heading > 0.0) {
            // moved on by it for the frame (x's product unrounded; y, z, w's stored first)
            double frame = frame_length_;
            auto y = static_cast<float>(frame * carried.y);
            auto z = static_cast<float>(frame * carried.z);
            auto w = static_cast<float>(frame * carried.w);
            maths::Vec4& position = pos_;
            position.x = static_cast<float>(frame * carried.x + position.x);
            position.y = append_one(y, position.y);
            position.z = append_one(z, position.z);
            position.w = append_one(w, position.w);
        }
        *vertical = append_one(carried.y, *vertical);
        carried.y = 0.0f;
        velocity->x = append_one(carried.x, velocity->x);
        velocity->y = append_one(carried.y, velocity->y);
        velocity->z = append_one(carried.z, velocity->z);
        velocity->w = append_one(carried.w, velocity->w);
    } else {
        if (tracker)
            static_cast<MovingSurfaceModule*>(ride)->stop_riding(); // (what it stood on is gone)
        carried = {0.0f, 0.0f, 0.0f, 1.0f};
    }
    // The ride let go.
    ride = ride_;
    tracker = ride->ride_;
    if (!tracker)
        return;
    ride->last_object_ = tracker->entity_.get_pull();
    reinterpret_cast<SharedRef<SharedBase>*>(&tracker->entity_)->get_release();
    memory::Hub::get_pull()->dealloc(tracker);
    ride->ride_ = nullptr;
    ride->stop_time_ = get_now();
}

// 0x0052B2D0
void FootMovement::go_to_air()
{
    change_phase(1); // (the state: in the air)
    maths::Vec4& heading = leap_heading_;
    heading = entity_->vel_;
    heading.y = 0.0f;
    heading.make_unit();
    leave_ride(&entity_->vel_, &entity_->vel_.y);
    maths::Vec4& carried = jump_base_vel_;
    carried = entity_->vel_;
    carried.y = 0.0f;
}

// 0x0052A180
int FootMovement::clip_finished_waiting(script::Struct*, script::Script*)
{
    apply_transition();
    return 1;
}

// 0x0052C280
int FootMovement::release_hang(script::Struct*, script::Script*)
{
    get_jump();
    return 1;
}

// 0x0052C290
int FootMovement::force_to_air_from_hang(script::Struct*, script::Script*)
{
    go_to_air();
    entity_->post_own_event(kAir, nullptr);
    return 1;
}

// 0x0052C2B0
void FootMovement::go_to_air_for(void*) { go_to_air(); }

// 0x0052D4B0
void FootMovement::tick_frame()
{
    if (jump_held_ && !input_->pad_primary_.stick_click_left_.held_down_)
        jump_held_ = false;
    uint32_t previous_anim = anim_state_;
    previous_anim_state_ = previous_anim;
    anim_state_ = 0;
    update_control();
    copy_from_object();
    maths::Vec4& position = pos_;
    old_pos_ = position;
    frame_length_ = static_cast<float>(frame_clock::get_slomo_frame_seconds());
    curb_float_ = false;
    position.y -= curb_offset_; // (put back below)
    update_run_adjustment();

    switch (state_) {
    case 0:
        update_ground();
        break;
    case 1:
        update_air();
        break;
    case 2:
        update_hang(); // hanging
        break;
    case 3:
        update_ladder(); // on a ladder
        break;
    case 4:
        update_anim_wait();
        break;
    default:
        break;
    }
    if (halted(this))
        return;

    // The step smoothing's height eases down unless held this frame.
    if (!curb_float_) {
        float rate = walk_parameter(kCurbFloatLerpDownRate);
        double offset = curb_offset_;
        curb_offset_ = static_cast<float>(-offset * (static_cast<double>(rate) * frame_length_) + offset);
    }
    position.y += curb_offset_;

    if (state_ == 0 && static_cast<uint32_t>(get_now() - state_time_) > 500) {
        skater::try_acid_drop(this); // (an acid drop)
        if (halted(this))
            return;
    }
    ease_critical_point_offset();
    restore_display_offset();
    if (state_ == 0 || state_ == 1)
        update_ground_below();
    update_upright();
    copy_to_object();
    update_rotate_upright();
    clamp_skater_speed(previous_anim);
    entity_->post_own_event(anim_state_, nullptr);
    update_anim_speed();
    update_camera_override();
    static_cast<skater::SkaterPhysicsModule*>(core_physics_)->update_focus_button();
    int& countdown = unknown710_;
    // NOLINTNEXTLINE(bugprone-inc-dec-in-conditions): the original's, as written
    if (countdown > 0 && --countdown == 0)
        locked_to_heading_ = false;
}

// 0x0052D300
void FootMovement::update_ground()
{
    update_ride();
    refresh_hit_cache();
    maths::Vec4& velocity = vel_;
    update_ground_velocity(&velocity);
    if (anim_state_ == kSkid) {
        float pegged = walk_parameter(kPeggedDurationForSkid);
        if (skid_time_ < pegged)
            anim_state_ = kStand;
    }
    if (update_contacts())
        turn_facing_to_velocity();
    else
        blocked_ = false;
    jump_unsteered_ = false;
    if (state_ != 0 || halted(this)) {
        clear_current_cache();
        return;
    }

    // Moved on by the velocity (x's move unrounded; y, z and w's stored first).
    double frame = frame_length_;
    maths::Vec4& position = pos_;
    auto y = static_cast<float>(frame * velocity.y);
    auto z = static_cast<float>(frame * velocity.z);
    auto w = static_cast<float>(frame * velocity.w);
    position.x = static_cast<float>(frame * velocity.x + position.x);
    position.y += y;
    position.z += z;
    position.w += w;
    settle_onto_floor();

    if (state_ == 0 && !halted(this)) {
        update_curb_float();
        objects::MovingSurfaceModule* ride = ride_;
        if (GameEntity* inside = moving_object_around(ride, &position, &old_pos_)) {
            report_inside(0x369, "WALKING_GROUND, within moving object");
            // Walk off with its velocity, facing that way when it is fast (over 10).
            velocity = inside->vel_;
            velocity.y = 0.0f;
            up_speed_ = 0.0f;
            double length_sqr = static_cast<double>(velocity.x) * velocity.x +
                                static_cast<double>(velocity.y) * velocity.y +
                                static_cast<double>(velocity.z) * velocity.z;
            if (length_sqr > 100.0)
                facing_ = velocity / static_cast<float>(std::sqrt(length_sqr));
        }
        static_cast<skater::SkaterPhysicsModule*>(core_physics_)->update_projectiles();
    }
    clear_current_cache();
}

// 0x0052CEF0
void FootMovement::update_air()
{
    const objects::PadSnapshot* pad = &input_->pad_primary_;
    refresh_hit_cache();
    anim_state_ = kAir;
    steer_jump();
    if (!update_contacts())
        blocked_ = false;
    if (halted(this))
        return;
    update_push();
    check_ceiling();
    double gravity = static_cast<float>(get_gravity());

    // Moved on by the velocity and the extra velocity (each move unrounded), then by the vertical speed under
    // gravity.
    double frame = frame_length_;
    maths::Vec4& position = pos_;
    const maths::Vec4& velocity = vel_;
    for (int i = 0; i < 4; ++i)
        (&position.x)[i] = static_cast<float>(frame * (&velocity.x)[i] + (&position.x)[i]);
    maths::Vec4& extra = jump_velocity_;
    for (int i = 0; i < 4; ++i)
        (&position.x)[i] = static_cast<float>(frame * (&extra.x)[i] + (&position.x)[i]);
    float& vertical = up_speed_;
    position.y = static_cast<float>((vertical * frame - frame * frame * gravity * 0.5) + position.y);
    vertical = static_cast<float>(vertical - gravity * frame);
    if (position.y > jump_target_height_)
        extra = {0.0f, 0.0f, 0.0f, 1.0f};
    static_cast<skater::SkaterPhysicsModule*>(core_physics_)->update_projectiles();
    maths::Vec4& old_position = old_pos_;
    collide_air_move(&old_position, &position);
    if (state_ != 1 || halted(this))
        return;

    // Grabbing a ledge or a ladder (not again within rehang_delay of letting go of one).
    if (pad->shoulder_right_a_.held_down_ && !jump_held_) {
        int previous = previous_state_;
        bool grab = true;
        if (previous == 2 || previous == 3 || previous == 4) {
            auto since = static_cast<float>(
                static_cast<double>(static_cast<uint32_t>(frame_clock::game_clock_time_since(state_time_))));
            grab = get_param(kRehangDelay) < since;
        }
        if (grab && (grab_rail(old_position, position) || grab_ladder(&old_position, &position))) {
            clear_current_cache();
            return;
        }
    }
    clear_current_cache();
    if (pad->button_wedge_.held_down_ && try_grind())
        return;
    if (skater::try_walk_acid_drop(this)) // (an acid drop)
        return;
    check_sticker_slap();
    try_wallplant();
    if (halted(this))
        return;

    // Inside a moving object: put back where it was, standing on it (a feeler 30 up and down through the object
    // finds it), stopped.
    maths::Vec4 start = position;
    const maths::Vec4& offset = critical_point_offset_;
    maths::Vec4 to = position + offset;
    maths::Vec4 from = old_position + offset;
    if (!moving_object_around(ride_, &to, &from))
        return;
    position = to - offset;
    report_inside(0x74B, "WALKING_AIR, within moving object");
    render::ProbeRay storage;
    render::ProbeRay* feeler = &storage;
    const maths::Vec4 up = {0.0f, 30.0f, 0.0f, 0.0f};
    maths::Vec4 top = entity_->at_point_ + up;
    feeler->change_origin(&top);
    maths::Vec4 bottom = entity_->at_point_ - up;
    feeler->change_finish(&bottom);
    if (feeler->get_hit_shape(true, false) && feeler->hit_object_alive())
        reinterpret_cast<walk::MovableContact*>(ride_)->start_riding(feeler->get_hit_object());
    vel_ = {0.0f, 0.0f, 0.0f, 1.0f};
    vertical = 0.0f;
    collide_air_move(&position, &start);
}

// 0x0052AD10
void FootMovement::update_anim_wait()
{
    // Carried by what it rides on: the offset moved, the facing turned (kept level).
    objects::MovingSurfaceModule* ride = ride_;
    if (auto* tracker = ride->ride_) {
        if (!tracker->entity_.get_pull() || !tracker->tick_frame(&pos_)) {
            static_cast<MovingSurfaceModule*>(ride)->stop_riding(); // (what it stood on is gone)
        } else {
            tracker = ride->ride_;
            maths::Vec4& carried = climb_offset_;
            carried.x += tracker->carried_.x;
            carried.y += tracker->carried_.y;
            carried.z += tracker->carried_.z;
            carried.w += tracker->carried_.w;
            tracker = ride->ride_;
            if (tracker->turned_) {
                maths::Vec4& facing = transition_facing_;
                maths::Vec4 turned;
                facing = tracker->turn_.rotate_vector(turned, facing);
                if (!(facing.y == 0.0f)) {
                    facing.y = 0.0f;
                    facing.make_unit();
                }
            }
        }
    }

    // How far through the animation it is: the position between the two ends (plus what it was carried), the
    // facing turned back from the full turn as the turn's time passes, the height eased.
    float start;
    float current;
    float end;
    reinterpret_cast<AnimBlenderComponent*>(anim_blender_)->get_times(&start, &current, &end);
    auto t = static_cast<float>((static_cast<double>(current) - start) / (static_cast<double>(end) - start));
    maths::Vec4 between;
    walk::get_lerp(&between, &transition_start_, &transition_pos_, t);
    const maths::Vec4& carried = climb_offset_;
    maths::Vec4& position = pos_;
    position.x = between.x + carried.x;
    position.y = between.y + carried.y;
    position.z = between.z + carried.z;
    position.w = between.w + carried.w;
    double turn_part = t / static_cast<double>(transition_rate_);
    if (turn_part > 1.0)
        turn_part = 1.0;
    double turn = transition_turn_;
    double angle = turn * turn_part - turn;
    maths::Vec4& facing = facing_;
    facing = transition_facing_;
    double sine = std::sin(angle);
    double cosine = std::cos(angle);
    double x = facing.x;
    facing.x = static_cast<float>(cosine * x + sine * facing.z);
    facing.z = static_cast<float>(-(x * sine) + cosine * facing.z);
    anim_state_ = kAnimWait;
    double low = transition_start_offset_;
    model_lift_ = static_cast<float>((transition_display_offset_ - low) * t + low);
}

// 0x0052A700
void FootMovement::collide_air_move(const maths::Vec4* from, const maths::Vec4* to)
{
    render::ProbeRay storage;
    render::ProbeRay* feeler = &storage;
    feeler->line_.start = *from;
    feeler->line_.end = *to;
    if (!feeler->get_hit_shape(true, false)) {
        release_hit(feeler);
        return;
    }
    maths::Vec4& position = pos_;
    if (feeler->hit_.face_flags_ & kFaceWall) {
        // A wall: put 0.1 off it, the rest of the move slid along it - stopped 0.05 off anything that meets.
        put_off(&position, feeler, kWallGap);
        auto left = static_cast<float>(1.0 - feeler->reach_);
        const maths::Seg& line = feeler->line_;
        auto dx = line.end.x - line.start.x;
        auto dy = line.end.y - line.start.y;
        auto dz = line.end.z - line.start.z;
        double dw = static_cast<double>(line.end.w) - line.start.w;
        maths::Vec4 rest = {dx * left, dy * left, dz * left, static_cast<float>(dw * left)};
        rest.flatten_onto_plane(feeler->surface_dir_);
        feeler->line_.start = position;
        feeler->line_.end = rest + position;
        if (feeler->get_hit_shape(true, false))
            put_off(&position, feeler, kSlideGap);
        else
            position = feeler->line_.end;
        release_hit(feeler);
        return;
    }

    // Landed: on the ground, the ground's trigger kinds set off, the velocity kept only along the facing (relative
    // to what it landed on) - stopped when heading away, or slow (under sticky_land_threshold_speed) without
    // control.
    position = feeler->contact_pos_;
    up_speed_ = 0.0f;
    change_phase(0);
    render::ProbeRay* ground = &feeler_;
    reinterpret_cast<render::ProbeRay*>(ground)->get_assign(*feeler); // (the feeler copied)
    feeler_hit_ = true;
    static_cast<ActivationZoneModule*>(trigger_)->probe_touches_zone(kWalkEvents0064c648, ground);
    if (!halted(this)) {
        floor_normal_ = feeler->surface_dir_;
        objects::MovingSurfaceModule* ride = ride_;
        static_cast<MovingSurfaceModule*>(ride)->start_riding(feeler); // (what it landed on)
        maths::Vec4& velocity = vel_;
        ride = ride_;
        if (auto* tracker = ride->ride_) {
            if (!tracker->entity_.get_pull()) {
                static_cast<MovingSurfaceModule*>(ride)->stop_riding(); // (what it stood on is gone)
            } else {
                velocity -= ride->ride_->entity_.get_pull()->vel_;
                velocity.y = 0.0f;
            }
        }
        const maths::Vec4& facing = facing_;
        double along = static_cast<double>(velocity.z) * facing.z + static_cast<double>(velocity.y) * facing.y +
                       static_cast<double>(velocity.x) * facing.x;
        bool stop = true;
        if (along > 0.0) {
            velocity.project_onto(facing);
            stop = false;
            if (stick_deflection_ == 0.0f) {
                auto speed = static_cast<float>(std::sqrt(static_cast<double>(velocity.x) * velocity.x +
                                                          static_cast<double>(velocity.y) * velocity.y +
                                                          static_cast<double>(velocity.z) * velocity.z));
                stop = get_param(kStickyLandThresholdSpeed) > speed;
            }
        }
        if (stop)
            velocity = {0.0f, 0.0f, 0.0f, 1.0f};
        critical_point_offset_ = {0.0f, 0.0f, 0.0f, 1.0f};
        input_->pad_primary_.pos_x_.forget_let_go(); // (a button's release taken)
        anim_state_ = kLand;
    }
    release_hit(feeler);
}

// 0x0052B370
void FootMovement::settle_onto_floor()
{
    // A feeler from snap_up_height above to snap_down_height below the position.
    render::ProbeRay storage;
    render::ProbeRay* feeler = &storage;
    maths::Vec4& position = pos_;
    feeler->line_.start = position;
    feeler->line_.start.y += walk_parameter(kSnapUpHeight);
    feeler->line_.end = position;
    feeler->line_.end.y -= walk_parameter(kSnapDownHeight);
    if (feeler->get_hit_shape(true, false) && !(feeler->hit_.face_flags_ & kFaceWall)) {
        // Onto the ground (a big step told to the object as an event); a step up, or down more than 3, goes into
        // the step smoothing's height.
        float step = feeler->contact_pos_.y - position.y;
        float unnoticed = walk_parameter(kMaxUnnoticedGroundSnap);
        if (std::fabs(step) > unnoticed)
            entity_->post_own_event(step > 0.0f ? kSteppedUp : kSteppedDown, nullptr);
        position.y = feeler->contact_pos_.y;
        if (step > 0.0f || step < kBigStepDown) {
            double offset = static_cast<double>(curb_offset_) - step;
            curb_offset_ = offset < 0.0 ? 0.0f : static_cast<float>(offset);
        }
        // Onto other collision: the old ground's and the new ground's trigger kinds set off.
        render::ProbeRay* ground = &feeler_;
        if (ground->hit_.object != feeler->hit_.object) {
            if (feeler_hit_) {
                static_cast<ActivationZoneModule*>(trigger_)->probe_touches_zone(kWalkEvents0064c660, ground);
                if (halted(this)) {
                    return;
                }
            }
            static_cast<ActivationZoneModule*>(trigger_)->probe_touches_zone(kWalkEvents0064c654, feeler);
            if (halted(this)) {
                return;
            }
        }
        reinterpret_cast<render::ProbeRay*>(ground)->get_assign(*feeler); // (the feeler copied)
        feeler_hit_ = true;
        floor_normal_ = feeler->surface_dir_;
        static_cast<MovingSurfaceModule*>(ride_)->start_riding(feeler); // (what it stands on)
        // (the terrain byte, passed as the dword there)
        static_cast<RiderPoseState*>(skater_state_)
            ->set_surface_kind(static_cast<uint8_t>(*reinterpret_cast<const uint32_t*>(&feeler->hit_.terrain)));
        release_hit(feeler);
        return;
    }

    // No ground (or only a wall): off the wall, then off the edge - unless a hang, a drop or a ladder takes over.
    if (feeler->get_hit_shape(true, false))
        put_off(&position, feeler, kWallGap);
    if (feeler_hit_) {
        static_cast<ActivationZoneModule*>(trigger_)->probe_touches_zone(kWalkEvents0064c630, &feeler_);
        if (halted(this)) {
            release_hit(feeler);
            return;
        }
    }
    if (input_->pad_primary_.button_wedge_.held_down_) {
        maths::Vec4& old_position = old_pos_;
        if (position == old_position)
            old_position.y += kNudge; // (so the two differ)
        if (try_grind()) {
            release_hit(feeler);
            return;
        }
    }
    if (climb_onto_top() || drop_to_hang()) {
        release_hit(feeler);
        return;
    }
    change_phase(1);
    leap_heading_ = facing_;
    leave_ride(&vel_, &up_speed_);
    anim_state_ = kWalkOffEdge;
    entity_->announce(kSkaterOffEdge, nullptr, 0.0f);
    release_hit(feeler);
}

// 0x0052AF40
void FootMovement::get_begin(bool on_ground)
{
    state_time_ = get_now();
    if (on_ground) {
        change_phase(0);
        floor_normal_ = {0.0f, 1.0f, 0.0f, 1.0f};
        feeler_hit_ = false;
        entity_->vel_.y = 0.0f;
        off_vert_ = false;
    } else {
        change_phase(1);
        // Off a vert ramp (the skater's flag +0x30) with a turn under way: thrown out along the object's forward
        // axis turned on (by +0x730 about +0x734) times initial_vert_vel_boost.
        if (core_physics_->state_->flag(PoseFlag::kVertAir).on && !(upright_time_ == 0.0f)) {
            maths::Vec4 forward = entity_->basis_frame_.row[2];
            forward.rotate_about_axis(upright_axis_, upright_angle_);
            double boost = walk_parameter(kInitialVertVelBoost);
            maths::Vec4& velocity = entity_->vel_;
            auto z = static_cast<float>(forward.z * boost);
            auto w = static_cast<float>(forward.w * boost);
            velocity.x = static_cast<float>(forward.x * boost + velocity.x); // (x and y's products unrounded)
            velocity.y = static_cast<float>(forward.y * boost + velocity.y);
            velocity.z += z;
            velocity.w += w;
        }
        if (!core_physics_->state_->flag(PoseFlag::kVertAir).on)
            jump_unsteered_ = true;
        // The heading: the velocity flattened, else the forward axis flattened, else the up axis reversed and
        // flattened; made unit length.
        maths::Vec4& heading = leap_heading_;
        heading = entity_->vel_;
        heading.y = 0.0f;
        double length =
            std::sqrt((static_cast<double>(heading.x) * heading.x + static_cast<double>(heading.y) * heading.y) +
                      static_cast<double>(heading.z) * heading.z);
        if (length < kMinHeadingLength) {
            heading = entity_->basis_frame_.row[2];
            heading.y = 0.0f;
            length =
                std::sqrt((static_cast<double>(heading.z) * heading.z + static_cast<double>(heading.x) * heading.x) +
                          static_cast<double>(heading.y) * heading.y);
            if (length < kMinHeadingLength) {
                heading = -entity_->basis_frame_.row[1];
                heading.y = 0.0f;
                length = std::sqrt(
                    (static_cast<double>(heading.x) * heading.x + static_cast<double>(heading.y) * heading.y) +
                    static_cast<double>(heading.z) * heading.z);
            }
        }
        double inverse = 1.0 / length;
        heading.x = static_cast<float>(inverse * heading.x);
        heading.y = static_cast<float>(inverse * heading.y);
        heading.z = static_cast<float>(inverse * heading.z);
        heading.w = static_cast<float>(inverse * heading.w);
        leave_ride(&entity_->vel_, &entity_->vel_.y);
        leap_duration_ = 0.0f;
        pushing_ = false;
        off_vert_ =
            core_physics_->state_->flag(PoseFlag::kVertAir).on || core_physics_->state_->flag(PoseFlag::kNoAcidDrop).on;
    }
    curb_offset_ = 0.0f;
    leave_for_ = 0;
    previous_anim_state_ = 0;
    jump_held_ = true;
    critical_point_offset_ = {0.0f, 0.0f, 0.0f, 1.0f};
    model_lift_ = 0.0f;
    tagging_->countdown_ = 0;
    locked_to_heading_ = false;
}

// 0x0052B7D0
void FootMovement::get_jump()
{
    // How high: on the ground or in the air by how long jump has been held (from min_jump_factor up to 1 at
    // hold_time_for_max_jump); off a hang or a ladder hang_jump_factor.
    float factor = 0.0f;
    // NOLINTNEXTLINE(bugprone-switch-missing-default-case): the other values do nothing, as the original's
    switch (state_) {
    case 0:
    case 1: {
        float hold = walk_parameter(kHoldTimeForMaxJump);
        auto* button = &input_->pad_primary_.pos_x_;
        double held = static_cast<uint32_t>(button->get_held_millis()) / static_cast<double>(hold);
        float t = held > 1.0 ? 1.0f : static_cast<float>(held);
        float minimum = walk_parameter(kMinJumpFactor);
        factor = static_cast<float>((1.0 - minimum) * t + minimum);
        break;
    }
    case 2:
    case 3:
    case 4:
        factor = walk_parameter(kHangJumpFactor);
        break;
    }
    if (state_ == 0 && feeler_hit_) {
        static_cast<ActivationZoneModule*>(trigger_)->probe_touches_zone(kWalkEvents0064c63c,
                                                                         &feeler_); // (the ground's jump triggers)
        if (halted(this))
            return;
    }
    entity_->vel_.y = walk_parameter(kJumpVelocity) * factor;

    maths::Vec4& heading = leap_heading_;
    const maths::Vec4& facing = facing_;
    // NOLINTNEXTLINE(bugprone-switch-missing-default-case): the other values do nothing, as the original's
    switch (state_) {
    case 0:
    case 1: {
        // Running: pushed along the control's direction at least as fast as jump_horiz_speed (scaled by how far
        // the control is pushed, over the run speed's range); the heading the velocity's when moving.
        copy_from_object();
        maths::Vec4& velocity = vel_;
        if (stick_deflection_ == 0.0f) {
            heading = facing;
        } else {
            auto push = static_cast<float>(get_control_speed()); // (how far the control is pushed, 0 - 1)
            float scaled = get_param(kJumpHorizSpeed) * push;
            auto speed = static_cast<float>(scaled / run_speed());
            const maths::Vec4& direction = stick_heading_;
            double along =
                (static_cast<double>(velocity.z) * direction.z + static_cast<double>(velocity.y) * direction.y) +
                static_cast<double>(velocity.x) * direction.x;
            if (along > 0.0) {
                velocity.project_onto(direction);
                double length = std::sqrt(
                    (static_cast<double>(velocity.x) * velocity.x + static_cast<double>(velocity.y) * velocity.y) +
                    static_cast<double>(velocity.z) * velocity.z);
                if (length < speed)
                    velocity.set_length(speed);
            } else {
                velocity = speed * direction;
            }
            heading = direction;
            facing_ = direction;
        }
        plan_jump_over_obstruction();
        double length_sqr =
            (static_cast<double>(velocity.x) * velocity.x + static_cast<double>(velocity.y) * velocity.y) +
            static_cast<double>(velocity.z) * velocity.z;
        if (length_sqr > kMovingSqr) {
            heading = velocity;
            heading /= static_cast<float>(std::sqrt(length_sqr));
        } else {
            heading = facing;
        }
        copy_to_object();
        break;
    }
    case 2: // hanging: off the ledge's plane, kept level
        pushing_ = true;
        push_normal_ = facing;
        push_plane_distance_ = hang_plane(this);
        push_height_ = pos_.y - rail_up_;
        entity_->vel_.x = 0.0f;
        entity_->vel_.z = 0.0f;
        heading = facing;
        break;
    case 3: // on a ladder
        entity_->vel_.x = 0.0f;
        entity_->vel_.z = 0.0f;
        heading = facing;
        break;
    case 4: // moving along a hang: the plane along the facing it had when the hang began
        pushing_ = true;
        push_plane_distance_ = hang_plane(this);
        entity_->vel_.x = 0.0f;
        entity_->vel_.z = 0.0f;
        heading = facing;
        break;
    }
    leave_ride(&entity_->vel_, &entity_->vel_.y);
    change_phase(1);
    entity_->announce(kSkaterJump, nullptr, 0.0f);
}

// 0x0052BC70
bool FootMovement::try_barrier_jump()
{
    if (core_physics_->state_->flag(PoseFlag::kBailing).on)
        return false;
    static const int kAhead[3] = {0, 1, 6}; // the contacts ahead: straight on and either side of it

    float highest = walk_parameter(kBarrierJumpHighestBarrier);
    float reach = walk_parameter(kFeelerLength) * 3.0f;
    float feeler_height = walk_parameter(kProbeElevation);
    auto step = static_cast<float>((static_cast<double>(highest) - feeler_height) * kNinth);
    render::ProbeRay feeler_storage;
    render::ProbeRay* feeler = &feeler_storage;

    // The collision near it: `reach` + 1 to each side, from its feet to above the highest barrier.
    const maths::Vec4& position = pos_;
    auto side = static_cast<float>(static_cast<double>(reach) + 1.0);
    maths::BBox box;
    box.max = {side + position.x, static_cast<float>(static_cast<double>(highest) + 1.0) + position.y,
               side + position.z, static_cast<float>(static_cast<double>(position.w) + 1.0)};
    box.min = {position.x - side, position.y, position.z - side,
               static_cast<float>(static_cast<double>(position.w) - 1.0)};
    render::HitMeshCache cache_storage;
    render::HitMeshCache* cache = &cache_storage;
    cache->tick_frame(box);
    feeler->hit_cache_ = cache;

    // Clear: a contact ahead, facing the control's direction head-on (within barrier_jump_max_angle), with nothing in
    // the way at the highest barrier's height.
    bool clear = false;
    for (int index : kAhead) {
        auto& contact = contact_at(this, index);
        if (!contact.hit || (contact.feeler.hit_.face_flags_ & kFaceWall))
            continue;
        double limit = std::cos(walk_parameter(kBarrierJumpMaxAngle) * kDegrees);
        const maths::Vec4& control = stick_heading_;
        double facing_it =
            (static_cast<double>(control.z) * contact.normal.z + static_cast<double>(control.y) * contact.normal.y) +
            static_cast<double>(control.x) * contact.normal.x;
        if (-limit < facing_it) {
            contact.clear = false;
            continue;
        }
        maths::Vec4& start = feeler->line_.start;
        start = position;
        start.y = position.y + highest;
        maths::Vec4 direction;
        get_turned_facing(&direction, index); // (the contact's direction)
        maths::Vec4& end = feeler->line_.end;
        end.x = static_cast<float>(static_cast<double>(reach) * direction.x) + start.x;
        end.y = static_cast<float>(static_cast<double>(reach) * direction.y) + start.y;
        end.z = static_cast<float>(static_cast<double>(reach) * direction.z) + start.z;
        end.w = static_cast<float>(static_cast<double>(reach) * direction.w + start.w);
        contact.clear = !feeler->get_hit_shape(true, false);
        if (contact.clear)
            clear = true;
    }
    if (!clear) {
        release_hit(feeler);
        return false;
    }

    // How high the barrier is: from the highest barrier down in ninths until each clear contact's feeler meets it.
    const maths::Vec4& facing = facing_;
    float top = highest + position.y;
    for (int index : kAhead) {
        const auto& contact = contact_at(this, index);
        if (!contact.hit || !contact.clear)
            continue;
        maths::Vec4& start = feeler->line_.start;
        start = position;
        start.y = position.y + highest;
        double angle = index * kSeventhTurn;
        double c = std::cos(angle);
        double s = std::sin(angle);
        auto x = static_cast<float>(c * facing.x - s * facing.z);
        maths::Vec4& end = feeler->line_.end;
        end.x = static_cast<float>(static_cast<double>(x) * reach) + start.x;
        end.y = static_cast<float>(static_cast<double>(reach) * 0.0) + start.y;
        end.z = static_cast<float>((s * facing.x + c * facing.z) * reach) + start.z;
        end.w = reach + start.w;
        int tries = 8;
        bool hit;
        do {
            start.y = start.y - step;
            end.y = end.y - step;
            --tries;
            hit = feeler->get_hit_shape(true, false);
        } while (!hit && tries != 0);
        if (top > start.y)
            top = start.y;
    }

    // Over it: up fast enough to clear it by barrier_jump_min_clearance.
    float clearance = walk_parameter(kBarrierJumpMinClearance);
    auto rise = static_cast<float>(((static_cast<double>(clearance) + top) + step) - position.y);
    double gravity = static_cast<float>(get_gravity());
    auto speed = static_cast<float>(std::sqrt(gravity * rise * 2.0));
    if (feeler_hit_) {
        static_cast<ActivationZoneModule*>(trigger_)->probe_touches_zone(kWalkEvents0064c63c,
                                                                         &feeler_); // (the ground's jump triggers)
        if (halted(this)) {
            release_hit(feeler);
            return true;
        }
    }
    up_speed_ = speed;
    change_phase(1);
    leap_heading_ = facing_;
    leave_ride(&vel_, &up_speed_);
    anim_state_ = kJump;
    entity_->announce(kSkaterJump, nullptr, 0.0f);
    entity_->clear_event_handler(kJumpRequested);
    release_hit(feeler);
    return true;
}

// 0x0052C2C0
bool FootMovement::update_contacts()
{
    float reach = walk_parameter(kFeelerLength);
    float feeler_height = walk_parameter(kProbeElevation);
    render::ProbeRay storage;
    render::ProbeRay* feeler = &storage;
    maths::Vec4& position = pos_;
    maths::Vec4& velocity = vel_;
    const maths::Vec4& facing = facing_;

    // The eight feelers: seven around it at feeler_height, a seventh of a turn apart from the facing, and in the air
    // one along this frame's fall.
    bool any = false;
    for (int i = 0; i < 8; ++i) {
        maths::Vec4& start = feeler->line_.start;
        maths::Vec4& end = feeler->line_.end;
        if (i == 7) {
            if (state_ != 1) {
                contact_at(this, 7).hit = false;
                continue;
            }
            float frame = frame_length_;
            const maths::Vec4& offset = critical_point_offset_;
            float z = position.z + offset.z;
            float y = position.y + offset.y;
            float x = offset.x + position.x;
            double w = static_cast<double>(position.w) + offset.w;
            start = {x, y, z, static_cast<float>(w)};
            float move_w = frame * velocity.w;
            float move_z = frame * velocity.z;
            float move_y = frame * velocity.y;
            float move_x = frame * velocity.x;
            end = {move_x + x, move_y + y, move_z + z, static_cast<float>(w + move_w)};
            float fall = up_speed_ * frame;
            double gravity = static_cast<float>(get_gravity());
            end.y = static_cast<float>((fall - gravity * (static_cast<double>(frame) * frame) * kSteepGround) + end.y);
        } else {
            start = position;
            start.y = position.y + feeler_height;
            double angle = i * kSeventhTurn;
            double c = std::cos(angle);
            double s = std::sin(angle);
            auto x = static_cast<float>(c * facing.x - s * facing.z);
            auto z = static_cast<float>((s * facing.x + c * facing.z) * reach);
            float y = static_cast<float>(static_cast<double>(reach) * 0.0) + position.y;
            end = {static_cast<float>(static_cast<double>(x) * reach) + position.x, y + feeler_height, z + position.z,
                   reach + position.w};
        }
        auto& contact = contact_at(this, i);
        contact.hit = feeler->get_hit_shape(true, false);
        if (!contact.hit)
            continue;
        if (i == 7 && feeler->surface_dir_.y > kFloorBelow) { // (a floor below: not a contact)
            contact.hit = false;
            continue;
        }
        reinterpret_cast<render::ProbeRay*>(&contact.feeler)->get_assign(*feeler); // (the feeler copied)
        // The normal flattened and made unit length; how fast the object hit moves along it (not what it rides).
        contact.normal = feeler->surface_dir_;
        contact.normal.y = 0.0f;
        any = true;
        maths::Vec4& normal = contact.normal;
        double length =
            std::sqrt((static_cast<double>(normal.x) * normal.x + static_cast<double>(normal.y) * normal.y) +
                      static_cast<double>(normal.z) * normal.z);
        if (length > 0.0) {
            double inverse = 1.0 / length;
            normal.x = static_cast<float>(inverse * normal.x);
            normal.y = static_cast<float>(inverse * normal.y);
            normal.z = static_cast<float>(inverse * normal.z);
        }
        contact.object_speed_ = 0.0f;
        if (feeler->hit_object_alive()) {
            objects::MovingSurfaceModule* ride = ride_;
            bool ridden = false;
            if (auto* tracker = ride->ride_) {
                if (!tracker->entity_.get_pull())
                    static_cast<MovingSurfaceModule*>(ride)->stop_riding(); // (what it stood on is gone)
                else
                    ridden = tracker->entity_.get_pull() == feeler->get_hit_object();
            }
            if (!ridden) {
                const maths::Vec4& moving = feeler->get_hit_object()->vel_;
                contact.object_speed_ = static_cast<float>(
                    (static_cast<double>(moving.z) * normal.z + static_cast<double>(moving.y) * normal.y) +
                    static_cast<double>(moving.x) * normal.x);
            }
        }
    }

    if (state_ == 0) {
        // Walking into a barrier ahead for barrier_jump_delay: a jump over it (or another kind of jump) tried.
        if ((contact_at(this, 0).hit || contact_at(this, 1).hit || contact_at(this, 6).hit) && control_full_) {
            if (!barrier_timed_) {
                barrier_since_ = get_now();
                barrier_timed_ = true;
            }
            int since = barrier_since_;
            float delay = walk_parameter(kBarrierJumpDelay);
            auto elapsed = static_cast<uint32_t>(frame_clock::game_clock_elapsed() + (g_clock_offset - since));
            if (static_cast<double>(elapsed) > delay && (climb_onto_bottom(false) || try_barrier_jump())) {
                release_hit(feeler);
                return false;
            }
        } else {
            barrier_timed_ = false;
        }
        if (input_->pad_primary_.shoulder_right_a_.held_down_ && !jump_held_ && climb_onto_bottom(true)) {
            release_hit(feeler);
            return false;
        }
        if (halted(this)) {
            release_hit(feeler);
            return false;
        }
        // On steep ground: pushed down the slope by push_strength.
        if (state_ == 0 && floor_normal_.y < kSteepGround) {
            const maths::Vec4& ground = floor_normal_;
            float x = ground.x;
            float y = 0.0f;
            float z = ground.z;
            float w = ground.w;
            double length = std::sqrt(static_cast<double>(x) * x + static_cast<double>(z) * z);
            if (length > 0.0) {
                double inverse = 1.0 / length;
                x = static_cast<float>(x * inverse);
                y = static_cast<float>(0.0 * inverse);
                z = static_cast<float>(z * inverse);
            }
            double push = static_cast<double>(walk_parameter(kPushStrength)) * frame_length_;
            position.x = static_cast<float>(x * push + position.x);
            position.y = static_cast<float>(y * push + position.y);
            position.z = static_cast<float>(z * push + position.z);
            position.w = static_cast<float>(w * push + position.w);
        }
    }
    if (!any) {
        release_hit(feeler);
        return false;
    }

    // Pushed out of what is close: within push_feeler_length (as a part of feeler_length) of a contact.
    for (int i = 0; i < 8; ++i) {
        const auto& contact = contact_at(this, i);
        if (!contact.hit)
            continue;
        float distance = contact.feeler.reach_;
        if (walk_parameter(kPushFeelerLength) / static_cast<double>(reach) > distance) {
            double push = static_cast<double>(walk_parameter(kPushStrength)) * frame_length_;
            position.x = static_cast<float>(contact.normal.x * push + position.x);
            position.y = static_cast<float>(contact.normal.y * push + position.y);
            position.z = static_cast<float>(contact.normal.z * push + position.z);
            position.w = static_cast<float>(contact.normal.w * push + position.w);
        }
    }

    // Only the contacts it is moving into (faster than they move away) count.
    bool pressing = false;
    for (int i = 0; i < 8; ++i) {
        auto& contact = contact_at(this, i);
        if (!contact.hit)
            continue;
        double into =
            (static_cast<double>(velocity.z) * contact.normal.z + static_cast<double>(contact.normal.x) * velocity.x) +
            static_cast<double>(velocity.y) * contact.normal.y;
        if (into >= contact.object_speed_)
            contact.hit = false;
        else
            pressing = true;
    }
    if (!pressing) {
        release_hit(feeler);
        return false;
    }

    // In a corner (two of them facing apart): stopped.
    for (int a = 0; a < 8; ++a) {
        const auto& first = contact_at(this, a);
        if (!first.hit)
            continue;
        for (int b = a + 1; b < 8; ++b) {
            const auto& second = contact_at(this, b);
            if (!second.hit)
                continue;
            double apart = (static_cast<double>(first.normal.z) * second.normal.z +
                            static_cast<double>(first.normal.y) * second.normal.y) +
                           static_cast<double>(second.normal.x) * first.normal.x;
            if (apart <= 0.0) {
                halt(this);
                release_hit(feeler);
                return true;
            }
        }
    }

    // Slid along them: the part of the velocity into each taken off (the fall's first, then the front ones'); stopped
    // if that turns it back.
    static const int kOrder[8] = {7, 4, 3, 5, 2, 6, 1, 0};
    double x = velocity.x; // (x and w are kept unrounded, as on the x87's stack)
    float y = velocity.y;
    float z = velocity.z;
    double w = velocity.w;
    for (int i : kOrder) {
        const auto& contact = contact_at(this, i);
        if (!contact.hit)
            continue;
        const maths::Vec4& normal = contact.normal;
        double into = (static_cast<double>(z) * normal.z + static_cast<double>(y) * normal.y) + x * normal.x;
        if (into >= contact.object_speed_)
            continue;
        double excess = into - contact.object_speed_;
        auto off_w = static_cast<float>(excess * normal.w);
        auto off_z = static_cast<float>(excess * normal.z);
        auto nudge_along_y = static_cast<float>(excess * normal.y);
        x -= excess * normal.x;
        y = y - nudge_along_y;
        z = z - off_z;
        w -= off_w;
        double still = (static_cast<double>(z) * velocity.z + static_cast<double>(y) * velocity.y) + x * velocity.x;
        if (still <= 0.0) {
            halt(this);
            release_hit(feeler);
            return true;
        }
    }

    // No faster than it was going along the new direction.
    auto w_rounded = static_cast<float>(w);
    double length = std::sqrt((x * x + static_cast<double>(z) * z) + static_cast<double>(y) * y);
    auto length_rounded = static_cast<float>(length);
    double new_x;
    float new_y, new_z, new_w;
    if (length <= kStillLength) {
        new_x = 0.0;
        new_y = 0.0f;
        new_z = 0.0f;
        new_w = 1.0f;
    } else {
        double inverse = 1.0 / length_rounded;
        auto unit_x = static_cast<float>(inverse * x);
        auto unit_y = static_cast<float>(y * inverse);
        auto unit_z = static_cast<float>(z * inverse);
        auto unit_w = static_cast<float>(w * inverse);
        auto along =
            static_cast<float>((static_cast<double>(unit_z) * velocity.z + static_cast<double>(unit_y) * velocity.y) +
                               static_cast<double>(unit_x) * velocity.x);
        if (length_rounded > along) {
            new_w = unit_w * along;
            new_z = unit_z * along;
            new_y = unit_y * along;
            new_x = static_cast<double>(unit_x) * along;
        } else {
            new_x = x;
            new_y = y;
            new_z = z;
            new_w = w_rounded;
        }
    }

    // Blocked: slowed to under half its speed.
    double before = (static_cast<double>(velocity.x) * velocity.x + static_cast<double>(velocity.y) * velocity.y) +
                    static_cast<double>(velocity.z) * velocity.z;
    if (before <= kStillSqr) {
        blocked_ = false;
    } else {
        double after = (new_x * new_x + static_cast<double>(new_z) * new_z) + static_cast<double>(new_y) * new_y;
        blocked_ = after / before < kMostlyStopped;
    }
    velocity = {static_cast<float>(new_x), new_y, new_z, new_w};

    // The run animation's speed follows (kept at least min_anim_run_speed) unless it is already that slow.
    auto speed = static_cast<float>(
        std::sqrt((static_cast<double>(velocity.x) * velocity.x + static_cast<double>(velocity.y) * velocity.y) +
                  static_cast<double>(velocity.z) * velocity.z));
    if (speed_ > walk_parameter(kMinAnimRunSpeed)) {
        speed_ = speed;
        float minimum = walk_parameter(kMinAnimRunSpeed);
        speed_ = minimum > speed ? minimum : speed;
    }
    release_hit(feeler);
    return true;
}

} // namespace objects

