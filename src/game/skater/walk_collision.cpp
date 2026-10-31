// The Walk component's collision checks (GameObjects::CFootMovement, see walk_physics.h): the curb float, the ceiling,
// the walking sticker slap, the ground below ("uber frig"), the critical point's offset, the jump over an
// obstruction, the wallplant and the grind. From the disassembly of 0x005264F0-0x00527BD3 and 0x00529160-0x00529CDD.

#include <cmath>
#include <cstdint>
#include <initializer_list>

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
#include "game/obj/trigger_events.h"

namespace GameObjects {

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

// The game's own messages (DebugOut::Printf): "M:%s:%i: %s", the source file's name, and the texts.
const char* const kMessageFormat = "M:%s:%i: %s\n";
const char* const kSourceFile = "mponents\\WalkComponent.cpp";
const char* const kSlapMessage = "walking sticker slap...........\n";
const char* const kUberFrigMessage = "applying uber frig";
const char* const kBadPointMessage = "adjusting position due to bad critical point";
const char* const kAdjustmentFormat = "D:%s:%i: required_pos_adjustment = %g, %g, %g\n";
const char* const kOffsetFormat = "D:%s:%i: m_critical_point_offset = %g, %g, %g\n";
const char* const kTrackingFormat = "Tracking%d %.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n";

double FrameLength(void* walk) { return At<float>(walk, kFrameLength); }

} // namespace

// 0x005264F0
void CFootMovement::UpdateCurbFloat()
{
    // The facing along the ground, never downhill, made unit (its w left as it is).
    Maths::Vec4 ahead = At<Maths::Vec4>(this, kFacing);
    ahead.FlattenOntoPlane(At<Maths::Vec4>(this, kGroundNormal));
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
    Engine3D::CProbeRay feeler;
    feeler.MakeNew();
    const Maths::Vec4& pos = At<Maths::Vec4>(this, kPos);
    feeler.m_line.start = {pos.x, static_cast<float>(static_cast<double>(pos.y) + kHalf), pos.z, 0.0f};
    float reach = WalkParameter(kCurbFloatFeelerLength);
    float reachY = ahead.y * reach;
    float reachX = reach * ahead.x;
    feeler.m_line.end = {reachX + pos.x, static_cast<float>((static_cast<double>(reachY) + pos.y) + kHalf),
                         static_cast<float>(static_cast<double>(ahead.z) * reach + pos.z), 0.0f};
    if (!feeler.GetHitShape(true, false))
        return;
    float distance = feeler.m_reach;

    // Something ahead: straight down onto it from snap_up_height, half a unit past where it was hit.
    Maths::Vec4 over = {ahead.x * kHalf + feeler.m_contactPos.x, ahead.y * kHalf + feeler.m_contactPos.y,
                        ahead.z * kHalf + feeler.m_contactPos.z,
                        static_cast<float>(static_cast<double>(ahead.w) * kHalf + feeler.m_contactPos.w)};
    feeler.m_line.end = over;
    feeler.m_line.start = over;
    feeler.m_line.start.y = static_cast<float>(static_cast<double>(WalkParameter(kSnapUpHeight)) + pos.y);
    if (!feeler.GetHitShape(true, false))
        return;

    // Its height, less the nearer it is, eased toward - unless the float is settled, there's no push and it's
    // slower than min_curb_height_adjust_vel (the original reads it through 0x0043DA70).
    float target = static_cast<float>((static_cast<double>(feeler.m_contactPos.y) - pos.y) * (kOne - static_cast<double>(distance)));
    float& curb = At<float>(this, kCurbOffset);
    const Maths::Vec4& vel = At<Maths::Vec4>(this, kVel);
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
void CFootMovement::CheckCeiling()
{
    if (At<bool>(this, kPushing))
        return;
    float height = WalkParameter(kWalkerHeight);
    float top = height;
    if (At<float>(this, kVerticalVel) < 0.0f)
        top = static_cast<float>(static_cast<double>(height) - kFallingHead);

    // A feeler up from just above the feet to the head.
    Engine3D::CProbeRay feeler;
    feeler.MakeNew();
    Maths::Vec4& pos = At<Maths::Vec4>(this, kPos);
    feeler.m_line.start = {pos.x, static_cast<float>(static_cast<double>(pos.y) + kHeadClearance), pos.z, pos.w};
    feeler.m_line.end = {pos.x, static_cast<float>(static_cast<double>(pos.y) + top), pos.z, pos.w};
    if (!feeler.GetHitShape(true, false))
        return;

    // A ceiling: the velocity into it taken away, and the head moved back out of it.
    Maths::Vec4& vel = At<Maths::Vec4>(this, kVel);
    vel.y = At<float>(this, kVerticalVel);
    const Maths::Vec4& normal = feeler.m_surfaceDir;
    if ((static_cast<double>(normal.y) * vel.y + static_cast<double>(normal.z) * vel.z) + static_cast<double>(normal.x) * vel.x <
        0.0)
        vel.FlattenOntoPlane(normal);
    const Maths::Vec4& end = feeler.m_line.end;
    Maths::Vec4 back = {feeler.m_contactPos.x - end.x, feeler.m_contactPos.y - end.y, feeler.m_contactPos.z - end.z,
                        feeler.m_contactPos.w - end.w};
    At<float>(this, kVerticalVel) = vel.y;
    vel.y = 0.0f;
    back.ProjectOnto(normal);
    feeler.m_line.end = {back.x + pos.x, back.y + pos.y, back.z + pos.z, back.w + pos.w};
    if (feeler.GetHitShape(true, false))
        pos = {feeler.m_contactPos.x + feeler.m_surfaceDir.x, feeler.m_contactPos.y + feeler.m_surfaceDir.y,
               feeler.m_contactPos.z + feeler.m_surfaceDir.z, feeler.m_contactPos.w + feeler.m_surfaceDir.w};
    else
        pos = feeler.m_line.end;
    m_entity->PostOwnEvent(kCeilingEvent, nullptr);
}

// 0x00526E00
void CFootMovement::CheckStickerSlap()
{
    // X and the pad's +0x20 button (m_btnDown) both down, the latter within 30 ms.
    auto* pad = reinterpret_cast<CPadState*>(At<uint8_t*>(this, kInput) + 0x20);
    if (!pad->m_posX.m_heldDown || !pad->m_btnDown.m_heldDown)
        return;
    if (static_cast<uint32_t>(pad->m_btnDown.GetHeldMillis()) > 30)
        return;

    // A wall ahead: a feeler from feeler_height up, feeler_length along the facing (static collision only).
    Engine3D::CProbeRay feeler;
    feeler.MakeNew();
    const Maths::Vec4& facing = At<Maths::Vec4>(this, kFacing);
    Maths::Vec4& start = feeler.m_line.start;
    start = At<Maths::Vec4>(this, kPos);
    start.y = static_cast<float>(static_cast<double>(start.y) + WalkParameter(kFeelerHeight));
    float reach = WalkParameter(kFeelerLength);
    feeler.m_line.end = {reach * facing.x + start.x, reach * facing.y + start.y, reach * facing.z + start.z,
                         static_cast<float>(static_cast<double>(reach) * facing.w + start.w)};
    if (!feeler.GetHitShape(false, false))
        return;

    // High enough, not just wallplanted, not just after leaving the ground, and the physics allows it.
    if (Scripts::FetchFloat(kMinWallplantHeight) > At<float>(At<uint8_t*>(this, kSkaterState), 0x11C))
        return;
    auto sinceAsFloat = [](int then) {
        return static_cast<float>(static_cast<double>(static_cast<uint32_t>(ClockSince(then))));
    };
    float sinceWallplant = sinceAsFloat(At<int>(At<uint8_t*>(this, kCorePhysics), 0x4E0));
    if (Scripts::FetchFloat(kDisallowRewallplant) > sinceWallplant)
        return;
    float sinceState = sinceAsFloat(At<int>(this, kStateTime));
    if (Scripts::FetchFloat(kLateJumpSlop) > sinceState)
        return;
    if (!At<Skater::CorePhysicsComponent*>(this, kCorePhysics)->WallPlant(false, true, false))
        return;
    DebugOut::Printf(kSlapMessage);
    Scripts::LaunchScript(kWalkingWallplantSwitch, nullptr, m_entity, false, nullptr);
}

// 0x00527080
void CFootMovement::UpdateGroundBelow()
{
    float depth = static_cast<float>(static_cast<double>(Scripts::FetchFloat(kUberFrigCurrentHeight)) * kTwelve);
    const Maths::Vec4& pos = At<Maths::Vec4>(this, kPos);
    const Maths::Vec4& offset = At<Maths::Vec4>(this, kCriticalPointOffset);

    // Down from the critical point: the ground below.
    Engine3D::CProbeRay down;
    down.MakeNew();
    float y = pos.y + offset.y;
    Maths::Vec4 point = {pos.x + offset.x, y, pos.z + offset.z, pos.w + offset.w};
    down.m_line.start = {point.x, static_cast<float>(static_cast<double>(y) + kOne), point.z, point.w};
    down.m_line.end = {point.x, static_cast<float>(static_cast<double>(y) - depth), point.z, point.w};
    bool below = down.GetHitShape(true, false);

    // From 12 higher (faces flagged 0x20 only): down to just under that ground, or as deep.
    Engine3D::CProbeRay shade;
    shade.MakeNew();
    shade.SetExcluded(0, 0x20);
    shade.m_line.start = {point.x, static_cast<float>((static_cast<double>(pos.y) + offset.y) + kTwelve), point.z,
                          point.w};
    if (below)
        shade.m_line.end = {down.m_contactPos.x, static_cast<float>(static_cast<double>(down.m_contactPos.y) - kOne),
                            down.m_contactPos.z, down.m_contactPos.w};
    else
        shade.m_line.end = {point.x, static_cast<float>(static_cast<double>(shade.m_line.start.y) - depth), point.z,
                            point.w};
    // The model shaded by the face hit (its vertex value).
    auto shadeModel = [this](Engine3D::CProbeRay* feeler) { At<CMeshInstanceModule*>(this, kModel)->ApplyBrightness(feeler, true, kOne); };
    if (shade.GetHitShape(true, false))
        shadeModel(&shade);
    else if (below)
        shadeModel(&down);

    if (below) {
        // The height above it (the skater state's +0x11C), the shadow on it.
        At<float>(At<uint8_t*>(this, kSkaterState), 0x11C) = static_cast<float>(static_cast<double>(pos.y) - down.m_contactPos.y);
        auto* shadow = static_cast<CGroundShadowModule*>(m_entity->FetchModule(kShadow));
        shadow->ChangePlace(&down.m_contactPos);
        shadow->SetNormal(&down.m_surfaceDir);
        return;
    }

    // Nothing below at all: back to where the frame began, turned around, on the ground.
    DebugOut::Printf(kMessageFormat, kSourceFile, 0x941, kUberFrigMessage);
    At<Maths::Vec4>(this, kPos) = At<Maths::Vec4>(this, kOldPos);
    Maths::Vec4& vel = At<Maths::Vec4>(this, kVel);
    vel.y = 0.0f;
    vel.w = 1.0f;
    vel.x = vel.x * kMinus;
    vel.z = vel.z * kMinus;
    Maths::Vec4& facing = At<Maths::Vec4>(this, kFacing);
    facing = {facing.x * kMinus, facing.y * kMinus, facing.z * kMinus, facing.w * kMinus};
    At<float>(this, kVerticalVel) = 0.0f;
    At<bool>(this, kForwardControlLock) = true;
    At<int>(this, kFrames710) = 60;
    ChangePhase(0);
    At<bool>(this, kFlag1864) = false;
    At<Maths::Vec4>(this, kGroundNormal) = {0.0f, 1.0f, 0.0f, 1.0f};
    At<uint32_t>(this, kAnimState) = kLand;
}

// 0x005278D0
void CFootMovement::EaseCriticalPointOffset()
{
    Maths::Vec4& offset = At<Maths::Vec4>(this, kCriticalPointOffset);
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
    Maths::Vec4 old = offset;
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
    Engine3D::CProbeRay feeler;
    feeler.MakeNew();
    const Maths::Vec4& oldPos = At<Maths::Vec4>(this, kOldPos);
    Maths::Vec4& pos = At<Maths::Vec4>(this, kPos);
    feeler.m_line.start = {old.x + oldPos.x, old.y + oldPos.y, old.z + oldPos.z, old.w + oldPos.w};
    feeler.m_line.end = {pos.x + offset.x, pos.y + offset.y, pos.z + offset.z, pos.w + offset.w};
    if (feeler.GetHitShape(true, false)) {
        Maths::Vec4 adjustment = (feeler.m_contactPos + kAdjustClearance * feeler.m_surfaceDir) - feeler.m_line.end;
        pos += adjustment;
        DebugOut::Printf(kMessageFormat, kSourceFile, 0xB8C, kBadPointMessage);
        DebugOut::Printf(kAdjustmentFormat, kSourceFile, 0xB8D, static_cast<double>(adjustment.x),
                    static_cast<double>(adjustment.y), static_cast<double>(adjustment.z));
        DebugOut::Printf(kOffsetFormat, kSourceFile, 0xB8E, static_cast<double>(offset.x),
                    static_cast<double>(offset.y), static_cast<double>(offset.z));
    }
    // Pushed back off a wall: the critical point kept on its side of the plane.
    if (At<bool>(this, kPushing)) {
        Maths::Vec4 point = pos + offset;
        const Maths::Vec4& normal = At<Maths::Vec4>(this, kPushNormal);
        double past = ((static_cast<double>(point.z) * normal.z + static_cast<double>(point.y) * normal.y) +
                       static_cast<double>(point.x) * normal.x) -
                      At<float>(this, kPushPlaneDistance);
        if (past > 0.0)
            pos -= static_cast<float>(past) * normal;
    }
}

// 0x00529160
void CFootMovement::PlanJumpOverObstruction()
{
    // A feeler up from the head by jump_obstruction_check_height (static collision only)...
    Engine3D::CProbeRay feeler;
    feeler.MakeNew();
    Maths::Vec4& start = feeler.m_line.start;
    Maths::Vec4& end = feeler.m_line.end;
    start = At<Maths::Vec4>(this, kPos);
    start.y = static_cast<float>(static_cast<double>(start.y) + WalkParameter(kWalkerHeight));
    end = start;
    end.y = static_cast<float>(static_cast<double>(end.y) + WalkParameter(kJumpObstructionCheckHeight));
    if (!feeler.GetHitShape(false, false))
        return;
    // ... meets something: the same jump_obstruction_check_back further back must be clear.
    float back = WalkParameter(kJumpObstructionCheckBack);
    float height = feeler.m_contactPos.y - start.y;
    const Maths::Vec4& facing = At<Maths::Vec4>(this, kFacing);
    for (Maths::Vec4* v : {&start, &end}) {
        v->x = static_cast<float>(v->x - static_cast<double>(back) * facing.x);
        v->y = static_cast<float>(v->y - static_cast<double>(back) * facing.y);
        v->z = static_cast<float>(v->z - static_cast<double>(back) * facing.z);
        v->w = static_cast<float>(v->w - static_cast<double>(back) * facing.w);
    }
    if (feeler.GetHitShape(false, false))
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
    At<Maths::Vec4>(this, kJumpVelocity) = {speedF * facing.x, speedF * facing.y, speedF * facing.z,
                                            static_cast<float>(speed * facing.w)};
    At<float>(this, kJumpTargetHeight) = static_cast<float>(static_cast<double>(height) + At<Maths::Vec4>(this, kPos).y);
    At<float>(this, kJumpTime) = static_cast<float>((inverse * rise + inverse * rise) - time);
}

// 0x005293F0
void CFootMovement::TryWallplant()
{
    // X down, not rising, high enough, not just wallplanted, not just after leaving the ground...
    auto* pad = reinterpret_cast<CPadState*>(At<uint8_t*>(this, kInput) + 0x20);
    if (!pad->m_posX.m_heldDown || At<float>(this, kVerticalVel) > 0.0f)
        return;
    if (Scripts::FetchFloat(kMinWallplantHeight) > At<float>(At<uint8_t*>(this, kSkaterState), 0x11C))
        return;
    auto sinceAsFloat = [](int then) {
        return static_cast<float>(static_cast<double>(static_cast<uint32_t>(ClockSince(then))));
    };
    float sinceWallplant = sinceAsFloat(At<int>(At<uint8_t*>(this, kCorePhysics), 0x4E0));
    if (Scripts::FetchFloat(kDisallowRewallplant) > sinceWallplant)
        return;
    float sinceState = sinceAsFloat(At<int>(this, kStateTime));
    if (Scripts::FetchFloat(kLateJumpSlop) > sinceState)
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
    Maths::Vec4 direction = At<float>(this, kControlMagnitude) == 0.0f ? At<Maths::Vec4>(this, kJumpDirection)
                                                                      : At<Maths::Vec4>(this, kControlDirection);
    const Maths::Vec4& normal = At<Maths::Vec4>(contact, 4);
    double into = (static_cast<double>(direction.z) * normal.z + static_cast<double>(direction.y) * normal.y) +
                  static_cast<double>(direction.x) * normal.x;
    if (into < 0.0)
        direction -= Maths::operator*(static_cast<float>(into + into), normal);
    float rise = WalkParameter(kVertWallJumpSpeed);
    float hold = WalkParameter(kHoldTimeForMaxJump);
    double held = static_cast<double>(static_cast<uint32_t>(pad->m_posX.GetHeldMillis())) / hold;
    float strength = held > kOne ? kOne : static_cast<float>(held);
    float least = WalkParameter(kMinJumpFactor);
    At<float>(this, kVerticalVel) = static_cast<float>(((kOne - static_cast<double>(least)) * strength + least) * rise);
    float across = WalkParameter(kHorizWallJumpSpeed);
    At<Maths::Vec4>(this, kVel) = {across * direction.x, direction.y * across, direction.z * across, direction.w * across};
    At<Maths::Vec4>(this, kJumpDirection) = At<Maths::Vec4>(this, kFacing);
    // Off a moving object: its velocity added.
    auto& feeler = At<Engine3D::CProbeRay>(contact, 0x1C);
    if (feeler.HitObjectAlive()) {
        const Maths::Vec4& carried = feeler.GetHitObject()->m_vel;
        At<float>(this, kVerticalVel) = carried.y + At<float>(this, kVerticalVel);
        At<Maths::Vec4>(this, kVel).x = carried.x + At<Maths::Vec4>(this, kVel).x;
        At<Maths::Vec4>(this, kVel).z = carried.z + At<Maths::Vec4>(this, kVel).z;
    }
    PlanJumpOverObstruction();
    // The trigger told (events 0x0064C63C); a trigger's script may take over.
    At<CActivationZoneModule*>(this, kTrigger)->CheckFeeler(g_walkEvents0064C63C, &feeler);
    if (At<bool>(At<uint8_t*>(this, kPhysicsControl), 0x26))
        return;
    // The wall's checksum (the feeler hit's +0x34) to the trick objects; the wallplant's time; its animation.
    static_cast<CMoveExecutor*>(m_entity->FetchModule(kTrick))->PassToTrickObjects(At<uint32_t>(contact, 0x74));
    At<int>(At<uint8_t*>(this, kCorePhysics), 0x4E0) = FrameClock::GameClockElapsed() + g_clockOffset;
    At<uint32_t>(this, kAnimState) = kWallplant;
}

// 0x005299F0
bool CFootMovement::TryGrind()
{
    if (At<bool>(At<uint8_t*>(this, kPhysicsControl), 0x24))
        return false;
    // A rail along the move: the level's, else one riding an object (in that object's space).
    Maths::Vec4& oldPos = At<Maths::Vec4>(this, kOldPos);
    Maths::Vec4& pos = At<Maths::Vec4>(this, kPos);
    Maths::Vec4& point = At<Maths::Vec4>(this, kGrindPoint);
    CGrindPoint*& node = At<CGrindPoint*>(this, kGrindNode);
    CGrindRailHub* levelRails = At<CGrindRailHub*>(static_cast<void*>(GameModules::g_skateModule), 0x24);
    bool found = levelRails->FindGrindRail(&oldPos, &pos, &point, &node, nullptr, kOne, 0);
    if (found) {
        At<CGrindRailHub*>(this, kGrindRails) = levelRails;
        At<CGameEntity*>(this, kGrindObject) = nullptr;
    } else {
        oldPos.w = kOne;
        pos.w = kOne;
        CObjectModule* component = CompositeObjectManager()->FindFirstModuleOfKind(kRailManagerType);
        if (!component)
            return false;
        // (vtable +0x34: the component's rails)
        auto railsOf = [](CObjectModule* c) { return reinterpret_cast<CGrindRailHub*(__thiscall*)(CObjectModule*)>(c->m_vtable[13])(c); };
        for (; component && !found; component = component->mp_nextOfType) {
            Maths::Mat4 world;
            static_cast<CGrindPathLinkModule*>(component)->UpdateRailMatrix(&world);
            Maths::Mat4 inverse = world;
            inverse.Upend();
            Maths::Vec4 localOld, relPos;
            inverse.Xform(localOld, oldPos);
            inverse.Xform(relPos, pos);
            found = railsOf(component)->FindGrindRail(&localOld, &relPos, &point, &node, nullptr, kOne, 0);
            if (found) {
                At<CGrindRailHub*>(this, kGrindRails) = railsOf(component);
                Maths::Vec4 worldPoint;
                point = world.Xform(worldPoint, point);
                At<CGameEntity*>(this, kGrindObject) = component->m_entity;
            }
        }
        if (!found)
            return false;
    }
    // The core physics' say, and nothing in the way to it (a wall within 6 of the rail is fine).
    if (!At<Skater::CorePhysicsComponent*>(this, kCorePhysics)->CanGrindRail(node, At<CGrindRailHub*>(this, kGrindRails),
                                                                             true))
        return false;
    Engine3D::CProbeRay feeler;
    feeler.MakeNew(&pos, &point);
    if (feeler.GetHitShape(true, false)) {
        double dz = static_cast<double>(point.z) - feeler.m_contactPos.z;
        double dy = static_cast<double>(point.y) - feeler.m_contactPos.y;
        double dx = static_cast<double>(point.x) - feeler.m_contactPos.x;
        if ((dx * dx + dy * dy) + dz * dz > kGrindClearance)
            return false;
    }
    if (Scripts::FetchIntValue(kOutputTrackingLines))
        DebugOut::Printf(kTrackingFormat, 2, static_cast<double>(oldPos.x), static_cast<double>(oldPos.y),
                    static_cast<double>(oldPos.z), static_cast<double>(pos.x), static_cast<double>(pos.y),
                    static_cast<double>(pos.z));
    At<int>(this, kLeaveFor) = 1;
    At<uint32_t>(this, kAnimState) = kRail;
    return true;
}

} // namespace Obj
