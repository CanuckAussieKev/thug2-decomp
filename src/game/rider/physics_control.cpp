// The skater's physics control component (GameObjects::RiderBoardPhysics): see physics_control.h.

#include "game/rider/physics_control.h"

#include <cstdint>
#include <gsl/pointers>
#include <array>

#include "engine/maths/mat4.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "game/rider/core_physics.h"
#include "game/rider/rider.h"
#include "game/rider/skater_state.h"
#include "game/rider/walk_physics.h"
#include "platform/game_clock.h"
#include "game/rider/skater_camera_component.h"
#include "game/rider/walk_camera.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/core/down_cast.h"

namespace objects {

std::array<script::MemberFunctionEntry, 11> g_physics_control_member_functions{{
    {.callsign_ = 0x3522D612, .owner_kind_ = 0xD9151D4D}, {.callsign_ = 0x0595627C, .owner_kind_ = 0xD9151D4D}, {.callsign_ = 0x9569D5B1, .owner_kind_ = 0xD9151D4D}, {.callsign_ = 0xA887285A, .owner_kind_ = 0xD9151D4D},
    {.callsign_ = 0x9366A509, .owner_kind_ = 0xD9151D4D}, {.callsign_ = 0x04830E80, .owner_kind_ = 0xD9151D4D}, {.callsign_ = 0xD5A9F889, .owner_kind_ = 0xD9151D4D}, {.callsign_ = 0x27FDDEB7, .owner_kind_ = 0xD9151D4D},
    {.callsign_ = 0x174A6AD9, .owner_kind_ = 0xD9151D4D}, {.callsign_ = 0x246322C9, .owner_kind_ = 0xD9151D4D}, {.callsign_ = 0, .owner_kind_ = 0xD9151D4D}}};

namespace {

constexpr uint32_t kSkaterPhysicsControl = 0xD9151D4D;
constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29, kSkaterState = 0x43686585, kSkaterRotate = 0x52D61A0D,
                   kSkaterAdjustPhysics = 0x9F9CC949, kSkaterFinalizePhysics = 0x9B373E58, kWalk = 0x726E85AA,
                   kSkaterCamera = 0x5E43A604, kWalkCamera = 0xC7D37EC2;
constexpr uint32_t kCmdWalkToSkate = 0x5C038F9B, kCmdSkateToWalk = 0x6E8E39B3;
constexpr uint32_t kTimeSincePhysicsSwitch = 0x27F48E93, kPreviousPhysicsStateDuration = 0x24D061BA;

constexpr int kSkating = 0, kWalking = 1; // the state component's physics mode (+0x1C)
constexpr float kSecondsPerMs = 0.001f;   // 0x006461D8


int get_clock() { return frame_clock::game_clock_elapsed() + g_clock_offset; }

// Milliseconds as unsigned seconds (the original's unsigned conversion through the x87), rounded once to float.
float get_seconds(int ms) { return static_cast<float>(static_cast<double>(static_cast<uint32_t>(ms)) * kSecondsPerMs); }

void freeze_module(ObjectModule* component, bool suspend) { component->hold_off(suspend); }

// Calls into the other components: the core physics told it's leaving (0x004FDD50, not rewritten yet) / given the
// walk's state on coming back (resume_skating), the state component's physics mode (1: walking), the walk component
// told it's starting (Begin) / leaving (its model offset cleared), the cameras' state handed over (the skater
// camera's 0x004FA420 / 0x004F9A40, the walk camera's 0x00524FF0 / 0x00525060).
void core_leaving(skater::SkaterPhysicsModule* core) { core->leave_skating(); }
void core_returning(skater::SkaterPhysicsModule* core, bool on_ground, const FootMovement* grind_from,
                    const skater::AcidDrop* acid_drop)
{
    core->resume_skating(on_ground, grind_from, acid_drop);
}
void set_walking(RiderPoseState* state, bool walking) { state->set_physics_mode(walking ? 1 : 0); }
void walk_starting(ObjectModule* walk, bool from_ground) { core::down_cast<FootMovement>(walk)->get_begin(from_ground); }
void walk_leaving(ObjectModule* walk) { core::down_cast<FootMovement>(walk)->clear_model_offset(); }
// The skater camera's 0x004FA420 (save) / 0x004F9A40 (set), the walk camera's 0x00525060 (save) / 0x00524FF0 (restore).
void skater_camera_save(ObjectModule* camera, void* state)
{
    core::down_cast<RiderChaseView>(camera)->save_state(static_cast<WalkCameraState*>(state));
}
void skater_camera_set(ObjectModule* camera, void* state)
{
    core::down_cast<RiderChaseView>(camera)->change_phase(static_cast<const WalkCameraState*>(state));
}
void walk_camera_save(ObjectModule* camera, void* state)
{
    core::down_cast<FootView>(camera)->save_state(static_cast<WalkCameraState*>(state));
}
void walk_camera_restore(ObjectModule* camera, void* state)
{
    core::down_cast<FootView>(camera)->restore_state(static_cast<const WalkCameraState*>(state));
}

} // namespace

// 0x00517830
gsl::owner<ObjectModule*> RiderBoardPhysics::fabricate() { return new RiderBoardPhysics; }

// (the constructor: inlined in fabricate in the original)
RiderBoardPhysics::RiderBoardPhysics() { kind_tag_ = kSkaterPhysicsControl; }

// 0x005176A0
void RiderBoardPhysics::publish_method_table()
{
    script::register_member_function_table(g_physics_control_member_functions);
}

// 0x005176C0 (0x00517740 the deleting one)
RiderBoardPhysics::~RiderBoardPhysics() = default;

// 0x005176D0
void RiderBoardPhysics::configure_from_data(script::Struct* /*params*/) { is_halted_ = false; }

// 0x00517770
void RiderBoardPhysics::seal()
{
    core_physics_ = core::down_cast<skater::SkaterPhysicsModule>(entity_->fetch_module(kSkaterCorePhysics));
    pose_ = core::down_cast<RiderPoseState>(entity_->fetch_module(kSkaterState));
    pose_->physics_mode_ = -1;
}

// 0x005178B0
void RiderBoardPhysics::set_frozen(bool paused)
{
    is_halted_ = paused;
    if (!paused && (entity_->composite_flags_ & kCompositeFlagSuspended))
        return;
    switch (pose_->physics_mode_) {
    case kSkating:
        freeze_module(static_cast<ObjectModule*>(core_physics_), paused);
        freeze_module(entity_->fetch_module(kSkaterRotate), paused);
        freeze_module(entity_->fetch_module(kSkaterAdjustPhysics), paused);
        freeze_module(entity_->fetch_module(kSkaterFinalizePhysics), paused);
        break;
    case kWalking:
        freeze_module(entity_->fetch_module(kWalk), paused);
        break;
    default:
        break;
    }
}

// 0x00517940
void RiderBoardPhysics::dismount_to_walk()
{
    if (pose_->physics_mode_ == kWalking)
        return;
    previous_duration_ = get_clock() - switch_time_;
    switch_time_ = get_clock();
    auto* walk = core::down_cast<FootMovement>(entity_->fetch_module(kWalk));
    auto* camera = core::down_cast<GameEntity>(core::down_cast<Rider>(entity_)->get_skater_camera());
    ObjectModule* skater_camera = camera->fetch_module(kSkaterCamera);
    ObjectModule* walk_camera = camera->fetch_module(kWalkCamera);
    const int skater_state = core_physics_->state_->cur_phase_;
    core_leaving(core_physics_);
    freeze_module(static_cast<ObjectModule*>(core_physics_), true);
    freeze_module(entity_->fetch_module(kSkaterRotate), true);
    freeze_module(entity_->fetch_module(kSkaterAdjustPhysics), true);
    freeze_module(entity_->fetch_module(kSkaterFinalizePhysics), true);
    freeze_module(skater_camera, true);
    set_walking(pose_, true);
    freeze_module(walk, false);
    walk_starting(walk, skater_state == 0);
    freeze_module(walk_camera, false);
    alignas(16) std::array<uint8_t, 0x60> camera_state{};
    skater_camera_save(skater_camera, camera_state.data());
    walk_camera_restore(walk_camera, camera_state.data());
    if (is_halted_)
        set_frozen(is_halted_);
}

// 0x00517B20
void RiderBoardPhysics::remount_board()
{
    if (pose_->physics_mode_ == kSkating)
        return;
    previous_duration_ = get_clock() - switch_time_;
    switch_time_ = get_clock();
    auto* walk = core::down_cast<FootMovement>(entity_->fetch_module(kWalk));
    auto* camera = core::down_cast<GameEntity>(core::down_cast<Rider>(entity_)->get_skater_camera());
    ObjectModule* skater_camera = camera->fetch_module(kSkaterCamera);
    ObjectModule* walk_camera = camera->fetch_module(kWalkCamera);
    walk_leaving(walk);
    freeze_module(walk, true);
    freeze_module(walk_camera, true);
    set_walking(pose_, false);
    freeze_module(static_cast<ObjectModule*>(core_physics_), false);
    // The walk's ground state (0) and what it leaves for: 1 its grind, 2 its acid drop.
    const int hold = walk->leave_for_;
    const skater::AcidDrop* acid_drop = hold == 2 ? &walk->acid_drop_ : nullptr;
    const FootMovement* grind_from = hold == 1 ? walk : nullptr;
    core_returning(core_physics_, walk->state_ == 0, grind_from, acid_drop);
    freeze_module(entity_->fetch_module(kSkaterRotate), false);
    freeze_module(entity_->fetch_module(kSkaterAdjustPhysics), false);
    freeze_module(entity_->fetch_module(kSkaterFinalizePhysics), false);
    freeze_module(skater_camera, false);
    alignas(16) std::array<uint8_t, 0x60> camera_state{};
    walk_camera_save(walk_camera, camera_state.data());
    skater_camera_set(skater_camera, camera_state.data());
    if (is_halted_)
        set_frozen(is_halted_);
}

// 0x00517AC0
void RiderBoardPhysics::tick_frame()
{
    flag26_ = 0;
    if (!is_halted_)
        return;
    switch (pose_->physics_mode_) {
    case kSkating:
        entity_->display_matrix_ = core_physics_->display_matrix_;
        break;
    case kWalking:
        entity_->display_matrix_ = entity_->basis_frame_;
        break;
    default:
        break;
    }
    if (!(static_cast<ObjectModule*>(core_physics_)->flag_bits_ & kComponentFlagSuspended))
        set_frozen(true);
}

// 0x00517C80
int RiderBoardPhysics::invoke_method_by_name(uint32_t name, script::Struct* /*params*/, script::Script* /*script*/)
{
    switch (name) {
    case kCmdWalkToSkate:
        remount_board();
        return 1;
    case kCmdSkateToWalk:
        dismount_to_walk();
        return 1;
    default:
        return 2;
    }
}

// 0x00517A80
bool RiderBoardPhysics::pause_physics(script::Struct*, script::Script*)
{
    if (!is_halted_)
        set_frozen(true);
    return true;
}
// 0x00517AA0
bool RiderBoardPhysics::un_pause_physics(script::Struct*, script::Script*)
{
    if (is_halted_)
        set_frozen(false);
    return true;
}
// 0x005177A0
bool RiderBoardPhysics::get_time_since_physics_switch(script::Struct*, script::Script* script)
{
    script->args_->push_float(kTimeSincePhysicsSwitch, get_seconds(get_clock() - switch_time_));
    return true;
}
// 0x005177F0
bool RiderBoardPhysics::get_previous_physics_state_duration(script::Struct*, script::Script* script)
{
    script->args_->push_float(kPreviousPhysicsStateDuration, get_seconds(previous_duration_));
    return true;
}
// 0x005176E0
bool RiderBoardPhysics::set_board_missing(script::Struct*, script::Script*)
{
    board_missing_ = true;
    return true;
}
// 0x005176F0
bool RiderBoardPhysics::unset_board_missing(script::Struct*, script::Script*)
{
    board_missing_ = false;
    return true;
}
// 0x00517700
bool RiderBoardPhysics::is_board_missing(script::Struct*, script::Script*) { return board_missing_; }
// 0x00517710
bool RiderBoardPhysics::set_force_walk(script::Struct*, script::Script*)
{
    board_missing_ = true;
    force_walk_ = true;
    return true;
}
// 0x00517720
bool RiderBoardPhysics::un_set_force_walk(script::Struct*, script::Script*)
{
    board_missing_ = false;
    force_walk_ = false;
    return true;
}
// 0x00517730
bool RiderBoardPhysics::is_force_walk_on(script::Struct*, script::Script*) { return force_walk_; }

} // namespace objects
