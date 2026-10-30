// Skatetris, TrickBeat, TimedScoreRun and TrickObjects goals. See trick_goals.h.

#include "game/skate/trick_goals.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/front/menu_element.h"
#include "game/front/screen_element.h"
#include "game/front/screen_element_manager.h"
#include "game/front/text_element.h"
#include "game/mth/random.h"
#include "game/obj/object.h"
#include "game/obj/smtptr.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sfx/sfx.h"
#include "game/skate/profile_manager.h"
#include "game/skater/skater.h"
#include "game/sys/game_clock.h"
#include "game/sys/timer.h"
#include "game/goals/goal_manager.h"
#include "game/skate/score.h"
#include "game/skate/skate_module.h"
#include "game/goals/goal.h"
#include "game/obj/control_pad.h"
#include "game/ed/piece_menu.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "game/gfx/tod_manager.h"
#include "game/core/stubs.h"
#include "game/gfx/weather.h"
#include "hook/game_code.h"

#pragma warning(disable : 4996) // (the original's strcpy / strcat / sprintf into its fixed buffers, kept)

namespace Game {

GAME_GLOBAL(double, g_trickGoalsThreeQuarters, 0x0064E918, 0.75);
GAME_GLOBAL(float, g_trickGoalsBeatMilliseconds, 0x0064E9B4, 15000.0f);

namespace {

// Names.
constexpr uint32_t kTestingGoal = 0x54D3CAC1;          // testing_goal
constexpr uint32_t kJustWonGoal = 0xC309CAD1;          // just_won_goal
constexpr uint32_t kTricksAdded = 0xC93DA683;          // (the single combo's tricks have been added)
constexpr uint32_t kCombo = 0x4EC3CFB5;                // combo
constexpr uint32_t kSingleCombo = 0xDF47B144;          // single_combo
constexpr uint32_t kTrickTris = 0xEA7BA666;            // TrickTris
constexpr uint32_t kTrickTime = 0xDED8540A;            // trick_time
constexpr uint32_t kCurrentTrickTime = 0x648D8D2A;     // (the current interval between tricks)
constexpr uint32_t kTricksCompleted = 0x8D958F01;      // tricks_completed
constexpr uint32_t kNoAlphaCheck = 0x60B827D5;         // (0: the tricks aren't checked or faded this frame)
constexpr uint32_t kFadedAlpha = 0x855E6DA4;           // goal_tetris_faded_trick_alpha
constexpr uint32_t kUnfadedAlpha = 0xDBE5B19E;         // goal_tetris_unfaded_trick_alpha
constexpr uint32_t kExpire = 0xD34EB52B;               // expire
constexpr uint32_t kTetrisMenu = 0xE35EC715;           // tetris_tricks_menu
constexpr uint32_t kMaxTricksName = 0x89473DB7;        // max_tricks
constexpr uint32_t kId = 0x40C698AF;                   // id
constexpr uint32_t kResetContainer = 0x235BDE21;       // goal_tetris_reset_trick_container
constexpr uint32_t kRedTricks = 0xD02AC196;            // (the tricks are red)
constexpr uint32_t kTurnRedScript = 0xA39B24E0;        // (a script: a trick back from red)
constexpr uint32_t kRemoveTrick = 0x23A6A456;          // goal_tetris_remove_trick
constexpr uint32_t kAccelerationInterval = 0x01181B29; // acceleration_interval
constexpr uint32_t kAccelerationPercent = 0x076F65C8;  // acceleration_percent
constexpr uint32_t kTetrisKeyCombos = 0x7BE1E689;      // goal_tetris_key_combos
constexpr uint32_t kTetrisTricks = 0xEB79FB49;         // goal_tetris_tricks
constexpr uint32_t kTrick = 0x270F56E1;                // trick
constexpr uint32_t kGrind = 0x255ED86F;                // Grind
constexpr uint32_t kText = 0xC4745838;                 // text
constexpr uint32_t kKeyCombo = 0xACFDB27A;             // key_combo
constexpr uint32_t kRandomSpin = 0x8C8ABD19;           // random_spin
constexpr uint32_t kSpin = 0xEDF5DB70;                 // spin
constexpr uint32_t kPerfect = 0x1C39F1B9;              // perfect
constexpr uint32_t kNumTaps = 0xA4BEE6A1;              // num_taps
constexpr uint32_t kTrickMapping = 0xD544AA2D;         // trick_mapping
constexpr uint32_t kParams = 0x7031F10C;               // params
constexpr uint32_t kExtraTricks = 0x6E855102;          // ExtraTricks
constexpr uint32_t kName = 0xA1DC81F9;                 // name
constexpr uint32_t kDims = 0x34A68574;                 // dims
constexpr uint32_t kFont = 0x2F6BF72D;                 // font
constexpr uint32_t kNewTrickFont = 0xBC19C379;         // newtrickfont
constexpr uint32_t kNotFocusable = 0x1D944426;         // not_focusable
constexpr uint32_t kSmall = 0x8ABA15EC;                // small
constexpr uint32_t kTrickTextDoubleTap = 0xE7ABFB95;   // goal_tetris_trick_text_double_tap
constexpr uint32_t kTrickText = 0x8856C817;            // goal_tetris_trick_text
constexpr uint32_t kNoKeyCombo = 0x7B716096;           // no_key_combo
constexpr uint32_t kAddTrick = 0x668CC9E3;             // goal_tetris_add_trick
constexpr uint32_t kAddRedTrick = 0x8E68AFC2;          // goal_tetris_add_red_trick
constexpr uint32_t kTurnTrickRed = 0xEB016D1E;         // goal_tetris_turn_trick_red
constexpr uint32_t kComboSize = 0x67DD90CA;            // combo_size
constexpr uint32_t kTrickRemovedSound = 0xE4EA743A;    // goal_tetris_play_trick_removed_sound
constexpr uint32_t kWaitToAddTricks = 0x7824963C;      // wait_to_add_tricks
constexpr uint32_t kTimeToStop = 0xA6D96603;           // time_to_stop_adding_tricks
constexpr uint32_t kTrickTrisTotal = 0xBFD3A831;       // tricktris_total_to_win
constexpr uint32_t kTrickTrisBlockSize = 0x45A375C9;   // tricktris_block_size
// TrickBeat's.
constexpr uint32_t kBeatPattern = 0x407CCE70;          // beat_pattern
constexpr uint32_t kTempo = 0x8B4A1490;                // tempo
constexpr uint32_t kBeatWindowTime = 0x5CCDD89D;       // beat_window_time
constexpr uint32_t kTotalToWin = 0xD11D2F91;           // total_to_win
constexpr uint32_t kBeatFadedAlpha = 0x465128A1;       // goal_trick_beat_faded_trick_alpha
constexpr uint32_t kBeatUnfadedAlpha = 0xA02433CB;     // goal_trick_beat_unfaded_trick_alpha
constexpr uint32_t kWaitTime = 0xEB7BBCCA;             // wait_time
constexpr uint32_t kPulseTrick = 0x04CDC75C;           // goal_trick_beat_pulse_trick
constexpr uint32_t kBeatResetContainer = 0xE721EEFD;   // goal_trick_beat_reset_trick_container
constexpr uint32_t kBeatRemoveTrick = 0x0118FB06;      // goal_trick_beat_remove_trick
constexpr uint32_t kSounds = 0x0EDCF90E;               // sounds
constexpr uint32_t kVol = 0xF6A36814;                  // vol
constexpr uint32_t kPitch = 0xD8604126;                // pitch
constexpr uint32_t kPulse = 0xE4E93525;                // pulse
constexpr uint32_t kBlockSize = 0xCE5189EE;            // block_size
constexpr uint32_t kBeatKeyCombos = 0x548B4A38;        // goal_trick_beat_key_combos
constexpr uint32_t kBeatTricks = 0x09381808;           // goal_trick_beat_tricks
constexpr uint32_t kBeatMenu = 0xBF7DF8D6;             // trick_beat_tricks_menu
constexpr uint32_t kBeatAddTrick = 0x635CFFDF;         // goal_trick_beat_add_trick
// TrickObjects'.
constexpr uint32_t kRequiredScore = 0x1B35D319;        // Required_Score
constexpr uint32_t kGoalObjects = 0x3CCE31F0;          // goal_objects
constexpr uint32_t kClusterId = 0x3C95CCD7;            // cluster_id
constexpr uint32_t kFlag = 0x2E0B1465;                 // flag
constexpr uint32_t kSkaterState = 0x43686585;          // SkaterState (the component)
constexpr uint32_t kSkaterScore = 0xD262802F;          // SkaterScore (the component)

GAME_VTABLE(kTetrisVtable, 0x0064E8A0,
            &Game::CTetrisGoal::Destroy, &Game::CGoal::ShouldExpire, &Game::CGoal::IsExpired,
            &Game::CGoal::IsActive, Nx::VirtualSlot3Jump, &Game::CTetrisGoal::Expire, &Game::CGoal::LoadSaveData,
            &Game::CGoal::GetSaveData, &Game::CGoal::HasTimeLimit, Stubs::ReturnTrue,
            &Game::CGoal::HasComponentA302D7CD, &Game::CTetrisGoal::Activate, &Game::CGoal::SetActive,
            &Game::CTetrisGoal::Deactivate, &Game::CGoal::SetInactive, &Game::CTetrisGoal::Win,
            &Game::CGoal::Lose, &Game::CTetrisGoal::Update, &Game::CGoal::IsPaused, &Game::CGoal::SetFlag,
            &Game::CGoal::StageFlagSet, &Game::CGoal::HasWon, &Game::CGoal::NetStageFlagSet,
            &Game::CGoal::HasWonGoalOrChild, &Game::CGoal::IsInitialized, &Game::CGoal::CanRetry,
            &Game::CGoal::MarkWon, &Game::CGoal::SetStageFlag, &Game::CGoal::SetNetStageFlag,
            &Game::CGoal::UnBeat);
GAME_VTABLE(kTimedScoreRunVtable, 0x0064E938,
            &Game::CTimedScoreRunGoal::Destroy, &Game::CGoal::ShouldExpire, &Game::CGoal::IsExpired,
            &Game::CGoal::IsActive, Nx::VirtualSlot3Jump, &Game::CGoal::Expire, &Game::CGoal::LoadSaveData,
            &Game::CGoal::GetSaveData, &Game::CGoal::HasTimeLimit, Stubs::ReturnTrue,
            &Game::CTimedScoreRunGoal::IsRunning, &Game::CGoal::Activate, &Game::CGoal::SetActive,
            &Game::CGoal::Deactivate, &Game::CGoal::SetInactive, &Game::CGoal::Win, &Game::CGoal::Lose,
            &Game::CGoal::Update, &Game::CGoal::IsPaused, &Game::CGoal::SetFlag, &Game::CGoal::StageFlagSet,
            &Game::CGoal::HasWon, &Game::CGoal::NetStageFlagSet, &Game::CGoal::HasWonGoalOrChild,
            &Game::CGoal::IsInitialized, &Game::CGoal::CanRetry, &Game::CGoal::MarkWon,
            &Game::CGoal::SetStageFlag, &Game::CGoal::SetNetStageFlag, &Game::CGoal::UnBeat);
GAME_VTABLE(kTrickBeatVtable, 0x0064E9B8,
            &Game::CTrickBeatGoal::Destroy, &Game::CGoal::ShouldExpire, &Game::CGoal::IsExpired,
            &Game::CGoal::IsActive, Nx::VirtualSlot3Jump, &Game::CGoal::Expire, &Game::CGoal::LoadSaveData,
            &Game::CGoal::GetSaveData, &Game::CGoal::HasTimeLimit, Stubs::ReturnTrue,
            &Game::CGoal::HasComponentA302D7CD, &Game::CTrickBeatGoal::Activate, &Game::CGoal::SetActive,
            &Game::CTrickBeatGoal::Deactivate, &Game::CGoal::SetInactive, &Game::CTetrisGoal::Win,
            &Game::CGoal::Lose, &Game::CTrickBeatGoal::Update, &Game::CGoal::IsPaused, &Game::CGoal::SetFlag,
            &Game::CGoal::StageFlagSet, &Game::CGoal::HasWon, &Game::CGoal::NetStageFlagSet,
            &Game::CGoal::HasWonGoalOrChild, &Game::CGoal::IsInitialized, &Game::CGoal::CanRetry,
            &Game::CGoal::MarkWon, &Game::CGoal::SetStageFlag, &Game::CGoal::SetNetStageFlag,
            &Game::CGoal::UnBeat);
GAME_VTABLE(kTrickObjectsVtable, 0x0064EA30,
            &Game::CTrickObjectsGoal::Destroy, &Game::CGoal::ShouldExpire, &Game::CGoal::IsExpired,
            &Game::CGoal::IsActive, Nx::VirtualSlot3Jump, &Game::CGoal::Expire, &Game::CGoal::LoadSaveData,
            &Game::CGoal::GetSaveData, &Game::CGoal::HasTimeLimit, Stubs::ReturnTrue,
            &Game::CGoal::HasComponentA302D7CD, &Game::CGoal::Activate, &Game::CGoal::SetActive,
            &Game::CGoal::Deactivate, &Game::CGoal::SetInactive, &Game::CGoal::Win, &Game::CGoal::Lose,
            &Game::CGoal::Update, &Game::CGoal::IsPaused, &Game::CGoal::SetFlag, &Game::CGoal::StageFlagSet,
            &Game::CGoal::HasWon, &Game::CGoal::NetStageFlagSet, &Game::CGoal::HasWonGoalOrChild,
            &Game::CGoal::IsInitialized, &Game::CGoal::CanRetry, &Game::CGoal::MarkWon,
            &Game::CGoal::SetStageFlag, &Game::CGoal::SetNetStageFlag, &Game::CGoal::UnBeat);

// The red-tricks threshold (0x0064E918): three quarters of max_tricks.
double ThreeQuarters() { return g_trickGoalsThreeQuarters; }

// The C runtime's _ftol (0x006236FC): truncation, the low 32 bits of the 64-bit result.
int Ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

template <typename R, typename... A>
R Virtual(GoalBase* goal, int offset, A... args)
{
    return reinterpret_cast<R(__thiscall*)(GoalBase*, A...)>(goal->m_vtable[offset / 4])(goal, args...);
}
void ConstructGoal(GoalBase* goal, Script::CStruct* params) { reinterpret_cast<GoalBase*>(reinterpret_cast<CGoal*>(goal)->Construct(params)); }
void DestroyGoal(GoalBase* goal) { reinterpret_cast<CGoal*>(goal)->DestroyBody(); }
uint32_t GoalId(GoalBase* goal) { return reinterpret_cast<CGoal*>(goal)->GetGoalId(); }
bool BaseUpdate(GoalBase* goal) { return reinterpret_cast<CGoal*>(goal)->Update(); }
bool BaseActivate(GoalBase* goal) { return reinterpret_cast<CGoal*>(goal)->Activate(); }
// The goal manager (the skate module's, 0x005591B0) winning / losing / starting a goal.
void* GoalManager() { return GetGoalManager(); }
void WinGoal(void* manager, uint32_t id) { static_cast<CGoalManager*>(manager)->WinGoal(id); }
void LoseGoal(void* manager, uint32_t id) { static_cast<CGoalManager*>(manager)->LoseGoal(id); }

// The skate module (0x007CE478): the local skater, its score (Mdl::Score), the tricks the score has seen.
uint8_t* Skate() { return reinterpret_cast<uint8_t*>(Mdl::g_skateModule); }
uint8_t* LocalSkater() { return reinterpret_cast<uint8_t*>(Mdl::Skate::Instance()->GetLocalSkater()); }
uint8_t* ScoreOf(uint8_t* skater) { return static_cast<uint8_t*>(reinterpret_cast<Obj::CSkater*>(skater)->GetScore()); }
int TotalScore(uint8_t* score) { return reinterpret_cast<Mdl::Score*>(score)->GetTotalScore(); }
// How often the trick (by name / key combo) has been done with this spin, perfect, taps.
int TrickCount(uint8_t* score, uint32_t trick, int spin, bool perfect, int taps)
{
    return reinterpret_cast<Mdl::Score*>(score)->CountTrickAfterTaps(trick, spin, perfect, taps);
}
int KeyComboCount(uint8_t* score, uint32_t keyCombo, int spin, bool perfect, int taps)
{
    return reinterpret_cast<Mdl::Score*>(score)->CountKeyCombo(keyCombo, spin, perfect, taps);
}

// The screen elements: the manager (0x00701440), an element by id (its raw pointer: the smart pointer the lookup
// makes is let go at once, as in the original), the containers and text elements made for the tricks.
using Front::ElementPtr;
void* ElementManager() { return Front::g_screenElementManager; }
Front::CScreenElementManager* Manager(void* manager) { return static_cast<Front::CScreenElementManager*>(manager); }
Front::CScreenElement* ElementById(void* manager, uint32_t id)
{
    ElementPtr element;
    Manager(manager)->GetElement(&element, id, 1);
    Front::CScreenElement* raw = element.Get();
    element.Release();
    return raw;
}
void SetAlpha(Front::CScreenElement* element, float alpha)
{
    element->SetAlpha(alpha, 1);
}
void* NewContainer()
{
    void* memory = Mem::Calloc(0x17C);
    return memory ? static_cast<Front::CContainerElement*>(memory)->Construct() : nullptr;
}
void* NewText()
{
    void* memory = Mem::Calloc(0x1B0);
    return memory ? static_cast<Front::CTextElement*>(memory)->Construct() : nullptr;
}
void SetId(void* element, uint32_t id) { static_cast<Obj::CObject*>(element)->SetID(id); }
// The manager's +4 (adds an element) / an element's +4 (its properties from a structure).
void AddToManager(void* manager, void* element)
{
    reinterpret_cast<void(__thiscall*)(void*, void*)>((*static_cast<void***>(manager))[1])(manager, element);
}
void SetProperties(void* element, Script::CStruct* params)
{
    reinterpret_cast<void(__thiscall*)(void*, Script::CStruct*)>((*static_cast<void***>(element))[1])(element, params);
}
// `child` under `parent` (0x004ABBE0, by smart pointers made for the call).
void SetParent(void* manager, void* parent, void* child)
{
    ElementPtr childPtr;
    childPtr.Set(static_cast<Front::CScreenElement*>(child));
    ElementPtr parentPtr;
    parentPtr.Set(static_cast<Front::CScreenElement*>(parent));
    Manager(manager)->SetParent(&parentPtr, &childPtr, 1);
    parentPtr.Release();
    childPtr.Release();
}

Script::CStruct* NewStructure()
{
    Script::CStruct* memory = Script::NewStruct();
    return memory ? memory->Init() : nullptr;
}
void DeleteStructure(Script::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Script::DeleteStruct(structure);
    }
}
Script::CScript* Spawn(uint32_t name, Script::CStruct* params)
{
    return Script::SpawnScript(name, params, 0, nullptr, 0, 0, false, false, false, false);
}
void Run(uint32_t name, Script::CStruct* params) { Script::RunScript(name, params, nullptr, false, nullptr); }
// A spawned script run at once on `object`.
void RunOn(Script::CScript* script, void* object)
{
    script->m_object.Set(static_cast<Obj::CObject*>(object));
    script->Update();
}

// "<prefix><name>": a grind's front and back side names.
uint32_t PrefixedChecksum(const char* prefix, const char* name)
{
    char buffer[512];
    std::strcpy(buffer, prefix);
    std::strcat(buffer, name);
    return Crc::GenerateCRCFromString(buffer);
}

} // namespace

bool GoalBase::IsActive() { return Virtual<bool>(this, 0x0C); }
bool GoalBase::IsPaused() { return Virtual<bool>(this, 0x48); }

// --- Skatetris ---------------------------------------------------------------------------------------------------

// 0x0056F090
CTetrisGoal* CTetrisGoal::Construct(Script::CStruct* params)
{
    ConstructGoal(this, params);
    m_vtable = kTetrisVtable;
    return this;
}

// 0x0056F0B0
void CTetrisGoal::DestroyBody()
{
    m_vtable = kTetrisVtable;
    DestroyGoal(this);
}

// 0x0056F290
CTetrisGoal* CTetrisGoal::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0056F0C0
bool CTetrisGoal::Win()
{
    bool testing = m_params->ContainsFlag(kTestingGoal);
    m_params->AddChecksum(0u, kJustWonGoal);
    Virtual<void>(this, 0x34, 0, 1); // Deactivate
    m_params->RemoveFlag(kJustWonGoal);
    if (testing)
        m_params->AddChecksum(0u, kTestingGoal);
    bool won = reinterpret_cast<CGoal*>(this)->Win();
    m_params->RemoveFlag(kTestingGoal);
    return won;
}

// 0x0056F130
void CTetrisGoal::ClearTricks()
{
    m_numTricks = 0;
    m_timer = 0;
    for (int i = 0; i < kMaxTricks; ++i) {
        TetrisTrick& t = m_tricks[i];
        t.free = false;
        t.trick = 0;
        t.trick2 = 0;
        t.keyCombo = 0;
        t.spin = 0;
        t.taps = 1;
        t.perfect = false;
        m_groupUsed[i] = false;
    }
    m_params->RemoveFlag(kTricksAdded);
}

// 0x0056F180
bool CTetrisGoal::AllFree()
{
    for (const TetrisTrick& t : m_tricks) {
        if (!t.free)
            return false;
    }
    return true;
}

namespace {
bool IntegerOrFlag(Script::CStruct* params, uint32_t name)
{
    int value = 0;
    params->GetInteger(name, &value, 0);
    return value || params->ContainsFlag(name);
}
} // namespace

// 0x0056F1A0
bool CTetrisGoal::IsCombo() { return IntegerOrFlag(m_params, kCombo); }
// 0x0056F1F0
bool CTetrisGoal::IsSingleCombo() { return IntegerOrFlag(m_params, kSingleCombo); }
// 0x0056F240
bool CTetrisGoal::IsTrickTris() { return IntegerOrFlag(m_params, kTrickTris); }

// 0x0056F2C0
void CTetrisGoal::Deactivate(int a, int b)
{
    ClearTricks();
    reinterpret_cast<CGoal*>(this)->Deactivate(reinterpret_cast<bool&>(a), reinterpret_cast<bool&>(b)); // (the bytes as they are)
}

// 0x0056F2E0
void CTetrisGoal::Init()
{
    m_numTricks = 0;
    m_timer = 0;
    int trickTime;
    m_params->GetInteger(kTrickTime, &trickTime, 1);
    m_params->AddInteger(kCurrentTrickTime, trickTime);
    m_params->AddInteger(kTricksCompleted, 0);
    // The first trick comes 200 ms in (at once for a single combo).
    m_timer = IntegerOrFlag(m_params, kSingleCombo) ? 0 : trickTime - 200;
    for (int i = 0; i < kMaxTricks; ++i) {
        TetrisTrick& t = m_tricks[i];
        t.free = true;
        t.trick = 0;
        t.trick2 = 0;
        t.keyCombo = 0;
        t.spin = 0;
        t.taps = 1;
        t.perfect = false;
        m_groupUsed[i] = false;
    }
}

// 0x0056F3B0
void CTetrisGoal::UpdateAlphas()
{
    int check = 0; // (only read when given)
    bool checkGiven = m_params->GetInteger(kNoAlphaCheck, &check, 0);
    uint8_t* score = ScoreOf(LocalSkater());
    void* manager = ElementManager();
    float faded = Script::GetFloat(kFadedAlpha);
    float unfaded = Script::GetFloat(kUnfadedAlpha);
    for (TetrisTrick& t : m_tricks) {
        if (t.free)
            continue;
        Front::CScreenElement* element = ElementById(manager, t.element);
        bool done = false;
        if (!checkGiven || check) {
            if (t.keyCombo)
                done = KeyComboCount(score, t.keyCombo, t.spin, t.perfect, t.taps) > 0;
            else if (t.trick)
                done = TrickCount(score, t.trick, t.spin, t.perfect, 1) > 0 ||
                       (t.trick2 && TrickCount(score, t.trick2, t.spin, t.perfect, 1) > 0);
        }
        SetAlpha(element, done ? faded : unfaded);
    }
}

// 0x0056F500
void CTetrisGoal::Expire()
{
    if (IntegerOrFlag(m_params, kSingleCombo) || IsTrickTris()) {
        reinterpret_cast<CGoal*>(this)->Lose(); // CGoal's
        return;
    }
    if (AllFree()) {
        void* manager = GoalManager();
        WinGoal(manager, GoalId(this));
        return;
    }
    reinterpret_cast<CGoal*>(this)->RunCallbackScript(kExpire);
    if (m_state != 2)
        Virtual<void>(this, 0x34, 0, 1); // Deactivate
}

// 0x0056F5B0
void CTetrisGoal::ClearTricksAndMenu()
{
    m_numTricks = 0;
    m_timer = 0;
    for (int i = 0; i < kMaxTricks; ++i) {
        TetrisTrick& t = m_tricks[i];
        t.free = true;
        t.trick = 0;
        t.trick2 = 0;
        t.keyCombo = 0;
        m_groupUsed[i] = false;
    }
    m_params->RemoveFlag(kTricksAdded);
    Manager(ElementManager())->DestroyElement(kTetrisMenu, 1, 1, nullptr);
}

// 0x0056F610
bool CTetrisGoal::Activate()
{
    if (IsActive())
        return false;
    Init();
    return BaseActivate(this);
}

// 0x0056F630
void CTetrisGoal::RemoveTrick(int index)
{
    int maxTricks;
    m_params->GetInteger(kMaxTricksName, &maxTricks, 1);
    uint32_t id = m_tricks[index].element;
    void* manager = ElementManager();
    ElementPtr element;
    Manager(manager)->GetElement(&element, id, 1);
    Script::CStruct* params = NewStructure();
    params->AddChecksum(kId, id);
    Run(kResetContainer, params);
    DeleteStructure(params);

    // Red tricks back to normal once the stack is under three quarters full.
    int red = 0;
    m_params->GetInteger(kRedTricks, &red, 0);
    if (red == 1 && static_cast<double>(m_numTricks - 1) < static_cast<double>(maxTricks) * ThreeQuarters()) {
        m_params->AddInteger(kRedTricks, 0);
        for (TetrisTrick& t : m_tricks) {
            if (t.free)
                continue;
            Front::CScreenElement* trick = ElementById(manager, t.element);
            RunOn(Spawn(kTurnRedScript, nullptr), trick);
        }
    }

    Script::CStruct* removeParams = NewStructure();
    if (IsTrickTris())
        removeParams->AddChecksum(0u, kTrickTris);
    RunOn(Spawn(kRemoveTrick, removeParams), element.Get());
    DeleteStructure(removeParams);

    // Every acceleration_interval tricks, the interval between tricks shortens by acceleration_percent.
    int completed = 0;
    m_params->GetInteger(kTricksCompleted, &completed, 0);
    ++completed;
    int interval = 0;
    m_params->GetInteger(kAccelerationInterval, &interval, 0);
    if (interval && completed % interval == 0) {
        float percent = 0.0f;
        m_params->GetFloat(kAccelerationPercent, &percent, 0);
        int time;
        m_params->GetInteger(kCurrentTrickTime, &time, 1);
        time = Ftol((1.0 - percent) * time);
        m_params->AddInteger(kCurrentTrickTime, time);
    }
    m_params->AddInteger(kTricksCompleted, completed);
    --m_numTricks;
    m_tricks[index].free = true;
}

// 0x0056FA30
void CTetrisGoal::AddTricks(int count)
{
    int maxTricks;
    m_params->GetInteger(kMaxTricksName, &maxTricks, 1);
    int total = m_numTricks + count;
    bool combo = IntegerOrFlag(m_params, kCombo) || IsSingleCombo();
    if (total > maxTricks) {
        uint32_t id = GoalId(this);
        LoseGoal(GoalManager(), id);
        return;
    }
    int group = 0;
    if (combo) {
        for (int i = 0; i < kMaxTricks; ++i) {
            if (!m_groupUsed[i]) {
                m_groupUsed[i] = true;
                group = i;
                break;
            }
        }
    }
    for (int remaining = count; remaining > 0; --remaining) {
        void* manager = ElementManager();
        Script::CArray* keyCombos = nullptr;
        m_params->GetArray(kTetrisKeyCombos, &keyCombos, 0);
        uint32_t keyCombo = 0;
        uint32_t trickName = 0;
        int slot = 0;
        for (int i = 0; i < kMaxTricks; ++i) {
            if (m_tricks[i].free) {
                TetrisTrick& t = m_tricks[i];
                slot = i;
                t.free = false;
                t.group = group;
                t.keyCombo = 0;
                t.trick = 0;
                t.trick2 = 0;
                t.spin = 0;
                t.taps = 1;
                t.perfect = false;
                break;
            }
        }
        TetrisTrick& t = m_tricks[slot];
        const char* keyComboText = nullptr;
        int taps = 1;
        char text[512];
        if (!keyCombos) {
            // A random trick of goal_tetris_tricks.
            Script::CArray* tricks = nullptr;
            m_params->GetArray(kTetrisTricks, &tricks, 1);
            auto* trick = reinterpret_cast<Script::CStruct*>(tricks->GetElement(Mth::Rnd(static_cast<int>(tricks->m_size))));
            const char* name;
            trick->GetString(kTrick, &name, 1);
            if (trick->ContainsFlag(kGrind)) {
                t.trick = PrefixedChecksum("FS ", name);
                t.trick2 = PrefixedChecksum("BS ", name);
            } else {
                t.trick = Crc::GenerateCRCFromString(name);
            }
            const char* trickText;
            trick->GetString(kText, &trickText, 1);
            std::strcpy(text, trickText);
            if (!trick->GetChecksum(kKeyCombo, &keyCombo, 0))
                trick->GetString(kKeyCombo, &keyComboText, 0);
        } else {
            // A key combo: its trick from the skater's trick mapping.
            int index = reinterpret_cast<CGoal*>(this)->PickKeyCombo(keyCombos);
            if (index == -1)
                return;
            if (keyCombos->m_type == Script::ESYMBOLTYPE_NAME) {
                keyCombo = keyCombos->GetElement(static_cast<uint32_t>(index));
            } else {
                auto* entry = reinterpret_cast<Script::CStruct*>(keyCombos->GetElement(static_cast<uint32_t>(index)));
                entry->GetChecksum(kKeyCombo, &keyCombo, 1);
                int spin = 0;
                if (entry->GetInteger(kRandomSpin, &spin, 0)) {
                    if (spin < 360)
                        spin = 360;
                    t.spin = Mth::Rnd(spin / 180) + 1;
                    if (t.spin == 1)
                        t.spin = 0;
                }
                if (entry->GetInteger(kSpin, &spin, 0))
                    t.spin = spin / 180;
                if (entry->ContainsFlag(kPerfect))
                    t.perfect = true;
                if (entry->GetInteger(kNumTaps, &taps, 0))
                    t.taps = taps;
            }
            void* profile = Mdl::Skate::Instance()->GetProfile(static_cast<Script::CStruct*>(nullptr));
            auto* mapping = static_cast<Obj::CSkaterProfile*>(profile)->GetInfoStructure(kTrickMapping);
            const char* trickText = nullptr;
            if (!mapping->GetChecksum(keyCombo, &trickName, 0)) {
                // A special trick slot: the skater's trick in it.
                int specialSlot;
                mapping->GetInteger(keyCombo, &specialSlot, 1);
                uint8_t* skater = LocalSkater();
                if (skater) {
                    uint8_t* special = *reinterpret_cast<uint8_t**>(skater + 0x17C + specialSlot * 4);
                    (*reinterpret_cast<Script::CStruct**>(special + 4))->GetString(kName, &trickText, 1);
                    t.taps = 1;
                }
            }
            if (!trickText) {
                Script::CStruct* trick = Script::GetStructure(trickName);
                if (!trick)
                    trick = reinterpret_cast<Script::CStruct*>(Script::GetArray(trickName)->GetElement(0));
                Script::CStruct* trickParams;
                trick->GetStructure(kParams, &trickParams, 1);
                // A double tap: the trick's ExtraTricks' (if it has none, a single tap after all).
                while (taps > 1) {
                    --taps;
                    uint32_t extra;
                    if (!trickParams->GetChecksum(kExtraTricks, &extra, 0)) {
                        taps = 1;
                        t.taps = 1;
                        break;
                    }
                    auto* extraTrick = reinterpret_cast<Script::CStruct*>(Script::GetArray(extra)->GetElement(0));
                    extraTrick->GetStructure(kParams, &trickParams, 1);
                }
                trickParams->GetString(kName, &trickText, true);
            }
            text[0] = '\0';
            if (t.spin) {
#pragma warning(suppress : 4996)
                if (t.perfect)
                    std::sprintf(text, "Perfect ");
                // (written over "Perfect ": the original's too)
#pragma warning(suppress : 4996)
                std::sprintf(text, "\\c5%i\\c0 ", t.spin * 180);
            }
            std::strcat(text, trickText);
            t.keyCombo = keyCombo;
        }

        // The trick's container in the stack, its text, and its key combo's text.
        Script::CStruct* params = NewStructure();
        void* container = NewContainer();
        SetId(container, Obj::kUnassignedId);
        AddToManager(manager, container);
        int height = combo && remaining == 1 ? 40 : 20;
        params->AddPair(kDims, 100.0f, static_cast<float>(height));
        SetProperties(container, params);
        Front::CScreenElement* menu = ElementById(manager, kTetrisMenu);
        menu->SetFocusSent(0);
        SetParent(manager, menu, container);
        DeleteStructure(params);

        params = NewStructure();
        params->AddString(kText, text);
        params->AddChecksum(kFont, kNewTrickFont);
        params->AddChecksum(0u, kNotFocusable);
        void* textElement = NewText();
        SetId(textElement, Obj::kUnassignedId);
        AddToManager(manager, textElement);
        SetProperties(textElement, params);
        SetParent(manager, container, textElement);
        DeleteStructure(params);

        if (keyComboText || keyCombo) {
            Script::CStruct* texts = Script::GetStructure(t.taps == 2 ? kTrickTextDoubleTap : kTrickText);
            if (!keyComboText)
                texts->GetString(keyCombo, &keyComboText, 0);
            params = NewStructure();
            params->AddString(kText, keyComboText);
            params->AddChecksum(kFont, kSmall);
            params->AddChecksum(0u, kNotFocusable);
            void* keyText = NewText();
            SetId(keyText, Obj::kUnassignedId);
            AddToManager(manager, keyText);
            SetProperties(keyText, params);
            SetParent(manager, container, keyText);
            DeleteStructure(params);
        }
        t.element = *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(container) + 0x14); // its id
        ++m_numTricks;

        // The add script (red once the stack is three quarters full); then the stack's tricks turned red.
        params = NewStructure();
        if (!keyComboText)
            params->AddChecksum(0u, kNoKeyCombo);
        auto stackFull = [&]() {
            return !IntegerOrFlag(m_params, kTrickTris) && !IntegerOrFlag(m_params, kSingleCombo) &&
                   static_cast<double>(maxTricks) * ThreeQuarters() < static_cast<double>(m_numTricks);
        };
        uint32_t addScript = stackFull() ? kAddRedTrick : kAddTrick;
        RunOn(Spawn(addScript, params), container);
        DeleteStructure(params);
        if (stackFull()) {
            m_params->AddInteger(kRedTricks, 1);
            for (TetrisTrick& other : m_tricks) {
                if (other.free)
                    continue;
                Front::CScreenElement* element = ElementById(manager, other.element);
                RunOn(Spawn(kTurnTrickRed, nullptr), element);
            }
        }
    }
}

// 0x00570A70
void CTetrisGoal::CheckCombos()
{
    uint8_t* score = ScoreOf(LocalSkater());
    int comboSize;
    m_params->GetInteger(kComboSize, &comboSize, 1);
    for (int group = 0; group < kMaxTricks; ++group) {
        if (!m_groupUsed[group])
            continue;
        bool soundPlayed = false;
        int done = 0;
        for (TetrisTrick& t : m_tricks) {
            bool inGroup = !t.free && t.group == group;
            if (t.keyCombo) {
                if (inGroup && KeyComboCount(score, t.keyCombo, t.spin, t.perfect, t.taps) > 0)
                    ++done;
            } else if (t.trick) {
                if (inGroup && TrickCount(score, t.trick, t.spin, t.perfect, t.taps) > 0)
                    ++done;
                else if (t.trick2 && inGroup && TrickCount(score, t.trick2, t.spin, t.perfect, t.taps) > 0)
                    ++done;
            }
        }
        if (done < comboSize)
            continue;
        for (int i = 0; i < kMaxTricks; ++i) {
            if (!m_tricks[i].free && m_tricks[i].group == group) {
                if (!soundPlayed)
                    Run(kTrickRemovedSound, nullptr);
                soundPlayed = true;
                RemoveTrick(i);
            }
        }
        m_groupUsed[group] = false;
        if (IntegerOrFlag(m_params, kSingleCombo)) {
            void* manager = GoalManager();
            WinGoal(manager, GoalId(this));
        }
    }
}

// 0x00570C40
bool CTetrisGoal::Update()
{
    int wait = 0;
    m_params->GetInteger(kWaitToAddTricks, &wait, 0);
    if (wait)
        return BaseUpdate(this);
    int stopTime = 1;
    if (!IsSingleCombo() && m_params->GetInteger(kTimeToStop, &stopTime, 0))
        stopTime *= 1000;
    auto win = [&]() {
        void* manager = GoalManager();
        WinGoal(manager, GoalId(this));
        return true;
    };
    if (IsTrickTris()) {
        if (IsActive() && !IsPaused()) {
            int completed = 0;
            m_params->GetInteger(kTricksCompleted, &completed, 0);
            int total;
            m_params->GetInteger(kTrickTrisTotal, &total, 1);
            if (completed >= total)
                return win();
        }
    } else if (IsActive() && m_timeLeft < stopTime && !IsPaused() && AllFree()) {
        return win();
    }
    if (IsActive() && !IsPaused())
        UpdateAlphas();
    if (IsTrickTris()) {
        if (!(m_flags & 0x10) && !(static_cast<double>(m_timeLeft) > 0.0))
            return BaseUpdate(this);
        if (IsPaused())
            return BaseUpdate(this);
        int completed = 0;
        m_params->GetInteger(kTricksCompleted, &completed, 0);
        int total;
        m_params->GetInteger(kTrickTrisTotal, &total, 1);
        int used = 0;
        for (const TetrisTrick& t : m_tricks) {
            if (!t.free)
                ++used;
        }
        int blockSize;
        m_params->GetInteger(kTrickTrisBlockSize, &blockSize, 1);
        if (used >= blockSize || used + completed >= total)
            return BaseUpdate(this);
        int count = total - used - completed;
        if (count > blockSize - used)
            count = blockSize - used;
        AddTricks(count);
        return BaseUpdate(this);
    }
    if (!IsActive() || (!(m_flags & 0x10) && m_timeLeft < stopTime) || IsPaused())
        return BaseUpdate(this);
    m_timer += Ftol(Tmr::GetSlomoFrameSeconds() * 1000.0f);
    if (IsSingleCombo()) {
        if (m_params->ContainsFlag(kTricksAdded) || m_timer <= 50)
            return BaseUpdate(this);
        int comboSize;
        m_params->GetInteger(kComboSize, &comboSize, 1);
        AddTricks(comboSize);
        m_params->AddChecksum(0u, kTricksAdded);
        return BaseUpdate(this);
    }
    int trickTime;
    m_params->GetInteger(kCurrentTrickTime, &trickTime, 1);
    if (m_timer <= trickTime)
        return BaseUpdate(this);
    m_timer = 0;
    int count = 1;
    if (IsCombo())
        m_params->GetInteger(kComboSize, &count, 1);
    AddTricks(count);
    return BaseUpdate(this);
}

// 0x00570F60
void CTetrisGoal::CheckTricks()
{
    int check;
    if (m_params->GetInteger(kNoAlphaCheck, &check, 0)) {
        if (!check)
            return;
        m_params->AddInteger(kNoAlphaCheck, 0);
    }
    bool combo = IntegerOrFlag(m_params, kSingleCombo) || IsCombo();
    uint8_t* score = ScoreOf(LocalSkater());
    if (combo) {
        CheckCombos();
        return;
    }
    bool soundPlayed = false;
    for (int i = 0; i < kMaxTricks; ++i) {
        TetrisTrick& t = m_tricks[i];
        if (t.free)
            continue;
        bool done;
        if (t.trick)
            done = TrickCount(score, t.trick, t.spin, t.perfect, 1) != 0 ||
                   (t.trick2 && TrickCount(score, t.trick2, t.spin, t.perfect, 1) != 0);
        else
            done = t.keyCombo && KeyComboCount(score, t.keyCombo, t.spin, t.perfect, t.taps) != 0;
        if (!done)
            continue;
        if (!soundPlayed)
            Run(kTrickRemovedSound, nullptr);
        soundPlayed = true;
        RemoveTrick(i);
    }
}

// --- TimedScoreRun -------------------------------------------------------------------------------------------------

// 0x005710B0
CTimedScoreRunGoal* CTimedScoreRunGoal::Construct(Script::CStruct* params)
{
    ConstructGoal(this, params);
    m_vtable = kTimedScoreRunVtable;
    return this;
}

// 0x005710D0
void CTimedScoreRunGoal::DestroyBody()
{
    m_vtable = kTimedScoreRunVtable;
    DestroyGoal(this);
}

// 0x00571110
CTimedScoreRunGoal* CTimedScoreRunGoal::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x005710E0
int CTimedScoreRunGoal::IsRunning() { return IsActive() && !IsPaused() ? 1 : 0; }

// --- TrickBeat -----------------------------------------------------------------------------------------------------

// 0x00574960
void CTrickBeatGoal::SetBeatPattern(Script::CArray* pattern, int tempo)
{
    Script::CopyArray(m_pattern, pattern);
    m_beatLength = static_cast<float>(g_trickGoalsBeatMilliseconds / static_cast<double>(tempo)); // 15000 / tempo
}

// 0x00574990
void CTrickBeatGoal::ClearTricks()
{
    m_numTricks = 0;
    m_beatTime = 0;
    for (int i = 0; i < kMaxTricks; ++i) {
        BeatTrick& t = m_tricks[i];
        t.free = true; // (overwritten below: the original stores both)
        t.trick = 0;
        t.trick2 = 0;
        t.landed = false;
        t.keyCombo = 0;
        t.spin = 0;
        t.taps = 1;
        t.perfect = false;
        t.free = false;
        m_unknown45C[i] = false;
    }
    m_params->RemoveFlag(kTricksAdded);
}

// 0x005749F0
void CTrickBeatGoal::Deactivate(int a, int b)
{
    ClearTricks();
    reinterpret_cast<CGoal*>(this)->Deactivate(reinterpret_cast<bool&>(a), reinterpret_cast<bool&>(b)); // (the bytes as they are)
}

namespace {
int GameClock() { return Tmr::GameClockElapsed() + Obj::g_clockOffset; }
} // namespace

// 0x00574A10
void CTrickBeatGoal::Init()
{
    m_lastTime = GameClock();
    m_numTricks = 0;
    m_beatTime = 0;
    m_completed = 0;
    m_params->GetInteger(kBeatWindowTime, &m_windowTime, 1);
    m_params->GetInteger(kTotalToWin, &m_totalToWin, 1);
    m_params->AddInteger(kTricksCompleted, 0);
    for (int i = 0; i < kMaxTricks; ++i) {
        BeatTrick& t = m_tricks[i];
        t.free = true;
        t.trick = 0;
        t.trick2 = 0;
        t.landed = false;
        t.keyCombo = 0;
        t.spin = 0;
        t.taps = 1;
        t.perfect = false;
        m_unknown45C[i] = false;
    }
}

// 0x00574AB0
void CTrickBeatGoal::TrickLanded(Script::CStruct* trick)
{
    Script::CStruct* params = trick;
    trick->GetStructure(kParams, &params, 1);
    const char* name;
    params->GetString(kName, &name, 1);
    uint32_t checksum = Crc::GenerateCRCFromString(name);
    if (!m_inWindow)
        return;
    void* manager = ElementManager();
    float alpha = Script::GetFloat(kBeatFadedAlpha);
    for (BeatTrick& t : m_tricks) {
        if (t.free || t.landed || t.trick != checksum)
            continue;
        t.landed = true;
        SetAlpha(ElementById(manager, t.element), alpha);
    }
}

// 0x00574B90
void CTrickBeatGoal::ClearLanded()
{
    void* manager = ElementManager();
    float alpha = Script::GetFloat(kBeatUnfadedAlpha);
    for (BeatTrick& t : m_tricks) {
        if (t.free || !t.landed)
            continue;
        t.landed = false;
        SetAlpha(ElementById(manager, t.element), alpha);
    }
}

// 0x00574C20
void CTrickBeatGoal::ResetClock() { m_lastTime = GameClock(); }

// 0x00574C40
CTrickBeatGoal* CTrickBeatGoal::Construct(Script::CStruct* params)
{
    ConstructGoal(this, params);
    m_vtable = kTrickBeatVtable;
    Script::CArray* memory = Script::NewArray();
    m_pattern = memory ? memory->Init() : nullptr;
    m_pattern->SetSizeAndType(kBeats, Script::ESYMBOLTYPE_STRUCTURE);
    Script::CArray* pattern;
    params->GetArray(kBeatPattern, &pattern, 1);
    int tempo;
    params->GetInteger(kTempo, &tempo, 1);
    Script::CopyArray(m_pattern, pattern);
    m_beatLength = static_cast<float>(g_trickGoalsBeatMilliseconds / static_cast<double>(tempo));
    return this;
}

// 0x00574D50
bool CTrickBeatGoal::Activate()
{
    m_inWindow = false;
    if (IsActive())
        return false;
    Init();
    return BaseActivate(this);
}

// 0x00574D80
void CTrickBeatGoal::PulseTricks()
{
    for (BeatTrick& t : m_tricks) {
        if (t.free || t.landed)
            continue;
        Front::CScreenElement* element = ElementById(ElementManager(), t.element);
        Script::CStruct* params = NewStructure();
        float window = static_cast<float>(m_windowTime);
        params->AddInteger(kWaitTime, Ftol(window / static_cast<double>(Tmr::GetSlomo())));
        Script::CScript* script = Spawn(kPulseTrick, params);
        DeleteStructure(params);
        RunOn(script, element);
    }
}

// 0x00574F00
void CTrickBeatGoal::RemoveTrick(int index)
{
    uint32_t id = m_tricks[index].element;
    ElementPtr element;
    Manager(ElementManager())->GetElement(&element, id, 1);
    Script::CStruct* params = NewStructure();
    params->AddChecksum(kId, id);
    Run(kBeatResetContainer, params);
    DeleteStructure(params);
    int red = 0;
    m_params->GetInteger(kRedTricks, &red, 0); // (unused)
    Script::CStruct* removeParams = NewStructure();
    removeParams->AddChecksum(0u, kTrickTris);
    RunOn(Spawn(kBeatRemoveTrick, removeParams), element.Get());
    DeleteStructure(removeParams);
    int completed = 0;
    m_params->GetInteger(kTricksCompleted, &completed, 0);
    ++completed;
    int interval = 0;
    m_params->GetInteger(kAccelerationInterval, &interval, 0); // (unused)
    m_params->AddInteger(kTricksCompleted, completed);
    --m_numTricks;
    m_tricks[index].free = true;
}

namespace {
// A sound's volume (Sfx::sVolume, 0x2C bytes; 0x0047B030 constructs it, 0x0047AD60 silences it; its destructor is
// the empty function 0x005480A0).
struct SoundVolume {
    float channels[5];
    int type;
    uint8_t unknown18[0x14];
};
static_assert(sizeof(SoundVolume) == 0x2C, "layout must match the original");
inline void DestroySoundVolume(SoundVolume*) {}
} // namespace

// 0x00575140
void CTrickBeatGoal::DestroyBody()
{
    m_vtable = kTrickBeatVtable;
    Script::CleanUpArray(m_pattern);
    m_pattern->Clear();
    if (m_pattern) {
        m_pattern->Clear();
        Script::DeleteArray(m_pattern);
    }
    DestroyGoal(this);
}

// 0x005751C0
void CTrickBeatGoal::UpdateBeat()
{
    float slomo = Tmr::GetSlomo();
    int last = m_lastTime;
    int elapsed = Tmr::GameClockElapsed() + (Obj::g_clockOffset - last);
    m_beatTime += elapsed;
    m_lastTime += elapsed;
    double time = static_cast<double>(m_beatTime);
    if (m_beatTime < 0)
        time += Gfx::g_twoTo32; // (unsigned)
    double inverse = 1.0 / slomo;
    float scale = static_cast<float>(inverse); // (kept as a float; the beat's length uses the unrounded value)
    float beat = static_cast<float>(inverse * m_beatLength);
    if (!(time <= beat)) {
        // A beat: its sounds, then on to the next.
        auto* step = reinterpret_cast<Script::CStruct*>(m_pattern->GetElement(static_cast<uint32_t>(m_beat)));
        Script::CArray* sounds;
        if (step->GetArray(kSounds, &sounds, 0)) {
            int count = static_cast<int>(sounds->m_size);
            for (int i = 0; i < count; ++i) {
                auto* sound = reinterpret_cast<Script::CStruct*>(sounds->GetElement(static_cast<uint32_t>(i)));
                uint32_t name;
                sound->GetChecksum(0u, &name, 1);
                float volume = 100.0f;
                float pitch = 100.0f;
                sound->GetFloat(kVol, &volume, 0);
                sound->GetFloat(kPitch, &pitch, 0);
                pitch = static_cast<float>(static_cast<double>(pitch) * slomo);
                SoundVolume level;
                auto* volumeOf = reinterpret_cast<Sfx::sVolume*>(&level);
                volumeOf->Init();
                volumeOf->SetSilent();
                level.channels[0] = volume;
                level.channels[1] = volume;
                Sfx::g_sfxManager->PlaySound(name, volumeOf, pitch, 0, nullptr, 0);
                DestroySoundVolume(&level);
            }
        }
        if (++m_beat > kBeats - 1) {
            m_beat = 0;
            m_beatTime = 0;
        } else {
            m_beatTime -= Ftol(static_cast<double>(scale) * m_beatLength);
        }
        step = reinterpret_cast<Script::CStruct*>(m_pattern->GetElement(static_cast<uint32_t>(m_beat)));
        m_pulse = step->ContainsFlag(kPulse);
        return;
    }
    // The beat window: half of beat_window_time either side of the beat.
    int half = m_windowTime / 2;
    if (m_pulse) {
        if (!(time <= static_cast<double>(beat) - static_cast<double>(half) * slomo)) {
            PulseTricks();
            m_inWindow = true;
        }
    } else if (!(time <= static_cast<double>(half) * slomo)) {
        m_inWindow = false;
    }
}

// 0x00575450
void CTrickBeatGoal::AddTricks(int count)
{
    for (int remaining = count; remaining > 0; --remaining) {
        void* manager = ElementManager();
        Script::CArray* keyCombos = nullptr;
        m_params->GetArray(kBeatKeyCombos, &keyCombos, 0);
        uint32_t keyCombo = 0;
        uint32_t trickName = 0;
        int slot = 0;
        for (int i = 0; i < kMaxTricks; ++i) {
            if (m_tricks[i].free) {
                BeatTrick& t = m_tricks[i];
                slot = i;
                t.free = true;
                t.trick = 0;
                t.trick2 = 0;
                t.landed = false;
                t.keyCombo = 0;
                t.spin = 0;
                t.taps = 1;
                t.perfect = false;
                t.free = false;
                t.unknown14 = 0;
                break;
            }
        }
        BeatTrick& t = m_tricks[slot];
        const char* keyComboText = nullptr;
        int taps = 1;
        char text[512];
        if (!keyCombos) {
            Script::CArray* tricks = nullptr;
            m_params->GetArray(kBeatTricks, &tricks, 1);
            auto* trick = reinterpret_cast<Script::CStruct*>(tricks->GetElement(Mth::Rnd(static_cast<int>(tricks->m_size))));
            const char* name;
            trick->GetString(kTrick, &name, 1);
            if (trick->ContainsFlag(kGrind)) {
                t.trick = PrefixedChecksum("FS ", name);
                t.trick2 = PrefixedChecksum("BS ", name);
            } else {
                t.trick = Crc::GenerateCRCFromString(name);
            }
            const char* trickText;
            trick->GetString(kText, &trickText, 1);
            std::strcpy(text, trickText);
            if (!trick->GetChecksum(kKeyCombo, &keyCombo, 0))
                trick->GetString(kKeyCombo, &keyComboText, 0);
        } else {
            int index = reinterpret_cast<CGoal*>(this)->PickKeyCombo(keyCombos);
            if (index == -1)
                return;
            if (keyCombos->m_type == Script::ESYMBOLTYPE_NAME) {
                keyCombo = keyCombos->GetElement(static_cast<uint32_t>(index));
            } else {
                auto* entry = reinterpret_cast<Script::CStruct*>(keyCombos->GetElement(static_cast<uint32_t>(index)));
                entry->GetChecksum(kKeyCombo, &keyCombo, 1);
                int spin = 0;
                if (entry->GetInteger(kRandomSpin, &spin, 0)) {
                    if (spin < 360)
                        spin = 360;
                    t.spin = Mth::Rnd(spin / 180) + 1;
                    if (t.spin == 1)
                        t.spin = 0;
                }
                if (entry->GetInteger(kSpin, &spin, 0))
                    t.spin = spin / 180;
                if (entry->ContainsFlag(kPerfect))
                    t.perfect = true;
                if (entry->GetInteger(kNumTaps, &taps, 0))
                    t.taps = taps;
            }
            void* profile = Mdl::Skate::Instance()->GetProfile(static_cast<Script::CStruct*>(nullptr));
            auto* mapping = static_cast<Obj::CSkaterProfile*>(profile)->GetInfoStructure(kTrickMapping);
            const char* trickText = nullptr;
            if (!mapping->GetChecksum(keyCombo, &trickName, 0)) {
                int specialSlot;
                mapping->GetInteger(keyCombo, &specialSlot, 1);
                uint8_t* skater = LocalSkater();
                if (skater) {
                    uint8_t* special = *reinterpret_cast<uint8_t**>(skater + 0x17C + specialSlot * 4);
                    (*reinterpret_cast<Script::CStruct**>(special + 4))->GetString(kName, &trickText, 1);
                    t.taps = 1;
                }
            }
            if (!trickText) {
                Script::CStruct* trick = Script::GetStructure(trickName);
                if (!trick)
                    trick = reinterpret_cast<Script::CStruct*>(Script::GetArray(trickName)->GetElement(0));
                Script::CStruct* trickParams;
                trick->GetStructure(kParams, &trickParams, 1);
                while (taps > 1) {
                    --taps;
                    uint32_t extra;
                    if (!trickParams->GetChecksum(kExtraTricks, &extra, 0)) {
                        taps = 1;
                        t.taps = 1;
                        break;
                    }
                    auto* extraTrick = reinterpret_cast<Script::CStruct*>(Script::GetArray(extra)->GetElement(0));
                    extraTrick->GetStructure(kParams, &trickParams, 1);
                }
                trickParams->GetString(kName, &trickText, true);
            }
            text[0] = '\0';
            if (t.spin) {
#pragma warning(suppress : 4996)
                if (t.perfect)
                    std::sprintf(text, "Perfect ");
#pragma warning(suppress : 4996)
                std::sprintf(text, "\\c5%i\\c0 ", t.spin * 180); // (over "Perfect ", as in the original)
            }
            std::strcat(text, trickText);
            t.keyCombo = keyCombo;
            t.trick = Crc::GenerateCRCFromString(trickText); // (TrickLanded matches by the trick's name)
        }

        Script::CStruct* params = NewStructure();
        void* container = NewContainer();
        SetId(container, Obj::kUnassignedId);
        AddToManager(manager, container);
        params->AddPair(kDims, 100.0f, 20.0f);
        SetProperties(container, params);
        Front::CScreenElement* menu = ElementById(manager, kBeatMenu);
        menu->SetFocusSent(0);
        SetParent(manager, menu, container);
        DeleteStructure(params);

        params = NewStructure();
        params->AddString(kText, text);
        params->AddChecksum(kFont, kNewTrickFont);
        params->AddChecksum(0u, kNotFocusable);
        void* textElement = NewText();
        SetId(textElement, Obj::kUnassignedId);
        AddToManager(manager, textElement);
        SetProperties(textElement, params);
        SetParent(manager, container, textElement);
        DeleteStructure(params);

        if (keyComboText || keyCombo) {
            Script::CStruct* texts = Script::GetStructure(t.taps == 2 ? kTrickTextDoubleTap : kTrickText);
            if (!keyComboText)
                texts->GetString(keyCombo, &keyComboText, 0);
            params = NewStructure();
            params->AddString(kText, keyComboText);
            params->AddChecksum(kFont, kSmall);
            params->AddChecksum(0u, kNotFocusable);
            void* keyText = NewText();
            SetId(keyText, Obj::kUnassignedId);
            AddToManager(manager, keyText);
            SetProperties(keyText, params);
            SetParent(manager, container, keyText);
            DeleteStructure(params);
        }
        t.element = *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(container) + 0x14);
        ++m_numTricks;
        params = NewStructure();
        if (!keyComboText)
            params->AddChecksum(0u, kNoKeyCombo);
        RunOn(Spawn(kBeatAddTrick, params), container);
        DeleteStructure(params);
    }
}

// 0x005761D0
void CTrickBeatGoal::RemoveLandedTricks()
{
    for (int i = 0; i < kMaxTricks; ++i) {
        if (!m_tricks[i].free && m_tricks[i].landed) {
            ++m_completed;
            RemoveTrick(i);
        }
    }
}

// 0x00576210
CTrickBeatGoal* CTrickBeatGoal::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00576240
bool CTrickBeatGoal::Update()
{
    int wait = 0;
    m_params->GetInteger(kWaitToAddTricks, &wait, 0);
    if (wait)
        return BaseUpdate(this);
    if (IsActive() && !IsPaused() && m_completed >= m_totalToWin) {
        void* manager = GoalManager();
        WinGoal(manager, GoalId(this));
        return true;
    }
    if (IsActive())
        IsPaused(); // (the result isn't used)
    if (IsActive() && ((m_flags & 0x10) || static_cast<double>(m_timeLeft) > 0.0) && !IsPaused()) {
        UpdateBeat();
        int used = 0;
        for (const BeatTrick& t : m_tricks) {
            if (!t.free)
                ++used;
        }
        int blockSize;
        m_params->GetInteger(kBlockSize, &blockSize, 1);
        if (used < blockSize && m_completed + used < m_totalToWin) {
            int count = m_totalToWin - m_completed - used;
            if (count > blockSize - used)
                count = blockSize - used;
            AddTricks(count);
        }
    }
    return BaseUpdate(this);
}

// --- TrickObjects --------------------------------------------------------------------------------------------------

// 0x005763A0
CTrickObjectsGoal* CTrickObjectsGoal::Construct(Script::CStruct* params)
{
    ConstructGoal(this, params);
    m_vtable = kTrickObjectsVtable;
    return this;
}

// 0x005763C0
void CTrickObjectsGoal::DestroyBody()
{
    m_vtable = kTrickObjectsVtable;
    DestroyGoal(this);
}

// 0x005763D0
CTrickObjectsGoal* CTrickObjectsGoal::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00576400
void CTrickObjectsGoal::CheckScore()
{
    void* manager = GoalManager();
    if (!reinterpret_cast<CGoal*>(this)->AllFlagsSet()) {
        LoseGoal(manager, GoalId(this));
        return;
    }
    int required;
    if (!m_params->GetInteger(kRequiredScore, &required, 0)) {
        WinGoal(manager, GoalId(this));
        return;
    }
    uint8_t* skater = LocalSkater();
    if (!skater)
        return;
    uint8_t* score = ScoreOf(skater);
    // The points of the trick in progress (+0x14, times the multiplier +0x60) count too.
    int pending = Ftol(static_cast<double>(*reinterpret_cast<int*>(score + 0x14)) * *reinterpret_cast<float*>(score + 0x60));
    if (TotalScore(score) + pending < required)
        LoseGoal(manager, GoalId(this));
    else
        WinGoal(manager, GoalId(this));
}

// 0x005764B0
Script::CStruct* CTrickObjectsGoal::GetObjectByCluster(uint32_t cluster)
{
    Script::CArray* objects;
    m_params->GetArray(kGoalObjects, &objects, 1);
    int count = static_cast<int>(objects->m_size);
    uint32_t found = cluster; // (kept from the last entry with a cluster_id: an entry without one matches that)
    for (int i = 0; i < count; ++i) {
        auto* object = reinterpret_cast<Script::CStruct*>(objects->GetElement(static_cast<uint32_t>(i)));
        object->GetChecksum(kClusterId, &found, 1);
        if (found == cluster)
            return object;
    }
    return nullptr;
}

// 0x00576520
void CTrickObjectsGoal::ObjectHit(uint32_t cluster)
{
    Script::CStruct* object = GetObjectByCluster(cluster);
    if (!object || Virtual<bool>(this, 0x54) || reinterpret_cast<CGoal*>(this)->IsLocked())
        return;
    uint8_t* skater = LocalSkater();
    if (!skater)
        return;
    auto component = [skater](uint32_t type) {
        return reinterpret_cast<uint8_t*>(reinterpret_cast<Obj::CCompositeObject*>(skater)->GetComponent(type));
    };
    uint8_t* state = component(kSkaterState);
    uint8_t* scoreComponent = component(kSkaterScore);
    uint8_t* score = *reinterpret_cast<uint8_t**>(scoreComponent + 0x18);
    // Only while the skater is in the air (+0xE0) or has points pending.
    if (!state[0xE0] &&
        Ftol(static_cast<double>(*reinterpret_cast<int*>(score + 0x14)) * *reinterpret_cast<float*>(score + 0x60)) <= 0)
        return;
    if (!IsActive()) {
        void* manager = GoalManager();
        static_cast<CGoalManager*>(manager)->ActivateGoal(GoalId(this), false);
    }
    uint32_t flag;
    object->GetChecksum(kFlag, &flag, 1);
    if (!reinterpret_cast<CGoal*>(this)->GoalFlagSet(flag, 0))
        Virtual<void>(this, 0x4C, flag, 1, 0);
}

} // namespace Game

REPLACE(0x0056F090, &Game::CTetrisGoal::Construct);
REPLACE(0x0056F0B0, &Game::CTetrisGoal::DestroyBody);
REPLACE(0x0056F0C0, &Game::CTetrisGoal::Win);
REPLACE(0x0056F130, &Game::CTetrisGoal::ClearTricks);
REPLACE(0x0056F180, &Game::CTetrisGoal::AllFree);
REPLACE(0x0056F1A0, &Game::CTetrisGoal::IsCombo);
REPLACE(0x0056F1F0, &Game::CTetrisGoal::IsSingleCombo);
REPLACE(0x0056F240, &Game::CTetrisGoal::IsTrickTris);
REPLACE(0x0056F290, &Game::CTetrisGoal::Destroy);
REPLACE(0x0056F2C0, &Game::CTetrisGoal::Deactivate);
REPLACE(0x0056F2E0, &Game::CTetrisGoal::Init);
REPLACE(0x0056F3B0, &Game::CTetrisGoal::UpdateAlphas);
REPLACE(0x0056F500, &Game::CTetrisGoal::Expire);
REPLACE(0x0056F5B0, &Game::CTetrisGoal::ClearTricksAndMenu);
REPLACE(0x0056F610, &Game::CTetrisGoal::Activate);
REPLACE(0x0056F630, &Game::CTetrisGoal::RemoveTrick);
REPLACE(0x0056FA30, &Game::CTetrisGoal::AddTricks);
REPLACE(0x00570A70, &Game::CTetrisGoal::CheckCombos);
REPLACE(0x00570C40, &Game::CTetrisGoal::Update);
REPLACE(0x00570F60, &Game::CTetrisGoal::CheckTricks);
REPLACE(0x005710B0, &Game::CTimedScoreRunGoal::Construct);
REPLACE(0x005710D0, &Game::CTimedScoreRunGoal::DestroyBody);
REPLACE(0x005710E0, &Game::CTimedScoreRunGoal::IsRunning);
REPLACE(0x00571110, &Game::CTimedScoreRunGoal::Destroy);
REPLACE(0x00574960, &Game::CTrickBeatGoal::SetBeatPattern);
REPLACE(0x00574990, &Game::CTrickBeatGoal::ClearTricks);
REPLACE(0x005749F0, &Game::CTrickBeatGoal::Deactivate);
REPLACE(0x00574A10, &Game::CTrickBeatGoal::Init);
REPLACE(0x00574AB0, &Game::CTrickBeatGoal::TrickLanded);
REPLACE(0x00574B90, &Game::CTrickBeatGoal::ClearLanded);
REPLACE(0x00574C20, &Game::CTrickBeatGoal::ResetClock);
REPLACE(0x00574C40, &Game::CTrickBeatGoal::Construct);
REPLACE(0x00574D50, &Game::CTrickBeatGoal::Activate);
REPLACE(0x00574D80, &Game::CTrickBeatGoal::PulseTricks);
REPLACE(0x00574F00, &Game::CTrickBeatGoal::RemoveTrick);
REPLACE(0x00575140, &Game::CTrickBeatGoal::DestroyBody);
REPLACE(0x005751C0, &Game::CTrickBeatGoal::UpdateBeat);
REPLACE(0x00575450, &Game::CTrickBeatGoal::AddTricks);
REPLACE(0x005761D0, &Game::CTrickBeatGoal::RemoveLandedTricks);
REPLACE(0x00576210, &Game::CTrickBeatGoal::Destroy);
REPLACE(0x00576240, &Game::CTrickBeatGoal::Update);
REPLACE(0x005763A0, &Game::CTrickObjectsGoal::Construct);
REPLACE(0x005763C0, &Game::CTrickObjectsGoal::DestroyBody);
REPLACE(0x005763D0, &Game::CTrickObjectsGoal::Destroy);
REPLACE(0x00576400, &Game::CTrickObjectsGoal::CheckScore);
REPLACE(0x005764B0, &Game::CTrickObjectsGoal::GetObjectByCluster);
REPLACE(0x00576520, &Game::CTrickObjectsGoal::ObjectHit);
