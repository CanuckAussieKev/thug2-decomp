// Game::CGameMode and Game::CGameFlow: 0x00552E10-0x00553DF9 (see game_mode.h).

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
#include "hook/game.h"
#include "hook/hook.h"

namespace Game {

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

void** const kGameFlowVtable = reinterpret_cast<void**>(0x0064DD50);
void** const kGameModeVtable = reinterpret_cast<void**>(0x0064DD58);
void** const kConditionHeadVtable = reinterpret_cast<void**>(0x0064DD90);
void** const kConditionNodeVtable = reinterpret_cast<void**>(0x0064DD54);
CVictoryCondition* const kHeadMarker = reinterpret_cast<CVictoryCondition*>(-1);

// (the getters' local when an asserted integer isn't there: the original's is the pushed `this`)
int Unset(const void* mode) { return static_cast<int>(reinterpret_cast<uintptr_t>(mode)); }

CGameMode*& TheGameMode() { return GameVariable<CGameMode*>(0x007CE470); }
uint32_t& References() { return GameVariable<uint32_t>(0x007CE474); }
GameNet::Manager* NetManager() { return GameVariable<GameNet::Manager*>(0x007CCE14); }

// A virtual function of an object with a game vtable.
template <typename R, typename... A>
R VirtualCall(void* object, uint32_t slot, A... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, A...)>((*static_cast<void***>(object))[slot / 4])(object, args...);
}

// The goal manager (0x005591B0): 0x0055DC30 how many goals (`active`...) and 0x0055E980 its current net goal.
void* GoalManager() { return GetGoalManager(); }
int CountGoals(void* manager, int which) { return static_cast<CGoalManager*>(manager)->NumActiveGoals(which != 0); }
CGoal* CurrentNetGoal(void* manager) { return static_cast<CGoalManager*>(manager)->GetActiveNetGoal(); }
CGoal* GetGoal(void* manager, uint32_t id, int assert)
{
    return static_cast<CGoalManager*>(manager)->GetGoal(id, assert);
}

// The conditions' list: its length and its `index`th node (Lst::Head's, inlined by the original: each counts from
// the start).
uint32_t CountConditions(CGameMode::ConditionNode* head)
{
    uint32_t count = 0;
    CGameMode::ConditionNode* node = head->m_next;
    if (node->m_condition == kHeadMarker || !node)
        return 0;
    do {
        node = node->m_next;
        ++count;
    } while (node->m_condition != kHeadMarker && node);
    return count;
}
CGameMode::ConditionNode* ConditionAt(CGameMode::ConditionNode* head, uint32_t index)
{
    CGameMode::ConditionNode* node = head->m_next;
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
CGameFlow* CGameFlow::Construct()
{
    m_vtable = kGameFlowVtable;
    m_script = nullptr;
    m_name = 0;
    m_pending = false;
    return this;
}

// 0x00552F30
CGameFlow* CGameFlow::Destroy(uint8_t flags)
{
    m_vtable = kGameFlowVtable;
    if (Script::CScript* script = m_script) {
        script->Destroy();
        Script::CScript::operator delete(script);
    }
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x00552E30
void CGameFlow::Request(uint32_t name)
{
    m_name = name;
    m_pending = true;
}

// 0x00552E40
void CGameFlow::Update()
{
    if (m_pending) {
        m_pending = false;
        if (Script::CScript* old = m_script) {
            old->Destroy();
            Script::CScript::operator delete(old);
        }
        void* memory = Script::CScript::operator new(0xF4);
        auto* script = memory ? static_cast<Script::CScript*>(memory)->Construct() : nullptr;
        m_script = script;
        script->SetScript(m_name, nullptr, nullptr);
    }
    if (m_script && static_cast<int>(m_script->Update()) == 0) {
        if (Script::CScript* done = m_script) {
            done->Destroy();
            Script::CScript::operator delete(done);
        }
        m_script = nullptr;
    }
}

// --- The game mode -------------------------------------------------------------------------------------------

// 0x00553DB0
CGameMode* CGameMode::Instance(bool create)
{
    CGameMode* mode = TheGameMode();
    if (!mode && create) {
        void* memory = Mem::Calloc(sizeof(CGameMode), nullptr, 1);
        mode = memory ? static_cast<CGameMode*>(memory)->Construct() : nullptr;
        TheGameMode() = mode;
    }
    ++References();
    return mode;
}

// 0x00552F70
void CGameMode::Release()
{
    if (References() > 1) {
        --References();
        return;
    }
    if (CGameMode* mode = TheGameMode())
        VirtualCall<CGameMode*>(mode, 0, static_cast<uint8_t>(1));
    TheGameMode() = nullptr;
    References() = 0;
}

// 0x00553D30
CGameMode* CGameMode::Construct()
{
    m_vtable = kGameModeVtable;
    m_params.Init();
    m_conditions.m_condition = kHeadMarker;
    m_conditions.m_priority = 0;
    m_conditions.m_next = &m_conditions;
    m_conditions.m_prev = &m_conditions;
    m_conditions.m_vtable = kConditionHeadVtable;
    ClearVictoryConditions();
    LoadGameType(kFreeskate);
    return this;
}

// 0x00553890
void CGameMode::DestroyBody()
{
    m_vtable = kGameModeVtable;
    ClearVictoryConditions();
    // (the head's destructor: Lst::Node's, inlined)
    m_conditions.m_vtable = kConditionNodeVtable;
    m_conditions.m_prev->m_next = m_conditions.m_next;
    m_conditions.m_next->m_prev = m_conditions.m_prev;
    m_conditions.m_next = &m_conditions;
    m_conditions.m_prev = &m_conditions;
    m_params.Clear();
}

// 0x00553CE0
CGameMode* CGameMode::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x00553710
void CGameMode::ConditionNode::DestroyBody()
{
    m_vtable = kConditionNodeVtable;
    m_prev->m_next = m_next;
    m_next->m_prev = m_prev;
    m_next = this;
    m_prev = this;
}

// 0x00553730
CGameMode::ConditionNode* CGameMode::ConditionNode::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x00553CB0
CGameMode::ConditionNode* CGameMode::ConditionNode::DestroyHead(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x00553D10
void CGameMode::Reset()
{
    ClearVictoryConditions();
    LoadGameType(kFreeskate);
}

// 0x00553770
void CGameMode::ClearVictoryConditions()
{
    ConditionNode* node = m_conditions.m_next;
    if (node->m_condition == kHeadMarker || !node)
        return;
    while (node) {
        ConditionNode* next = node->m_next->m_condition == kHeadMarker ? nullptr : node->m_next;
        node->m_prev->m_next = node->m_next;
        node->m_next->m_prev = node->m_prev;
        node->m_next = node;
        node->m_prev = node;
        if (node->m_condition)
            VirtualCall<void*>(node->m_condition, 0, static_cast<uint8_t>(1));
        VirtualCall<void*>(node, 0, static_cast<uint8_t>(1));
        node = next;
    }
}

// 0x005537D0
void CGameMode::BuildVictoryConditions()
{
    ClearVictoryConditions();
    Script::CArray* conditions;
    if (!m_params.GetArray(kVictoryConditions, &conditions, 0))
        return;
    Mem::Manager* memory = Mem::Manager::Get();
    memory->PushContext(memory->m_skaterInfoHeap);
    for (uint32_t i = 0; i < conditions->m_size; ++i) {
        CVictoryCondition* condition =
            CreateVictoryCondition(reinterpret_cast<Script::CStruct*>(conditions->GetElement(i)));
        auto* node = static_cast<ConditionNode*>(Mem::Calloc(sizeof(ConditionNode)));
        if (node) {
            node->m_vtable = kConditionNodeVtable;
            node->m_condition = condition;
            node->m_priority = 0;
            node->m_next = node;
            node->m_prev = node;
        }
        // (at the end of the list; a failed allocation writes through null, as the original)
        node->m_prev = m_conditions.m_prev;
        node->m_next = &m_conditions;
        m_conditions.m_prev->m_next = node;
        m_conditions.m_prev = node;
    }
    Mem::Manager::Get()->PopContext();
}

// 0x00553910
bool CGameMode::EndConditionsMet()
{
    int elimiskate = 0;
    m_params.GetInteger(kIsElimiskate, &elimiskate, 0);
    bool isElimiskate = elimiskate == 1;
    int scavenger = 0;
    m_params.GetInteger(kIsScavengerHunt, &scavenger, 0);
    bool isScavengerHunt = scavenger == 1;
    if (ShouldUseClock() && !isElimiskate && !isScavengerHunt) {
        void* manager = GoalManager();
        if (CountGoals(manager, 1) > 0) {
            CGoal* goal = CurrentNetGoal(manager);
            if (goal && goal->GetTimeLeft() <= 0)
                return true;
        }
    }
    int terminal = 0;
    int complete = 0;
    for (uint32_t i = 0; i < CountConditions(&m_conditions); ++i) {
        // (a node past the list would be read through null, as the original)
        CVictoryCondition* condition = ConditionAt(&m_conditions, i)->m_condition;
        if (VirtualCall<bool>(condition, 0x08)) {
            ++terminal;
            if (VirtualCall<bool>(condition, 0x0C))
                ++complete;
        }
    }
    return terminal && complete == terminal;
}

// 0x00553A60
int CGameMode::GetFirstConditionValue()
{
    for (uint32_t i = 0; i < CountConditions(&m_conditions); ++i) {
        CVictoryCondition* condition = ConditionAt(&m_conditions, i)->m_condition;
        if (condition)
            return *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(condition) + 4);
    }
    return 0;
}

// 0x00553AD0
bool CGameMode::LoadGameType(uint32_t type)
{
    int maxPlayers = 8;
    GameNet::Manager* net = NetManager();
    if (net && net->InNetGame())
        m_params.GetInteger(kMaxPlayers, &maxPlayers, true);
    m_params.Clear();
    Script::CArray* modes = Script::GetArray(kModeInfo);
    for (uint32_t i = 0; i < modes->m_size; ++i) {
        auto* mode = reinterpret_cast<Script::CStruct*>(modes->GetElement(i));
        uint32_t name;
        mode->GetChecksum(kChecksum, &name, true);
        if (name != type)
            continue;
        const char* definition;
        mode->GetString(kDefinition, &definition, true);
        m_params.AppendStructureIfAny(Script::GetStructure(definition));
        ClearVictoryConditions();
        BuildVictoryConditions();
        if (net && net->InNetGame())
            m_params.AddInteger(kMaxPlayers, maxPlayers);
        return true;
    }
    return false;
}

// 0x00553BE0
bool CGameMode::OverrideOptions(Script::CStruct* options)
{
    for (const char* name : {reinterpret_cast<const char*>(0x0064DD80),   // "time_limit_type"
                             reinterpret_cast<const char*>(0x0064DD68),   // "victory_condition_type"
                             reinterpret_cast<const char*>(0x0064DD5C)}) { // "screenmode"
        if (options->ContainsComponentNamed(name))
            m_params.RemoveComponent(Crc::GenerateCRCFromString(name));
    }
    m_params.AppendStructureIfAny(options);
    ClearVictoryConditions();
    BuildVictoryConditions();
    Script::PrintContents(&m_params, 0);
    return true;
}

// 0x00552FB0
bool CGameMode::ShouldAccumulateScore()
{
    int accumulate = Unset(this);
    m_params.GetInteger(kAccumulateScore, &accumulate, true);
    return accumulate != 0;
}

// 0x00552FD0
bool CGameMode::ShouldTrackBestCombo()
{
    int track;
    if (!m_params.GetInteger(kTrackBestCombo, &track, false))
        return false;
    return track != 0;
}

// 0x00553000
void CGameMode::SetScoreAccumulation(bool on) { m_params.AddInteger(kAccumulateScore, on); }

// 0x00553020
void CGameMode::SetScoreFrozen(bool on) { m_params.AddInteger(kScoreFrozen, on); }

// 0x00553040
void CGameMode::SetScoreDegradation(bool on) { m_params.AddInteger(kDegradeScore, on); }

// 0x00553060
void CGameMode::SetTrickScoreTracking(bool on) { m_params.AddInteger(kTrackTrickScore, on); }

// 0x00553080
void CGameMode::SetTeamGraffitiMode(uint32_t mode) { m_params.AddChecksum(kTeamGraffitiMode, mode); }

// 0x005530A0
void CGameMode::SetGoalsMode(uint32_t mode) { m_params.AddChecksum(kGoals, mode); }

// 0x005530C0
int CGameMode::IsScoreFrozen()
{
    int frozen = Unset(this);
    m_params.GetInteger(kScoreFrozen, &frozen, true);
    return frozen != 0 ? 1 : 0;
}

// 0x005530F0
bool CGameMode::ShouldTrackTrickScore()
{
    int track = Unset(this);
    m_params.GetInteger(kTrackTrickScore, &track, true);
    return track != 0;
}

// 0x00553110
bool CGameMode::ShouldDegradeScore()
{
    int degrade = Unset(this);
    m_params.GetInteger(kDegradeScore, &degrade, true);
    return degrade != 0;
}

// 0x00553130
int CGameMode::NumTeams()
{
    int teams = Unset(this);
    m_params.GetInteger(kNumTeams, &teams, true);
    return teams;
}

// 0x00553150
bool CGameMode::ShouldTimerBeep()
{
    int beeps = Unset(this);
    m_params.GetInteger(kTimerBeeps, &beeps, true);
    return beeps != 0;
}

// 0x00553170
int CGameMode::GetTimeLimit()
{
    int limit = Unset(this);
    m_params.GetInteger(kDefaultTimeLimit, &limit, true);
    return limit;
}

// 0x00553190
int CGameMode::GetInitialNumberOfPlayers()
{
    int players = Unset(this);
    m_params.GetInteger(kInitialPlayers, &players, true);
    return players;
}

// 0x005531B0
void CGameMode::SetMaximumNumberOfPlayers(int players) { m_params.AddInteger(kMaxPlayers, players); }

// 0x005531D0
int CGameMode::GetMaximumNumberOfPlayers()
{
    int players = Unset(this);
    m_params.GetInteger(kMaxPlayers, &players, true);
    return players;
}

// 0x005531F0
int CGameMode::GetFireballLevel()
{
    int level = 3;
    m_params.GetInteger(kFireballLevel, &level, 0);
    return level;
}

// 0x00553220
bool CGameMode::IsTrue(uint32_t name)
{
    int value = 0;
    m_params.GetInteger(name, &value, false);
    return value != 0;
}

// 0x00553250
void CGameMode::SetRound(int round) { m_params.AddInteger(kRound, round); }

// 0x00553270
int CGameMode::GetRound()
{
    int round = 0;
    m_params.GetInteger(kRound, &round, false);
    return round;
}

// 0x005532A0
uint32_t CGameMode::GetNameChecksum()
{
    uint32_t name = 0;
    m_params.GetChecksum(kName, &name, 0);
    return name;
}

// 0x005532D0
uint32_t CGameMode::GetGoalsMode()
{
    uint32_t mode = 0;
    m_params.GetChecksum(kGoals, &mode, 0);
    return mode;
}

// 0x00553300
uint32_t CGameMode::GetTeamGraffitiMode()
{
    uint32_t mode = 0;
    m_params.GetChecksum(kTeamGraffitiMode, &mode, 0);
    return mode;
}

// 0x00553330
uint32_t CGameMode::ConvertNetNameChecksum(uint32_t name)
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
int CGameMode::GetGoalTimeLeft()
{
    void* manager = GoalManager();
    uint32_t name = 0;
    m_params.GetChecksum(kName, &name, 0);
    CGoal* goal = GetGoal(manager, ConvertNetNameChecksum(name), 0);
    return goal ? goal->GetTimeLeft() : 0;
}

// 0x00553400
void CGameMode::SetGoalTimeLeft(int seconds)
{
    void* manager = GoalManager();
    uint32_t name = 0;
    m_params.GetChecksum(kName, &name, 0);
    CGoal* goal = GetGoal(manager, ConvertNetNameChecksum(name), 0);
    if (goal)
        goal->SetTimeLeft(seconds);
}

// 0x00553460
bool CGameMode::ShouldUseClock()
{
    // (the name looked up again for each of the first four)
    uint32_t name = 0;
    for (uint32_t clockless : {kKing, kNetKing, kScoreChallenge, kNetScoreChallenge}) {
        name = 0;
        m_params.GetChecksum(kName, &name, 0);
        if (name == clockless)
            return false;
    }
    if (GetNameChecksum() == kCreateGoals || GetNameChecksum() == kFirefight || GetNameChecksum() == kNetFirefight)
        return false;
    int limit = static_cast<int>(name); // (the original's local is the name's)
    m_params.GetInteger(kDefaultTimeLimit, &limit, true);
    return limit != 0;
}

// 0x00553550
bool CGameMode::ShouldStopAtZero()
{
    if (!Mdl::Skate::IsMultiplayer()) // (the skate module's IsMultiplayerGame)
        return false;
    if (!ShouldUseClock())
        return true;
    int stop = Unset(this);
    m_params.GetInteger(kStopAtZero, &stop, true);
    return stop == 1;
}

// 0x005535A0
void CGameMode::SetNumTeams(int teams)
{
    GameNet::Manager* net = NetManager();
    m_params.AddInteger(kNumTeams, teams);
    uint32_t name = 0;
    m_params.GetChecksum(kName, &name, 0);
    if (name == kNetLobby)
        net->CreateTeamFlags(teams);
}

// 0x005535F0
int CGameMode::IsTeamGame()
{
    if (!NetManager()->InNetGame())
        return 0;
    int teams = Unset(this);
    m_params.GetInteger(kNumTeams, &teams, true);
    return teams > 0 ? 1 : 0;
}

// 0x00553630
int CGameMode::GetScreenMode()
{
    uint32_t mode;
    m_params.GetChecksum(kScreenMode, &mode, true);
    if (mode == kSplitScreen) {
        auto* prefs = Mdl::Skate::Instance()->GetSplitScreenPreferences();
        uint32_t viewport;
        prefs->GetPreference(kViewportType)->GetChecksum(kChecksum, &viewport, true);
        if (viewport == kViewportVertical)
            return 1;
        return viewport == kViewportHorizontal ? 2 : 1;
    }
    if (mode == kSingle)
        return 0;
    return mode == kHorse ? 4 : 0;
}

// 0x005536D0
void CGameMode::IncrementRound()
{
    int round = 0;
    m_params.GetInteger(kRound, &round, false);
    m_params.AddInteger(kRound, round + 1);
}

// 0x00553C90
bool __cdecl CFunc_OverrideGameModeOptions(Script::CStruct* params, Script::CScript*)
{
    return TheGameMode()->OverrideOptions(params);
}

} // namespace Game

REPLACE(0x00552E10, &Game::CGameFlow::Construct);
REPLACE(0x00552E30, &Game::CGameFlow::Request);
REPLACE(0x00552E40, &Game::CGameFlow::Update);
REPLACE(0x00552F30, &Game::CGameFlow::Destroy);
REPLACE(0x00552F70, Game::CGameMode::Release);
REPLACE(0x00552FB0, &Game::CGameMode::ShouldAccumulateScore);
REPLACE(0x00552FD0, &Game::CGameMode::ShouldTrackBestCombo);
REPLACE(0x00553000, &Game::CGameMode::SetScoreAccumulation);
REPLACE(0x00553020, &Game::CGameMode::SetScoreFrozen);
REPLACE(0x00553040, &Game::CGameMode::SetScoreDegradation);
REPLACE(0x00553060, &Game::CGameMode::SetTrickScoreTracking);
REPLACE(0x00553080, &Game::CGameMode::SetTeamGraffitiMode);
REPLACE(0x005530A0, &Game::CGameMode::SetGoalsMode);
REPLACE(0x005530C0, &Game::CGameMode::IsScoreFrozen);
REPLACE(0x005530F0, &Game::CGameMode::ShouldTrackTrickScore);
REPLACE(0x00553110, &Game::CGameMode::ShouldDegradeScore);
REPLACE(0x00553130, &Game::CGameMode::NumTeams);
REPLACE(0x00553150, &Game::CGameMode::ShouldTimerBeep);
REPLACE(0x00553170, &Game::CGameMode::GetTimeLimit);
REPLACE(0x00553190, &Game::CGameMode::GetInitialNumberOfPlayers);
REPLACE(0x005531B0, &Game::CGameMode::SetMaximumNumberOfPlayers);
REPLACE(0x005531D0, &Game::CGameMode::GetMaximumNumberOfPlayers);
REPLACE(0x005531F0, &Game::CGameMode::GetFireballLevel);
REPLACE(0x00553220, &Game::CGameMode::IsTrue);
REPLACE(0x00553250, &Game::CGameMode::SetRound);
REPLACE(0x00553270, &Game::CGameMode::GetRound);
REPLACE(0x005532A0, &Game::CGameMode::GetNameChecksum);
REPLACE(0x005532D0, &Game::CGameMode::GetGoalsMode);
REPLACE(0x00553300, &Game::CGameMode::GetTeamGraffitiMode);
REPLACE(0x00553330, &Game::CGameMode::ConvertNetNameChecksum);
REPLACE(0x005533B0, &Game::CGameMode::GetGoalTimeLeft);
REPLACE(0x00553400, &Game::CGameMode::SetGoalTimeLeft);
REPLACE(0x00553460, &Game::CGameMode::ShouldUseClock);
REPLACE(0x00553550, &Game::CGameMode::ShouldStopAtZero);
REPLACE(0x005535A0, &Game::CGameMode::SetNumTeams);
REPLACE(0x005535F0, &Game::CGameMode::IsTeamGame);
REPLACE(0x00553630, &Game::CGameMode::GetScreenMode);
REPLACE(0x005536D0, &Game::CGameMode::IncrementRound);
REPLACE(0x00553710, &Game::CGameMode::ConditionNode::DestroyBody);
REPLACE(0x00553730, &Game::CGameMode::ConditionNode::Destroy);
REPLACE(0x00553770, &Game::CGameMode::ClearVictoryConditions);
REPLACE(0x005537D0, &Game::CGameMode::BuildVictoryConditions);
REPLACE(0x00553890, &Game::CGameMode::DestroyBody);
REPLACE(0x00553910, &Game::CGameMode::EndConditionsMet);
REPLACE(0x00553A60, &Game::CGameMode::GetFirstConditionValue);
REPLACE(0x00553AD0, &Game::CGameMode::LoadGameType);
REPLACE(0x00553BE0, &Game::CGameMode::OverrideOptions);
REPLACE(0x00553C90, Game::CFunc_OverrideGameModeOptions);
REPLACE(0x00553CB0, &Game::CGameMode::ConditionNode::DestroyHead);
REPLACE(0x00553CE0, &Game::CGameMode::Destroy);
REPLACE(0x00553D10, &Game::CGameMode::Reset);
REPLACE(0x00553D30, &Game::CGameMode::Construct);
REPLACE(0x00553DB0, Game::CGameMode::Instance);
