// The skate-the-line goal (CSkateLineGoal, 0x005691F0-0x0056952F; see goal_skate_line.h).

#include <cstdint>

#include "game/goals/goal_skate_line.h"

#include "game/core/dbg.h"
#include "game/core/mem.h"
#include "game/goals/goal_manager.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"
#include "game/core/stubs.h"
#include "game/gfx/weather.h"
#include "game/core/code_address.h"

namespace GoalSystem {

namespace {

constexpr uint32_t kRequiresLine = 0x7100155D;          // (name unknown: the line must be landed)
constexpr uint32_t kGoalObjects = 0x3CCE31F0;           // goal_objects
constexpr uint32_t kFlag = 0x2E0B1465;                  // flag
constexpr uint32_t kTime = 0x906B67BA;                  // time
constexpr uint32_t kGoalObjectsIndex = 0x89B927D0;      // goal_objects_index
constexpr uint32_t kFirstSpotParams = 0xDB5515AA;       // first_spot_params
constexpr uint32_t kNextCluster = 0x918C00F2;           // goal_skate_the_line_next_cluster

static VtableSlot kSkateLineVtable_slots[] = {&GoalSystem::CSkateLineGoal::Teardown, &GoalSystem::CObjective::WantsToEnd, &GoalSystem::CObjective::HasRunOut,
                 &GoalSystem::CObjective::IsOperative, Engine3D::VirtualSlot3Jump, &GoalSystem::CObjective::Expire, &GoalSystem::CObjective::RestoreSaveBlock,
                 &GoalSystem::CObjective::CaptureSaveBlock, &GoalSystem::CObjective::HasTimeLimit, Stubs::ReturnTrue,
                 &GoalSystem::CObjective::HasComponentA302D7CD, &GoalSystem::CObjective::Activate, &GoalSystem::CObjective::ChangeLive,
                 &GoalSystem::CObjective::Disengage, &GoalSystem::CObjective::ClearEngagedBit, &GoalSystem::CObjective::Win, &GoalSystem::CObjective::Lose,
                 &GoalSystem::CObjective::TickFrame, &GoalSystem::CObjective::IsFrozen, &GoalSystem::CSkateLineGoal::SetFlag,
                 &GoalSystem::CObjective::StageFlagSet, &GoalSystem::CObjective::HasWon, &GoalSystem::CObjective::NetStageFlagSet,
                 &GoalSystem::CObjective::HasWonGoalOrChild, &GoalSystem::CObjective::IsPrepared, &GoalSystem::CObjective::MayTryAgain,
                 &GoalSystem::CObjective::MarkWon, &GoalSystem::CObjective::SetStageFlag,
                 &GoalSystem::CObjective::SetNetStageFlag,
                 &GoalSystem::CObjective::UnBeat};
void** const kSkateLineVtable = reinterpret_cast<void**>(kSkateLineVtable_slots);

Scripts::CStruct* NewStructure()
{
    Scripts::CStruct* made = Scripts::NewStruct();
    return made ? made->PrepareAll() : nullptr;
}
void DeleteStructure(Scripts::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Scripts::DeleteStruct(structure);
    }
}

} // namespace

// 0x005691F0
CSkateLineGoal* CSkateLineGoal::MakeNew(Scripts::CStruct* params)
{
    CObjective::MakeNew(params);
    m_listLink.vtable = kSkateLineVtable;
    return this;
}

// 0x00569210
void CSkateLineGoal::DestroyBody()
{
    m_listLink.vtable = kSkateLineVtable;
    CObjective::DestroyBody();
}

// 0x00569290
CSkateLineGoal* CSkateLineGoal::Teardown(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x00569220
bool CSkateLineGoal::Land()
{
    if (!RequiresCombo())
        return false;
    if (!m_args->HasBareFlag(kRequiresLine))
        return false;
    CGoalManager* manager = GetObjectiveBoard();
    if (HasEveryBit())
        return manager->CompleteObjective(GetObjectiveKey());
    DebugOut::Printf("all flags not set\n"); // "all flags not set\n"
    return manager->FailObjective(GetObjectiveKey());
}

// 0x005692C0
void CSkateLineGoal::NextCluster()
{
    Scripts::CArray* objects = nullptr;
    m_args->FetchList(kGoalObjects, &objects, 1);
    auto* cluster = reinterpret_cast<Scripts::CStruct*>(objects->FetchPart(static_cast<uint32_t>(m_numFlagsSet)));
    int time;
    if (cluster->FetchIntValue(kTime, &time, 1))
        SetSecondsRemaining(time);
    Scripts::CStruct* params = NewStructure();
    params->AppendStructureIfAny(m_args);
    params->AppendStructureIfAny(cluster);
    params->PutInt(kGoalObjectsIndex, m_numFlagsSet);
    if (m_numFlagsSet == 1)
        params->PutSubStruct(kFirstSpotParams, reinterpret_cast<Scripts::CStruct*>(objects->FetchPart(0)));
    Scripts::LaunchScript(kNextCluster, params, nullptr, false, nullptr);
    DeleteStructure(params);
}

// 0x00569410 (vtable +0x4C)
bool CSkateLineGoal::SetFlag(uint32_t flag, int value, uint32_t sub)
{
    if (Virtual<bool>(this, kVtHasWon) || IsLatched())
        return false;
    if (HasEveryBit() && RequiresCombo())
        return false;
    Scripts::CArray* objects = nullptr;
    m_args->FetchList(kGoalObjects, &objects, 1);
    uint32_t clusterFlag;
    reinterpret_cast<Scripts::CStruct*>(objects->FetchPart(static_cast<uint32_t>(m_numFlagsSet)))
        ->ReadHashValue(kFlag, &clusterFlag, 1);
    bool set = false;
    if (clusterFlag != flag)
        return set; // (only the line's next cluster)
    if (!Virtual<bool>(this, kVtIsActive))
        GetObjectiveBoard()->EngageObjective(GetObjectiveKey(), false);
    set = CObjective::SetFlag(flag, value, sub);
    if (!Virtual<bool>(this, kVtIsActive))
        return set;
    if (!HasEveryBit()) {
        NextCluster();
        return set;
    }
    if (!RequiresCombo())
        GetObjectiveBoard()->CompleteObjective(GetObjectiveKey());
    return set;
}

} // namespace Game
