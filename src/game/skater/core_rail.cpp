// The skater's core physics component: grinding - the rail state's update and moving on from a rail's end.

#include <cmath>
#include <cstdint>

#include "game/mth/maths.h"
#include "game/gfx/d3dx.h"
#include "game/mth/mat4.h"
#include "game/mth/vec4.h"
#include "game/obj/node_components.h"
#include "game/obj/entity.h"
#include "game/obj/rail_manager.h"
#include "game/obj/ride_tracker.h"
#include "game/obj/walk_hang.h"
#include "game/script/nodes.h"
#include "game/script/param_struct.h"
#include "game/skate/career.h"
#include "game/skater/core_fields.h"
#include "game/skater/core_physics.h"
#include "game/skater/manual_balance.h"
#include "game/skater/rider.h"
#include "game/skater/thaw_bmx.h"
#include "game/skater/trick_component.h"
#include "game/skate/point_tally.h"
#include "game/ed/park_editor.h"
#include "game/skate/skate_module.h"
#include "game/net/gamenet_manager.h"
#include "game/obj/trigger_events.h"

namespace Skater {

using namespace CoreFields;

namespace {

constexpr uint32_t kCornerLeaveAngle = 0x76C1DA15; // Rail_Corner_Leave_Angle
constexpr uint32_t kRerailTime = 0x2B4645AD;       // Rail_minimum_rerail_time
constexpr uint32_t kJumpRerailTime = 0x0BF35053;   // Rail_jump_rerail_time
constexpr uint32_t kRailGravity = 0xD1F46992;      // Physics_Rail_Gravity
constexpr uint32_t kRailManagerType = 0x1CDFB2A3;  // RailManager (a component)
constexpr uint32_t kGrind = 0x255ED86F;            // Grind (the balance trick's kind)
constexpr uint32_t kSlide = 0x8D10119D;            // Slide
constexpr uint32_t kPerfectRail = 0xCD09E062;      // CHEAT_PERFECT_RAIL
constexpr uint32_t kOffRail = 0xAFAA46BA;          // OffRail (event)
constexpr uint32_t kName = 0xA1DC81F9;
constexpr uint32_t kParkEditorLevel = 0xD443A2BC;

constexpr float kDegrees = 0.0174532924f;
constexpr float kRaise = 6.0f;      // 0x00646284: the transfer line's start raised
constexpr float kRaiseEnd = 6.1f;   // 0x0064BDA8: its end
constexpr float kFeelerAhead = 6.0f; // (the feeler past the move: 6 along it)
constexpr float kTurnFacing = 0.3f; // (how far the display facing turns to the rail each frame)

bool InParkEditor() { return At<uint32_t>(reinterpret_cast<uint8_t*>(ParkEditing::g_parkEditor), 0x40) == kParkEditorLevel; }
void ChangeState(void* component, int state) { static_cast<CorePhysicsComponent*>(component)->SetPhysicsState(state); }

Maths::Vec4 WorldPos(void* component, const GameObjects::CGrindPoint* node)
{
    Maths::Vec4 out;
    return *At<GameObjects::CGrindRailHub*>(component, kRailManager)->GetAbsolutePos(&out, node);
}

// -1 for a dot product below 0, else 1 (NaN: 1).
float SignOf(double dot) { return dot < 0.0 ? -1.0f : 1.0f; }
double Dot3(const Maths::Vec4& a, const Maths::Vec4& v)
{
    return (static_cast<double>(a.z) * v.z + static_cast<double>(a.y) * v.y) + static_cast<double>(a.x) * v.x;
}

// A node rails go on from (flag 4 - its >> 2 & 1).
bool Continues(const GameObjects::CGrindPoint* node) { return (node->m_flagBits >> 2) & 1; }

// The corner from segment `a` (to - from) into segment `b` (level): turning less than Rail_Corner_Leave_Angle.
bool GentleCorner(Maths::Vec4 a, Maths::Vec4 b)
{
    a.y = 0.0f;
    b.y = 0.0f;
    a.MakeUnit();
    b.MakeUnit();
    float turn = static_cast<float>((static_cast<double>(b.z) * a.z + static_cast<double>(b.y) * a.y) +
                                    static_cast<double>(b.x) * a.x);
    return !(std::cos(static_cast<double>(Scripts::FetchFloat(kCornerLeaveAngle)) * kDegrees) > turn);
}

} // namespace

// 0x0050C200
void CorePhysicsComponent::TransferOrLeaveRail(const Maths::Vec4* end)
{
    // The line looked along: from `end` (in the park editor: back along the velocity) to the skater, raised.
    void* object = ObjectOf(this);
    Maths::Vec4 start;
    if (!InParkEditor()) {
        start = *end;
    } else {
        const Maths::Vec4& p = VectorAt(object, kPosition);
        const Maths::Vec4& v = VectorAt(object, kVelocity);
        float t = At<float>(this, kFrameLength);
        float bx = t * v.x, by = t * v.y, bz = t * v.z;
        double bw = static_cast<double>(t) * v.w;
        start.x = p.x - bx;
        start.y = p.y - by;
        start.z = p.z - bz;
        start.w = static_cast<float>(p.w - bw);
    }
    start.y = start.y + kRaise;
    Maths::Vec4 stop = VectorAt(ObjectOf(this), kPosition);
    stop.y = stop.y + kRaiseEnd;
    float angle = Scripts::FetchFloat(kCornerLeaveAngle);
    auto* node = At<GameObjects::CGrindPoint*>(this, kRailNode);
    int side = node->GetWallSide(&VectorAt(ObjectOf(this), kVelocity));
    float alignment = static_cast<float>(std::cos(static_cast<double>(angle) * kDegrees));
    Maths::Vec4 point;
    GameObjects::CGrindPoint* found = nullptr;
    bool onto = At<GameObjects::CGrindRailHub*>(this, kRailManager)
                    ->FindGrindRail(&start, &stop, &point, &found, At<GameObjects::CGrindPoint*>(this, kRailNode), alignment, side) &&
                found && found != At<GameObjects::CGrindPoint*>(this, kRailNode) && found->m_succ;
    if (onto && !InParkEditor()) {
        // (Not this rail's next or last node.)
        auto* current = At<GameObjects::CGrindPoint*>(this, kRailNode);
        onto = current->m_succ != found && current->m_lastValue != found;
    }
    if (onto) {
        GameObjects::CGrindPoint* next = found->m_succ;
        const Maths::Vec4& p = VectorAt(ObjectOf(this), kPosition);
        Maths::Vec4 foundPos = WorldPos(this, found);
        Maths::Vec4 toFound = foundPos - p;
        Maths::Vec4 nextPos = WorldPos(this, next);
        Maths::Vec4 toNext = nextPos - VectorAt(ObjectOf(this), kPosition);
        double toward = (static_cast<double>(toNext.x) * toFound.x + static_cast<double>(toNext.z) * toFound.z) +
                        static_cast<double>(toNext.y) * toFound.y;
        float towardRounded = static_cast<float>(toward);
        if (!(toward < 0.0)) {
            // Not between its two nodes: never in the park editor; elsewhere only when the move reaches as far as
            // the segment is long - then onto its middle.
            if (!InParkEditor()) {
                Maths::Vec4 a = WorldPos(this, found);
                Maths::Vec4 b = WorldPos(this, next);
                Maths::Vec4 segment = b - a;
                Maths::Vec4 move = stop - start;
                float segmentLength = static_cast<float>(LengthOf(segment));
                onto = !(LengthOf(move) < segmentLength);
                if (onto && towardRounded >= 0.0f) {
                    Maths::Vec4 c = WorldPos(this, found);
                    Maths::Vec4 d = WorldPos(this, next);
                    point = (d + c) / 2.0f;
                }
            } else {
                onto = false;
            }
        }
    }
    if (!onto) {
        TriggerRailScript(g_grindOffEvents);
        ChangeState(this, kAir);
        Maths::Vec4& p = VectorAt(ObjectOf(this), kPosition);
        p.y = p.y + 1.0f;
        static_cast<GameObjects::CEntity*>(ObjectOf(this))->PostOwnEvent(kOffRail, nullptr);
        return;
    }
    // Onto it: the velocity along it (the same way), the skater at the point.
    GameObjects::CGrindPoint* next = found->m_succ;
    Maths::Vec4 a = WorldPos(this, found);
    Maths::Vec4 b = WorldPos(this, next);
    Maths::Vec4 direction = b - a;
    direction.MakeUnit();
    Maths::Vec4& v = VectorAt(ObjectOf(this), kVelocity);
    if (v.x == 0.0f && v.z == 0.0f) {
        v.x = VectorAt(ObjectOf(this), kFacing).x;
        VectorAt(ObjectOf(this), kVelocity).z = VectorAt(ObjectOf(this), kFacing).z;
    }
    Maths::Vec4& velocity = VectorAt(ObjectOf(this), kVelocity);
    float sign = Maths::Sign(static_cast<float>(Dot3(direction, velocity)));
    velocity.ChangeHeading(direction);
    VectorAt(ObjectOf(this), kVelocity) *= sign;
    At<GameObjects::CGrindPoint*>(this, kRailNode) = found;
    VectorAt(ObjectOf(this), kPosition) = point;
    TriggerRailScript(g_grindEvents);
    SetSurfaceKind(At<GameObjects::CGrindPoint*>(this, kRailNode)->m_surfaceType);
}

// 0x0050DDF0
void CorePhysicsComponent::DoRailPhysics()
{
    SetFlag(this, 0x78, 0);
    SetFlag(this, 0xA8, 0);
    SetFlag(this, 0x80, 0);
    SetFlag(this, 0xB0, 0);
    At<uint8_t>(this, 0x2E4) = 0;
    RideMovingRail();

    void* object = ObjectOf(this);
    At<int>(this, 0x23C) = static_cast<int>(Scripts::FetchFloat(kRerailTime));
    SetFlag(this, 0x50, 0);
    // Off the rail (0x004FFB10): the TriggerScripts, the jump's rerail time, up and out.
    if (JumpReleased()) {
        TriggerRailScript(g_jumpTriggerEvents);
        TriggerRailScript(g_grindOffEvents);
        At<int>(this, 0x23C) = static_cast<int>(Scripts::FetchFloat(kJumpRerailTime));
        SetFlag(this, 0xD0, 1);
        SetFlag(this, 0xD8, 1);
        Maths::Vec4& p = VectorAt(ObjectOf(this), kPosition);
        p.y = p.y + 1.0f;
        FinishRail();
        if (ThawBmx::LiveNow(this))
            ThawBmx::RailStallSlip(this); // (not original: THAW 0x005E7074 - the bike's stall)
        return;
    }
    UpdateGrindBalance();

    auto* node = At<GameObjects::CGrindPoint*>(this, kRailNode);
    GameObjects::CGrindPoint* const startNode = node;
    GameObjects::CGrindPoint* next = node->m_succ;
    GameObjects::CGrindPoint* goOnTo = nullptr;
    if (!next) {
        // The rail's last node.
        NatasSpin();
        return;
    }

    // The segment's direction (x scaled by the unrounded inverse length, the rest by it rounded - as the original).
    Maths::Vec4 from = WorldPos(this, node);
    Maths::Vec4 to = WorldPos(this, next);
    double dx = static_cast<double>(to.x) - from.x, dy = static_cast<double>(to.y) - from.y;
    double dz = static_cast<double>(to.z) - from.z, dw = static_cast<double>(to.w) - from.w;
    float segmentLength = static_cast<float>(std::sqrt((dx * dx + dy * dy) + dz * dz));
    double inverse = 1.0 / segmentLength;
    float inverseRounded = static_cast<float>(inverse);
    Maths::Vec4 along = {static_cast<float>(inverse * dx), static_cast<float>(inverseRounded * dy),
                         static_cast<float>(inverseRounded * dz), static_cast<float>(inverseRounded * dw)};

    // The rail's gravity along it; turned back by it: the other way round (+0x34C).
    float before = SignOf(Dot3(along, VectorAt(ObjectOf(this), kVelocity)));
    Maths::Vec4 gravity = {0.0f, PhysicsFloat(kRailGravity, object), 0.0f, 1.0f};
    gravity.ProjectOnto(along);
    {
        float t = At<float>(this, kFrameLength);
        Maths::Vec4& v = VectorAt(ObjectOf(this), kVelocity);
        float gy = t * gravity.y, gz = t * gravity.z, gw = t * gravity.w;
        v.x = static_cast<float>(static_cast<double>(gravity.x) * t + v.x);
        v.y = gy + v.y;
        v.z = gz + v.z;
        v.w = gw + v.w;
    }
    float after = SignOf(Dot3(along, VectorAt(ObjectOf(this), kVelocity)));
    if (before != after)
        At<uint8_t>(this, 0x34C) = !At<uint8_t>(this, 0x34C);

    // The segment to go on to that way (its node flagged 4, the corner gentle enough) - or none.
    bool noNextSegment = false;
    if (after < 0.0f) {
        GameObjects::CGrindPoint* prev = node->m_lastValue;
        if (prev && Continues(prev)) {
            Maths::Vec4 a = WorldPos(this, node);
            Maths::Vec4 b = WorldPos(this, next);
            Maths::Vec4 ahead = b - a;
            Maths::Vec4 c = WorldPos(this, prev);
            Maths::Vec4 d = WorldPos(this, node);
            Maths::Vec4 behind = d - c;
            if (GentleCorner(ahead, behind))
                goOnTo = node->m_lastValue;
            else
                noNextSegment = true;
        } else {
            noNextSegment = true;
        }
    } else {
        GameObjects::CGrindPoint* beyond = next->m_succ;
        if (beyond && Continues(beyond)) {
            Maths::Vec4 a = WorldPos(this, next);
            Maths::Vec4 b = WorldPos(this, node);
            Maths::Vec4 back = b - a;
            Maths::Vec4 c = WorldPos(this, next->m_succ);
            Maths::Vec4 d = WorldPos(this, next);
            Maths::Vec4 backFurther = d - c;
            if (GentleCorner(back, backFurther))
                goOnTo = next;
            else
                noNextSegment = true;
        } else {
            noNextSegment = true;
        }
    }

    // Past the segment's end (measured from the node it started at): onto the next segment, else on from the rail.
    float overshoot = 0.0f;
    {
        GameObjects::CGrindPoint* fromNode = after < 0.0f ? next : node;
        const Maths::Vec4& p = VectorAt(ObjectOf(this), kPosition);
        Maths::Vec4 q = WorldPos(this, fromNode);
        double ex = static_cast<double>(p.x) - q.x, ey = static_cast<double>(p.y) - q.y, ez = static_cast<double>(p.z) - q.z;
        double distance = std::sqrt((ez * ez + ey * ey) + ex * ex);
        if (distance > static_cast<double>(segmentLength) + 0.1f) {
            overshoot = static_cast<float>(distance - segmentLength);
            GameObjects::CGrindPoint* corner = after < 0.0f ? node : next;               // the node passed
            GameObjects::CGrindPoint* onward = after < 0.0f ? node->m_lastValue : next->m_succ; // the next segment's far node
            GameObjects::CGrindPoint* flagged = after < 0.0f ? node->m_lastValue : next;      // the node whose flag says go on
            bool clear = false;
            if (onward && Continues(flagged)) {
                Maths::Vec4 far = WorldPos(this, onward);
                Maths::Vec4 near = WorldPos(this, corner);
                clear = GameObjects::RailClear(near, far);
            }
            if (clear && !noNextSegment) {
                VectorAt(ObjectOf(this), kPosition) = WorldPos(this, corner);
                At<GameObjects::CGrindPoint*>(this, kRailNode) = after < 0.0f ? node->m_lastValue : next;
                SetSurfaceKind(At<GameObjects::CGrindPoint*>(this, kRailNode)->m_surfaceType);
                TriggerRailScript(g_grindEvents);
            } else {
                Maths::Vec4 end = WorldPos(this, corner);
                TransferOrLeaveRail(&end);
            }
        }
    }
    if (StateOf(this) != kRail) {
        FinishRail();
        if (ThawBmx::LiveNow(this))
            ThawBmx::RailStallSlip(this); // (not original, as above)
        return;
    }

    GrindAlong(startNode, goOnTo, noNextSegment, overshoot);
    FinishRail();
    if (ThawBmx::LiveNow(this))
        ThawBmx::RailStallSlip(this); // (not original, as above)
}

// (Part of 0x0050DDF0) A moving rail: its matrix asked for (0x0042BA10 - not used), then carried along with it.
void CorePhysicsComponent::RideMovingRail()
{
    if (GameObjects::CRideTracker* tracker = Tracker(this)) {
        if (!tracker->m_entity.m_ownerObject) {
            StopRiding(this);
        } else {
            void* rails = static_cast<GameObjects::CGameEntity*>(Tracker(this)->m_entity.m_ownerObject)->FetchModule(kRailManagerType);
            Maths::Mat4 matrix;
            static_cast<GameObjects::CGrindPathLinkModule*>(rails)->UpdateRailMatrix(&matrix);
            tracker = Tracker(this);
            if (tracker) {
                if (!tracker->m_entity.m_ownerObject || !tracker->TickFrame(&VectorAt(ObjectOf(this), kPosition))) {
                    StopRiding(this);
                } else {
                    VectorAt(ObjectOf(this), kPosition) += Tracker(this)->m_carried;
                    if (Tracker(this)->m_turned) {
                        auto multiply = D3dx::MatrixMultiply;
                        Maths::Mat4& m = MatrixAt(ObjectOf(this), kMatrix);
                        multiply(&m, &m, &Tracker(this)->m_turn);
                        multiply(&MatrixAt(this, kDisplayMatrix), &MatrixAt(this, kDisplayMatrix),
                                 &Tracker(this)->m_turn);
                    }
                }
            }
        }
    }
}

// (Part of 0x0050DDF0) A grind or a slide: its balance (+0x7C of the balance trick component), the Perfect Rail cheat.
void CorePhysicsComponent::UpdateGrindBalance()
{
    uint32_t kind = At<uint32_t>(At<void*>(this, kBalanceTrick), 0x18);
    if (kind == kGrind || kind == kSlide) {
        reinterpret_cast<GameObjects::CBalance*>(At<uint8_t*>(this, kBalanceTrick) + 0x7C)->TickFrame();
        auto career = [] { return At<GameObjects::CRiderStoryProgress*>(reinterpret_cast<uint8_t*>(GameModules::g_skateModule), 0x20); };
        if (career()->IsCodeOn(kPerfectRail)) {
            career()->SetGlobalFlag(Scripts::FetchIntValue(kPerfectRail));
            At<uint32_t>(At<void*>(this, kBalanceTrick), 0xAC) = 0;
            At<uint32_t>(At<void*>(this, kBalanceTrick), 0xB0) = 0;
            reinterpret_cast<uint8_t&>(OnlinePlay::g_cheatFlag) = 1;
        }
    }
}

// (Part of 0x0050DDF0) Still on the rail: the velocity along the segment, the facing turned to it, the move (and
// what went past the last segment's end), then a feeler ahead along the move.
void CorePhysicsComponent::GrindAlong(GameObjects::CGrindPoint* startNode, GameObjects::CGrindPoint* goOnTo, bool noNextSegment,
                                      float overshoot)
{
    auto* node = At<GameObjects::CGrindPoint*>(this, kRailNode);
    GameObjects::CGrindPoint* next = node->m_succ;
    Maths::Vec4 from = WorldPos(this, node);
    Maths::Vec4 to = WorldPos(this, next);
    Maths::Vec4 direction = {to.x - from.x, to.y - from.y, to.z - from.z, to.w - from.w};
    direction.MakeUnit();
    float sign = SignOf(Dot3(direction, VectorAt(ObjectOf(this), kVelocity)));
    At<int>(this, 0x234) = Now();
    VectorAt(ObjectOf(this), kVelocity).ChangeHeading(direction);
    Maths::Vec4& v = VectorAt(ObjectOf(this), kVelocity);
    v.x = sign * v.x;
    v.y = sign * v.y;
    v.z = sign * v.z;
    v.w = sign * v.w;

    // The facing: along the rail (the other way round with +0x34C); the display facing (+0x38) turned 0.3 of the way
    // to it, the display and the object's matrices built around them (level rights).
    float way = At<uint8_t>(this, 0x34C) ? -sign : sign;
    Maths::Vec4 facing = {direction.x * way, direction.y * way, direction.z * way, way * direction.w};
    Maths::Vec4& display = VectorAt(this, 0x38);
    Maths::Vec4 turned;
    Walk::Lerp(&turned, &display, &facing, kTurnFacing);
    display = turned;
    display.MakeUnit();
    Maths::Vec4& right = VectorAt(this, 0x18);
    right.x = At<float>(this, 0x40);
    right.z = -display.x;
    right.y = 0.0f;
    right.w = 0.0f;
    right.MakeUnit();
    VectorAt(this, 0x28) = Maths::CrossVec(display, right);
    VectorAt(this, 0x28).MakeUnit();
    void* object = ObjectOf(this);
    VectorAt(object, kFacing) = facing;
    VectorAt(ObjectOf(this), kFacing).MakeUnit();
    object = ObjectOf(this);
    Maths::Vec4& objectRight = VectorAt(object, kRight);
    float fz = VectorAt(object, kFacing).z;
    objectRight.z = -VectorAt(object, kFacing).x;
    objectRight.x = fz;
    objectRight.y = 0.0f;
    objectRight.w = 0.0f;
    VectorAt(ObjectOf(this), kRight).MakeUnit();
    object = ObjectOf(this);
    VectorAt(object, kUp) = Maths::CrossVec(VectorAt(object, kFacing), VectorAt(object, kRight));
    VectorAt(ObjectOf(this), kUp).MakeUnit();

    // What went past the last segment's end is only carried on when this segment is longer.
    Maths::Vec4 a = WorldPos(this, node);
    Maths::Vec4 b = WorldPos(this, next);
    double sx = static_cast<double>(b.x) - a.x, sy = static_cast<double>(b.y) - a.y;
    double sz = static_cast<double>(b.z) - a.z;
    double length = std::sqrt((sx * sx + sz * sz) + sy * sy);
    if (length <= overshoot)
        overshoot = 0.0f;

    // The move: the velocity over the frame, plus the overshoot along the facing (x, y of both unrounded).
    Maths::Vec4& p = VectorAt(ObjectOf(this), kPosition);
    Maths::Vec4 old = p;
    {
        float t = At<float>(this, kFrameLength);
        const Maths::Vec4& u = VectorAt(ObjectOf(this), kVelocity);
        float my = t * u.y, mz = t * u.z, mw = t * u.w;
        p.x = static_cast<float>(static_cast<double>(t) * u.x + p.x);
        p.y = my + p.y;
        p.z = mz + p.z;
        p.w = mw + p.w;
        Maths::Vec4& q = VectorAt(ObjectOf(this), kPosition);
        float oz = facing.z * overshoot, ow = facing.w * overshoot;
        q.x = static_cast<float>(static_cast<double>(facing.x) * overshoot + q.x);
        q.y = static_cast<float>(static_cast<double>(facing.y) * overshoot + q.y);
        q.z = oz + q.z;
        q.w = ow + q.w;
        q.w = q.w + 1.0f;
    }
    const Maths::Vec4& now = VectorAt(ObjectOf(this), kPosition);
    Maths::Vec4 move = {now.x - old.x, now.y - old.y, now.z - old.z, now.w - old.w};
    Maths::Vec4 moveDirection = move;
    moveDirection.MakeUnit();

    // Going on along the rail: only (not in the park editor) when the next segment climbs as this one does.
    if (!noNextSegment) {
        if (InParkEditor())
            return;
        Maths::Vec4 c = WorldPos(this, startNode);
        Maths::Vec4 d = WorldPos(this, startNode->m_succ);
        Maths::Vec4 thisWay = d - c;
        Maths::Vec4 e = WorldPos(this, goOnTo);
        Maths::Vec4 f = WorldPos(this, goOnTo->m_succ);
        Maths::Vec4 thatWay = f - e;
        thisWay.MakeUnit();
        thatWay.MakeUnit();
        if (!(std::fabs(static_cast<double>(thisWay.y) - thatWay.y) < 0.01f))
            return;
    }

    // The feeler: from the last position to 6 past the new one along the move, raised by the up.
    Maths::Vec4& start = VectorAt(this, kFeelerStart);
    Maths::Vec4& end = VectorAt(this, kFeelerEnd);
    double aheadX = static_cast<double>(moveDirection.x) * kFeelerAhead;
    double aheadY = static_cast<double>(moveDirection.y) * kFeelerAhead;
    float aheadZ = moveDirection.z * kFeelerAhead;
    float aheadW = moveDirection.w * kFeelerAhead;
    start = old;
    end = VectorAt(ObjectOf(this), kPosition);
    float extendX = static_cast<float>(aheadX + move.x);
    double extendY = aheadY + move.y;
    double extendZ = static_cast<double>(aheadZ) + move.z;
    float extendW = aheadW + move.w;
    end.x = extendX + end.x;
    end.y = static_cast<float>(extendY + end.y);
    end.z = static_cast<float>(extendZ + end.z);
    end.w = extendW + end.w;
    const Maths::Vec4& up = VectorAt(ObjectOf(this), kUp);
    start.x = up.x + start.x, start.y = up.y + start.y, start.z = up.z + start.z, start.w = up.w + start.w;
    const Maths::Vec4& up2 = VectorAt(ObjectOf(this), kUp);
    end.x = end.x + up2.x, end.y = end.y + up2.y, end.z = end.z + up2.z, end.w = end.w + up2.w;
    if (!Feel(this, 0x10, 0))
        return;
    // Something ahead: off the rail (not for faces flagged 0x10 at +0xF9, outside the park editor).
    if (!InParkEditor() && (At<uint8_t>(this, 0xF9) & 0x10))
        return;
    TriggerRailScript(g_grindOffEvents);
    void* o = ObjectOf(this);
    VectorAt(o, kPosition) = VectorAt(o, kOldPosition);
    Maths::Vec4& position = VectorAt(ObjectOf(this), kPosition);
    position.y = position.y + 1.0f;
    VectorAt(ObjectOf(this), kVelocity).FlattenOntoPlane(VectorAt(this, kHitNormal));
    ChangeState(this, kAir);
    static_cast<GameObjects::CEntity*>(ObjectOf(this))->PostOwnEvent(kOffRail, nullptr);
}

// (Part of 0x0050DDF0) Every way out of the update (but the rail's last node): the ground normal, the score's rail
// time (0x00564270, +0x3A8 scaled), the node's Name.
void CorePhysicsComponent::FinishRail()
{
    VectorAt(this, kGroundNormal) = VectorAt(ObjectOf(this), kUp);
    void* score = At<void*>(At<void*>(this, 0x1520), 0x18);
    int time = static_cast<int>(static_cast<double>(At<int>(this, 0x3A8)) * At<float>(score, 0x154C));
    static_cast<GameModules::PointTally*>(score)->TweakTrick(time);
    if (At<GameObjects::CGrindRailHub*>(this, kRailManager)->m_nodeArray) {
        Scripts::CStruct* script = LevelNodes::FetchVertex(At<GameObjects::CGrindPoint*>(this, kRailNode)->m_label, true);
        // (0x00476EC0: ReadHashValue with a bool for its last argument - the wrapper passes it on as 1)
        script->ReadHashValue(kName, &At<uint32_t>(this, kRailScriptNode), 1);
        static_cast<GameObjects::CMoveExecutor*>(At<void*>(this, kSkaterComponent))
            ->PassToTrickObjects(At<uint32_t>(this, kRailScriptNode));
    }
}

// Not original: THAW's stall state (THAW 0x005E2E30; game/skater/thaw_bmx.h - its bike's, R1 held as a grind
// starts): the rail state's update around a bike held still on the rail, facing along it. Update runs it in that state
// (ThawBmx::GrindStall / RailStallSlip set it) while the feature is on.
void CorePhysicsComponent::DoBikeStallPhysics()
{
    // As the rail state's: the flags (THAW clears a fifth of its own), the moving rail, the rerail time.
    SetFlag(this, 0x78, 0);
    SetFlag(this, 0xA8, 0);
    SetFlag(this, 0x80, 0);
    SetFlag(this, 0xB0, 0);
    At<uint8_t>(this, 0x2E4) = 0;
    RideMovingRail();
    At<int>(this, 0x23C) = static_cast<int>(Scripts::FetchFloat(kRerailTime));
    SetFlag(this, 0x50, 0);
    if (JumpReleased()) {
        // Jumped (0x005E3053): the rail's TriggerScripts (THAW's with its stall event, which THUG2's haven't), the
        // jump's rerail time, up.
        TriggerRailScript(g_jumpTriggerEvents);
        TriggerRailScript(g_grindOffEvents);
        At<int>(this, 0x23C) = static_cast<int>(Scripts::FetchFloat(kJumpRerailTime));
        SetFlag(this, 0xD0, 1);
        SetFlag(this, 0xD8, 1);
        Maths::Vec4& p = VectorAt(ObjectOf(this), kPosition);
        p.y = p.y + 1.0f;
    } else {
        UpdateGrindBalance();
        // (THAW draws the segment here with rail_highlights on - a debug view of its own)
        auto* node = At<GameObjects::CGrindPoint*>(this, kRailNode);
        GameObjects::CGrindPoint* next = node->m_succ;
        if (!next)
            return;
        VectorAt(ObjectOf(this), kVelocity) = {0.0f, 0.0f, 0.0f, 1.0f};
        if (StateOf(this) == kBikeStall) {
            // 0x005E34AE: the facing along the segment the way it points, the display facing turned 0.3 of the way
            // to it and the matrices built round them as a grind's (GrindAlong); no move.
            Maths::Vec4 from = WorldPos(this, node);
            Maths::Vec4 to = WorldPos(this, next);
            Maths::Vec4 direction = {to.x - from.x, to.y - from.y, to.z - from.z, to.w - from.w};
            direction.MakeUnit();
            At<int>(this, 0x234) = Now();
            Maths::Vec4 facing = ThawBmx::Bike::StallFacing(VectorAt(ObjectOf(this), kFacing), direction);
            Maths::Vec4& display = VectorAt(this, 0x38);
            Maths::Vec4 turned;
            Walk::Lerp(&turned, &display, &facing, kTurnFacing);
            display = turned;
            display.MakeUnit();
            Maths::Vec4& right = VectorAt(this, 0x18);
            right.x = At<float>(this, 0x40);
            right.z = -display.x;
            right.y = 0.0f;
            right.w = 0.0f;
            right.MakeUnit();
            VectorAt(this, 0x28) = Maths::CrossVec(display, right);
            VectorAt(this, 0x28).MakeUnit();
            void* object = ObjectOf(this);
            VectorAt(object, kFacing) = facing;
            VectorAt(object, kFacing).MakeUnit();
            Maths::Vec4& objectRight = VectorAt(object, kRight);
            float fz = VectorAt(object, kFacing).z;
            objectRight.z = -VectorAt(object, kFacing).x;
            objectRight.x = fz;
            objectRight.y = 0.0f;
            objectRight.w = 0.0f;
            objectRight.MakeUnit();
            VectorAt(object, kUp) = Maths::CrossVec(VectorAt(object, kFacing), objectRight);
            VectorAt(object, kUp).MakeUnit();
            VectorAt(object, kVelocity) = {0.0f, 0.0f, 0.0f, 1.0f};
        }
    }
    // 0x005E37FE: on a rail too steep, grinding again; the ground normal the up, the score's rail time, the node's name.
    ThawBmx::StallSteepRail(this);
    FinishRail();
}

} // namespace Skater
