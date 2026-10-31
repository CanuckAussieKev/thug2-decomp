// The skater core physics component: taken down, set up and finalized, the physics state changed, and the small
// helpers next to them (0x004FC370-0x004FC778, 0x004FDD50-0x004FE1E2).

#include <cstdint>

#include "game/core/dbg.h"
#include "game/maths/mat4.h"
#include "game/maths/vec4.h"
#include "game/engine3d/coll_cache.h"
#include "game/obj/control_pad.h"
#include "game/obj/movable_contact.h"
#include "game/obj/entity.h"
#include "game/obj/smtptr.h"
#include "game/script/param_struct.h"
#include "game/skater/core_physics.h"
#include "game/skater/core_physics_fields.h"
#include "game/skater/physics_control.h"
#include "game/skater/skater_state.h"
#include "game/skater/trick_component.h"

namespace Skater {

using namespace Physics;

namespace {

constexpr uint32_t kCoastDrag = 0x78F80EC4;   // Physics_Rolling_Friction
constexpr uint32_t kGetOffEvent = 0x5E27200A;       // (broadcast when the skater gets off its board; name not recovered)
constexpr uint32_t kRailWaitName = 0xE4412EB7;      // (a script value: the rail wait, ms; name not recovered)
constexpr uint32_t kTrackingName = 0x19FB78FA;      // (a script value: the tracking printout on; name not recovered)
const char* const kProjectileControl = "projectile_control"; // "projectile_control"

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
constexpr uint32_t kContactMeshStore = 0x4F4;
constexpr uint32_t kSfxModuleSlot = 0x1514;       // (+0x18 the state, +0x1C the terrain, +0x1E sliding)

// The state component's (+0x1528) flags used here.
constexpr uint32_t kFlagFlipped = 0x28;
constexpr uint32_t kFlagLipToAir = 0x50;
constexpr uint32_t kFlagSlide = 0x58;
constexpr uint32_t kFlagC0 = 0xC0;
constexpr uint32_t kFlagD0 = 0xD0;
constexpr uint32_t kFlagD8 = 0xD8;

enum : int { kGround = 0, kAir = 1, kWallRide = 2, kLip = 3, kRail = 4 };

GameObjects::CRiderPoseState* StateOf(void* component)
{
    return At<GameObjects::CRiderPoseState*>(component, kTimedFlags);
}
uint8_t* SoundOf(void* component) { return At<uint8_t*>(component, kSfxModuleSlot); }
GameObjects::CMoveExecutor* TrickOf(void* component) { return At<GameObjects::CMoveExecutor*>(component, kSkaterComponent); }
void ReleasePointer(void* component, uint32_t offset)
{
    At<GameObjects::CSharedRef<GameObjects::CSharedBase>>(component, offset).Release();
}

} // namespace

// 0x004FC4A0 (cdecl): with the script value 0x19FB78FA set, "Tracking%d" and two vectors printed (compiled out).
void __cdecl TrackingPrint(int index, const Maths::Vec4* a, const Maths::Vec4* b)
{
    if (!Scripts::FetchIntValue(kTrackingName))
        return;
    DebugOut::Printf("Tracking%d %.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n", index, static_cast<double>(a->x),
                static_cast<double>(a->y), static_cast<double>(a->z), static_cast<double>(b->x),
                static_cast<double>(b->y), static_cast<double>(b->z));
}

// 0x004FC3C0
bool SkaterPhysicsModule::ProjectileButtonPressed()
{
    uint32_t button = Scripts::GetSymbolValue(kProjectileControl); // (the game's string)
    if (!TrickOf(this)->CheckReleasePress(button, false, true))
        return false;
    At<int>(this, kProjectileInput) = -1;
    return true;
}

// 0x004FC440
int SkaterPhysicsModule::IsSidesSwapped()
{
    uint8_t flipped = At<uint8_t>(StateOf(this), kFlagFlipped);
    if (At<uint8_t>(ObjectOf(this), 0x110))
        return flipped;
    return flipped ? 0 : 1;
}

// 0x004FC470
void SkaterPhysicsModule::SetSurfaceKind(uint32_t terrain)
{
    StateOf(this)->SetSurfaceKind(static_cast<uint8_t>(terrain));
    SoundOf(this)[0x1C] = static_cast<uint8_t>(terrain);
}

// 0x004FC500
void SkaterPhysicsModule::DestroyBody()
{
    At<void**>(this, 0) = g_corePhysicsVtable;
    At<Engine3D::CHitMeshCache>(this, kContactMeshStore).Destruct();
    ReleasePointer(this, kSkitched);
    ReleasePointer(this, kGroundFeelerObject);
    ReleasePointer(this, kFeelerObject);
    reinterpret_cast<GameObjects::CObjectModule*>(this)->GameObjects::CObjectModule::DestroyBody();
}

// 0x004FC5C0
void SkaterPhysicsModule::ConfigureFromData(Scripts::CStruct*)
{
    At<float>(this, kRollingFrictionValue) = TuningFloatLookup(kCoastDrag, ObjectOf(this));
    void* object = ObjectOf(this);
    VectorAt(object, kPosition) = Maths::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    Maths::Mat4& matrix = At<Maths::Mat4>(ObjectOf(this), kMatrix);
    matrix.row[0] = Maths::Vec4(1.0f, 0.0f, 0.0f, 0.0f);
    matrix.row[1] = Maths::Vec4(0.0f, 1.0f, 0.0f, 0.0f);
    matrix.row[2] = Maths::Vec4(0.0f, 0.0f, 1.0f, 0.0f);
    matrix.row[3] = Maths::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    At<Maths::Mat4>(this, kDisplayMatrix) = At<Maths::Mat4>(ObjectOf(this), kMatrix);
    VectorAt(this, kGroundNormalA) = Maths::Vec4(0.0f, 1.0f, 0.0f, 1.0f);
    VectorAt(this, kGroundNormalB) = VectorAt(this, kGroundNormalA);
    VectorAt(this, kGroundNormalC) = VectorAt(this, kGroundNormalA);
    At<uint8_t>(this, kUnknown2EC) = 0;
    At<int>(this, kProjectileCount) = 0;
}

// 0x004FC690
void SkaterPhysicsModule::Seal()
{
    for (const auto& c : kComponents)
        At<void*>(this, c.offset) = static_cast<GameObjects::CGameEntity*>(ObjectOf(this))->FetchModule(c.type);
}

// 0x004FDD50
void SkaterPhysicsModule::LeaveSkating()
{
    if (At<uint8_t>(this, kGetOffPending)) {
        At<uint8_t>(this, kGetOffPending) = 0;
        static_cast<GameObjects::CEntity*>(ObjectOf(this))->Announce(kGetOffEvent, nullptr, 0.0f);
    }
    At<uint32_t>(this, kRailWait) = Ftol(Scripts::FetchFloat(kRailWaitName));
    if (At<uint32_t>(this, kRailLeftAt) > At<uint32_t>(this, kRailLeft))
        At<uint32_t>(this, kRailLeft) = At<uint32_t>(this, kRailLeftAt);
    uint8_t* state = reinterpret_cast<uint8_t*>(StateOf(this));
    if (At<int>(state, kState) == kAir && At<int>(this, kOldState) == kLip && state[kFlagLipToAir])
        SetTimedFlag(this, kFlagLipToAir, 0);
    StateOf(this)->ChangePhase(kGround);
    VectorAt(StateOf(this), 0xF8) = Maths::Vec4(0.0f, 1.0f, 0.0f, 1.0f);
    VectorAt(StateOf(this), 0x108) = Maths::Vec4(0.0f, 1.0f, 0.0f, 1.0f);
    VectorAt(StateOf(this), 0xE4) = Maths::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    At<uint8_t>(StateOf(this), 0xF4) = 0;
}

// 0x004FDE40
void SkaterPhysicsModule::ClearFlags()
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
void SkaterPhysicsModule::SetPhysicsState(int state)
{
    if (StateOf(this)->m_curPhase != state) {
        At<int>(this, kStateTime) = Now();
        At<int>(this, kOldState) = StateOf(this)->m_curPhase;
    }
    int old = StateOf(this)->m_curPhase;
    Maths::Vec4& lip = VectorAt(this, kLipPosition);
    if (old == kLip && state != kLip && (lip.x != 0.0f || lip.y != 0.0f || lip.z != 0.0f)) {
        void* object = ObjectOf(this);
        VectorAt(object, kPosition) = lip;
        VectorAt(object, kOldPosition) = VectorAt(object, kPosition);
    }
    if (state != kLip)
        lip = Maths::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    if (state != kGround)
        TrickOf(this)->SetTrickObjects(true);
    if (StateOf(this)->m_curPhase == kRail && state != kRail) {
        SetTimedFlag(this, kFlagSlide, 0);
        SoundOf(this)[0x1E] = 0;
    }
    old = StateOf(this)->m_curPhase;
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
    StateOf(this)->ChangePhase(state);
    At<int>(SoundOf(this), 0x18) = state;
}

// 0x004FE120
void SkaterPhysicsModule::TurnAround()
{
    void* object = ObjectOf(this);
    Maths::Vec4& facing = VectorAt(object, kForward);
    facing = Maths::Vec4(-facing.x, -facing.y, -facing.z, -facing.w);
    Maths::Vec4& right = VectorAt(ObjectOf(this), kMatrix);
    right = Maths::Vec4(-right.x, -right.y, -right.z, -right.w);
    At<Maths::Mat4>(this, kDisplayMatrix) = At<Maths::Mat4>(ObjectOf(this), kMatrix);
    At<uint8_t>(this, kFacingBackwards) = At<uint8_t>(this, kFacingBackwards) ? 0 : 1;
}

// 0x004FE1D0
void SkaterPhysicsModule::ResetToAir(void*)
{
    ClearFlags();
    SetPhysicsState(kAir);
}

} // namespace Skater

namespace Maths {

// 0x004FC370
Vec4& Vec4::ClampTinyToNothing(float length)
{
    double lengthSquared = (static_cast<double>(x) * x + static_cast<double>(y) * y) + static_cast<double>(z) * z;
    if (lengthSquared < static_cast<double>(length) * length)
        *this = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
    return *this;
}

} // namespace Mth

namespace GameObjects {

// 0x004FC400
int CMovingSurfaceModule::TimeSinceStop() { return static_cast<int>(Skater::Physics::Since(m_stopTime)); }

// 0x004FC420
bool CRiderBoardPhysics::IsOnFoot()
{
    auto* state = reinterpret_cast<CRiderPoseState*>(m_curPhase);
    return !state->m_inVehicle && state->m_physicsMode == 1;
}

} // namespace Obj

using Skater::SkaterPhysicsModule;
