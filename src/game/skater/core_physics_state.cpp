// The skater's core physics component: made, reset and destroyed, and the timers its update counts down.
//
// Float arithmetic follows the original's x87 code at 53-bit precision: intermediate results are doubles, rounded
// to float only where the original stores them.

#include <cmath>
#include <cstdint>
#include <cstring>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/geometry.h"
#include "game/net/gamenet_manager.h"
#include "game/nx/coll_cache.h"
#include "game/nx/feeler.h"
#include "game/obj/model_component.h"
#include "game/obj/object.h"
#include "game/obj/smtptr.h"
#include "game/obj/walk_hang.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skater/core_physics.h"
#include "game/skater/core_physics_fields.h"
#include "game/skater/flip_and_rotate.h"
#include "game/skater/skater.h"
#include "game/skater/thaw_bmx.h"
#include "game/skater/physics_control.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/game_code.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/camera_lookaround.h"
#include "game/obj/node_components.h"
#include "game/skater/core_physics.h"

namespace Skater {

using namespace Physics;

namespace {

constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29;    // SkaterCorePhysics (the component type)
constexpr uint32_t kModel = 0x286A8D26;                // Model (a component)
constexpr uint32_t kSkaterFlipAndRotate = 0x488AF07A;  // SkaterFlipAndRotate (a component)
constexpr uint32_t kTimeBeforeFreeRevert = 0xF4813AD5; // Physics_Time_Before_Free_Revert
constexpr uint32_t kFocusButtonTime = 0xD7E8D8DE;      // focus_button_pressed_time
constexpr uint32_t kToggleBulletTime = 0xD7196D01;     // toggle_bullettime (a script)
constexpr uint32_t kWalkThrow = 0x0CE5B783;            // walk_maybe_throw_projectile (a script)
constexpr uint32_t kSkateThrow = 0x3F356C63;           // skate_maybe_throw_projectile (a script)
constexpr uint32_t kTimeLimit = 0x70F16EA9;            // TimeLimit
constexpr uint32_t kKillSpecial = 0x266B570C;          // KillSpecial (a script)
constexpr uint32_t kAirTimeLeft = 0x7E2A8993;          // AirTimeLeft
constexpr uint32_t kGrindTrickList = 0x2AB3341D;       // GrindTrickList
constexpr uint32_t kMotoskateboardGrind = 0xF7CA3F78; // Trick_Motoskateboard_Grind (a script)
constexpr uint32_t kMotoGrind = 0x302;                 // (on the motoskateboard)

// The free revert's message: the game's own format, file name and text.
const char* const kMessageFormat = GAME_TEXT(0x006466B0, "M:%s:%i: %s\n"); // "M:%s:%i: %s\n"
const char* const kSourceFile = GAME_TEXT(0x0064BCE0, "mponents\\SkaterCorePhysicsComponent.cpp"); // (the component's source file)
const char* const kFreeRevertText = GAME_TEXT(0x0064BD08, "You earned a free revert!!!!"); // "You earned a free revert!!!!"

// The fields used here.
constexpr uint32_t kFreeRevertsPending = 0x318;
constexpr uint32_t kFreeRevertStart = 0x31C;
constexpr uint32_t kFocusHeld = 0x14C1;
constexpr uint32_t kThrowWait = 0x14C4;               // frames until the next throw
constexpr uint32_t kNoThrow = 0x14C8;
constexpr uint32_t kProjectiles = 0x2F4;              // (on)
constexpr uint32_t kProjectileCount = 0x2F8;
constexpr uint32_t kProjectileComponent = 0x1538;     // (+0x18, +0x1C, +0x20: what stops a throw)
constexpr uint32_t kModelRotating = 0x2DB;
constexpr uint32_t kModelFlipped = 0x2DC;
constexpr uint32_t kModelRotationLeft = 0x2E0;
constexpr uint32_t kModelRotationFlag = 0x2E4;
constexpr uint32_t kModelRotationStep = 0x2E8;
constexpr uint32_t kModelRotationEnding = 0x3A4;
constexpr uint32_t kSpecialTimed = 0x301;
constexpr uint32_t kTakeOffHeight = 0x11C;            // (in the flags block)
constexpr uint32_t kLastGroundNormal = 0x58;
constexpr uint32_t kCollisionCache = 0x4F4;           // (Nx::CCollCache)

// The game's constants.
constexpr float kMillisecondsPerSecond = 1000.0f;     // 0x006453C0
constexpr float kModelRotationEnd = 20.0f;            // 0x006486AC
constexpr float kNeverLands = 0.00001f;               // 0x006453B8 (as a float: 0x3727C5AC)
constexpr float kCacheReach = 180.0f;                 // 0x0064B278: the collision cache's box, along x and z
constexpr float kCacheBelow = 204.0f;                 // 0x0064BCD0
constexpr float kCacheAbove = 96.0f;                  // 0x0064B808

Obj::CCompositeObject* SkaterObject(void* component) { return static_cast<Obj::CCompositeObject*>(ObjectOf(component)); }

} // namespace

// 0x00505440
void* __cdecl CorePhysicsComponent::Create()
{
    void* memory = Mem::Calloc(0x153C);
    return memory ? static_cast<CorePhysicsComponent*>(memory)->Construct() : nullptr;
}

// 0x00500CA0
CorePhysicsComponent* CorePhysicsComponent::Construct()
{
    reinterpret_cast<Obj::CBaseComponent*>(this)->Construct();
    At<void**>(this, 0) = g_corePhysicsVtable;
    reinterpret_cast<Nx::CFeeler*>(&At<uint8_t>(this, kFeeler))->Construct();
    reinterpret_cast<Nx::CFeeler*>(&At<uint8_t>(this, kLastGroundFeeler))->Construct();
    At<uint32_t>(this, 0x368) = 0;
    At<uint32_t>(this, 0x36C) = 0;
    At<uint32_t>(this, 0x364) = 0;
    // (+0x3F0: its constructor 0x005902E0 only returns it.)
    reinterpret_cast<Nx::CCollCache*>(&At<uint8_t>(this, 0x4F4))->Construct();
    for (uint32_t component = kInput; component <= 0x1538; component += 4)
        At<void*>(this, component) = nullptr;
    At<uint8_t>(this, kSpecialTimed) = 0;
    At<uint8_t>(this, 0x2D8) = 0;
    At<uint8_t>(this, kModelRotationFlag) = 0;
    At<uint8_t>(this, 0x2ED) = 0;
    At<uint8_t>(this, 0x304) = 0;
    At<uint32_t>(this, 4) = kSkaterCorePhysics;
    At<int>(this, 0x308) = -1;
    At<float>(this, kModelRotationStep) = 1.0f;
    // Not original - a component made where an old one was starts off THAW's bike (game/skater/thaw_bmx.h).
    if (ThawBmx::Enabled())
        ThawBmx::Forget(this);
    return this;
}

// 0x00500DB0
CorePhysicsComponent* CorePhysicsComponent::Destroy(unsigned int flags)
{
    CorePhysicsComponent::DestroyBody(); // (the destructor body)
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00506BE0
void CorePhysicsComponent::Reset()
{
    auto* gameNet = GameNet::g_gameNetManager;
    if (!gameNet->InNetGame()) {
        // (Off line: the old position, and the one kept at +0x98, are where the skater is now.)
        VectorAt(ObjectOf(this), kOldPosition) = VectorAt(ObjectOf(this), kPosition);
        VectorAt(this, kSafePosition) = VectorAt(ObjectOf(this), kPosition);
    }
    VectorAt(this, 0x258) = {0.0f, 0.0f, 0.0f, 1.0f};
    At<int>(this, kFrameStart) = Now();
    VectorAt(ObjectOf(this), kVelocity) = {0.0f, 0.0f, 0.0f, 1.0f};
    Physics::SetState(this, 0);
    At<uint32_t>(this, 0x94) = 0;
    // Every timed flag (23 down to 0) set, then cleared; flag 7 tells +0x1514's +0x1E each time.
    for (int flag = 0x17; flag >= 0; --flag) {
        uint32_t offset = 0x20 + flag * 8;
        SetTimedFlag(this, offset, 1);
        if (flag == 7)
            At<uint8_t>(At<void*>(this, kOtherComponent), 0x1E) = 1;
        SetTimedFlag(this, offset, 0);
        if (flag == 7)
            At<uint8_t>(At<void*>(this, kOtherComponent), 0x1E) = 0;
    }
    SetTimedFlag(this, 0x28, At<uint8_t>(ObjectOf(this), 0x110) == 0);
    // The ride let go of: its object kept at +0x6C, the smart pointer freed, the time noted.
    uint8_t* ride = At<uint8_t*>(this, kRide);
    auto* rider = At<Obj::CSmtPtr<Obj::CRefCounted>*>(ride, 0x18);
    if (rider) {
        At<void*>(ride, 0x6C) = rider->mp_object;
        rider->Release();
        Mem::Manager::Get()->Free(rider);
        At<void*>(ride, 0x18) = nullptr;
        At<int>(ride, 0x70) = Now();
    }
    At<uint8_t>(this, 0x1C5) = 0;
    At<uint32_t>(this, 0x2C0) = 0;
    At<uint32_t>(this, 0x378) = 0;
    At<uint32_t>(this, 0x39C) = 0;
    At<uint32_t>(this, 0x3A0) = 0;
    At<uint8_t>(this, 0x370) = 0;
    At<uint8_t>(this, 0x371) = 0;
    At<uint8_t>(this, 0x310) = 0;
    At<uint8_t>(this, 0x311) = 1;
    FlagsOf(this)[0xF4] = 0;
    At<uint32_t>(this, 0x22C) = 0;
    if (At<uint32_t>(this, 0x350) != 0xC0000000u) // (-2: kept)
        At<uint32_t>(this, 0x350) = 0;
    At<uint32_t>(this, 0x4E0) = 0;
    At<uint32_t>(this, 0x4E4) = 0;
    At<uint32_t>(this, 0x4EC) = 0;
    At<uint32_t>(this, 0x320) = 0;
    At<uint8_t>(this, 0x4E8) = 0;
    At<float>(this, 0x3AC) = 1.0f;
}

// 0x00506BA0
void CorePhysicsComponent::SetLastGroundFeeler(const void* feeler)
{
    At<Nx::CFeeler>(this, kLastGroundFeeler).Assign(*static_cast<const Nx::CFeeler*>(feeler));
    At<uint8_t>(At<void*>(this, kOtherComponent), 0x1D) = At<uint8_t>(this, 0x1AA);
}

// 0x00506BD0
int CorePhysicsComponent::CancelRotateDisplayandUpdateMatrix(Script::CStruct*, Script::CScript*)
{
    return ApplyModelRotations() ? 1 : 0;
}

// 0x00500670
void CorePhysicsComponent::UpdateCollisionCache()
{
    const Mth::Vector& position = VectorAt(ObjectOf(this), kPosition);
    Mth::BBox box;
    box.min = {position.x - kCacheReach, position.y - kCacheBelow, position.z - kCacheReach, position.w};
    box.max = {position.x + kCacheReach, position.y + kCacheAbove, position.z + kCacheReach, position.w};
    Nx::CCollCache& cache = At<Nx::CCollCache>(this, kCollisionCache);
    cache.Update(box);
    Nx::g_currentCache = &cache; // the feelers' current cache
}

// 0x00500710
void CorePhysicsComponent::UpdateFreeRevert()
{
    if (!At<int>(this, kFreeRevertsPending))
        return;
    uint32_t start = At<uint32_t>(this, kFreeRevertStart);
    uint32_t wait = Ftol(static_cast<double>(Script::GetFloat(kTimeBeforeFreeRevert)) * kMillisecondsPerSecond);
    if (Since(start) <= wait)
        return;
    --At<int>(this, kFreeRevertsPending);
    Dbg::Printf(kMessageFormat, kSourceFile, 0x293F,
                kFreeRevertText);
    if (At<int>(this, kFreeRevertsPending))
        At<int>(this, kFreeRevertStart) = Now();
}

// 0x005007A0
void CorePhysicsComponent::UpdateFocusButton()
{
    uint8_t* button = InputOf(this) + 0xE0;
    if (At<uint8_t>(this, kFocusHeld)) {
        if (!button[0])
            At<uint8_t>(this, kFocusHeld) = 0;
        return;
    }
    if (!button[0])
        return;
    int wait = Script::GetInteger(kFocusButtonTime);
    if (TimeSincePress(button) > wait) {
        At<uint8_t>(this, kFocusHeld) = 1;
        Script::RunScript(kToggleBulletTime, nullptr, nullptr, false, nullptr);
    }
}

// 0x00500820
void CorePhysicsComponent::UpdateProjectiles()
{
    if (At<uint8_t>(this, kNoThrow) || !At<uint8_t>(this, kProjectiles) || FlagsOf(this)[0x70])
        return;
    uint8_t* other = At<uint8_t*>(this, kProjectileComponent);
    if (other[0x1C] || At<int>(other, 0x18) || !(At<float>(other, 0x20) == 0.0f))
        return;
    int& wait = At<int>(this, kThrowWait);
    if (wait) {
        --wait;
        return;
    }
    if (At<int>(this, kProjectileCount) <= 0)
        return;
    if (!ProjectileButtonPressed() || wait)
        return;
    wait = 30;
    // (0x004FC420: walking.)
    bool walking = static_cast<Obj::CSkaterPhysicsControlComponent*>(At<void*>(this, kStateComponent))->IsWalking();
    Script::RunScript(walking ? kWalkThrow : kSkateThrow, nullptr, SkaterObject(this), false, nullptr);
}

// 0x00500900
void CorePhysicsComponent::UpdateModelRotation()
{
    if (!At<uint8_t>(this, kModelRotating))
        return;
    auto* model = static_cast<Obj::CModelComponent*>(SkaterObject(this)->GetComponent(kModel));
    double left = static_cast<double>(At<float>(this, kModelRotationLeft)) - At<float>(this, kModelRotationStep);
    At<float>(this, kModelRotationLeft) = static_cast<float>(left); // (compared below unrounded)
    auto resetRotations = [model] { model->CancelRotateDisplay(); };
    if (At<int>(FlagsOf(this), kState) != 1) {
        resetRotations();
        At<uint8_t>(this, kModelRotating) = 0;
        At<uint8_t>(this, kModelRotationEnding) = 0;
        At<uint32_t>(this, kModelRotationLeft) = 0;
        At<uint8_t>(this, kModelRotationFlag) = 0;
        return;
    }
    if (left <= kModelRotationEnd)
        At<uint8_t>(this, kModelRotationEnding) = 0;
    if (left <= 0.0) {
        At<uint8_t>(this, kModelRotating) = 0;
        At<uint32_t>(this, kModelRotationLeft) = 0;
        if (At<uint8_t>(this, kModelFlipped)) {
            static_cast<Obj::CSkaterFlipAndRotateComponent*>(SkaterObject(this)->GetComponent(kSkaterFlipAndRotate))->Flip();
        }
        resetRotations();
    }
}

// 0x00500BC0
void CorePhysicsComponent::UpdateSpecialTime()
{
    if (!At<uint8_t>(this, kSpecialTimed))
        return;
    auto* time = static_cast<float*>(static_cast<Obj::CSkater*>(ObjectOf(this))->GetScore()); // (the score)
    if (!time)
        return;
    if (*time > 0.0f) {
        *time = *time - At<float>(this, kFrameLength);
        return;
    }
    Script::CStruct* params = Script::NewStruct();
    params = params ? params->Init() : nullptr;
    params->AddChecksum(kTimeLimit, 1);
    Script::RunScript(kKillSpecial, params, SkaterObject(this), false, nullptr);
    if (params) {
        params->Clear();
        Script::DeleteStruct(params);
    }
}

// 0x00500DE0
int CorePhysicsComponent::GetAirTimeLeft(Script::CStruct*, Script::CScript* script)
{
    void* object = ObjectOf(this);
    float y = VectorAt(object, kPosition).y;
    float drop = static_cast<float>(y - (static_cast<double>(y) - At<float>(FlagsOf(this), kTakeOffHeight)));
    float upward = VectorAt(object, kVelocity).y;
    double down = -Gravity(this);
    // Solving y + upward * t - down * t^2 / 2 = the take-off height.
    double discriminant = static_cast<double>(upward) * upward + 2.0 * (down * drop);
    float left;
    if (discriminant >= 0.0 || discriminant != discriminant) {
        double t = (std::sqrt(discriminant) + upward) / down;
        left = t < 0.0 ? 0.0f : static_cast<float>(t);
    } else {
        left = kNeverLands;
    }
    script->m_params->AddFloat(kAirTimeLeft, left);
    return 1;
}

// 0x00500E80
int CorePhysicsComponent::CanKick(Script::CStruct*, Script::CScript*)
{
    return CanPushOff() ? 1 : 0;
}

// 0x00500E90
int CorePhysicsComponent::OrientToNormal(Script::CStruct*, Script::CScript*)
{
    VectorAt(this, kLastGroundNormal) = VectorAt(ObjectOf(this), kUp);
    VectorAt(this, kGroundNormal) = VectorAt(ObjectOf(this), kUp);
    TurnToNormal(VectorAt(this, kHitNormal));
    return 1;
}

// 0x005004B0
void CorePhysicsComponent::StartGrindTrick(uint32_t railType, bool trick1, bool trick2, bool trick3, bool trick4)
{
    Obj::CCompositeObject* object = SkaterObject(this);
    if (!object->m_script) {
        void* memory = Script::CScript::operator new(sizeof(Script::CScript));
        SkaterObject(this)->m_script = memory ? static_cast<Script::CScript*>(memory)->Construct() : nullptr;
    }
    if (At<uint8_t>(this, kMotoGrind)) {
        object = SkaterObject(this);
        object->m_script->SetScript(kMotoskateboardGrind, nullptr, object);
    } else {
        // GrindTrickList[the rail's type (0-8; anything else 0)][the four trick buttons as bits 0-3].
        uint32_t index = railType <= 8 ? railType : 0;
        auto* tricks = reinterpret_cast<Script::CArray*>(Script::GetArray(kGrindTrickList)->GetElement(index));
        uint32_t buttons = trick1 ? 1 : 0;
        if (trick2)
            buttons |= 2;
        if (trick3)
            buttons |= 4;
        if (trick4)
            buttons |= 8;
        if (ThawBmx::Active(this)) {
            // Not original: THAW 0x005CF2F0 - on the bike BikeGrindTrickList by the stick's pitch (when the overlay
            // has it), the over-grind state.
            uint32_t column = 0;
            if (Script::CArray* row = ThawBmx::GrindTrickRow(this, trick1, trick2, trick3, &column)) {
                tricks = row;
                buttons = column;
            }
        }
        At<uint32_t>(this, 0x3A8) = 0;
        static_cast<Obj::CSkaterFlipAndRotateComponent*>(SkaterObject(this)->GetComponent(kSkaterFlipAndRotate))
            ->DoAfterAnim();
        object = SkaterObject(this);
        object->m_script->SetScript(tricks->GetElement(buttons), nullptr, object);
    }
    SkaterObject(this)->m_script->Update();
    FlagsOf(this)[0xE0] = 1;
}

} // namespace Skater

REPLACE(0x005004B0, &Skater::CorePhysicsComponent::StartGrindTrick);
REPLACE(0x00505440, Skater::CorePhysicsComponent::Create);
REPLACE(0x00500CA0, &Skater::CorePhysicsComponent::Construct);
REPLACE(0x00500DB0, &Skater::CorePhysicsComponent::Destroy);
REPLACE(0x00506BE0, &Skater::CorePhysicsComponent::Reset);
REPLACE(0x00506BA0, &Skater::CorePhysicsComponent::SetLastGroundFeeler);
REPLACE(0x00506BD0, &Skater::CorePhysicsComponent::CancelRotateDisplayandUpdateMatrix);
REPLACE(0x00500670, &Skater::CorePhysicsComponent::UpdateCollisionCache);
REPLACE(0x00500710, &Skater::CorePhysicsComponent::UpdateFreeRevert);
REPLACE(0x005007A0, &Skater::CorePhysicsComponent::UpdateFocusButton);
REPLACE(0x00500820, &Skater::CorePhysicsComponent::UpdateProjectiles);
REPLACE(0x00500900, &Skater::CorePhysicsComponent::UpdateModelRotation);
REPLACE(0x00500BC0, &Skater::CorePhysicsComponent::UpdateSpecialTime);
REPLACE(0x00500DE0, &Skater::CorePhysicsComponent::GetAirTimeLeft);
REPLACE(0x00500E80, &Skater::CorePhysicsComponent::CanKick);
REPLACE(0x00500E90, &Skater::CorePhysicsComponent::OrientToNormal);

namespace Skater {
namespace {


} // namespace
} // namespace Skater

namespace Skater {

GAME_VTABLE(g_corePhysicsVtable, 0x0064BC48,
    &Skater::CorePhysicsComponent::Destroy,
    &Skater::CorePhysicsComponent::Update,
    &CorePhysicsComponent::InitFromStructure,
    &CorePhysicsComponent::Finalize,
    Stubs::DoNothing,
    Obj::RefreshFromStructure,
    Stubs::IgnoreArguments2,
    &Obj::CBaseComponent::Suspend,
    Stubs::IgnoreArgument,
    Stubs::DoNothing,
    Stubs::IgnoreArgument,
    &Obj::CBaseComponent::RunMemberFunction,
    &Skater::CorePhysicsComponent::CallMemberFunction);

} // namespace Skater
