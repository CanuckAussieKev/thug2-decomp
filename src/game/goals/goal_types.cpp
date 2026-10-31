// The goal types of 0x005517C0-0x00552E0F: competition, film, find gaps (see goal_types.h).

#include "game/goals/goal_types.h"

#include <cmath>
#include <cstdint>

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
#include "game/core/code_address.h"
#include "game/gfx/tod_manager.h"
#include "game/core/stubs.h"
#include "game/gfx/weather.h"

namespace GoalSystem {

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

const char* const kGotBronze = "gotBronze";    // "gotBronze"
const char* const kGotSilver = "gotSilver";    // "gotSilver"
const char* const kGotGold = "gotGold";      // "gotGold"
const char* const kRewardGiven = "reward_given";  // "reward_given"
const char* const kWinRecordName = "win_record"; // "win_record"
const char* const kRewardParams = "reward_params"; // "reward_params"

static VtableSlot kCompetitionGoalVtable_slots[] = {&GoalSystem::CContestObjective::Teardown, &GoalSystem::CObjective::WantsToEnd, &GoalSystem::CObjective::HasRunOut,
                 &GoalSystem::CObjective::IsOperative, Engine3D::VirtualSlot3Jump, &GoalSystem::CContestObjective::Expire,
                 &GoalSystem::CContestObjective::RestoreSaveBlock, &GoalSystem::CContestObjective::CaptureSaveBlock,
                 &GoalSystem::CObjective::HasTimeLimit, Stubs::ReturnTrue, &GoalSystem::CObjective::HasComponentA302D7CD,
                 &GoalSystem::CObjective::Activate, &GoalSystem::CContestObjective::ChangeLive, &GoalSystem::CObjective::Disengage,
                 &GoalSystem::CContestObjective::ClearEngagedBit, &GoalSystem::CContestObjective::Win, &GoalSystem::CObjective::Lose,
                 &GoalSystem::CObjective::TickFrame, &GoalSystem::CContestObjective::IsFrozen, &GoalSystem::CObjective::SetFlag,
                 &GoalSystem::CObjective::StageFlagSet, &GoalSystem::CContestObjective::HasWon, &GoalSystem::CObjective::NetStageFlagSet,
                 &GoalSystem::CObjective::HasWonGoalOrChild, &GoalSystem::CObjective::IsPrepared, &GoalSystem::CObjective::MayTryAgain,
                 &GoalSystem::CContestObjective::MarkWon, &GoalSystem::CObjective::SetStageFlag, &GoalSystem::CObjective::SetNetStageFlag,
                 &GoalSystem::CContestObjective::UnBeat};
void** const kCompetitionGoalVtable = reinterpret_cast<void**>(kCompetitionGoalVtable_slots);
static VtableSlot kFilmGoalVtable_slots[] = {&GoalSystem::CShootVideoObjective::Teardown, &GoalSystem::CObjective::WantsToEnd, &GoalSystem::CObjective::HasRunOut,
                 &GoalSystem::CObjective::IsOperative, Engine3D::VirtualSlot3Jump, &GoalSystem::CObjective::Expire, &GoalSystem::CObjective::RestoreSaveBlock,
                 &GoalSystem::CObjective::CaptureSaveBlock, &GoalSystem::CObjective::HasTimeLimit, Stubs::ReturnTrue,
                 &GoalSystem::CObjective::HasComponentA302D7CD, &GoalSystem::CShootVideoObjective::Activate, &GoalSystem::CObjective::ChangeLive,
                 &GoalSystem::CObjective::Disengage, &GoalSystem::CObjective::ClearEngagedBit, &GoalSystem::CObjective::Win, &GoalSystem::CObjective::Lose,
                 &GoalSystem::CShootVideoObjective::TickFrame, &GoalSystem::CObjective::IsFrozen, &GoalSystem::CObjective::SetFlag, &GoalSystem::CObjective::StageFlagSet,
                 &GoalSystem::CObjective::HasWon, &GoalSystem::CObjective::NetStageFlagSet, &GoalSystem::CObjective::HasWonGoalOrChild,
                 &GoalSystem::CObjective::IsPrepared, &GoalSystem::CObjective::MayTryAgain, &GoalSystem::CObjective::MarkWon,
                 &GoalSystem::CObjective::SetStageFlag, &GoalSystem::CObjective::SetNetStageFlag, &GoalSystem::CObjective::UnBeat};
void** const kFilmGoalVtable = reinterpret_cast<void**>(kFilmGoalVtable_slots);
static VtableSlot kFindGapsGoalVtable_slots[] = {&GoalSystem::CHuntGapsObjective::Teardown, &GoalSystem::CObjective::WantsToEnd, &GoalSystem::CObjective::HasRunOut,
                 &GoalSystem::CObjective::IsOperative, Engine3D::VirtualSlot3Jump, &GoalSystem::CObjective::Expire, &GoalSystem::CObjective::RestoreSaveBlock,
                 &GoalSystem::CObjective::CaptureSaveBlock, &GoalSystem::CObjective::HasTimeLimit, Stubs::ReturnTrue,
                 &GoalSystem::CObjective::HasComponentA302D7CD, &GoalSystem::CHuntGapsObjective::Activate, &GoalSystem::CObjective::ChangeLive,
                 &GoalSystem::CObjective::Disengage, &GoalSystem::CObjective::ClearEngagedBit, &GoalSystem::CObjective::Win, &GoalSystem::CObjective::Lose,
                 &GoalSystem::CObjective::TickFrame, &GoalSystem::CObjective::IsFrozen, &GoalSystem::CObjective::SetFlag, &GoalSystem::CObjective::StageFlagSet,
                 &GoalSystem::CObjective::HasWon, &GoalSystem::CObjective::NetStageFlagSet, &GoalSystem::CObjective::HasWonGoalOrChild,
                 &GoalSystem::CObjective::IsPrepared, &GoalSystem::CObjective::MayTryAgain, &GoalSystem::CObjective::MarkWon,
                 &GoalSystem::CObjective::SetStageFlag, &GoalSystem::CObjective::SetNetStageFlag, &GoalSystem::CObjective::UnBeat};
void** const kFindGapsGoalVtable = reinterpret_cast<void**>(kFindGapsGoalVtable_slots);

int Truncate(double value) { return static_cast<int>(value); }

Scripts::CStruct* NewStructure()
{
    Scripts::CStruct* structure = Scripts::NewStruct();
    return structure ? structure->PrepareAll() : nullptr;
}
void DeleteStructure(Scripts::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Scripts::DeleteStruct(structure);
    }
}
GameModules::CContest* TheCompetition() { return SkateCalls::SkateField<GameModules::CContest*>(0x360); }

} // namespace

// --- The competition ---------------------------------------------------------------------------------------------

// 0x005517C0
CContestObjective* CContestObjective::MakeNew(Scripts::CStruct* params)
{
    CObjective::MakeNew(params);
    m_isHalted = false;
    m_hasThirdPlace = false;
    m_hasSecondPlace = false;
    m_hasTopPrize = false;
    m_prizePaid = 0;
    m_listLink.vtable = kCompetitionGoalVtable;
    m_bestScore = 0x7FFFFFFF;
    m_runEnding = 1;
    return this;
}

// 0x00551800
void CContestObjective::DestroyBody()
{
    m_listLink.vtable = kCompetitionGoalVtable;
    CObjective::DestroyBody();
}

// 0x00551A00
CContestObjective* CContestObjective::Teardown(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x00551810
bool CContestObjective::Expire() { return InvokeHookScript(kExpireScript); }

// 0x00551820
void CContestObjective::ChangeLive()
{
    m_isHalted = false;
    m_curPhase = static_cast<uint8_t>((m_curPhase & ~kStatePaused) | kStateActive);
}

// 0x00551830
void CContestObjective::ClearEngagedBit()
{
    m_isHalted = false;
    m_curPhase &= ~(kStateActive | kStatePaused);
}

// 0x00551840
bool CContestObjective::HasWon() { return (m_flagBits & kFlagWon) != 0; }

// 0x00551850
bool CContestObjective::UnBeat()
{
    m_flagBits &= ~kFlagWon;
    m_hasThirdPlace = false;
    m_hasSecondPlace = false;
    if (!Virtual<bool>(this, kVtHasWon))
        return false;
    m_hasTopPrize = false;
    return true;
}

// 0x00551880
void CContestObjective::FreezeContest() { m_isHalted = true; }

// 0x00551890
void CContestObjective::ThawContest()
{
    m_isHalted = false;
    m_flagBits &= ~kFlagEndOfRunStarted;
}

// 0x005518A0
int CContestObjective::IsFrozen() { return (m_curPhase & kStatePaused) || m_isHalted ? 1 : 0; }

// 0x005518C0
void CContestObjective::RestoreSaveBlock(Scripts::CStruct* data)
{
    // (the first value's local is the argument's slot: a missing gotBronze leaves the pointer - set)
    int got = static_cast<int>(reinterpret_cast<uintptr_t>(data));
    data->FetchIntValue(kGotBronze, &got, 0);
    if (got)
        m_hasThirdPlace = true;
    data->FetchIntValue(kGotSilver, &got, 0);
    if (got)
        m_hasThirdPlace = true; // (the original sets gotBronze for each medal)
    data->FetchIntValue(kGotGold, &got, 0);
    if (got)
        m_hasThirdPlace = true;
    int given = 0; // (the original's local is left as it was when reward_given is missing)
    data->FetchIntValue(kRewardGiven, &given, 0);
    m_prizePaid = given;
    int record;
    if (data->FetchIntValue(kWinRecordName, &record, 0))
        m_bestScore = record;
    CObjective::RestoreSaveBlock(data);
}

// 0x00551970
void CContestObjective::CaptureSaveBlock(Scripts::CStruct* data)
{
    CObjective::CaptureSaveBlock(data);
    data->PutInt(kGotBronze, m_hasThirdPlace ? 1 : 0);
    data->PutNameKey(kGotSilver, m_hasSecondPlace ? 1 : 0); // (the silver and gold medals saved as checksums)
    data->PutNameKey(kGotGold, m_hasTopPrize ? 1 : 0);
    data->PutInt(kWinRecordName, m_bestScore);
    data->PutInt(kRewardGiven, m_prizePaid);
}

// 0x00551A30
void CContestObjective::MarkWon()
{
    GameModules::CContest* competition = TheCompetition();
    if (competition->PlaceIs(1)) {
        m_hasTopPrize = true;
        m_hasSecondPlace = true;
        m_hasThirdPlace = true;
    } else if (competition->PlaceIs(2)) {
        m_hasSecondPlace = true;
        m_hasThirdPlace = true;
    } else if (competition->PlaceIs(3)) {
        m_hasThirdPlace = true;
    }
    m_flagBits |= kFlagWon;
}

// 0x00551A90
bool CContestObjective::Win()
{
    m_args->PutNameKey(kNoName, kJustWonGoal);
    InvokeHookScript(kDeactivateScript);
    m_args->RemoveFlag(kJustWonGoal);
    Virtual<void>(this, kVtSetInactive);
    if (!OnlinePlay::g_gameNetManager->InNetGame()) {
        GameModules::CContest* competition = TheCompetition();
        int place = 999;
        if (competition->PlaceIs(1))
            place = 1;
        else if (competition->PlaceIs(2))
            place = 2;
        else if (competition->PlaceIs(3))
            place = 3;
        if (place < m_bestScore) {
            m_args->PutInt(kWinRecordName, place);
            m_bestScore = place;
        }
    }
    Scripts::CStruct* rewards = NewStructure();
    if (!Virtual<bool>(this, kVtHasWon)) {
        Scripts::CStruct* own = NewStructure();
        GetPrizeStruct(own);
        rewards->AppendStructureIfAny(own);
        DeleteStructure(own);
        OpenUpPrizeObjectives();
    } else {
        m_args->EraseModule(kRewardParams);
    }
    m_args->PutSubStruct(kRewardParams, rewards);
    InvokeHookScript(kSuccessScript);
    DeleteStructure(rewards);
    if (!Virtual<bool>(this, kVtHasWon))
        Virtual<void>(this, kVtMarkWon);
    return true;
}

// --- Film --------------------------------------------------------------------------------------------------------

// 0x005527D0
CShootVideoObjective* CShootVideoObjective::MakeNew(Scripts::CStruct* params)
{
    CObjective::MakeNew(params);
    m_listLink.vtable = kFilmGoalVtable;
    return this;
}

// 0x005527F0
void CShootVideoObjective::DestroyBody()
{
    m_listLink.vtable = kFilmGoalVtable;
    CObjective::DestroyBody();
}

// 0x005528A0
CShootVideoObjective* CShootVideoObjective::Teardown(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x00552800
bool CShootVideoObjective::Activate()
{
    m_shotHoldTime = 0;
    m_unknown7C = 0;
    m_timeNeeded = 0;
    m_checkpoints = 0;
    m_recordingShot = false;
    m_args->PutInt(kTimeOnCamera, 0);
    int seconds;
    if (m_args->FetchIntValue(kTotalTimeRequired, &seconds, 0)) {
        m_timeNeeded = static_cast<uint32_t>(seconds * 1000);
        int time;
        m_args->FetchIntValue(kTime, &time, 1);
        m_unknown7C = m_unknown7C * static_cast<uint32_t>(time) * 1000;
        return CObjective::Activate();
    }
    m_args->FetchIntValue(kCheckpoints, &m_checkpoints, 0);
    return CObjective::Activate();
}

// 0x005528D0
bool CShootVideoObjective::TargetInView()
{
    uint32_t id;
    m_args->ReadHashValue(kFilmTarget, &id, 1);
    GameObjects::CEntity* target = GameObjects::FindObject(id);
    if (!target)
        return false;
    auto* object = static_cast<GameObjects::CGameEntity*>(target);
    void* model = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(object->FetchModule(kModel)) + 0x208);
    // The model's bounding sphere (0x00492F30) in the world.
    Maths::Vec4 sphere;
    const Maths::Vec4* bounds = static_cast<Engine3D::CRenderModel*>(model)->FetchBoundSphere(&sphere);
    Maths::Vec4 centre;
    centre[0] = sphere[0];
    centre[1] = sphere[1];
    centre[2] = sphere[2];
    centre[3] = 1.0f;
    (void)bounds;
    centre.MultiplyBy(object->m_basisFrame);
    for (int i = 0; i < 4; ++i)
        centre[i] = centre[i] + object->m_atPoint[i];
    // (0x0048C490: the sphere hidden from the camera)
    if (Engine3D::Engine::SphereVisibleInOneViewport(&centre, 6.0f))
        return false;
    auto* skater = static_cast<GameObjects::CRider*>(SkateCalls::GetHomeRider());
    Maths::Vec4* camera = static_cast<Gfx2D::ViewCam*>(skater->ObtainActiveLens())->FetchLocation();
    float dx = (*camera)[0] - object->m_atPoint[0];
    float dy = (*camera)[1] - object->m_atPoint[1];
    float dz = (*camera)[2] - object->m_atPoint[2];
    int maximum;
    m_args->FetchIntValue(kMaxDistanceToTarget, &maximum, 1);
    double distance = std::sqrt(static_cast<double>(dz) * dz + static_cast<double>(dy) * dy +
                                static_cast<double>(dx) * dx);
    return static_cast<double>(maximum) > distance;
}

// 0x00552A10
bool CShootVideoObjective::TickFrame()
{
    m_args->PutInt(kLastTimeOnCamera, static_cast<int>(m_shotHoldTime));
    if (!Virtual<bool>(this, kVtIsActive) || Virtual<bool>(this, kVtIsPaused))
        return CObjective::TickFrame();
    if (m_recordingShot) {
        if (TargetInView()) {
            m_shotHoldTime += Truncate(FrameClock::GetSlomoFrameSeconds() * Gfx2D::g_thousand); // (1000)
            m_args->PutInt(kTimeOnCamera, static_cast<int>(m_shotHoldTime));
        }
        if (m_shotHoldTime >= m_timeNeeded) {
            void* manager = ObjectiveBoard();
            return CompleteObjective(manager, GetObjectiveKey());
        }
        // (no time left to film enough in)
        if (m_timeNeeded - m_shotHoldTime > static_cast<uint32_t>(m_secondsRemaining)) {
            void* manager = ObjectiveBoard();
            return FailObjective(manager, GetObjectiveKey());
        }
        return CObjective::TickFrame();
    }
    if (m_checkpoints && m_checkpointsPassed >= m_checkpoints) {
        void* manager = ObjectiveBoard();
        return CompleteObjective(manager, GetObjectiveKey());
    }
    return CObjective::TickFrame();
}

// 0x00552AF0
void CShootVideoObjective::Checkpoint()
{
    if (!TargetInView())
        return;
    if (++m_checkpointsPassed > m_checkpoints) {
        void* manager = ObjectiveBoard();
        CompleteObjective(manager, GetObjectiveKey());
    }
}

// --- Find gaps ---------------------------------------------------------------------------------------------------

// 0x00552B30
CHuntGapsObjective* CHuntGapsObjective::MakeNew(Scripts::CStruct* params)
{
    CObjective::MakeNew(params);
    m_listLink.vtable = kFindGapsGoalVtable;
    return this;
}

// 0x00552B50
void CHuntGapsObjective::DestroyBody()
{
    m_listLink.vtable = kFindGapsGoalVtable;
    CObjective::DestroyBody();
}

// 0x00552B60
CHuntGapsObjective* CHuntGapsObjective::Teardown(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x00552B90
void CHuntGapsObjective::ScanForGaps()
{
    auto* skater = static_cast<GameObjects::CRider*>(SkateCalls::GetHomeRider());
    Scripts::CStruct* found;
    m_args->ReadSubStruct(kGapsFound, &found, 1);
    int count;
    m_args->FetchIntValue(kNumGapsFound, &count, 1);
    auto* score = static_cast<uint8_t*>(skater->GetPointsTotal());
    int gaps = *reinterpret_cast<int*>(score + 0x5C);
    bool foundNew = false;
    for (int i = 0; i < gaps; ++i) {
        // (GameModules::PointTally: 0x00562740 the gap `i` is one of the run's, 0x00561660 its name)
        if (!static_cast<GameModules::PointTally*>(static_cast<void*>(score))->IsGap(i))
            continue;
        uint32_t gap = reinterpret_cast<GameModules::PointTally*>(score)->GetTrickName(i);
        if (found->HasEntryNamed(gap))
            continue;
        found->PutInt(gap, 1);
        ++count;
        foundNew = true;
    }
    m_args->PutInt(kNumGapsFound, count);
    m_args->PutInt(kNumberCollected, count);
    if (!foundNew)
        return;
    Scripts::CStruct* params = NewStructure();
    params->PutNameKey(kGoalIdChecksum, GetObjectiveKey());
    Scripts::LaunchScript(kFindGapsFoundGap, params, nullptr, false, nullptr);
    DeleteStructure(params);
}

// 0x00552D20
bool CHuntGapsObjective::Activate()
{
    m_args->EraseModule(kGapsFound);
    Scripts::CStruct* found = NewStructure();
    m_args->PutSubStruct(kGapsFound, found);
    DeleteStructure(found);
    m_args->PutInt(kNumGapsFound, 0);
    m_args->PutInt(kNumberCollected, 0);
    int toFind;
    m_args->FetchIntValue(kNumGapsToFind, &toFind, 1); // (read and not used)
    return CObjective::Activate();
}

} // namespace Game
