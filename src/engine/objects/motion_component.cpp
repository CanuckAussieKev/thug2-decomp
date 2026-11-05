// The motion component (GameObjects::KinematicsModule, motion.h): its life, the script's wait on it, and its simpler
// member functions - storing positions and nodes, constant height, random links, hovering, speed, jumping.

#include <cstddef>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <gsl/pointers>
#include <memory>

#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "base/maths/random.h"
#include "base/maths/trig.h"
#include "engine/objects/emitter_object.h"
#include "engine/objects/lock_obj.h"
#include "engine/objects/motion.h"
#include "engine/objects/motion_internal.h"
#include "engine/objects/path_follower.h"
#include "engine/script/cscript.h"
#include "engine/script/host.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "engine/sys/game_clock.h"
#include "engine/sys/stopwatch.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/camera_lookaround.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "base/maths/maths.h"

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
    if (turn_.blender) {
        memory::Hub::dealloc(turn_.blender); // (no destructor run)
        turn_.blender = nullptr;
    }
    if (FollowTrail* trail = leader_.trail) {
        // (its destructor: the empty 0x005480A0)
        memory::Hub::dealloc(trail);
        leader_.trail = nullptr;
    }
}

// 0x00423D00
void KinematicsModule::get_reset()
{
    const maths::Vec4 rest = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    axes_.target = rest;
    axes_.done = rest;
    axes_.max_speed = rest;
    axes_.rates = rest;
    axes_.acceleration = rest;
    axes_.deceleration = rest;
    move_start_ = rest;
    move_.target = rest;
    move_.distance = 0.0F;
    move_.done = 0.0F;
    move_speed_ = 0.0F;
    move_.max_speed = 0.0F;
    move_.acceleration = 0.0F;
    path_.acceleration = 0.0F;
    path_.deceleration = 0.0F;
    path_.max_speed = 0.0F;
    path_.speed = 0.0F;
    path_.stop_distance = 0.0F;
    path_stop_.covered = 0.0F;
    path_stop_.start_speed = 0.0F;
    path_stop_.min_speed = 0.0F;
    turn_.time = 0.0F;
    turn_.elapsed = 0.0F;
    motion_flags_ = 0;
    motion_flags2_ = 0;
    for (maths::Vec4& corner : ground_.triangle) {
        corner = rest;
    }
    for (auto& triangle : ground_.triangles) {
        for (maths::Vec4& corner : triangle) {
            corner = rest;
        }
    }
}

// 0x00423C90
void KinematicsModule::init_path_follower(GameEntity* owner)
{
    if (path_follower_) {
        return;
    }
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
        if ((motion_flags_ & kMotionMoveMask) == 0U) {
            script::host().stop_waiting(script);
        }
        break;
    case script::BlockReason::kJump:
        if ((motion_flags_ & kMotionJumping) == 0U) {
            script::host().stop_waiting(script);
        }
        break;
    case script::BlockReason::kPathStop:
        if (!(path_.speed > 0.0F)) { // (a NaN stops it too)
            script::host().stop_waiting(script);
        }
        break;
    case script::BlockReason::kRotate:
        if ((motion_flags_ & kMotionRotateMask) == 0U) {
            script::host().stop_waiting(script);
        }
        break;
    default: // (6: never)
        break;
    }
}

// 0x004274D0
int KinematicsModule::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script)
{
    if (name != kObjMoveToNode) {
        return 2;
    }
    glide_to_node(params, script);
    return 1;
}

// --- the update's parts ---

// 0x00423740
void KinematicsModule::update_hover()
{
    auto period = static_cast<uint32_t>(hover_.period);
    const uint32_t in_period = static_cast<uint32_t>(game_clock()) % period;
    const int doubled = static_cast<int>(in_period * 2);
    auto phase = static_cast<double>(doubled);
    if (doubled < 0) {
        phase += maths::kUnsignedWrap; // (the count taken as unsigned)
    }
    phase = phase * static_cast<double>(maths::kPi) / static_cast<double>(hover_.period); // (pi)
    auto offset = static_cast<float>(std::sin(phase) * static_cast<double>(hover_.height));
    hover_.bob = offset;
    const float height = entity()->at_point().y;
    hover_.base = height;
    entity()->at_point().y = static_cast<float>(static_cast<double>(offset) + height);
}

// --- script member functions ---

// 0x004237D0
bool KinematicsModule::obj_store_pos(script::Struct* /*params*/, script::Script* /*script*/)
{
    stored_.pos = entity()->at_point();
    return true;
}

// 0x00423810
bool KinematicsModule::obj_store_node(script::Struct* /*params*/, script::Script* /*script*/)
{
    stored_.node = path_follower_->list_link();
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
    if (links != 0U) {
        script->args()->put_int(checksums::Name{kLink}, maths::rnd(static_cast<int>(links)) + 1);
    }
    return true;
}

// 0x004238D0
bool KinematicsModule::obj_hover(script::Struct* params, script::Script* /*script*/)
{
    if (params->has_bare_flag(kOff)) {
        entity()->at_point().y = hover_.base;
        motion_flags_ &= ~kMotionHovering;
        return true;
    }
    hover_.bob = 0.0F;
    params->fetch_float(kAmp, &hover_.height, 0);
    float frequency = 1.0F;
    params->fetch_float(kFreq, &frequency, 0);
    int period = truncate(static_cast<double>(kMillisecondsPerSecond) / frequency); // (1000)
    hover_.period = period;
    // (a tenth either way: period - period / 10 + Rnd(period * 2 / 10), in the original's 32-bit arithmetic)
    const int twice = static_cast<int>(static_cast<uint32_t>(period) * 20U);
    const int random = maths::rnd(twice / 10);
    period = static_cast<int>(static_cast<uint32_t>(hover_.period - (hover_.period / kBobSpreadDivisor)) +
                              static_cast<uint32_t>(random));
    hover_.period = period;
    if (period <= 0) {
        hover_.period = 1;
    }
    motion_flags_ |= kMotionHovering;
    hover_.base = entity()->at_point().y;
    return true;
}

// 0x004239F0
bool KinematicsModule::obj_get_speed(script::Struct* params, script::Script* script)
{
    float speed = 0.0F;
    if ((motion_flags_ & kMotionMoving) != 0U) {
        speed = move_speed_;
    } else if (entity()->fetch_module(kLockObj) &&
               ((core::down_cast<PinnedObjectModule>(entity()->fetch_module(kLockObj))->lock_flags() &
                 kLockFlagLocked) != 0)) {
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
    if (params->fetch_float(kLandHeight, &leap_touchdown_y_, 0)) {
        jump_has_land_height_ = true;
    }
    motion_flags_ |= kMotionJumping;
    return true;
}

// NOT ORIGINAL
void KinematicsModule::check_layout()
{
    // Each field at the original's offset (32-bit builds; the size counts on the heap it is made on).
    struct FieldAt {
        std::size_t now;
        std::size_t original;
    };
    constexpr std::array kLayout{
        FieldAt{.now = offsetof(KinematicsModule, motion_flags_), .original = 0x18},
        FieldAt{.now = offsetof(KinematicsModule, motion_flags2_), .original = 0x1C},
        FieldAt{.now = offsetof(KinematicsModule, move_speed_), .original = 0x20},
        FieldAt{.now = offsetof(KinematicsModule, path_.acceleration), .original = 0x24},
        FieldAt{.now = offsetof(KinematicsModule, path_.deceleration), .original = 0x28},
        FieldAt{.now = offsetof(KinematicsModule, path_.max_speed), .original = 0x2C},
        FieldAt{.now = offsetof(KinematicsModule, path_.speed), .original = 0x30},
        FieldAt{.now = offsetof(KinematicsModule, path_.stop_distance), .original = 0x34},
        FieldAt{.now = offsetof(KinematicsModule, ground_.above), .original = 0x38},
        FieldAt{.now = offsetof(KinematicsModule, ground_.below), .original = 0x3C},
        FieldAt{.now = offsetof(KinematicsModule, ground_.triangle), .original = 0x40},
        FieldAt{.now = offsetof(KinematicsModule, ground_.height_only), .original = 0x70},
        FieldAt{.now = offsetof(KinematicsModule, ground_.triangles), .original = 0x74},
        FieldAt{.now = offsetof(KinematicsModule, path_group_), .original = 0x104},
        FieldAt{.now = offsetof(KinematicsModule, move_.target), .original = 0x108},
        FieldAt{.now = offsetof(KinematicsModule, move_.distance), .original = 0x118},
        FieldAt{.now = offsetof(KinematicsModule, move_.done), .original = 0x11C},
        FieldAt{.now = offsetof(KinematicsModule, move_.max_speed), .original = 0x120},
        FieldAt{.now = offsetof(KinematicsModule, move_.acceleration), .original = 0x124},
        FieldAt{.now = offsetof(KinematicsModule, move_.arrive_node), .original = 0x128},
        FieldAt{.now = offsetof(KinematicsModule, axes_.target), .original = 0x12C},
        FieldAt{.now = offsetof(KinematicsModule, axes_.done), .original = 0x13C},
        FieldAt{.now = offsetof(KinematicsModule, axes_.max_speed), .original = 0x14C},
        FieldAt{.now = offsetof(KinematicsModule, axes_.rates), .original = 0x15C},
        FieldAt{.now = offsetof(KinematicsModule, axes_.acceleration), .original = 0x16C},
        FieldAt{.now = offsetof(KinematicsModule, axes_.deceleration), .original = 0x17C},
        FieldAt{.now = offsetof(KinematicsModule, move_start_), .original = 0x18C},
        FieldAt{.now = offsetof(KinematicsModule, ground_speed_), .original = 0x19C},
        FieldAt{.now = offsetof(KinematicsModule, ground_offset_), .original = 0x1A0},
        FieldAt{.now = offsetof(KinematicsModule, path_stop_.covered), .original = 0x1A4},
        FieldAt{.now = offsetof(KinematicsModule, path_stop_.start_speed), .original = 0x1A8},
        FieldAt{.now = offsetof(KinematicsModule, path_stop_.min_speed), .original = 0x1AC},
        FieldAt{.now = offsetof(KinematicsModule, stored_.pos), .original = 0x1B0},
        FieldAt{.now = offsetof(KinematicsModule, stored_.node), .original = 0x1C0},
        FieldAt{.now = offsetof(KinematicsModule, turn_.time), .original = 0x1C4},
        FieldAt{.now = offsetof(KinematicsModule, turn_.elapsed), .original = 0x1C8},
        FieldAt{.now = offsetof(KinematicsModule, turn_.blender), .original = 0x1CC},
        FieldAt{.now = offsetof(KinematicsModule, hover_.base), .original = 0x1D0},
        FieldAt{.now = offsetof(KinematicsModule, hover_.bob), .original = 0x1D4},
        FieldAt{.now = offsetof(KinematicsModule, hover_.height), .original = 0x1D8},
        FieldAt{.now = offsetof(KinematicsModule, hover_.period), .original = 0x1DC},
        FieldAt{.now = offsetof(KinematicsModule, step_seconds_), .original = 0x1E0},
        FieldAt{.now = offsetof(KinematicsModule, own_node_name_), .original = 0x1E4},
        FieldAt{.now = offsetof(KinematicsModule, list_link_), .original = 0x1E8},
        FieldAt{.now = offsetof(KinematicsModule, path_follower_), .original = 0x1EC},
        FieldAt{.now = offsetof(KinematicsModule, leader_.trail), .original = 0x1F0},
        FieldAt{.now = offsetof(KinematicsModule, leader_.leave_y_unaffected), .original = 0x1F4},
        FieldAt{.now = offsetof(KinematicsModule, leader_.pad5), .original = 0x1F5},
        FieldAt{.now = offsetof(KinematicsModule, jump_speed_), .original = 0x1F8},
        FieldAt{.now = offsetof(KinematicsModule, leap_pull_), .original = 0x1FC},
        FieldAt{.now = offsetof(KinematicsModule, leap_direction_), .original = 0x200},
        FieldAt{.now = offsetof(KinematicsModule, jump_has_heading_), .original = 0x210},
        FieldAt{.now = offsetof(KinematicsModule, jump_pos_), .original = 0x214},
        FieldAt{.now = offsetof(KinematicsModule, jump_start_speed_), .original = 0x224},
        FieldAt{.now = offsetof(KinematicsModule, jump_col_above_), .original = 0x228},
        FieldAt{.now = offsetof(KinematicsModule, jump_col_below_), .original = 0x22C},
        FieldAt{.now = offsetof(KinematicsModule, unknown230_), .original = 0x230},
        FieldAt{.now = offsetof(KinematicsModule, jump_has_land_height_), .original = 0x234},
        FieldAt{.now = offsetof(KinematicsModule, leap_touchdown_y_), .original = 0x238},
        FieldAt{.now = offsetof(KinematicsModule, jump_start_), .original = 0x23C},
        FieldAt{.now = sizeof(KinematicsModule), .original = 0x24C},
    };
    static_assert(sizeof(void*) != 4 || std::ranges::all_of(kLayout, [](const FieldAt& field) {
                      return field.now == field.original;
                  }),
                  "layout must match the original");
}

} // namespace objects

