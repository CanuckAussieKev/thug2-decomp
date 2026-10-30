// The skater's core physics component: getting onto a rail - a point rail's kick or spin, a lip trick, a grind.

#include <cmath>
#include <cstdint>

#include "game/core/dbg.h"
#include "game/mth/math.h"
#include "game/mth/matrix.h"
#include "game/mth/random.h"
#include "game/mth/vector.h"
#include "game/obj/compositemgr.h"
#include "game/obj/movable_contact.h"
#include "game/obj/control_pad.h"
#include "game/obj/object.h"
#include "game/obj/rail_manager.h"
#include "game/obj/node_components.h"
#include "game/obj/ride_tracker.h"
#include "game/obj/trigger_component.h"
#include "game/obj/walk_hang.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "game/skater/core_fields.h"
#include "game/skater/core_physics.h"
#include "game/skater/thaw_bmx.h"
#include "game/skater/flip_and_rotate.h"
#include "game/skater/gap.h"
#include "game/skater/skater.h"
#include "game/skater/skater_sound.h"
#include "game/skater/skater_state.h"
#include "game/skater/trick_component.h"
#include "game/skater/walk_physics.h"
#include "game/sys/game_clock.h"
#include "game/skater/balance_trick.h"
#include "game/skate/score.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Skater {

using namespace CoreFields;

namespace {

constexpr uint32_t kSkaterFlipAndRotate = 0x488AF07A; // (a component)
constexpr uint32_t kSkaterGap = 0x11B4C67F;           // (a component)
constexpr uint32_t kTriggerScript = 0x2CA8A299;
constexpr uint32_t kName = 0xA1DC81F9;
constexpr uint32_t kNatasDelay = 0x0A477668;          // Physics_Natas_Delay (seconds)
constexpr uint32_t kKickUpwardAngle = 0xBB357ECB;     // Physics_Point_Rail_Kick_Upward_Angle
constexpr uint32_t kPointRailSpeedBoost = 0xA3EF4833; // Point_Rail_Speed_Boost
constexpr uint32_t kRailSpeedBoost = 0x457BB395;      // Rail_Speed_Boost
constexpr uint32_t kRerailTime = 0x2B4645AD;          // Rail_minimum_rerail_time
constexpr uint32_t kJumpAdjustSpeed = 0x896C8888;     // jump_adjust_speed
constexpr uint32_t kMaxMaxSpeed = 0xCC5F87AA;         // Skater_Max_Max_Speed_Stat
constexpr uint32_t kLipAllowAngle = 0x8157C5D9;       // LipAllowAngle
constexpr uint32_t kLipAllowAngleOverride = 0xF3B6F95E; // LipAllowAngle_Override
constexpr uint32_t kLipHorizontalAngle = 0xE2A85FBB;  // LipPlayerHorizontalAngle
constexpr uint32_t kLipRampVertAngle = 0x61079462;    // LipRampVertAngle
constexpr uint32_t kLipTrick = 0x1647CF96;            // LipTrick (a script)
constexpr uint32_t kGroundSnapUp = 0xE4D79235;        // Physics_Ground_Snap_Up
constexpr uint32_t kBadLedgeDropDown = 0x9CD7ED5C;    // Rail_Bad_Ledge_Drop_Down_Dist
constexpr uint32_t kBadLedgeSide = 0x31669752;        // Rail_Bad_Ledge_Side_Dist
constexpr uint32_t kRailTolerance = 0xD0688D4E;       // Rail_Tolerance
constexpr uint32_t kPointRail = 0xB8048F1D;           // PointRail (event)
constexpr uint32_t kPointRailSpin = 0xDA82D6D6;       // PointRailSpin (event)
constexpr uint32_t kDisallowGrind = 0x96DCA7DC;       // Physics_Wallplant_Disallow_Grind_Duration
constexpr uint32_t kCornerLeaveAngle = 0x76C1DA15;    // Rail_Corner_Leave_Angle
constexpr uint32_t kRailManagerType = 0x1CDFB2A3;     // RailManager (a component)
constexpr uint32_t kParkEditorLevel = 0xD443A2BC;     // (the park editor's level)

// The trigger events (const int[3]).
constexpr uint32_t kGrindEvents = 0x0064BC28;     // getting onto a rail
constexpr uint32_t kKickEvents = 0x0064BBA4;      // kicked off a point rail
constexpr uint32_t kSpinEvents = 0x0064BC04;      // a point rail spin
constexpr uint32_t kLipEvents = 0x0064BBC8;       // a lip trick

constexpr float kDegrees = 0.0174532924f; // (degrees to radians, 0x00645464)
constexpr float kNearPoint = 36.0f;       // (6 squared: how far from the point a hit may be)

// The acid drop's hold velocity (debugging: "GRINDING FROM ACID DROP").
Mth::Vector& AcidHold() { return GameVariable<Mth::Vector>(0x007CCDBC); }

const Mth::Vector kUpAxis = {0.0f, 1.0f, 0.0f, 1.0f};

void SetState(void* component, int state) { static_cast<CorePhysicsComponent*>(component)->SetPhysicsState(state); }
void* Component(void* component, uint32_t offset) { return At<void*>(component, offset); }

// The rail's terrain (the node being ground) told to the flags (0x005196A0) and the sound component.
void SetRailTerrain(void* component)
{
    uint8_t terrain = At<Obj::CRailNode*>(component, kRailNode)->m_terrain;
    reinterpret_cast<Obj::CSkaterStateComponent*>(FlagsOf(component))->SetTerrain(terrain);
    At<uint8_t>(Component(component, kSoundComponent), 0x1C) = terrain;
}

// The landing sound (0x00518CF0): by speed (a fraction of the max max speed) and flag +0xE1.
void PlayLandSound(void* component)
{
    void* object = ObjectOf(component);
    float speed = static_cast<float>(LengthOf(VectorAt(object, kVelocity)));
    float fraction = speed / static_cast<Obj::CSkater*>(object)->GetScaledPhysics(kMaxMaxSpeed);
    static_cast<Obj::CSkaterSoundComponent*>(Component(component, kSoundComponent))
        ->PlayLand(fraction, Flag(component, 0xE1));
}

// The rail node's TriggerScript run for `events` (with what the skater rides on, if anything still there).
void TriggerRailNode(void* component, uint32_t events)
{
    const uint8_t* rideObject = nullptr;
    void* ride = Component(component, kRide);
    if (auto* tracker = At<Obj::CRideTracker*>(ride, 0x18)) {
        if (tracker->m_object.mp_object)
            rideObject = reinterpret_cast<const uint8_t*>(tracker->m_object.mp_object);
        else
            static_cast<Obj::CMovableContactComponent*>(ride)->StopRiding();
    }
    static_cast<Obj::CTriggerComponent*>(Component(component, kTriggerComponent))
        ->TriggerNode(reinterpret_cast<const int*>(events), At<uint32_t>(component, kRailScriptNode), nullptr,
                      rideObject);
}

// A feeler from the skater to the point: false when it hits something more than 6 from the point. The distance
// squared as the original: from the differences unrounded, z first (`fromVector`: the point less the hit as a
// vector - rounded - x first).
bool ClearToPoint(void* component, const Mth::Vector* point, bool fromVector)
{
    VectorAt(component, kFeelerStart) = VectorAt(ObjectOf(component), kPosition);
    VectorAt(component, kFeelerEnd) = *point;
    if (!Feel(component, 0x10, 0))
        return true;
    const Mth::Vector& hit = VectorAt(component, kHitPoint);
    double distance;
    if (fromVector) {
        Mth::Vector d = *point - hit;
        distance = (static_cast<double>(d.x) * d.x + static_cast<double>(d.y) * d.y) + static_cast<double>(d.z) * d.z;
    } else {
        double dx = static_cast<double>(point->x) - hit.x;
        double dy = static_cast<double>(point->y) - hit.y;
        double dz = static_cast<double>(point->z) - hit.z;
        distance = (dz * dz + dy * dy) + dx * dx;
    }
    return !(distance > kNearPoint);
}

// What every way onto a rail does first: the skater at the point, off vert, not wall riding (as it ends: 0x004FE860
// with the up straight and the display matrix reset - `always` for a point rail spin), into the rail state.
void OntoRail(void* component, const Mth::Vector* point, bool always)
{
    VectorAt(ObjectOf(component), kPosition) = *point;
    SetFlag(component, 0x30, 0);
    SetFlag(component, 0x38, 0);
    At<uint8_t>(component, 0x2D0) = 0;
    if (always || StateOf(component) == kWallRide) {
        static_cast<CorePhysicsComponent*>(component)->TurnToNormal(kUpAxis);
        MatrixAt(component, kDisplayMatrix) = MatrixAt(ObjectOf(component), kMatrix);
    }
    SetState(component, kRail);
}

// Barely moving: the velocity's x and z from the facing (a point rail: within 0.01; a rail: exactly 0). Not from an
// object: a fast skater's velocity turned level (its speed kept).
void PrepareVelocity(void* component, bool exactlyStill, bool fromObject)
{
    void* object = ObjectOf(component);
    Mth::Vector& v = VectorAt(object, kVelocity);
    bool still = exactlyStill ? v.x == 0.0f && v.z == 0.0f : std::fabs(v.x) < 0.01f && std::fabs(v.z) < 0.01f;
    if (still) {
        v.x = VectorAt(object, kFacing).x;
        VectorAt(ObjectOf(component), kVelocity).z = VectorAt(ObjectOf(component), kFacing).z;
    }
    if (!fromObject) {
        Mth::Vector& w = VectorAt(ObjectOf(component), kVelocity);
        if (static_cast<double>(w.z) * w.z + static_cast<double>(w.x) * w.x > 100.0f)
            w.RotateToPlane(kUpAxis);
    }
}

// Facing away from the velocity (the skater pointing against it).
bool MovingAgainstFacing(void* object)
{
    const Mth::Vector& v = VectorAt(object, kVelocity);
    const Mth::Vector& f = VectorAt(object, kFacing);
    double dot = (static_cast<double>(v.z) * f.z + static_cast<double>(v.y) * f.y) + static_cast<double>(f.x) * v.x;
    return dot < 0.0;
}

float JumpAdjustSpeed() { return Obj::CWalkComponent::Param(kJumpAdjustSpeed); }

// The flags a new rail clears (the time of getting on kept at +0x234).
void ClearRailFlags(void* component)
{
    At<int>(component, 0x234) = Now();
    SetFlag(component, 0x78, 0);
    SetFlag(component, 0xA8, 0);
    SetFlag(component, 0x80, 0);
    SetFlag(component, 0xB0, 0);
}

// (0x0050A770, a node on its own) Kicked off a point rail: up into the air along the move (or the facing, when
// from an object and too slow against it), at the kick angle, with the point rail boost.
void KickOffPointRail(CorePhysicsComponent* c, const Mth::Vector* point, bool fromObject)
{
    if (!ClearToPoint(c, point, false))
        return;
    PrepareVelocity(c, false, fromObject);
    Mth::Vector along = VectorAt(ObjectOf(c), kVelocity);
    along.y = 0.0f;
    along.Normalize();
    double angle = static_cast<double>(Script::GetFloat(kKickUpwardAngle)) * kDegrees;
    double cosine = std::cos(angle);
    Mth::Vector kick;
    kick.w = 1.0f;
    kick.x = static_cast<float>(along.x * cosine);
    kick.y = static_cast<float>(std::sin(angle));
    kick.z = static_cast<float>(along.z * cosine);

    Script::CStruct* script = SkateScript::GetNode(At<Obj::CRailNode*>(c, kRailNode)->m_name, true);
    script->GetChecksum(kName, &At<uint32_t>(c, kRailScriptNode), 1);
    static_cast<Obj::CTrickComponent*>(Component(c, kSkaterComponent))->PassToTrickObjects(At<uint32_t>(c, kRailScriptNode));
    uint32_t triggerScript = 0;
    if (script->GetChecksum(kTriggerScript, &triggerScript, 0))
        TriggerRailNode(c, kGrindEvents);
    OntoRail(c, point, false);
    SetRailTerrain(c);
    if (!At<uint8_t>(c, 0x2ED))
        PlayLandSound(c);

    Mth::Vector& v = VectorAt(ObjectOf(c), kVelocity);
    float keptY = v.y;
    v.ProjectOnto(along);
    if (fromObject && MovingAgainstFacing(ObjectOf(c))) {
        float adjust = static_cast<float>(JumpAdjustSpeed() * 1.1f);
        const Mth::Vector& w = VectorAt(ObjectOf(c), kVelocity);
        double speed = (static_cast<double>(w.x) * w.x + static_cast<double>(w.y) * w.y) + static_cast<double>(w.z) * w.z;
        if (static_cast<double>(adjust) * adjust > speed) {
            VectorAt(ObjectOf(c), kVelocity) = {0.0f, 0.0f, 0.0f, 1.0f};
            along = VectorAt(ObjectOf(c), kFacing);
            along.y = 0.0f;
            along.Normalize();
        }
    }
    VectorAt(ObjectOf(c), kVelocity).y = keptY;
    float boost = Script::GetFloat(kPointRailSpeedBoost);
    Mth::Vector& u = VectorAt(ObjectOf(c), kVelocity);
    float y = along.y * boost, z = along.z * boost, w = along.w * boost;
    u.x = static_cast<float>(static_cast<double>(along.x) * boost + u.x);
    u.y += y;
    u.z += z;
    u.w += w;
    ClearRailFlags(c);
    At<int>(c, 0x23C) = static_cast<int>(Script::GetFloat(kRerailTime));
    SetFlag(c, 0x50, 0);
    VectorAt(ObjectOf(c), kVelocity).SetDirection(kick);
    if (triggerScript)
        TriggerRailNode(c, kKickEvents);
    SetState(c, kAir);
    Mth::Vector& position = VectorAt(ObjectOf(c), kPosition);
    position.y = position.y + 1.0f;
    static_cast<Obj::CObject*>(ObjectOf(c))->SelfEvent(kPointRail, nullptr);
}

// (0x0050A770, a node on its own, a spin button held) A point rail spin (a Natas spin): stopped on the point.
void SpinOnPointRail(CorePhysicsComponent* c, const Mth::Vector* point)
{
    At<uint8_t>(c, 0x2EC) = 1;
    At<uint8_t>(c, 0x1500) = InputOf(c)[0x100] != 0;
    if (!ClearToPoint(c, point, true))
        return;
    OntoRail(c, point, true);
    static_cast<CorePhysicsComponent*>(c)->SetTerrain(At<Obj::CRailNode*>(c, kRailNode)->m_terrain);
    if (!At<uint8_t>(c, 0x2ED))
        PlayLandSound(c);
    Mth::Vector& v = VectorAt(ObjectOf(c), kVelocity);
    v.x = 0.0f;
    VectorAt(ObjectOf(c), kVelocity).y = 0.0f;
    VectorAt(ObjectOf(c), kVelocity).z = 0.0f;
    ClearRailFlags(c);
    static_cast<Obj::CObject*>(ObjectOf(c))->SelfEvent(kPointRailSpin, nullptr);
    c->TriggerRailScript(reinterpret_cast<const int*>(kSpinEvents));
    At<int>(c, 0x1504) = Now();
}

// (0x0050A770) A lip trick on the rail at the top of a vert ramp: stopped there, upright along the ramp's top, the
// LipTrick script.
void StartLipTrick(CorePhysicsComponent* c, const Mth::Vector* point)
{
    VectorAt(ObjectOf(c), kVelocity) = {0.0f, 0.0f, 0.0f, 1.0f};
    SetFlag(c, 0x30, 0);
    SetFlag(c, 0x38, 0);
    SetFlag(c, 0x40, 0);
    SetFlag(c, 0x48, 0);
    At<uint8_t>(c, 0x2D0) = 0;
    static_cast<Obj::CSkaterBalanceTrickComponent*>(Component(c, 0x1524))->Stop();
    VectorAt(c, 0x258) = VectorAt(ObjectOf(c), kPosition); // (where it started)
    SetState(c, kLip);
    VectorAt(ObjectOf(c), kPosition) = *point;
    // Up: the ramp's normal, level; facing straight up; right across.
    const Mth::Vector& ground = VectorAt(c, kGroundNormal);
    Mth::Vector up = {ground.x, 0.0f, ground.z, ground.w};
    up.Normalize();
    void* object = ObjectOf(c);
    VectorAt(object, kUp) = up;
    VectorAt(ObjectOf(c), kFacing) = kUpAxis;
    Mth::Vector& right = VectorAt(ObjectOf(c), kRight);
    const Mth::Vector& u = VectorAt(ObjectOf(c), kUp);
    right.y = 0.0f;
    right.w = 0.0f;
    right.x = -u.z;
    right.z = u.x;
    MatrixAt(c, kDisplayMatrix) = MatrixAt(ObjectOf(c), kMatrix);
    auto* gap = static_cast<Obj::CSkaterGapComponent*>(static_cast<Obj::CCompositeObject*>(ObjectOf(c))->GetComponent(kSkaterGap));
    gap->CheckGaps(0, 0x100000);
    c->TriggerRailScript(reinterpret_cast<const int*>(kLipEvents));
    At<uint8_t>(Component(c, kSkaterComponent), 0xA24) = 0;
    auto* skater = static_cast<Obj::CObject*>(ObjectOf(c));
    if (!skater->m_script) {
        void* memory = Script::CScript::operator new(0xF4);
        static_cast<Obj::CObject*>(ObjectOf(c))->m_script =
            memory ? static_cast<Script::CScript*>(memory)->Construct() : nullptr;
    }
    static_cast<Obj::CObject*>(ObjectOf(c))->m_script->SetScript(kLipTrick, nullptr, static_cast<Obj::CObject*>(ObjectOf(c)));
    static_cast<Obj::CObject*>(ObjectOf(c))->m_script->Update();
    At<uint8_t>(FlagsOf(c), 0xE0) = 1; // (without its time)
    SetRailTerrain(c);
}

// (0x0050A770) A grind along the rail from `node` to the next: the way the skater goes along it.
void GrindAlongRail(CorePhysicsComponent* c, const Mth::Vector* point, bool fromObject)
{
    // (not original: THAW 0x005E0EAD - the velocity as the grind start began, for the bike's stall)
    bool bike = ThawBmx::Active(c);
    Mth::Vector startVelocity = VectorAt(ObjectOf(c), kVelocity);
    SetRailTerrain(c);
    auto* node = At<Obj::CRailNode*>(c, kRailNode);
    Obj::CRailNode* next = node->m_next;
    auto* manager = At<Obj::CRailManager*>(c, kRailManager);
    Mth::Vector from, to;
    const Mth::Vector* a = manager->GetWorldPos(&from, node);
    const Mth::Vector* b = At<Obj::CRailManager*>(c, kRailManager)->GetWorldPos(&to, next);
    Mth::Vector direction = {b->x - a->x, b->y - a->y, b->z - a->z, b->w - a->w};
    direction.Normalize();
    if (!ClearToPoint(c, point, false))
        return;
    if (Flag(c, 0xA8)) {
        // (From an acid drop: its hold velocity added.)
        Dbg::Printf(reinterpret_cast<const char*>(0x006466B0), reinterpret_cast<const char*>(0x0064BCE0), 9116,
                    reinterpret_cast<const char*>(0x0064BD90)); // "M:%s:%i: %s" ... "GRINDING FROM ACID DROP"
        const Mth::Vector& hold = AcidHold();
        Dbg::Printf(reinterpret_cast<const char*>(0x0064BD6C), reinterpret_cast<const char*>(0x0064BCE0), 9117,
                    static_cast<double>(hold.x), static_cast<double>(hold.y), static_cast<double>(hold.z));
        Mth::Vector& v = VectorAt(ObjectOf(c), kVelocity);
        v.x = AcidHold().x + v.x;
        v.y = AcidHold().y + v.y;
        v.z = AcidHold().z + v.z;
        v.w = AcidHold().w + v.w;
    }
    PrepareVelocity(c, true, fromObject);

    // Which way: along the velocity; straight across it, a coin toss. At the rail's end that way: nothing.
    const Mth::Vector& v = VectorAt(ObjectOf(c), kVelocity);
    double along = (static_cast<double>(direction.z) * v.z + static_cast<double>(direction.y) * v.y) +
                   static_cast<double>(direction.x) * v.x;
    bool forward = along == 0.0 ? Mth::Rnd(2) != 0 : !(along < 0.0);
    float sign;
    Mth::Vector end;
    if (forward) {
        sign = 1.0f;
        if (!next->m_next && *At<Obj::CRailManager*>(c, kRailManager)->GetWorldPos(&end, next) == *point)
            return;
    } else {
        sign = -1.0f;
        if (!node->m_prev && *At<Obj::CRailManager*>(c, kRailManager)->GetWorldPos(&end, node) == *point)
            return;
    }
    c->TriggerRailScript(reinterpret_cast<const int*>(kGrindEvents));
    OntoRail(c, point, false);
    if (Since(At<uint32_t>(Component(c, kTriggerState), 0x1C)) != 0 && !At<uint8_t>(c, 0x2ED))
        PlayLandSound(c);

    // The speed along the rail, the height kept; plus the rail boost. From an object, too slow against the facing:
    // from standing, the way the facing points along the rail.
    Mth::Vector& velocity = VectorAt(ObjectOf(c), kVelocity);
    float keptY = velocity.y;
    velocity.ProjectOnto(direction);
    void* object = ObjectOf(c);
    if (fromObject && MovingAgainstFacing(object)) {
        double adjust = static_cast<double>(JumpAdjustSpeed()) * 1.1f;
        const Mth::Vector& w = VectorAt(object, kVelocity);
        double speed = (static_cast<double>(w.z) * w.z + static_cast<double>(w.x) * w.x) + static_cast<double>(w.y) * w.y;
        if (adjust * adjust > speed) {
            VectorAt(ObjectOf(c), kVelocity) = {0.0f, 0.0f, 0.0f, 1.0f};
            const Mth::Vector& f = VectorAt(ObjectOf(c), kFacing);
            sign = Mth::Sign(static_cast<float>((static_cast<double>(direction.z) * f.z +
                                                 static_cast<double>(direction.y) * f.y) +
                                                static_cast<double>(direction.x) * f.x));
        }
    }
    VectorAt(ObjectOf(c), kVelocity).y = keptY;
    Mth::Vector way = {sign * direction.x, direction.y * sign, direction.z * sign, direction.w * sign};
    float boost = Script::GetFloat(kRailSpeedBoost);
    {
        Mth::Vector& u = VectorAt(ObjectOf(c), kVelocity);
        float y = way.y * boost, z = way.z * boost, w = way.w * boost;
        u.x = static_cast<float>(static_cast<double>(way.x) * boost + u.x);
        u.y = y + u.y;
        u.z = z + u.z;
        u.w = w + u.w;
    }
    At<uint8_t>(c, 0x34C) = 0;
    if (sign < 0.0f)
        direction = {-direction.x, -direction.y, -direction.z, -direction.w};

    // Which side of the rail the jump to it started (+0x338).
    const Mth::Vector& jumped = VectorAt(c, 0x338);
    double across = -(static_cast<double>(point->x) - jumped.x) * direction.z +
                    (static_cast<double>(point->z) - jumped.z) * direction.x;
    bool leftSide = across < 0.0;

    // A bad ledge: feelers down beside the rail, either side.
    Mth::Vector& start = VectorAt(c, kFeelerStart);
    Mth::Vector& stop = VectorAt(c, kFeelerEnd);
    start.y = PhysicsFloat(kGroundSnapUp, At<void*>(c, kObject));
    start.x = 0.0f, start.z = 0.0f, start.w = 0.0f;
    stop.y = -Script::GetFloat(kBadLedgeDropDown);
    stop.x = 0.0f, stop.z = 0.0f, stop.w = 0.0f;
    start.x = point->x + start.x;
    start.y = start.y + point->y;
    start.z = start.z + point->z;
    start.w = start.w + point->w;
    stop.x = point->x + stop.x;
    stop.y = stop.y + point->y;
    stop.z = stop.z + point->z;
    stop.w = stop.w + point->w;
    float sideDistance = Script::GetFloat(kBadLedgeSide);
    float zero = 0.0f * sideDistance;
    Mth::Vector side = {direction.z * sideDistance, zero, -direction.x * sideDistance, zero};
    start.x = side.x + start.x, start.y = side.y + start.y, start.z = side.z + start.z, start.w = side.w + start.w;
    stop.x = side.x + stop.x, stop.y = side.y + stop.y, stop.z = side.z + stop.z, stop.w = side.w + stop.w;
    bool leftHit = Feel(c, 0x10, 0);
    Mth::Vector twice = {side.x + side.x, side.y + side.y, side.z + side.z, side.w + side.w};
    start.x -= twice.x, start.y -= twice.y, start.z -= twice.z, start.w -= twice.w;
    stop.x -= twice.x, stop.y -= twice.y, stop.z -= twice.z, stop.w -= twice.w;
    bool rightHit = Feel(c, 0x10, 0);
    At<uint8_t>(c, 0x249) = (leftHit && !leftSide) || (rightHit && leftSide);
    At<uint8_t>(c, 0x248) = leftHit || rightHit;
    if (ThawBmx::Active(c)) // (not original: THAW 0x005E27F5 - on the bike, a bad ledge on that side: the other side)
        leftSide = ThawBmx::GrindSide(c, leftSide, At<uint8_t>(c, 0x249) != 0);

    // Grinding sideways (across the skater's right) or which way round.
    void* o = ObjectOf(c);
    const Mth::Vector& right = VectorAt(o, kRight);
    const Mth::Vector& facing = VectorAt(o, kFacing);
    float acrossRight = static_cast<float>((static_cast<double>(direction.z) * right.z +
                                            static_cast<double>(direction.y) * right.y) +
                                           static_cast<double>(direction.x) * right.x);
    float alongFacing = static_cast<float>((static_cast<double>(direction.z) * facing.z +
                                            static_cast<double>(direction.y) * facing.y) +
                                           static_cast<double>(direction.x) * facing.x);
    float acrossAmount = std::fabs(acrossRight);
    bool sideways = false;
    if (Script::GetFloat(kRailTolerance) > acrossAmount) {
        sideways = true;
        if (alongFacing < 0.0f)
            At<uint8_t>(c, 0x34C) = 1;
    } else {
        float turned = Flag(c, 0x28) ? -acrossRight : acrossRight;
        if (turned < 0.0f)
            At<uint8_t>(c, 0x34C) = 1;
    }
    void* skater = Component(c, kSkaterComponent);
    At<uint8_t>(skater, 0xA24) = 0;
    // The grind trick: the skater component's (0x00522E80), else the physics' pick by the d-pad (0x005004B0).
    if (!static_cast<Obj::CTrickComponent*>(skater)->DoExtraGrindTrick(leftSide, sideways, At<uint8_t>(c, 0x34C) != 0,
                                                                        Flag(c, 0x28) != 0)) {
        if (bike) // (not original: THAW 0x005E2935 - on the bike R1 held: the stall)
            ThawBmx::GrindStall(c, startVelocity, direction);
        const uint8_t* input = InputOf(c);
        int pad = Obj::DPadDirection(input[0x20], input[0x40], input[0x60], input[0x80]);
        c->StartGrindTrick(static_cast<uint32_t>(pad), leftSide, sideways, At<uint8_t>(c, 0x34C) != 0, Flag(c, 0x28) != 0);
    }
    At<int>(c, 0x234) = Now();
}

} // namespace

// 0x0050A770
void CorePhysicsComponent::StartGrind(const Mth::Vector* point, Obj::CRailNode* node, Obj::CRailManager* manager,
                                      bool noLipTrick, bool fromObject)
{
    const uint8_t* input = InputOf(this);
    auto* flipAndRotate = static_cast<Obj::CSkaterFlipAndRotateComponent*>(
        static_cast<Obj::CCompositeObject*>(ObjectOf(this))->GetComponent(kSkaterFlipAndRotate));
    flipAndRotate->DoAfterAnim();
    if (At<Obj::CRailNode*>(this, kRailNode) != node) {
        // (A new rail: its node's index to 0x005627B0 of +0x1520's +0x18.)
        uint32_t index = static_cast<uint32_t>(reinterpret_cast<uint8_t*>(node) -
                                               reinterpret_cast<uint8_t*>(At<Obj::CRailNode*>(manager, 0x40))) /
                         sizeof(Obj::CRailNode);
        static_cast<Mdl::Score*>(At<void*>(Component(this, 0x1520), 0x18))->UpdateRobotDetection(static_cast<int>(index));
    }
    At<Obj::CRailManager*>(this, kRailManager) = manager;
    At<Obj::CRailNode*>(this, kRailNode) = node;

    if (!node->m_prev && !node->m_next) {
        // A point rail: a spin when a spin button is held, long enough after the last one; else a kick.
        if (input[0x100] || input[0xA0]) {
            int elapsed = static_cast<int>(Since(At<uint32_t>(this, 0x1504)));
            if (!(static_cast<double>(Script::GetFloat(kNatasDelay)) * 1000.0f > static_cast<float>(elapsed))) {
                SpinOnPointRail(this, point);
                return;
            }
        }
        KickOffPointRail(this, point, fromObject);
        return;
    }

    // A lip trick: going up, upright (the up within the horizontal angle), facing up, on a ramp near vertical, the
    // skater's right near level (the allow angle - its override for nodes flagged 1).
    float allow = static_cast<float>(std::sin(static_cast<double>(Script::GetFloat(kLipAllowAngle)) * kDegrees));
    if (node->m_flags & 1)
        allow = static_cast<float>(std::sin(static_cast<double>(Script::GetFloat(kLipAllowAngleOverride)) * kDegrees));
    float horizontal = static_cast<float>(std::sin(static_cast<double>(Script::GetFloat(kLipHorizontalAngle)) * kDegrees));
    float rampVert = static_cast<float>(std::cos(static_cast<double>(Script::GetFloat(kLipRampVertAngle)) * kDegrees));
    bool lip = true;
    if (!At<uint8_t>(this, 0x375)) {
        void* object = ObjectOf(this);
        lip = VectorAt(object, kVelocity).y > 0.0f && std::fabs(VectorAt(object, kUp).y) < horizontal &&
              VectorAt(object, kFacing).y > 0.0f && std::fabs(VectorAt(this, kGroundNormal).y) < rampVert &&
              std::fabs(VectorAt(object, kRight).y) < allow;
    }
    // (not original: THAW 0x005E1B31 - no lip trick on the bike)
    if (lip && !noLipTrick && !At<uint8_t>(this, 0x2ED) && !ThawBmx::Active(this))
        StartLipTrick(this, point);
    else
        GrindAlongRail(this, point, fromObject);
}

namespace {

// 0x00500020: whether the rail `node` (of `manager`) may be ground.
bool CanGrind(void* component, Obj::CRailNode* node, Obj::CRailManager* manager)
{
    return static_cast<CorePhysicsComponent*>(component)->CanGrindRail(node, manager, false);
}

} // namespace

// 0x0050DAE0
void CorePhysicsComponent::TryGrind(bool always)
{
    if (StateOf(this) != kAir && !always)
        return;
    if (Flag(this, 0x70) || At<uint8_t>(this, 0x3A4) || !InputOf(this)[0x1A0])
        return;
    if (Since(At<uint32_t>(this, 0x4E0)) < static_cast<uint32_t>(Script::GetInteger(kDisallowGrind)))
        return;
    void* object = ObjectOf(this);
    Mth::Vector last = VectorAt(object, kOldPosition);
    Mth::Vector now = VectorAt(object, kPosition);
    // Soon after leaving a rail (within +0x23C of +0x234; not in the park editor), only a rail lining up.
    float minAlignment = 1.0f;
    if (At<uint32_t>(GameVariable<uint8_t*>(0x007CE5D8), 0x40) != kParkEditorLevel &&
        static_cast<uint32_t>(Tmr::GameClockTimeSince(At<int>(this, 0x234))) < At<uint32_t>(this, 0x23C))
        minAlignment = static_cast<float>(std::cos(static_cast<double>(Script::GetFloat(kCornerLeaveAngle)) * kDegrees));
    auto* manager = At<Obj::CRailManager*>(GameVariable<uint8_t*>(0x007CE478), 0x24);
    Mth::Vector point;
    Obj::CRailNode* node;
    if (manager->FindGrindRail(&last, &now, &point, &node, nullptr, minAlignment, 0)) {
        void* o = ObjectOf(this);
        TrackingPrint(2, &VectorAt(o, kOldPosition), &VectorAt(o, kPosition));
        StopRiding(this);
        if (CanGrind(this, node, manager))
            StartGrind(&point, node, manager, false, false);
        return;
    }
    // The rails on moving objects: the move in the object's space.
    for (auto* rails = Obj::CompositeObjectManager()->GetFirstComponentByType(kRailManagerType); rails;
         rails = At<Obj::CBaseComponent*>(rails, 0x14)) {
        auto* objectRails = reinterpret_cast<Obj::CRailManager*(__thiscall*)(void*)>(
            (*reinterpret_cast<void***>(rails))[13])(rails);
        Mth::Matrix world;
        static_cast<Obj::CRailManagerComponent*>(rails)->UpdateRailMatrix(&world);
        Mth::Matrix inverse;
        inverse = world;
        inverse.Invert();
        last.w = 1.0f;
        now.w = 1.0f;
        Mth::Vector localLast, localNow;
        inverse.Transform(localLast, last);
        inverse.Transform(localNow, now);
        if (objectRails->FindGrindRail(&localLast, &localNow, &point, &node, nullptr, minAlignment, 0)) {
            point.w = 1.0f;
            Mth::Vector inWorld;
            point = world.Transform(inWorld, point);
            if (!CanGrind(this, node, objectRails))
                return;
            StartGrind(&point, node, objectRails, false, false);
            At<Walk::MovableContact*>(this, kRide)->StartRiding(At<Obj::CCompositeObject*>(rails, 0xC));
            At<Obj::CMovableContactComponent*>(this, kRide)->UpdateContactPoint();
            VectorAt(ObjectOf(this), kVelocity) -= VectorAt(Tracker(this)->m_object.mp_object, kVelocity);
            return;
        }
    }
}

} // namespace Skater

REPLACE(0x0050DAE0, &Skater::CorePhysicsComponent::TryGrind);
REPLACE(0x0050A770, &Skater::CorePhysicsComponent::StartGrind);
