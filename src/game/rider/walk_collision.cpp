// The Walk component's collision checks (GameObjects::FootMovement, see walk_physics.h): the curb float, the ceiling,
// the walking sticker slap, the ground below ("uber frig"), the critical point's offset, the jump over an
// obstruction, the wallplant and the grind. From the disassembly of 0x005264F0-0x00527BD3 and 0x00529160-0x00529CDD.

#include <cmath>
#include <cstdint>
#include <initializer_list>

#include "engine/core/dbg.h"
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

namespace objects {

namespace {

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

constexpr float kOne = 1.0f;               // 0x006453A8
constexpr float kHalf = 0.5f;              // 0x006453BC
constexpr float kCurbStill = 0.01f;        // 0x0064544C
constexpr float kHeadClearance = 3.0f;     // 0x006467A8
constexpr float kFallingHead = 18.0f;      // 0x0064AAAC: the head's reach is this much less while falling
constexpr float kTwelve = 12.0f;           // 0x0064546C (uber_frig_current_height is in feet)
constexpr float kMinus = -1.0f;            // 0x00645420
constexpr float kOffsetEaseRate = 1000.0f; // 0x006453C0: the critical point's offset eased out at this a second
constexpr float kAdjustClearance = 0.1f;   // the bad critical point's adjustment: this far off the face hit
constexpr float kGrindClearance = 36.0f;   // 0x0064B9CC: a wall this close (squared) to the rail's point is fine

// The game's own messages (debug::Printf): "M:%s:%i: %s", the source file's name, and the texts.
const char* const kMessageFormat = "M:%s:%i: %s\n";
const char* const kSourceFile = "mponents\\WalkComponent.cpp";
const char* const kSlapMessage = "walking sticker slap...........\n";
const char* const kUberFrigMessage = "applying uber frig";
const char* const kBadPointMessage = "adjusting position due to bad critical point";
const char* const kAdjustmentFormat = "D:%s:%i: required_pos_adjustment = %g, %g, %g\n";
const char* const kOffsetFormat = "D:%s:%i: m_critical_point_offset = %g, %g, %g\n";
const char* const kTrackingFormat = "Tracking%d %.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n";

double get_frame_length(FootMovement* walk) { return walk->frame_length_; }

} // namespace

// 0x005264F0
void FootMovement::update_curb_float()
{
    // The facing along the ground, never downhill, made unit (its w left as it is).
    maths::Vec4 ahead = facing_;
    ahead.flatten_onto_plane(floor_normal_);
    if (ahead.y < 0.0f)
        ahead.y = 0.0f;
    double length = std::sqrt((static_cast<double>(ahead.z) * ahead.z + static_cast<double>(ahead.x) * ahead.x) +
                              static_cast<double>(ahead.y) * ahead.y);
    if (length > 0.0) {
        float inverse = static_cast<float>(kOne / length);
        ahead.x = inverse * ahead.x;
        ahead.y = inverse * ahead.y;
        ahead.z = ahead.z * inverse;
    }

    // A feeler from half a unit above the feet, curb_float_feeler_length ahead.
    render::ProbeRay feeler;
    const maths::Vec4& pos = pos_;
    feeler.line_.start = {pos.x, static_cast<float>(static_cast<double>(pos.y) + kHalf), pos.z, 0.0f};
    float reach = walk_parameter(kCurbFloatFeelerLength);
    float reach_y = ahead.y * reach;
    float reach_x = reach * ahead.x;
    feeler.line_.end = {reach_x + pos.x, static_cast<float>((static_cast<double>(reach_y) + pos.y) + kHalf),
                        static_cast<float>(static_cast<double>(ahead.z) * reach + pos.z), 0.0f};
    if (!feeler.get_hit_shape(true, false))
        return;
    float distance = feeler.reach_;

    // Something ahead: straight down onto it from snap_up_height, half a unit past where it was hit.
    maths::Vec4 over = {ahead.x * kHalf + feeler.contact_pos_.x, ahead.y * kHalf + feeler.contact_pos_.y,
                        ahead.z * kHalf + feeler.contact_pos_.z,
                        static_cast<float>(static_cast<double>(ahead.w) * kHalf + feeler.contact_pos_.w)};
    feeler.line_.end = over;
    feeler.line_.start = over;
    feeler.line_.start.y = static_cast<float>(static_cast<double>(walk_parameter(kSnapUpHeight)) + pos.y);
    if (!feeler.get_hit_shape(true, false))
        return;

    // Its height, less the nearer it is, eased toward - unless the float is settled, there's no push and it's
    // slower than min_curb_height_adjust_vel (the original reads it through 0x0043DA70).
    float target = static_cast<float>((static_cast<double>(feeler.contact_pos_.y) - pos.y) *
                                      (kOne - static_cast<double>(distance)));
    float& curb = curb_offset_;
    const maths::Vec4& vel = vel_;
    bool settled = std::fabs(curb) < kCurbStill && stick_deflection_ == 0.0f;
    if (settled) {
        double least = walk_parameter(kMinCurbHeightAdjustVel);
        double speed_squared = (static_cast<double>(vel.x) * vel.x + static_cast<double>(vel.y) * vel.y) +
                               static_cast<double>(vel.z) * vel.z;
        settled = least * least > speed_squared;
    }
    if (!settled) {
        double rate = static_cast<double>(walk_parameter(kCurbFloatLerpUpRate)) * get_frame_length(this);
        curb = static_cast<float>((static_cast<double>(target) - curb) * rate + curb);
    }
    curb_float_ = true;
}

// 0x00526BA0
void FootMovement::check_ceiling()
{
    if (pushing_)
        return;
    float height = walk_parameter(kWalkerHeight);
    float top = height;
    if (up_speed_ < 0.0f)
        top = static_cast<float>(static_cast<double>(height) - kFallingHead);

    // A feeler up from just above the feet to the head.
    render::ProbeRay feeler;
    maths::Vec4& pos = pos_;
    feeler.line_.start = {pos.x, static_cast<float>(static_cast<double>(pos.y) + kHeadClearance), pos.z, pos.w};
    feeler.line_.end = {pos.x, static_cast<float>(static_cast<double>(pos.y) + top), pos.z, pos.w};
    if (!feeler.get_hit_shape(true, false))
        return;

    // A ceiling: the velocity into it taken away, and the head moved back out of it.
    maths::Vec4& vel = vel_;
    vel.y = up_speed_;
    const maths::Vec4& normal = feeler.surface_dir_;
    if ((static_cast<double>(normal.y) * vel.y + static_cast<double>(normal.z) * vel.z) +
            static_cast<double>(normal.x) * vel.x <
        0.0)
        vel.flatten_onto_plane(normal);
    const maths::Vec4& end = feeler.line_.end;
    maths::Vec4 back = {feeler.contact_pos_.x - end.x, feeler.contact_pos_.y - end.y, feeler.contact_pos_.z - end.z,
                        feeler.contact_pos_.w - end.w};
    up_speed_ = vel.y;
    vel.y = 0.0f;
    back.project_onto(normal);
    feeler.line_.end = {back.x + pos.x, back.y + pos.y, back.z + pos.z, back.w + pos.w};
    if (feeler.get_hit_shape(true, false))
        pos = {feeler.contact_pos_.x + feeler.surface_dir_.x, feeler.contact_pos_.y + feeler.surface_dir_.y,
               feeler.contact_pos_.z + feeler.surface_dir_.z, feeler.contact_pos_.w + feeler.surface_dir_.w};
    else
        pos = feeler.line_.end;
    entity_->post_own_event(kCeilingEvent, nullptr);
}

// 0x00526E00
void FootMovement::check_sticker_slap()
{
    // X and the pad's +0x20 button (btn_down_) both down, the latter within 30 ms.
    auto* pad = &input_->pad_primary_;
    if (!pad->pos_x_.held_down_ || !pad->btn_down_.held_down_)
        return;
    if (static_cast<uint32_t>(pad->btn_down_.get_held_millis()) > 30)
        return;

    // A wall ahead: a feeler from feeler_height up, feeler_length along the facing (static collision only).
    render::ProbeRay feeler;
    const maths::Vec4& facing = facing_;
    maths::Vec4& start = feeler.line_.start;
    start = pos_;
    start.y = static_cast<float>(static_cast<double>(start.y) + walk_parameter(kProbeElevation));
    float reach = walk_parameter(kFeelerLength);
    feeler.line_.end = {reach * facing.x + start.x, reach * facing.y + start.y, reach * facing.z + start.z,
                        static_cast<float>(static_cast<double>(reach) * facing.w + start.w)};
    if (!feeler.get_hit_shape(false, false))
        return;

    // High enough, not just wallplanted, not just after leaving the ground, and the physics allows it.
    if (script::fetch_float(kMinWallplantHeight) > skater_state_->height_above_ground_)
        return;
    auto since_as_float = [](int then) {
        return static_cast<float>(static_cast<double>(static_cast<uint32_t>(clock_since(then))));
    };
    float since_wallplant = since_as_float(static_cast<int>(core_physics_->wallplant_time_));
    if (script::fetch_float(kDisallowRewallplant) > since_wallplant)
        return;
    float since_state = since_as_float(state_time_);
    if (script::fetch_float(kLateJumpSlop) > since_state)
        return;
    if (!core_physics_->wall_plant(false, true, false))
        return;
    debug::printf(kSlapMessage);
    script::launch_script(kWalkingWallplantSwitch, nullptr, entity_, false, nullptr);
}

// 0x00527080
void FootMovement::update_ground_below()
{
    float depth = static_cast<float>(static_cast<double>(script::fetch_float(kUberFrigCurrentHeight)) * kTwelve);
    const maths::Vec4& pos = pos_;
    const maths::Vec4& offset = critical_point_offset_;

    // Down from the critical point: the ground below.
    render::ProbeRay down;
    float y = pos.y + offset.y;
    maths::Vec4 point = {pos.x + offset.x, y, pos.z + offset.z, pos.w + offset.w};
    down.line_.start = {point.x, static_cast<float>(static_cast<double>(y) + kOne), point.z, point.w};
    down.line_.end = {point.x, static_cast<float>(static_cast<double>(y) - depth), point.z, point.w};
    bool below = down.get_hit_shape(true, false);

    // From 12 higher (faces flagged 0x20 only): down to just under that ground, or as deep.
    render::ProbeRay shade;
    shade.set_excluded(0, 0x20);
    shade.line_.start = {point.x, static_cast<float>((static_cast<double>(pos.y) + offset.y) + kTwelve), point.z,
                         point.w};
    if (below)
        shade.line_.end = {down.contact_pos_.x, static_cast<float>(static_cast<double>(down.contact_pos_.y) - kOne),
                           down.contact_pos_.z, down.contact_pos_.w};
    else
        shade.line_.end = {point.x, static_cast<float>(static_cast<double>(shade.line_.start.y) - depth), point.z,
                           point.w};
    // The model shaded by the face hit (its vertex value).
    auto shade_model = [this](render::ProbeRay* feeler) { model_->apply_brightness(feeler, true, kOne); };
    if (shade.get_hit_shape(true, false))
        shade_model(&shade);
    else if (below)
        shade_model(&down);

    if (below) {
        // The height above it (the skater state's +0x11C), the shadow on it.
        skater_state_->height_above_ground_ = static_cast<float>(static_cast<double>(pos.y) - down.contact_pos_.y);
        auto* shadow = static_cast<GroundShadowModule*>(entity_->fetch_module(kShadow));
        shadow->change_place(&down.contact_pos_);
        shadow->set_normal(&down.surface_dir_);
        return;
    }

    // Nothing below at all: back to where the frame began, turned around, on the ground.
    debug::printf(kMessageFormat, kSourceFile, 0x941, kUberFrigMessage);
    pos_ = old_pos_;
    maths::Vec4& vel = vel_;
    vel.y = 0.0f;
    vel.w = 1.0f;
    vel.x = vel.x * kMinus;
    vel.z = vel.z * kMinus;
    maths::Vec4& facing = facing_;
    facing = {facing.x * kMinus, facing.y * kMinus, facing.z * kMinus, facing.w * kMinus};
    up_speed_ = 0.0f;
    locked_to_heading_ = true;
    unknown710_ = 60;
    change_phase(0);
    feeler_hit_ = false;
    floor_normal_ = {0.0f, 1.0f, 0.0f, 1.0f};
    anim_state_ = kLand;
}

// 0x005278D0
void FootMovement::ease_critical_point_offset()
{
    maths::Vec4& offset = critical_point_offset_;
    float length_squared =
        static_cast<float>((static_cast<double>(offset.x) * offset.x + static_cast<double>(offset.y) * offset.y) +
                           static_cast<double>(offset.z) * offset.z);
    if (length_squared == 0.0f)
        return;
    int state = state_;
    if (state < 0 || (state > 1 && state != 3))
        return;

    // Eased out at 1000 a second.
    double step = get_frame_length(this) * kOffsetEaseRate;
    maths::Vec4 old = offset;
    double length = std::sqrt(static_cast<double>(length_squared));
    if (step < length) {
        double k = step / length;
        double kx = k * offset.x, ky = k * offset.y, kz = k * offset.z, kw = k * offset.w;
        offset.x = static_cast<float>(offset.x - kx);
        offset.y = static_cast<float>(offset.y - ky);
        offset.z = static_cast<float>(offset.z - kz);
        offset.w = static_cast<float>(offset.w - kw);
    } else {
        offset = {0.0f, 0.0f, 0.0f, 1.0f};
    }
    if (state == 3)
        return;

    // The critical point's path from the frame's start: what it went through moves the position back off it.
    render::ProbeRay feeler;
    const maths::Vec4& old_pos = old_pos_;
    maths::Vec4& pos = pos_;
    feeler.line_.start = {old.x + old_pos.x, old.y + old_pos.y, old.z + old_pos.z, old.w + old_pos.w};
    feeler.line_.end = {pos.x + offset.x, pos.y + offset.y, pos.z + offset.z, pos.w + offset.w};
    if (feeler.get_hit_shape(true, false)) {
        maths::Vec4 adjustment = (feeler.contact_pos_ + kAdjustClearance * feeler.surface_dir_) - feeler.line_.end;
        pos += adjustment;
        debug::printf(kMessageFormat, kSourceFile, 0xB8C, kBadPointMessage);
        debug::printf(kAdjustmentFormat, kSourceFile, 0xB8D, static_cast<double>(adjustment.x),
                      static_cast<double>(adjustment.y), static_cast<double>(adjustment.z));
        debug::printf(kOffsetFormat, kSourceFile, 0xB8E, static_cast<double>(offset.x), static_cast<double>(offset.y),
                      static_cast<double>(offset.z));
    }
    // Pushed back off a wall: the critical point kept on its side of the plane.
    if (pushing_) {
        maths::Vec4 point = pos + offset;
        const maths::Vec4& normal = push_normal_;
        double past = ((static_cast<double>(point.z) * normal.z + static_cast<double>(point.y) * normal.y) +
                       static_cast<double>(point.x) * normal.x) -
                      push_plane_distance_;
        if (past > 0.0)
            pos -= static_cast<float>(past) * normal;
    }
}

// 0x00529160
void FootMovement::plan_jump_over_obstruction()
{
    // A feeler up from the head by jump_obstruction_check_height (static collision only)...
    render::ProbeRay feeler;
    maths::Vec4& start = feeler.line_.start;
    maths::Vec4& end = feeler.line_.end;
    start = pos_;
    start.y = static_cast<float>(static_cast<double>(start.y) + walk_parameter(kWalkerHeight));
    end = start;
    end.y = static_cast<float>(static_cast<double>(end.y) + walk_parameter(kJumpObstructionCheckHeight));
    if (!feeler.get_hit_shape(false, false))
        return;
    // ... meets something: the same jump_obstruction_check_back further back must be clear.
    float back = walk_parameter(kJumpObstructionCheckBack);
    float height = feeler.contact_pos_.y - start.y;
    const maths::Vec4& facing = facing_;
    for (maths::Vec4* v : {&start, &end}) {
        v->x = static_cast<float>(v->x - static_cast<double>(back) * facing.x);
        v->y = static_cast<float>(v->y - static_cast<double>(back) * facing.y);
        v->z = static_cast<float>(v->z - static_cast<double>(back) * facing.z);
        v->w = static_cast<float>(v->w - static_cast<double>(back) * facing.w);
    }
    if (feeler.get_hit_shape(false, false))
        return;

    // A ledge: the jump to it - the time to rise `height` at the vertical speed under gravity, and the speed back
    // toward it to cover `back` in that time.
    double gravity = get_gravity();
    double rise = up_speed_;
    double discriminant = rise * rise - (gravity * height + gravity * height);
    if (!(discriminant > 0.0))
        return;
    double inverse = kOne / gravity;
    double time = (rise - std::sqrt(static_cast<double>(static_cast<float>(discriminant)))) * inverse;
    double speed = -(back / time);
    float speed_f = static_cast<float>(speed);
    jump_velocity_ = {speed_f * facing.x, speed_f * facing.y, speed_f * facing.z, static_cast<float>(speed * facing.w)};
    jump_target_height_ = static_cast<float>(static_cast<double>(height) + pos_.y);
    leap_duration_ = static_cast<float>((inverse * rise + inverse * rise) - time);
}

// 0x005293F0
void FootMovement::try_wallplant()
{
    // X down, not rising, high enough, not just wallplanted, not just after leaving the ground...
    auto* pad = &input_->pad_primary_;
    if (!pad->pos_x_.held_down_ || up_speed_ > 0.0f)
        return;
    if (script::fetch_float(kMinWallplantHeight) > skater_state_->height_above_ground_)
        return;
    auto since_as_float = [](int then) {
        return static_cast<float>(static_cast<double>(static_cast<uint32_t>(clock_since(then))));
    };
    float since_wallplant = since_as_float(static_cast<int>(core_physics_->wallplant_time_));
    if (script::fetch_float(kDisallowRewallplant) > since_wallplant)
        return;
    float since_state = since_as_float(state_time_);
    if (script::fetch_float(kLateJumpSlop) > since_state)
        return;
    // ... and touching a wall: the first contact in this order.
    static const int kOrder[7] = {0, 1, 6, 2, 5, 3, 4};
    WalkContact* contact = nullptr;
    for (int index : kOrder) {
        WalkContact* candidate = &contacts_[index];
        if (candidate->hit) {
            contact = candidate;
            break;
        }
    }
    if (!contact)
        return;

    // Off the wall: the way pushed (or the jump's way) mirrored off it, up by the hold of X.
    maths::Vec4 direction = stick_deflection_ == 0.0f ? leap_heading_ : stick_heading_;
    const maths::Vec4& normal = contact->normal;
    double into = (static_cast<double>(direction.z) * normal.z + static_cast<double>(direction.y) * normal.y) +
                  static_cast<double>(direction.x) * normal.x;
    if (into < 0.0)
        direction -= maths::operator*(static_cast<float>(into + into), normal);
    float rise = walk_parameter(kVertWallJumpSpeed);
    float hold = walk_parameter(kHoldTimeForMaxJump);
    double held = static_cast<double>(static_cast<uint32_t>(pad->pos_x_.get_held_millis())) / hold;
    float strength = held > kOne ? kOne : static_cast<float>(held);
    float least = walk_parameter(kMinJumpFactor);
    up_speed_ = static_cast<float>(((kOne - static_cast<double>(least)) * strength + least) * rise);
    float across = walk_parameter(kHorizWallJumpSpeed);
    vel_ = {across * direction.x, direction.y * across, direction.z * across, direction.w * across};
    leap_heading_ = facing_;
    // Off a moving object: its velocity added.
    auto& feeler = contact->feeler;
    if (feeler.hit_object_alive()) {
        const maths::Vec4& carried = feeler.get_hit_object()->vel_;
        up_speed_ = carried.y + up_speed_;
        vel_.x = carried.x + vel_.x;
        vel_.z = carried.z + vel_.z;
    }
    plan_jump_over_obstruction();
    // The trigger told (events 0x0064C63C); a trigger's script may take over.
    trigger_->probe_touches_zone(kWalkEvents0064c63c, &feeler);
    if (static_cast<bool>(physics_control_->flag26_))
        return;
    // The wall's checksum (the feeler hit's +0x34) to the trick objects; the wallplant's time; its animation.
    static_cast<MoveExecutor*>(entity_->fetch_module(kTrick))
        ->pass_to_trick_objects(contact->feeler.hit_.object_checksum_);
    core_physics_->wallplant_time_ = frame_clock::game_clock_elapsed() + g_clock_offset;
    anim_state_ = kWallplant;
}

// 0x005299F0
bool FootMovement::try_grind()
{
    if (physics_control_->board_missing_)
        return false;
    // A rail along the move: the level's, else one riding an object (in that object's space).
    maths::Vec4& old_pos = old_pos_;
    maths::Vec4& pos = pos_;
    maths::Vec4& point = grind_point_;
    GrindPoint*& node = grind_node_;
    GrindRailHub* level_rails = modules::g_skate_module->rail_manager_;
    bool found = level_rails->find_grind_rail(&old_pos, &pos, &point, &node, nullptr, kOne, 0);
    if (found) {
        grind_rails_ = level_rails;
        grind_object_ = nullptr;
    } else {
        old_pos.w = kOne;
        pos.w = kOne;
        ObjectModule* component = object_hub_slot()->find_first_module_of_kind(kRailManagerType);
        if (!component)
            return false;
        // (the original's slot 13: the component's rails)
        auto rails_of = [](ObjectModule* c) { return static_cast<GrindPathLinkModule*>(c)->get_rail_groups(); };
        for (; component && !found; component = component->next_of_type_) {
            maths::Mat4 world;
            static_cast<GrindPathLinkModule*>(component)->update_rail_matrix(&world);
            maths::Mat4 inverse = world;
            inverse.upend();
            maths::Vec4 local_old, rel_pos;
            inverse.xform(local_old, old_pos);
            inverse.xform(rel_pos, pos);
            found = rails_of(component)->find_grind_rail(&local_old, &rel_pos, &point, &node, nullptr, kOne, 0);
            if (found) {
                grind_rails_ = rails_of(component);
                maths::Vec4 world_point;
                point = world.xform(world_point, point);
                grind_object_ = component->entity_;
            }
        }
        if (!found)
            return false;
    }
    // The core physics' say, and nothing in the way to it (a wall within 6 of the rail is fine).
    if (!core_physics_->can_grind_rail(node, grind_rails_, true))
        return false;
    render::ProbeRay feeler(&pos, &point);
    if (feeler.get_hit_shape(true, false)) {
        double dz = static_cast<double>(point.z) - feeler.contact_pos_.z;
        double dy = static_cast<double>(point.y) - feeler.contact_pos_.y;
        double dx = static_cast<double>(point.x) - feeler.contact_pos_.x;
        if ((dx * dx + dy * dy) + dz * dz > kGrindClearance)
            return false;
    }
    if (script::fetch_int_value(kOutputTrackingLines))
        debug::printf(kTrackingFormat, 2, static_cast<double>(old_pos.x), static_cast<double>(old_pos.y),
                      static_cast<double>(old_pos.z), static_cast<double>(pos.x), static_cast<double>(pos.y),
                      static_cast<double>(pos.z));
    leave_for_ = 1;
    anim_state_ = kRail;
    return true;
}

} // namespace objects
