// The walk component's hanging, ladder and rail-climbing code (see walk_hang.h). Floating point follows the
// original's x87 code: values it keeps on the x87 stack are doubles here, values it stores are floats.

#include "game/obj/walk_hang.h"

#include <bit>
#include <cmath>
#include <cstdint>

#include "game/core/dbg.h"
#include "game/core/mem.h"
#include "game/mth/shape_tests.h"
#include "game/mth/mat4.h"
#include "game/mth/vec4.h"
#include "game/nx/coll_cache.h"
#include "game/nx/probe_ray.h"
#include "game/obj/compositemgr.h"
#include "game/obj/movable_contact.h"
#include "game/obj/node_components.h"
#include "game/obj/entity.h"
#include "game/obj/rail_manager.h"
#include "game/obj/ride_tracker.h"
#include "game/obj/smtptr.h"
#include "game/obj/trigger_component.h"
#include "game/script/param_struct.h"
#include "game/skater/core_physics.h"
#include "game/skater/walk_camera.h"
#include "game/skater/walk_physics.h"
#include "game/sys/game_clock.h"
#include "game/obj/control_pad.h"
#include "game/skate/skate_module.h"
#include "game/core/code_address.h"
#include "game/obj/trigger_events.h"
#include "game/obj/wait_and_die.h"

namespace {

template <typename T>
T& At(void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}
template <typename T>
const T& At(const void* object, uint32_t offset)
{
    return *reinterpret_cast<const T*>(static_cast<const uint8_t*>(object) + offset);
}

// --- the walk component's fields (offsets) ---
constexpr uint32_t kObject = 0x0C;          // the skater's object (GameObjects::CGameEntity*)
constexpr uint32_t kPos = 0x24;             // the position (a vector)
constexpr uint32_t kVelocity = 0x34;        // the velocity (a vector)
constexpr uint32_t kVerticalSpeed = 0x44;   // (the vertical speed)
constexpr uint32_t kFacing = 0x48;          // the facing (a vector)
constexpr uint32_t kOldPos = 0x68;          // last frame's position
constexpr uint32_t kUnknown74 = 0x74;       // (a float set to 1 by a drop to hang)
constexpr uint32_t kAnim = 0x78;            // the animation to play (checksum)
constexpr uint32_t kHeightOffset = 0x9C;    // the origin's height offset
constexpr uint32_t kHangOffset = 0x71C;     // the hang's offset from the critical point (a vector)
constexpr uint32_t kWalkCamera = 0x1760;    // the walk camera (GameObjects::CFootView)
constexpr uint32_t kRailStart = 0x1764;     // the rail held: its node...
constexpr uint32_t kRailEnd = 0x1768;       // ... and the next
constexpr uint32_t kRailManager = 0x176C;   // whose rail manager
constexpr uint32_t kModel = 0x1778;         // (+0x208: a node with flag 8 at +0x44 and a fraction at +0x20)
constexpr uint32_t kTrigger = 0x177C;       // the trigger component
constexpr uint32_t kContact = 0x1784;       // the movable contact component (+0x18: its ride tracker)
constexpr uint32_t kPhysics = 0x178C;       // the skater core physics component
constexpr uint32_t kPullUpFeeler = 0x17B4;  // the feeler a pull-up's ledge was found with (Engine3D::CProbeRay)
constexpr uint32_t kPullUpHit = 0x1864;     // (whether that feeler hit something)
constexpr uint32_t kHitNormal = 0x1794;     // its hit's normal
constexpr uint32_t kRunBlend = 0x1868;      // run_speed (0) to combo_run_speed (1)
constexpr uint32_t kExitFacing = 0x1878;    // the facing kept on leaving a hang (a vector)
constexpr uint32_t kStartPos = 0x18E4;      // an animated move's start ...
constexpr uint32_t kEndPos = 0x1904;        // ... and end (vectors)
constexpr uint32_t kAlong = 0x18CC;         // how far along the rail (a fraction)
constexpr uint32_t kRailUp = 0x18D0;        // the hang point's offset from the rail: up ...
constexpr uint32_t kRailOut = 0x18D4;       // ... and out
constexpr uint32_t kStartHeightOffset = 0x1914; // the height offset at an animated move's start ...
constexpr uint32_t kEndHeightOffset = 0x1918;   // ... and end
constexpr uint32_t kAnimWaiting = 0x1944;   // an animation is playing: the callback waits
constexpr uint32_t kAnimDone = 0x1948;      // the callback (a member function pointer: the code, then 3 words)

// The events the rail scripts and triggers get (const int[3]).

constexpr uint32_t kWalkParameters = 0x6775E538;       // "walk_parameters"
constexpr uint32_t kComboRunSpeed = 0x7213EF07;        // "combo_run_speed"
constexpr uint32_t kRunSpeed = 0xCC461B87;             // "run_speed"
constexpr uint32_t kHangVertOriginOffset = 0xA8C13E74; // "hang_vert_origin_offset"
constexpr uint32_t kHangCriticalPointVertOffset = 0xDE86132E; // "hang_critical_point_vert_offset"
constexpr uint32_t kMaxCasScaling = 0x7201AD48;        // "max_cas_scaling"
constexpr uint32_t kHangCriticalPointHorizOffset = 0xC3263E6E; // "hang_critical_point_horiz_offset"
constexpr uint32_t kHangObstructionFeelerSide = 0xAFBF7687; // "hang_obstruction_feeler_side"
constexpr uint32_t kHangObstructionFeelerUp = 0x8F35F29F;   // "hang_obstruction_feeler_up"
constexpr uint32_t kSkaterExitHang = 0xBEEDB40D;       // "SkaterExitHang"
constexpr uint32_t kPushFeelerLength = 0xA20C43B7;     // "push_feeler_length"
constexpr uint32_t kFeelerHeight = 0x6DA7F696;         // "feeler_height"
constexpr uint32_t kPullUpObstructionHeight = 0x9B2818CF; // "pull_up_obstruction_height"
constexpr uint32_t kPullUpOffsetForward = 0x21DFBE77;  // "pull_up_offset_forward"
constexpr uint32_t kDropToHangSpeedFactor = 0x01D1E6AF; // "drop_to_hang_speed_factor"
constexpr uint32_t kDropToClimbMaxSnap = 0xA8276303;   // "Drop_To_Climb_Max_Snap" (a global float)
constexpr uint32_t kClimbMaxSnap = 0x30CE7F2C;         // "Climb_Max_Snap" (a global float)
constexpr uint32_t kDropToHangRotateFactor = 0xFDC8AEC0; // "drop_to_hang_rotate_factor"
constexpr uint32_t kHangMoveCollisionSideLength = 0x3344A6D0; // "hang_move_collision_side_length"
constexpr uint32_t kHangMoveCollisionUp = 0x84FF931C;   // "hang_move_collision_up"
constexpr uint32_t kHangMoveCollisionBack = 0xCBEB3D89; // "hang_move_collision_back"
constexpr uint32_t kHangMoveCollisionSideHeight = 0xC774DD6D; // "hang_move_collision_side_height"
constexpr uint32_t kRailManagerComponent = 0x1CDFB2A3; // "railmanager" (the component type)
constexpr uint32_t kAnimPullUpFromHang = 0x9EF9F8F1;   // "PullUpFromHang"
constexpr uint32_t kAnimDropToHang = 0xEB71D98E;       // "DropToHang"
constexpr uint32_t kAnimHang = 0x4194ECCA;             // "Hang"

// More fields: the facing aimed at in an animated move, the height there, the hang point's offset along the rail.
constexpr uint32_t kTargetFacing = 0x18B4;
constexpr uint32_t kTargetHeight = 0x18C8;
constexpr uint32_t kHangAnim = 0x18D8; // the hang's animation (SWING / WALL)

constexpr uint32_t kTaggingControl = 0x380D909A;       // "TaggingControl"

// The hang's own update.
constexpr uint32_t kState = 0x18;            // 0 walking, 1 in the air, 2 hanging, 3 on a ladder, 4 an animated move
constexpr uint32_t kFrameLength = 0x80;      // the frame's length (seconds)
constexpr uint32_t kControlMagnitude = 0x94; // how far the control is pushed
constexpr uint32_t kControlHeld = 0x98;      // (a byte: the control is pushed)
constexpr uint32_t kAnimSpeed = 0x704;       // the move's speed (for the animation's)
constexpr uint32_t kInput = 0x1770;          // the input component (+0x20: the control - see Walk::SkaterTurn)
constexpr uint32_t kHangMoveSpeed = 0x18DC;  // the shimmy's speed, eased towards the control's
constexpr uint32_t kFlag1958 = 0x1958;       // (a byte cleared each hanging frame)
constexpr uint32_t kHangHorizOriginOffset = 0x262A9467; // "hang_horiz_origin_offset"
constexpr uint32_t kHopObstructionFeelerUp = 0xA85EC070; // "hop_obstruction_feeler_up"
constexpr uint32_t kHangMaxRailAscent = 0x98724EBC;   // "hang_max_rail_ascent"
constexpr uint32_t kHangInitAnimFeelerHeight = 0xDBB3D53E; // "hang_init_anim_feeler_height"
constexpr uint32_t kHangInitAnimFeelerLength = 0x2F83AE83; // "hang_init_anim_feeler_length"
constexpr uint32_t kPullUpOffsetUp = 0x52FA7CA6;      // "pull_up_offset_up"
constexpr uint32_t kHangVertControlTolerance = 0x1B877928; // "hang_vert_control_tolerance"
constexpr uint32_t kHangMoveSpeedParam = 0xD77EE881;  // "hang_move_speed"
constexpr uint32_t kHangMoveLerpRate = 0xC506A97C;    // "hang_move_lerp_rate"
constexpr uint32_t kHangMoveCutoff = 0x228B5376;      // "hang_move_cutoff"
constexpr uint32_t kAnimAir = 0x439F4704;             // "Air"
constexpr uint32_t kAnimHangMoveLeft = 0x2D9815C3;    // "HangMoveLeft"
constexpr uint32_t kAnimHangMoveRight = 0x279B1F0B;   // "HangMoveRight"
constexpr uint32_t kAnimSwing = 0x1EE948A5;           // "SWING"
constexpr uint32_t kAnimWall = 0xEC0A1009;            // "WALL"

// Ladders.
constexpr uint32_t kStateTime = 0x1C;                  // when the state last changed
constexpr uint32_t kLadderBottomOffsetForward = 0xCAF2831B; // "ladder_bottom_offset_forward"
constexpr uint32_t kLadderTopOffsetForward = 0xB620B580;    // "ladder_top_offset_forward"
constexpr uint32_t kMaxOntoLadderAngle = 0x456F216D;   // "max_onto_ladder_angle"
constexpr uint32_t kLadderBottomOffsetUp = 0x5296E3FD; // "ladder_bottom_offset_up"
constexpr uint32_t kLadderTopOffsetUp = 0x76BF49E0;    // "ladder_top_offset_up"
constexpr uint32_t kLadderClimbOffset = 0x35B3BBDA;    // "ladder_climb_offset"
constexpr uint32_t kMaxVertSnapDistance = 0xC4C164E8;  // "max_vert_snap_distance"
constexpr uint32_t kButtonHorizSnapDistance = 0x0EECD255; // "button_horiz_snap_distance"
constexpr uint32_t kMaxHorizSnapDistance = 0xA2735DD9; // "max_horiz_snap_distance"
constexpr uint32_t kLadderMoveSpeed = 0x0AB2DB54;      // "ladder_move_speed"
constexpr uint32_t kLadderControlTolerance = 0x7EE1B95B; // "ladder_control_tolerance"
constexpr uint32_t kAnimLadder = 0xC84243DA;           // "Ladder"
constexpr uint32_t kAnimLadderMoveUp = 0xAF5ABC82;     // "LadderMoveUp"
constexpr uint32_t kAnimLadderMoveDown = 0xFEC9DDED;   // "LadderMoveDown"
constexpr uint32_t kAnimLadderOntoBottom = 0xE2524194; // "LadderOntoBottom"
constexpr uint32_t kAnimLadderOntoTop = 0xD63ADCAD;    // "LadderOntoTop"
constexpr uint32_t kAnimLadderOffBottom = 0x4DB1BBB1;  // "LadderOffBottom"
constexpr uint32_t kAnimLadderOffTop = 0x12521BFB;     // "LadderOffTop"

GameObjects::CGameEntity* ObjectOf(void* walk) { return At<GameObjects::CGameEntity*>(walk, kObject); }
GameObjects::CGrindRailHub*& RailsOf(void* walk) { return At<GameObjects::CGrindRailHub*>(walk, kRailManager); }
Skater::CorePhysicsComponent* PhysicsOf(void* walk) { return At<Skater::CorePhysicsComponent*>(walk, kPhysics); }
Maths::Vec4& VectorAt(void* walk, uint32_t offset) { return At<Maths::Vec4>(walk, offset); }

// What FindRail finds (its 0x34-byte record, filled by CheckRail): the hang point, the direction to face (out from
// the wall), how far along, out and up from the rail, the animation, and the rail's node.
struct FoundRail {
    Maths::Vec4 point;     // +0x00
    Maths::Vec4 facing;    // +0x10
    float along;           // +0x20
    float out;             // +0x24
    float up;              // +0x28
    uint32_t anim;         // +0x2C the hang's animation (SWING, or WALL)
    GameObjects::CGrindPoint* node;  // +0x30
};
static_assert(sizeof(FoundRail) == sizeof(GameObjects::Record34), "the rail manager's record");

// A feeler copied (its assignment 0x0043A930).
void AssignFeeler(Engine3D::CProbeRay* to, const Engine3D::CProbeRay* from)
{
    to->Assign(*from);
}
// The movable contact: no ride any more (0x00427C70).
void StopRiding(void* walk) { At<GameObjects::CMovingSurfaceModule*>(walk, kContact)->StopRiding(); }
// A rail manager component's object's matrix (0x0042BA10) and its rail manager (vtable +0x34).
void ObjectMatrixOf(GameObjects::CObjectModule* component, Maths::Mat4* matrix)
{
    static_cast<GameObjects::CGrindPathLinkModule*>(component)->UpdateRailMatrix(matrix);
}
GameObjects::CGrindRailHub* RailManagerOf(GameObjects::CObjectModule* component)
{
    using Pull = GameObjects::CGrindRailHub*(__thiscall*)(GameObjects::CObjectModule*);
    return (*reinterpret_cast<Pull**>(component))[0x34 / 4](component);
}
// The skate module's rail manager (the level's).
GameObjects::CGrindRailHub* LevelRails() { return At<GameObjects::CGrindRailHub*>(reinterpret_cast<uint8_t*>(GameModules::g_skateModule), 0x24); }

// Onto a ladder from the level's rails: the ride let go, stopped.
void GrabbedLadder(GameObjects::CFootMovement* walk)
{
    RailsOf(walk) = LevelRails();
    StopRiding(walk);
    At<float>(walk, kVerticalSpeed) = 0.0f;
    VectorAt(walk, kVelocity) = {0.0f, 0.0f, 0.0f, 1.0f};
}

// FindRail for a walk component between `start` and `end`: the level's rails, else each moving rail manager's (its
// object's space: riding that object if one is found). `direction` / the margin's global float are read again for
// each. The rail manager used is left at +0x176C.
bool FindWalkRail(GameObjects::CFootMovement* walk, const Maths::Vec4* start, const Maths::Vec4* end,
                  const Maths::Vec4* (*direction)(GameObjects::CFootMovement*, Maths::Vec4*), uint32_t margin, FoundRail* found)
{
    Maths::Vec4 buffer;
    RailsOf(walk) = LevelRails();
    const Maths::Vec4* towards = direction(walk, &buffer);
    float snap = Scripts::FetchFloat(margin);
    auto* record = reinterpret_cast<GameObjects::Record34*>(found);
    if (RailsOf(walk)->FindRail(start, end, towards, walk, record, snap)) {
        StopRiding(walk);
        return true;
    }
    for (GameObjects::CObjectModule* component = GameObjects::CompositeObjectManager()->FindFirstModuleOfKind(kRailManagerComponent);
         component; component = At<GameObjects::CObjectModule*>(component, 0x14)) {
        Maths::Mat4 toLocal;
        ObjectMatrixOf(component, &toLocal);
        toLocal.Upend();
        Maths::Vec4 localStart, localEnd;
        toLocal.Xform(localStart, *start);
        toLocal.Xform(localEnd, *end);
        RailsOf(walk) = RailManagerOf(component);
        towards = direction(walk, &buffer);
        snap = Scripts::FetchFloat(margin);
        if (RailsOf(walk)->FindRail(&localStart, &localEnd, towards, walk, record, snap)) {
            reinterpret_cast<Walk::MovableContact*>(At<void*>(walk, kContact))
                ->StartRiding(At<GameObjects::CGameEntity*>(component, 0x0C));
            return true;
        }
    }
    return false;
}
const Maths::Vec4* FacingAway(GameObjects::CFootMovement* walk, Maths::Vec4* buffer)
{
    const Maths::Vec4& facing = At<Maths::Vec4>(walk, 0x48);
    *buffer = {-facing.x, -facing.y, -facing.z, -facing.w};
    return buffer;
}
const Maths::Vec4* Facing(GameObjects::CFootMovement* walk, Maths::Vec4*) { return &At<Maths::Vec4>(walk, 0x48); }

// The animation-end callback (+0x1948): a member function pointer as the compiler lays it out - the code address
// and three words of adjustment.
template <typename Method>
uint32_t MemberCodeAddress(Method method)
{
    static_assert(sizeof(Method) == sizeof(uint32_t), "a plain member function pointer: just the code address");
    return std::bit_cast<uint32_t>(method);
}
void SetAnimDone(void* walk, uint32_t code)
{
    uint32_t* callback = &At<uint32_t>(walk, kAnimDone);
    callback[0] = code;
    callback[1] = 0;
    callback[2] = 0;
    callback[3] = 0;
}

// The skater core's rail node (its +0x22C), if there is one.
void TellPhysicsNode(void* walk)
{
    if (GameObjects::CGrindPoint* node = At<GameObjects::CGrindPoint*>(walk, kRailStart))
        At<GameObjects::CGrindPoint*>(PhysicsOf(walk), 0x22C) = node;
}

// The skater core's rail node and rail manager (its +0x22C / +0x230), for its rail scripts.
void TellPhysicsRail(void* walk)
{
    TellPhysicsNode(walk);
    if (GameObjects::CGrindRailHub* rails = RailsOf(walk))
        At<GameObjects::CGrindRailHub*>(PhysicsOf(walk), 0x230) = rails;
}
void TriggerRailScript(void* walk, const int* events)
{
    PhysicsOf(walk)->TriggerRailScript(events);
}
// The hang offset (+0x71C) for the facing: hang_critical_point_horiz_offset along it, the critical point's height up.
void SetHangOffset(GameObjects::CFootMovement* walk)
{
    float horiz = GameObjects::CFootMovement::Param(kHangCriticalPointHorizOffset);
    const Maths::Vec4& facing = VectorAt(walk, kFacing);
    Maths::Vec4& offset = VectorAt(walk, kHangOffset);
    offset.z = horiz * facing.z;
    offset.w = horiz * facing.w;
    offset.x = horiz * facing.x;
    offset.y = horiz * facing.y;
    offset.y = static_cast<float>(walk->CriticalPointUp());
}

// The trigger component told a feeler hit something (for `events`), and the movable contact the feeler's hit.
void TriggerFeeler(void* walk, const int* events, Engine3D::CProbeRay* feeler)
{
    At<GameObjects::CActivationZoneModule*>(walk, kTrigger)->CheckFeeler(events, feeler);
}
void ContactFeeler(void* walk, Engine3D::CProbeRay* feeler)
{
    At<GameObjects::CMovingSurfaceModule*>(walk, kContact)->StartRiding(feeler);
}

// The game's clock with its offset.
int Now() { return FrameClock::GameClockElapsed() + GameObjects::g_clockOffset; }

// The movable contact's ride let go (its object kept at +0x6C, the time at +0x70).
void LetGoOfRide(void* contact)
{
    auto* tracker = At<GameObjects::CRideTracker*>(contact, 0x18);
    if (!tracker)
        return;
    At<GameObjects::CGameEntity*>(contact, 0x6C) = tracker->m_entity.Pull(); // (the object last ridden)
    tracker->m_entity.Release();
    MemSys::Dealloc(tracker);
    At<GameObjects::CRideTracker*>(contact, 0x18) = nullptr;
    At<int>(contact, 0x70) = Now();
}

// The skater core's timed flag +0xA0 of +0x1528 set (with the time at +0xA4), unless it is.
void SetPhysicsFlagA0(void* walk)
{
    uint8_t* flags = At<uint8_t*>(PhysicsOf(walk), 0x1528);
    if (At<uint8_t>(flags, 0xA0) != 1) {
        At<uint8_t>(flags, 0xA0) = 1;
        At<int>(flags, 0xA4) = Now();
    }
}

// The x87's cosine (the original's FCOS), rounded to a double.
double X87Cos(double angle)
{
    double result;
    __asm {
        fld angle
        fcos
        fstp result
    }
    return result;
}
double X87Sin(double angle)
{
    double result;
    __asm {
        fld angle
        fsin
        fstp result
    }
    return result;
}

// What FindLadder / FindLadderOnLine find (their 0x24-byte record, filled by the ladder checkers): the point to climb
// from, the direction to face (the ladder's orientation's third row), how far along.
struct FoundLadder {
    Maths::Vec4 point;  // +0x00
    Maths::Vec4 facing; // +0x10
    float along;        // +0x20
};
static_assert(sizeof(FoundLadder) == sizeof(GameObjects::Record24), "the rail manager's record");

// A ladder's other end: the node's next, else its previous.
GameObjects::CGrindPoint* LadderOtherEnd(GameObjects::CGrindPoint* node) { return node->m_succ ? node->m_succ : node->m_lastValue; }

// 0x006468F8: 60 degrees (a double)
constexpr double kSixtyDegrees = 1.0471975511965976;

} // namespace

// --- small helpers ---

// 0x0043D990
Maths::Vec4* __cdecl Walk::Lerp(Maths::Vec4* out, const Maths::Vec4* a, const Maths::Vec4* b, float t)
{
    double dx = static_cast<double>(b->x) - a->x;
    double dy = static_cast<double>(b->y) - a->y;
    float dz = static_cast<float>(static_cast<double>(b->z) - a->z);
    float dw = static_cast<float>(static_cast<double>(b->w) - a->w);
    float sx = static_cast<float>(dx * t);
    float sz = static_cast<float>(static_cast<double>(dz) * t);
    float sw = static_cast<float>(static_cast<double>(dw) * t);
    out->w = static_cast<float>(static_cast<double>(sw) + a->w);
    out->x = static_cast<float>(static_cast<double>(sx) + a->x);
    out->y = static_cast<float>(dy * t + a->y);
    out->z = static_cast<float>(static_cast<double>(sz) + a->z);
    return out;
}

// 0x0043DA20
int Walk::SkaterTurn::TurnedRound() const
{
    const void* skater = this;
    if (At<int>(skater, 0x234) == 0 && At<float>(skater, 0x22C) != 0.0f &&
        3.1415927410125732 - std::fabs(static_cast<double>(At<float>(skater, 0x228))) < 0.78539818525314331)
        return 1;
    return 0;
}

// 0x0043DA70
float __cdecl GameObjects::CFootMovement::Param(uint32_t name)
{
    float value;
    Scripts::ReadSubStruct(kWalkParameters)->FetchFloat(name, &value, 1);
    return value;
}

// 0x0043DAA0
double GameObjects::CFootMovement::RunSpeed()
{
    float combo = Param(kComboRunSpeed);
    float run = Param(kRunSpeed);
    return (static_cast<double>(combo) - run) * At<float>(this, kRunBlend) + run;
}

// 0x0043DC00
double GameObjects::CFootMovement::HangHeightOffset()
{
    const uint8_t* node = At<uint8_t*>(At<void*>(this, kModel), 0x208);
    if (!(node[0x44] & 8))
        return 0.0;
    float fraction = At<float>(node, 0x20);
    float offset = Param(kHangVertOriginOffset);
    return (1.0 - fraction) * offset;
}

// 0x0043DC60
double GameObjects::CFootMovement::CriticalPointUp()
{
    float vert = Param(kHangCriticalPointVertOffset);
    float scaling = Param(kMaxCasScaling);
    float origin = Param(kHangVertOriginOffset);
    return vert - static_cast<double>(origin) * scaling;
}

// 0x0043DB00
void GameObjects::CFootMovement::EndPullUp()
{
    ChangePhase(0);
    ObjectOf(this)->PostOwnEvent(kSkaterExitHang, nullptr);
    TriggerRailScript(this, g_exitHangEvents);
    if (At<bool>(this, kPullUpHit)) {
        auto* feeler = &At<Engine3D::CProbeRay>(this, kPullUpFeeler);
        TriggerFeeler(this, g_pullUpEvents, feeler);
        ContactFeeler(this, feeler);
    }
    VectorAt(this, kHangOffset) = {0.0f, 0.0f, 0.0f, 1.0f};
    At<float>(this, kHeightOffset) = At<float>(this, kEndHeightOffset);
}

// 0x0043DB90
void GameObjects::CFootMovement::EndDropFromHang()
{
    ChangePhase(1);
    ObjectOf(this)->PostOwnEvent(kSkaterExitHang, nullptr);
    TriggerRailScript(this, g_exitHangEvents);
    VectorAt(this, kExitFacing) = VectorAt(this, kFacing);
    LeaveRide(&VectorAt(this, kVelocity), &At<float>(this, kVerticalSpeed));
    At<float>(this, kHeightOffset) = At<float>(this, kEndHeightOffset);
}

// 0x0043DCF0
void GameObjects::CFootMovement::EndDropToHang()
{
    SetHangOffset(this);
    ChangePhase(2);
    TellPhysicsRail(this);
    TriggerRailScript(this, g_hangEvents);
    At<bool>(ObjectOf(this)->FetchModule(kTaggingControl), 0x1D) = false;
}

// 0x0043DDD0
bool GameObjects::CFootMovement::SideClear(Engine3D::CProbeRay* feeler, const Maths::Vec4* hang, const Maths::Vec4* side)
{
    float sideLength = Param(kHangObstructionFeelerSide);
    float sx = sideLength * side->x;
    float sy = sideLength * side->y;
    float sz = sideLength * side->z;
    float sw = sideLength * side->w;
    float horiz = Param(kHangCriticalPointHorizOffset);
    const Maths::Vec4& point = hang[0];
    const Maths::Vec4& out = hang[1];
    // the critical point (the y left unrounded, as the original keeps it on the x87 stack), moved along the side
    float cx = static_cast<float>(static_cast<double>(horiz) * out.x + point.x);
    double cy = static_cast<double>(horiz) * out.y + point.y;
    float hz = horiz * out.z;
    float hw = horiz * out.w;
    float cz = static_cast<float>(static_cast<double>(hz) + point.z);
    float cw = static_cast<float>(static_cast<double>(hw) + point.w);
    Maths::Seg& line = feeler->m_line;
    line.start.w = cw + sw;
    line.start.x = cx + sx;
    line.start.y = static_cast<float>(cy + sy);
    line.start.z = static_cast<float>(static_cast<double>(cz) + sz);
    line.end = line.start;
    float origin = Param(kHangVertOriginOffset);
    line.start.y = static_cast<float>(static_cast<double>(line.start.y) - origin + CriticalPointUp());
    float up = Param(kHangObstructionFeelerUp);
    line.end.y = up + line.end.y;
    if (feeler->GetHitShape(true, false))
        return false;
    Maths::Vec4 start = line.start;
    line.start = line.end;
    line.end = start;
    return !feeler->GetHitShape(true, false);
}

// 0x0043DFC0
void GameObjects::CFootMovement::FaceHangRail(float distance, bool fromEnd, const Maths::Vec4* direction)
{
    GameObjects::CGrindRailHub* rails = RailsOf(this);
    Maths::Vec4 a, b;
    rails->GetAbsolutePos(&a, At<GameObjects::CGrindPoint*>(this, kRailEnd));
    rails->GetAbsolutePos(&b, At<GameObjects::CGrindPoint*>(this, kRailStart));
    Maths::Vec4 d = {b.x - a.x, b.y - a.y, b.z - a.z, b.w - a.w};
    float xx = d.x * d.x;
    double length = std::sqrt(static_cast<double>(d.z) * d.z + static_cast<double>(d.y) * d.y + xx);
    if (distance > length)
        distance = static_cast<float>(length);
    double fraction = distance / length;
    At<float>(this, kAlong) = static_cast<float>(fraction);
    if (fromEnd)
        At<float>(this, kAlong) = static_cast<float>(1.0 - fraction);
    // across the rail, flat
    Maths::Vec4 across = {-d.z, 0.0f, d.x, 0.0f};
    double acrossLength = std::sqrt(static_cast<double>(across.x) * across.x + xx);
    if (acrossLength > 0.0) {
        double scale = 1.0 / acrossLength;
        across.x = static_cast<float>(across.x * scale);
        across.y = static_cast<float>(0.0 * scale);
        across.z = static_cast<float>(scale * d.x);
    }
    Maths::Vec4 reversed = {-d.x, -d.y, -d.z, -d.w};
    const Maths::Vec4& along = fromEnd ? reversed : d;
    const Maths::Vec4& facing = VectorAt(this, kFacing);
    double turn = static_cast<double>(direction->z) * facing.x - static_cast<double>(facing.z) * direction->x;
    double side = static_cast<double>(across.x) * along.z - static_cast<double>(across.z) * along.x;
    if (side * turn < 0.0) {
        across.x = -across.x;
        across.y = -across.y;
        across.z = -across.z;
    }
    double dot = static_cast<double>(across.z) * facing.z + static_cast<double>(across.y) * facing.y +
                 static_cast<double>(across.x) * facing.x;
    if (X87Cos(kSixtyDegrees) > dot)
        At<CFootView*>(this, kWalkCamera)->ApplyQueuedChange();
    VectorAt(this, kFacing) = {across.x, across.y, across.z, 1.0f};
    SetHangOffset(this);
}

// 0x0043E230
bool GameObjects::CFootMovement::NextHangRail(float* t, GameObjects::CGrindPoint* start, GameObjects::CGrindPoint* end, GameObjects::CGrindPoint** newStart,
                             GameObjects::CGrindPoint** newEnd)
{
    GameObjects::CGrindRailHub* rails = RailsOf(this);
    auto grabbable = [rails](GameObjects::CGrindPoint* from, GameObjects::CGrindPoint* to) {
        Maths::Vec4 a, b;
        rails->GetAbsolutePos(&b, to);
        rails->GetAbsolutePos(&a, from);
        return GameObjects::RailClear(a, b);
    };
    // a segment's flags are its first node's
    auto usable = [](const GameObjects::CGrindPoint* first) {
        return (first->m_flagBits & GameObjects::RAIL_FLAG_ACTIVE) && !(first->m_flagBits & GameObjects::RAIL_FLAG_NO_CLIMBING);
    };
    GameObjects::CGrindPoint* joining = nullptr;
    if (*t < 0.0f) {
        // back past the start: the segment before it
        GameObjects::CGrindPoint* previous = start->m_lastValue;
        if (previous && usable(previous) && grabbable(previous, start)) {
            *newEnd = start;
            *newStart = start->m_lastValue;
            return true;
        }
        if (!rails->FindJoiningRail(start, 0x28, &joining))
            return false;
    } else {
        GameObjects::CGrindPoint* next = end->m_succ;
        if (next && usable(end) && grabbable(end, next)) {
            *newStart = end;
            *newEnd = end->m_succ;
            return true;
        }
        if (!rails->FindJoiningRail(end, 0x28, &joining))
            return false;
    }
    // a joining rail: does it run on the same way?
    bool sameWay = GameObjects::NodesTogether(joining, end) || GameObjects::NodesTogether(joining->m_succ, start);
    Maths::Vec4 a, b;
    rails->GetAbsolutePos(&a, start);
    rails->GetAbsolutePos(&b, end);
    Maths::Vec4 here = {b.x - a.x, b.y - a.y, b.z - a.z, b.w - a.w};
    here.MakeUnit();
    GameObjects::CGrindPoint* joinedNext = joining->m_succ;
    rails->GetAbsolutePos(&a, joining);
    rails->GetAbsolutePos(&b, joinedNext);
    Maths::Vec4 there = {b.x - a.x, b.y - a.y, b.z - a.z, b.w - a.w};
    there.MakeUnit();
    double tx = there.x, ty = there.y, tz = there.z;
    if (!sameWay) {
        tx *= -1.0f;
        ty *= -1.0f;
        tz *= -1.0f;
    }
    double dot = here.z * tz + ty * here.y + tx * here.x;
    if (dot < -0.86599999666213989) {
        DebugOut::Printf("M:%s:%i: %s\n", "omponents\\WalkHangUtil.cpp", 0x4F2, "BAD ANGLE ON COINCIDENT RAIL");
        return false;
    }
    if (!sameWay)
        *t = 1.0f - *t;
    *newStart = joining;
    *newEnd = joining->m_succ;
    DebugOut::Printf("M:%s:%i: %s\n", "omponents\\WalkHangUtil.cpp", 0x4FC, "FOUND COINCIDENT RAIL");
    return true;
}

// 0x0043E530
bool GameObjects::CFootMovement::PullUp(Maths::Vec4* target)
{
    // the ground to stand on: near the target, else 12 to one side of it, else 12 to the other
    Engine3D::CProbeRay ledge;
    ledge.MakeNew();
    Maths::Vec4 ground;
    bool found = FindStandPosition(target, &ground, &ledge);
    if (!found) {
        float across = -VectorAt(this, kFacing).z;
        Maths::Vec4 first = ground;
        float facingX = VectorAt(this, kFacing).x;
        target->x = static_cast<float>(static_cast<double>(across) * 12.0f + target->x);
        target->z = static_cast<float>(static_cast<double>(facingX) * 12.0f + target->z);
        target->w = target->w + 12.0f;
        found = FindStandPosition(target, &ground, &ledge);
        if (!found) {
            target->x = static_cast<float>(target->x - static_cast<double>(across) * 24.0f);
            target->z = static_cast<float>(target->z - static_cast<double>(facingX) * 24.0f);
            target->w = target->w - 24.0f;
            found = FindStandPosition(target, &ground, &ledge);
            if (!found)
                ground = first;
        }
    }
    // seven feelers round the ground point, half the push feeler's length out (an x87 sine and cosine each)
    Engine3D::CProbeRay probe;
    probe.MakeNew();
    float push = Param(kPushFeelerLength);
    float halfPush = static_cast<float>(static_cast<double>(push) * 0.5f);
    float height = Param(kFeelerHeight);
    float noRise = static_cast<float>(static_cast<double>(halfPush) * 0.0f);
    const Maths::Vec4& facing = VectorAt(this, kFacing);
    for (int i = 0; i < 7; ++i) {
        double angle = static_cast<double>(i) * 0.89759790897369385;
        double c = X87Cos(angle);
        double s = X87Sin(angle);
        probe.m_line.start = {ground.x, static_cast<float>(static_cast<double>(ground.y) + height), ground.z, ground.w};
        float dx = static_cast<float>(c * facing.x - s * facing.z);
        double dz = s * facing.x + c * facing.z;
        float outX = static_cast<float>(static_cast<double>(dx) * halfPush);
        float outZ = static_cast<float>(dz * halfPush);
        float rise = noRise + ground.y;
        probe.m_line.end = {outX + ground.x, static_cast<float>(static_cast<double>(rise) + height), outZ + ground.z,
                            ground.w + halfPush};
        if (probe.GetHitShape(true, false))
            return false;
    }
    // and from behind the skater, up past the obstruction height, to just above the ground point
    const Maths::Vec4& pos = VectorAt(this, kPos);
    probe.m_line.start = {static_cast<float>(static_cast<double>(facing.x) * -6.0f + pos.x),
                          static_cast<float>(pos.y + static_cast<double>(facing.y) * -6.0f), pos.z + facing.z * -6.0f,
                          pos.w + facing.w * -6.0f};
    float top = Param(kHangVertOriginOffset);
    float obstruction = Param(kPullUpObstructionHeight);
    probe.m_line.start.y = static_cast<float>(static_cast<double>(probe.m_line.start.y) + obstruction + top);
    probe.m_line.end = {ground.x, ground.y + 1.0f, ground.z, ground.w};
    if (probe.GetHitShape(true, false))
        return false;

    // the animated move: walking at the end (the ledge's feeler kept), or in the air with no ground
    if (found) {
        SetAnimDone(this, MemberCodeAddress(&GameObjects::CFootMovement::EndPullUp));
        AssignFeeler(&At<Engine3D::CProbeRay>(this, kPullUpFeeler), &ledge);
        At<bool>(this, kPullUpHit) = true;
        VectorAt(this, kHitNormal) = ledge.m_surfaceDir;
    } else {
        SetAnimDone(this, MemberCodeAddress(&GameObjects::CFootMovement::EndDropFromHang));
    }
    VectorAt(this, kStartPos) = pos;
    VectorAt(this, kEndPos) = ground;
    SetTransitionFacing(&VectorAt(this, kFacing), 1.0f);
    At<int>(this, kAnimWaiting) = 1;
    At<float>(this, kStartHeightOffset) = At<float>(this, kHeightOffset);
    At<float>(this, kEndHeightOffset) = 0.0f;
    double back = -(static_cast<double>(Param(kPullUpOffsetForward)) + 6.0f);
    float backF = static_cast<float>(back);
    Maths::Vec4& offset = VectorAt(this, kHangOffset);
    offset = {backF * facing.x, backF * facing.y, backF * facing.z, static_cast<float>(back * facing.w)};
    offset.y = 1.0f;
    VectorAt(this, kTargetFacing) = facing;
    At<float>(this, kTargetHeight) = pos.y - At<float>(this, kRailUp);
    ChangePhase(4);
    At<uint32_t>(this, kAnim) = kAnimPullUpFromHang;
    return true;
}

// 0x0043EC40
bool GameObjects::CFootMovement::DropToHang()
{
    if (At<bool>(At<uint8_t*>(PhysicsOf(this), 0x1528), 0x70))
        return false;
    float factor = Param(kDropToHangSpeedFactor);
    double speed = RunSpeed() * factor;
    const Maths::Vec4& velocity = VectorAt(this, kVelocity);
    double moving = static_cast<double>(velocity.x) * velocity.x + static_cast<double>(velocity.y) * velocity.y +
                    static_cast<double>(velocity.z) * velocity.z;
    if (speed * speed < moving)
        return false;
    GameObjects::CObjectModule* tagging = ObjectOf(this)->FetchModule(kTaggingControl);
    if (At<bool>(tagging, 0x1C))
        return false;
    At<float>(this, kUnknown74) = 1.0f;
    VectorAt(this, kPos).w = 1.0f;
    FoundRail rail;
    if (!FindWalkRail(this, &VectorAt(this, kOldPos), &VectorAt(this, kPos), FacingAway, kDropToClimbMaxSnap, &rail))
        return false;
    At<GameObjects::CGrindPoint*>(this, kRailStart) = rail.node;
    At<GameObjects::CGrindPoint*>(this, kRailEnd) = rail.node->m_succ;
    At<float>(this, kVerticalSpeed) = 0.0f;
    VectorAt(this, kVelocity) = {0.0f, 0.0f, 0.0f, 1.0f};
    At<float>(this, kAlong) = rail.along;
    At<float>(this, kRailOut) = rail.out;
    At<float>(this, kRailUp) = rail.up;
    At<bool>(tagging, 0x1D) = true;
    VectorAt(this, kStartPos) = VectorAt(this, kPos);
    VectorAt(this, kEndPos) = rail.point;
    SetAnimDone(this, MemberCodeAddress(&GameObjects::CFootMovement::EndDropToHang));
    SetTransitionFacing(&rail.facing, Param(kDropToHangRotateFactor));
    At<int>(this, kAnimWaiting) = 1;
    float horiz = Param(kHangCriticalPointHorizOffset);
    VectorAt(this, kHangOffset) = {rail.facing.x * horiz, rail.facing.y * horiz, rail.facing.z * horiz,
                                   rail.facing.w * horiz};
    VectorAt(this, kTargetFacing) = rail.facing;
    At<float>(this, kTargetHeight) = VectorAt(this, kPos).y;
    At<float>(this, kStartHeightOffset) = At<float>(this, kHeightOffset);
    At<float>(this, kEndHeightOffset) = static_cast<float>(HangHeightOffset());
    ChangePhase(4);
    At<uint32_t>(this, kAnim) = kAnimDropToHang;
    return true;
}

// 0x0043F030
bool GameObjects::CFootMovement::GrabRail(Maths::Vec4 start, Maths::Vec4 end)
{
    if (At<float>(this, kVerticalSpeed) > 50.0f)
        return false;
    start.y = start.y + Param(kHangVertOriginOffset);
    end.y = end.y + Param(kHangVertOriginOffset);
    start.w = 1.0f;
    end.w = 1.0f;
    FoundRail rail;
    if (!FindWalkRail(this, &start, &end, Facing, kClimbMaxSnap, &rail))
        return false;
    At<GameObjects::CGrindPoint*>(this, kRailStart) = rail.node;
    At<GameObjects::CGrindPoint*>(this, kRailEnd) = rail.node->m_succ;
    VectorAt(this, kPos) = rail.point;
    VectorAt(this, kFacing) = rail.facing;
    At<float>(this, kAlong) = rail.along;
    At<float>(this, kRailUp) = rail.up;
    At<float>(this, kRailOut) = rail.out;
    At<uint32_t>(this, kHangAnim) = rail.anim;
    SetHangOffset(this);
    VectorAt(this, kVelocity) = {0.0f, 0.0f, 0.0f, 1.0f};
    At<float>(this, kVerticalSpeed) = 0.0f;
    At<float>(this, kHeightOffset) = static_cast<float>(HangHeightOffset());
    ChangePhase(2);
    TellPhysicsRail(this);
    TriggerRailScript(this, g_hangEvents);
    At<uint32_t>(this, kAnim) = kAnimHang;
    SetPhysicsFlagA0(this);
    return true;
}

// 0x0043F3A0
bool GameObjects::CFootMovement::ShimmyBlocked(const Maths::Vec4* out, float along, const Maths::Vec4* point, bool left,
                              const Maths::Vec4* move, Maths::Vec4 segment, GameObjects::CGrindPoint* start, GameObjects::CGrindPoint* end)
{
    double dot = static_cast<double>(segment.y) * move->y + static_cast<double>(segment.x) * move->x +
                 static_cast<double>(segment.z) * move->z;
    bool towardsEnd = dot < 0.0 ? left : !left;
    Maths::Vec4 sideways = left ? *move : Maths::Vec4{-move->x, -move->y, -move->z, -move->w};
    Maths::Vec4 outwards = *out;
    double t = towardsEnd ? 1.0 - along : along;
    double segmentLength = std::sqrt(static_cast<double>(segment.z) * segment.z +
                                     static_cast<double>(segment.y) * segment.y +
                                     static_cast<double>(segment.x) * segment.x);
    float distance = static_cast<float>(segmentLength * t);
    float sideLength = Param(kHangMoveCollisionSideLength);
    if (static_cast<double>(sideLength) + 1.0f > distance) {
        // near the segment's end: look along the next segment instead, if it doesn't turn too far
        along = (towardsEnd ? 2.0f : -2.0f) + along;
        GameObjects::CGrindPoint* nextStart;
        GameObjects::CGrindPoint* nextEnd;
        if (NextHangRail(&along, start, end, &nextStart, &nextEnd)) {
            float sign = along > 0.5f ? -1.0f : 1.0f;
            Maths::Vec4 a, b;
            RailsOf(this)->GetAbsolutePos(&a, nextEnd);
            RailsOf(this)->GetAbsolutePos(&b, nextStart);
            double dw = static_cast<double>(b.w) - a.w;
            float dz = b.z - a.z;
            float dx = b.x - a.x;
            Maths::Vec4 next = {dx * sign, 0.0f, dz * sign, static_cast<float>(dw * sign)};
            next.MakeUnit();
            double turn = static_cast<double>(next.y) * out->y + static_cast<double>(next.z) * out->z +
                          static_cast<double>(next.x) * out->x;
            if (turn < 0.0 && turn > -0.87000000476837158) {
                sideways = next;
                if (left) {
                    outwards.x = next.z;
                    outwards.z = -next.x;
                } else {
                    outwards.z = next.x;
                    outwards.x = -next.z;
                }
            }
        }
    }
    // the feeler: from the hang point, raised and pulled back out of the wall
    Engine3D::CProbeRay feeler;
    feeler.MakeNew();
    Maths::Seg& line = feeler.m_line;
    line.start = *point;
    line.start.y = line.start.y + Param(kHangMoveCollisionUp);
    float back = Param(kHangMoveCollisionBack);
    line.start.x = static_cast<float>(line.start.x - static_cast<double>(back) * out->x);
    line.start.y = static_cast<float>(line.start.y - static_cast<double>(back) * out->y);
    line.start.z = static_cast<float>(line.start.z - static_cast<double>(back) * out->z);
    line.start.w = static_cast<float>(line.start.w - static_cast<double>(back) * out->w);
    // `v` times `scale` added to the end (each product unrounded, as the x87 code adds it)
    auto addScaled = [&line](float scale, const Maths::Vec4& v) {
        line.end.x = static_cast<float>(static_cast<double>(scale) * v.x + line.end.x);
        line.end.y = static_cast<float>(static_cast<double>(scale) * v.y + line.end.y);
        line.end.z = static_cast<float>(static_cast<double>(scale) * v.z + line.end.z);
        line.end.w = static_cast<float>(static_cast<double>(scale) * v.w + line.end.w);
    };
    // sideways at the hang point's height, at the side height and at half of it
    line.end = line.start;
    addScaled(Param(kHangMoveCollisionSideLength), sideways);
    if (feeler.GetHitShape(true, false))
        return true;
    line.end = line.start;
    line.end.y = line.end.y + Param(kHangMoveCollisionSideHeight);
    addScaled(Param(kHangMoveCollisionSideLength), sideways);
    if (feeler.GetHitShape(true, false))
        return true;
    line.end = line.start;
    line.end.y = static_cast<float>(static_cast<double>(Param(kHangMoveCollisionSideHeight)) * 0.5f + line.end.y);
    addScaled(Param(kHangMoveCollisionSideLength), sideways);
    if (feeler.GetHitShape(true, false))
        return true;
    // down at the critical point, out from the wall, sideways and one more along the segment
    line.end = line.start;
    float up = Param(kHangMoveCollisionUp);
    line.end.y = static_cast<float>(CriticalPointUp() - up - 1.0f + line.end.y);
    addScaled(Param(kHangCriticalPointHorizOffset), outwards);
    addScaled(Param(kHangMoveCollisionSideLength), sideways);
    segment.MakeUnit();
    const Maths::Vec4 onward = towardsEnd ? Maths::Vec4{-segment.x, -segment.y, -segment.z, -segment.w} : segment;
    line.end.x = line.end.x + onward.x;
    line.end.y = line.end.y + onward.y;
    line.end.z = line.end.z + onward.z;
    line.end.w = line.end.w + onward.w;
    if (feeler.GetHitShape(true, false))
        return true;
    // half the side height down, out and sideways (each step rounded: the original's vector operators)
    line.end = line.start;
    line.end.y = static_cast<float>(line.end.y - static_cast<double>(Param(kHangMoveCollisionSideHeight)) * 0.5f);
    line.end += Param(kHangCriticalPointHorizOffset) * outwards;
    line.end += Param(kHangMoveCollisionSideLength) * sideways;
    return feeler.GetHitShape(true, false) != 0;
}

// 0x0043FC70
void GameObjects::CFootMovement::PlaceOnHang()
{
    CGrindRailHub* rails = RailsOf(this);
    Maths::Vec4 a, b, point;
    Maths::Vec4* endPos = rails->GetAbsolutePos(&a, At<CGrindPoint*>(this, kRailEnd));
    Maths::Vec4* startPos = rails->GetAbsolutePos(&b, At<CGrindPoint*>(this, kRailStart));
    Maths::Vec4& pos = VectorAt(this, kPos);
    pos = *Walk::Lerp(&point, startPos, endPos, At<float>(this, kAlong));
    pos.y = At<float>(this, kRailUp) + pos.y;
    // out from the rail along the facing (x's product left unrounded, as the x87 code adds it)
    float out = At<float>(this, kRailOut);
    Maths::Vec4& facing = VectorAt(this, kFacing);
    double outX = static_cast<double>(out) * facing.x;
    float outY = out * facing.y;
    float outZ = out * facing.z;
    float outW = out * facing.w;
    pos.x = static_cast<float>(outX + pos.x);
    pos.y = outY + pos.y;
    pos.z = outZ + pos.z;
    pos.w = outW + pos.w;

    // riding something: let go of it once it's gone; when it turned, the facing across the rail again
    void* contact = At<void*>(this, kContact);
    auto* tracker = At<CRideTracker*>(contact, 0x18);
    if (!tracker)
        return;
    if (!tracker->m_entity.Pull()) {
        StopRiding(this);
        return;
    }
    if (!tracker->m_turned)
        return;
    rails->GetAbsolutePos(&a, At<CGrindPoint*>(this, kRailEnd));
    rails->GetAbsolutePos(&b, At<CGrindPoint*>(this, kRailStart));
    Maths::Vec4 along = {b.x - a.x, 0.0f, b.z - a.z, b.w - a.w};
    along.MakeUnit();
    double side = static_cast<double>(along.x) * facing.z + static_cast<double>(-along.z) * facing.x;
    float facingY = facing.y;
    facing.y = 0.0f;
    side = side + static_cast<double>(facingY) * 0.0f;
    if (side > 0.0) {
        facing.x = -along.z;
        facing.z = along.x;
        facing.w = 1.0f;
    } else {
        facing.w = -1.0f; // (the original's)
        facing.x = along.z;
        facing.z = -along.x;
    }
}

// 0x0043FE30
bool GameObjects::CFootMovement::HangClear(Engine3D::CProbeRay* feeler, const Maths::Vec4* point, Record34* record)
{
    auto* rail = reinterpret_cast<FoundRail*>(record);
    rail->point = *point;
    CGrindPoint* node = rail->node;
    Maths::Vec4 side = {-rail->facing.z, 0.0f, rail->facing.x, 1.0f};
    Maths::Vec4 a, b;
    Maths::Vec4* nextPos = RailsOf(this)->GetAbsolutePos(&a, node->m_succ);
    Maths::Vec4* nodePos = RailsOf(this)->GetAbsolutePos(&b, rail->node);
    Maths::Vec4 segment = {nodePos->x - nextPos->x, nodePos->y - nextPos->y, nodePos->z - nextPos->z,
                           nodePos->w - nextPos->w};
    // room at both sides
    if (!SideClear(feeler, &rail->point, &side))
        return false;
    Maths::Vec4 otherSide = {-side.x, 0.0f, -side.z, -1.0f};
    if (!SideClear(feeler, &rail->point, &otherSide))
        return false;
    // the hang point: below the rail and out from it; room to shimmy either way from there
    rail->point.y = rail->point.y - Param(kHangVertOriginOffset);
    float horiz = Param(kHangHorizOriginOffset);
    rail->point.x = static_cast<float>(static_cast<double>(horiz) * rail->facing.x + rail->point.x);
    rail->point.y = static_cast<float>(static_cast<double>(horiz) * rail->facing.y + rail->point.y);
    rail->point.z = static_cast<float>(static_cast<double>(horiz) * rail->facing.z + rail->point.z);
    rail->point.w = static_cast<float>(static_cast<double>(horiz) * rail->facing.w + rail->point.w);
    if (ShimmyBlocked(&rail->facing, rail->along, &rail->point, true, &side, segment, rail->node, rail->node->m_succ))
        return false;
    if (ShimmyBlocked(&rail->facing, rail->along, &rail->point, false, &side, segment, rail->node, rail->node->m_succ))
        return false;
    // nothing between the skater and the critical point - or, on the ground, over whatever is (up first)
    Maths::Vec4 critical = rail->point + Param(kHangCriticalPointHorizOffset) * rail->facing;
    critical.y = static_cast<float>(CriticalPointUp() + critical.y);
    Maths::Seg& line = feeler->m_line;
    line.start = VectorAt(this, kPos);
    line.end = critical;
    if (!feeler->GetHitShape(true, false))
        return true;
    if (At<int>(this, kState) != 0)
        return false;
    line.end = VectorAt(this, kPos);
    line.end.y = Param(kHopObstructionFeelerUp) + line.end.y;
    if (feeler->GetHitShape(true, false))
        return false;
    line.start = line.end;
    line.end = critical;
    return !feeler->GetHitShape(true, false);
}

// 0x00440130
void GameObjects::CFootMovement::Shimmy(float move)
{
    Maths::Vec4& pos = VectorAt(this, kPos);
    Maths::Vec4& offset = VectorAt(this, kHangOffset);
    Maths::Vec4& facing = VectorAt(this, kFacing);
    float& along = At<float>(this, kAlong);
    CGrindPoint*& start = At<CGrindPoint*>(this, kRailStart);
    CGrindPoint*& end = At<CGrindPoint*>(this, kRailEnd);
    // all of it kept, to be put back if the move is blocked
    const Maths::Vec4 oldPos = pos;
    const Maths::Vec4 oldCritical = {pos.x + offset.x, pos.y + offset.y, pos.z + offset.z, pos.w + offset.w};
    const float oldAlong = along;
    CGrindPoint* const oldStart = start;
    CGrindPoint* const oldEnd = end;
    const Maths::Vec4 oldOffset = offset;
    const Maths::Vec4 oldFacing = facing;

    Maths::Vec4 side = {-facing.z, 0.0f, facing.x, 1.0f};
    Maths::Vec4 a, b;
    Maths::Vec4* endPos = RailsOf(this)->GetAbsolutePos(&a, end);
    Maths::Vec4* startPos = RailsOf(this)->GetAbsolutePos(&b, start);
    Maths::Vec4 segment = {startPos->x - endPos->x, startPos->y - endPos->y, startPos->z - endPos->z,
                           startPos->w - endPos->w};
    bool left = move > 0.0f;
    // (the differences unrounded here, as the x87 code keeps them)
    double dot = static_cast<double>(side.x) * (static_cast<double>(startPos->x) - endPos->x) +
                 (static_cast<double>(startPos->z) - endPos->z) * oldFacing.x +
                 (static_cast<double>(startPos->y) - endPos->y) * 0.0f;
    bool towardsEnd = dot < 0.0 ? left : !left;
    if (ShimmyBlocked(&facing, along, &pos, left, &side, segment, start, end)) {
        At<float>(this, kHangMoveSpeed) = 0.0f;
        At<uint32_t>(this, kAnim) = kAnimHang;
        return;
    }

    // along the rail by the move
    double lengthSquared = static_cast<double>(segment.x) * segment.x + static_cast<double>(segment.y) * segment.y +
                           static_cast<double>(segment.z) * segment.z;
    float lengthSquaredF = static_cast<float>(lengthSquared);
    double step = std::fabs(move) / std::sqrt(lengthSquared);
    if (!towardsEnd)
        step = -step;
    double moved = step + along;
    along = static_cast<float>(moved);
    if (moved < 0.0 || moved > 1.0) {
        // past an end: onto the next rail (the rest of the way along it), or stopped at the end
        if (!NextHangRail(&along, start, end, &start, &end)) {
            float t = along;
            At<float>(this, kHangMoveSpeed) = 0.0f;
            if (t > 1.0f)
                along = 1.0f;
            else
                along = t < 0.0f ? 0.0f : t;
            At<uint32_t>(this, kAnim) = kAnimHang;
            goto check;
        }
        float t = along;
        double clamped = t > 1.0f ? 1.0 : t < 0.0f ? 0.0 : t;
        double over = static_cast<double>(along) - clamped;
        float rest = static_cast<float>(std::fabs(over) * std::sqrt(static_cast<double>(lengthSquaredF)));
        Maths::Vec4 reversed;
        const Maths::Vec4* direction = &segment;
        if (!towardsEnd) {
            reversed = {-segment.x, -segment.y, -segment.z, -segment.w};
            direction = &reversed;
        }
        FaceHangRail(rest, along < 0.0f, direction);
    }
    At<uint32_t>(this, kAnim) = left ? kAnimHangMoveLeft : kAnimHangMoveRight;

check:
    // nothing in the way from the old critical point to the new one, or it's all put back
    {
        Maths::Vec4 critical = {offset.x + pos.x, offset.y + pos.y, offset.z + pos.z, offset.w + pos.w};
        Engine3D::CProbeRay feeler;
        feeler.MakeNew();
        feeler.m_line.start = oldCritical;
        feeler.m_line.end = critical;
        if (feeler.GetHitShape(true, false)) {
            pos = oldPos;
            along = oldAlong;
            start = oldStart;
            end = oldEnd;
            offset = oldOffset;
            facing = oldFacing;
            At<float>(this, kHangMoveSpeed) = 0.0f;
            At<uint32_t>(this, kAnim) = kAnimHang;
        }
    }
}

// 0x00440690
bool GameObjects::CFootMovement::CheckRail(const Maths::Vec4* start, const Maths::Vec4* end, const Maths::Vec4* onRail,
                                    const Maths::Vec4* onLine, float along, Record34* record, bool away)
{
    auto* rail = reinterpret_cast<FoundRail*>(record);
    float origin = Param(kHangVertOriginOffset);
    // in the air: only rails at or below the line, and close to it
    if (At<int>(this, kState) == 1) {
        if (onRail->y > onLine->y)
            return false;
        if (std::fabs(static_cast<double>(onRail->y) - onLine->y) > 12.0f)
            return false;
    }
    // not too steep
    float dz = end->z - start->z;
    float dx = end->x - start->x;
    Maths::Vec4 direction = {dx, end->y - start->y, dz, end->w - start->w};
    direction.MakeUnit();
    if (std::fabs(direction.y) > X87Sin(static_cast<double>(Param(kHangMaxRailAscent)) * 0.017453292384743690))
        return false;

    // the facing: across the rail, flat - towards the skater's facing (or away)
    Maths::Vec4& out = rail->facing;
    out = {-dz, 0.0f, dx, 1.0f};
    out.MakeUnit();
    const Maths::Vec4& facing = VectorAt(this, kFacing);
    auto flip = [&out] {
        out.x = -out.x;
        out.y = -out.y;
        out.z = -out.z;
    };
    double towards = static_cast<double>(facing.z) * out.z + static_cast<double>(facing.y) * out.y +
                     static_cast<double>(facing.x) * out.x;
    if (away ? towards > 0.0 : towards < 0.0)
        flip();
    // a one-sided rail (flag 0x80: hung from one side, 0x40: the other) turns it round
    bool onRight = static_cast<double>(direction.x) * out.z - static_cast<double>(direction.z) * out.x > 0.0;
    uint32_t flags = rail->node->m_flagBits;
    if (((flags & 0x80) && onRight) || ((flags & 0x40) && !onRight)) {
        flip();
        onRight = !onRight;
    }

    // room to hang there (the collision near the rail cached), facing that way - or the other, if the rail allows
    Engine3D::CProbeRay feeler;
    feeler.MakeNew();
    Engine3D::CHitMeshCache cache;
    cache.MakeNew();
    float height = origin + 12.0f;
    Maths::BBox box;
    box.max = {onRail->x + 24.0f, height + onRail->y, onRail->z + 24.0f, onRail->w + 1.0f};
    box.min = {onRail->x - 24.0f, onRail->y - 24.0f, onRail->z - 24.0f, onRail->w - 1.0f};
    cache.TickFrame(box);
    feeler.m_hitCache = &cache;
    rail->along = along;
    if (!HangClear(&feeler, onRail, record)) {
        if ((At<int>(this, kState) != 1 && away) || ((flags & 0x80) && !onRight) || ((flags & 0x40) && onRight) ||
            (flip(), !HangClear(&feeler, onRail, record))) {
            cache.Destruct();
            return false;
        }
    }

    // up and out from the rail; the animation: WALL when a feeler out from the hang point, raised, hits
    rail->up = rail->point.y - onRail->y;
    rail->out = static_cast<float>(((static_cast<double>(rail->point.y) - onRail->y) * out.y +
                                    (static_cast<double>(rail->point.z) - onRail->z) * out.z) +
                                   (static_cast<double>(rail->point.x) - onRail->x) * out.x);
    float horiz = Param(kHangCriticalPointHorizOffset);
    float hx = horiz * out.x, hy = horiz * out.y, hz = horiz * out.z;
    double hw = static_cast<double>(horiz) * out.w;
    Maths::Seg& line = feeler.m_line;
    line.start = {hx + rail->point.x, hy + rail->point.y, hz + rail->point.z, static_cast<float>(hw + rail->point.w)};
    line.start.y = line.start.y + Param(kHangInitAnimFeelerHeight);
    line.end = line.start;
    float length = Param(kHangInitAnimFeelerLength);
    line.end.x = static_cast<float>(static_cast<double>(length) * out.x + line.end.x);
    line.end.y = static_cast<float>(static_cast<double>(length) * out.y + line.end.y);
    line.end.z = static_cast<float>(static_cast<double>(length) * out.z + line.end.z);
    line.end.w = static_cast<float>(static_cast<double>(length) * out.w + line.end.w);
    rail->anim = feeler.GetHitShape(true, false) ? kAnimWall : kAnimSwing;
    cache.Destruct();
    return true;
}

// 0x00440BF0
void GameObjects::CFootMovement::UpdateHang()
{
    // carried by what it rides on
    void* contact = At<void*>(this, kContact);
    if (auto* tracker = At<CRideTracker*>(contact, 0x18)) {
        if (!tracker->m_entity.Pull() || !tracker->TickFrame(&VectorAt(this, kPos))) {
            StopRiding(this);
        } else {
            CObjectModule* component =
                At<CRideTracker*>(At<void*>(this, kContact), 0x18)->m_entity.Pull()->FetchModule(kRailManagerComponent);
            Maths::Mat4 matrix;
            ObjectMatrixOf(component, &matrix); // (unused)
            PlaceOnHang();
        }
    }
    At<bool>(this, kFlag1958) = false;

    Maths::Vec4& pos = VectorAt(this, kPos);
    const Maths::Vec4& facing = VectorAt(this, kFacing);
    uint8_t* input = At<uint8_t*>(this, kInput);
    if (At<bool>(this, kControlHeld)) {
        if (At<int>(input, 0x250) == 0 && At<float>(input, 0x24C) != 0.0f &&
            std::fabs(At<float>(input, 0x248)) < 0.78539818525314331f) {
            // pushed forward: a pull-up, pull_up_offset_forward ahead - then half that
            float forward = Param(kPullUpOffsetForward);
            Maths::Vec4 target = {static_cast<float>(static_cast<double>(forward) * facing.x + pos.x),
                                  static_cast<float>(static_cast<double>(forward) * facing.y + pos.y),
                                  forward * facing.z + pos.z, forward * facing.w + pos.w};
            target.y = Param(kPullUpOffsetUp) + target.y;
            if (PullUp(&target))
                return;
            double half = static_cast<double>(Param(kPullUpOffsetForward)) * 0.5f;
            target = {static_cast<float>(half * facing.x) + pos.x, static_cast<float>(half * facing.y) + pos.y,
                      static_cast<float>(half * facing.z) + pos.z, static_cast<float>(half * facing.w + pos.w)};
            target.y = Param(kPullUpOffsetUp) + target.y;
            if (PullUp(&target))
                return;
        } else if (reinterpret_cast<const Walk::SkaterTurn*>(input + 0x20)->TurnedRound()) {
            // turned round: let go
            ChangePhase(1);
            ObjectOf(this)->PostOwnEvent(kSkaterExitHang, nullptr);
            TriggerRailScript(this, g_exitHangEvents);
            VectorAt(this, kExitFacing) = facing;
            LeaveRide(&VectorAt(this, kVelocity), &At<float>(this, kVerticalSpeed));
            At<uint32_t>(this, kAnim) = kAnimAir;
            return;
        }
    }

    // the shimmy's speed: the control's sideways part (within hang_vert_control_tolerance of straight sideways)
    float sideways = static_cast<float>(1.5707963705062866 - std::fabs(At<float>(input, 0x248)));
    float tolerance = Param(kHangVertControlTolerance);
    float wanted = 0.0f;
    if (X87Cos(static_cast<double>(tolerance) * 0.017453292384743690) > std::fabs(sideways)) {
        float direction = At<float>(input, 0x248) > 0.0f ? 1.0f : -1.0f;
        float speed = Param(kHangMoveSpeedParam);
        wanted = static_cast<float>(static_cast<double>(direction) * At<float>(this, kControlMagnitude) * speed);
    }
    float rate = Param(kHangMoveLerpRate);
    float& moveSpeed = At<float>(this, kHangMoveSpeed);
    moveSpeed = static_cast<float>(moveSpeed + (static_cast<double>(wanted) - moveSpeed) *
                                                   (static_cast<double>(rate) * At<float>(this, kFrameLength)));
    float speedNow = moveSpeed;
    float cutoff = Param(kHangMoveCutoff);
    if (std::fabs(speedNow) > cutoff) {
        At<float>(this, kAnimSpeed) = std::fabs(moveSpeed);
        Shimmy(moveSpeed * At<float>(this, kFrameLength));
        PlaceOnHang();
        return;
    }
    At<uint32_t>(this, kAnim) = kAnimHang;
}

// --- ladders ---

// 0x00441020
void GameObjects::CFootMovement::EndLadderOffBottom()
{
    ChangePhase(0);
    TriggerRailScript(this, g_exitLadderEvents);
    if (At<bool>(this, kPullUpHit)) {
        auto* feeler = &At<Engine3D::CProbeRay>(this, kPullUpFeeler);
        TriggerFeeler(this, g_ladderOffBottomEvents, feeler);
        ContactFeeler(this, feeler);
    }
}

// 0x00441070
void GameObjects::CFootMovement::EndLadderOffTop()
{
    ChangePhase(0);
    TriggerRailScript(this, g_exitLadderEvents);
    if (At<bool>(this, kPullUpHit)) {
        auto* feeler = &At<Engine3D::CProbeRay>(this, kPullUpFeeler);
        TriggerFeeler(this, g_ladderOffTopEvents, feeler);
        ContactFeeler(this, feeler);
    }
}

// 0x00441860
void GameObjects::CFootMovement::EndLadderOffTopToAir()
{
    ChangePhase(1);
    TriggerRailScript(this, g_exitLadderEvents);
    VectorAt(this, kExitFacing) = VectorAt(this, kFacing);
    LeaveRide(&VectorAt(this, kVelocity), &At<float>(this, kVerticalSpeed));
}

// 0x004410C0
void GameObjects::CFootMovement::EndOntoLadderBottom()
{
    CGrindPoint* node = At<CGrindPoint*>(this, kRailStart);
    CGrindPoint* other = LadderOtherEnd(node);
    Maths::Vec4 a, b;
    Maths::Vec4* nodePos = RailsOf(this)->GetAbsolutePos(&a, node);
    Maths::Vec4* otherPos = RailsOf(this)->GetAbsolutePos(&b, other);
    float dx = otherPos->x - nodePos->x;
    float dy = otherPos->y - nodePos->y;
    float dz = otherPos->z - nodePos->z;
    float up = Param(kLadderBottomOffsetUp);
    double length = std::sqrt(static_cast<double>(dz) * dz + static_cast<double>(dy) * dy + static_cast<double>(dx) * dx);
    At<float>(this, kAlong) = static_cast<float>(up / length);
    ChangePhase(3);
    TellPhysicsNode(this);
}

// 0x00441190
void GameObjects::CFootMovement::EndOntoLadderTop()
{
    CGrindPoint* node = At<CGrindPoint*>(this, kRailStart);
    CGrindPoint* other = LadderOtherEnd(node);
    Maths::Vec4 a, b;
    Maths::Vec4* nodePos = RailsOf(this)->GetAbsolutePos(&a, node);
    Maths::Vec4* otherPos = RailsOf(this)->GetAbsolutePos(&b, other);
    double dx = static_cast<double>(otherPos->x) - nodePos->x;
    double dy = static_cast<double>(otherPos->y) - nodePos->y;
    double dz = static_cast<double>(otherPos->z) - nodePos->z;
    float length = static_cast<float>(std::sqrt(dz * dz + dy * dy + dx * dx));
    float up = Param(kLadderTopOffsetUp);
    At<float>(this, kAlong) = static_cast<float>((static_cast<double>(length) - up) / length);
    ChangePhase(3);
    TellPhysicsNode(this);
}

// 0x00441260
bool GameObjects::CFootMovement::CheckLadderTop(const Maths::Vec4* bottom, const Maths::Vec4* top,
                                         const Maths::Mat4* orientation, Record24* record)
{
    if (bottom->y > top->y)
        return false;
    Maths::Vec4 up = {top->x - bottom->x, top->y - bottom->y, top->z - bottom->z, top->w - bottom->w};
    up.MakeUnit();
    if (up.y < 0.995f)
        return false;
    // ladder_top_offset_up below the top, ladder_climb_offset out of the ladder (x and y's products unrounded, as the
    // x87 code subtracts them)
    auto* ladder = reinterpret_cast<FoundLadder*>(record);
    float offset = Param(kLadderTopOffsetUp);
    float oy = offset * up.y, oz = offset * up.z, ow = offset * up.w;
    ladder->point = {static_cast<float>(top->x - static_cast<double>(offset) * up.x), top->y - oy, top->z - oz,
                     top->w - ow};
    float climb = Param(kLadderClimbOffset);
    const Maths::Vec4& at = orientation->row[2];
    float cz = climb * at.z, cw = climb * at.w;
    ladder->point.x = static_cast<float>(ladder->point.x - static_cast<double>(climb) * at.x);
    ladder->point.y = static_cast<float>(ladder->point.y - static_cast<double>(climb) * at.y);
    ladder->point.z = ladder->point.z - cz;
    ladder->point.w = ladder->point.w - cw;
    ladder->facing = at;
    return true;
}

// 0x004413E0
bool GameObjects::CFootMovement::CheckLadderLine(const Maths::Vec4* bottom, const Maths::Vec4* top,
                                          const Maths::Mat4* orientation, const Maths::Vec4* onLadder, float along,
                                          Record24* record)
{
    // upright enough (its height as part of its length; no length: the height itself)
    float dy = top->y - bottom->y;
    double dz = static_cast<double>(top->z) - bottom->z;
    double dx = static_cast<double>(top->x) - bottom->x;
    double length = std::sqrt(dx * dx + static_cast<double>(dy) * dy + dz * dz);
    float upY = dy;
    if (length > 0.0)
        upY = static_cast<float>(dy * (1.0 / length));
    if (upY < 0.995f)
        return false;
    // the line meets it between ladder_bottom_offset_up above the bottom and ladder_top_offset_up below the top
    if (static_cast<double>(upY) * Param(kLadderBottomOffsetUp) + bottom->y > onLadder->y)
        return false;
    if (top->y - static_cast<double>(upY) * Param(kLadderTopOffsetUp) < onLadder->y)
        return false;
    // the point ladder_climb_offset out of it there, if nothing is in the way
    auto* ladder = reinterpret_cast<FoundLadder*>(record);
    float climb = Param(kLadderClimbOffset);
    const Maths::Vec4& at = orientation->row[2];
    float cx = climb * at.x, cy = climb * at.y, cz = climb * at.z;
    ladder->point = {onLadder->x - cx, onLadder->y - cy, onLadder->z - cz,
                     static_cast<float>(onLadder->w - static_cast<double>(climb) * at.w)};
    Engine3D::CProbeRay feeler;
    feeler.MakeNew();
    feeler.m_line.start = VectorAt(this, kPos);
    feeler.m_line.end = ladder->point;
    if (feeler.GetHitShape(true, false))
        return false;
    ladder->facing = at;
    ladder->along = along;
    SetPhysicsFlagA0(this);
    return true;
}

// 0x00441680
void GameObjects::CFootMovement::ClimbOffBottom()
{
    // ladder_bottom_offset_forward back and ladder_bottom_offset_up down: the ground there, if any
    float forward = Param(kLadderBottomOffsetForward);
    Maths::Vec4& pos = VectorAt(this, kPos);
    Maths::Vec4& facing = VectorAt(this, kFacing);
    float fz = forward * facing.z, fw = forward * facing.w;
    Maths::Vec4 target = {static_cast<float>(pos.x - static_cast<double>(forward) * facing.x),
                          static_cast<float>(pos.y - static_cast<double>(forward) * facing.y), pos.z - fz, pos.w - fw};
    target.y = target.y - Param(kLadderBottomOffsetUp);
    Engine3D::CProbeRay* ground = &At<Engine3D::CProbeRay>(this, kPullUpFeeler);
    bool found = FindStandPosition(&target, &target, ground);
    At<bool>(this, kPullUpHit) = found;
    if (found)
        VectorAt(this, kHitNormal) = ground->m_surfaceDir;
    // the animated move there
    VectorAt(this, kStartPos) = pos;
    VectorAt(this, kEndPos) = target;
    SetAnimDone(this, MemberCodeAddress(&GameObjects::CFootMovement::EndLadderOffBottom));
    At<float>(this, kStartHeightOffset) = At<float>(this, kHeightOffset);
    At<float>(this, kEndHeightOffset) = 0.0f;
    SetTransitionFacing(&facing, 1.0f);
    At<int>(this, kAnimWaiting) = 1;
    VectorAt(this, kTargetFacing) = facing;
    At<float>(this, kTargetHeight) = pos.y;
    ChangePhase(4);
    At<uint32_t>(this, kAnim) = kAnimLadderOffBottom;
}

// 0x004418B0
bool GameObjects::CFootMovement::CheckLadderBottom(const Maths::Vec4* bottom, const Maths::Vec4* top,
                                            const Maths::Mat4* orientation, Record24* record)
{
    if (bottom->y > top->y)
        return false;
    Maths::Vec4 up = {top->x - bottom->x, top->y - bottom->y, top->z - bottom->z, top->w - bottom->w};
    up.MakeUnit();
    if (up.y < 0.995f)
        return false;
    // faced (within max_onto_ladder_angle)
    float maxAngle = Param(kMaxOntoLadderAngle);
    const Maths::Vec4& at = orientation->row[2];
    const Maths::Vec4& facing = VectorAt(this, kFacing);
    double dot = static_cast<double>(at.z) * facing.z + static_cast<double>(at.y) * facing.y +
                 static_cast<double>(at.x) * facing.x;
    if (dot < X87Cos(static_cast<double>(maxAngle) * 0.017453292384743690))
        return false;
    // ladder_bottom_offset_up above the bottom, ladder_climb_offset out of the ladder
    auto* ladder = reinterpret_cast<FoundLadder*>(record);
    float offset = Param(kLadderBottomOffsetUp);
    float ox = offset * up.x, oy = offset * up.y, oz = offset * up.z;
    ladder->point = {ox + bottom->x, oy + bottom->y, oz + bottom->z,
                     static_cast<float>(static_cast<double>(offset) * up.w + bottom->w)};
    float climb = Param(kLadderClimbOffset);
    ladder->point.x = static_cast<float>(ladder->point.x - static_cast<double>(climb) * at.x);
    ladder->point.y = static_cast<float>(ladder->point.y - static_cast<double>(climb) * at.y);
    ladder->point.z = static_cast<float>(ladder->point.z - static_cast<double>(climb) * at.z);
    ladder->point.w = static_cast<float>(ladder->point.w - static_cast<double>(climb) * at.w);
    // nothing between the skater and 6 behind that point
    Engine3D::CProbeRay feeler;
    feeler.MakeNew();
    feeler.m_line.start = VectorAt(this, kPos);
    float bx = at.x * 6.0f, by = at.y * 6.0f, bz = at.z * 6.0f;
    feeler.m_line.end = {ladder->point.x - bx, ladder->point.y - by, ladder->point.z - bz,
                         static_cast<float>(ladder->point.w - static_cast<double>(at.w) * 6.0f)};
    if (feeler.GetHitShape(true, false))
        return false;
    ladder->facing = at;
    return true;
}

// 0x00441B80
void GameObjects::CFootMovement::ClimbOffTop()
{
    // ladder_top_offset_forward ahead and ladder_top_offset_up up: the ground there - or into the air
    float forward = Param(kLadderTopOffsetForward);
    Maths::Vec4& pos = VectorAt(this, kPos);
    Maths::Vec4& facing = VectorAt(this, kFacing);
    float fx = forward * facing.x, fy = forward * facing.y, fz = forward * facing.z;
    Maths::Vec4 target = {fx + pos.x, fy + pos.y, fz + pos.z,
                          static_cast<float>(static_cast<double>(forward) * facing.w + pos.w)};
    target.y = target.y + Param(kLadderTopOffsetUp);
    Engine3D::CProbeRay ledge;
    ledge.MakeNew();
    bool found = FindStandPosition(&target, &target, &ledge);
    VectorAt(this, kStartPos) = pos;
    VectorAt(this, kEndPos) = target;
    if (found) {
        SetAnimDone(this, MemberCodeAddress(&GameObjects::CFootMovement::EndLadderOffTop));
        AssignFeeler(&At<Engine3D::CProbeRay>(this, kPullUpFeeler), &ledge);
        At<bool>(this, kPullUpHit) = true;
        VectorAt(this, kHitNormal) = At<Engine3D::CProbeRay>(this, kPullUpFeeler).m_surfaceDir;
    } else {
        SetAnimDone(this, MemberCodeAddress(&GameObjects::CFootMovement::EndLadderOffTopToAir));
    }
    At<float>(this, kStartHeightOffset) = At<float>(this, kHeightOffset);
    At<float>(this, kEndHeightOffset) = 0.0f;
    SetTransitionFacing(&facing, 1.0f);
    At<int>(this, kAnimWaiting) = 1;
    VectorAt(this, kTargetFacing) = facing;
    At<float>(this, kTargetHeight) = target.y;
    ChangePhase(4);
    At<uint32_t>(this, kAnim) = kAnimLadderOffTop;
}

// 0x00441DE0
bool GameObjects::CFootMovement::ClimbOntoBottom(bool button)
{
    if (At<bool>(At<uint8_t*>(PhysicsOf(this), 0x1528), 0x70))
        return false;
    float radius = button ? Param(kButtonHorizSnapDistance) : Param(kMaxHorizSnapDistance);
    float height = Param(kMaxVertSnapDistance);
    FoundLadder ladder;
    if (!LevelRails()->FindLadder(&VectorAt(this, kPos), radius, height, true, this, reinterpret_cast<uint32_t>(&ladder),
                                  &At<CGrindPoint*>(this, kRailStart)))
        return false;
    GrabbedLadder(this);
    VectorAt(this, kStartPos) = VectorAt(this, kPos);
    VectorAt(this, kEndPos) = ladder.point;
    SetAnimDone(this, MemberCodeAddress(&GameObjects::CFootMovement::EndOntoLadderBottom));
    At<float>(this, kStartHeightOffset) = At<float>(this, kHeightOffset);
    At<float>(this, kEndHeightOffset) = 0.0f;
    SetTransitionFacing(&ladder.facing, 1.0f);
    At<int>(this, kAnimWaiting) = 1;
    VectorAt(this, kTargetFacing) = ladder.facing;
    At<float>(this, kTargetHeight) = ladder.point.y;
    ChangePhase(4);
    At<uint32_t>(this, kAnim) = kAnimLadderOntoBottom;
    return true;
}

// 0x00441FC0
bool GameObjects::CFootMovement::ClimbOntoTop()
{
    if (At<bool>(At<uint8_t*>(PhysicsOf(this), 0x1528), 0x70))
        return false;
    float height = Param(kMaxVertSnapDistance);
    float radius = Param(kMaxHorizSnapDistance);
    FoundLadder ladder;
    if (!LevelRails()->FindLadder(&VectorAt(this, kPos), radius, height, false, this, reinterpret_cast<uint32_t>(&ladder),
                                  &At<CGrindPoint*>(this, kRailStart)))
        return false;
    GrabbedLadder(this);
    VectorAt(this, kStartPos) = VectorAt(this, kPos);
    VectorAt(this, kEndPos) = ladder.point;
    SetAnimDone(this, MemberCodeAddress(&GameObjects::CFootMovement::EndOntoLadderTop));
    At<float>(this, kStartHeightOffset) = At<float>(this, kHeightOffset);
    At<float>(this, kEndHeightOffset) = 0.0f;
    SetTransitionFacing(&ladder.facing, 1.0f);
    At<int>(this, kAnimWaiting) = 1;
    VectorAt(this, kTargetFacing) = ladder.facing;
    At<float>(this, kTargetHeight) = VectorAt(this, kPos).y;
    ChangePhase(4);
    At<uint32_t>(this, kAnim) = kAnimLadderOntoTop;
    return true;
}

// 0x00442170
bool GameObjects::CFootMovement::GrabLadder(const Maths::Vec4* start, const Maths::Vec4* end)
{
    FoundLadder ladder;
    if (!LevelRails()->FindLadderOnLine(start, end, this, reinterpret_cast<Record24*>(&ladder),
                                        &At<CGrindPoint*>(this, kRailStart)))
        return false;
    RailsOf(this) = LevelRails();
    LetGoOfRide(At<void*>(this, kContact));
    VectorAt(this, kPos) = ladder.point;
    VectorAt(this, kFacing) = ladder.facing;
    At<float>(this, kAlong) = ladder.along;
    At<float>(this, kVerticalSpeed) = 0.0f;
    VectorAt(this, kVelocity) = {0.0f, 0.0f, 0.0f, 1.0f};
    ChangePhase(3);
    TellPhysicsNode(this);
    At<uint32_t>(this, kAnim) = kAnimLadder;
    return true;
}

// 0x00442280
void GameObjects::CFootMovement::ClimbLadder()
{
    // the control's up / down part (its angle's cosine) at ladder_move_speed
    float speed = Param(kLadderMoveSpeed);
    uint8_t* input = At<uint8_t*>(this, kInput);
    double moveSpeed =
        X87Cos(At<float>(input, 0x248)) * At<float>(this, kControlMagnitude) * static_cast<double>(speed);
    At<float>(this, kAnimSpeed) = static_cast<float>(moveSpeed);
    float move = static_cast<float>(moveSpeed * At<float>(this, kFrameLength));
    CGrindPoint* node = At<CGrindPoint*>(this, kRailStart);
    CGrindPoint* other = LadderOtherEnd(node);
    Maths::Vec4 a, b;
    Maths::Vec4* nodePos = RailsOf(this)->GetAbsolutePos(&a, node);
    Maths::Vec4* otherPos = RailsOf(this)->GetAbsolutePos(&b, other);
    Maths::Vec4 d = {otherPos->x - nodePos->x, otherPos->y - nodePos->y, otherPos->z - nodePos->z,
                     otherPos->w - nodePos->w};
    double length = std::sqrt(static_cast<double>(d.z) * d.z + static_cast<double>(d.y) * d.y +
                              static_cast<double>(d.x) * d.x);
    double step = move / length;
    bool up = step > 0.0;
    float& along = At<float>(this, kAlong);
    along = static_cast<float>(step + along);
    float lengthF = static_cast<float>(length);
    // off the bottom or the top
    float bottomUp = Param(kLadderBottomOffsetUp);
    double inverse = 1.0 / lengthF;
    float inverseF = static_cast<float>(inverse);
    if (inverse * bottomUp > along) {
        ClimbOffBottom();
        return;
    }
    float topUp = Param(kLadderTopOffsetUp);
    if ((static_cast<double>(lengthF) - topUp) * inverseF < along) {
        ClimbOffTop();
        return;
    }
    // else on it: the animation, the place along it (ladder_climb_offset out of it)
    float t = along;
    along = t > 1.0f ? 1.0f : t < 0.0f ? 0.0f : t;
    if (along == 0.0f || along == 1.0f) {
        At<uint32_t>(this, kAnim) = kAnimLadder;
        At<float>(this, kAnimSpeed) = 0.0f;
    } else {
        At<uint32_t>(this, kAnim) = up ? kAnimLadderMoveUp : kAnimLadderMoveDown;
    }
    float climb = Param(kLadderClimbOffset);
    const Maths::Vec4& facing = VectorAt(this, kFacing);
    Maths::Vec4 out = {climb * facing.x, climb * facing.y, climb * facing.z, climb * facing.w};
    Maths::Vec4 part = {d.x * along, d.y * along, d.z * along, d.w * along};
    nodePos = RailsOf(this)->GetAbsolutePos(&a, At<CGrindPoint*>(this, kRailStart));
    double x = static_cast<double>(part.x) + nodePos->x;
    float y = part.y + nodePos->y;
    float z = part.z + nodePos->z;
    float w = part.w + nodePos->w;
    Maths::Vec4& pos = VectorAt(this, kPos);
    pos.z = z - out.z;
    pos.w = w - out.w;
    pos.x = static_cast<float>(x - out.x);
    pos.y = y - out.y;
    At<float>(this, kAnimSpeed) = std::fabs(At<float>(this, kAnimSpeed));
}

// 0x004425B0
void GameObjects::CFootMovement::UpdateLadder()
{
    // the ladder's rail script for the first 25 ticks
    if (static_cast<uint32_t>(Now()) - At<uint32_t>(this, kStateTime) < 25)
        TriggerRailScript(this, g_ontoLadderEvents);
    At<bool>(this, kFlag1958) = false;
    // the control within ladder_control_tolerance of up or down: climbing
    uint8_t* input = At<uint8_t*>(this, kInput);
    float upright = static_cast<float>(1.5707963705062866 - std::fabs(At<float>(input, 0x248)));
    float tolerance = Param(kLadderControlTolerance);
    if (X87Cos(static_cast<double>(tolerance) * 0.017453292384743690) < std::fabs(upright)) {
        ClimbLadder();
        return;
    }
    At<uint32_t>(this, kAnim) = kAnimLadder;
    At<float>(this, kAnimSpeed) = 0.0f;
}

// 0x0043EB80
void Walk::MovableContact::StartRiding(GameObjects::CGameEntity* object)
{
    LetGoOfRide(this);
    void* memory = MemSys::Alloc(sizeof(GameObjects::CRideTracker));
    At<GameObjects::CRideTracker*>(this, 0x18) = memory ? static_cast<GameObjects::CRideTracker*>(memory)->MakeNew(object) : nullptr;
}

namespace GameObjects {

// 0x0043D930
CObjectModule* __cdecl CreateWaitAndDieComponent()
{
    void* memory = MemSys::AllocZeroed(0x28);
    if (!memory)
        return nullptr;
    auto* component = static_cast<CObjectModule*>(memory);
    component->MakeNew();
    *reinterpret_cast<void***>(component) = g_waitAndDieVtable; // its vtable
    reinterpret_cast<uint32_t*>(component)[1] = 0x03AC8065; // its type
    return component;
}

} // namespace Obj
