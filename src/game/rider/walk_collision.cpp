// The Walk component's collision checks (GameObjects::FootMovement, see walk_physics.h): the curb float, the ceiling,
// the walking sticker slap, the ground below ("uber frig"), the critical point's offset, the jump over an
// obstruction, the wallplant and the grind. From the disassembly of 0x005264F0-0x00527BD3 and 0x00529160-0x00529CDD.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <array>

#include "engine/core/dbg.h"
#include "engine/render/coll_obj.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/model_component.h"
#include "engine/objects/node_components.h"
#include "engine/objects/shadow_component.h"
#include "engine/objects/trigger_component.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/entity.h"
#include "engine/objects/rail_manager.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/rider/core_physics.h"
#include "game/rider/trick_component.h"
#include "game/rider/walk_fields.h"
#include "game/rider/walk_physics.h"
#include "platform/game_clock.h"
#include "game/skate/skate_module.h"
#include "engine/objects/trigger_events.h"
#include "game/rider/physics_control.h"
#include "game/rider/skater_state.h"
#include "engine/objects/input_component.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"

namespace objects {

namespace {
constexpr int kButtonHeldMs = 30;
constexpr render::FaceFlag kShadeRequireFlags = render::FaceFlag::kShadeSurface;
constexpr int kSourceLineUberFrig = 0x941;         // the source lines the original reports its messages from
constexpr int kUnknown710Reset = 60;
constexpr int kSourceLineBadPoint = 0xB8C;
constexpr int kSourceLineAdjustment = 0xB8D;
constexpr int kSourceLineOffset = 0xB8E;

using namespace walk_fields;

// walk_parameters' names.
constexpr uint32_t kCurbFloatFeelerLength = 0x11EDCC52, kSnapUpHeight = 0xCEE3A3E1,
                   kMinCurbHeightAdjustVel = 0x227D72EE, kCurbFloatLerpUpRate = 0x856A80D3, kWalkerHeight = 0x9EA1974A,
                   kProbeElevation = 0x6DA7F696, kFeelerLength = 0x99978D2B, kJumpObstructionCheckHeight = 0x48C0AC2A,
                   kJumpObstructionCheckBack = 0x06EBBAB7, kVertWallJumpSpeed = 0x420D18BC,
                   kHoldTimeForMaxJump = 0x12333EBD, kMinJumpFactor = 0x246D0BF3, kHorizWallJumpSpeed = 0x99510695;
// Global script values and names.
constexpr uint32_t kMinWallplantHeight = 0xD5349CC6;     // Physics_Min_Wallplant_Height
constexpr uint32_t kDisallowRewallplant = 0x82135DD7;    // Physics_Disallow_Rewallplant_Duration
constexpr uint32_t kLateJumpSlop = 0x4C2B6DF3;           // Skater_Late_Jump_Slop
constexpr uint32_t kWalkingWallplantSwitch = 0x7243098F; // walking_wallplant_switch (a script)
constexpr uint32_t kUberFrigCurrentHeight = 0x801C9CB6;  // uber_frig_current_height
constexpr uint32_t kCeilingEvent = 0x6E84ACF3;           // (its name not recovered)
constexpr uint32_t kOutputTrackingLines = 0x19FB78FA;    // output_tracking_lines
constexpr uint32_t kShadow = 0x8A897DD2, kTrick = 0x270F56E1, kRailManagerType = 0x1CDFB2A3;
constexpr uint32_t kLand = 0x57FF2A27, kWallplant = 0xCF74F6B7, kRail = 0xA6A3147E;

constexpr float kOne = 1.0F;               // 0x006453A8
constexpr float kHalf = 0.5F;              // 0x006453BC
constexpr float kCurbStill = 0.01F;        // 0x0064544C
constexpr float kHeadClearance = 3.0F;     // 0x006467A8
constexpr float kFallingHead = 18.0F;      // 0x0064AAAC: the head's reach is this much less while falling
constexpr float kTwelve = 12.0F;           // 0x0064546C (uber_frig_current_height is in feet)
constexpr float kMinus = -1.0F;            // 0x00645420
constexpr float kOffsetEaseRate = 1000.0F; // 0x006453C0: the critical point's offset eased out at this a second
constexpr float kAdjustClearance = 0.1F;   // the bad critical point's adjustment: this far off the face hit
constexpr float kGrindClearance = 36.0F;   // 0x0064B9CC: a wall this close (squared) to the rail's point is fine

// The game's own messages (debug::Printf): "M:%s:%i: %s", the source file's name, and the texts.
constexpr const char* kMessageFormat = "M:%s:%i: %s\n";
const char* const kSourceFile = "mponents\\WalkComponent.cpp";
constexpr const char* kSlapMessage = "walking sticker slap...........\n";
const char* const kUberFrigMessage = "applying uber frig";
const char* const kBadPointMessage = "adjusting position due to bad critical point";
constexpr const char* kAdjustmentFormat = "D:%s:%i: required_pos_adjustment = %g, %g, %g\n";
constexpr const char* kOffsetFormat = "D:%s:%i: m_critical_point_offset = %g, %g, %g\n";
constexpr const char* kTrackingFormat = "Tracking%d %.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n";

double get_frame_length(FootMovement* walk) { return walk->frame_length(); }

} // namespace

// 0x005264F0
void FootMovement::update_curb_float()
{
    // The facing along the ground, never downhill, made unit (its w left as it is).
    maths::Vec4 ahead = facing_;
    ahead.flatten_onto_plane(floor_normal_);
    ahead.y = std::max(ahead.y, 0.0F);
    const double length =
        std::sqrt(((static_cast<double>(ahead.z) * ahead.z) + (static_cast<double>(ahead.x) * ahead.x)) +
                  (static_cast<double>(ahead.y) * ahead.y));
    if (length > 0.0) {
        auto inverse = static_cast<float>(kOne / length);
        ahead.x = inverse * ahead.x;
        ahead.y = inverse * ahead.y;
        ahead.z = ahead.z * inverse;
    }

    // A feeler from half a unit above the feet, curb_float_feeler_length ahead.
    render::ProbeRay feeler;
    const maths::Vec4& pos = pos_;
    feeler.line().start = {
        .x = pos.x, .y = static_cast<float>(static_cast<double>(pos.y) + kHalf), .z = pos.z, .w = 0.0F};
    const float reach = walk_parameter(kCurbFloatFeelerLength);
    const float reach_y = ahead.y * reach;
    const float reach_x = reach * ahead.x;
    feeler.line().end = {.x = reach_x + pos.x,
                         .y = static_cast<float>((static_cast<double>(reach_y) + pos.y) + kHalf),
                         .z = static_cast<float>((static_cast<double>(ahead.z) * reach) + pos.z),
                         .w = 0.0F};
    if (feeler.get_hit_shape(true, false) == 0) {
        return;
    }
    const float distance = feeler.reach();

    // Something ahead: straight down onto it from snap_up_height, half a unit past where it was hit.
    const maths::Vec4 over = {.x = (ahead.x * kHalf) + feeler.contact_pos().x,
                              .y = (ahead.y * kHalf) + feeler.contact_pos().y,
                              .z = (ahead.z * kHalf) + feeler.contact_pos().z,
                              .w = static_cast<float>((static_cast<double>(ahead.w) * kHalf) + feeler.contact_pos().w)};
    feeler.line().end = over;
    feeler.line().start = over;
    feeler.line().start.y = static_cast<float>(static_cast<double>(walk_parameter(kSnapUpHeight)) + pos.y);
    if (feeler.get_hit_shape(true, false) == 0) {
        return;
    }

    // Its height, less the nearer it is, eased toward - unless the float is settled, there's no push and it's
    // slower than min_curb_height_adjust_vel (the original reads it through 0x0043DA70).
    auto target = static_cast<float>((static_cast<double>(feeler.contact_pos().y) - pos.y) *
                                     (kOne - static_cast<double>(distance)));
    float& curb = curb_offset_;
    const maths::Vec4& vel = vel_;
    bool settled = std::fabs(curb) < kCurbStill && stick_deflection_ == 0.0F;
    if (settled) {
        const double least = walk_parameter(kMinCurbHeightAdjustVel);
        const double speed_squared = ((static_cast<double>(vel.x) * vel.x) + (static_cast<double>(vel.y) * vel.y)) +
                                     (static_cast<double>(vel.z) * vel.z);
        settled = least * least > speed_squared;
    }
    if (!settled) {
        const double rate = static_cast<double>(walk_parameter(kCurbFloatLerpUpRate)) * get_frame_length(this);
        curb = static_cast<float>(((static_cast<double>(target) - curb) * rate) + curb);
    }
    curb_float_ = true;
}

// 0x00526BA0
void FootMovement::check_ceiling()
{
    if (pushing_) {
        return;
    }
    const float height = walk_parameter(kWalkerHeight);
    float top = height;
    if (up_speed_ < 0.0F) {
        top = static_cast<float>(static_cast<double>(height) - kFallingHead);
    }

    // A feeler up from just above the feet to the head.
    render::ProbeRay feeler;
    maths::Vec4& pos = pos_;
    feeler.line().start = {.x = pos.x, .y = static_cast<float>(static_cast<double>(pos.y) + kHeadClearance), .z = pos.z, .w = pos.w};
    feeler.line().end = {.x = pos.x, .y = static_cast<float>(static_cast<double>(pos.y) + top), .z = pos.z, .w = pos.w};
    if (feeler.get_hit_shape(true, false) == 0) {
        return;
    }

    // A ceiling: the velocity into it taken away, and the head moved back out of it.
    maths::Vec4& vel = vel_;
    vel.y = up_speed_;
    const maths::Vec4& normal = feeler.surface_dir();
    if (((static_cast<double>(normal.y) * vel.y) + (static_cast<double>(normal.z) * vel.z)) +
            (static_cast<double>(normal.x) * vel.x) <
        0.0) {
        vel.flatten_onto_plane(normal);
    }
    const maths::Vec4& end = feeler.line().end;
    maths::Vec4 back = {.x = feeler.contact_pos().x - end.x, .y = feeler.contact_pos().y - end.y, .z = feeler.contact_pos().z - end.z,
                        .w = feeler.contact_pos().w - end.w};
    up_speed_ = vel.y;
    vel.y = 0.0F;
    back.project_onto(normal);
    feeler.line().end = {.x = back.x + pos.x, .y = back.y + pos.y, .z = back.z + pos.z, .w = back.w + pos.w};
    if (feeler.get_hit_shape(true, false) != 0) {
        pos = {.x = feeler.contact_pos().x + feeler.surface_dir().x, .y = feeler.contact_pos().y + feeler.surface_dir().y,
               .z = feeler.contact_pos().z + feeler.surface_dir().z, .w = feeler.contact_pos().w + feeler.surface_dir().w};
    } else {
        pos = feeler.line().end;
    }
    entity()->post_own_event(kCeilingEvent, nullptr);
}

// 0x00526E00
void FootMovement::check_sticker_slap()
{
    // X and the pad's +0x20 button (btn_down_) both down, the latter within 30 ms.
    auto* pad = &input_->pad_primary();
    if ((pad->pos_x().held_down() == 0U) || (pad->btn_down().held_down() == 0U)) {
        return;
    }
    if (static_cast<uint32_t>(pad->btn_down().get_held_millis()) > kButtonHeldMs) {
        return;
    }

    // A wall ahead: a feeler from feeler_height up, feeler_length along the facing (static collision only).
    render::ProbeRay feeler;
    const maths::Vec4& facing = facing_;
    maths::Vec4& start = feeler.line().start;
    start = pos_;
    start.y = static_cast<float>(static_cast<double>(start.y) + walk_parameter(kProbeElevation));
    const float reach = walk_parameter(kFeelerLength);
    feeler.line().end = {.x = (reach * facing.x) + start.x,
                         .y = (reach * facing.y) + start.y,
                         .z = (reach * facing.z) + start.z,
                         .w = static_cast<float>((static_cast<double>(reach) * facing.w) + start.w)};
    if (feeler.get_hit_shape(false, false) == 0) {
        return;
    }

    // High enough, not just wallplanted, not just after leaving the ground, and the physics allows it.
    if (script::fetch_float(kMinWallplantHeight) > skater_state_->height_above_ground()) {
        return;
    }
    auto since_as_float = [](int then) {
        return static_cast<float>(static_cast<double>(static_cast<uint32_t>(clock_since(then))));
    };
    const float since_wallplant = since_as_float(static_cast<int>(core_physics_->wallplant_time()));
    if (script::fetch_float(kDisallowRewallplant) > since_wallplant) {
        return;
    }
    const float since_state = since_as_float(state_time_);
    if (script::fetch_float(kLateJumpSlop) > since_state) {
        return;
    }
    if (!core_physics_->wall_plant(false, true, false)) {
        return;
    }
    debug::printf(kSlapMessage);
    script::launch_script(kWalkingWallplantSwitch, nullptr, entity(), false, nullptr);
}

// 0x00527080
void FootMovement::update_ground_below()
{
    auto depth = static_cast<float>(static_cast<double>(script::fetch_float(kUberFrigCurrentHeight)) * kTwelve);
    const maths::Vec4& pos = pos_;
    const maths::Vec4& offset = critical_point_offset_;

    // Down from the critical point: the ground below.
    render::ProbeRay down;
    const float y = pos.y + offset.y;
    const maths::Vec4 point = {.x = pos.x + offset.x, .y = y, .z = pos.z + offset.z, .w = pos.w + offset.w};
    down.line().start = {.x = point.x, .y = static_cast<float>(static_cast<double>(y) + kOne), .z = point.z, .w = point.w};
    down.line().end = {.x = point.x, .y = static_cast<float>(static_cast<double>(y) - depth), .z = point.z, .w = point.w};
    const bool below = down.get_hit_shape(true, false) != 0;

    // From 12 higher (faces flagged 0x20 only): down to just under that ground, or as deep.
    render::ProbeRay shade;
    shade.set_excluded({.ignore = render::kNoFaceFlags, .require = kShadeRequireFlags});
    shade.line().start = {.x = point.x, .y = static_cast<float>((static_cast<double>(pos.y) + offset.y) + kTwelve), .z = point.z,
                         .w = point.w};
    if (below) {
        shade.line().end = {.x = down.contact_pos().x, .y = static_cast<float>(static_cast<double>(down.contact_pos().y) - kOne),
                           .z = down.contact_pos().z, .w = down.contact_pos().w};
    } else {
        shade.line().end = {.x = point.x, .y = static_cast<float>(static_cast<double>(shade.line().start.y) - depth), .z = point.z,
                           .w = point.w};
    }
    // The model shaded by the face hit (its vertex value).
    auto shade_model = [this](render::ProbeRay* feeler) { model_->apply_brightness(feeler, true, kOne); };
    if (shade.get_hit_shape(true, false) != 0) {
        shade_model(&shade);
    } else if (below) {
        shade_model(&down);
    }

    if (below) {
        // The height above it (the skater state's +0x11C), the shadow on it.
        skater_state_->set_height_above_ground(static_cast<float>(static_cast<double>(pos.y) - down.contact_pos().y));
        auto* shadow = core::down_cast<GroundShadowModule>(entity()->fetch_module(kShadow));
        shadow->change_place(&down.contact_pos());
        shadow->set_normal(&down.surface_dir());
        return;
    }

    // Nothing below at all: back to where the frame began, turned around, on the ground.
    debug::printf(kMessageFormat, kSourceFile, kSourceLineUberFrig, kUberFrigMessage);
    pos_ = old_pos_;
    maths::Vec4& vel = vel_;
    vel.y = 0.0F;
    vel.w = 1.0F;
    vel.x = vel.x * kMinus;
    vel.z = vel.z * kMinus;
    maths::Vec4& facing = facing_;
    facing = {.x = facing.x * kMinus, .y = facing.y * kMinus, .z = facing.z * kMinus, .w = facing.w * kMinus};
    up_speed_ = 0.0F;
    locked_to_heading_ = true;
    unknown710_ = kUnknown710Reset;
    change_phase(0);
    feeler_hit_ = false;
    floor_normal_ = {.x = 0.0F, .y = 1.0F, .z = 0.0F, .w = 1.0F};
    anim_state_ = kLand;
}

// 0x005278D0
void FootMovement::ease_critical_point_offset()
{
    maths::Vec4& offset = critical_point_offset_;
    auto length_squared =
        static_cast<float>(((static_cast<double>(offset.x) * offset.x) + (static_cast<double>(offset.y) * offset.y)) +
                           (static_cast<double>(offset.z) * offset.z));
    if (length_squared == 0.0F) {
        return;
    }
    const int state = state_;
    if (state < 0 || (state > 1 && state != 3)) {
        return;
    }

    // Eased out at 1000 a second.
    const double step = get_frame_length(this) * kOffsetEaseRate;
    const maths::Vec4 old = offset;
    const double length = std::sqrt(static_cast<double>(length_squared));
    if (step < length) {
        const double scale = step / length;
        const double kx = scale * offset.x;
        const double ky = scale * offset.y;
        const double kz = scale * offset.z;
        const double kw = scale * offset.w;
        offset.x = static_cast<float>(offset.x - kx);
        offset.y = static_cast<float>(offset.y - ky);
        offset.z = static_cast<float>(offset.z - kz);
        offset.w = static_cast<float>(offset.w - kw);
    } else {
        offset = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    }
    if (state == 3) {
        return;
    }

    // The critical point's path from the frame's start: what it went through moves the position back off it.
    render::ProbeRay feeler;
    const maths::Vec4& old_pos = old_pos_;
    maths::Vec4& pos = pos_;
    feeler.line().start = {.x = old.x + old_pos.x, .y = old.y + old_pos.y, .z = old.z + old_pos.z, .w = old.w + old_pos.w};
    feeler.line().end = {.x = pos.x + offset.x, .y = pos.y + offset.y, .z = pos.z + offset.z, .w = pos.w + offset.w};
    if (feeler.get_hit_shape(true, false) != 0) {
        const maths::Vec4 adjustment =
            (feeler.contact_pos() + kAdjustClearance * feeler.surface_dir()) - feeler.line().end;
        pos += adjustment;
        debug::printf(kMessageFormat, kSourceFile, kSourceLineBadPoint, kBadPointMessage);
        debug::printf(kAdjustmentFormat, kSourceFile, kSourceLineAdjustment, static_cast<double>(adjustment.x),
                      static_cast<double>(adjustment.y), static_cast<double>(adjustment.z));
        debug::printf(kOffsetFormat, kSourceFile, kSourceLineOffset, static_cast<double>(offset.x), static_cast<double>(offset.y),
                      static_cast<double>(offset.z));
    }
    // Pushed back off a wall: the critical point kept on its side of the plane.
    if (pushing_) {
        const maths::Vec4 point = pos + offset;
        const maths::Vec4& normal = push_normal_;
        const double past = (((static_cast<double>(point.z) * normal.z) + (static_cast<double>(point.y) * normal.y)) +
                             (static_cast<double>(point.x) * normal.x)) -
                            push_plane_distance_;
        if (past > 0.0) {
            pos -= static_cast<float>(past) * normal;
        }
    }
}

// 0x00529160
void FootMovement::plan_jump_over_obstruction()
{
    // A feeler up from the head by jump_obstruction_check_height (static collision only)...
    render::ProbeRay feeler;
    maths::Vec4& start = feeler.line().start;
    maths::Vec4& end = feeler.line().end;
    start = pos_;
    start.y = static_cast<float>(static_cast<double>(start.y) + walk_parameter(kWalkerHeight));
    end = start;
    end.y = static_cast<float>(static_cast<double>(end.y) + walk_parameter(kJumpObstructionCheckHeight));
    if (feeler.get_hit_shape(false, false) == 0) {
        return;
    }
    // ... meets something: the same jump_obstruction_check_back further back must be clear.
    const float back = walk_parameter(kJumpObstructionCheckBack);
    const float height = feeler.contact_pos().y - start.y;
    const maths::Vec4& facing = facing_;
    for (maths::Vec4* v : {&start, &end}) {
        v->x = static_cast<float>(v->x - (static_cast<double>(back) * facing.x));
        v->y = static_cast<float>(v->y - (static_cast<double>(back) * facing.y));
        v->z = static_cast<float>(v->z - (static_cast<double>(back) * facing.z));
        v->w = static_cast<float>(v->w - (static_cast<double>(back) * facing.w));
    }
    if (feeler.get_hit_shape(false, false) != 0) {
        return;
    }

    // A ledge: the jump to it - the time to rise `height` at the vertical speed under gravity, and the speed back
    // toward it to cover `back` in that time.
    const double gravity = get_gravity();
    const double rise = up_speed_;
    const double discriminant = (rise * rise) - ((gravity * height) + (gravity * height));
    if (!(discriminant > 0.0)) {
        return;
    }
    const double inverse = kOne / gravity;
    const double time = (rise - std::sqrt(static_cast<double>(static_cast<float>(discriminant)))) * inverse;
    const double speed = -(back / time);
    auto speed_f = static_cast<float>(speed);
    jump_velocity_ = {.x = speed_f * facing.x, .y = speed_f * facing.y, .z = speed_f * facing.z, .w = static_cast<float>(speed * facing.w)};
    jump_target_height_ = static_cast<float>(static_cast<double>(height) + pos_.y);
    leap_duration_ = static_cast<float>(((inverse * rise) + (inverse * rise)) - time);
}

// 0x005293F0
void FootMovement::try_wallplant()
{
    // X down, not rising, high enough, not just wallplanted, not just after leaving the ground...
    auto* pad = &input_->pad_primary();
    if ((pad->pos_x().held_down() == 0U) || up_speed_ > 0.0F) {
        return;
    }
    if (script::fetch_float(kMinWallplantHeight) > skater_state_->height_above_ground()) {
        return;
    }
    auto since_as_float = [](int then) {
        return static_cast<float>(static_cast<double>(static_cast<uint32_t>(clock_since(then))));
    };
    const float since_wallplant = since_as_float(static_cast<int>(core_physics_->wallplant_time()));
    if (script::fetch_float(kDisallowRewallplant) > since_wallplant) {
        return;
    }
    const float since_state = since_as_float(state_time_);
    if (script::fetch_float(kLateJumpSlop) > since_state) {
        return;
    }
    // ... and touching a wall: the first contact in this order.
    static const std::array<int, 7> kOrder = {{0, 1, 6, 2, 5, 3, 4}};
    WalkContact* contact = nullptr;
    for (const int index : kOrder) {
        WalkContact* candidate = &core::at(contacts_, index);
        if (candidate->hit) {
            contact = candidate;
            break;
        }
    }
    if (!contact) {
        return;
    }

    // Off the wall: the way pushed (or the jump's way) mirrored off it, up by the hold of X.
    maths::Vec4 direction = stick_deflection_ == 0.0F ? leap_heading_ : stick_heading_;
    const maths::Vec4& normal = contact->normal;
    const double into =
        ((static_cast<double>(direction.z) * normal.z) + (static_cast<double>(direction.y) * normal.y)) +
        (static_cast<double>(direction.x) * normal.x);
    if (into < 0.0) {
        direction -= maths::operator*(static_cast<float>(into + into), normal);
    }
    const float rise = walk_parameter(kVertWallJumpSpeed);
    const float hold = walk_parameter(kHoldTimeForMaxJump);
    const double held = static_cast<double>(static_cast<uint32_t>(pad->pos_x().get_held_millis())) / hold;
    const float strength = held > kOne ? kOne : static_cast<float>(held);
    const float least = walk_parameter(kMinJumpFactor);
    up_speed_ = static_cast<float>((((kOne - static_cast<double>(least)) * strength) + least) * rise);
    const float across = walk_parameter(kHorizWallJumpSpeed);
    vel_ = {.x = across * direction.x, .y = direction.y * across, .z = direction.z * across, .w = direction.w * across};
    leap_heading_ = facing_;
    // Off a moving object: its velocity added.
    auto& feeler = contact->feeler;
    if (feeler.hit_object_alive()) {
        const maths::Vec4& carried = feeler.get_hit_object()->vel();
        up_speed_ = carried.y + up_speed_;
        vel_.x = carried.x + vel_.x;
        vel_.z = carried.z + vel_.z;
    }
    plan_jump_over_obstruction();
    // The trigger told (events 0x0064C63C); a trigger's script may take over.
    trigger_->probe_touches_zone(kWalkEvents0064c63c, &feeler);
    if (static_cast<bool>(physics_control_->flag26())) {
        return;
    }
    // The wall's checksum (the feeler hit's +0x34) to the trick objects; the wallplant's time; its animation.
    core::down_cast<MoveExecutor>(entity()->fetch_module(kTrick))
        ->pass_to_trick_objects(contact->feeler.hit().object_checksum_);
    core_physics_->set_wallplant_time(frame_clock::game_clock_elapsed() + clock_offset());
    anim_state_ = kWallplant;
}

// 0x005299F0
bool FootMovement::try_grind()
{
    if (physics_control_->board_missing()) {
        return false;
    }
    // A rail along the move: the level's, else one riding an object (in that object's space).
    maths::Vec4& old_pos = old_pos_;
    maths::Vec4& pos = pos_;
    maths::Vec4& point = grind_point_;
    GrindPoint*& node = grind_node_;
    GrindRailHub* level_rails = modules::Skate::sole()->rail_manager().get();
    bool found = level_rails->find_grind_rail(&old_pos, &pos, &point, &node, nullptr, {.min_alignment_ = kOne, .side = 0});
    if (found) {
        grind_rails_ = level_rails;
        grind_object_ = nullptr;
    } else {
        old_pos.w = kOne;
        pos.w = kOne;
        ObjectModule* component = object_hub_slot()->find_first_module_of_kind(kRailManagerType);
        if (!component) {
            return false;
        }
        // (the original's slot 13: the component's rails)
        auto rails_of = [](ObjectModule* link_module) {
            return core::down_cast<GrindPathLinkModule>(link_module)->get_rail_groups();
        };
        for (; component && !found; component = component->next_of_type()) {
            maths::Mat4 world{};
            core::down_cast<GrindPathLinkModule>(component)->update_rail_matrix(&world);
            maths::Mat4 inverse = world;
            inverse.upend();
            maths::Vec4 local_old{};
            maths::Vec4 rel_pos{};
            inverse.xform(local_old, old_pos);
            inverse.xform(rel_pos, pos);
            found = rails_of(component)->find_grind_rail(&local_old, &rel_pos, &point, &node, nullptr, {.min_alignment_ = kOne, .side = 0});
            if (found) {
                grind_rails_ = rails_of(component);
                maths::Vec4 world_point{};
                point = world.xform(world_point, point);
                grind_object_ = component->entity();
            }
        }
        if (!found) {
            return false;
        }
    }
    // The core physics' say, and nothing in the way to it (a wall within 6 of the rail is fine).
    if (!core_physics_->can_grind_rail(node, grind_rails_, true)) {
        return false;
    }
    render::ProbeRay feeler(&pos, &point);
    if (feeler.get_hit_shape(true, false) != 0) {
        const double dz = static_cast<double>(point.z) - feeler.contact_pos().z;
        const double dy = static_cast<double>(point.y) - feeler.contact_pos().y;
        const double dx = static_cast<double>(point.x) - feeler.contact_pos().x;
        if (((dx * dx) + (dy * dy)) + (dz * dz) > kGrindClearance) {
            return false;
        }
    }
    if (script::fetch_int_value(kOutputTrackingLines) != 0) {
        debug::printf(kTrackingFormat, 2, static_cast<double>(old_pos.x), static_cast<double>(old_pos.y),
                      static_cast<double>(old_pos.z), static_cast<double>(pos.x), static_cast<double>(pos.y),
                      static_cast<double>(pos.z));
    }
    leave_for_ = 1;
    anim_state_ = kRail;
    return true;
}

} // namespace objects
