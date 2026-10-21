// The Walk component's update and moves (Obj::CWalkComponent, 0x00529F80-0x0052D6A0): see walk_physics.h. From the
// disassembly: the x87 arithmetic is followed where it keeps values unrounded (doubles here) and where it stores
// them (floats).

#include "game/skater/walk_physics.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/geometry.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/net/gamenet_manager.h"
#include "game/nx/coll_cache.h"
#include "game/nx/coll_line.h"
#include "game/nx/feeler.h"
#include "game/obj/anim_blender.h"
#include "game/obj/control_pad.h"
#include "game/obj/object.h"
#include "game/obj/ride_tracker.h"
#include "game/obj/smtptr.h"
#include "game/script/struct.h"
#include "game/sys/game_clock.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

template <typename T>
T& At(const void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(object) + offset);
}
Mth::Vector& VectorAt(const void* object, uint32_t offset) { return At<Mth::Vector>(object, offset); }
template <typename R = void, typename... A>
R Call(uint32_t address, const void* object, A... args)
{
    return GameFunction<R(__thiscall*)(const void*, A...)>(address)(object, args...);
}

// The component's fields (offsets).
constexpr uint32_t kState = 0x18;              // 0 ground, 1 air, 2 hang, 3 ladder, 4 hang move
constexpr uint32_t kStateTime = 0x1C;          // when the state last changed
constexpr uint32_t kPreviousState = 0x20;
constexpr uint32_t kPosition = 0x24;
constexpr uint32_t kVelocity = 0x34;           // (horizontal; +0x44 the vertical speed)
constexpr uint32_t kVerticalSpeed = 0x44;
constexpr uint32_t kFacing = 0x48;
constexpr uint32_t kOldPosition = 0x68;
constexpr uint32_t kAnimState = 0x78;          // the state's animation checksum (Stand, Land, Jump, HANG, ...)
constexpr uint32_t kPreviousAnimState = 0x7C;
constexpr uint32_t kFrameLength = 0x80;        // seconds
constexpr uint32_t kControlDirection = 0x84;
constexpr uint32_t kControlMagnitude = 0x94;
constexpr uint32_t kContacts = 0xA0;           // 8 contacts, 0xCC each
constexpr uint32_t kAnimSpeedMode = 0x700;
constexpr uint32_t kSpeed = 0x704;
constexpr uint32_t kCurbOffset = 0x714;        // the step smoothing's height
constexpr uint32_t kCollCache = 0x790;
constexpr uint32_t kWalkCamera = 0x1760;
constexpr uint32_t kInput = 0x1770;
constexpr uint32_t kAnimation = 0x1774;
constexpr uint32_t kComponents = 0x1770;       // the eight components found by Finalize (+0x1770-+0x178C)
constexpr uint32_t kRide = 0x1784;             // the ride component: +0x18 its tracker (CRideTracker)
constexpr uint32_t kRunSpeed = 0x17B0;         // the run animation's natural speed
constexpr uint32_t kTriggerFeeler = 0x17B4;
constexpr uint32_t kHeading = 0x1878;
constexpr uint32_t kCarried = 0x1888;          // the velocity of what it stood on
constexpr uint32_t kExtraVelocity = 0x189C;
constexpr uint32_t kSentLadderSpeed = 0x18E0;
constexpr uint32_t kHangMoveFacing = 0x191C;
constexpr uint32_t kHangMoving = 0x1944;
constexpr uint32_t kFlag1958 = 0x1958;
constexpr uint32_t kJumpHeld = 0x9A;           // (a byte: cleared once the input's +0x100 is)
constexpr uint32_t kTargetHeight = 0x9C;       // (the animation wait's height, eased from +0x1914 to +0x1918)
constexpr uint32_t kCurbSnapped = 0x718;       // (a byte: the step smoothing held this frame)
constexpr uint32_t kRideOffset = 0x71C;        // (the point checked against moving objects, from the position)
constexpr uint32_t kTrigger = 0x1780;          // (a component: +0x24 animation speed shown, +0x26 the walk halted)
constexpr uint32_t kSkater = 0x178C;           // the skater component
constexpr uint32_t kBlocked = 0x186C;          // (a byte: walking into something - see UpdateContacts)
constexpr uint32_t kSkidTime = 0x1870;
constexpr uint32_t kExtraVelocityTop = 0x18AC; // the extra velocity's lost above this height
constexpr uint32_t kWaitFrom = 0x18E4;         // the animation wait: from, to (vectors), the target height's ends,
constexpr uint32_t kWaitTo = 0x1904;           // the facing, its length (in the animation's time) and the turn
constexpr uint32_t kWaitHeightFrom = 0x1914;   // over it, the offset carried by what it rides on
constexpr uint32_t kWaitHeightTo = 0x1918;
constexpr uint32_t kWaitTurnTime = 0x192C;
constexpr uint32_t kWaitTurn = 0x1930;
constexpr uint32_t kWaitCarried = 0x1934;
constexpr uint32_t kOffVert = 0x9B;            // (a byte: began in the air off a vert ramp)
constexpr uint32_t kVertTurnSpeed = 0x72C;     // (a turn under way when it began: non-zero, the angle, the axis)
constexpr uint32_t kVertTurn = 0x730;
constexpr uint32_t kVertTurnAxis = 0x734;
constexpr uint32_t kTriggerComponent = 0x177C; // the object's trigger component (sets off the ground's triggers)
constexpr uint32_t kTerrainComponent = 0x1788; // (a component told the terrain under it)
constexpr uint32_t kGroundNormal = 0x1794;
constexpr uint32_t kHasGround = 0x1864;        // (a byte: the trigger feeler holds the ground stood on)
constexpr uint32_t kObjectUp = 0x7C;           // the object's matrix: the second row
constexpr uint32_t kObjectForward = 0x8C;      // ... the third
constexpr uint32_t kBarrierJumpAllowed = 0x98; // (a byte checked before a barrier jump)
constexpr uint32_t kBarrierTimed = 0x17A8;     // (a byte: walking into a barrier since +0x17AC)
constexpr uint32_t kBarrierSince = 0x17AC;
constexpr uint32_t kHangJump = 0x18B0;         // (a byte: jumped off a hang - from the plane through +0x18C4 along
constexpr uint32_t kHangJumpFacing = 0x18B4;   //  +0x18B4; +0x18C8 the height over +0x18D0)
constexpr uint32_t kHangJumpPlane = 0x18C4;
constexpr uint32_t kHangJumpHeight = 0x18C8;
constexpr uint32_t kHangHeight = 0x18D0;

// Animation states.
constexpr uint32_t kAnimWait = 0x4FE6069C;       // ANIMWAIT
constexpr uint32_t kHangMoveRight = 0x279B1F0B;  // HangMoveRight
constexpr uint32_t kHangMoveLeft = 0x2D9815C3;   // HangMoveLeft
constexpr uint32_t kHang = 0x4194ECCA;           // HANG
constexpr uint32_t kLadderMoveUp = 0xAF5ABC82;   // LadderMoveUp
constexpr uint32_t kLadder = 0xC84243DA;         // Ladder
constexpr uint32_t kLadderMoveDown = 0xFEC9DDED; // LadderMoveDown
constexpr uint32_t kSkid = 0x1D537EFF;           // Skid
constexpr uint32_t kStand = 0x9B46E749;          // Stand
constexpr uint32_t kLand = 0x57FF2A27;           // Land
constexpr uint32_t kWalkOffEdge = 0xABF1F6AC;    // WalkOffEdge

// Names.
constexpr uint32_t kWalkParameters = 0x6775E538;   // walk_parameters
constexpr uint32_t kHangMoveAnimSpeed = 0x1A47FFAB; // hang_move_animation_speed
constexpr uint32_t kLadderMoveSpeed = 0x0AB2DB54;  // ladder_move_speed
constexpr uint32_t kSpeedName = 0xF0D90109;        // speed
constexpr uint32_t kSetAnimSpeed = 0xBD4EDD44;     // (the animation component's member function given the speed)
constexpr uint32_t kAir = 0x439F4704;              // AIR (an event; also the air's animation state)
constexpr uint32_t kPeggedDurationForSkid = 0x8E1CF27A; // pegged_duration_for_skid
constexpr uint32_t kCurbFloatLerpDownRate = 0x9B3388FA; // curb_float_lerp_down_rate
constexpr uint32_t kRehangDelay = 0xE6E0C0A4;      // rehang_delay (a skater physics value)
constexpr uint32_t kStickyLandThresholdSpeed = 0x530DCF34; // sticky_land_threshold_speed (the same)
constexpr uint32_t kSnapUpHeight = 0xCEE3A3E1;     // snap_up_height
constexpr uint32_t kSnapDownHeight = 0xAF3E4251;   // snap_down_height
constexpr uint32_t kMaxUnnoticedGroundSnap = 0xD3193D8E; // max_unnoticed_ground_snap
constexpr uint32_t kInitialVertVelBoost = 0x17B37748; // initial_vert_vel_boost
constexpr uint32_t kSteppedUp = 0x93FCF3ED;        // (events for a big step up / down; names not recovered)
constexpr uint32_t kSteppedDown = 0x56E21153;
constexpr uint32_t kSkaterOffEdge = 0xD96F01F1;    // SkaterOffEdge (an event)
constexpr uint32_t kHoldTimeForMaxJump = 0x12333EBD; // hold_time_for_max_jump
constexpr uint32_t kMinJumpFactor = 0x246D0BF3;    // min_jump_factor
constexpr uint32_t kHangJumpFactor = 0xF2FA5845;   // hang_jump_factor
constexpr uint32_t kJumpVelocity = 0x63D62A21;     // jump_velocity
constexpr uint32_t kJumpHorizSpeed = 0x839FE542;   // jump_horiz_speed (a skater physics value)
constexpr uint32_t kBarrierJumpHighestBarrier = 0xDA85F5AE; // barrier_jump_highest_barrier
constexpr uint32_t kBarrierJumpMaxAngle = 0x78E6A5EC; // barrier_jump_max_angle
constexpr uint32_t kBarrierJumpMinClearance = 0x72660978; // barrier_jump_min_clearance
constexpr uint32_t kBarrierJumpDelay = 0x38D36700; // barrier_jump_delay
constexpr uint32_t kFeelerLength = 0x99978D2B;     // feeler_length
constexpr uint32_t kFeelerHeight = 0x6DA7F696;     // feeler_height
constexpr uint32_t kPushFeelerLength = 0xA20C43B7; // push_feeler_length
constexpr uint32_t kPushStrength = 0x4D16F37D;     // push_strength
constexpr uint32_t kMinAnimRunSpeed = 0x0BD6A05D;  // min_anim_run_speed
constexpr uint32_t kSkaterJump = 0x8687163A;       // SkaterJump (an event)
constexpr uint32_t kJump = 0x584CF9E9;             // Jump (the animation state)
constexpr uint32_t kJumpRequested = 0x6B9CA247;    // JumpRequested (an event handler type)

constexpr uint16_t kFaceWall = 0x2;        // a face flag: not stood on
constexpr float kWallGap = 0.1f;           // 0x00645D74
constexpr float kSlideGap = 0.05f;         // 0x00645D6C
constexpr float kBigStepDown = -3.0f;      // 0x0064C798
constexpr float kNudge = 0.001f;           // 0x006461D8
constexpr double kMinHeadingLength = 0.0010000000474974513; // (0.001f)
constexpr double kSeventhTurn = 0.8975979089736938;       // 0x00646968 (2 pi / 7, as a float)
constexpr double kDegrees = 0.01745329238474369;          // 0x00645464 (pi / 180, as a float)
constexpr double kNinth = 0.1111111119389534;             // 0x0064C79C (1 / 9, as a float)
constexpr double kSteepGround = 0.5;                      // 0x006453BC
constexpr double kFloorBelow = 0.7070000171661377;        // 0x006467BC
constexpr double kMovingSqr = 0.009999999776482582;       // 0x0064544C (0.01f)
constexpr double kStillLength = 0.0001;                   // 0x006484A0
constexpr double kStillSqr = 1e-08;                       // 0x0064C7A0
constexpr double kMostlyStopped = 0.25;                   // 0x006467AC

constexpr float kCameraRate = 0.05f;       // 0x3D4CCCCD
constexpr double kHangTurn = 0.949999988079071; // 0x0064C790 (radians)
constexpr float kHangTurnBack = -0.95f;    // 0xBF733333

// walk_parameters' float `name` (asserted).
float WalkParameter(uint32_t name)
{
    float value;
    Script::GetStructure(kWalkParameters)->GetFloat(name, &value, 1);
    return value;
}

// The game clock as the skater code reads it (0x0044ECE0 plus the offset at 0x00696D60).
int Now() { return Tmr::GameClockElapsed() + GameVariable<int>(0x00696D60); }

// The walk halted (the trigger component's +0x26: what it does this frame is over).
bool Halted(const void* walk) { return At<bool>(At<void*>(walk, kTrigger), 0x26); }

// Nx's current collision cache (0x00691974), cleared after each state's update.
void ClearCurrentCache() { GameVariable<void*>(0x00691974) = nullptr; }

// The ride component's moving object containing the segment from `from` to `to` (0x00427B30), or null.
CCompositeObject* MovingObjectAround(void* ride, Mth::Vector* to, Mth::Vector* from)
{
    return Call<CCompositeObject*>(0x00427B30, ride, to, from);
}

// The debug line the original prints when it finds itself inside a moving object.
void ReportInside(int line, const char* where)
{
    Dbg::Printf("M:%s:%i: %s\n", "mponents\\WalkComponent.cpp", line, where);
}

// A local feeler going away: the object hit let go of (the original's inlined smart pointer destructor).
void ReleaseHit(Nx::CFeeler* feeler)
{
    if (CCompositeObject* hit = feeler->m_object.Get())
        reinterpret_cast<CRefCounted*>(hit)->RemoveSmartPointer(
            reinterpret_cast<CSmtPtr<CRefCounted>*>(&feeler->m_object));
}

// `position` put `gap` off the feeler's hit along its normal (x, y and z's offsets rounded first, w's not).
void PutOff(Mth::Vector* position, const Nx::CFeeler* feeler, float gap)
{
    const Mth::Vector& point = feeler->m_point;
    const Mth::Vector& normal = feeler->m_normal;
    double scale = gap;
    position->x = point.x + static_cast<float>(normal.x * scale);
    position->y = point.y + static_cast<float>(normal.y * scale);
    position->z = point.z + static_cast<float>(normal.z * scale);
    position->w = static_cast<float>(normal.w * scale + point.w);
}

// The skater component's state flags (its +0x1528).
const uint8_t* SkaterFlags(const void* walk) { return At<uint8_t*>(At<void*>(walk, kSkater), 0x1528); }

// One of the eight contacts (+0xA0, 0xCC each): what feeler 0-6 (around it, a seventh of a turn apart from the facing)
// or 7 (along the fall) found.
struct Contact {
    bool hit;
    uint8_t unknown1[3];
    Mth::Vector normal;  // +0x04 the hit's, flattened and made unit length
    bool clear;          // +0x14 (a barrier jump: nothing in the way at the highest barrier's height)
    uint8_t unknown15[3];
    float objectSpeed;   // +0x18 how fast the object hit moves along the normal
    Nx::CFeeler feeler;  // +0x1C
};
static_assert(offsetof(Contact, normal) == 0x04 && offsetof(Contact, clear) == 0x14 &&
                  offsetof(Contact, objectSpeed) == 0x18 && offsetof(Contact, feeler) == 0x1C && sizeof(Contact) == 0xCC,
              "layout must match the original");
Contact& ContactAt(const void* walk, int index) { return At<Contact>(walk, kContacts + 0xCC * index); }

// The x87's FSIN / FCOS (the C runtime's differ in the last bits).
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

// Where it hangs along the hang's facing (+0x18B4): (the position plus the ride check's offset) . that facing.
float HangPlane(const void* walk)
{
    const Mth::Vector& position = VectorAt(walk, kPosition);
    const Mth::Vector& offset = VectorAt(walk, kRideOffset);
    const Mth::Vector& facing = VectorAt(walk, kHangJumpFacing);
    double x = static_cast<double>(position.x) + offset.x;
    double y = static_cast<double>(position.y) + offset.y;
    double z = static_cast<double>(position.z) + offset.z;
    return static_cast<float>((z * facing.z + y * facing.y) + x * facing.x);
}

// Stopped by what it walks into: no velocity, the run animation's speed down to min_anim_run_speed, blocked.
void Stop(void* walk)
{
    VectorAt(walk, kVelocity) = {0.0f, 0.0f, 0.0f, 1.0f};
    float minimum = WalkParameter(kMinAnimRunSpeed);
    float& speed = At<float>(walk, kSpeed);
    speed = minimum < speed ? minimum : speed;
    At<bool>(walk, kBlocked) = true;
}

// a + b in the x87's double precision, stored as a float.
float Add(float a, float b) { return static_cast<float>(static_cast<double>(a) + b); }

// The walk camera's override (0x00525F70: SetOverrideFacing - the walk camera component, not rewritten here).
void SetCameraOverride(void* camera, const Mth::Vector* facing, float rate)
{
    Call(0x00525F70, camera, facing, rate);
}

} // namespace

// 0x0052A5C0
CBaseComponent* __cdecl CWalkComponent::Create()
{
    void* memory = Mem::Calloc(0x1960);
    return memory ? static_cast<CWalkComponent*>(memory)->Construct() : nullptr;
}

// 0x0052A190
CWalkComponent* CWalkComponent::Construct()
{
    CBaseComponent::Construct();
    m_vtable = reinterpret_cast<void**>(0x0064C75C);
    for (uint32_t i = 0; i < 8; ++i)
        Call(0x00528880, &At<uint8_t>(this, kContacts + 0xCC * i)); // (each contact's constructor)
    reinterpret_cast<Nx::CCollCache*>(&At<uint8_t>(this, kCollCache))->Construct();
    reinterpret_cast<Nx::CFeeler*>(&At<uint8_t>(this, kTriggerFeeler))->Construct();
    for (uint32_t offset = kComponents; offset <= 0x178C; offset += 4)
        At<void*>(this, offset) = nullptr;
    m_type = 0x726E85AA; // Walk
    VectorAt(this, kControlDirection) = {0.0f, 0.0f, 0.0f, 1.0f};
    VectorAt(this, kExtraVelocity) = {0.0f, 0.0f, 0.0f, 1.0f};
    At<float>(this, 0x72C) = 0.0f;
    At<bool>(this, kFlag1958) = false;
    return this;
}

// 0x0052A290
CWalkComponent* CWalkComponent::Destroy(unsigned int flags)
{
    Call(0x005288B0, this); // the destructor body
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00529F80
void CWalkComponent::UpdateCameraOverride()
{
    void* camera = At<void*>(this, kWalkCamera);
    const Mth::Vector& facing = VectorAt(this, kFacing);
    switch (At<uint32_t>(this, kAnimState)) {
    case kAnimWait: {
        At<bool>(camera, 0x78) = true;
        // (while hang-moving, the hang move's facing)
        VectorAt(camera, 0x7C) = At<int>(this, kHangMoving) ? VectorAt(this, kHangMoveFacing) : facing;
        At<float>(camera, 0x8C) = kCameraRate;
        return;
    }
    case kHangMoveRight: {
        // The facing turned 0.95 radians about y (sin / cos stored as floats, the sums rounded once).
        auto sine = static_cast<float>(X87Sin(kHangTurn));
        auto cosine = static_cast<float>(X87Cos(kHangTurn));
        At<bool>(camera, 0x78) = true;
        Mth::Vector& target = VectorAt(camera, 0x7C);
        float x = facing.x;
        float z = facing.z;
        target.x = static_cast<float>(static_cast<double>(z) * sine + static_cast<double>(cosine) * x);
        target.y = facing.y;
        target.z = static_cast<float>(-(static_cast<double>(x) * sine) + static_cast<double>(z) * cosine);
        target.w = facing.w;
        At<float>(camera, 0x8C) = kCameraRate;
        return;
    }
    case kHangMoveLeft: {
        Mth::Vector turned = facing;
        GameFunction<Mth::Vector*(__thiscall*)(Mth::Vector*, float)>(0x00420A90)(&turned, kHangTurnBack); // about y
        SetCameraOverride(camera, &turned, kCameraRate);
        return;
    }
    case kHang:
        SetCameraOverride(camera, &facing, kCameraRate);
        return;
    case kLadderMoveUp:
    case kLadder:
    case kLadderMoveDown:
        At<bool>(camera, 0x78) = true;
        VectorAt(camera, 0x7C) = facing;
        At<float>(camera, 0x8C) = kCameraRate;
        return;
    default:
        At<bool>(camera, 0x78) = false;
        return;
    }
}

// 0x0052A2C0
void CWalkComponent::UpdateAnimSpeed()
{
    void* animation = At<void*>(this, kAnimation);
    // 0x00419B90: the animation's first channel's speed (and whether to send it over the network).
    auto setSpeed = [animation](float speed, int send) {
        GameFunction<void(__thiscall*)(void*, float, int, int, int)>(0x00419B90)(animation, speed, send, 0, 0);
    };
    // The animation component's member function 0xBD4EDD44 with speed = `speed`.
    auto tell = [animation](float speed) {
        Script::CStruct* params = Script::NewStruct();
        params = params ? params->Init() : nullptr;
        params->AddFloat(kSpeedName, speed);
        reinterpret_cast<CBaseComponent*>(animation)->CallMemberFunction(kSetAnimSpeed, params, nullptr);
        if (params) {
            params->Clear();
            Script::DeleteStruct(params);
        }
    };
    switch (At<int>(this, kAnimSpeedMode)) {
    case 1: { // running
        if (!(At<float>(this, kRunSpeed) > 0.0f))
            return;
        setSpeed(At<float>(this, kSpeed) / At<float>(this, kRunSpeed), 0);
        if (!At<bool>(At<void*>(this, 0x1780), 0x24))
            return;
        tell(At<float>(this, kSpeed) / At<float>(this, kRunSpeed));
        return;
    }
    case 2: { // hanging
        float speed = At<float>(this, kSpeed) / WalkParameter(kHangMoveAnimSpeed);
        tell(speed);
        setSpeed(speed, 0);
        return;
    }
    case 3: { // on a ladder: also sent when it changed by 0.1 or more
        float speed = At<float>(this, kSpeed) / WalkParameter(kLadderMoveSpeed);
        tell(speed);
        auto* net = GameVariable<GameNet::Manager*>(0x007CCE14);
        if (net->InNetGame() &&
            !(std::fabs(static_cast<double>(speed) - At<float>(this, kSentLadderSpeed)) < 0.10000000149011612)) {
            At<float>(this, kSentLadderSpeed) = speed;
            setSpeed(speed, 1);
            return;
        }
        setSpeed(speed, 0);
        return;
    }
    default:
        return;
    }
}

// 0x0052A620
void CWalkComponent::UpdateRide()
{
    void* ride = At<void*>(this, kRide);
    auto* tracker = At<CRideTracker*>(ride, 0x18);
    if (!tracker)
        return;
    Mth::Vector& position = VectorAt(this, kPosition);
    if (!tracker->m_object.Get() || !tracker->Update(&position)) {
        Call(0x00427C70, ride); // (what it stood on is gone)
        return;
    }
    tracker = At<CRideTracker*>(ride, 0x18);
    position.x = Add(tracker->m_carried.x, position.x);
    position.y = Add(tracker->m_carried.y, position.y);
    position.z = Add(tracker->m_carried.z, position.z);
    position.w = Add(tracker->m_carried.w, position.w);
    if (!At<CRideTracker*>(ride, 0x18)->m_turned)
        return;
    Mth::Vector& facing = VectorAt(this, kFacing);
    Mth::Vector turned;
    facing = At<CRideTracker*>(ride, 0x18)->m_turn.RotateVector(turned, facing);
    if (!(facing.y == 0.0f)) { // kept level
        facing.y = 0.0f;
        facing.Normalize();
    }
}

// 0x0052AB90
void CWalkComponent::LeaveRide(Mth::Vector* velocity, float* vertical)
{
    void* ride = At<void*>(this, kRide);
    auto* tracker = At<CRideTracker*>(ride, 0x18);
    Mth::Vector& carried = VectorAt(this, kCarried);
    if (tracker && tracker->m_object.Get()) {
        carried = VectorAt(tracker->m_object.Get(), 0xAC); // its velocity
        double heading = static_cast<double>(carried.z) * velocity->z + static_cast<double>(carried.y) * velocity->y +
                         static_cast<double>(carried.x) * velocity->x;
        if (heading > 0.0) {
            // moved on by it for the frame (x's product unrounded; y, z, w's stored first)
            double frame = At<float>(this, kFrameLength);
            auto y = static_cast<float>(frame * carried.y);
            auto z = static_cast<float>(frame * carried.z);
            auto w = static_cast<float>(frame * carried.w);
            Mth::Vector& position = VectorAt(this, kPosition);
            position.x = static_cast<float>(frame * carried.x + position.x);
            position.y = Add(y, position.y);
            position.z = Add(z, position.z);
            position.w = Add(w, position.w);
        }
        *vertical = Add(carried.y, *vertical);
        carried.y = 0.0f;
        velocity->x = Add(carried.x, velocity->x);
        velocity->y = Add(carried.y, velocity->y);
        velocity->z = Add(carried.z, velocity->z);
        velocity->w = Add(carried.w, velocity->w);
    } else {
        if (tracker)
            Call(0x00427C70, ride); // (what it stood on is gone)
        carried = {0.0f, 0.0f, 0.0f, 1.0f};
    }
    // The ride let go.
    ride = At<void*>(this, kRide);
    tracker = At<CRideTracker*>(ride, 0x18);
    if (!tracker)
        return;
    At<CCompositeObject*>(ride, 0x6C) = tracker->m_object.Get();
    reinterpret_cast<CSmtPtr<CRefCounted>*>(&tracker->m_object)->Release();
    Mem::Manager::Get()->Free(tracker);
    At<CRideTracker*>(ride, 0x18) = nullptr;
    At<int>(ride, 0x70) = Now();
}

// 0x0052B2D0
void CWalkComponent::GoToAir()
{
    Call(0x005260E0, this, 1); // (the state: in the air)
    Mth::Vector& heading = VectorAt(this, kHeading);
    heading = m_object->m_vel;
    heading.y = 0.0f;
    heading.Normalize();
    LeaveRide(&m_object->m_vel, &m_object->m_vel.y);
    Mth::Vector& carried = VectorAt(this, kCarried);
    carried = m_object->m_vel;
    carried.y = 0.0f;
}

// 0x0052A180
int CWalkComponent::AnimWaitComplete(Script::CStruct*, Script::CScript*)
{
    Call(0x005298D0, this);
    return 1;
}

// 0x0052C280
int CWalkComponent::ReleaseHang(Script::CStruct*, Script::CScript*)
{
    Jump();
    return 1;
}

// 0x0052C290
int CWalkComponent::ForceToAirFromHang(Script::CStruct*, Script::CScript*)
{
    GoToAir();
    m_object->SelfEvent(kAir, nullptr);
    return 1;
}

// 0x0052C2B0
void CWalkComponent::GoToAirFor(void*) { GoToAir(); }

// 0x0052D4B0
void CWalkComponent::Update()
{
    if (At<bool>(this, kJumpHeld) && !At<bool>(At<void*>(this, kInput), 0x100))
        At<bool>(this, kJumpHeld) = false;
    uint32_t previousAnim = At<uint32_t>(this, kAnimState);
    At<uint32_t>(this, kPreviousAnimState) = previousAnim;
    At<uint32_t>(this, kAnimState) = 0;
    Call(0x00527F20, this);
    Call(0x00527DB0, this);
    Mth::Vector& position = VectorAt(this, kPosition);
    VectorAt(this, kOldPosition) = position;
    At<float>(this, kFrameLength) = static_cast<float>(Tmr::GetSlomoFrameSeconds());
    At<bool>(this, kCurbSnapped) = false;
    position.y -= At<float>(this, kCurbOffset); // (put back below)
    Call(0x00528370, this);

    switch (At<int>(this, kState)) {
    case 0:
        UpdateGround();
        break;
    case 1:
        UpdateAir();
        break;
    case 2:
        Call(0x00440BF0, this); // hanging
        break;
    case 3:
        Call(0x004425B0, this); // on a ladder
        break;
    case 4:
        UpdateAnimWait();
        break;
    default:
        break;
    }
    if (Halted(this))
        return;

    // The step smoothing's height eases down unless held this frame.
    if (!At<bool>(this, kCurbSnapped)) {
        float rate = WalkParameter(kCurbFloatLerpDownRate);
        double offset = At<float>(this, kCurbOffset);
        At<float>(this, kCurbOffset) =
            static_cast<float>(-offset * (static_cast<double>(rate) * At<float>(this, kFrameLength)) + offset);
    }
    position.y += At<float>(this, kCurbOffset);

    if (At<int>(this, kState) == 0 && static_cast<uint32_t>(Now() - At<int>(this, kStateTime)) > 500) {
        Call(0x00527600, this); // (an acid drop)
        if (Halted(this))
            return;
    }
    Call(0x005278D0, this);
    Call(0x00527BE0, this);
    if (At<int>(this, kState) == 0 || At<int>(this, kState) == 1)
        Call(0x00527080, this);
    Call(0x00527420, this);
    Call(0x00529CE0, this);
    Call(0x00529800, this);
    Call(0x00528400, this, previousAnim);
    m_object->SelfEvent(At<uint32_t>(this, kAnimState), nullptr);
    UpdateAnimSpeed();
    UpdateCameraOverride();
    Call(0x005007A0, At<void*>(this, kSkater));
    int& countdown = At<int>(this, 0x710);
    if (countdown > 0 && --countdown == 0)
        At<bool>(this, 0x70C) = false;
}

// 0x0052D300
void CWalkComponent::UpdateGround()
{
    UpdateRide();
    Call(0x005277B0, this);
    Mth::Vector& velocity = VectorAt(this, kVelocity);
    Call(0x00528950, this, &velocity);
    if (At<uint32_t>(this, kAnimState) == kSkid) {
        float pegged = WalkParameter(kPeggedDurationForSkid);
        if (At<float>(this, kSkidTime) < pegged)
            At<uint32_t>(this, kAnimState) = kStand;
    }
    if (UpdateContacts())
        Call(0x00526260, this);
    else
        At<bool>(this, kBlocked) = false;
    At<bool>(this, kFlag1958) = false;
    if (At<int>(this, kState) != 0 || Halted(this)) {
        ClearCurrentCache();
        return;
    }

    // Moved on by the velocity (x's move unrounded; y, z and w's stored first).
    double frame = At<float>(this, kFrameLength);
    Mth::Vector& position = VectorAt(this, kPosition);
    auto y = static_cast<float>(frame * velocity.y);
    auto z = static_cast<float>(frame * velocity.z);
    auto w = static_cast<float>(frame * velocity.w);
    position.x = static_cast<float>(frame * velocity.x + position.x);
    position.y += y;
    position.z += z;
    position.w += w;
    SnapToGround();

    if (At<int>(this, kState) == 0 && !Halted(this)) {
        Call(0x005264F0, this);
        void* ride = At<void*>(this, kRide);
        if (CCompositeObject* inside = MovingObjectAround(ride, &position, &VectorAt(this, kOldPosition))) {
            ReportInside(0x369, "WALKING_GROUND, within moving object");
            // Walk off with its velocity, facing that way when it is fast (over 10).
            velocity = inside->m_vel;
            velocity.y = 0.0f;
            At<float>(this, kVerticalSpeed) = 0.0f;
            double lengthSqr = static_cast<double>(velocity.x) * velocity.x +
                               static_cast<double>(velocity.y) * velocity.y +
                               static_cast<double>(velocity.z) * velocity.z;
            if (lengthSqr > 100.0)
                VectorAt(this, kFacing) = velocity / static_cast<float>(std::sqrt(lengthSqr));
        }
        Call(0x00500820, At<void*>(this, kSkater));
    }
    ClearCurrentCache();
}

// 0x0052CEF0
void CWalkComponent::UpdateAir()
{
    const uint8_t* pad = At<uint8_t*>(this, kInput) + 0x20;
    Call(0x005277B0, this);
    At<uint32_t>(this, kAnimState) = kAir;
    Call(0x00526850, this);
    if (!UpdateContacts())
        At<bool>(this, kBlocked) = false;
    if (Halted(this))
        return;
    Call(0x00527C50, this);
    Call(0x00526BA0, this);
    double gravity = Call<float>(0x005284C0, this);

    // Moved on by the velocity and the extra velocity (each move unrounded), then by the vertical speed under
    // gravity.
    double frame = At<float>(this, kFrameLength);
    Mth::Vector& position = VectorAt(this, kPosition);
    const Mth::Vector& velocity = VectorAt(this, kVelocity);
    for (int i = 0; i < 4; ++i)
        (&position.x)[i] = static_cast<float>(frame * (&velocity.x)[i] + (&position.x)[i]);
    Mth::Vector& extra = VectorAt(this, kExtraVelocity);
    for (int i = 0; i < 4; ++i)
        (&position.x)[i] = static_cast<float>(frame * (&extra.x)[i] + (&position.x)[i]);
    float& vertical = At<float>(this, kVerticalSpeed);
    position.y = static_cast<float>((vertical * frame - frame * frame * gravity * 0.5) + position.y);
    vertical = static_cast<float>(vertical - gravity * frame);
    if (position.y > At<float>(this, kExtraVelocityTop))
        extra = {0.0f, 0.0f, 0.0f, 1.0f};
    Call(0x00500820, At<void*>(this, kSkater));
    Mth::Vector& oldPosition = VectorAt(this, kOldPosition);
    CollideAirMove(&oldPosition, &position);
    if (At<int>(this, kState) != 1 || Halted(this))
        return;

    // Grabbing a ledge or a ladder (not again within rehang_delay of letting go of one).
    if (pad[0xE0] && !At<bool>(this, kJumpHeld)) {
        int previous = At<int>(this, kPreviousState);
        bool grab = true;
        if (previous == 2 || previous == 3 || previous == 4) {
            auto since = static_cast<float>(
                static_cast<double>(GameFunction<uint32_t(__cdecl*)(int)>(0x00420250)(At<int>(this, kStateTime))));
            grab = GameFunction<float(__cdecl*)(uint32_t)>(0x0043DA70)(kRehangDelay) < since;
        }
        if (grab && (GameFunction<bool(__thiscall*)(void*, Mth::Vector, Mth::Vector)>(0x0043F030)(
                         this, oldPosition, position) ||
                     Call<bool>(0x00442170, this, &oldPosition, &position))) {
            ClearCurrentCache();
            return;
        }
    }
    ClearCurrentCache();
    if (pad[0x180] && Call<bool>(0x005299F0, this))
        return;
    if (Call<bool>(0x00527510, this)) // (an acid drop)
        return;
    Call(0x00526E00, this);
    Call(0x005293F0, this);
    if (Halted(this))
        return;

    // Inside a moving object: put back where it was, standing on it (a feeler 30 up and down through the object
    // finds it), stopped.
    Mth::Vector start = position;
    const Mth::Vector& offset = VectorAt(this, kRideOffset);
    Mth::Vector to = position + offset;
    Mth::Vector from = oldPosition + offset;
    if (!MovingObjectAround(At<void*>(this, kRide), &to, &from))
        return;
    position = to - offset;
    ReportInside(0x74B, "WALKING_AIR, within moving object");
    alignas(16) uint8_t storage[sizeof(Nx::CFeeler)];
    auto* feeler = reinterpret_cast<Nx::CFeeler*>(storage)->Construct();
    const Mth::Vector up = {0.0f, 30.0f, 0.0f, 0.0f};
    Mth::Vector top = m_object->m_pos + up;
    feeler->SetStart(&top);
    Mth::Vector bottom = m_object->m_pos - up;
    feeler->SetEnd(&bottom);
    if (feeler->GetCollision(true, false) && feeler->HitObjectAlive())
        Call(0x0043EB80, At<void*>(this, kRide), feeler->GetHitObject());
    VectorAt(this, kVelocity) = {0.0f, 0.0f, 0.0f, 1.0f};
    vertical = 0.0f;
    CollideAirMove(&position, &start);
    GameFunction<void(__thiscall*)(Nx::CFeeler*)>(0x0041C350)(feeler); // (its destructor)
}

// 0x0052AD10
void CWalkComponent::UpdateAnimWait()
{
    // Carried by what it rides on: the offset moved, the facing turned (kept level).
    void* ride = At<void*>(this, kRide);
    if (auto* tracker = At<CRideTracker*>(ride, 0x18)) {
        if (!tracker->m_object.Get() || !tracker->Update(&VectorAt(this, kPosition))) {
            Call(0x00427C70, ride); // (what it stood on is gone)
        } else {
            tracker = At<CRideTracker*>(ride, 0x18);
            Mth::Vector& carried = VectorAt(this, kWaitCarried);
            carried.x += tracker->m_carried.x;
            carried.y += tracker->m_carried.y;
            carried.z += tracker->m_carried.z;
            carried.w += tracker->m_carried.w;
            tracker = At<CRideTracker*>(ride, 0x18);
            if (tracker->m_turned) {
                Mth::Vector& facing = VectorAt(this, kHangMoveFacing);
                Mth::Vector turned;
                facing = tracker->m_turn.RotateVector(turned, facing);
                if (!(facing.y == 0.0f)) {
                    facing.y = 0.0f;
                    facing.Normalize();
                }
            }
        }
    }

    // How far through the animation it is: the position between the two ends (plus what it was carried), the
    // facing turned back from the full turn as the turn's time passes, the height eased.
    float start;
    float current;
    float end;
    reinterpret_cast<CAnimBlenderComponent*>(At<void*>(this, kAnimation))->GetTimes(&start, &current, &end);
    auto t = static_cast<float>((static_cast<double>(current) - start) / (static_cast<double>(end) - start));
    Mth::Vector between;
    GameFunction<Mth::Vector*(__cdecl*)(Mth::Vector*, const Mth::Vector*, const Mth::Vector*, float)>(0x0043D990)(
        &between, &VectorAt(this, kWaitFrom), &VectorAt(this, kWaitTo), t);
    const Mth::Vector& carried = VectorAt(this, kWaitCarried);
    Mth::Vector& position = VectorAt(this, kPosition);
    position.x = between.x + carried.x;
    position.y = between.y + carried.y;
    position.z = between.z + carried.z;
    position.w = between.w + carried.w;
    double turnPart = t / static_cast<double>(At<float>(this, kWaitTurnTime));
    if (turnPart > 1.0)
        turnPart = 1.0;
    double turn = At<float>(this, kWaitTurn);
    double angle = turn * turnPart - turn;
    Mth::Vector& facing = VectorAt(this, kFacing);
    facing = VectorAt(this, kHangMoveFacing);
    double sine = X87Sin(angle);
    double cosine = X87Cos(angle);
    double x = facing.x;
    facing.x = static_cast<float>(cosine * x + sine * facing.z);
    facing.z = static_cast<float>(-(x * sine) + cosine * facing.z);
    At<uint32_t>(this, kAnimState) = kAnimWait;
    double low = At<float>(this, kWaitHeightFrom);
    At<float>(this, kTargetHeight) = static_cast<float>((At<float>(this, kWaitHeightTo) - low) * t + low);
}

// 0x0052A700
void CWalkComponent::CollideAirMove(const Mth::Vector* from, const Mth::Vector* to)
{
    alignas(16) uint8_t storage[sizeof(Nx::CFeeler)];
    auto* feeler = reinterpret_cast<Nx::CFeeler*>(storage)->Construct();
    feeler->m_line.start = *from;
    feeler->m_line.end = *to;
    if (!feeler->GetCollision(true, false)) {
        ReleaseHit(feeler);
        return;
    }
    Mth::Vector& position = VectorAt(this, kPosition);
    if (feeler->m_hit.faceFlags & kFaceWall) {
        // A wall: put 0.1 off it, the rest of the move slid along it - stopped 0.05 off anything that meets.
        PutOff(&position, feeler, kWallGap);
        auto left = static_cast<float>(1.0 - feeler->m_distance);
        const Mth::Line& line = feeler->m_line;
        auto dx = line.end.x - line.start.x;
        auto dy = line.end.y - line.start.y;
        auto dz = line.end.z - line.start.z;
        double dw = static_cast<double>(line.end.w) - line.start.w;
        Mth::Vector rest = {dx * left, dy * left, dz * left, static_cast<float>(dw * left)};
        rest.ProjectToPlane(feeler->m_normal);
        feeler->m_line.start = position;
        feeler->m_line.end = rest + position;
        if (feeler->GetCollision(true, false))
            PutOff(&position, feeler, kSlideGap);
        else
            position = feeler->m_line.end;
        ReleaseHit(feeler);
        return;
    }

    // Landed: on the ground, the ground's trigger kinds set off, the velocity kept only along the facing (relative
    // to what it landed on) - stopped when heading away, or slow (under sticky_land_threshold_speed) without
    // control.
    position = feeler->m_point;
    At<float>(this, kVerticalSpeed) = 0.0f;
    Call(0x005260E0, this, 0);
    Nx::CFeeler* ground = &At<Nx::CFeeler>(this, kTriggerFeeler);
    Call(0x0043A930, ground, feeler); // (the feeler copied)
    At<bool>(this, kHasGround) = true;
    Call(0x00434BF0, At<void*>(this, kTriggerComponent), reinterpret_cast<const void*>(0x0064C648), ground);
    if (!Halted(this)) {
        VectorAt(this, kGroundNormal) = feeler->m_normal;
        void* ride = At<void*>(this, kRide);
        Call(0x00427CD0, ride, feeler); // (what it landed on)
        Mth::Vector& velocity = VectorAt(this, kVelocity);
        ride = At<void*>(this, kRide);
        if (auto* tracker = At<CRideTracker*>(ride, 0x18)) {
            if (!tracker->m_object.Get()) {
                Call(0x00427C70, ride); // (what it stood on is gone)
            } else {
                velocity -= At<CRideTracker*>(ride, 0x18)->m_object.Get()->m_vel;
                velocity.y = 0.0f;
            }
        }
        const Mth::Vector& facing = VectorAt(this, kFacing);
        double along = static_cast<double>(velocity.z) * facing.z + static_cast<double>(velocity.y) * facing.y +
                       static_cast<double>(velocity.x) * facing.x;
        bool stop = true;
        if (along > 0.0) {
            velocity.ProjectOnto(facing);
            stop = false;
            if (At<float>(this, kControlMagnitude) == 0.0f) {
                auto speed = static_cast<float>(std::sqrt(static_cast<double>(velocity.x) * velocity.x +
                                                          static_cast<double>(velocity.y) * velocity.y +
                                                          static_cast<double>(velocity.z) * velocity.z));
                stop = GameFunction<float(__cdecl*)(uint32_t)>(0x0043DA70)(kStickyLandThresholdSpeed) > speed;
            }
        }
        if (stop)
            velocity = {0.0f, 0.0f, 0.0f, 1.0f};
        VectorAt(this, kRideOffset) = {0.0f, 0.0f, 0.0f, 1.0f};
        Call(0x0046B7F0, At<uint8_t*>(this, kInput) + 0x1C0); // (a button's release taken)
        At<uint32_t>(this, kAnimState) = kLand;
    }
    ReleaseHit(feeler);
}

// 0x0052B370
void CWalkComponent::SnapToGround()
{
    // A feeler from snap_up_height above to snap_down_height below the position.
    alignas(16) uint8_t storage[sizeof(Nx::CFeeler)];
    auto* feeler = reinterpret_cast<Nx::CFeeler*>(storage)->Construct();
    Mth::Vector& position = VectorAt(this, kPosition);
    feeler->m_line.start = position;
    feeler->m_line.start.y += WalkParameter(kSnapUpHeight);
    feeler->m_line.end = position;
    feeler->m_line.end.y -= WalkParameter(kSnapDownHeight);
    if (feeler->GetCollision(true, false) && !(feeler->m_hit.faceFlags & kFaceWall)) {
        // Onto the ground (a big step told to the object as an event); a step up, or down more than 3, goes into
        // the step smoothing's height.
        float step = feeler->m_point.y - position.y;
        float unnoticed = WalkParameter(kMaxUnnoticedGroundSnap);
        if (std::fabs(step) > unnoticed)
            m_object->SelfEvent(step > 0.0f ? kSteppedUp : kSteppedDown, nullptr);
        position.y = feeler->m_point.y;
        if (step > 0.0f || step < kBigStepDown) {
            double offset = static_cast<double>(At<float>(this, kCurbOffset)) - step;
            At<float>(this, kCurbOffset) = offset < 0.0 ? 0.0f : static_cast<float>(offset);
        }
        // Onto other collision: the old ground's and the new ground's trigger kinds set off.
        Nx::CFeeler* ground = &At<Nx::CFeeler>(this, kTriggerFeeler);
        if (ground->m_hit.object != feeler->m_hit.object) {
            if (At<bool>(this, kHasGround)) {
                Call(0x00434BF0, At<void*>(this, kTriggerComponent), reinterpret_cast<const void*>(0x0064C660), ground);
                if (Halted(this)) {
                    GameFunction<void(__thiscall*)(Nx::CFeeler*)>(0x0041C350)(feeler); // (its destructor)
                    return;
                }
            }
            Call(0x00434BF0, At<void*>(this, kTriggerComponent), reinterpret_cast<const void*>(0x0064C654), feeler);
            if (Halted(this)) {
                GameFunction<void(__thiscall*)(Nx::CFeeler*)>(0x0041C350)(feeler); // (its destructor)
                return;
            }
        }
        Call(0x0043A930, ground, feeler); // (the feeler copied)
        At<bool>(this, kHasGround) = true;
        VectorAt(this, kGroundNormal) = feeler->m_normal;
        Call(0x00427CD0, At<void*>(this, kRide), feeler); // (what it stands on)
        // (the terrain byte, passed as the dword there)
        Call(0x005196A0, At<void*>(this, kTerrainComponent), *reinterpret_cast<const uint32_t*>(&feeler->m_hit.terrain));
        ReleaseHit(feeler);
        return;
    }

    // No ground (or only a wall): off the wall, then off the edge - unless a hang, a drop or a ladder takes over.
    if (feeler->GetCollision(true, false))
        PutOff(&position, feeler, kWallGap);
    if (At<bool>(this, kHasGround)) {
        Call(0x00434BF0, At<void*>(this, kTriggerComponent), reinterpret_cast<const void*>(0x0064C630),
             &At<Nx::CFeeler>(this, kTriggerFeeler));
        if (Halted(this)) {
            ReleaseHit(feeler);
            return;
        }
    }
    if (At<uint8_t*>(this, kInput)[0x1A0]) {
        Mth::Vector& oldPosition = VectorAt(this, kOldPosition);
        if (position == oldPosition)
            oldPosition.y += kNudge; // (so the two differ)
        if (Call<bool>(0x005299F0, this)) {
            ReleaseHit(feeler);
            return;
        }
    }
    if (Call<bool>(0x00441FC0, this) || Call<bool>(0x0043EC40, this)) {
        ReleaseHit(feeler);
        return;
    }
    Call(0x005260E0, this, 1);
    VectorAt(this, kHeading) = VectorAt(this, kFacing);
    LeaveRide(&VectorAt(this, kVelocity), &At<float>(this, kVerticalSpeed));
    At<uint32_t>(this, kAnimState) = kWalkOffEdge;
    m_object->BroadcastEvent(kSkaterOffEdge, nullptr, 0.0f);
    ReleaseHit(feeler);
}

// 0x0052AF40
void CWalkComponent::Begin(bool onGround)
{
    At<int>(this, kStateTime) = Now();
    if (onGround) {
        Call(0x005260E0, this, 0);
        VectorAt(this, kGroundNormal) = {0.0f, 1.0f, 0.0f, 1.0f};
        At<bool>(this, kHasGround) = false;
        m_object->m_vel.y = 0.0f;
        At<bool>(this, kOffVert) = false;
    } else {
        Call(0x005260E0, this, 1);
        // Off a vert ramp (the skater's flag +0x30) with a turn under way: thrown out along the object's forward
        // axis turned on (by +0x730 about +0x734) times initial_vert_vel_boost.
        if (SkaterFlags(this)[0x30] && !(At<float>(this, kVertTurnSpeed) == 0.0f)) {
            Mth::Vector forward = VectorAt(m_object, kObjectForward);
            GameFunction<void(__thiscall*)(Mth::Vector*, const Mth::Vector*, float)>(0x00421440)(
                &forward, &VectorAt(this, kVertTurnAxis), At<float>(this, kVertTurn));
            double boost = WalkParameter(kInitialVertVelBoost);
            Mth::Vector& velocity = m_object->m_vel;
            auto z = static_cast<float>(forward.z * boost);
            auto w = static_cast<float>(forward.w * boost);
            velocity.x = static_cast<float>(forward.x * boost + velocity.x); // (x and y's products unrounded)
            velocity.y = static_cast<float>(forward.y * boost + velocity.y);
            velocity.z += z;
            velocity.w += w;
        }
        if (!SkaterFlags(this)[0x30])
            At<bool>(this, kFlag1958) = true;
        // The heading: the velocity flattened, else the forward axis flattened, else the up axis reversed and
        // flattened; made unit length.
        Mth::Vector& heading = VectorAt(this, kHeading);
        heading = m_object->m_vel;
        heading.y = 0.0f;
        double length = std::sqrt((static_cast<double>(heading.x) * heading.x +
                                   static_cast<double>(heading.y) * heading.y) +
                                  static_cast<double>(heading.z) * heading.z);
        if (length < kMinHeadingLength) {
            heading = VectorAt(m_object, kObjectForward);
            heading.y = 0.0f;
            length = std::sqrt((static_cast<double>(heading.z) * heading.z +
                                static_cast<double>(heading.x) * heading.x) +
                               static_cast<double>(heading.y) * heading.y);
            if (length < kMinHeadingLength) {
                heading = -VectorAt(m_object, kObjectUp);
                heading.y = 0.0f;
                length = std::sqrt((static_cast<double>(heading.x) * heading.x +
                                    static_cast<double>(heading.y) * heading.y) +
                                   static_cast<double>(heading.z) * heading.z);
            }
        }
        double inverse = 1.0 / length;
        heading.x = static_cast<float>(inverse * heading.x);
        heading.y = static_cast<float>(inverse * heading.y);
        heading.z = static_cast<float>(inverse * heading.z);
        heading.w = static_cast<float>(inverse * heading.w);
        LeaveRide(&m_object->m_vel, &m_object->m_vel.y);
        At<uint32_t>(this, 0x1898) = 0;
        At<bool>(this, 0x18B0) = false;
        At<bool>(this, kOffVert) = SkaterFlags(this)[0x30] || SkaterFlags(this)[0xB0];
    }
    At<float>(this, kCurbOffset) = 0.0f;
    At<uint32_t>(this, 0x748) = 0;
    At<uint32_t>(this, kPreviousAnimState) = 0;
    At<bool>(this, kJumpHeld) = true;
    VectorAt(this, kRideOffset) = {0.0f, 0.0f, 0.0f, 1.0f};
    At<uint32_t>(this, kTargetHeight) = 0;
    At<uint32_t>(At<void*>(this, 0x1790), 0x18) = 0;
    At<bool>(this, 0x70C) = false;
}

// 0x0052B7D0
void CWalkComponent::Jump()
{
    // How high: on the ground or in the air by how long jump has been held (from min_jump_factor up to 1 at
    // hold_time_for_max_jump); off a hang or a ladder hang_jump_factor.
    float factor = 0.0f;
    switch (At<int>(this, kState)) {
    case 0:
    case 1: {
        float hold = WalkParameter(kHoldTimeForMaxJump);
        auto* button = reinterpret_cast<CControlButton*>(At<uint8_t*>(this, kInput) + 0x1C0);
        double held = static_cast<uint32_t>(button->GetPressedTime()) / static_cast<double>(hold);
        float t = held > 1.0 ? 1.0f : static_cast<float>(held);
        float minimum = WalkParameter(kMinJumpFactor);
        factor = static_cast<float>((1.0 - minimum) * t + minimum);
        break;
    }
    case 2:
    case 3:
    case 4:
        factor = WalkParameter(kHangJumpFactor);
        break;
    }
    if (At<int>(this, kState) == 0 && At<bool>(this, kHasGround)) {
        Call(0x00434BF0, At<void*>(this, kTriggerComponent), reinterpret_cast<const void*>(0x0064C63C),
             &At<Nx::CFeeler>(this, kTriggerFeeler)); // (the ground's jump triggers)
        if (Halted(this))
            return;
    }
    m_object->m_vel.y = WalkParameter(kJumpVelocity) * factor;

    Mth::Vector& heading = VectorAt(this, kHeading);
    const Mth::Vector& facing = VectorAt(this, kFacing);
    switch (At<int>(this, kState)) {
    case 0:
    case 1: {
        // Running: pushed along the control's direction at least as fast as jump_horiz_speed (scaled by how far
        // the control is pushed, over the run speed's range); the heading the velocity's when moving.
        Call(0x00527DB0, this);
        Mth::Vector& velocity = VectorAt(this, kVelocity);
        if (At<float>(this, kControlMagnitude) == 0.0f) {
            heading = facing;
        } else {
            float push = Call<float>(0x00528120, this); // (how far the control is pushed, 0 - 1)
            float scaled = GameFunction<float(__cdecl*)(uint32_t)>(0x0043DA70)(kJumpHorizSpeed) * push;
            auto speed = static_cast<float>(scaled / GameFunction<double(__thiscall*)(const void*)>(0x0043DAA0)(this));
            const Mth::Vector& direction = VectorAt(this, kControlDirection);
            double along = (static_cast<double>(velocity.z) * direction.z + static_cast<double>(velocity.y) * direction.y) +
                           static_cast<double>(velocity.x) * direction.x;
            if (along > 0.0) {
                velocity.ProjectOnto(direction);
                double length = std::sqrt((static_cast<double>(velocity.x) * velocity.x +
                                           static_cast<double>(velocity.y) * velocity.y) +
                                          static_cast<double>(velocity.z) * velocity.z);
                if (length < speed)
                    GameFunction<Mth::Vector*(__thiscall*)(Mth::Vector*, float)>(0x0042BBF0)(&velocity, speed); // (its length set)
            } else {
                velocity = speed * direction;
            }
            heading = direction;
            VectorAt(this, kFacing) = direction;
        }
        Call(0x00529160, this);
        double lengthSqr = (static_cast<double>(velocity.x) * velocity.x + static_cast<double>(velocity.y) * velocity.y) +
                           static_cast<double>(velocity.z) * velocity.z;
        if (lengthSqr > kMovingSqr) {
            heading = velocity;
            GameFunction<Mth::Vector*(__thiscall*)(Mth::Vector*, float)>(0x00423650)(
                &heading, static_cast<float>(std::sqrt(lengthSqr))); // (divided by it)
        } else {
            heading = facing;
        }
        Call(0x00529CE0, this);
        break;
    }
    case 2: // hanging: off the ledge's plane, kept level
        At<bool>(this, kHangJump) = true;
        VectorAt(this, kHangJumpFacing) = facing;
        At<float>(this, kHangJumpPlane) = HangPlane(this);
        At<float>(this, kHangJumpHeight) = VectorAt(this, kPosition).y - At<float>(this, kHangHeight);
        m_object->m_vel.x = 0.0f;
        m_object->m_vel.z = 0.0f;
        heading = facing;
        break;
    case 3: // on a ladder
        m_object->m_vel.x = 0.0f;
        m_object->m_vel.z = 0.0f;
        heading = facing;
        break;
    case 4: // moving along a hang: the plane along the facing it had when the hang began
        At<bool>(this, kHangJump) = true;
        At<float>(this, kHangJumpPlane) = HangPlane(this);
        m_object->m_vel.x = 0.0f;
        m_object->m_vel.z = 0.0f;
        heading = facing;
        break;
    }
    LeaveRide(&m_object->m_vel, &m_object->m_vel.y);
    Call(0x005260E0, this, 1);
    m_object->BroadcastEvent(kSkaterJump, nullptr, 0.0f);
}

// 0x0052BC70
bool CWalkComponent::TryBarrierJump()
{
    if (SkaterFlags(this)[0x70])
        return false;
    static const int kAhead[3] = {0, 1, 6}; // the contacts ahead: straight on and either side of it

    float highest = WalkParameter(kBarrierJumpHighestBarrier);
    float reach = WalkParameter(kFeelerLength) * 3.0f;
    float feelerHeight = WalkParameter(kFeelerHeight);
    auto step = static_cast<float>((static_cast<double>(highest) - feelerHeight) * kNinth);
    alignas(16) uint8_t feelerStorage[sizeof(Nx::CFeeler)];
    auto* feeler = reinterpret_cast<Nx::CFeeler*>(feelerStorage)->Construct();

    // The collision near it: `reach` + 1 to each side, from its feet to above the highest barrier.
    const Mth::Vector& position = VectorAt(this, kPosition);
    auto side = static_cast<float>(static_cast<double>(reach) + 1.0);
    Mth::BBox box;
    box.max = {side + position.x, static_cast<float>(static_cast<double>(highest) + 1.0) + position.y,
               side + position.z, static_cast<float>(static_cast<double>(position.w) + 1.0)};
    box.min = {position.x - side, position.y, position.z - side,
               static_cast<float>(static_cast<double>(position.w) - 1.0)};
    alignas(16) uint8_t cacheStorage[sizeof(Nx::CCollCache)];
    auto* cache = reinterpret_cast<Nx::CCollCache*>(cacheStorage)->Construct();
    cache->Update(box);
    feeler->mp_cache = cache;

    // Clear: a contact ahead, facing the control's direction head-on (within barrier_jump_max_angle), with nothing in
    // the way at the highest barrier's height.
    bool clear = false;
    for (int index : kAhead) {
        auto& contact = ContactAt(this, index);
        if (!contact.hit || (contact.feeler.m_hit.faceFlags & kFaceWall))
            continue;
        double limit = X87Cos(WalkParameter(kBarrierJumpMaxAngle) * kDegrees);
        const Mth::Vector& control = VectorAt(this, kControlDirection);
        double facingIt = (static_cast<double>(control.z) * contact.normal.z +
                           static_cast<double>(control.y) * contact.normal.y) +
                          static_cast<double>(control.x) * contact.normal.x;
        if (-limit < facingIt) {
            contact.clear = false;
            continue;
        }
        Mth::Vector& start = feeler->m_line.start;
        start = position;
        start.y = position.y + highest;
        Mth::Vector direction;
        Call(0x00528220, this, &direction, index); // (the contact's direction)
        Mth::Vector& end = feeler->m_line.end;
        end.x = static_cast<float>(static_cast<double>(reach) * direction.x) + start.x;
        end.y = static_cast<float>(static_cast<double>(reach) * direction.y) + start.y;
        end.z = static_cast<float>(static_cast<double>(reach) * direction.z) + start.z;
        end.w = static_cast<float>(static_cast<double>(reach) * direction.w + start.w);
        contact.clear = !feeler->GetCollision(true, false);
        if (contact.clear)
            clear = true;
    }
    if (!clear) {
        cache->Destruct();
        ReleaseHit(feeler);
        return false;
    }

    // How high the barrier is: from the highest barrier down in ninths until each clear contact's feeler meets it.
    const Mth::Vector& facing = VectorAt(this, kFacing);
    float top = highest + position.y;
    for (int index : kAhead) {
        const auto& contact = ContactAt(this, index);
        if (!contact.hit || !contact.clear)
            continue;
        Mth::Vector& start = feeler->m_line.start;
        start = position;
        start.y = position.y + highest;
        double angle = index * kSeventhTurn;
        double c = X87Cos(angle);
        double s = X87Sin(angle);
        auto x = static_cast<float>(c * facing.x - s * facing.z);
        Mth::Vector& end = feeler->m_line.end;
        end.x = static_cast<float>(static_cast<double>(x) * reach) + start.x;
        end.y = static_cast<float>(static_cast<double>(reach) * 0.0) + start.y;
        end.z = static_cast<float>((s * facing.x + c * facing.z) * reach) + start.z;
        end.w = reach + start.w;
        int tries = 8;
        bool hit;
        do {
            start.y = start.y - step;
            end.y = end.y - step;
            --tries;
            hit = feeler->GetCollision(true, false);
        } while (!hit && tries != 0);
        if (top > start.y)
            top = start.y;
    }

    // Over it: up fast enough to clear it by barrier_jump_min_clearance.
    float clearance = WalkParameter(kBarrierJumpMinClearance);
    auto rise = static_cast<float>(((static_cast<double>(clearance) + top) + step) - position.y);
    double gravity = Call<float>(0x005284C0, this);
    auto speed = static_cast<float>(std::sqrt(gravity * rise * 2.0));
    if (At<bool>(this, kHasGround)) {
        Call(0x00434BF0, At<void*>(this, kTriggerComponent), reinterpret_cast<const void*>(0x0064C63C),
             &At<Nx::CFeeler>(this, kTriggerFeeler)); // (the ground's jump triggers)
        if (Halted(this)) {
            cache->Destruct();
            ReleaseHit(feeler);
            return true;
        }
    }
    At<float>(this, kVerticalSpeed) = speed;
    Call(0x005260E0, this, 1);
    VectorAt(this, kHeading) = VectorAt(this, kFacing);
    LeaveRide(&VectorAt(this, kVelocity), &At<float>(this, kVerticalSpeed));
    At<uint32_t>(this, kAnimState) = kJump;
    m_object->BroadcastEvent(kSkaterJump, nullptr, 0.0f);
    m_object->ClearEventHandler(kJumpRequested);
    cache->Destruct();
    ReleaseHit(feeler);
    return true;
}

// 0x0052C2C0
bool CWalkComponent::UpdateContacts()
{
    float reach = WalkParameter(kFeelerLength);
    float feelerHeight = WalkParameter(kFeelerHeight);
    alignas(16) uint8_t storage[sizeof(Nx::CFeeler)];
    auto* feeler = reinterpret_cast<Nx::CFeeler*>(storage)->Construct();
    Mth::Vector& position = VectorAt(this, kPosition);
    Mth::Vector& velocity = VectorAt(this, kVelocity);
    const Mth::Vector& facing = VectorAt(this, kFacing);

    // The eight feelers: seven around it at feeler_height, a seventh of a turn apart from the facing, and in the air
    // one along this frame's fall.
    bool any = false;
    for (int i = 0; i < 8; ++i) {
        Mth::Vector& start = feeler->m_line.start;
        Mth::Vector& end = feeler->m_line.end;
        if (i == 7) {
            if (At<int>(this, kState) != 1) {
                ContactAt(this, 7).hit = false;
                continue;
            }
            float frame = At<float>(this, kFrameLength);
            const Mth::Vector& offset = VectorAt(this, kRideOffset);
            float z = position.z + offset.z;
            float y = position.y + offset.y;
            float x = offset.x + position.x;
            double w = static_cast<double>(position.w) + offset.w;
            start = {x, y, z, static_cast<float>(w)};
            float moveW = frame * velocity.w;
            float moveZ = frame * velocity.z;
            float moveY = frame * velocity.y;
            float moveX = frame * velocity.x;
            end = {moveX + x, moveY + y, moveZ + z, static_cast<float>(w + moveW)};
            float fall = At<float>(this, kVerticalSpeed) * frame;
            double gravity = Call<float>(0x005284C0, this);
            end.y = static_cast<float>((fall - gravity * (static_cast<double>(frame) * frame) * kSteepGround) + end.y);
        } else {
            start = position;
            start.y = position.y + feelerHeight;
            double angle = i * kSeventhTurn;
            double c = X87Cos(angle);
            double s = X87Sin(angle);
            auto x = static_cast<float>(c * facing.x - s * facing.z);
            auto z = static_cast<float>((s * facing.x + c * facing.z) * reach);
            float y = static_cast<float>(static_cast<double>(reach) * 0.0) + position.y;
            end = {static_cast<float>(static_cast<double>(x) * reach) + position.x, y + feelerHeight, z + position.z,
                   reach + position.w};
        }
        auto& contact = ContactAt(this, i);
        contact.hit = feeler->GetCollision(true, false);
        if (!contact.hit)
            continue;
        if (i == 7 && feeler->m_normal.y > kFloorBelow) { // (a floor below: not a contact)
            contact.hit = false;
            continue;
        }
        Call(0x0043A930, &contact.feeler, feeler); // (the feeler copied)
        // The normal flattened and made unit length; how fast the object hit moves along it (not what it rides).
        contact.normal = feeler->m_normal;
        contact.normal.y = 0.0f;
        any = true;
        Mth::Vector& normal = contact.normal;
        double length = std::sqrt((static_cast<double>(normal.x) * normal.x + static_cast<double>(normal.y) * normal.y) +
                                  static_cast<double>(normal.z) * normal.z);
        if (length > 0.0) {
            double inverse = 1.0 / length;
            normal.x = static_cast<float>(inverse * normal.x);
            normal.y = static_cast<float>(inverse * normal.y);
            normal.z = static_cast<float>(inverse * normal.z);
        }
        contact.objectSpeed = 0.0f;
        if (feeler->HitObjectAlive()) {
            void* ride = At<void*>(this, kRide);
            bool ridden = false;
            if (auto* tracker = At<CRideTracker*>(ride, 0x18)) {
                if (!tracker->m_object.Get())
                    Call(0x00427C70, ride); // (what it stood on is gone)
                else
                    ridden = tracker->m_object.Get() == feeler->GetHitObject();
            }
            if (!ridden) {
                const Mth::Vector& moving = feeler->GetHitObject()->m_vel;
                contact.objectSpeed = static_cast<float>(
                    (static_cast<double>(moving.z) * normal.z + static_cast<double>(moving.y) * normal.y) +
                    static_cast<double>(moving.x) * normal.x);
            }
        }
    }

    if (At<int>(this, kState) == 0) {
        // Walking into a barrier ahead for barrier_jump_delay: a jump over it (or another kind of jump) tried.
        if ((ContactAt(this, 0).hit || ContactAt(this, 1).hit || ContactAt(this, 6).hit) &&
            At<bool>(this, kBarrierJumpAllowed)) {
            if (!At<bool>(this, kBarrierTimed)) {
                At<int>(this, kBarrierSince) = Now();
                At<bool>(this, kBarrierTimed) = true;
            }
            int since = At<int>(this, kBarrierSince);
            float delay = WalkParameter(kBarrierJumpDelay);
            auto elapsed = static_cast<uint32_t>(Tmr::GameClockElapsed() + (GameVariable<int>(0x00696D60) - since));
            if (static_cast<double>(elapsed) > delay && (Call<bool>(0x00441DE0, this, false) || TryBarrierJump())) {
                ReleaseHit(feeler);
                return false;
            }
        } else {
            At<bool>(this, kBarrierTimed) = false;
        }
        if (At<uint8_t*>(this, kInput)[0x100] && !At<bool>(this, kJumpHeld) && Call<bool>(0x00441DE0, this, true)) {
            ReleaseHit(feeler);
            return false;
        }
        if (Halted(this)) {
            ReleaseHit(feeler);
            return false;
        }
        // On steep ground: pushed down the slope by push_strength.
        if (At<int>(this, kState) == 0 && VectorAt(this, kGroundNormal).y < kSteepGround) {
            const Mth::Vector& ground = VectorAt(this, kGroundNormal);
            float x = ground.x;
            float y = 0.0f;
            float z = ground.z;
            float w = ground.w;
            double length = std::sqrt(static_cast<double>(x) * x + static_cast<double>(z) * z);
            if (length > 0.0) {
                double inverse = 1.0 / length;
                x = static_cast<float>(x * inverse);
                y = static_cast<float>(0.0 * inverse);
                z = static_cast<float>(z * inverse);
            }
            double push = static_cast<double>(WalkParameter(kPushStrength)) * At<float>(this, kFrameLength);
            position.x = static_cast<float>(x * push + position.x);
            position.y = static_cast<float>(y * push + position.y);
            position.z = static_cast<float>(z * push + position.z);
            position.w = static_cast<float>(w * push + position.w);
        }
    }
    if (!any) {
        ReleaseHit(feeler);
        return false;
    }

    // Pushed out of what is close: within push_feeler_length (as a part of feeler_length) of a contact.
    for (int i = 0; i < 8; ++i) {
        const auto& contact = ContactAt(this, i);
        if (!contact.hit)
            continue;
        float distance = contact.feeler.m_distance;
        if (WalkParameter(kPushFeelerLength) / static_cast<double>(reach) > distance) {
            double push = static_cast<double>(WalkParameter(kPushStrength)) * At<float>(this, kFrameLength);
            position.x = static_cast<float>(contact.normal.x * push + position.x);
            position.y = static_cast<float>(contact.normal.y * push + position.y);
            position.z = static_cast<float>(contact.normal.z * push + position.z);
            position.w = static_cast<float>(contact.normal.w * push + position.w);
        }
    }

    // Only the contacts it is moving into (faster than they move away) count.
    bool pressing = false;
    for (int i = 0; i < 8; ++i) {
        auto& contact = ContactAt(this, i);
        if (!contact.hit)
            continue;
        double into = (static_cast<double>(velocity.z) * contact.normal.z +
                       static_cast<double>(contact.normal.x) * velocity.x) +
                      static_cast<double>(velocity.y) * contact.normal.y;
        if (into >= contact.objectSpeed)
            contact.hit = false;
        else
            pressing = true;
    }
    if (!pressing) {
        ReleaseHit(feeler);
        return false;
    }

    // In a corner (two of them facing apart): stopped.
    for (int a = 0; a < 8; ++a) {
        const auto& first = ContactAt(this, a);
        if (!first.hit)
            continue;
        for (int b = a + 1; b < 8; ++b) {
            const auto& second = ContactAt(this, b);
            if (!second.hit)
                continue;
            double apart = (static_cast<double>(first.normal.z) * second.normal.z +
                            static_cast<double>(first.normal.y) * second.normal.y) +
                           static_cast<double>(second.normal.x) * first.normal.x;
            if (apart <= 0.0) {
                Stop(this);
                ReleaseHit(feeler);
                return true;
            }
        }
    }

    // Slid along them: the part of the velocity into each taken off (the fall's first, then the front ones'); stopped
    // if that turns it back.
    static const int kOrder[8] = {7, 4, 3, 5, 2, 6, 1, 0};
    double x = velocity.x; // (x and w are kept unrounded, as on the x87's stack)
    float y = velocity.y;
    float z = velocity.z;
    double w = velocity.w;
    for (int i : kOrder) {
        const auto& contact = ContactAt(this, i);
        if (!contact.hit)
            continue;
        const Mth::Vector& normal = contact.normal;
        double into = (static_cast<double>(z) * normal.z + static_cast<double>(y) * normal.y) + x * normal.x;
        if (into >= contact.objectSpeed)
            continue;
        double excess = into - contact.objectSpeed;
        auto offW = static_cast<float>(excess * normal.w);
        auto offZ = static_cast<float>(excess * normal.z);
        auto offY = static_cast<float>(excess * normal.y);
        x -= excess * normal.x;
        y = y - offY;
        z = z - offZ;
        w -= offW;
        double still = (static_cast<double>(z) * velocity.z + static_cast<double>(y) * velocity.y) + x * velocity.x;
        if (still <= 0.0) {
            Stop(this);
            ReleaseHit(feeler);
            return true;
        }
    }

    // No faster than it was going along the new direction.
    auto wRounded = static_cast<float>(w);
    double length = std::sqrt((x * x + static_cast<double>(z) * z) + static_cast<double>(y) * y);
    auto lengthRounded = static_cast<float>(length);
    double newX;
    float newY, newZ, newW;
    if (length <= kStillLength) {
        newX = 0.0;
        newY = 0.0f;
        newZ = 0.0f;
        newW = 1.0f;
    } else {
        double inverse = 1.0 / lengthRounded;
        auto unitX = static_cast<float>(inverse * x);
        auto unitY = static_cast<float>(y * inverse);
        auto unitZ = static_cast<float>(z * inverse);
        auto unitW = static_cast<float>(w * inverse);
        auto along = static_cast<float>((static_cast<double>(unitZ) * velocity.z + static_cast<double>(unitY) * velocity.y) +
                                        static_cast<double>(unitX) * velocity.x);
        if (lengthRounded > along) {
            newW = unitW * along;
            newZ = unitZ * along;
            newY = unitY * along;
            newX = static_cast<double>(unitX) * along;
        } else {
            newX = x;
            newY = y;
            newZ = z;
            newW = wRounded;
        }
    }

    // Blocked: slowed to under half its speed.
    double before = (static_cast<double>(velocity.x) * velocity.x + static_cast<double>(velocity.y) * velocity.y) +
                    static_cast<double>(velocity.z) * velocity.z;
    if (before <= kStillSqr) {
        At<bool>(this, kBlocked) = false;
    } else {
        double after = (newX * newX + static_cast<double>(newZ) * newZ) + static_cast<double>(newY) * newY;
        At<bool>(this, kBlocked) = after / before < kMostlyStopped;
    }
    velocity = {static_cast<float>(newX), newY, newZ, newW};

    // The run animation's speed follows (kept at least min_anim_run_speed) unless it is already that slow.
    auto speed = static_cast<float>(std::sqrt((static_cast<double>(velocity.x) * velocity.x +
                                               static_cast<double>(velocity.y) * velocity.y) +
                                              static_cast<double>(velocity.z) * velocity.z));
    if (At<float>(this, kSpeed) > WalkParameter(kMinAnimRunSpeed)) {
        At<float>(this, kSpeed) = speed;
        float minimum = WalkParameter(kMinAnimRunSpeed);
        At<float>(this, kSpeed) = minimum > speed ? minimum : speed;
    }
    ReleaseHit(feeler);
    return true;
}

} // namespace Obj

using Obj::CWalkComponent;
REPLACE(0x0052A5C0, CWalkComponent::Create);
REPLACE(0x0052A190, &CWalkComponent::Construct);
REPLACE(0x0052A290, &CWalkComponent::Destroy);
REPLACE(0x00529F80, &CWalkComponent::UpdateCameraOverride);
REPLACE(0x0052A2C0, &CWalkComponent::UpdateAnimSpeed);
REPLACE(0x0052A620, &CWalkComponent::UpdateRide);
REPLACE(0x0052AB90, &CWalkComponent::LeaveRide);
REPLACE(0x0052B2D0, &CWalkComponent::GoToAir);
REPLACE(0x0052A180, &CWalkComponent::AnimWaitComplete);
REPLACE(0x0052C280, &CWalkComponent::ReleaseHang);
REPLACE(0x0052C290, &CWalkComponent::ForceToAirFromHang);
REPLACE(0x0052C2B0, &CWalkComponent::GoToAirFor);
REPLACE(0x0052D4B0, &CWalkComponent::Update);
REPLACE(0x0052D300, &CWalkComponent::UpdateGround);
REPLACE(0x0052CEF0, &CWalkComponent::UpdateAir);
REPLACE(0x0052AD10, &CWalkComponent::UpdateAnimWait);
REPLACE(0x0052A700, &CWalkComponent::CollideAirMove);
REPLACE(0x0052B370, &CWalkComponent::SnapToGround);
REPLACE(0x0052AF40, &CWalkComponent::Begin);
REPLACE(0x0052B7D0, &CWalkComponent::Jump);
REPLACE(0x0052BC70, &CWalkComponent::TryBarrierJump);
REPLACE(0x0052C2C0, &CWalkComponent::UpdateContacts);

