// The Walk component's collision checks (Obj::CWalkComponent, see walk_physics.h): the curb float, the ceiling,
// the walking sticker slap, the ground below ("uber frig"), the critical point's offset, the jump over an
// obstruction, the wallplant and the grind. From the disassembly of 0x005264F0-0x00527BD3 and 0x00529160-0x00529CDD.

#include <cmath>
#include <cstdint>

#include "game/core/dbg.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/nx/feeler.h"
#include "game/obj/model_component.h"
#include "game/obj/node_components.h"
#include "game/obj/shadow_component.h"
#include "game/obj/trigger_component.h"
#include "game/obj/compositemgr.h"
#include "game/obj/control_pad.h"
#include "game/obj/object.h"
#include "game/obj/rail_manager.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skater/core_physics.h"
#include "game/skater/trick_component.h"
#include "game/skater/walk_fields.h"
#include "game/skater/walk_physics.h"
#include "game/sys/game_clock.h"
#include "game/skate/skate_module.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

using namespace WalkFields;

// walk_parameters' names.
constexpr uint32_t kCurbFloatFeelerLength = 0x11EDCC52, kSnapUpHeight = 0xCEE3A3E1,
                   kMinCurbHeightAdjustVel = 0x227D72EE, kCurbFloatLerpUpRate = 0x856A80D3,
                   kWalkerHeight = 0x9EA1974A, kFeelerHeight = 0x6DA7F696, kFeelerLength = 0x99978D2B,
                   kJumpObstructionCheckHeight = 0x48C0AC2A, kJumpObstructionCheckBack = 0x06EBBAB7,
                   kVertWallJumpSpeed = 0x420D18BC, kHoldTimeForMaxJump = 0x12333EBD, kMinJumpFactor = 0x246D0BF3,
                   kHorizWallJumpSpeed = 0x99510695;
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

constexpr float kOne = 1.0f;          // 0x006453A8
constexpr float kHalf = 0.5f;         // 0x006453BC
constexpr float kCurbStill = 0.01f;   // 0x0064544C
constexpr float kHeadClearance = 3.0f; // 0x006467A8
constexpr float kFallingHead = 18.0f;  // 0x0064AAAC: the head's reach is this much less while falling
constexpr float kTwelve = 12.0f;       // 0x0064546C (uber_frig_current_height is in feet)
constexpr float kMinus = -1.0f;        // 0x00645420
constexpr float kOffsetEaseRate = 1000.0f; // 0x006453C0: the critical point's offset eased out at this a second
constexpr float kAdjustClearance = 0.1f;   // the bad critical point's adjustment: this far off the face hit
constexpr float kGrindClearance = 36.0f;   // 0x0064B9CC: a wall this close (squared) to the rail's point is fine

// The game's own messages (Dbg::Printf): "M:%s:%i: %s", the source file's name, and the texts.
const char* Text(uint32_t address) { return reinterpret_cast<const char*>(address); }
constexpr uint32_t kMessageFormat = 0x006466B0, kSourceFile = 0x0064C69C, kSlapMessage = 0x0064C66C,
                   kUberFrigMessage = 0x0064C6B8, kBadPointMessage = 0x0064C72C, kAdjustmentFormat = 0x0064C6FC,
                   kOffsetFormat = 0x0064C6CC, kTrackingFormat = 0x0064B998;

double FrameLength(void* walk) { return At<float>(walk, kFrameLength); }

} // namespace

// 0x005264F0
void CWalkComponent::UpdateCurbFloat()
{
    // The facing along the ground, never downhill, made unit (its w left as it is).
    Mth::Vector ahead = At<Mth::Vector>(this, kFacing);
    ahead.ProjectToPlane(At<Mth::Vector>(this, kGroundNormal));
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
    Nx::CFeeler feeler;
    feeler.Construct();
    const Mth::Vector& pos = At<Mth::Vector>(this, kPos);
    feeler.m_line.start = {pos.x, static_cast<float>(static_cast<double>(pos.y) + kHalf), pos.z, 0.0f};
    float reach = WalkParameter(kCurbFloatFeelerLength);
    float reachY = ahead.y * reach;
    float reachX = reach * ahead.x;
    feeler.m_line.end = {reachX + pos.x, static_cast<float>((static_cast<double>(reachY) + pos.y) + kHalf),
                         static_cast<float>(static_cast<double>(ahead.z) * reach + pos.z), 0.0f};
    if (!feeler.GetCollision(true, false))
        return;
    float distance = feeler.m_distance;

    // Something ahead: straight down onto it from snap_up_height, half a unit past where it was hit.
    Mth::Vector over = {ahead.x * kHalf + feeler.m_point.x, ahead.y * kHalf + feeler.m_point.y,
                        ahead.z * kHalf + feeler.m_point.z,
                        static_cast<float>(static_cast<double>(ahead.w) * kHalf + feeler.m_point.w)};
    feeler.m_line.end = over;
    feeler.m_line.start = over;
    feeler.m_line.start.y = static_cast<float>(static_cast<double>(WalkParameter(kSnapUpHeight)) + pos.y);
    if (!feeler.GetCollision(true, false))
        return;

    // Its height, less the nearer it is, eased toward - unless the float is settled, there's no push and it's
    // slower than min_curb_height_adjust_vel (the original reads it through 0x0043DA70).
    float target = static_cast<float>((static_cast<double>(feeler.m_point.y) - pos.y) * (kOne - static_cast<double>(distance)));
    float& curb = At<float>(this, kCurbOffset);
    const Mth::Vector& vel = At<Mth::Vector>(this, kVel);
    bool settled = std::fabs(curb) < kCurbStill && At<float>(this, kControlMagnitude) == 0.0f;
    if (settled) {
        double least = WalkParameter(kMinCurbHeightAdjustVel);
        double speedSquared = (static_cast<double>(vel.x) * vel.x + static_cast<double>(vel.y) * vel.y) +
                              static_cast<double>(vel.z) * vel.z;
        settled = least * least > speedSquared;
    }
    if (!settled) {
        double rate = static_cast<double>(WalkParameter(kCurbFloatLerpUpRate)) * FrameLength(this);
        curb = static_cast<float>((static_cast<double>(target) - curb) * rate + curb);
    }
    At<bool>(this, kCurbFloat) = true;
}

// 0x00526BA0
void CWalkComponent::CheckCeiling()
{
    if (At<bool>(this, kPushing))
        return;
    float height = WalkParameter(kWalkerHeight);
    float top = height;
    if (At<float>(this, kVerticalVel) < 0.0f)
        top = static_cast<float>(static_cast<double>(height) - kFallingHead);

    // A feeler up from just above the feet to the head.
    Nx::CFeeler feeler;
    feeler.Construct();
    Mth::Vector& pos = At<Mth::Vector>(this, kPos);
    feeler.m_line.start = {pos.x, static_cast<float>(static_cast<double>(pos.y) + kHeadClearance), pos.z, pos.w};
    feeler.m_line.end = {pos.x, static_cast<float>(static_cast<double>(pos.y) + top), pos.z, pos.w};
    if (!feeler.GetCollision(true, false))
        return;

    // A ceiling: the velocity into it taken away, and the head moved back out of it.
    Mth::Vector& vel = At<Mth::Vector>(this, kVel);
    vel.y = At<float>(this, kVerticalVel);
    const Mth::Vector& normal = feeler.m_normal;
    if ((static_cast<double>(normal.y) * vel.y + static_cast<double>(normal.z) * vel.z) + static_cast<double>(normal.x) * vel.x <
        0.0)
        vel.ProjectToPlane(normal);
    const Mth::Vector& end = feeler.m_line.end;
    Mth::Vector back = {feeler.m_point.x - end.x, feeler.m_point.y - end.y, feeler.m_point.z - end.z,
                        feeler.m_point.w - end.w};
    At<float>(this, kVerticalVel) = vel.y;
    vel.y = 0.0f;
    back.ProjectOnto(normal);
    feeler.m_line.end = {back.x + pos.x, back.y + pos.y, back.z + pos.z, back.w + pos.w};
    if (feeler.GetCollision(true, false))
        pos = {feeler.m_point.x + feeler.m_normal.x, feeler.m_point.y + feeler.m_normal.y,
               feeler.m_point.z + feeler.m_normal.z, feeler.m_point.w + feeler.m_normal.w};
    else
        pos = feeler.m_line.end;
    m_object->SelfEvent(kCeilingEvent, nullptr);
}

// 0x00526E00
void CWalkComponent::CheckStickerSlap()
{
    // X and the pad's +0x20 button (m_down) both down, the latter within 30 ms.
    auto* pad = reinterpret_cast<CControlPad*>(At<uint8_t*>(this, kInput) + 0x20);
    if (!pad->m_x.m_pressed || !pad->m_down.m_pressed)
        return;
    if (static_cast<uint32_t>(pad->m_down.GetPressedTime()) > 30)
        return;

    // A wall ahead: a feeler from feeler_height up, feeler_length along the facing (static collision only).
    Nx::CFeeler feeler;
    feeler.Construct();
    const Mth::Vector& facing = At<Mth::Vector>(this, kFacing);
    Mth::Vector& start = feeler.m_line.start;
    start = At<Mth::Vector>(this, kPos);
    start.y = static_cast<float>(static_cast<double>(start.y) + WalkParameter(kFeelerHeight));
    float reach = WalkParameter(kFeelerLength);
    feeler.m_line.end = {reach * facing.x + start.x, reach * facing.y + start.y, reach * facing.z + start.z,
                         static_cast<float>(static_cast<double>(reach) * facing.w + start.w)};
    if (!feeler.GetCollision(false, false))
        return;

    // High enough, not just wallplanted, not just after leaving the ground, and the physics allows it.
    if (Script::GetFloat(kMinWallplantHeight) > At<float>(At<uint8_t*>(this, kSkaterState), 0x11C))
        return;
    auto sinceAsFloat = [](int then) {
        return static_cast<float>(static_cast<double>(static_cast<uint32_t>(ClockSince(then))));
    };
    float sinceWallplant = sinceAsFloat(At<int>(At<uint8_t*>(this, kCorePhysics), 0x4E0));
    if (Script::GetFloat(kDisallowRewallplant) > sinceWallplant)
        return;
    float sinceState = sinceAsFloat(At<int>(this, kStateTime));
    if (Script::GetFloat(kLateJumpSlop) > sinceState)
        return;
    if (!At<Skater::CorePhysicsComponent*>(this, kCorePhysics)->WallPlant(false, true, false))
        return;
    Dbg::Printf(Text(kSlapMessage));
    Script::RunScript(kWalkingWallplantSwitch, nullptr, m_object, false, nullptr);
}

// 0x00527080
void CWalkComponent::UpdateGroundBelow()
{
    float depth = static_cast<float>(static_cast<double>(Script::GetFloat(kUberFrigCurrentHeight)) * kTwelve);
    const Mth::Vector& pos = At<Mth::Vector>(this, kPos);
    const Mth::Vector& offset = At<Mth::Vector>(this, kCriticalPointOffset);

    // Down from the critical point: the ground below.
    Nx::CFeeler down;
    down.Construct();
    float y = pos.y + offset.y;
    Mth::Vector point = {pos.x + offset.x, y, pos.z + offset.z, pos.w + offset.w};
    down.m_line.start = {point.x, static_cast<float>(static_cast<double>(y) + kOne), point.z, point.w};
    down.m_line.end = {point.x, static_cast<float>(static_cast<double>(y) - depth), point.z, point.w};
    bool below = down.GetCollision(true, false);

    // From 12 higher (faces flagged 0x20 only): down to just under that ground, or as deep.
    Nx::CFeeler shade;
    shade.Construct();
    shade.SetIgnore(0, 0x20);
    shade.m_line.start = {point.x, static_cast<float>((static_cast<double>(pos.y) + offset.y) + kTwelve), point.z,
                          point.w};
    if (below)
        shade.m_line.end = {down.m_point.x, static_cast<float>(static_cast<double>(down.m_point.y) - kOne),
                            down.m_point.z, down.m_point.w};
    else
        shade.m_line.end = {point.x, static_cast<float>(static_cast<double>(shade.m_line.start.y) - depth), point.z,
                            point.w};
    // The model shaded by the face hit (its vertex value).
    auto shadeModel = [this](Nx::CFeeler* feeler) { At<CModelComponent*>(this, kModel)->ApplyBrightness(feeler, true, kOne); };
    if (shade.GetCollision(true, false))
        shadeModel(&shade);
    else if (below)
        shadeModel(&down);

    if (below) {
        // The height above it (the skater state's +0x11C), the shadow on it.
        At<float>(At<uint8_t*>(this, kSkaterState), 0x11C) = static_cast<float>(static_cast<double>(pos.y) - down.m_point.y);
        auto* shadow = static_cast<CShadowComponent*>(m_object->GetComponent(kShadow));
        shadow->SetPosition(&down.m_point);
        shadow->SetNormal(&down.m_normal);
        return;
    }

    // Nothing below at all: back to where the frame began, turned around, on the ground.
    Dbg::Printf(Text(kMessageFormat), Text(kSourceFile), 0x941, Text(kUberFrigMessage));
    At<Mth::Vector>(this, kPos) = At<Mth::Vector>(this, kOldPos);
    Mth::Vector& vel = At<Mth::Vector>(this, kVel);
    vel.y = 0.0f;
    vel.w = 1.0f;
    vel.x = vel.x * kMinus;
    vel.z = vel.z * kMinus;
    Mth::Vector& facing = At<Mth::Vector>(this, kFacing);
    facing = {facing.x * kMinus, facing.y * kMinus, facing.z * kMinus, facing.w * kMinus};
    At<float>(this, kVerticalVel) = 0.0f;
    At<bool>(this, kForwardControlLock) = true;
    At<int>(this, kFrames710) = 60;
    SetState(0);
    At<bool>(this, kFlag1864) = false;
    At<Mth::Vector>(this, kGroundNormal) = {0.0f, 1.0f, 0.0f, 1.0f};
    At<uint32_t>(this, kAnimState) = kLand;
}

// 0x005278D0
void CWalkComponent::EaseCriticalPointOffset()
{
    Mth::Vector& offset = At<Mth::Vector>(this, kCriticalPointOffset);
    float lengthSquared = static_cast<float>(
        (static_cast<double>(offset.x) * offset.x + static_cast<double>(offset.y) * offset.y) +
        static_cast<double>(offset.z) * offset.z);
    if (lengthSquared == 0.0f)
        return;
    int state = At<int>(this, kState);
    if (state < 0 || (state > 1 && state != 3))
        return;

    // Eased out at 1000 a second.
    double step = FrameLength(this) * kOffsetEaseRate;
    Mth::Vector old = offset;
    double length = std::sqrt(static_cast<double>(lengthSquared));
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
    Nx::CFeeler feeler;
    feeler.Construct();
    const Mth::Vector& oldPos = At<Mth::Vector>(this, kOldPos);
    Mth::Vector& pos = At<Mth::Vector>(this, kPos);
    feeler.m_line.start = {old.x + oldPos.x, old.y + oldPos.y, old.z + oldPos.z, old.w + oldPos.w};
    feeler.m_line.end = {pos.x + offset.x, pos.y + offset.y, pos.z + offset.z, pos.w + offset.w};
    if (feeler.GetCollision(true, false)) {
        Mth::Vector adjustment = (feeler.m_point + kAdjustClearance * feeler.m_normal) - feeler.m_line.end;
        pos += adjustment;
        Dbg::Printf(Text(kMessageFormat), Text(kSourceFile), 0xB8C, Text(kBadPointMessage));
        Dbg::Printf(Text(kAdjustmentFormat), Text(kSourceFile), 0xB8D, static_cast<double>(adjustment.x),
                    static_cast<double>(adjustment.y), static_cast<double>(adjustment.z));
        Dbg::Printf(Text(kOffsetFormat), Text(kSourceFile), 0xB8E, static_cast<double>(offset.x),
                    static_cast<double>(offset.y), static_cast<double>(offset.z));
    }
    // Pushed back off a wall: the critical point kept on its side of the plane.
    if (At<bool>(this, kPushing)) {
        Mth::Vector point = pos + offset;
        const Mth::Vector& normal = At<Mth::Vector>(this, kPushNormal);
        double past = ((static_cast<double>(point.z) * normal.z + static_cast<double>(point.y) * normal.y) +
                       static_cast<double>(point.x) * normal.x) -
                      At<float>(this, kPushPlaneDistance);
        if (past > 0.0)
            pos -= static_cast<float>(past) * normal;
    }
}

// 0x00529160
void CWalkComponent::PlanJumpOverObstruction()
{
    // A feeler up from the head by jump_obstruction_check_height (static collision only)...
    Nx::CFeeler feeler;
    feeler.Construct();
    Mth::Vector& start = feeler.m_line.start;
    Mth::Vector& end = feeler.m_line.end;
    start = At<Mth::Vector>(this, kPos);
    start.y = static_cast<float>(static_cast<double>(start.y) + WalkParameter(kWalkerHeight));
    end = start;
    end.y = static_cast<float>(static_cast<double>(end.y) + WalkParameter(kJumpObstructionCheckHeight));
    if (!feeler.GetCollision(false, false))
        return;
    // ... meets something: the same jump_obstruction_check_back further back must be clear.
    float back = WalkParameter(kJumpObstructionCheckBack);
    float height = feeler.m_point.y - start.y;
    const Mth::Vector& facing = At<Mth::Vector>(this, kFacing);
    for (Mth::Vector* v : {&start, &end}) {
        v->x = static_cast<float>(v->x - static_cast<double>(back) * facing.x);
        v->y = static_cast<float>(v->y - static_cast<double>(back) * facing.y);
        v->z = static_cast<float>(v->z - static_cast<double>(back) * facing.z);
        v->w = static_cast<float>(v->w - static_cast<double>(back) * facing.w);
    }
    if (feeler.GetCollision(false, false))
        return;

    // A ledge: the jump to it - the time to rise `height` at the vertical speed under gravity, and the speed back
    // toward it to cover `back` in that time.
    double gravity = GetGravity();
    double rise = At<float>(this, kVerticalVel);
    double discriminant = rise * rise - (gravity * height + gravity * height);
    if (!(discriminant > 0.0))
        return;
    double inverse = kOne / gravity;
    double time = (rise - std::sqrt(static_cast<double>(static_cast<float>(discriminant)))) * inverse;
    double speed = -(back / time);
    float speedF = static_cast<float>(speed);
    At<Mth::Vector>(this, kJumpVelocity) = {speedF * facing.x, speedF * facing.y, speedF * facing.z,
                                            static_cast<float>(speed * facing.w)};
    At<float>(this, kJumpTargetHeight) = static_cast<float>(static_cast<double>(height) + At<Mth::Vector>(this, kPos).y);
    At<float>(this, kJumpTime) = static_cast<float>((inverse * rise + inverse * rise) - time);
}

// 0x005293F0
void CWalkComponent::TryWallplant()
{
    // X down, not rising, high enough, not just wallplanted, not just after leaving the ground...
    auto* pad = reinterpret_cast<CControlPad*>(At<uint8_t*>(this, kInput) + 0x20);
    if (!pad->m_x.m_pressed || At<float>(this, kVerticalVel) > 0.0f)
        return;
    if (Script::GetFloat(kMinWallplantHeight) > At<float>(At<uint8_t*>(this, kSkaterState), 0x11C))
        return;
    auto sinceAsFloat = [](int then) {
        return static_cast<float>(static_cast<double>(static_cast<uint32_t>(ClockSince(then))));
    };
    float sinceWallplant = sinceAsFloat(At<int>(At<uint8_t*>(this, kCorePhysics), 0x4E0));
    if (Script::GetFloat(kDisallowRewallplant) > sinceWallplant)
        return;
    float sinceState = sinceAsFloat(At<int>(this, kStateTime));
    if (Script::GetFloat(kLateJumpSlop) > sinceState)
        return;
    // ... and touching a wall: the first contact in this order.
    static const int kOrder[7] = {0, 1, 6, 2, 5, 3, 4};
    uint8_t* contact = nullptr;
    for (int index : kOrder) {
        uint8_t* candidate = &At<uint8_t>(this, kContacts + index * 0xCC);
        if (*candidate) {
            contact = candidate;
            break;
        }
    }
    if (!contact)
        return;

    // Off the wall: the way pushed (or the jump's way) mirrored off it, up by the hold of X.
    Mth::Vector direction = At<float>(this, kControlMagnitude) == 0.0f ? At<Mth::Vector>(this, kJumpDirection)
                                                                      : At<Mth::Vector>(this, kControlDirection);
    const Mth::Vector& normal = At<Mth::Vector>(contact, 4);
    double into = (static_cast<double>(direction.z) * normal.z + static_cast<double>(direction.y) * normal.y) +
                  static_cast<double>(direction.x) * normal.x;
    if (into < 0.0)
        direction -= Mth::operator*(static_cast<float>(into + into), normal);
    float rise = WalkParameter(kVertWallJumpSpeed);
    float hold = WalkParameter(kHoldTimeForMaxJump);
    double held = static_cast<double>(static_cast<uint32_t>(pad->m_x.GetPressedTime())) / hold;
    float strength = held > kOne ? kOne : static_cast<float>(held);
    float least = WalkParameter(kMinJumpFactor);
    At<float>(this, kVerticalVel) = static_cast<float>(((kOne - static_cast<double>(least)) * strength + least) * rise);
    float across = WalkParameter(kHorizWallJumpSpeed);
    At<Mth::Vector>(this, kVel) = {across * direction.x, direction.y * across, direction.z * across, direction.w * across};
    At<Mth::Vector>(this, kJumpDirection) = At<Mth::Vector>(this, kFacing);
    // Off a moving object: its velocity added.
    auto& feeler = At<Nx::CFeeler>(contact, 0x1C);
    if (feeler.HitObjectAlive()) {
        const Mth::Vector& carried = feeler.GetHitObject()->m_vel;
        At<float>(this, kVerticalVel) = carried.y + At<float>(this, kVerticalVel);
        At<Mth::Vector>(this, kVel).x = carried.x + At<Mth::Vector>(this, kVel).x;
        At<Mth::Vector>(this, kVel).z = carried.z + At<Mth::Vector>(this, kVel).z;
    }
    PlanJumpOverObstruction();
    // The trigger told (events 0x0064C63C); a trigger's script may take over.
    At<CTriggerComponent*>(this, kTrigger)->CheckFeeler(reinterpret_cast<const int*>(0x0064C63C), &feeler);
    if (At<bool>(At<uint8_t*>(this, kPhysicsControl), 0x26))
        return;
    // The wall's checksum (the feeler hit's +0x34) to the trick objects; the wallplant's time; its animation.
    static_cast<CTrickComponent*>(m_object->GetComponent(kTrick))->PassToTrickObjects(At<uint32_t>(contact, 0x74));
    At<int>(At<uint8_t*>(this, kCorePhysics), 0x4E0) = Tmr::GameClockElapsed() + g_clockOffset;
    At<uint32_t>(this, kAnimState) = kWallplant;
}

// 0x005299F0
bool CWalkComponent::TryGrind()
{
    if (At<bool>(At<uint8_t*>(this, kPhysicsControl), 0x24))
        return false;
    // A rail along the move: the level's, else one riding an object (in that object's space).
    Mth::Vector& oldPos = At<Mth::Vector>(this, kOldPos);
    Mth::Vector& pos = At<Mth::Vector>(this, kPos);
    Mth::Vector& point = At<Mth::Vector>(this, kGrindPoint);
    CRailNode*& node = At<CRailNode*>(this, kGrindNode);
    CRailManager* levelRails = At<CRailManager*>(static_cast<void*>(Mdl::g_skateModule), 0x24);
    bool found = levelRails->FindGrindRail(&oldPos, &pos, &point, &node, nullptr, kOne, 0);
    if (found) {
        At<CRailManager*>(this, kGrindRails) = levelRails;
        At<CCompositeObject*>(this, kGrindObject) = nullptr;
    } else {
        oldPos.w = kOne;
        pos.w = kOne;
        CBaseComponent* component = CompositeObjectManager()->GetFirstComponentByType(kRailManagerType);
        if (!component)
            return false;
        // (vtable +0x34: the component's rails)
        auto railsOf = [](CBaseComponent* c) { return reinterpret_cast<CRailManager*(__thiscall*)(CBaseComponent*)>(c->m_vtable[13])(c); };
        for (; component && !found; component = component->mp_nextOfType) {
            Mth::Matrix world;
            static_cast<CRailManagerComponent*>(component)->UpdateRailMatrix(&world);
            Mth::Matrix inverse = world;
            inverse.Invert();
            Mth::Vector localOld, localPos;
            inverse.Transform(localOld, oldPos);
            inverse.Transform(localPos, pos);
            found = railsOf(component)->FindGrindRail(&localOld, &localPos, &point, &node, nullptr, kOne, 0);
            if (found) {
                At<CRailManager*>(this, kGrindRails) = railsOf(component);
                Mth::Vector worldPoint;
                point = world.Transform(worldPoint, point);
                At<CCompositeObject*>(this, kGrindObject) = component->m_object;
            }
        }
        if (!found)
            return false;
    }
    // The core physics' say, and nothing in the way to it (a wall within 6 of the rail is fine).
    if (!At<Skater::CorePhysicsComponent*>(this, kCorePhysics)->CanGrindRail(node, At<CRailManager*>(this, kGrindRails),
                                                                             true))
        return false;
    Nx::CFeeler feeler;
    feeler.Construct(&pos, &point);
    if (feeler.GetCollision(true, false)) {
        double dz = static_cast<double>(point.z) - feeler.m_point.z;
        double dy = static_cast<double>(point.y) - feeler.m_point.y;
        double dx = static_cast<double>(point.x) - feeler.m_point.x;
        if ((dx * dx + dy * dy) + dz * dz > kGrindClearance)
            return false;
    }
    if (Script::GetInteger(kOutputTrackingLines))
        Dbg::Printf(Text(kTrackingFormat), 2, static_cast<double>(oldPos.x), static_cast<double>(oldPos.y),
                    static_cast<double>(oldPos.z), static_cast<double>(pos.x), static_cast<double>(pos.y),
                    static_cast<double>(pos.z));
    At<int>(this, kLeaveFor) = 1;
    At<uint32_t>(this, kAnimState) = kRail;
    return true;
}

} // namespace Obj

REPLACE(0x00529160, &Obj::CWalkComponent::PlanJumpOverObstruction);
REPLACE(0x005293F0, &Obj::CWalkComponent::TryWallplant);
REPLACE(0x005299F0, &Obj::CWalkComponent::TryGrind);
REPLACE(0x005264F0, &Obj::CWalkComponent::UpdateCurbFloat);
REPLACE(0x00526BA0, &Obj::CWalkComponent::CheckCeiling);
REPLACE(0x00526E00, &Obj::CWalkComponent::CheckStickerSlap);
REPLACE(0x00527080, &Obj::CWalkComponent::UpdateGroundBelow);
REPLACE(0x005278D0, &Obj::CWalkComponent::EaseCriticalPointOffset);
