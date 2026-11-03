// The motion component (GameObjects::KinematicsModule, motion.h): its life, the script's wait on it, and its simpler
// member functions - storing positions and nodes, constant height, random links, hovering, speed, jumping.

#include <cmath>
#include <cstdint>
#include <gsl/pointers>
#include <memory>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/maths/random.h"
#include "engine/maths/trig.h"
#include "engine/objects/emitter_object.h"
#include "engine/objects/lock_obj.h"
#include "engine/objects/motion.h"
#include "engine/objects/motion_internal.h"
#include "engine/objects/path_follower.h"
#include "engine/script/cscript.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "platform/game_clock.h"
#include "platform/stopwatch.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/maths/maths.h"

namespace objects {

using namespace motion_detail;

namespace {
constexpr int kBobSpreadDivisor = 10;               // the bob period varies by a tenth
constexpr float kJumpSpeed = 150.0F;
constexpr float kLeapPull = 360.0F; // (negated)
constexpr float kJumpProbeAbove = 6.0F;
constexpr float kJumpProbeBelow = 6.0F;
constexpr float kMillisecondsPerSecond = 1000.0F;

// The game clock with its offset (0x0044ECE0 + 0x00696D60).
int game_clock() { return frame_clock::game_clock_elapsed() + clock_offset(); }

} // namespace

// --- life ---

// 0x00426650
gsl::owner<ObjectModule*> KinematicsModule::fabricate() { return new KinematicsModule; }

// 0x00424AA0
KinematicsModule::KinematicsModule()
{
    set_kind_tag(kType);
    get_reset();
}

// 0x00424AE0 (0x004266F0 the deleting one)
KinematicsModule::~KinematicsModule()
{
    if (path_follower_) {
        memory::destroy_then_clear(path_follower_);
    }
    if (turn_ptr_) {
        memory::Hub::dealloc(turn_ptr_); // (no destructor run)
        turn_ptr_ = nullptr;
    }
    if (FollowTrail* trail = trail_) {
        // (its destructor: the empty 0x005480A0)
        memory::Hub::dealloc(trail);
        trail_ = nullptr;
    }
}

// 0x00423D00
void KinematicsModule::get_reset()
{
    const maths::Vec4 rest = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    rot_target_ = rest;
    rot_done_ = rest;
    rot_max_speed_ = rest;
    spin_rates_ = rest;
    rot_acceleration_ = rest;
    rot_deceleration_ = rest;
    move_start_ = rest;
    move_target_ = rest;
    move_distance_ = 0.0F;
    move_done_ = 0.0F;
    move_speed_ = 0.0F;
    move_max_speed_ = 0.0F;
    move_acceleration_ = 0.0F;
    path_acceleration_ = 0.0F;
    path_deceleration_ = 0.0F;
    path_max_speed_ = 0.0F;
    path_speed_ = 0.0F;
    path_stop_distance_ = 0.0F;
    path_stop_covered_ = 0.0F;
    path_stop_start_speed_ = 0.0F;
    path_min_stop_speed_ = 0.0F;
    turn_time_ = 0.0F;
    turn_elapsed_ = 0.0F;
    motion_flags_ = 0;
    motion_flags2_ = 0;
    for (maths::Vec4& corner : ground_triangle_)
        corner = rest;
    for (auto& triangle : ground_triangles_)
        for (maths::Vec4& corner : triangle)
            corner = rest;
}

// 0x00423C90
void KinematicsModule::init_path_follower(GameEntity* owner)
{
    if (path_follower_)
        return;
    path_follower_ = std::make_unique<PathFollower>(owner);
}

// --- the vtable ---

// 0x00426720
void KinematicsModule::configure_from_data(script::Struct* params)
{
    uint32_t name = 0;
    params->read_hash_value(kName, &name, 0);
    list_link_ = name;
    own_node_name_ = name;
    face_node(params);
}

// 0x004236C0
void KinematicsModule::step_wait_state(script::Script* script, uint32_t /*arg*/)
{
    switch (script->suspend_reason()) {
    case script::BlockReason::kMove:
        if (!(motion_flags_ & kMotionMoveMask))
            script->stop_waiting();
        break;
    case script::BlockReason::kJump:
        if (!(motion_flags_ & kMotionJumping))
            script->stop_waiting();
        break;
    case script::BlockReason::kPathStop:
        if (!(path_speed_ > 0.0F)) // (a NaN stops it too)
            script->stop_waiting();
        break;
    case script::BlockReason::kRotate:
        if (!(motion_flags_ & kMotionRotateMask))
            script->stop_waiting();
        break;
    default: // (6: never)
        break;
    }
}

// 0x004274D0
int KinematicsModule::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script)
{
    if (name != kObjMoveToNode)
        return 2;
    glide_to_node(params, script);
    return 1;
}

// --- the update's parts ---

// 0x00423740
void KinematicsModule::update_hover()
{
    auto period = static_cast<uint32_t>(bob_period_);
    const uint32_t in_period = static_cast<uint32_t>(game_clock()) % period;
    const int doubled = static_cast<int>(in_period * 2);
    auto phase = static_cast<double>(doubled);
    if (doubled < 0)
        phase += maths::kUnsignedWrap; // (the count taken as unsigned)
    phase = phase * static_cast<double>(maths::kPi) / static_cast<double>(bob_period_); // (pi)
    auto offset = static_cast<float>(std::sin(phase) * static_cast<double>(bob_height_));
    bob_now_ = offset;
    const float height = entity()->at_point().y;
    hover_base_ = height;
    entity()->at_point().y = static_cast<float>(static_cast<double>(offset) + height);
}

// --- script member functions ---

// 0x004237D0
bool KinematicsModule::obj_store_pos(script::Struct* /*params*/, script::Script* /*script*/)
{
    stored_pos_ = entity()->at_point();
    return true;
}

// 0x00423810
bool KinematicsModule::obj_store_node(script::Struct* /*params*/, script::Script* /*script*/)
{
    saved_node_id_ = path_follower_->list_link();
    return true;
}

// 0x00423830
bool KinematicsModule::obj_set_constant_height(script::Struct* params, script::Script* /*script*/)
{
    if (params->has_bare_flag(kOff)) {
        motion_flags2_ &= ~kMotion2ConstantHeight;
        return true;
    }
    motion_flags2_ |= kMotion2ConstantHeight;
    params->fetch_float(0U, &entity()->at_point().y, 0);
    return true;
}

// 0x00423890
bool KinematicsModule::obj_get_random_link(script::Struct* /*params*/, script::Script* script) const
{
    const uint32_t links = level_nodes::get_link_count(list_link_);
    if (links)
        script->args()->put_int(checksums::Name{kLink}, maths::rnd(static_cast<int>(links)) + 1);
    return true;
}

// 0x004238D0
bool KinematicsModule::obj_hover(script::Struct* params, script::Script* /*script*/)
{
    if (params->has_bare_flag(kOff)) {
        entity()->at_point().y = hover_base_;
        motion_flags_ &= ~kMotionHovering;
        return true;
    }
    bob_now_ = 0.0F;
    params->fetch_float(kAmp, &bob_height_, 0);
    float frequency = 1.0F;
    params->fetch_float(kFreq, &frequency, 0);
    int period = truncate(static_cast<double>(kMillisecondsPerSecond) / frequency); // (1000)
    bob_period_ = period;
    // (a tenth either way: period - period / 10 + Rnd(period * 2 / 10), in the original's 32-bit arithmetic)
    const int twice = static_cast<int>(static_cast<uint32_t>(period) * 20U);
    const int random = maths::rnd(twice / 10);
    period = static_cast<int>(static_cast<uint32_t>(bob_period_ - (bob_period_ / kBobSpreadDivisor)) +
                              static_cast<uint32_t>(random));
    bob_period_ = period;
    if (period <= 0)
        bob_period_ = 1;
    motion_flags_ |= kMotionHovering;
    hover_base_ = entity()->at_point().y;
    return true;
}

// 0x004239F0
bool KinematicsModule::obj_get_speed(script::Struct* params, script::Script* script)
{
    float speed = 0.0F;
    if (motion_flags_ & kMotionMoving) {
        speed = move_speed_;
    } else if (entity()->fetch_module(kLockObj) &&
               (core::down_cast<PinnedObjectModule>(entity()->fetch_module(kLockObj))->lock_flags() & kLockFlagLocked)) {
        auto dx = static_cast<float>(static_cast<double>(entity()->at_point().x) - entity()->old_pos().x);
        auto dy = static_cast<float>(static_cast<double>(entity()->at_point().y) - entity()->old_pos().y);
        auto dz = static_cast<float>(static_cast<double>(entity()->at_point().z) - entity()->old_pos().z);
        const double frame = frame_clock::get_slomo_frame_seconds();
        if (!(frame == 0.0)) {
            const double moved = std::sqrt(((static_cast<double>(dz) * dz) + (static_cast<double>(dy) * dy)) +
                                           (static_cast<double>(dx) * dx));
            speed = static_cast<float>(moved / frame);
        }
    } else {
        const maths::Vec4& v = entity()->vel();
        speed = static_cast<float>(std::sqrt(((static_cast<double>(v.x) * v.x) + (static_cast<double>(v.y) * v.y)) +
                                             (static_cast<double>(v.z) * v.z)));
    }
    if (!params->has_bare_flag(kIps)) {
        // (inches per second to feet per second, else to miles per hour)
        const float scale =
            params->has_bare_flag(kFps) ? get_bits(0x3DAAAAAB) : get_bits(0x3D68BA2E); // 1 / 12, 1 / 17.6
        speed = static_cast<float>(static_cast<double>(speed) * scale);
    }
    script->args()->push_float("Speed", speed);
    return true;
}

// 0x00423B30
bool KinematicsModule::obj_jump(script::Struct* params, script::Script* /*script*/)
{
    jump_speed_ = kJumpSpeed;
    params->fetch_float(kSpeed, &jump_speed_, 0);
    jump_start_speed_ = jump_speed_;
    leap_pull_ = -kLeapPull;
    params->fetch_float(kGravity, &leap_pull_, 0);
    jump_start_ = entity()->at_point();
    jump_col_above_ = kJumpProbeAbove;
    params->fetch_float(kColDistAbove, &jump_col_above_, 0);
    jump_col_below_ = kJumpProbeBelow;
    params->fetch_float(kColDistBelow, &jump_col_below_, 0);
    bool heading = params->fetch_vec(kHeading, &leap_direction_, 0);
    if (!heading && params->has_bare_flag(kUseCurrentHeading)) {
        leap_direction_ = core::at(entity()->display_matrix().row, 2);
        heading = true;
    }
    if (heading) {
        // (the heading's y scales the speed up, then goes)
        jump_has_heading_ = true;
        jump_speed_ = static_cast<float>(static_cast<double>(leap_direction_.y) * jump_speed_);
        leap_direction_.y = 0.0F;
    } else {
        jump_has_heading_ = false;
    }
    jump_has_land_height_ = false;
    if (params->fetch_float(kLandHeight, &leap_touchdown_y_, 0))
        jump_has_land_height_ = true;
    motion_flags_ |= kMotionJumping;
    return true;
}

} // namespace objects

