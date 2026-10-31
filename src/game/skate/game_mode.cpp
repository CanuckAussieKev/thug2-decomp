// GoalSystem::CMatchRules and GoalSystem::CMatchFlow: 0x00552E10-0x00553DF9 (see game_mode.h).

#include <cstdint>
#include <initializer_list>
#include <new>

#include "game/skate/game_mode.h"

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/goals/goal.h"
#include "game/net/gamenet_manager.h"
#include "game/script/cscript.h"
#include "game/script/preferences.h"
#include "game/skate/skate_module_calls.h"
#include "game/skate/victory_conditions.h"
#include "game/goals/goal_manager.h"
#include "game/core/code_address.h"

namespace GoalSystem {

GoalSystem::CMatchRules* g_gameMode;
uint32_t g_gameModeReferences;

namespace {

constexpr uint32_t kAccumulateScore = 0xC1F5864B;   // accumulate_score
constexpr uint32_t kTrackBestCombo = 0xD57C16BA;    // track_best_combo
constexpr uint32_t kScoreFrozen = 0x78FB9F04;       // score_frozen
constexpr uint32_t kDegradeScore = 0xCA37E639;      // degrade_score
constexpr uint32_t kTrackTrickScore = 0x47F763FF;   // track_trick_score
constexpr uint32_t kTeamGraffitiMode = 0xA8774BF7;  // team_graffiti_mode
constexpr uint32_t kGoals = 0x38DBE1D0;             // goals
constexpr uint32_t kNumTeams = 0x2EBE6CD1;          // num_teams
constexpr uint32_t kTimerBeeps = 0x035C8073;        // timer_beeps
constexpr uint32_t kDefaultTimeLimit = 0x8D38A280;  // default_time_limit
constexpr uint32_t kInitialPlayers = 0x6DC3D6F1;    // initial_players
constexpr uint32_t kMaxPlayers = 0x4D42E61D;        // max_players
constexpr uint32_t kFireballLevel = 0xCE87E4E3;     // fireball_level
constexpr uint32_t kRound = 0x3A1115CB;             // round
constexpr uint32_t kName = 0xA1DC81F9;              // Name
constexpr uint32_t kKing = 0x5D32129C;              // king
constexpr uint32_t kNetKing = 0x6EF8FDA0;           // netking
constexpr uint32_t kScoreChallenge = 0xF135ECB6;    // scorechallenge
constexpr uint32_t kNetScoreChallenge = 0x1498240A; // netscorechallenge
constexpr uint32_t kCreateGoals = 0xE45F0CA2;       // creategoals
constexpr uint32_t kFirefight = 0x3D6D444F;         // firefight
constexpr uint32_t kNetFirefight = 0xBFF33600;      // netfirefight
constexpr uint32_t kStopAtZero = 0x48E748B5;        // stop_at_zero
constexpr uint32_t kNetLobby = 0x1C471C60;          // NetLobby
constexpr uint32_t kScreenMode = 0x9BAB2EAE;        // screenmode
constexpr uint32_t kSplitScreen = 0x06AB02F2;       // splitscreen
constexpr uint32_t kSingle = 0x3558D8E6;            // single
constexpr uint32_t kHorse = 0x9D65D0E7;             // Horse
constexpr uint32_t kViewportType = 0x6F82C71E;      // viewport_type
constexpr uint32_t kChecksum = 0x21902065;          // checksum
constexpr uint32_t kViewportVertical = 0x92F5B4CB;  // viewport_type_vertical
constexpr uint32_t kViewportHorizontal = 0x7C442511; // viewport_type_horizontal
constexpr uint32_t kVictoryConditions = 0x94A24C5E; // victory_conditions
constexpr uint32_t kIsElimiskate = 0x2B1304A1;      // is_elimiskate
constexpr uint32_t kIsScavengerHunt = 0xD4734865;   // is_scavenger_hunt
constexpr uint32_t kModeInfo = 0xCA5F7FFB;          // mode_info
constexpr uint32_t kDefinition = 0x97CFD027;        // definition
constexpr uint32_t kFreeskate = 0x34861A16;         // freeskate

static VtableSlot kGameFlowVtable_slots[] = {&GoalSystem::CMatchFlow::Destroy};
void** const kGameFlowVtable = reinterpret_cast<void**>(kGameFlowVtable_slots);
static VtableSlot kGameModeVtable_slots[] = {&GoalSystem::CMatchRules::Destroy};
void** const kGameModeVtable = reinterpret_cast<void**>(kGameModeVtable_slots);
static VtableSlot kConditionHeadVtable_slots[] = {&GoalSystem::CMatchRules::ConditionNode::DestroyHead};
void** const kConditionHeadVtable = reinterpret_cast<void**>(kConditionHeadVtable_slots);
static VtableSlot kConditionNodeVtable_slots[] = {&GoalSystem::CMatchRules::ConditionNode::Destroy};
void** const kConditionNodeVtable = reinterpret_cast<void**>(kConditionNodeVtable_slots);
CWinRule* const kHeadMarker = reinterpret_cast<CWinRule*>(-1);

// (the getters' local when an asserted integer isn't there: the original's is the pushed `this`)
int Unset(const void* mode) { return static_cast<int>(reinterpret_cast<uintptr_t>(mode)); }

CMatchRules*& TheGameMode() { return g_gameMode; }
uint32_t& RefTally() { return g_gameModeReferences; }
OnlinePlay::Manager* NetManager() { return OnlinePlay::g_gameNetManager; }

// A virtual function of an object with a game vtable.
template <typename R, typename... A>
R VirtualCall(void* object, uint32_t slot, A... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, A...)>((*static_cast<void***>(object))[slot / 4])(object, args...);
}

// The goal manager (0x005591B0): 0x0055DC30 how many goals (`active`...) and 0x0055E980 its current net goal.
void* ObjectiveBoard() { return GetObjectiveBoard(); }
int CountGoals(void* manager, int which) { return static_cast<CGoalManager*>(manager)->NumActiveGoals(which != 0); }
CObjective* CurrentNetGoal(void* manager) { return static_cast<CGoalManager*>(manager)->GetActiveNetGoal(); }
CObjective* GetGoal(void* manager, uint32_t id, int assert)
{
    return static_cast<CGoalManager*>(manager)->GetGoal(id, assert);
}

// The conditions' list: its length and its `index`th node (ListKit::Head's, inlined by the original: each counts from
// the start).
uint32_t CountConditions(CMatchRules::ConditionNode* head)
{
    uint32_t count = 0;
    CMatchRules::ConditionNode* node = head->m_next;
    if (node->m_condition == kHeadMarker || !node)
        return 0;
    do {
        node = node->m_next;
        ++count;
    } while (node->m_condition != kHeadMarker && node);
    return count;
}
CMatchRules::ConditionNode* ConditionAt(CMatchRules::ConditionNode* head, uint32_t index)
{
    CMatchRules::ConditionNode* node = head->m_next;
    if (node->m_condition == kHeadMarker || !node)
        return nullptr;
    for (uint32_t left = index; left; --left) {
        node = node->m_next;
        if (node->m_condition == kHeadMarker || !node)
            return nullptr;
    }
    return node;
}

} // namespace

// --- The game flow -------------------------------------------------------------------------------------------

// 0x00552E10
CMatchFlow* CMatchFlow::Construct()
{
    m_vtable = kGameFlowVtable;
    m_runScript = nullptr;
    m_name = 0;
    m_pending = false;
    return this;
}

// 0x00552F30
CMatchFlow* CMatchFlow::Destroy(uint8_t flags)
{
    m_vtable = kGameFlowVtable;
    if (Scripts::CScript* script = m_runScript) {
        script->Destroy();
        Scripts::CScript::operator delete(script);
    }
    if (flags & 1)
        MemSys::Free(this);
    return this;
}

// 0x00552E30
void CMatchFlow::Request(uint32_t name)
{
    m_name = name;
    m_pending = true;
}

// 0x00552E40
void CMatchFlow::Update()
{
    if (m_pending) {
        m_pending = false;
        if (Scripts::CScript* old = m_runScript) {
            old->Destroy();
            Scripts::CScript::operator delete(old);
        }
        void* memory = Scripts::CScript::operator new(0xF4);
        auto* script = memory ? static_cast<Scripts::CScript*>(memory)->Construct() : nullptr;
        m_runScript = script;
        script->BindScript(m_name, nullptr, nullptr);
    }
    if (m_runScript && static_cast<int>(m_runScript->Update()) == 0) {
        if (Scripts::CScript* done = m_runScript) {
            done->Destroy();
            Scripts::CScript::operator delete(done);
        }
        m_runScript = nullptr;
    }
}

// --- The game mode -------------------------------------------------------------------------------------------

// 0x00553DB0
CMatchRules* CMatchRules::Instance(bool create)
{
    CMatchRules* mode = TheGameMode();
    if (!mode && create) {
        void* memory = MemSys::AllocZeroed(sizeof(CMatchRules), nullptr, 1);
        mode = memory ? static_cast<CMatchRules*>(memory)->Construct() : nullptr;
        TheGameMode() = mode;
    }
    ++RefTally();
    return mode;
}

// 0x00552F70
void CMatchRules::Release()
{
    if (RefTally() > 1) {
        --RefTally();
        return;
    }
    if (CMatchRules* mode = TheGameMode())
        VirtualCall<CMatchRules*>(mode, 0, static_cast<uint8_t>(1));
    TheGameMode() = nullptr;
    RefTally() = 0;
}

// 0x00553D30
CMatchRules* CMatchRules::Construct()
{
    m_vtable = kGameModeVtable;
    m_params.Init();
    m_conditions.m_condition = kHeadMarker;
    m_conditions.m_priority = 0;
    m_conditions.m_next = &m_conditions;
    m_conditions.m_lastValue = &m_conditions;
    m_conditions.m_vtable = kConditionHeadVtable;
    ClearVictoryConditions();
    ApplyMatchStyle(kFreeskate);
    return this;
}

// 0x00553890
void CMatchRules::DestroyBody()
{
    m_vtable = kGameModeVtable;
    ClearVictoryConditions();
    // (the head's destructor: ListKit::Node's, inlined)
    m_conditions.m_vtable = kConditionNodeVtable;
    m_conditions.m_lastValue->m_next = m_conditions.m_next;
    m_conditions.m_next->m_lastValue = m_conditions.m_lastValue;
    m_conditions.m_next = &m_conditions;
    m_conditions.m_lastValue = &m_conditions;
    m_params.Clear();
}

// 0x00553CE0
CMatchRules* CMatchRules::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Free(this);
    return this;
}

// 0x00553710
void CMatchRules::ConditionNode::DestroyBody()
{
    m_vtable = kConditionNodeVtable;
    m_lastValue->m_next = m_next;
    m_next->m_lastValue = m_lastValue;
    m_next = this;
    m_lastValue = this;
}

// 0x00553730
CMatchRules::ConditionNode* CMatchRules::ConditionNode::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Free(this);
    return this;
}

// 0x00553CB0
CMatchRules::ConditionNode* CMatchRules::ConditionNode::DestroyHead(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Free(this);
    return this;
}

// 0x00553D10
void CMatchRules::Reset()
{
    ClearVictoryConditions();
    ApplyMatchStyle(kFreeskate);
}

// 0x00553770
void CMatchRules::ClearVictoryConditions()
{
    ConditionNode* node = m_conditions.m_next;
    if (node->m_condition == kHeadMarker || !node)
        return;
    while (node) {
        ConditionNode* next = node->m_next->m_condition == kHeadMarker ? nullptr : node->m_next;
        node->m_lastValue->m_next = node->m_next;
        node->m_next->m_lastValue = node->m_lastValue;
        node->m_next = node;
        node->m_lastValue = node;
        if (node->m_condition)
            VirtualCall<void*>(node->m_condition, 0, static_cast<uint8_t>(1));
        VirtualCall<void*>(node, 0, static_cast<uint8_t>(1));
        node = next;
    }
}

// 0x005537D0
void CMatchRules::BuildVictoryConditions()
{
    ClearVictoryConditions();
    Scripts::CArray* conditions;
    if (!m_params.GetArray(kVictoryConditions, &conditions, 0))
        return;
    MemSys::Manager* memory = MemSys::Manager::Get();
    memory->EnterHeapScope(memory->m_skaterInfoHeap);
    for (uint32_t i = 0; i < conditions->m_size; ++i) {
        CWinRule* condition =
            CreateVictoryCondition(reinterpret_cast<Scripts::CStruct*>(conditions->GetElement(i)));
        auto* node = static_cast<ConditionNode*>(MemSys::AllocZeroed(sizeof(ConditionNode)));
        if (node) {
            node->m_vtable = kConditionNodeVtable;
            node->m_condition = condition;
            node->m_priority = 0;
            node->m_next = node;
            node->m_lastValue = node;
        }
        // (at the end of the list; a failed allocation writes through null, as the original)
        node->m_lastValue = m_conditions.m_lastValue;
        node->m_next = &m_conditions;
        m_conditions.m_lastValue->m_next = node;
        m_conditions.m_lastValue = node;
    }
    MemSys::Manager::Get()->LeaveHeapScope();
}

// 0x00553910
bool CMatchRules::FinishRulesReached()
{
    int elimiskate = 0;
    m_params.FetchIntValue(kIsElimiskate, &elimiskate, 0);
    bool isElimiskate = elimiskate == 1;
    int scavenger = 0;
    m_params.FetchIntValue(kIsScavengerHunt, &scavenger, 0);
    bool isScavengerHunt = scavenger == 1;
    if (UsesCountdown() && !isElimiskate && !isScavengerHunt) {
        void* manager = ObjectiveBoard();
        if (CountGoals(manager, 1) > 0) {
            CObjective* goal = CurrentNetGoal(manager);
            if (goal && goal->GetSecondsRemaining() <= 0)
                return true;
        }
    }
    int terminal = 0;
    int complete = 0;
    for (uint32_t i = 0; i < CountConditions(&m_conditions); ++i) {
        // (a node past the list would be read through null, as the original)
        CWinRule* condition = ConditionAt(&m_conditions, i)->m_condition;
        if (VirtualCall<bool>(condition, 0x08)) {
            ++terminal;
            if (VirtualCall<bool>(condition, 0x0C))
                ++complete;
        }
    }
    return terminal && complete == terminal;
}

// 0x00553A60
int CMatchRules::GetFirstConditionValue()
{
    for (uint32_t i = 0; i < CountConditions(&m_conditions); ++i) {
        CWinRule* condition = ConditionAt(&m_conditions, i)->m_condition;
        if (condition)
            return *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(condition) + 4);
    }
    return 0;
}

// 0x00553AD0
bool CMatchRules::ApplyMatchStyle(uint32_t type)
{
    int maxPlayers = 8;
    OnlinePlay::Manager* net = NetManager();
    if (net && net->InNetGame())
        m_params.FetchIntValue(kMaxPlayers, &maxPlayers, true);
    m_params.Clear();
    Scripts::CArray* modes = Scripts::GetArray(kModeInfo);
    for (uint32_t i = 0; i < modes->m_size; ++i) {
        auto* mode = reinterpret_cast<Scripts::CStruct*>(modes->GetElement(i));
        uint32_t name;
        mode->ReadHashValue(kChecksum, &name, true);
        if (name != type)
            continue;
        const char* definition;
        mode->GetString(kDefinition, &definition, true);
        m_params.AppendStructureIfAny(Scripts::ReadSubStruct(definition));
        ClearVictoryConditions();
        BuildVictoryConditions();
        if (net && net->InNetGame())
            m_params.PutInt(kMaxPlayers, maxPlayers);
        return true;
    }
    return false;
}

// 0x00553BE0
bool CMatchRules::ForceMatchSettings(Scripts::CStruct* options)
{
    for (const char* name : {"time_limit_type",   // "time_limit_type"
                             "victory_condition_type",   // "victory_condition_type"
                             "screenmode"}) { // "screenmode"
        if (options->HasEntryNamed(name))
            m_params.RemoveComponent(Checksums::HashFromText(name));
    }
    m_params.AppendStructureIfAny(options);
    ClearVictoryConditions();
    BuildVictoryConditions();
    Scripts::DumpToLog(&m_params, 0);
    return true;
}

// 0x00552FB0
bool CMatchRules::KeepsRunningTotal()
{
    int accumulate = Unset(this);
    m_params.FetchIntValue(kAccumulateScore, &accumulate, true);
    return accumulate != 0;
}

// 0x00552FD0
bool CMatchRules::KeepsBestChain()
{
    int track;
    if (!m_params.FetchIntValue(kTrackBestCombo, &track, false))
        return false;
    return track != 0;
}

// 0x00553000
void CMatchRules::SetScoreAccumulation(bool on) { m_params.PutInt(kAccumulateScore, on); }

// 0x00553020
void CMatchRules::HoldPoints(bool on) { m_params.PutInt(kScoreFrozen, on); }

// 0x00553040
void CMatchRules::SetScoreDegradation(bool on) { m_params.PutInt(kDegradeScore, on); }

// 0x00553060
void CMatchRules::SetTrickScoreTracking(bool on) { m_params.PutInt(kTrackTrickScore, on); }

// 0x00553080
void CMatchRules::SetTeamGraffitiMode(uint32_t mode) { m_params.PutNameKey(kTeamGraffitiMode, mode); }

// 0x005530A0
void CMatchRules::SetGoalsMode(uint32_t mode) { m_params.PutNameKey(kGoals, mode); }

// 0x005530C0
int CMatchRules::IsScoreFrozen()
{
    int frozen = Unset(this);
    m_params.FetchIntValue(kScoreFrozen, &frozen, true);
    return frozen != 0 ? 1 : 0;
}

// 0x005530F0
bool CMatchRules::KeepsMovePoints()
{
    int track = Unset(this);
    m_params.FetchIntValue(kTrackTrickScore, &track, true);
    return track != 0;
}

// 0x00553110
bool CMatchRules::RepeatsLosePoints()
{
    int degrade = Unset(this);
    m_params.FetchIntValue(kDegradeScore, &degrade, true);
    return degrade != 0;
}

// 0x00553130
int CMatchRules::NumTeams()
{
    int teams = Unset(this);
    m_params.FetchIntValue(kNumTeams, &teams, true);
    return teams;
}

// 0x00553150
bool CMatchRules::ClockBeepsNearEnd()
{
    int beeps = Unset(this);
    m_params.FetchIntValue(kTimerBeeps, &beeps, true);
    return beeps != 0;
}

// 0x00553170
int CMatchRules::GetTimeLimit()
{
    int limit = Unset(this);
    m_params.FetchIntValue(kDefaultTimeLimit, &limit, true);
    return limit;
}

// 0x00553190
int CMatchRules::GetStartingRiderCount()
{
    int players = Unset(this);
    m_params.FetchIntValue(kInitialPlayers, &players, true);
    return players;
}

// 0x005531B0
void CMatchRules::SetRiderCeiling(int players) { m_params.PutInt(kMaxPlayers, players); }

// 0x005531D0
int CMatchRules::GetRiderCeiling()
{
    int players = Unset(this);
    m_params.FetchIntValue(kMaxPlayers, &players, true);
    return players;
}

// 0x005531F0
int CMatchRules::GetFireballLevel()
{
    int level = 3;
    m_params.FetchIntValue(kFireballLevel, &level, 0);
    return level;
}

// 0x00553220
bool CMatchRules::IsTrue(uint32_t name)
{
    int value = 0;
    m_params.FetchIntValue(name, &value, false);
    return value != 0;
}

// 0x00553250
void CMatchRules::SetRound(int round) { m_params.PutInt(kRound, round); }

// 0x00553270
int CMatchRules::GetRound()
{
    int round = 0;
    m_params.FetchIntValue(kRound, &round, false);
    return round;
}

// 0x005532A0
uint32_t CMatchRules::GetNameKey()
{
    uint32_t name = 0;
    m_params.ReadHashValue(kName, &name, 0);
    return name;
}

// 0x005532D0
uint32_t CMatchRules::GetGoalsMode()
{
    uint32_t mode = 0;
    m_params.ReadHashValue(kGoals, &mode, 0);
    return mode;
}

// 0x00553300
uint32_t CMatchRules::GetTeamGraffitiMode()
{
    uint32_t mode = 0;
    m_params.ReadHashValue(kTeamGraffitiMode, &mode, 0);
    return mode;
}

// 0x00553330
uint32_t CMatchRules::RemapOnlineNameKey(uint32_t name)
{
    switch (name) {
    case 0x30C2FFA3: return 0xF9283EE7; // nettrickattack: trickattack
    case 0x5E2EA50C: return 0xC8A82B5A; // netgraffiti: Graffiti
    case 0xC50AFFD0: return 0x23EB3DA3; // netcombomambo: combomambo
    case 0xF9D5D933: return 0xCA1F360F; // netslap: slap
    case kNetKing: return kKing;
    case kNetFirefight: return kFirefight;
    case 0x6C5FF266: return 0xA5AD2B0B; // netctf: CTF
    case 0x10F84276: return 0xF6198005; // netelimiskate: elimiskate
    default: return name;
    }
}

// 0x005533B0
int CMatchRules::GetGoalTimeLeft()
{
    void* manager = ObjectiveBoard();
    uint32_t name = 0;
    m_params.ReadHashValue(kName, &name, 0);
    CObjective* goal = GetGoal(manager, RemapOnlineNameKey(name), 0);
    return goal ? goal->GetSecondsRemaining() : 0;
}

// 0x00553400
void CMatchRules::SetGoalTimeLeft(int seconds)
{
    void* manager = ObjectiveBoard();
    uint32_t name = 0;
    m_params.ReadHashValue(kName, &name, 0);
    CObjective* goal = GetGoal(manager, RemapOnlineNameKey(name), 0);
    if (goal)
        goal->SetSecondsRemaining(seconds);
}

// 0x00553460
bool CMatchRules::UsesCountdown()
{
    // (the name looked up again for each of the first four)
    uint32_t name = 0;
    for (uint32_t clockless : {kKing, kNetKing, kScoreChallenge, kNetScoreChallenge}) {
        name = 0;
        m_params.ReadHashValue(kName, &name, 0);
        if (name == clockless)
            return false;
    }
    if (GetNameKey() == kCreateGoals || GetNameKey() == kFirefight || GetNameKey() == kNetFirefight)
        return false;
    int limit = static_cast<int>(name); // (the original's local is the name's)
    m_params.FetchIntValue(kDefaultTimeLimit, &limit, true);
    return limit != 0;
}

// 0x00553550
bool CMatchRules::ClockHaltsAtZero()
{
    if (!GameModules::Skate::IsMultiplayer()) // (the skate module's IsVersusSession)
        return false;
    if (!UsesCountdown())
        return true;
    int stop = Unset(this);
    m_params.FetchIntValue(kStopAtZero, &stop, true);
    return stop == 1;
}

// 0x005535A0
void CMatchRules::SetNumTeams(int teams)
{
    OnlinePlay::Manager* net = NetManager();
    m_params.PutInt(kNumTeams, teams);
    uint32_t name = 0;
    m_params.ReadHashValue(kName, &name, 0);
    if (name == kNetLobby)
        net->BuildTeamFlagSet(teams);
}

// 0x005535F0
int CMatchRules::IsSquadMatch()
{
    if (!NetManager()->InNetGame())
        return 0;
    int teams = Unset(this);
    m_params.FetchIntValue(kNumTeams, &teams, true);
    return teams > 0 ? 1 : 0;
}

// 0x00553630
int CMatchRules::GetViewLayout()
{
    uint32_t mode;
    m_params.ReadHashValue(kScreenMode, &mode, true);
    if (mode == kSplitScreen) {
        auto* prefs = GameModules::Skate::Instance()->GetSplitViewSettings();
        uint32_t viewport;
        prefs->GetPreference(kViewportType)->ReadHashValue(kChecksum, &viewport, true);
        if (viewport == kViewportVertical)
            return 1;
        return viewport == kViewportHorizontal ? 2 : 1;
    }
    if (mode == kSingle)
        return 0;
    return mode == kHorse ? 4 : 0;
}

// 0x005536D0
void CMatchRules::IncrementRound()
{
    int round = 0;
    m_params.FetchIntValue(kRound, &round, false);
    m_params.PutInt(kRound, round + 1);
}

// 0x00553C90
bool __cdecl CFunc_OverrideGameModeOptions(Scripts::CStruct* params, Scripts::CScript*)
{
    return TheGameMode()->ForceMatchSettings(params);
}

} // namespace Game
