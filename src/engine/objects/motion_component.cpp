// The motion component (GameObjects::KinematicsModule, motion.h): its life, the script's wait on it, and its simpler
// member functions - storing positions and nodes, constant height, random links, hovering, speed, jumping.

#include <cmath>
#include <cstdint>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/maths/random.h"
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

namespace objects {

using namespace motion_detail;

namespace {

// Script wait types a component decides (CScript::suspend_reason_ 5-10; cscript.h).
constexpr uint32_t kWaitMove = 5;
constexpr uint32_t kWaitJump = 7;
constexpr uint32_t kWaitPathStop = 8;
constexpr uint32_t kWaitRotate = 9;

// The game clock with its offset (0x0044ECE0 + 0x00696D60).
int game_clock() { return frame_clock::game_clock_elapsed() + g_clock_offset; }

} // namespace

// --- life ---

// 0x00426650
memory::owner<ObjectModule*> KinematicsModule::fabricate() { return new KinematicsModule; }

// 0x00424AA0
KinematicsModule::KinematicsModule()
{
    kind_tag_ = kType;
    get_reset();
}

// 0x00424AE0 (0x004266F0 the deleting one)
KinematicsModule::~KinematicsModule()
{
    if (path_follower_) {
        delete path_follower_;
        path_follower_ = nullptr;
    }
    if (turn_ptr_) {
        memory::Hub::get_pull()->dealloc(turn_ptr_); // (no destructor run)
        turn_ptr_ = nullptr;
    }
    if (FollowTrail* trail = trail_) {
        // (its destructor: the empty 0x005480A0)
        memory::Hub::get_pull()->dealloc(trail);
        trail_ = nullptr;
    }
}

// 0x00423D00
void KinematicsModule::get_reset()
{
    const maths::Vec4 rest = {0.0f, 0.0f, 0.0f, 1.0f};
    rot_target_ = rest;
    rot_done_ = rest;
    rot_max_speed_ = rest;
    spin_rates_ = rest;
    rot_acceleration_ = rest;
    rot_deceleration_ = rest;
    move_start_ = rest;
    move_target_ = rest;
    move_distance_ = 0.0f;
    move_done_ = 0.0f;
    move_speed_ = 0.0f;
    move_max_speed_ = 0.0f;
    move_acceleration_ = 0.0f;
    path_acceleration_ = 0.0f;
    path_deceleration_ = 0.0f;
    path_max_speed_ = 0.0f;
    path_speed_ = 0.0f;
    path_turn_distance_ = 0.0f;
    path_speed_1a_4_ = 0.0f;
    path_speed_1a_8_ = 0.0f;
    path_min_stop_speed_ = 0.0f;
    turn_time_ = 0.0f;
    turn_elapsed_ = 0.0f;
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
    path_follower_ = new PathFollower(owner);
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
    switch (script->suspend_reason_) {
    case kWaitMove:
        if (!(motion_flags_ & kMotionMoveMask))
            script->stop_waiting();
        break;
    case kWaitJump:
        if (!(motion_flags_ & kMotionJumping))
            script->stop_waiting();
        break;
    case kWaitPathStop:
        if (!(path_speed_ > 0.0f)) // (a NaN stops it too)
            script->stop_waiting();
        break;
    case kWaitRotate:
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
    uint32_t in_period = static_cast<uint32_t>(game_clock()) % period;
    int doubled = static_cast<int>(in_period * 2);
    auto phase = static_cast<double>(doubled);
    if (doubled < 0)
        phase += 4294967296.0; // (the count taken as unsigned)
    phase = phase * static_cast<double>(get_bits(0x40490FDB)) / static_cast<double>(bob_period_); // (pi)
    auto offset = static_cast<float>(std::sin(phase) * static_cast<double>(bob_height_));
    bob_now_ = offset;
    float height = entity_->at_point_.y;
    hover_base_ = height;
    entity_->at_point_.y = static_cast<float>(static_cast<double>(offset) + height);
}

// --- script member functions ---

// 0x004237D0
bool KinematicsModule::obj_store_pos(script::Struct*, script::Script*)
{
    stored_pos_ = entity_->at_point_;
    return true;
}

// 0x00423810
bool KinematicsModule::obj_store_node(script::Struct*, script::Script*)
{
    saved_node_id_ = path_follower_->list_link_;
    return true;
}

// 0x00423830
bool KinematicsModule::obj_set_constant_height(script::Struct* params, script::Script*)
{
    if (params->has_bare_flag(kOff)) {
        motion_flags2_ &= ~kMotion2ConstantHeight;
        return true;
    }
    motion_flags2_ |= kMotion2ConstantHeight;
    params->fetch_float(0u, &entity_->at_point_.y, 0);
    return true;
}

// 0x00423890
bool KinematicsModule::obj_get_random_link(script::Struct*, script::Script* script)
{
    uint32_t links = level_nodes::get_link_count(list_link_);
    if (links)
        script->args_->put_int(kLink, maths::rnd(static_cast<int>(links)) + 1);
    return true;
}

// 0x004238D0
bool KinematicsModule::obj_hover(script::Struct* params, script::Script*)
{
    if (params->has_bare_flag(kOff)) {
        entity_->at_point_.y = hover_base_;
        motion_flags_ &= ~kMotionHovering;
        return true;
    }
    bob_now_ = 0.0f;
    params->fetch_float(kAmp, &bob_height_, 0);
    float frequency = 1.0f;
    params->fetch_float(kFreq, &frequency, 0);
    int period = truncate(static_cast<double>(get_bits(0x447A0000)) / frequency); // (1000)
    bob_period_ = period;
    // (a tenth either way: period - period / 10 + Rnd(period * 2 / 10), in the original's 32-bit arithmetic)
    int twice = static_cast<int>(static_cast<uint32_t>(period) * 20u);
    int random = maths::rnd(twice / 10);
    period = static_cast<int>(static_cast<uint32_t>(bob_period_ - bob_period_ / 10) + static_cast<uint32_t>(random));
    bob_period_ = period;
    if (period <= 0)
        bob_period_ = 1;
    motion_flags_ |= kMotionHovering;
    hover_base_ = entity_->at_point_.y;
    return true;
}

// 0x004239F0
bool KinematicsModule::obj_get_speed(script::Struct* params, script::Script* script)
{
    float speed = 0.0f;
    if (motion_flags_ & kMotionMoving) {
        speed = move_speed_;
    } else if (entity_->fetch_module(kLockObj) &&
               (static_cast<PinnedObjectModule*>(entity_->fetch_module(kLockObj))->lock_flags_ & kLockFlagLocked)) {
        auto dx = static_cast<float>(static_cast<double>(entity_->at_point_.x) - entity_->old_pos_.x);
        auto dy = static_cast<float>(static_cast<double>(entity_->at_point_.y) - entity_->old_pos_.y);
        auto dz = static_cast<float>(static_cast<double>(entity_->at_point_.z) - entity_->old_pos_.z);
        double frame = frame_clock::get_slomo_frame_seconds();
        if (!(frame == 0.0)) {
            double moved =
                std::sqrt((static_cast<double>(dz) * dz + static_cast<double>(dy) * dy) + static_cast<double>(dx) * dx);
            speed = static_cast<float>(moved / frame);
        }
    } else {
        const maths::Vec4& v = entity_->vel_;
        speed = static_cast<float>(std::sqrt((static_cast<double>(v.x) * v.x + static_cast<double>(v.y) * v.y) +
                                             static_cast<double>(v.z) * v.z));
    }
    if (!params->has_bare_flag(kIps)) {
        // (inches per second to feet per second, else to miles per hour)
        float scale = params->has_bare_flag(kFps) ? get_bits(0x3DAAAAAB) : get_bits(0x3D68BA2E); // 1 / 12, 1 / 17.6
        speed = static_cast<float>(static_cast<double>(speed) * scale);
    }
    script->args_->push_float("Speed", speed);
    return true;
}

// 0x00423B30
bool KinematicsModule::obj_jump(script::Struct* params, script::Script*)
{
    jump_speed_ = 150.0f;
    params->fetch_float(kSpeed, &jump_speed_, 0);
    jump_start_speed_ = jump_speed_;
    leap_pull_ = -360.0f;
    params->fetch_float(kGravity, &leap_pull_, 0);
    jump_start_ = entity_->at_point_;
    jump_col_above_ = 6.0f;
    params->fetch_float(kColDistAbove, &jump_col_above_, 0);
    jump_col_below_ = 6.0f;
    params->fetch_float(kColDistBelow, &jump_col_below_, 0);
    bool heading = params->fetch_vec(kHeading, reinterpret_cast<script::VectorValue*>(&leap_direction_), 0);
    if (!heading && params->has_bare_flag(kUseCurrentHeading)) {
        leap_direction_ = entity_->display_matrix_.row[2];
        heading = true;
    }
    if (heading) {
        // (the heading's y scales the speed up, then goes)
        jump_has_heading_ = true;
        jump_speed_ = static_cast<float>(static_cast<double>(leap_direction_.y) * jump_speed_);
        leap_direction_.y = 0.0f;
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

