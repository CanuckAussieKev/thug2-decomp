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
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/net/gamenet_manager.h"
#include "game/nx/coll_cache.h"
#include "game/nx/feeler.h"
#include "game/obj/anim_blender.h"
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
constexpr uint32_t kOnGround = 0x186C;         // (a byte: cleared when the contacts find nothing)
constexpr uint32_t kSkidTime = 0x1870;
constexpr uint32_t kExtraVelocityTop = 0x18AC; // the extra velocity's lost above this height
constexpr uint32_t kWaitFrom = 0x18E4;         // the animation wait: from, to (vectors), the target height's ends,
constexpr uint32_t kWaitTo = 0x1904;           // the facing, its length (in the animation's time) and the turn
constexpr uint32_t kWaitHeightFrom = 0x1914;   // over it, the offset carried by what it rides on
constexpr uint32_t kWaitHeightTo = 0x1918;
constexpr uint32_t kWaitTurnTime = 0x192C;
constexpr uint32_t kWaitTurn = 0x1930;
constexpr uint32_t kWaitCarried = 0x1934;

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
        auto sine = static_cast<float>(std::sin(kHangTurn));
        auto cosine = static_cast<float>(std::cos(kHangTurn));
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
    Call(0x0052B7D0, this); // (let go of the hang)
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
    if (Call<bool>(0x0052C2C0, this)) // (the contacts)
        Call(0x00526260, this);
    else
        At<bool>(this, kOnGround) = false;
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
    Call(0x0052B370, this); // (the ground under it)

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
    if (!Call<bool>(0x0052C2C0, this)) // (the contacts)
        At<bool>(this, kOnGround) = false;
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
    Call(0x0052A700, this, &oldPosition, &position); // (the move against the world)
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
    Call(0x0052A700, this, &position, &start);
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
    double sine = std::sin(angle);
    double cosine = std::cos(angle);
    double x = facing.x;
    facing.x = static_cast<float>(cosine * x + sine * facing.z);
    facing.z = static_cast<float>(-(x * sine) + cosine * facing.z);
    At<uint32_t>(this, kAnimState) = kAnimWait;
    double low = At<float>(this, kWaitHeightFrom);
    At<float>(this, kTargetHeight) = static_cast<float>((At<float>(this, kWaitHeightTo) - low) * t + low);
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
