// The skater's state component (Obj::CSkaterStateComponent) and three functions of its state history: see
// skater_state.h.

#include "game/skater/skater_state.h"
#include "game/skater/state_history.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

void** const kVtable = reinterpret_cast<void**>(0x0064C140);
void** const kStateHistoryVtable = reinterpret_cast<void**>(0x0064C174);
constexpr uint32_t kSkaterState = 0x43686585;
constexpr uint32_t kOnSkaterStateChange = 0xA7662059, kOnSkaterPhysicsStateChange = 0x6BAC488C,
                   kOnSkaterTerrainChange = 0xD3FDE02A;
constexpr uint32_t kTerrain = 0x3789AC4E, kSparksPos = 0xFF5318DA;

// The powerups' names (0x0067F09C): one.
constexpr int kPowerups = 1;
const uint32_t* PowerupNames() { return &GameVariable<uint32_t>(0x0067F09C); }

// The powerup the unnamed checksum names (asserted), or -1.
int FindPowerup(Script::CStruct* params)
{
    // (without one the original's variable keeps the params pointer: the stack slot it reuses)
    auto name = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(params));
    params->GetChecksum(uint32_t{0}, &name, 1);
    for (int i = 0; i < kPowerups; ++i) {
        if (PowerupNames()[i] == name)
            return i;
    }
    return -1;
}

} // namespace

// 0x00519740
CBaseComponent* __cdecl CSkaterStateComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CSkaterStateComponent));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CSkaterStateComponent*>(memory);
    component->CBaseComponent::Construct();
    component->m_vtable = kVtable;
    component->m_type = kSkaterState;
    component->m_powerups[0] = 0;
    return component;
}

// 0x00519430
void __cdecl CSkaterStateComponent::RegisterMemberFunctions()
{
    Script::RegisterMemberFunctionTable(reinterpret_cast<void*>(kSkaterState), &GameVariable<uint32_t>(0x00684A80),
                                        0x18);
}

// 0x00519450
void CSkaterStateComponent::DestroyBody()
{
    m_vtable = kVtable;
    CBaseComponent::DestroyBody();
}

// 0x00519620
CSkaterStateComponent* CSkaterStateComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00519460
void CSkaterStateComponent::InitFromStructure(Script::CStruct* /*params*/) { m_state = AIR; }

// 0x00519660
void CSkaterStateComponent::SetState(int state)
{
    if (state == m_state)
        return;
    m_state = state;
    m_object->SelfEvent(kOnSkaterStateChange, nullptr);
}

// 0x00519680
void CSkaterStateComponent::SetPhysicsMode(int mode)
{
    if (mode == m_physicsMode)
        return;
    m_physicsMode = mode;
    m_object->SelfEvent(kOnSkaterPhysicsStateChange, nullptr);
}

// 0x005196A0
void CSkaterStateComponent::SetTerrain(uint8_t terrain)
{
    if (terrain == m_terrain)
        return;
    m_terrain = terrain;
    m_object->SelfEvent(kOnSkaterTerrainChange, nullptr);
}

// 0x00519470
void CSkaterStateComponent::ClearPowerups() { m_powerups[0] = 0; }

// 0x00519650
void CSkaterStateComponent::ClearDoingTrick() { m_doingTrick = 0; }

// 0x00519480
bool CSkaterStateComponent::InAir(Script::CStruct*, Script::CScript*) { return m_state == AIR; }
// 0x00519490
bool CSkaterStateComponent::OnWall(Script::CStruct*, Script::CScript*) { return m_state == WALL; }
// 0x005194A0
bool CSkaterStateComponent::OnLip(Script::CStruct*, Script::CScript*) { return m_state == LIP; }
// 0x005194B0
bool CSkaterStateComponent::OnRail(Script::CStruct*, Script::CScript*) { return m_state == RAIL; }
// 0x005194C0
bool CSkaterStateComponent::InWallplant(Script::CStruct*, Script::CScript*) { return m_state == WALLPLANT; }
// 0x005196D0
bool CSkaterStateComponent::InBailState(Script::CStruct*, Script::CScript*) { return m_bailing != 0; }
// 0x005194D0
bool CSkaterStateComponent::Walking(Script::CStruct*, Script::CScript*)
{
    return !m_driving && m_physicsMode == WALKING;
}
// 0x005194F0
bool CSkaterStateComponent::Skating(Script::CStruct*, Script::CScript*)
{
    return !m_driving && m_physicsMode == SKATING;
}
// 0x00519510
bool CSkaterStateComponent::Driving(Script::CStruct*, Script::CScript*) { return m_driving != 0; }
// 0x00519520
bool CSkaterStateComponent::SetDriving(Script::CStruct*, Script::CScript*)
{
    m_driving = 1;
    return true;
}
// 0x00519530
bool CSkaterStateComponent::UnsetDriving(Script::CStruct*, Script::CScript*)
{
    m_driving = 0;
    return true;
}
// 0x005196E0
bool CSkaterStateComponent::DoingTrick(Script::CStruct*, Script::CScript*) { return m_doingTrick != 0; }

// 0x00519540
bool CSkaterStateComponent::HasPowerup(Script::CStruct* params, Script::CScript*)
{
    int index = FindPowerup(params);
    return index != -1 && m_powerups[index] != 0;
}

// 0x005195B0
bool CSkaterStateComponent::PickedUpPowerup(Script::CStruct* params, Script::CScript*)
{
    int index = FindPowerup(params);
    if (index != -1)
        m_powerups[index] = 1;
    return true;
}

// 0x005196F0
bool CSkaterStateComponent::GetTerrain(Script::CStruct*, Script::CScript* script)
{
    script->m_params->AddInteger(kTerrain, m_terrain);
    return true;
}

// 0x00519720
bool CSkaterStateComponent::GetSparksPos(Script::CStruct*, Script::CScript* script)
{
    script->m_params->AddChecksum(kSparksPos, m_sparksPos);
    return true;
}

// 0x00519600
bool CSkaterStateComponent::SetSparksPos(Script::CStruct* params, Script::CScript*)
{
    params->GetChecksum(uint32_t{0}, &m_sparksPos, 0);
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
void SkaterStateHistoryFrame::SetTime(uint32_t time)
{
    auto* self = reinterpret_cast<uint8_t*>(this);
    *reinterpret_cast<uint16_t*>(self + 0x78) = static_cast<uint16_t>(time);
    *reinterpret_cast<uint16_t*>(self + 0x7A) = static_cast<uint16_t>(time >> 16);
}

// 0x005197C0
void CSkaterStateHistoryComponent::DestroyBody()
{
    m_vtable = kStateHistoryVtable;
    CBaseComponent::DestroyBody();
}

} // namespace Obj

REPLACE(0x00519740, Obj::CSkaterStateComponent::Create);
REPLACE(0x00519430, Obj::CSkaterStateComponent::RegisterMemberFunctions);
REPLACE(0x00519450, &Obj::CSkaterStateComponent::DestroyBody);
REPLACE(0x00519620, &Obj::CSkaterStateComponent::Destroy);
REPLACE(0x00519460, &Obj::CSkaterStateComponent::InitFromStructure);
REPLACE(0x00519660, &Obj::CSkaterStateComponent::SetState);
REPLACE(0x00519680, &Obj::CSkaterStateComponent::SetPhysicsMode);
REPLACE(0x005196A0, &Obj::CSkaterStateComponent::SetTerrain);
REPLACE(0x00519470, &Obj::CSkaterStateComponent::ClearPowerups);
REPLACE(0x00519650, &Obj::CSkaterStateComponent::ClearDoingTrick);
REPLACE(0x00519480, &Obj::CSkaterStateComponent::InAir);
REPLACE(0x00519490, &Obj::CSkaterStateComponent::OnWall);
REPLACE(0x005194A0, &Obj::CSkaterStateComponent::OnLip);
REPLACE(0x005194B0, &Obj::CSkaterStateComponent::OnRail);
REPLACE(0x005194C0, &Obj::CSkaterStateComponent::InWallplant);
REPLACE(0x005196D0, &Obj::CSkaterStateComponent::InBailState);
REPLACE(0x005194D0, &Obj::CSkaterStateComponent::Walking);
REPLACE(0x005194F0, &Obj::CSkaterStateComponent::Skating);
REPLACE(0x00519510, &Obj::CSkaterStateComponent::Driving);
REPLACE(0x00519520, &Obj::CSkaterStateComponent::SetDriving);
REPLACE(0x00519530, &Obj::CSkaterStateComponent::UnsetDriving);
REPLACE(0x005196E0, &Obj::CSkaterStateComponent::DoingTrick);
REPLACE(0x00519540, &Obj::CSkaterStateComponent::HasPowerup);
REPLACE(0x005195B0, &Obj::CSkaterStateComponent::PickedUpPowerup);
REPLACE(0x005196F0, &Obj::CSkaterStateComponent::GetTerrain);
REPLACE(0x00519720, &Obj::CSkaterStateComponent::GetSparksPos);
REPLACE(0x00519600, &Obj::CSkaterStateComponent::SetSparksPos);
REPLACE(0x005197D0, &Obj::SkaterStateHistoryFrame::GetTime);
REPLACE(0x005197E0, &Obj::SkaterStateHistoryFrame::SetTime);
REPLACE(0x005197C0, &Obj::CSkaterStateHistoryComponent::DestroyBody);
