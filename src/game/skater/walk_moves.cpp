// The Walk component's update and moves (Obj::CWalkComponent, 0x00529F80-0x0052D6A0): see walk_physics.h. From the
// disassembly: the x87 arithmetic is followed where it keeps values unrounded (doubles here) and where it stores
// them (floats).

#include "game/skater/walk_physics.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/net/gamenet_manager.h"
#include "game/nx/coll_cache.h"
#include "game/nx/feeler.h"
#include "game/obj/object.h"
#include "game/obj/ride_tracker.h"
#include "game/obj/smtptr.h"
#include "game/script/struct.h"
#include "game/sys/game_clock.h"
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

// Animation states.
constexpr uint32_t kAnimWait = 0x4FE6069C;       // ANIMWAIT
constexpr uint32_t kHangMoveRight = 0x279B1F0B;  // HangMoveRight
constexpr uint32_t kHangMoveLeft = 0x2D9815C3;   // HangMoveLeft
constexpr uint32_t kHang = 0x4194ECCA;           // HANG
constexpr uint32_t kLadderMoveUp = 0xAF5ABC82;   // LadderMoveUp
constexpr uint32_t kLadder = 0xC84243DA;         // Ladder
constexpr uint32_t kLadderMoveDown = 0xFEC9DDED; // LadderMoveDown

// Names.
constexpr uint32_t kWalkParameters = 0x6775E538;   // walk_parameters
constexpr uint32_t kHangMoveAnimSpeed = 0x1A47FFAB; // hang_move_animation_speed
constexpr uint32_t kLadderMoveSpeed = 0x0AB2DB54;  // ladder_move_speed
constexpr uint32_t kSpeedName = 0xF0D90109;        // speed
constexpr uint32_t kSetAnimSpeed = 0xBD4EDD44;     // (the animation component's member function given the speed)
constexpr uint32_t kAir = 0x439F4704;              // AIR (an event)

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
