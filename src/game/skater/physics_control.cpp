// The skater's physics control component (GameObjects::CRiderBoardPhysics): see physics_control.h.

#include "game/skater/physics_control.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/maths/mat4.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/param_struct.h"
#include "game/skater/core_physics.h"
#include "game/skater/rider.h"
#include "game/skater/skater_state.h"
#include "game/skater/walk_physics.h"
#include "game/sys/game_clock.h"
#include "game/skater/skater_camera_component.h"
#include "game/skater/walk_camera.h"
#include "game/obj/control_pad.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/entity.h"
#include "game/obj/camera_lookaround.h"

namespace GameObjects {

uint32_t g_physicsControlMemberFunctions[66]{
            0x3522D612, 0xD9151D4D, 0, 0, 0, 0,
            0x0595627C, 0xD9151D4D, 0, 0, 0, 0,
            0x9569D5B1, 0xD9151D4D, 0, 0, 0, 0,
            0xA887285A, 0xD9151D4D, 0, 0, 0, 0,
            0x9366A509, 0xD9151D4D, 0, 0, 0, 0,
            0x04830E80, 0xD9151D4D, 0, 0, 0, 0,
            0xD5A9F889, 0xD9151D4D, 0, 0, 0, 0,
            0x27FDDEB7, 0xD9151D4D, 0, 0, 0, 0,
            0x174A6AD9, 0xD9151D4D, 0, 0, 0, 0,
            0x246322C9, 0xD9151D4D, 0, 0, 0, 0,
            0, 0xD9151D4D, 0, 0, 0, 0};

namespace {

extern void** const kVtable;
constexpr uint32_t kSkaterPhysicsControl = 0xD9151D4D;
constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29, kSkaterState = 0x43686585, kSkaterRotate = 0x52D61A0D,
                   kSkaterAdjustPhysics = 0x9F9CC949, kSkaterFinalizePhysics = 0x9B373E58, kWalk = 0x726E85AA,
                   kSkaterCamera = 0x5E43A604, kWalkCamera = 0xC7D37EC2;
constexpr uint32_t kSwitchWalkingToSkating = 0x5C038F9B, kSwitchSkatingToWalking = 0x6E8E39B3;
constexpr uint32_t kTimeSincePhysicsSwitch = 0x27F48E93, kPreviousPhysicsStateDuration = 0x24D061BA;

constexpr int kSkating = 0, kWalking = 1; // the state component's physics mode (+0x1C)
constexpr float kSecondsPerMs = 0.001f;   // 0x006461D8

template <typename T>
T& Prop(void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

int Clock() { return FrameClock::GameClockElapsed() + g_clockOffset; }

// Milliseconds as unsigned seconds (the original's unsigned conversion through the x87), rounded once to float.
float Seconds(int ms) { return static_cast<float>(static_cast<double>(static_cast<uint32_t>(ms)) * kSecondsPerMs); }

void SuspendComponent(CObjectModule* component, bool suspend) { component->VirtualSuspend(suspend); }

// Calls into the other components: the core physics told it's leaving (0x004FDD50, not rewritten yet) / given the
// walk's state on coming back (ResumeSkating), the state component's physics mode (1: walking), the walk component
// told it's starting (Begin) / leaving (its model offset cleared), the cameras' state handed over (the skater
// camera's 0x004FA420 / 0x004F9A40, the walk camera's 0x00524FF0 / 0x00525060).
void CoreLeaving(uint8_t* core) { reinterpret_cast<Skater::CorePhysicsComponent*>(core)->LeaveSkating(); }
void CoreReturning(uint8_t* core, bool onGround, void* hold1, void* hold2)
{
    reinterpret_cast<Skater::CorePhysicsComponent*>(core)->ResumeSkating(onGround, static_cast<const uint8_t*>(hold1),
                                                                         hold2);
}
void SetWalking(uint8_t* state, bool walking)
{
    reinterpret_cast<CRiderPoseState*>(state)->SetPhysicsMode(walking ? 1 : 0);
}
void WalkStarting(CObjectModule* walk, bool fromGround) { static_cast<CFootMovement*>(walk)->Begin(fromGround); }
void WalkLeaving(CObjectModule* walk) { static_cast<CFootMovement*>(walk)->ClearModelOffset(); }
// The skater camera's 0x004FA420 (save) / 0x004F9A40 (set), the walk camera's 0x00525060 (save) / 0x00524FF0 (restore).
void SkaterCameraSave(CObjectModule* camera, void* state)
{
    static_cast<CRiderChaseView*>(camera)->SaveState(static_cast<WalkCameraState*>(state));
}
void SkaterCameraSet(CObjectModule* camera, void* state)
{
    static_cast<CRiderChaseView*>(camera)->ChangePhase(static_cast<const WalkCameraState*>(state));
}
void WalkCameraSave(CObjectModule* camera, void* state)
{
    static_cast<CFootView*>(camera)->SaveState(static_cast<WalkCameraState*>(state));
}
void WalkCameraRestore(CObjectModule* camera, void* state)
{
    static_cast<CFootView*>(camera)->RestoreState(static_cast<const WalkCameraState*>(state));
}

} // namespace

// 0x00517830
CObjectModule* __cdecl CRiderBoardPhysics::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CRiderBoardPhysics));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CRiderBoardPhysics*>(memory);
    component->CObjectModule::MakeNew();
    component->m_vtable = kVtable;
    component->m_kindTag = kSkaterPhysicsControl;
    component->m_corePhysics = nullptr;
    return component;
}

// 0x005176A0
void __cdecl CRiderBoardPhysics::PublishMethodTable()
{
    Scripts::RegisterMemberFunctionTable(reinterpret_cast<void*>(kSkaterPhysicsControl),
                                        g_physicsControlMemberFunctions, 0x18);
}

// 0x005176C0
void CRiderBoardPhysics::DestroyBody()
{
    m_vtable = kVtable;
    CObjectModule::DestroyBody();
}

// 0x00517740
CRiderBoardPhysics* CRiderBoardPhysics::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x005176D0
void CRiderBoardPhysics::ConfigureFromData(Scripts::CStruct* /*params*/) { m_isHalted = false; }

// 0x00517770
void CRiderBoardPhysics::Seal()
{
    m_corePhysics = reinterpret_cast<uint8_t*>(m_entity->FetchModule(kSkaterCorePhysics));
    m_curPhase = reinterpret_cast<uint8_t*>(m_entity->FetchModule(kSkaterState));
    Prop<int>(m_curPhase, 0x1C) = -1;
}

// 0x005178B0
void CRiderBoardPhysics::SetFrozen(bool paused)
{
    m_isHalted = paused;
    if (!paused && (m_entity->m_compositeFlags & COMPOSITE_FLAG_SUSPENDED))
        return;
    switch (Prop<int>(m_curPhase, 0x1C)) {
    case kSkating:
        SuspendComponent(reinterpret_cast<CObjectModule*>(m_corePhysics), paused);
        SuspendComponent(m_entity->FetchModule(kSkaterRotate), paused);
        SuspendComponent(m_entity->FetchModule(kSkaterAdjustPhysics), paused);
        SuspendComponent(m_entity->FetchModule(kSkaterFinalizePhysics), paused);
        break;
    case kWalking:
        SuspendComponent(m_entity->FetchModule(kWalk), paused);
        break;
    default:
        break;
    }
}

// 0x00517940
void CRiderBoardPhysics::SwitchSkatingToWalking()
{
    if (Prop<int>(m_curPhase, 0x1C) == kWalking)
        return;
    m_previousDuration = Clock() - m_switchTime;
    m_switchTime = Clock();
    CObjectModule* walk = m_entity->FetchModule(kWalk);
    auto* camera = static_cast<CGameEntity*>(static_cast<CRider*>(m_entity)->GetSkaterCamera());
    CObjectModule* skaterCamera = camera->FetchModule(kSkaterCamera);
    CObjectModule* walkCamera = camera->FetchModule(kWalkCamera);
    int skaterState = Prop<int>(Prop<uint8_t*>(m_corePhysics, 0x1528), 0x18);
    CoreLeaving(m_corePhysics);
    SuspendComponent(reinterpret_cast<CObjectModule*>(m_corePhysics), true);
    SuspendComponent(m_entity->FetchModule(kSkaterRotate), true);
    SuspendComponent(m_entity->FetchModule(kSkaterAdjustPhysics), true);
    SuspendComponent(m_entity->FetchModule(kSkaterFinalizePhysics), true);
    SuspendComponent(skaterCamera, true);
    SetWalking(m_curPhase, true);
    SuspendComponent(walk, false);
    WalkStarting(walk, skaterState == 0);
    SuspendComponent(walkCamera, false);
    alignas(16) uint8_t cameraState[0x60];
    SkaterCameraSave(skaterCamera, cameraState);
    WalkCameraRestore(walkCamera, cameraState);
    if (m_isHalted)
        SetFrozen(m_isHalted);
}

// 0x00517B20
void CRiderBoardPhysics::SwitchWalkingToSkating()
{
    if (Prop<int>(m_curPhase, 0x1C) == kSkating)
        return;
    m_previousDuration = Clock() - m_switchTime;
    m_switchTime = Clock();
    CObjectModule* walk = m_entity->FetchModule(kWalk);
    auto* camera = static_cast<CGameEntity*>(static_cast<CRider*>(m_entity)->GetSkaterCamera());
    CObjectModule* skaterCamera = camera->FetchModule(kSkaterCamera);
    CObjectModule* walkCamera = camera->FetchModule(kWalkCamera);
    WalkLeaving(walk);
    SuspendComponent(walk, true);
    SuspendComponent(walkCamera, true);
    SetWalking(m_curPhase, false);
    SuspendComponent(reinterpret_cast<CObjectModule*>(m_corePhysics), false);
    // The walk's ground state (+0x18 0) and what it holds (+0x748: 1 - its +0x74C -, or 2 - its +0x768).
    int hold = Prop<int>(walk, 0x748);
    void* hold2 = hold == 2 ? reinterpret_cast<uint8_t*>(walk) + 0x768 : nullptr;
    void* hold1 = hold == 1 ? reinterpret_cast<uint8_t*>(walk) + 0x74C : nullptr;
    CoreReturning(m_corePhysics, Prop<int>(walk, 0x18) == 0, hold1, hold2);
    SuspendComponent(m_entity->FetchModule(kSkaterRotate), false);
    SuspendComponent(m_entity->FetchModule(kSkaterAdjustPhysics), false);
    SuspendComponent(m_entity->FetchModule(kSkaterFinalizePhysics), false);
    SuspendComponent(skaterCamera, false);
    alignas(16) uint8_t cameraState[0x60];
    WalkCameraSave(walkCamera, cameraState);
    SkaterCameraSet(skaterCamera, cameraState);
    if (m_isHalted)
        SetFrozen(m_isHalted);
}

// 0x00517AC0
void CRiderBoardPhysics::TickFrame()
{
    m_flag26 = 0;
    if (!m_isHalted)
        return;
    switch (Prop<int>(m_curPhase, 0x1C)) {
    case kSkating: m_entity->m_displayMatrix = Prop<Maths::Mat4>(m_corePhysics, 0x18); break;
    case kWalking: m_entity->m_displayMatrix = m_entity->m_basisFrame; break;
    default: break;
    }
    if (!(reinterpret_cast<CObjectModule*>(m_corePhysics)->m_flagBits & COMPONENT_FLAG_SUSPENDED))
        SetFrozen(true);
}

// 0x00517C80
int CRiderBoardPhysics::InvokeMethodByName(uint32_t name, Scripts::CStruct* /*params*/,
                                                       Scripts::CScript* /*script*/)
{
    switch (name) {
    case kSwitchWalkingToSkating: SwitchWalkingToSkating(); return 1;
    case kSwitchSkatingToWalking: SwitchSkatingToWalking(); return 1;
    default: return 2;
    }
}

// 0x00517A80
bool CRiderBoardPhysics::PausePhysics(Scripts::CStruct*, Scripts::CScript*)
{
    if (!m_isHalted)
        SetFrozen(true);
    return true;
}
// 0x00517AA0
bool CRiderBoardPhysics::UnPausePhysics(Scripts::CStruct*, Scripts::CScript*)
{
    if (m_isHalted)
        SetFrozen(false);
    return true;
}
// 0x005177A0
bool CRiderBoardPhysics::GetTimeSincePhysicsSwitch(Scripts::CStruct*, Scripts::CScript* script)
{
    script->m_args->PushFloat(kTimeSincePhysicsSwitch, Seconds(Clock() - m_switchTime));
    return true;
}
// 0x005177F0
bool CRiderBoardPhysics::GetPreviousPhysicsStateDuration(Scripts::CStruct*, Scripts::CScript* script)
{
    script->m_args->PushFloat(kPreviousPhysicsStateDuration, Seconds(m_previousDuration));
    return true;
}
// 0x005176E0
bool CRiderBoardPhysics::SetBoardMissing(Scripts::CStruct*, Scripts::CScript*)
{
    m_boardMissing = true;
    return true;
}
// 0x005176F0
bool CRiderBoardPhysics::UnsetBoardMissing(Scripts::CStruct*, Scripts::CScript*)
{
    m_boardMissing = false;
    return true;
}
// 0x00517700
bool CRiderBoardPhysics::IsBoardMissing(Scripts::CStruct*, Scripts::CScript*) { return m_boardMissing; }
// 0x00517710
bool CRiderBoardPhysics::SetForceWalk(Scripts::CStruct*, Scripts::CScript*)
{
    m_boardMissing = true;
    m_forceWalk = true;
    return true;
}
// 0x00517720
bool CRiderBoardPhysics::UnSetForceWalk(Scripts::CStruct*, Scripts::CScript*)
{
    m_boardMissing = false;
    m_forceWalk = false;
    return true;
}
// 0x00517730
bool CRiderBoardPhysics::IsForceWalkOn(Scripts::CStruct*, Scripts::CScript*) { return m_forceWalk; }

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CRiderBoardPhysics::Teardown,
         &GameObjects::CRiderBoardPhysics::TickFrame,
         &GameObjects::CRiderBoardPhysics::ConfigureFromData,
         &GameObjects::CRiderBoardPhysics::Seal,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         &GameObjects::CObjectModule::RunMemberFunction,
         &GameObjects::CRiderBoardPhysics::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
