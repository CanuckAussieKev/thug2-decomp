// The skate-the-line goal (CSkateLineGoal, 0x005691F0-0x0056952F; see goal_skate_line.h).

#include <cstdint>

#include "game/goals/goal_skate_line.h"

#include "game/core/dbg.h"
#include "game/core/mem.h"
#include "game/goals/goal_manager.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/core/stubs.h"
#include "game/gfx/weather.h"
#include "game/core/code_address.h"

namespace Game {

namespace {

constexpr uint32_t kRequiresLine = 0x7100155D;          // (name unknown: the line must be landed)
constexpr uint32_t kGoalObjects = 0x3CCE31F0;           // goal_objects
constexpr uint32_t kFlag = 0x2E0B1465;                  // flag
constexpr uint32_t kTime = 0x906B67BA;                  // time
constexpr uint32_t kGoalObjectsIndex = 0x89B927D0;      // goal_objects_index
constexpr uint32_t kFirstSpotParams = 0xDB5515AA;       // first_spot_params
constexpr uint32_t kNextCluster = 0x918C00F2;           // goal_skate_the_line_next_cluster

static VtableSlot kSkateLineVtable_slots[] = {&Game::CSkateLineGoal::Destroy, &Game::CGoal::ShouldExpire, &Game::CGoal::IsExpired,
                 &Game::CGoal::IsActive, Nx::VirtualSlot3Jump, &Game::CGoal::Expire, &Game::CGoal::LoadSaveData,
                 &Game::CGoal::GetSaveData, &Game::CGoal::HasTimeLimit, Stubs::ReturnTrue,
                 &Game::CGoal::HasComponentA302D7CD, &Game::CGoal::Activate, &Game::CGoal::SetActive,
                 &Game::CGoal::Deactivate, &Game::CGoal::SetInactive, &Game::CGoal::Win, &Game::CGoal::Lose,
                 &Game::CGoal::Update, &Game::CGoal::IsPaused, &Game::CSkateLineGoal::SetFlag,
                 &Game::CGoal::StageFlagSet, &Game::CGoal::HasWon, &Game::CGoal::NetStageFlagSet,
                 &Game::CGoal::HasWonGoalOrChild, &Game::CGoal::IsInitialized, &Game::CGoal::CanRetry,
                 &Game::CGoal::MarkWon, &Game::CGoal::SetStageFlag,
                 &Game::CGoal::SetNetStageFlag,
                 &Game::CGoal::UnBeat};
void** const kSkateLineVtable = reinterpret_cast<void**>(kSkateLineVtable_slots);

Script::CStruct* NewStructure()
{
    Script::CStruct* made = Script::NewStruct();
    return made ? made->Init() : nullptr;
}
void DeleteStructure(Script::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Script::DeleteStruct(structure);
    }
}

} // namespace

// 0x005691F0
CSkateLineGoal* CSkateLineGoal::Construct(Script::CStruct* params)
{
    CGoal::Construct(params);
    m_node.vtable = kSkateLineVtable;
    return this;
}

// 0x00569210
void CSkateLineGoal::DestroyBody()
{
    m_node.vtable = kSkateLineVtable;
    CGoal::DestroyBody();
}

// 0x00569290
CSkateLineGoal* CSkateLineGoal::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x00569220
bool CSkateLineGoal::Land()
{
    if (!RequiresCombo())
        return false;
    if (!m_params->ContainsFlag(kRequiresLine))
        return false;
    CGoalManager* manager = GetGoalManager();
    if (AllFlagsSet())
        return manager->WinGoal(GetGoalId());
    Dbg::Printf("all flags not set\n"); // "all flags not set\n"
    return manager->LoseGoal(GetGoalId());
}

// 0x005692C0
void CSkateLineGoal::NextCluster()
{
    Script::CArray* objects = nullptr;
    m_params->GetArray(kGoalObjects, &objects, 1);
    auto* cluster = reinterpret_cast<Script::CStruct*>(objects->GetElement(static_cast<uint32_t>(m_numFlagsSet)));
    int time;
    if (cluster->GetInteger(kTime, &time, 1))
        SetTimeLeft(time);
    Script::CStruct* params = NewStructure();
    params->AppendStructureIfAny(m_params);
    params->AppendStructureIfAny(cluster);
    params->AddInteger(kGoalObjectsIndex, m_numFlagsSet);
    if (m_numFlagsSet == 1)
        params->AddStructure(kFirstSpotParams, reinterpret_cast<Script::CStruct*>(objects->GetElement(0)));
    Script::RunScript(kNextCluster, params, nullptr, false, nullptr);
    DeleteStructure(params);
}

// 0x00569410 (vtable +0x4C)
bool CSkateLineGoal::SetFlag(uint32_t flag, int value, uint32_t sub)
{
    if (Virtual<bool>(this, kVtHasWon) || IsLocked())
        return false;
    if (AllFlagsSet() && RequiresCombo())
        return false;
    Script::CArray* objects = nullptr;
    m_params->GetArray(kGoalObjects, &objects, 1);
    uint32_t clusterFlag;
    reinterpret_cast<Script::CStruct*>(objects->GetElement(static_cast<uint32_t>(m_numFlagsSet)))
        ->GetChecksum(kFlag, &clusterFlag, 1);
    bool set = false;
    if (clusterFlag != flag)
        return set; // (only the line's next cluster)
    if (!Virtual<bool>(this, kVtIsActive))
        GetGoalManager()->ActivateGoal(GetGoalId(), false);
    set = CGoal::SetFlag(flag, value, sub);
    if (!Virtual<bool>(this, kVtIsActive))
        return set;
    if (!AllFlagsSet()) {
        NextCluster();
        return set;
    }
    if (!RequiresCombo())
        GetGoalManager()->WinGoal(GetGoalId());
    return set;
}

} // namespace Game
