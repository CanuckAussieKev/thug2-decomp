// The goal types of 0x005517C0-0x00552E0F: competition, film, find gaps (see goal_types.h).

#include "game/goals/goal_types.h"

#include <cmath>

#include "game/core/mem.h"
#include "game/goals/goal_calls.h"
#include "game/goals/goal_names.h"
#include "game/gfx/camera.h"
#include "game/mth/vector.h"
#include "game/net/gamenet_manager.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skate/competition.h"
#include "game/skate/skate_module_calls.h"
#include "game/skater/skater.h"
#include "game/sys/timer.h"
#include "game/gfx/model.h"
#include "game/gfx/engine.h"
#include "game/skate/score.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "game/gfx/tod_manager.h"

namespace Game {

namespace {

using namespace GoalCalls;
using namespace GoalNames;

constexpr uint32_t kJustWonGoal = 0xC309CAD1;          // just_won_goal
constexpr uint32_t kDeactivateScript = 0x01C92CAC;     // deactivate
constexpr uint32_t kSuccessScript = 0x90FF204D;        // success
constexpr uint32_t kTimeOnCamera = 0x9278CA8D;         // time_on_camera
constexpr uint32_t kTotalTimeRequired = 0x184E30FD;    // total_time_required
constexpr uint32_t kCheckpoints = 0xC808F9FB;          // (name unknown: the film checkpoints)
constexpr uint32_t kFilmTarget = 0xD262ED09;           // film_target
constexpr uint32_t kModel = 0x286A8D26;                // model
constexpr uint32_t kMaxDistanceToTarget = 0x856FAB92;  // max_distance_to_target
constexpr uint32_t kLastTimeOnCamera = 0xFC7FC309;     // last_time_on_camera
constexpr uint32_t kGapsFound = 0x735AF2DD;            // (name unknown: the gaps found)
constexpr uint32_t kNumGapsFound = 0xF1E59083;         // num_gaps_found
constexpr uint32_t kNumGapsToFind = 0xE75C4BBF;        // num_gaps_to_find
constexpr uint32_t kFindGapsFoundGap = 0x851AA6B8;     // goal_find_gaps_found_gap

const char* const kGotBronze = reinterpret_cast<const char*>(0x0064DA98);    // "gotBronze"
const char* const kGotSilver = reinterpret_cast<const char*>(0x0064DA8C);    // "gotSilver"
const char* const kGotGold = reinterpret_cast<const char*>(0x0064DA84);      // "gotGold"
const char* const kRewardGiven = reinterpret_cast<const char*>(0x0064DA74);  // "reward_given"
const char* const kWinRecordName = reinterpret_cast<const char*>(0x0064DA68); // "win_record"
const char* const kRewardParams = reinterpret_cast<const char*>(0x0064DAA4); // "reward_params"

void** const kCompetitionGoalVtable = reinterpret_cast<void**>(0x0064D9F0);
void** const kFilmGoalVtable = reinterpret_cast<void**>(0x0064DC60);
void** const kFindGapsGoalVtable = reinterpret_cast<void**>(0x0064DCD8);

int Truncate(double value) { return static_cast<int>(value); }

Script::CStruct* NewStructure()
{
    Script::CStruct* structure = Script::NewStruct();
    return structure ? structure->Init() : nullptr;
}
void DeleteStructure(Script::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Script::DeleteStruct(structure);
    }
}
Mdl::CCompetition* TheCompetition() { return SkateCalls::SkateField<Mdl::CCompetition*>(0x360); }

} // namespace

// --- The competition ---------------------------------------------------------------------------------------------

// 0x005517C0
CCompetitionGoal* CCompetitionGoal::Construct(Script::CStruct* params)
{
    CGoal::Construct(params);
    m_paused = false;
    m_gotBronze = false;
    m_gotSilver = false;
    m_gotGold = false;
    m_rewardGiven = 0;
    m_node.vtable = kCompetitionGoalVtable;
    m_bestScore = 0x7FFFFFFF;
    m_endRunType = 1;
    return this;
}

// 0x00551800
void CCompetitionGoal::DestroyBody()
{
    m_node.vtable = kCompetitionGoalVtable;
    CGoal::DestroyBody();
}

// 0x00551A00
CCompetitionGoal* CCompetitionGoal::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x00551810
bool CCompetitionGoal::Expire() { return RunCallbackScript(kExpireScript); }

// 0x00551820
void CCompetitionGoal::SetActive()
{
    m_paused = false;
    m_state = static_cast<uint8_t>((m_state & ~kStatePaused) | kStateActive);
}

// 0x00551830
void CCompetitionGoal::SetInactive()
{
    m_paused = false;
    m_state &= ~(kStateActive | kStatePaused);
}

// 0x00551840
bool CCompetitionGoal::HasWon() { return (m_flags & kFlagWon) != 0; }

// 0x00551850
bool CCompetitionGoal::UnBeat()
{
    m_flags &= ~kFlagWon;
    m_gotBronze = false;
    m_gotSilver = false;
    if (!Virtual<bool>(this, kVtHasWon))
        return false;
    m_gotGold = false;
    return true;
}

// 0x00551880
void CCompetitionGoal::PauseCompetition() { m_paused = true; }

// 0x00551890
void CCompetitionGoal::UnPauseCompetition()
{
    m_paused = false;
    m_flags &= ~kFlagEndOfRunStarted;
}

// 0x005518A0
int CCompetitionGoal::IsPaused() { return (m_state & kStatePaused) || m_paused ? 1 : 0; }

// 0x005518C0
void CCompetitionGoal::LoadSaveData(Script::CStruct* data)
{
    // (the first value's local is the argument's slot: a missing gotBronze leaves the pointer - set)
    int got = static_cast<int>(reinterpret_cast<uintptr_t>(data));
    data->GetInteger(kGotBronze, &got, 0);
    if (got)
        m_gotBronze = true;
    data->GetInteger(kGotSilver, &got, 0);
    if (got)
        m_gotBronze = true; // (the original sets gotBronze for each medal)
    data->GetInteger(kGotGold, &got, 0);
    if (got)
        m_gotBronze = true;
    int given = 0; // (the original's local is left as it was when reward_given is missing)
    data->GetInteger(kRewardGiven, &given, 0);
    m_rewardGiven = given;
    int record;
    if (data->GetInteger(kWinRecordName, &record, 0))
        m_bestScore = record;
    CGoal::LoadSaveData(data);
}

// 0x00551970
void CCompetitionGoal::GetSaveData(Script::CStruct* data)
{
    CGoal::GetSaveData(data);
    data->AddInteger(kGotBronze, m_gotBronze ? 1 : 0);
    data->AddChecksum(kGotSilver, m_gotSilver ? 1 : 0); // (the silver and gold medals saved as checksums)
    data->AddChecksum(kGotGold, m_gotGold ? 1 : 0);
    data->AddInteger(kWinRecordName, m_bestScore);
    data->AddInteger(kRewardGiven, m_rewardGiven);
}

// 0x00551A30
void CCompetitionGoal::MarkWon()
{
    Mdl::CCompetition* competition = TheCompetition();
    if (competition->PlaceIs(1)) {
        m_gotGold = true;
        m_gotSilver = true;
        m_gotBronze = true;
    } else if (competition->PlaceIs(2)) {
        m_gotSilver = true;
        m_gotBronze = true;
    } else if (competition->PlaceIs(3)) {
        m_gotBronze = true;
    }
    m_flags |= kFlagWon;
}

// 0x00551A90
bool CCompetitionGoal::Win()
{
    m_params->AddChecksum(kNoName, kJustWonGoal);
    RunCallbackScript(kDeactivateScript);
    m_params->RemoveFlag(kJustWonGoal);
    Virtual<void>(this, kVtSetInactive);
    if (!GameNet::g_gameNetManager->InNetGame()) {
        Mdl::CCompetition* competition = TheCompetition();
        int place = 999;
        if (competition->PlaceIs(1))
            place = 1;
        else if (competition->PlaceIs(2))
            place = 2;
        else if (competition->PlaceIs(3))
            place = 3;
        if (place < m_bestScore) {
            m_params->AddInteger(kWinRecordName, place);
            m_bestScore = place;
        }
    }
    Script::CStruct* rewards = NewStructure();
    if (!Virtual<bool>(this, kVtHasWon)) {
        Script::CStruct* own = NewStructure();
        GetRewardsStructure(own);
        rewards->AppendStructureIfAny(own);
        DeleteStructure(own);
        UnlockRewardGoals();
    } else {
        m_params->RemoveComponent(kRewardParams);
    }
    m_params->AddStructure(kRewardParams, rewards);
    RunCallbackScript(kSuccessScript);
    DeleteStructure(rewards);
    if (!Virtual<bool>(this, kVtHasWon))
        Virtual<void>(this, kVtMarkWon);
    return true;
}

// --- Film --------------------------------------------------------------------------------------------------------

// 0x005527D0
CFilmGoal* CFilmGoal::Construct(Script::CStruct* params)
{
    CGoal::Construct(params);
    m_node.vtable = kFilmGoalVtable;
    return this;
}

// 0x005527F0
void CFilmGoal::DestroyBody()
{
    m_node.vtable = kFilmGoalVtable;
    CGoal::DestroyBody();
}

// 0x005528A0
CFilmGoal* CFilmGoal::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x00552800
bool CFilmGoal::Activate()
{
    m_timeOnCamera = 0;
    m_unknown7C = 0;
    m_timeNeeded = 0;
    m_checkpoints = 0;
    m_filming = false;
    m_params->AddInteger(kTimeOnCamera, 0);
    int seconds;
    if (m_params->GetInteger(kTotalTimeRequired, &seconds, 0)) {
        m_timeNeeded = static_cast<uint32_t>(seconds * 1000);
        int time;
        m_params->GetInteger(kTime, &time, 1);
        m_unknown7C = m_unknown7C * static_cast<uint32_t>(time) * 1000;
        return CGoal::Activate();
    }
    m_params->GetInteger(kCheckpoints, &m_checkpoints, 0);
    return CGoal::Activate();
}

// 0x005528D0
bool CFilmGoal::TargetInView()
{
    uint32_t id;
    m_params->GetChecksum(kFilmTarget, &id, 1);
    Obj::CObject* target = Obj::FindObject(id);
    if (!target)
        return false;
    auto* object = static_cast<Obj::CCompositeObject*>(target);
    void* model = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(object->GetComponent(kModel)) + 0x208);
    // The model's bounding sphere (0x00492F30) in the world.
    Mth::Vector sphere;
    const Mth::Vector* bounds = static_cast<Nx::CModel*>(model)->GetBoundingSphere(&sphere);
    Mth::Vector centre;
    centre[0] = sphere[0];
    centre[1] = sphere[1];
    centre[2] = sphere[2];
    centre[3] = 1.0f;
    (void)bounds;
    centre.MultiplyBy(object->m_matrix);
    for (int i = 0; i < 4; ++i)
        centre[i] = centre[i] + object->m_pos[i];
    // (0x0048C490: the sphere hidden from the camera)
    if (Nx::Engine::SphereVisibleInOneViewport(&centre, 6.0f))
        return false;
    auto* skater = static_cast<Obj::CSkater*>(SkateCalls::GetLocalSkater());
    Mth::Vector* camera = static_cast<Gfx::Camera*>(skater->GetCamera())->GetPos();
    float dx = (*camera)[0] - object->m_pos[0];
    float dy = (*camera)[1] - object->m_pos[1];
    float dz = (*camera)[2] - object->m_pos[2];
    int maximum;
    m_params->GetInteger(kMaxDistanceToTarget, &maximum, 1);
    double distance = std::sqrt(static_cast<double>(dz) * dz + static_cast<double>(dy) * dy +
                                static_cast<double>(dx) * dx);
    return static_cast<double>(maximum) > distance;
}

// 0x00552A10
bool CFilmGoal::Update()
{
    m_params->AddInteger(kLastTimeOnCamera, static_cast<int>(m_timeOnCamera));
    if (!Virtual<bool>(this, kVtIsActive) || Virtual<bool>(this, kVtIsPaused))
        return CGoal::Update();
    if (m_filming) {
        if (TargetInView()) {
            m_timeOnCamera += Truncate(Tmr::GetSlomoFrameSeconds() * Gfx::g_thousand); // (1000)
            m_params->AddInteger(kTimeOnCamera, static_cast<int>(m_timeOnCamera));
        }
        if (m_timeOnCamera >= m_timeNeeded) {
            void* manager = GoalManager();
            return WinGoal(manager, GetGoalId());
        }
        // (no time left to film enough in)
        if (m_timeNeeded - m_timeOnCamera > static_cast<uint32_t>(m_timeLeft)) {
            void* manager = GoalManager();
            return LoseGoal(manager, GetGoalId());
        }
        return CGoal::Update();
    }
    if (m_checkpoints && m_checkpointsPassed >= m_checkpoints) {
        void* manager = GoalManager();
        return WinGoal(manager, GetGoalId());
    }
    return CGoal::Update();
}

// 0x00552AF0
void CFilmGoal::Checkpoint()
{
    if (!TargetInView())
        return;
    if (++m_checkpointsPassed > m_checkpoints) {
        void* manager = GoalManager();
        WinGoal(manager, GetGoalId());
    }
}

// --- Find gaps ---------------------------------------------------------------------------------------------------

// 0x00552B30
CFindGapsGoal* CFindGapsGoal::Construct(Script::CStruct* params)
{
    CGoal::Construct(params);
    m_node.vtable = kFindGapsGoalVtable;
    return this;
}

// 0x00552B50
void CFindGapsGoal::DestroyBody()
{
    m_node.vtable = kFindGapsGoalVtable;
    CGoal::DestroyBody();
}

// 0x00552B60
CFindGapsGoal* CFindGapsGoal::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x00552B90
void CFindGapsGoal::CheckGaps()
{
    auto* skater = static_cast<Obj::CSkater*>(SkateCalls::GetLocalSkater());
    Script::CStruct* found;
    m_params->GetStructure(kGapsFound, &found, 1);
    int count;
    m_params->GetInteger(kNumGapsFound, &count, 1);
    auto* score = static_cast<uint8_t*>(skater->GetScore());
    int gaps = *reinterpret_cast<int*>(score + 0x5C);
    bool foundNew = false;
    for (int i = 0; i < gaps; ++i) {
        // (Mdl::Score: 0x00562740 the gap `i` is one of the run's, 0x00561660 its name)
        if (!static_cast<Mdl::Score*>(static_cast<void*>(score))->IsGap(i))
            continue;
        uint32_t gap = reinterpret_cast<Mdl::Score*>(score)->GetTrickName(i);
        if (found->ContainsComponentNamed(gap))
            continue;
        found->AddInteger(gap, 1);
        ++count;
        foundNew = true;
    }
    m_params->AddInteger(kNumGapsFound, count);
    m_params->AddInteger(kNumberCollected, count);
    if (!foundNew)
        return;
    Script::CStruct* params = NewStructure();
    params->AddChecksum(kGoalIdChecksum, GetGoalId());
    Script::RunScript(kFindGapsFoundGap, params, nullptr, false, nullptr);
    DeleteStructure(params);
}

// 0x00552D20
bool CFindGapsGoal::Activate()
{
    m_params->RemoveComponent(kGapsFound);
    Script::CStruct* found = NewStructure();
    m_params->AddStructure(kGapsFound, found);
    DeleteStructure(found);
    m_params->AddInteger(kNumGapsFound, 0);
    m_params->AddInteger(kNumberCollected, 0);
    int toFind;
    m_params->GetInteger(kNumGapsToFind, &toFind, 1); // (read and not used)
    return CGoal::Activate();
}

} // namespace Game

REPLACE(0x005517C0, &Game::CCompetitionGoal::Construct);
REPLACE(0x00551800, &Game::CCompetitionGoal::DestroyBody);
REPLACE(0x00551810, &Game::CCompetitionGoal::Expire);
REPLACE(0x00551820, &Game::CCompetitionGoal::SetActive);
REPLACE(0x00551830, &Game::CCompetitionGoal::SetInactive);
REPLACE(0x00551840, &Game::CCompetitionGoal::HasWon);
REPLACE(0x00551850, &Game::CCompetitionGoal::UnBeat);
REPLACE(0x00551880, &Game::CCompetitionGoal::PauseCompetition);
REPLACE(0x00551890, &Game::CCompetitionGoal::UnPauseCompetition);
REPLACE(0x005518A0, &Game::CCompetitionGoal::IsPaused);
REPLACE(0x005518C0, &Game::CCompetitionGoal::LoadSaveData);
REPLACE(0x00551970, &Game::CCompetitionGoal::GetSaveData);
REPLACE(0x00551A00, &Game::CCompetitionGoal::Destroy);
REPLACE(0x00551A30, &Game::CCompetitionGoal::MarkWon);
REPLACE(0x00551A90, &Game::CCompetitionGoal::Win);
REPLACE(0x005527D0, &Game::CFilmGoal::Construct);
REPLACE(0x005527F0, &Game::CFilmGoal::DestroyBody);
REPLACE(0x00552800, &Game::CFilmGoal::Activate);
REPLACE(0x005528A0, &Game::CFilmGoal::Destroy);
REPLACE(0x005528D0, &Game::CFilmGoal::TargetInView);
REPLACE(0x00552A10, &Game::CFilmGoal::Update);
REPLACE(0x00552AF0, &Game::CFilmGoal::Checkpoint);
REPLACE(0x00552B30, &Game::CFindGapsGoal::Construct);
REPLACE(0x00552B50, &Game::CFindGapsGoal::DestroyBody);
REPLACE(0x00552B60, &Game::CFindGapsGoal::Destroy);
REPLACE(0x00552B90, &Game::CFindGapsGoal::CheckGaps);
REPLACE(0x00552D20, &Game::CFindGapsGoal::Activate);
