// The skater's state component (GameObjects::CRiderPoseState) and three functions of its state history: see
// skater_state.h.

#include "game/skater/skater_state.h"
#include "game/skater/state_history.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/param_struct.h"
#include "game/core/code_address.h"
#include "game/skater/skitch.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/entity.h"
#include "game/skater/vehicle_sound.h"
#include "game/obj/camera_lookaround.h"
#include "game/skater/state_history.h"

namespace GameObjects {

uint32_t g_powerupNames[1]{0xD039432C};
uint32_t g_skaterStateMemberFunctions[114]{
            0x5EA287F2, 0x43686585, 0, 0, 0, 0,
            0x3527FC07, 0x43686585, 0, 0, 0, 0,
            0xA32C1A15, 0x43686585, 0, 0, 0, 0,
            0x5CB1FBD8, 0x43686585, 0, 0, 0, 0,
            0xE9851E62, 0x43686585, 0, 0, 0, 0,
            0xA64DCF8B, 0x43686585, 0, 0, 0, 0,
            0x1AB2389B, 0x43686585, 0, 0, 0, 0,
            0x57BFBAE8, 0x43686585, 0, 0, 0, 0,
            0xF2813EE5, 0x43686585, 0, 0, 0, 0,
            0xEE584CBC, 0x43686585, 0, 0, 0, 0,
            0xB32E9EAE, 0x43686585, 0, 0, 0, 0,
            0x953A7B6D, 0x43686585, 0, 0, 0, 0,
            0x58AD903F, 0x43686585, 0, 0, 0, 0,
            0xB07AC662, 0x43686585, 0, 0, 0, 0,
            0x0E11B7CA, 0x43686585, 0, 0, 0, 0,
            0x44BA5FCE, 0x43686585, 0, 0, 0, 0,
            0xE0645004, 0x43686585, 0, 0, 0, 0,
            0xEF7A8687, 0x43686585, 0, 0, 0, 0,
            0, 0x43686585, 0, 0, 0, 0};

namespace {

extern void** const kVtable;
constexpr uint32_t kSkaterState = 0x43686585;
constexpr uint32_t kOnSkaterStateChange = 0xA7662059, kOnSkaterPhysicsStateChange = 0x6BAC488C,
                   kOnSkaterTerrainChange = 0xD3FDE02A;
constexpr uint32_t kTerrain = 0x3789AC4E, kSparksPos = 0xFF5318DA;

// The powerups' names (0x0067F09C): one.
constexpr int kPowerups = 1;
const uint32_t* PowerupNames() { return g_powerupNames; }

// The powerup the unnamed checksum names (asserted), or -1.
int FindPowerup(Scripts::CStruct* params)
{
    // (without one the original's variable keeps the params pointer: the stack slot it reuses)
    auto name = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(params));
    params->ReadHashValue(uint32_t{0}, &name, 1);
    for (int i = 0; i < kPowerups; ++i) {
        if (PowerupNames()[i] == name)
            return i;
    }
    return -1;
}

} // namespace

// 0x00519740
CObjectModule* __cdecl CRiderPoseState::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CRiderPoseState));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CRiderPoseState*>(memory);
    component->CObjectModule::MakeNew();
    component->m_vtable = kVtable;
    component->m_kindTag = kSkaterState;
    component->m_boostItems[0] = 0;
    return component;
}

// 0x00519430
void __cdecl CRiderPoseState::PublishMethodTable()
{
    Scripts::RegisterMemberFunctionTable(reinterpret_cast<void*>(kSkaterState), g_skaterStateMemberFunctions,
                                        0x18);
}

// 0x00519450
void CRiderPoseState::DestroyBody()
{
    m_vtable = kVtable;
    CObjectModule::DestroyBody();
}

// 0x00519620
CRiderPoseState* CRiderPoseState::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00519460
void CRiderPoseState::ConfigureFromData(Scripts::CStruct* /*params*/) { m_curPhase = AIR; }

// 0x00519660
void CRiderPoseState::ChangePhase(int state)
{
    if (state == m_curPhase)
        return;
    m_curPhase = state;
    m_entity->PostOwnEvent(kOnSkaterStateChange, nullptr);
}

// 0x00519680
void CRiderPoseState::SetPhysicsMode(int mode)
{
    if (mode == m_physicsMode)
        return;
    m_physicsMode = mode;
    m_entity->PostOwnEvent(kOnSkaterPhysicsStateChange, nullptr);
}

// 0x005196A0
void CRiderPoseState::SetSurfaceKind(uint8_t terrain)
{
    if (terrain == m_surfaceType)
        return;
    m_surfaceType = terrain;
    m_entity->PostOwnEvent(kOnSkaterTerrainChange, nullptr);
}

// 0x00519470
void CRiderPoseState::ClearPowerups() { m_boostItems[0] = 0; }

// 0x00519650
void CRiderPoseState::ClearDoingTrick() { m_inTrickMove = 0; }

// 0x00519480
bool CRiderPoseState::InAir(Scripts::CStruct*, Scripts::CScript*) { return m_curPhase == AIR; }
// 0x00519490
bool CRiderPoseState::OnWall(Scripts::CStruct*, Scripts::CScript*) { return m_curPhase == WALL; }
// 0x005194A0
bool CRiderPoseState::OnLip(Scripts::CStruct*, Scripts::CScript*) { return m_curPhase == LIP; }
// 0x005194B0
bool CRiderPoseState::OnRail(Scripts::CStruct*, Scripts::CScript*) { return m_curPhase == RAIL; }
// 0x005194C0
bool CRiderPoseState::InWallplant(Scripts::CStruct*, Scripts::CScript*) { return m_curPhase == WALLPLANT; }
// 0x005196D0
bool CRiderPoseState::InBailState(Scripts::CStruct*, Scripts::CScript*) { return m_bailing != 0; }
// 0x005194D0
bool CRiderPoseState::OnFoot(Scripts::CStruct*, Scripts::CScript*)
{
    return !m_inVehicle && m_physicsMode == WALKING;
}
// 0x005194F0
bool CRiderPoseState::Skating(Scripts::CStruct*, Scripts::CScript*)
{
    return !m_inVehicle && m_physicsMode == SKATING;
}
// 0x00519510
bool CRiderPoseState::Driving(Scripts::CStruct*, Scripts::CScript*) { return m_inVehicle != 0; }
// 0x00519520
bool CRiderPoseState::SetDriving(Scripts::CStruct*, Scripts::CScript*)
{
    m_inVehicle = 1;
    return true;
}
// 0x00519530
bool CRiderPoseState::UnsetDriving(Scripts::CStruct*, Scripts::CScript*)
{
    m_inVehicle = 0;
    return true;
}
// 0x005196E0
bool CRiderPoseState::InAirMove(Scripts::CStruct*, Scripts::CScript*) { return m_inTrickMove != 0; }

// 0x00519540
bool CRiderPoseState::HasPowerup(Scripts::CStruct* params, Scripts::CScript*)
{
    int index = FindPowerup(params);
    return index != -1 && m_boostItems[index] != 0;
}

// 0x005195B0
bool CRiderPoseState::PickedUpPowerup(Scripts::CStruct* params, Scripts::CScript*)
{
    int index = FindPowerup(params);
    if (index != -1)
        m_boostItems[index] = 1;
    return true;
}

// 0x005196F0
bool CRiderPoseState::GetSurfaceKind(Scripts::CStruct*, Scripts::CScript* script)
{
    script->m_args->PutInt(kTerrain, m_surfaceType);
    return true;
}

// 0x00519720
bool CRiderPoseState::GetSparksPos(Scripts::CStruct*, Scripts::CScript* script)
{
    script->m_args->PutNameKey(kSparksPos, m_sparksPos);
    return true;
}

// 0x00519600
bool CRiderPoseState::SetSparksPos(Scripts::CStruct* params, Scripts::CScript*)
{
    params->ReadHashValue(uint32_t{0}, &m_sparksPos, 0);
    return true;
}

// 0x005197D0
uint32_t SkaterStateHistoryFrame::GetTime()
{
    auto* self = reinterpret_cast<uint8_t*>(this);
    return static_cast<uint32_t>(*reinterpret_cast<uint16_t*>(self + 0x7A)) << 16 |
           *reinterpret_cast<uint16_t*>(self + 0x78);
}

// 0x005197E0
void SkaterStateHistoryFrame::ChangeMoment(uint32_t time)
{
    auto* self = reinterpret_cast<uint8_t*>(this);
    *reinterpret_cast<uint16_t*>(self + 0x78) = static_cast<uint16_t>(time);
    *reinterpret_cast<uint16_t*>(self + 0x7A) = static_cast<uint16_t>(time >> 16);
}

// 0x005197C0
void CRiderFrameLog::DestroyBody()
{
    m_vtable = g_stateHistoryVtable;
    CObjectModule::DestroyBody();
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CRiderPoseState::Teardown,
         &GameObjects::ComponentWithoutUpdate::TickFrame,
         &GameObjects::CRiderPoseState::ConfigureFromData,
         Stubs::DoNothing,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         &GameObjects::CObjectModule::DispatchScriptMethod,
         &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj

namespace GameObjects {

static VtableSlot g_stateHistoryVtable_slots[] = {&GameObjects::CRiderFrameLog::Teardown,
         &GameObjects::ComponentWithoutUpdate::TickFrame,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName};
void** const g_stateHistoryVtable = reinterpret_cast<void**>(g_stateHistoryVtable_slots);

} // namespace Obj
