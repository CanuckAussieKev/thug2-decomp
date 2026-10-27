// The skater core physics component: taken down, set up and finalized, the physics state changed, and the small
// helpers next to them (0x004FC370-0x004FC778, 0x004FDD50-0x004FE1E2).

#include <cstdint>

#include "game/core/dbg.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/nx/coll_cache.h"
#include "game/obj/control_pad.h"
#include "game/obj/movable_contact.h"
#include "game/obj/object.h"
#include "game/obj/smtptr.h"
#include "game/script/struct.h"
#include "game/skater/core_physics.h"
#include "game/skater/core_physics_fields.h"
#include "game/skater/physics_control.h"
#include "game/skater/skater_state.h"
#include "game/skater/trick_component.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Skater {

using namespace Physics;

namespace {

constexpr uint32_t kVtable = 0x0064BC48;
constexpr uint32_t kRollingFriction = 0x78F80EC4;   // Physics_Rolling_Friction
constexpr uint32_t kGetOffEvent = 0x5E27200A;       // (broadcast when the skater gets off its board; name not recovered)
constexpr uint32_t kRailWaitName = 0xE4412EB7;      // (a script value: the rail wait, ms; name not recovered)
constexpr uint32_t kTrackingName = 0x19FB78FA;      // (a script value: the tracking printout on; name not recovered)
const char* const kProjectileControl = reinterpret_cast<const char*>(0x0064BC34); // "projectile_control"

// The components it finds in Finalize (each one's type, then where it's kept).
constexpr struct {
    uint32_t type;
    uint32_t offset;
} kComponents[] = {
    {0x27D7CD28, 0x150C}, // Input
    {0x187574FA, 0x1514}, // SkaterSound
    {0xE594F0A2, 0x1510}, // Trigger
    {0x270F56E1, 0x1518}, // Trick
    {0x52D61A0D, 0x151C}, // SkaterRotate
    {0xD262802F, 0x1520}, // SkaterScore
    {0x589E5FB3, 0x1524}, // SkaterBalanceTrick
    {0x43686585, 0x1528}, // SkaterState
    {0x408D2AA9, 0x152C}, // MovableContact
    {0xD9151D4D, 0x1530}, // SkaterPhysicsControl
    {0x726E85AA, 0x1534}, // Walk
    {0x380D909A, 0x1538}, // (the projectiles')
};

// The component's fields used here.
constexpr uint32_t kGroundNormalA = 0x58;
constexpr uint32_t kGroundNormalB = 0x68;
constexpr uint32_t kGroundNormalC = 0x78;
constexpr uint32_t kStateTime = 0x90;
constexpr uint32_t kOldState = 0x94;
constexpr uint32_t kFeelerObject = 0x140;          // (the feeler +0xA8's smart pointer)
constexpr uint32_t kGroundFeelerObject = 0x1F0;    // (the feeler +0x158's)
constexpr uint32_t kRailLeft = 0x234;
constexpr uint32_t kRailLeftAt = 0x238;
constexpr uint32_t kRailWait = 0x23C;
constexpr uint32_t kLipPosition = 0x258;
constexpr uint32_t kUnknown2EC = 0x2EC;
constexpr uint32_t kProjectileCount = 0x2F8;
constexpr uint32_t kProjectileInput = 0x308;
constexpr uint32_t kSpinQueued = 0x30C;
constexpr uint32_t kRollingFrictionValue = 0x314;
constexpr uint32_t kFacingBackwards = 0x34C;
constexpr uint32_t kSkitched = 0x364;
constexpr uint32_t kAirStart = 0x380;
constexpr uint32_t kAirEnd = 0x384;
constexpr uint32_t kGetOffPending = 0x4F0;
constexpr uint32_t kCollisionCache = 0x4F4;
constexpr uint32_t kSoundComponent = 0x1514;       // (+0x18 the state, +0x1C the terrain, +0x1E sliding)

// The state component's (+0x1528) flags used here.
constexpr uint32_t kFlagFlipped = 0x28;
constexpr uint32_t kFlagLipToAir = 0x50;
constexpr uint32_t kFlagSlide = 0x58;
constexpr uint32_t kFlagC0 = 0xC0;
constexpr uint32_t kFlagD0 = 0xD0;
constexpr uint32_t kFlagD8 = 0xD8;

enum : int { kGround = 0, kAir = 1, kWallRide = 2, kLip = 3, kRail = 4 };

Obj::CSkaterStateComponent* StateOf(void* component)
{
    return At<Obj::CSkaterStateComponent*>(component, kTimedFlags);
}
uint8_t* SoundOf(void* component) { return At<uint8_t*>(component, kSoundComponent); }
Obj::CTrickComponent* TrickOf(void* component) { return At<Obj::CTrickComponent*>(component, kSkaterComponent); }
void ReleasePointer(void* component, uint32_t offset)
{
    At<Obj::CSmtPtr<Obj::CRefCounted>>(component, offset).Release();
}

} // namespace

// 0x004FC4A0 (cdecl): with the script value 0x19FB78FA set, "Tracking%d" and two vectors printed (compiled out).
void __cdecl TrackingPrint(int index, const Mth::Vector* a, const Mth::Vector* b)
{
    if (!Script::GetInteger(kTrackingName))
        return;
    Dbg::Printf(reinterpret_cast<const char*>(0x0064B998), index, static_cast<double>(a->x),
                static_cast<double>(a->y), static_cast<double>(a->z), static_cast<double>(b->x),
                static_cast<double>(b->y), static_cast<double>(b->z));
}

// 0x004FC3C0
bool CorePhysicsComponent::ProjectileButtonPressed()
{
    uint32_t button = Script::GetSymbolValue(kProjectileControl); // (the game's string)
    if (!TrickOf(this)->CheckReleasePress(button, false, true))
        return false;
    At<int>(this, kProjectileInput) = -1;
    return true;
}

// 0x004FC440
int CorePhysicsComponent::IsSwitched()
{
    uint8_t flipped = At<uint8_t>(StateOf(this), kFlagFlipped);
    if (At<uint8_t>(ObjectOf(this), 0x110))
        return flipped;
    return flipped ? 0 : 1;
}

// 0x004FC470
void CorePhysicsComponent::SetTerrain(uint32_t terrain)
{
    StateOf(this)->SetTerrain(static_cast<uint8_t>(terrain));
    SoundOf(this)[0x1C] = static_cast<uint8_t>(terrain);
}

// 0x004FC500
void CorePhysicsComponent::DestroyBody()
{
    At<uint32_t>(this, 0) = kVtable;
    At<Nx::CCollCache>(this, kCollisionCache).Destruct();
    ReleasePointer(this, kSkitched);
    ReleasePointer(this, kGroundFeelerObject);
    ReleasePointer(this, kFeelerObject);
    reinterpret_cast<Obj::CBaseComponent*>(this)->Obj::CBaseComponent::DestroyBody();
}

// 0x004FC5C0
void CorePhysicsComponent::InitFromStructure(Script::CStruct*)
{
    At<float>(this, kRollingFrictionValue) = PhysicsFloat(kRollingFriction, ObjectOf(this));
    void* object = ObjectOf(this);
    VectorAt(object, kPosition) = Mth::Vector(0.0f, 0.0f, 0.0f, 1.0f);
    Mth::Matrix& matrix = At<Mth::Matrix>(ObjectOf(this), kMatrix);
    matrix.row[0] = Mth::Vector(1.0f, 0.0f, 0.0f, 0.0f);
    matrix.row[1] = Mth::Vector(0.0f, 1.0f, 0.0f, 0.0f);
    matrix.row[2] = Mth::Vector(0.0f, 0.0f, 1.0f, 0.0f);
    matrix.row[3] = Mth::Vector(0.0f, 0.0f, 0.0f, 1.0f);
    At<Mth::Matrix>(this, kDisplayMatrix) = At<Mth::Matrix>(ObjectOf(this), kMatrix);
    VectorAt(this, kGroundNormalA) = Mth::Vector(0.0f, 1.0f, 0.0f, 1.0f);
    VectorAt(this, kGroundNormalB) = VectorAt(this, kGroundNormalA);
    VectorAt(this, kGroundNormalC) = VectorAt(this, kGroundNormalA);
    At<uint8_t>(this, kUnknown2EC) = 0;
    At<int>(this, kProjectileCount) = 0;
}

// 0x004FC690
void CorePhysicsComponent::Finalize()
{
    for (const auto& c : kComponents)
        At<void*>(this, c.offset) = static_cast<Obj::CCompositeObject*>(ObjectOf(this))->GetComponent(c.type);
}

// 0x004FDD50
void CorePhysicsComponent::LeaveSkating()
{
    if (At<uint8_t>(this, kGetOffPending)) {
        At<uint8_t>(this, kGetOffPending) = 0;
        static_cast<Obj::CObject*>(ObjectOf(this))->BroadcastEvent(kGetOffEvent, nullptr, 0.0f);
    }
    At<uint32_t>(this, kRailWait) = Ftol(Script::GetFloat(kRailWaitName));
    if (At<uint32_t>(this, kRailLeftAt) > At<uint32_t>(this, kRailLeft))
        At<uint32_t>(this, kRailLeft) = At<uint32_t>(this, kRailLeftAt);
    uint8_t* state = reinterpret_cast<uint8_t*>(StateOf(this));
    if (At<int>(state, kState) == kAir && At<int>(this, kOldState) == kLip && state[kFlagLipToAir])
        SetTimedFlag(this, kFlagLipToAir, 0);
    StateOf(this)->SetState(kGround);
    VectorAt(StateOf(this), 0xF8) = Mth::Vector(0.0f, 1.0f, 0.0f, 1.0f);
    VectorAt(StateOf(this), 0x108) = Mth::Vector(0.0f, 1.0f, 0.0f, 1.0f);
    VectorAt(StateOf(this), 0xE4) = Mth::Vector(0.0f, 0.0f, 0.0f, 1.0f);
    At<uint8_t>(StateOf(this), 0xF4) = 0;
}

// 0x004FDE40
void CorePhysicsComponent::ClearFlags()
{
    // (Flags 23 down to 0 - the flags block's +0x20 + 8 each -, all but 1: flipped.)
    for (int flag = 23; flag >= 0; --flag) {
        if (flag == 1)
            continue;
        uint32_t offset = 0x20 + 8 * static_cast<uint32_t>(flag);
        SetTimedFlag(this, offset, 1);
        if (flag == 7)
            SoundOf(this)[0x1E] = 1;
        SetTimedFlag(this, offset, 0);
        if (flag == 7)
            SoundOf(this)[0x1E] = 0;
    }
}

// 0x004FDED0
void CorePhysicsComponent::SetPhysicsState(int state)
{
    if (StateOf(this)->m_state != state) {
        At<int>(this, kStateTime) = Now();
        At<int>(this, kOldState) = StateOf(this)->m_state;
    }
    int old = StateOf(this)->m_state;
    Mth::Vector& lip = VectorAt(this, kLipPosition);
    if (old == kLip && state != kLip && (lip.x != 0.0f || lip.y != 0.0f || lip.z != 0.0f)) {
        void* object = ObjectOf(this);
        VectorAt(object, kPosition) = lip;
        VectorAt(object, kOldPosition) = VectorAt(object, kPosition);
    }
    if (state != kLip)
        lip = Mth::Vector(0.0f, 0.0f, 0.0f, 1.0f);
    if (state != kGround)
        TrickOf(this)->SetTrickObjects(true);
    if (StateOf(this)->m_state == kRail && state != kRail) {
        SetTimedFlag(this, kFlagSlide, 0);
        SoundOf(this)[0x1E] = 0;
    }
    old = StateOf(this)->m_state;
    if (old == kAir) {
        if (state != kAir) {
            At<int>(this, kAirEnd) = Now();
            SetTimedFlag(this, kFlagC0, 0);
            SetTimedFlag(this, kFlagD0, 0);
            SetTimedFlag(this, kFlagD8, 0);
        }
    } else if (state == kAir) {
        if (old != kWallRide)
            At<int>(this, kAirStart) = Now();
        At<int>(TrickOf(this), 0x13A4) = 0;
        TakePress(InputOf(this) + 0xA0);
        TakePress(InputOf(this) + 0x100);
        At<int>(this, kSpinQueued) = 0;
    }
    StateOf(this)->SetState(state);
    At<int>(SoundOf(this), 0x18) = state;
}

// 0x004FE120
void CorePhysicsComponent::TurnAround()
{
    void* object = ObjectOf(this);
    Mth::Vector& facing = VectorAt(object, kForward);
    facing = Mth::Vector(-facing.x, -facing.y, -facing.z, -facing.w);
    Mth::Vector& right = VectorAt(ObjectOf(this), kMatrix);
    right = Mth::Vector(-right.x, -right.y, -right.z, -right.w);
    At<Mth::Matrix>(this, kDisplayMatrix) = At<Mth::Matrix>(ObjectOf(this), kMatrix);
    At<uint8_t>(this, kFacingBackwards) = At<uint8_t>(this, kFacingBackwards) ? 0 : 1;
}

// 0x004FE1D0
void CorePhysicsComponent::ResetToAir(void*)
{
    ClearFlags();
    SetPhysicsState(kAir);
}

} // namespace Skater

namespace Mth {

// 0x004FC370
Vector& Vector::ZeroIfShorterThan(float length)
{
    double lengthSquared = (static_cast<double>(x) * x + static_cast<double>(y) * y) + static_cast<double>(z) * z;
    if (lengthSquared < static_cast<double>(length) * length)
        *this = Vector(0.0f, 0.0f, 0.0f, 0.0f);
    return *this;
}

} // namespace Mth

namespace Obj {

// 0x004FC400
int CMovableContactComponent::TimeSinceStop() { return static_cast<int>(Skater::Physics::Since(m_stopTime)); }

// 0x004FC420
bool CSkaterPhysicsControlComponent::IsWalking()
{
    auto* state = reinterpret_cast<CSkaterStateComponent*>(m_state);
    return !state->m_driving && state->m_physicsMode == 1;
}

} // namespace Obj

using Skater::CorePhysicsComponent;
REPLACE(0x004FC370, &Mth::Vector::ZeroIfShorterThan);
REPLACE(0x004FC3C0, &CorePhysicsComponent::ProjectileButtonPressed);
REPLACE(0x004FC400, &Obj::CMovableContactComponent::TimeSinceStop);
REPLACE(0x004FC420, &Obj::CSkaterPhysicsControlComponent::IsWalking);
REPLACE(0x004FC440, &CorePhysicsComponent::IsSwitched);
REPLACE(0x004FC470, &CorePhysicsComponent::SetTerrain);
REPLACE(0x004FC4A0, Skater::TrackingPrint);
REPLACE(0x004FC500, &CorePhysicsComponent::DestroyBody);
REPLACE(0x004FC5C0, &CorePhysicsComponent::InitFromStructure);
REPLACE(0x004FC690, &CorePhysicsComponent::Finalize);
REPLACE(0x004FDD50, &CorePhysicsComponent::LeaveSkating);
REPLACE(0x004FDE40, &CorePhysicsComponent::ClearFlags);
REPLACE(0x004FDED0, &CorePhysicsComponent::SetPhysicsState);
REPLACE(0x004FE120, &CorePhysicsComponent::TurnAround);
REPLACE(0x004FE1D0, &CorePhysicsComponent::ResetToAir);
