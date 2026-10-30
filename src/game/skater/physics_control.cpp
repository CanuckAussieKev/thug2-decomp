// The skater's physics control component (Obj::CSkaterPhysicsControlComponent): see physics_control.h.

#include "game/skater/physics_control.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/matrix.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/skater/core_physics.h"
#include "game/skater/skater.h"
#include "game/skater/skater_state.h"
#include "game/skater/walk_physics.h"
#include "game/sys/game_clock.h"
#include "game/skater/skater_camera_component.h"
#include "game/skater/walk_camera.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

void** const kVtable = reinterpret_cast<void**>(0x0064BFE0);
constexpr uint32_t kSkaterPhysicsControl = 0xD9151D4D;
constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29, kSkaterState = 0x43686585, kSkaterRotate = 0x52D61A0D,
                   kSkaterAdjustPhysics = 0x9F9CC949, kSkaterFinalizePhysics = 0x9B373E58, kWalk = 0x726E85AA,
                   kSkaterCamera = 0x5E43A604, kWalkCamera = 0xC7D37EC2;
constexpr uint32_t kSwitchWalkingToSkating = 0x5C038F9B, kSwitchSkatingToWalking = 0x6E8E39B3;
constexpr uint32_t kTimeSincePhysicsSwitch = 0x27F48E93, kPreviousPhysicsStateDuration = 0x24D061BA;

constexpr int kSkating = 0, kWalking = 1; // the state component's physics mode (+0x1C)
constexpr float kSecondsPerMs = 0.001f;   // 0x006461D8

template <typename T>
T& Field(void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

int Clock() { return Tmr::GameClockElapsed() + GameVariable<int>(0x00696D60); }

// Milliseconds as unsigned seconds (the original's unsigned conversion through the x87), rounded once to float.
float Seconds(int ms) { return static_cast<float>(static_cast<double>(static_cast<uint32_t>(ms)) * kSecondsPerMs); }

void SuspendComponent(CBaseComponent* component, bool suspend) { component->VirtualSuspend(suspend); }

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
    reinterpret_cast<CSkaterStateComponent*>(state)->SetPhysicsMode(walking ? 1 : 0);
}
void WalkStarting(CBaseComponent* walk, bool fromGround) { static_cast<CWalkComponent*>(walk)->Begin(fromGround); }
void WalkLeaving(CBaseComponent* walk) { static_cast<CWalkComponent*>(walk)->ClearModelOffset(); }
// The skater camera's 0x004FA420 (save) / 0x004F9A40 (set), the walk camera's 0x00525060 (save) / 0x00524FF0 (restore).
void SkaterCameraSave(CBaseComponent* camera, void* state)
{
    static_cast<CSkaterCameraComponent*>(camera)->SaveState(static_cast<WalkCameraState*>(state));
}
void SkaterCameraSet(CBaseComponent* camera, void* state)
{
    static_cast<CSkaterCameraComponent*>(camera)->SetState(static_cast<const WalkCameraState*>(state));
}
void WalkCameraSave(CBaseComponent* camera, void* state)
{
    static_cast<CWalkCameraComponent*>(camera)->SaveState(static_cast<WalkCameraState*>(state));
}
void WalkCameraRestore(CBaseComponent* camera, void* state)
{
    static_cast<CWalkCameraComponent*>(camera)->RestoreState(static_cast<const WalkCameraState*>(state));
}

} // namespace

// 0x00517830
CBaseComponent* __cdecl CSkaterPhysicsControlComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CSkaterPhysicsControlComponent));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CSkaterPhysicsControlComponent*>(memory);
    component->CBaseComponent::Construct();
    component->m_vtable = kVtable;
    component->m_type = kSkaterPhysicsControl;
    component->m_corePhysics = nullptr;
    return component;
}

// 0x005176A0
void __cdecl CSkaterPhysicsControlComponent::RegisterMemberFunctions()
{
    Script::RegisterMemberFunctionTable(reinterpret_cast<void*>(kSkaterPhysicsControl),
                                        &GameVariable<uint32_t>(0x006846F0), 0x18);
}

// 0x005176C0
void CSkaterPhysicsControlComponent::DestroyBody()
{
    m_vtable = kVtable;
    CBaseComponent::DestroyBody();
}

// 0x00517740
CSkaterPhysicsControlComponent* CSkaterPhysicsControlComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x005176D0
void CSkaterPhysicsControlComponent::InitFromStructure(Script::CStruct* /*params*/) { m_paused = false; }

// 0x00517770
void CSkaterPhysicsControlComponent::Finalize()
{
    m_corePhysics = reinterpret_cast<uint8_t*>(m_object->GetComponent(kSkaterCorePhysics));
    m_state = reinterpret_cast<uint8_t*>(m_object->GetComponent(kSkaterState));
    Field<int>(m_state, 0x1C) = -1;
}

// 0x005178B0
void CSkaterPhysicsControlComponent::SetPaused(bool paused)
{
    m_paused = paused;
    if (!paused && (m_object->m_compositeFlags & COMPOSITE_FLAG_SUSPENDED))
        return;
    switch (Field<int>(m_state, 0x1C)) {
    case kSkating:
        SuspendComponent(reinterpret_cast<CBaseComponent*>(m_corePhysics), paused);
        SuspendComponent(m_object->GetComponent(kSkaterRotate), paused);
        SuspendComponent(m_object->GetComponent(kSkaterAdjustPhysics), paused);
        SuspendComponent(m_object->GetComponent(kSkaterFinalizePhysics), paused);
        break;
    case kWalking:
        SuspendComponent(m_object->GetComponent(kWalk), paused);
        break;
    default:
        break;
    }
}

// 0x00517940
void CSkaterPhysicsControlComponent::SwitchSkatingToWalking()
{
    if (Field<int>(m_state, 0x1C) == kWalking)
        return;
    m_previousDuration = Clock() - m_switchTime;
    m_switchTime = Clock();
    CBaseComponent* walk = m_object->GetComponent(kWalk);
    auto* camera = static_cast<CCompositeObject*>(static_cast<CSkater*>(m_object)->GetSkaterCamera());
    CBaseComponent* skaterCamera = camera->GetComponent(kSkaterCamera);
    CBaseComponent* walkCamera = camera->GetComponent(kWalkCamera);
    int skaterState = Field<int>(Field<uint8_t*>(m_corePhysics, 0x1528), 0x18);
    CoreLeaving(m_corePhysics);
    SuspendComponent(reinterpret_cast<CBaseComponent*>(m_corePhysics), true);
    SuspendComponent(m_object->GetComponent(kSkaterRotate), true);
    SuspendComponent(m_object->GetComponent(kSkaterAdjustPhysics), true);
    SuspendComponent(m_object->GetComponent(kSkaterFinalizePhysics), true);
    SuspendComponent(skaterCamera, true);
    SetWalking(m_state, true);
    SuspendComponent(walk, false);
    WalkStarting(walk, skaterState == 0);
    SuspendComponent(walkCamera, false);
    alignas(16) uint8_t cameraState[0x60];
    SkaterCameraSave(skaterCamera, cameraState);
    WalkCameraRestore(walkCamera, cameraState);
    if (m_paused)
        SetPaused(m_paused);
}

// 0x00517B20
void CSkaterPhysicsControlComponent::SwitchWalkingToSkating()
{
    if (Field<int>(m_state, 0x1C) == kSkating)
        return;
    m_previousDuration = Clock() - m_switchTime;
    m_switchTime = Clock();
    CBaseComponent* walk = m_object->GetComponent(kWalk);
    auto* camera = static_cast<CCompositeObject*>(static_cast<CSkater*>(m_object)->GetSkaterCamera());
    CBaseComponent* skaterCamera = camera->GetComponent(kSkaterCamera);
    CBaseComponent* walkCamera = camera->GetComponent(kWalkCamera);
    WalkLeaving(walk);
    SuspendComponent(walk, true);
    SuspendComponent(walkCamera, true);
    SetWalking(m_state, false);
    SuspendComponent(reinterpret_cast<CBaseComponent*>(m_corePhysics), false);
    // The walk's ground state (+0x18 0) and what it holds (+0x748: 1 - its +0x74C -, or 2 - its +0x768).
    int hold = Field<int>(walk, 0x748);
    void* hold2 = hold == 2 ? reinterpret_cast<uint8_t*>(walk) + 0x768 : nullptr;
    void* hold1 = hold == 1 ? reinterpret_cast<uint8_t*>(walk) + 0x74C : nullptr;
    CoreReturning(m_corePhysics, Field<int>(walk, 0x18) == 0, hold1, hold2);
    SuspendComponent(m_object->GetComponent(kSkaterRotate), false);
    SuspendComponent(m_object->GetComponent(kSkaterAdjustPhysics), false);
    SuspendComponent(m_object->GetComponent(kSkaterFinalizePhysics), false);
    SuspendComponent(skaterCamera, false);
    alignas(16) uint8_t cameraState[0x60];
    WalkCameraSave(walkCamera, cameraState);
    SkaterCameraSet(skaterCamera, cameraState);
    if (m_paused)
        SetPaused(m_paused);
}

// 0x00517AC0
void CSkaterPhysicsControlComponent::Update()
{
    m_flag26 = 0;
    if (!m_paused)
        return;
    switch (Field<int>(m_state, 0x1C)) {
    case kSkating: m_object->m_displayMatrix = Field<Mth::Matrix>(m_corePhysics, 0x18); break;
    case kWalking: m_object->m_displayMatrix = m_object->m_matrix; break;
    default: break;
    }
    if (!(reinterpret_cast<CBaseComponent*>(m_corePhysics)->m_flags & COMPONENT_FLAG_SUSPENDED))
        SetPaused(true);
}

// 0x00517C80
int CSkaterPhysicsControlComponent::CallMemberFunction(uint32_t name, Script::CStruct* /*params*/,
                                                       Script::CScript* /*script*/)
{
    switch (name) {
    case kSwitchWalkingToSkating: SwitchWalkingToSkating(); return 1;
    case kSwitchSkatingToWalking: SwitchSkatingToWalking(); return 1;
    default: return 2;
    }
}

// 0x00517A80
bool CSkaterPhysicsControlComponent::PausePhysics(Script::CStruct*, Script::CScript*)
{
    if (!m_paused)
        SetPaused(true);
    return true;
}
// 0x00517AA0
bool CSkaterPhysicsControlComponent::UnPausePhysics(Script::CStruct*, Script::CScript*)
{
    if (m_paused)
        SetPaused(false);
    return true;
}
// 0x005177A0
bool CSkaterPhysicsControlComponent::GetTimeSincePhysicsSwitch(Script::CStruct*, Script::CScript* script)
{
    script->m_params->AddFloat(kTimeSincePhysicsSwitch, Seconds(Clock() - m_switchTime));
    return true;
}
// 0x005177F0
bool CSkaterPhysicsControlComponent::GetPreviousPhysicsStateDuration(Script::CStruct*, Script::CScript* script)
{
    script->m_params->AddFloat(kPreviousPhysicsStateDuration, Seconds(m_previousDuration));
    return true;
}
// 0x005176E0
bool CSkaterPhysicsControlComponent::SetBoardMissing(Script::CStruct*, Script::CScript*)
{
    m_boardMissing = true;
    return true;
}
// 0x005176F0
bool CSkaterPhysicsControlComponent::UnsetBoardMissing(Script::CStruct*, Script::CScript*)
{
    m_boardMissing = false;
    return true;
}
// 0x00517700
bool CSkaterPhysicsControlComponent::IsBoardMissing(Script::CStruct*, Script::CScript*) { return m_boardMissing; }
// 0x00517710
bool CSkaterPhysicsControlComponent::SetForceWalk(Script::CStruct*, Script::CScript*)
{
    m_boardMissing = true;
    m_forceWalk = true;
    return true;
}
// 0x00517720
bool CSkaterPhysicsControlComponent::UnSetForceWalk(Script::CStruct*, Script::CScript*)
{
    m_boardMissing = false;
    m_forceWalk = false;
    return true;
}
// 0x00517730
bool CSkaterPhysicsControlComponent::IsForceWalkOn(Script::CStruct*, Script::CScript*) { return m_forceWalk; }

} // namespace Obj

REPLACE(0x00517830, Obj::CSkaterPhysicsControlComponent::Create);
REPLACE(0x005176A0, Obj::CSkaterPhysicsControlComponent::RegisterMemberFunctions);
REPLACE(0x005176C0, &Obj::CSkaterPhysicsControlComponent::DestroyBody);
REPLACE(0x00517740, &Obj::CSkaterPhysicsControlComponent::Destroy);
REPLACE(0x005176D0, &Obj::CSkaterPhysicsControlComponent::InitFromStructure);
REPLACE(0x00517770, &Obj::CSkaterPhysicsControlComponent::Finalize);
REPLACE(0x00517AC0, &Obj::CSkaterPhysicsControlComponent::Update);
REPLACE(0x00517C80, &Obj::CSkaterPhysicsControlComponent::CallMemberFunction);
REPLACE(0x005178B0, &Obj::CSkaterPhysicsControlComponent::SetPaused);
REPLACE(0x00517940, &Obj::CSkaterPhysicsControlComponent::SwitchSkatingToWalking);
REPLACE(0x00517B20, &Obj::CSkaterPhysicsControlComponent::SwitchWalkingToSkating);
REPLACE(0x00517A80, &Obj::CSkaterPhysicsControlComponent::PausePhysics);
REPLACE(0x00517AA0, &Obj::CSkaterPhysicsControlComponent::UnPausePhysics);
REPLACE(0x005177A0, &Obj::CSkaterPhysicsControlComponent::GetTimeSincePhysicsSwitch);
REPLACE(0x005177F0, &Obj::CSkaterPhysicsControlComponent::GetPreviousPhysicsStateDuration);
REPLACE(0x005176E0, &Obj::CSkaterPhysicsControlComponent::SetBoardMissing);
REPLACE(0x005176F0, &Obj::CSkaterPhysicsControlComponent::UnsetBoardMissing);
REPLACE(0x00517700, &Obj::CSkaterPhysicsControlComponent::IsBoardMissing);
REPLACE(0x00517710, &Obj::CSkaterPhysicsControlComponent::SetForceWalk);
REPLACE(0x00517720, &Obj::CSkaterPhysicsControlComponent::UnSetForceWalk);
REPLACE(0x00517730, &Obj::CSkaterPhysicsControlComponent::IsForceWalkOn);
