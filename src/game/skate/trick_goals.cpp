// Skatetris, TrickBeat, TimedScoreRun and TrickObjects goals. See trick_goals.h.

#include "game/skate/trick_goals.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/checksums.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/front/menu_element.h"
#include "game/front/screen_element.h"
#include "game/front/screen_element_manager.h"
#include "game/front/text_element.h"
#include "game/maths/random.h"
#include "game/obj/entity.h"
#include "game/obj/smtptr.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"
#include "game/sound/sfx.h"
#include "game/skate/profile_manager.h"
#include "game/skater/rider.h"
#include "game/sys/game_clock.h"
#include "game/sys/stopwatch.h"
#include "game/goals/goal_manager.h"
#include "game/skate/point_tally.h"
#include "game/skate/skate_module.h"
#include "game/goals/objective.h"
#include "game/obj/control_pad.h"
#include "game/park_editing/piece_menu.h"
#include "game/core/code_address.h"
#include "game/gfx/tod_manager.h"
#include "game/core/stubs.h"
#include "game/gfx/weather.h"

#pragma warning(disable : 4996) // (the original's strcpy / strcat / sprintf into its fixed buffers, kept)

namespace GoalSystem {

double g_trickGoalsThreeQuarters{0.75};
float g_trickGoalsBeatMilliseconds{15000.0f};

namespace {

// Names.
constexpr uint32_t kTrialRunFlag = 0x54D3CAC1;          // testing_goal
constexpr uint32_t kJustWonGoal = 0xC309CAD1;          // just_won_goal
constexpr uint32_t kTricksAdded = 0xC93DA683;          // (the single combo's tricks have been added)
constexpr uint32_t kCombo = 0x4EC3CFB5;                // combo
constexpr uint32_t kSingleCombo = 0xDF47B144;          // single_combo
constexpr uint32_t kTrickTris = 0xEA7BA666;            // TrickTris
constexpr uint32_t kTrickTime = 0xDED8540A;            // trick_time
constexpr uint32_t kCurrentTrickTime = 0x648D8D2A;     // (the current interval between tricks)
constexpr uint32_t kTricksDoneCount = 0x8D958F01;      // tricks_completed
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
constexpr uint32_t kDropTrickScript = 0x23A6A456;          // goal_tetris_remove_trick
constexpr uint32_t kAccelerationInterval = 0x01181B29; // acceleration_interval
constexpr uint32_t kAccelerationPercent = 0x076F65C8;  // acceleration_percent
constexpr uint32_t kTetrisKeyCombos = 0x7BE1E689;      // goal_tetris_key_combos
constexpr uint32_t kStackTrickList = 0xEB79FB49;         // goal_tetris_tricks
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
constexpr uint32_t kPushTrickScript = 0x668CC9E3;             // goal_tetris_add_trick
constexpr uint32_t kAddRedTrick = 0x8E68AFC2;          // goal_tetris_add_red_trick
constexpr uint32_t kTurnTrickRed = 0xEB016D1E;         // goal_tetris_turn_trick_red
constexpr uint32_t kChainLength = 0x67DD90CA;            // combo_size
constexpr uint32_t kTrickRemovedSound = 0xE4EA743A;    // goal_tetris_play_trick_removed_sound
constexpr uint32_t kTrickAddDelay = 0x7824963C;      // wait_to_add_tricks
constexpr uint32_t kTimeToStop = 0xA6D96603;           // time_to_stop_adding_tricks
constexpr uint32_t kTrickTrisTotal = 0xBFD3A831;       // tricktris_total_to_win
constexpr uint32_t kStackBlockSize = 0x45A375C9;   // tricktris_block_size
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

static VtableSlot kTetrisVtable_slots[] = {&GoalSystem::CFallingTricksGoal::Teardown, &GoalSystem::CObjective::WantsToEnd, &GoalSystem::CObjective::HasRunOut,
                 &GoalSystem::CObjective::IsOperative, Engine3D::VirtualSlot3Jump, &GoalSystem::CFallingTricksGoal::Expire, &GoalSystem::CObjective::RestoreSaveBlock,
                 &GoalSystem::CObjective::CaptureSaveBlock, &GoalSystem::CObjective::HasTimeLimit, Stubs::ReturnTrue,
                 &GoalSystem::CObjective::HasComponentA302D7CD, &GoalSystem::CFallingTricksGoal::Activate, &GoalSystem::CObjective::ChangeLive,
                 &GoalSystem::CFallingTricksGoal::Disengage, &GoalSystem::CObjective::ClearEngagedBit, &GoalSystem::CFallingTricksGoal::Win,
                 &GoalSystem::CObjective::Lose, &GoalSystem::CFallingTricksGoal::TickFrame, &GoalSystem::CObjective::IsFrozen, &GoalSystem::CObjective::SetFlag,
                 &GoalSystem::CObjective::StageFlagSet, &GoalSystem::CObjective::HasWon, &GoalSystem::CObjective::NetStageFlagSet,
                 &GoalSystem::CObjective::HasWonGoalOrChild, &GoalSystem::CObjective::IsPrepared, &GoalSystem::CObjective::MayTryAgain,
                 &GoalSystem::CObjective::MarkWon, &GoalSystem::CObjective::SetStageFlag, &GoalSystem::CObjective::SetNetStageFlag,
                 &GoalSystem::CObjective::UnBeat};
void** const kTetrisVtable = reinterpret_cast<void**>(kTetrisVtable_slots);
static VtableSlot kTimedScoreRunVtable_slots[] = {&GoalSystem::CTimedScoreRunGoal::Teardown, &GoalSystem::CObjective::WantsToEnd, &GoalSystem::CObjective::HasRunOut,
                 &GoalSystem::CObjective::IsOperative, Engine3D::VirtualSlot3Jump, &GoalSystem::CObjective::Expire, &GoalSystem::CObjective::RestoreSaveBlock,
                 &GoalSystem::CObjective::CaptureSaveBlock, &GoalSystem::CObjective::HasTimeLimit, Stubs::ReturnTrue,
                 &GoalSystem::CTimedScoreRunGoal::IsUnderway, &GoalSystem::CObjective::Activate, &GoalSystem::CObjective::ChangeLive,
                 &GoalSystem::CObjective::Disengage, &GoalSystem::CObjective::ClearEngagedBit, &GoalSystem::CObjective::Win, &GoalSystem::CObjective::Lose,
                 &GoalSystem::CObjective::TickFrame, &GoalSystem::CObjective::IsFrozen, &GoalSystem::CObjective::SetFlag, &GoalSystem::CObjective::StageFlagSet,
                 &GoalSystem::CObjective::HasWon, &GoalSystem::CObjective::NetStageFlagSet, &GoalSystem::CObjective::HasWonGoalOrChild,
                 &GoalSystem::CObjective::IsPrepared, &GoalSystem::CObjective::MayTryAgain, &GoalSystem::CObjective::MarkWon,
                 &GoalSystem::CObjective::SetStageFlag, &GoalSystem::CObjective::SetNetStageFlag, &GoalSystem::CObjective::UnBeat};
void** const kTimedScoreRunVtable = reinterpret_cast<void**>(kTimedScoreRunVtable_slots);
static VtableSlot kTrickBeatVtable_slots[] = {&GoalSystem::CTrickBeatGoal::Teardown, &GoalSystem::CObjective::WantsToEnd, &GoalSystem::CObjective::HasRunOut,
                 &GoalSystem::CObjective::IsOperative, Engine3D::VirtualSlot3Jump, &GoalSystem::CObjective::Expire, &GoalSystem::CObjective::RestoreSaveBlock,
                 &GoalSystem::CObjective::CaptureSaveBlock, &GoalSystem::CObjective::HasTimeLimit, Stubs::ReturnTrue,
                 &GoalSystem::CObjective::HasComponentA302D7CD, &GoalSystem::CTrickBeatGoal::Activate, &GoalSystem::CObjective::ChangeLive,
                 &GoalSystem::CTrickBeatGoal::Disengage, &GoalSystem::CObjective::ClearEngagedBit, &GoalSystem::CFallingTricksGoal::Win,
                 &GoalSystem::CObjective::Lose, &GoalSystem::CTrickBeatGoal::TickFrame, &GoalSystem::CObjective::IsFrozen, &GoalSystem::CObjective::SetFlag,
                 &GoalSystem::CObjective::StageFlagSet, &GoalSystem::CObjective::HasWon, &GoalSystem::CObjective::NetStageFlagSet,
                 &GoalSystem::CObjective::HasWonGoalOrChild, &GoalSystem::CObjective::IsPrepared, &GoalSystem::CObjective::MayTryAgain,
                 &GoalSystem::CObjective::MarkWon, &GoalSystem::CObjective::SetStageFlag, &GoalSystem::CObjective::SetNetStageFlag,
                 &GoalSystem::CObjective::UnBeat};
void** const kTrickBeatVtable = reinterpret_cast<void**>(kTrickBeatVtable_slots);
static VtableSlot kTrickObjectsVtable_slots[] = {&GoalSystem::CTrickObjectsGoal::Teardown, &GoalSystem::CObjective::WantsToEnd, &GoalSystem::CObjective::HasRunOut,
                 &GoalSystem::CObjective::IsOperative, Engine3D::VirtualSlot3Jump, &GoalSystem::CObjective::Expire, &GoalSystem::CObjective::RestoreSaveBlock,
                 &GoalSystem::CObjective::CaptureSaveBlock, &GoalSystem::CObjective::HasTimeLimit, Stubs::ReturnTrue,
                 &GoalSystem::CObjective::HasComponentA302D7CD, &GoalSystem::CObjective::Activate, &GoalSystem::CObjective::ChangeLive,
                 &GoalSystem::CObjective::Disengage, &GoalSystem::CObjective::ClearEngagedBit, &GoalSystem::CObjective::Win, &GoalSystem::CObjective::Lose,
                 &GoalSystem::CObjective::TickFrame, &GoalSystem::CObjective::IsFrozen, &GoalSystem::CObjective::SetFlag, &GoalSystem::CObjective::StageFlagSet,
                 &GoalSystem::CObjective::HasWon, &GoalSystem::CObjective::NetStageFlagSet, &GoalSystem::CObjective::HasWonGoalOrChild,
                 &GoalSystem::CObjective::IsPrepared, &GoalSystem::CObjective::MayTryAgain, &GoalSystem::CObjective::MarkWon,
                 &GoalSystem::CObjective::SetStageFlag, &GoalSystem::CObjective::SetNetStageFlag, &GoalSystem::CObjective::UnBeat};
void** const kTrickObjectsVtable = reinterpret_cast<void**>(kTrickObjectsVtable_slots);

// The red-tricks threshold (0x0064E918): three quarters of max_tricks.
double ThreeQuarters() { return g_trickGoalsThreeQuarters; }

// The C runtime's _ftol (0x006236FC): truncation, the low 32 bits of the 64-bit result.
int Ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

template <typename R, typename... A>
R Virtual(GoalBase* goal, int offset, A... args)
{
    return reinterpret_cast<R(__thiscall*)(GoalBase*, A...)>(goal->m_vtable[offset / 4])(goal, args...);
}
void ConstructGoal(GoalBase* goal, Scripts::CStruct* params) { reinterpret_cast<GoalBase*>(reinterpret_cast<CObjective*>(goal)->MakeNew(params)); }
void DestroyGoal(GoalBase* goal) { reinterpret_cast<CObjective*>(goal)->DestroyBody(); }
uint32_t GoalId(GoalBase* goal) { return reinterpret_cast<CObjective*>(goal)->GetObjectiveKey(); }
bool BaseUpdate(GoalBase* goal) { return reinterpret_cast<CObjective*>(goal)->TickFrame(); }
bool BaseActivate(GoalBase* goal) { return reinterpret_cast<CObjective*>(goal)->Activate(); }
// The goal manager (the skate module's, 0x005591B0) winning / losing / starting a goal.
void* ObjectiveBoard() { return GetObjectiveBoard(); }
void CompleteObjective(void* manager, uint32_t id) { static_cast<CGoalManager*>(manager)->CompleteObjective(id); }
void FailObjective(void* manager, uint32_t id) { static_cast<CGoalManager*>(manager)->FailObjective(id); }

// The skate module (0x007CE478): the local skater, its score (GameModules::PointTally), the tricks the score has seen.
uint8_t* Skate() { return reinterpret_cast<uint8_t*>(GameModules::g_skateModule); }
uint8_t* LocalSkater() { return reinterpret_cast<uint8_t*>(GameModules::Skate::Sole()->GetHomeRider()); }
uint8_t* ScoreOf(uint8_t* skater) { return static_cast<uint8_t*>(reinterpret_cast<GameObjects::CRider*>(skater)->GetPointsTotal()); }
int TotalScore(uint8_t* score) { return reinterpret_cast<GameModules::PointTally*>(score)->GetTotalScore(); }
// How often the trick (by name / key combo) has been done with this spin, perfect, taps.
int TrickCount(uint8_t* score, uint32_t trick, int spin, bool perfect, int taps)
{
    return reinterpret_cast<GameModules::PointTally*>(score)->CountTrickAfterTaps(trick, spin, perfect, taps);
}
int KeyComboCount(uint8_t* score, uint32_t keyCombo, int spin, bool perfect, int taps)
{
    return reinterpret_cast<GameModules::PointTally*>(score)->CountKeyCombo(keyCombo, spin, perfect, taps);
}

// The screen elements: the manager (0x00701440), an element by id (its raw pointer: the smart pointer the lookup
// makes is let go at once, as in the original), the containers and text elements made for the tricks.
using MenuSystem::ElementPtr;
void* UiTreeHandle() { return MenuSystem::g_uiElementHub; }
MenuSystem::CUiTree* Hub(void* manager) { return static_cast<MenuSystem::CUiTree*>(manager); }
MenuSystem::CUiWidget* ElementById(void* manager, uint32_t id)
{
    ElementPtr element;
    Hub(manager)->FetchPart(&element, id, 1);
    MenuSystem::CUiWidget* raw = element.Pull();
    element.Release();
    return raw;
}
void ChangeOpacity(MenuSystem::CUiWidget* element, float alpha)
{
    element->ChangeOpacity(alpha, 1);
}
void* NewContainer()
{
    void* memory = MemSys::AllocZeroed(0x17C);
    return memory ? static_cast<MenuSystem::CGroupWidget*>(memory)->MakeNew() : nullptr;
}
void* NewText()
{
    void* memory = MemSys::AllocZeroed(0x1B0);
    return memory ? static_cast<MenuSystem::CLabelWidget*>(memory)->MakeNew() : nullptr;
}
void SetId(void* element, uint32_t id) { static_cast<GameObjects::CEntity*>(element)->ChangeSerial(id); }
// The manager's +4 (adds an element) / an element's +4 (its properties from a structure).
void AddToManager(void* manager, void* element)
{
    reinterpret_cast<void(__thiscall*)(void*, void*)>((*static_cast<void***>(manager))[1])(manager, element);
}
void ChangeTraits(void* element, Scripts::CStruct* params)
{
    reinterpret_cast<void(__thiscall*)(void*, Scripts::CStruct*)>((*static_cast<void***>(element))[1])(element, params);
}
// `child` under `parent` (0x004ABBE0, by smart pointers made for the call).
void ChangeOwner(void* manager, void* parent, void* child)
{
    ElementPtr childPtr;
    childPtr.AssignValue(static_cast<MenuSystem::CUiWidget*>(child));
    ElementPtr parentPtr;
    parentPtr.AssignValue(static_cast<MenuSystem::CUiWidget*>(parent));
    Hub(manager)->ChangeOwner(&parentPtr, &childPtr, 1);
    parentPtr.Release();
    childPtr.Release();
}

Scripts::CStruct* SpawnParamTable()
{
    Scripts::CStruct* memory = Scripts::SpawnParamBag();
    return memory ? memory->PrepareAll() : nullptr;
}
void DeleteStructure(Scripts::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Scripts::DeleteStruct(structure);
    }
}
Scripts::CScript* Spawn(uint32_t name, Scripts::CStruct* params)
{
    return Scripts::SpawnScript(name, params, 0, nullptr, 0, 0, false, false, false, false);
}
void Run(uint32_t name, Scripts::CStruct* params) { Scripts::LaunchScript(name, params, nullptr, false, nullptr); }
// A spawned script run at once on `object`.
void RunOn(Scripts::CScript* script, void* object)
{
    script->m_entity.AssignValue(static_cast<GameObjects::CEntity*>(object));
    script->TickFrame();
}

// "<prefix><name>": a grind's front and back side names.
uint32_t PrefixedChecksum(const char* prefix, const char* name)
{
    char buffer[512];
    std::strcpy(buffer, prefix);
    std::strcat(buffer, name);
    return Checksums::HashFromText(buffer);
}

} // namespace

bool GoalBase::IsOperative() { return Virtual<bool>(this, 0x0C); }
bool GoalBase::IsFrozen() { return Virtual<bool>(this, 0x48); }

// --- Skatetris ---------------------------------------------------------------------------------------------------

// 0x0056F090
CFallingTricksGoal* CFallingTricksGoal::MakeNew(Scripts::CStruct* params)
{
    ConstructGoal(this, params);
    m_vtable = kTetrisVtable;
    return this;
}

// 0x0056F0B0
void CFallingTricksGoal::DestroyBody()
{
    m_vtable = kTetrisVtable;
    DestroyGoal(this);
}

// 0x0056F290
CFallingTricksGoal* CFallingTricksGoal::Teardown(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x0056F0C0
bool CFallingTricksGoal::Win()
{
    bool testing = m_args->HasBareFlag(kTrialRunFlag);
    m_args->PutNameKey(0u, kJustWonGoal);
    Virtual<void>(this, 0x34, 0, 1); // Deactivate
    m_args->RemoveFlag(kJustWonGoal);
    if (testing)
        m_args->PutNameKey(0u, kTrialRunFlag);
    bool won = reinterpret_cast<CObjective*>(this)->Win();
    m_args->RemoveFlag(kTrialRunFlag);
    return won;
}

// 0x0056F130
void CFallingTricksGoal::ClearTricks()
{
    m_trickTotal = 0;
    m_stopwatch = 0;
    for (int i = 0; i < kMaxTricks; ++i) {
        StackedTrickEntry& t = m_tricks[i];
        t.free = false;
        t.trick = 0;
        t.trick2 = 0;
        t.keyCombo = 0;
        t.spin = 0;
        t.taps = 1;
        t.perfect = false;
        m_groupUsed[i] = false;
    }
    m_args->RemoveFlag(kTricksAdded);
}

// 0x0056F180
bool CFallingTricksGoal::AllFree()
{
    for (const StackedTrickEntry& t : m_tricks) {
        if (!t.free)
            return false;
    }
    return true;
}

namespace {
bool IntegerOrFlag(Scripts::CStruct* params, uint32_t name)
{
    int value = 0;
    params->FetchIntValue(name, &value, 0);
    return value || params->HasBareFlag(name);
}
} // namespace

// 0x0056F1A0
bool CFallingTricksGoal::IsCombo() { return IntegerOrFlag(m_args, kCombo); }
// 0x0056F1F0
bool CFallingTricksGoal::IsSingleCombo() { return IntegerOrFlag(m_args, kSingleCombo); }
// 0x0056F240
bool CFallingTricksGoal::IsTrickTris() { return IntegerOrFlag(m_args, kTrickTris); }

// 0x0056F2C0
void CFallingTricksGoal::Disengage(int a, int b)
{
    ClearTricks();
    reinterpret_cast<CObjective*>(this)->Disengage(reinterpret_cast<bool&>(a), reinterpret_cast<bool&>(b)); // (the bytes as they are)
}

// 0x0056F2E0
void CFallingTricksGoal::PrepareAll()
{
    m_trickTotal = 0;
    m_stopwatch = 0;
    int comboTime;
    m_args->FetchIntValue(kTrickTime, &comboTime, 1);
    m_args->PutInt(kCurrentTrickTime, comboTime);
    m_args->PutInt(kTricksDoneCount, 0);
    // The first trick comes 200 ms in (at once for a single combo).
    m_stopwatch = IntegerOrFlag(m_args, kSingleCombo) ? 0 : comboTime - 200;
    for (int i = 0; i < kMaxTricks; ++i) {
        StackedTrickEntry& t = m_tricks[i];
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
void CFallingTricksGoal::UpdateAlphas()
{
    int check = 0; // (only read when given)
    bool checkGiven = m_args->FetchIntValue(kNoAlphaCheck, &check, 0);
    uint8_t* score = ScoreOf(LocalSkater());
    void* manager = UiTreeHandle();
    float faded = Scripts::FetchFloat(kFadedAlpha);
    float unfaded = Scripts::FetchFloat(kUnfadedAlpha);
    for (StackedTrickEntry& t : m_tricks) {
        if (t.free)
            continue;
        MenuSystem::CUiWidget* element = ElementById(manager, t.element);
        bool done = false;
        if (!checkGiven || check) {
            if (t.keyCombo)
                done = KeyComboCount(score, t.keyCombo, t.spin, t.perfect, t.taps) > 0;
            else if (t.trick)
                done = TrickCount(score, t.trick, t.spin, t.perfect, 1) > 0 ||
                       (t.trick2 && TrickCount(score, t.trick2, t.spin, t.perfect, 1) > 0);
        }
        ChangeOpacity(element, done ? faded : unfaded);
    }
}

// 0x0056F500
void CFallingTricksGoal::Expire()
{
    if (IntegerOrFlag(m_args, kSingleCombo) || IsTrickTris()) {
        reinterpret_cast<CObjective*>(this)->Lose(); // CObjective's
        return;
    }
    if (AllFree()) {
        void* manager = ObjectiveBoard();
        CompleteObjective(manager, GoalId(this));
        return;
    }
    reinterpret_cast<CObjective*>(this)->InvokeHookScript(kExpire);
    if (m_curPhase != 2)
        Virtual<void>(this, 0x34, 0, 1); // Deactivate
}

// 0x0056F5B0
void CFallingTricksGoal::ClearTricksAndMenu()
{
    m_trickTotal = 0;
    m_stopwatch = 0;
    for (int i = 0; i < kMaxTricks; ++i) {
        StackedTrickEntry& t = m_tricks[i];
        t.free = true;
        t.trick = 0;
        t.trick2 = 0;
        t.keyCombo = 0;
        m_groupUsed[i] = false;
    }
    m_args->RemoveFlag(kTricksAdded);
    Hub(UiTreeHandle())->DismantlePart(kTetrisMenu, 1, 1, nullptr);
}

// 0x0056F610
bool CFallingTricksGoal::Activate()
{
    if (IsOperative())
        return false;
    PrepareAll();
    return BaseActivate(this);
}

// 0x0056F630
void CFallingTricksGoal::DropMoveEntry(int index)
{
    int maxTricks;
    m_args->FetchIntValue(kMaxTricksName, &maxTricks, 1);
    uint32_t id = m_tricks[index].element;
    void* manager = UiTreeHandle();
    ElementPtr element;
    Hub(manager)->FetchPart(&element, id, 1);
    Scripts::CStruct* params = SpawnParamTable();
    params->PutNameKey(kId, id);
    Run(kResetContainer, params);
    DeleteStructure(params);

    // Red tricks back to normal once the stack is under three quarters full.
    int red = 0;
    m_args->FetchIntValue(kRedTricks, &red, 0);
    if (red == 1 && static_cast<double>(m_trickTotal - 1) < static_cast<double>(maxTricks) * ThreeQuarters()) {
        m_args->PutInt(kRedTricks, 0);
        for (StackedTrickEntry& t : m_tricks) {
            if (t.free)
                continue;
            MenuSystem::CUiWidget* trick = ElementById(manager, t.element);
            RunOn(Spawn(kTurnRedScript, nullptr), trick);
        }
    }

    Scripts::CStruct* removeParams = SpawnParamTable();
    if (IsTrickTris())
        removeParams->PutNameKey(0u, kTrickTris);
    RunOn(Spawn(kDropTrickScript, removeParams), element.Pull());
    DeleteStructure(removeParams);

    // Every acceleration_interval tricks, the interval between tricks shortens by acceleration_percent.
    int completed = 0;
    m_args->FetchIntValue(kTricksDoneCount, &completed, 0);
    ++completed;
    int interval = 0;
    m_args->FetchIntValue(kAccelerationInterval, &interval, 0);
    if (interval && completed % interval == 0) {
        float percent = 0.0f;
        m_args->FetchFloat(kAccelerationPercent, &percent, 0);
        int time;
        m_args->FetchIntValue(kCurrentTrickTime, &time, 1);
        time = Ftol((1.0 - percent) * time);
        m_args->PutInt(kCurrentTrickTime, time);
    }
    m_args->PutInt(kTricksDoneCount, completed);
    --m_trickTotal;
    m_tricks[index].free = true;
}

// 0x0056FA30
void CFallingTricksGoal::AddTricks(int count)
{
    int maxTricks;
    m_args->FetchIntValue(kMaxTricksName, &maxTricks, 1);
    int total = m_trickTotal + count;
    bool combo = IntegerOrFlag(m_args, kCombo) || IsSingleCombo();
    if (total > maxTricks) {
        uint32_t id = GoalId(this);
        FailObjective(ObjectiveBoard(), id);
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
        void* manager = UiTreeHandle();
        Scripts::CArray* keyCombos = nullptr;
        m_args->FetchList(kTetrisKeyCombos, &keyCombos, 0);
        uint32_t keyCombo = 0;
        uint32_t trickName = 0;
        int slot = 0;
        for (int i = 0; i < kMaxTricks; ++i) {
            if (m_tricks[i].free) {
                StackedTrickEntry& t = m_tricks[i];
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
        StackedTrickEntry& t = m_tricks[slot];
        const char* keyComboText = nullptr;
        int taps = 1;
        char text[512];
        if (!keyCombos) {
            // A random trick of goal_tetris_tricks.
            Scripts::CArray* tricks = nullptr;
            m_args->FetchList(kStackTrickList, &tricks, 1);
            auto* trick = reinterpret_cast<Scripts::CStruct*>(tricks->FetchPart(Maths::Rnd(static_cast<int>(tricks->m_extent))));
            const char* name;
            trick->FetchText(kTrick, &name, 1);
            if (trick->HasBareFlag(kGrind)) {
                t.trick = PrefixedChecksum("FS ", name);
                t.trick2 = PrefixedChecksum("BS ", name);
            } else {
                t.trick = Checksums::HashFromText(name);
            }
            const char* trickText;
            trick->FetchText(kText, &trickText, 1);
            std::strcpy(text, trickText);
            if (!trick->ReadHashValue(kKeyCombo, &keyCombo, 0))
                trick->FetchText(kKeyCombo, &keyComboText, 0);
        } else {
            // A key combo: its trick from the skater's trick mapping.
            int index = reinterpret_cast<CObjective*>(this)->PickKeyCombo(keyCombos);
            if (index == -1)
                return;
            if (keyCombos->m_kindTag == Scripts::QBVAL_NAME) {
                keyCombo = keyCombos->FetchPart(static_cast<uint32_t>(index));
            } else {
                auto* entry = reinterpret_cast<Scripts::CStruct*>(keyCombos->FetchPart(static_cast<uint32_t>(index)));
                entry->ReadHashValue(kKeyCombo, &keyCombo, 1);
                int spin = 0;
                if (entry->FetchIntValue(kRandomSpin, &spin, 0)) {
                    if (spin < 360)
                        spin = 360;
                    t.spin = Maths::Rnd(spin / 180) + 1;
                    if (t.spin == 1)
                        t.spin = 0;
                }
                if (entry->FetchIntValue(kSpin, &spin, 0))
                    t.spin = spin / 180;
                if (entry->HasBareFlag(kPerfect))
                    t.perfect = true;
                if (entry->FetchIntValue(kNumTaps, &taps, 0))
                    t.taps = taps;
            }
            void* profile = GameModules::Skate::Sole()->FetchAccount(static_cast<Scripts::CStruct*>(nullptr));
            auto* mapping = static_cast<GameObjects::CRiderIdentity*>(profile)->FetchDetailsParams(kTrickMapping);
            const char* trickText = nullptr;
            if (!mapping->ReadHashValue(keyCombo, &trickName, 0)) {
                // A special trick slot: the skater's trick in it.
                int specialSlot;
                mapping->FetchIntValue(keyCombo, &specialSlot, 1);
                uint8_t* skater = LocalSkater();
                if (skater) {
                    uint8_t* special = *reinterpret_cast<uint8_t**>(skater + 0x17C + specialSlot * 4);
                    (*reinterpret_cast<Scripts::CStruct**>(special + 4))->FetchText(kName, &trickText, 1);
                    t.taps = 1;
                }
            }
            if (!trickText) {
                Scripts::CStruct* trick = Scripts::ReadSubStruct(trickName);
                if (!trick)
                    trick = reinterpret_cast<Scripts::CStruct*>(Scripts::FetchList(trickName)->FetchPart(0));
                Scripts::CStruct* trickParams;
                trick->ReadSubStruct(kParams, &trickParams, 1);
                // A double tap: the trick's ExtraTricks' (if it has none, a single tap after all).
                while (taps > 1) {
                    --taps;
                    uint32_t extra;
                    if (!trickParams->ReadHashValue(kExtraTricks, &extra, 0)) {
                        taps = 1;
                        t.taps = 1;
                        break;
                    }
                    auto* extraTrick = reinterpret_cast<Scripts::CStruct*>(Scripts::FetchList(extra)->FetchPart(0));
                    extraTrick->ReadSubStruct(kParams, &trickParams, 1);
                }
                trickParams->FetchText(kName, &trickText, true);
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
        Scripts::CStruct* params = SpawnParamTable();
        void* container = NewContainer();
        SetId(container, GameObjects::kUnassignedId);
        AddToManager(manager, container);
        int height = combo && remaining == 1 ? 40 : 20;
        params->InsertPair(kDims, 100.0f, static_cast<float>(height));
        ChangeTraits(container, params);
        MenuSystem::CUiWidget* menu = ElementById(manager, kTetrisMenu);
        menu->SetFocusSent(0);
        ChangeOwner(manager, menu, container);
        DeleteStructure(params);

        params = SpawnParamTable();
        params->AppendWord(kText, text);
        params->PutNameKey(kFont, kNewTrickFont);
        params->PutNameKey(0u, kNotFocusable);
        void* textElement = NewText();
        SetId(textElement, GameObjects::kUnassignedId);
        AddToManager(manager, textElement);
        ChangeTraits(textElement, params);
        ChangeOwner(manager, container, textElement);
        DeleteStructure(params);

        if (keyComboText || keyCombo) {
            Scripts::CStruct* texts = Scripts::ReadSubStruct(t.taps == 2 ? kTrickTextDoubleTap : kTrickText);
            if (!keyComboText)
                texts->FetchText(keyCombo, &keyComboText, 0);
            params = SpawnParamTable();
            params->AppendWord(kText, keyComboText);
            params->PutNameKey(kFont, kSmall);
            params->PutNameKey(0u, kNotFocusable);
            void* keyText = NewText();
            SetId(keyText, GameObjects::kUnassignedId);
            AddToManager(manager, keyText);
            ChangeTraits(keyText, params);
            ChangeOwner(manager, container, keyText);
            DeleteStructure(params);
        }
        t.element = *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(container) + 0x14); // its id
        ++m_trickTotal;

        // The add script (red once the stack is three quarters full); then the stack's tricks turned red.
        params = SpawnParamTable();
        if (!keyComboText)
            params->PutNameKey(0u, kNoKeyCombo);
        auto stackFull = [&]() {
            return !IntegerOrFlag(m_args, kTrickTris) && !IntegerOrFlag(m_args, kSingleCombo) &&
                   static_cast<double>(maxTricks) * ThreeQuarters() < static_cast<double>(m_trickTotal);
        };
        uint32_t addScript = stackFull() ? kAddRedTrick : kPushTrickScript;
        RunOn(Spawn(addScript, params), container);
        DeleteStructure(params);
        if (stackFull()) {
            m_args->PutInt(kRedTricks, 1);
            for (StackedTrickEntry& other : m_tricks) {
                if (other.free)
                    continue;
                MenuSystem::CUiWidget* element = ElementById(manager, other.element);
                RunOn(Spawn(kTurnTrickRed, nullptr), element);
            }
        }
    }
}

// 0x00570A70
void CFallingTricksGoal::CheckCombos()
{
    uint8_t* score = ScoreOf(LocalSkater());
    int comboSize;
    m_args->FetchIntValue(kChainLength, &comboSize, 1);
    for (int group = 0; group < kMaxTricks; ++group) {
        if (!m_groupUsed[group])
            continue;
        bool soundPlayed = false;
        int done = 0;
        for (StackedTrickEntry& t : m_tricks) {
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
                DropMoveEntry(i);
            }
        }
        m_groupUsed[group] = false;
        if (IntegerOrFlag(m_args, kSingleCombo)) {
            void* manager = ObjectiveBoard();
            CompleteObjective(manager, GoalId(this));
        }
    }
}

// 0x00570C40
bool CFallingTricksGoal::TickFrame()
{
    int wait = 0;
    m_args->FetchIntValue(kTrickAddDelay, &wait, 0);
    if (wait)
        return BaseUpdate(this);
    int stopTime = 1;
    if (!IsSingleCombo() && m_args->FetchIntValue(kTimeToStop, &stopTime, 0))
        stopTime *= 1000;
    auto win = [&]() {
        void* manager = ObjectiveBoard();
        CompleteObjective(manager, GoalId(this));
        return true;
    };
    if (IsTrickTris()) {
        if (IsOperative() && !IsFrozen()) {
            int completed = 0;
            m_args->FetchIntValue(kTricksDoneCount, &completed, 0);
            int total;
            m_args->FetchIntValue(kTrickTrisTotal, &total, 1);
            if (completed >= total)
                return win();
        }
    } else if (IsOperative() && m_secondsRemaining < stopTime && !IsFrozen() && AllFree()) {
        return win();
    }
    if (IsOperative() && !IsFrozen())
        UpdateAlphas();
    if (IsTrickTris()) {
        if (!(m_flagBits & 0x10) && !(static_cast<double>(m_secondsRemaining) > 0.0))
            return BaseUpdate(this);
        if (IsFrozen())
            return BaseUpdate(this);
        int completed = 0;
        m_args->FetchIntValue(kTricksDoneCount, &completed, 0);
        int total;
        m_args->FetchIntValue(kTrickTrisTotal, &total, 1);
        int used = 0;
        for (const StackedTrickEntry& t : m_tricks) {
            if (!t.free)
                ++used;
        }
        int blockSize;
        m_args->FetchIntValue(kStackBlockSize, &blockSize, 1);
        if (used >= blockSize || used + completed >= total)
            return BaseUpdate(this);
        int count = total - used - completed;
        if (count > blockSize - used)
            count = blockSize - used;
        AddTricks(count);
        return BaseUpdate(this);
    }
    if (!IsOperative() || (!(m_flagBits & 0x10) && m_secondsRemaining < stopTime) || IsFrozen())
        return BaseUpdate(this);
    m_stopwatch += Ftol(FrameClock::GetSlomoFrameSeconds() * 1000.0f);
    if (IsSingleCombo()) {
        if (m_args->HasBareFlag(kTricksAdded) || m_stopwatch <= 50)
            return BaseUpdate(this);
        int comboSize;
        m_args->FetchIntValue(kChainLength, &comboSize, 1);
        AddTricks(comboSize);
        m_args->PutNameKey(0u, kTricksAdded);
        return BaseUpdate(this);
    }
    int comboTime;
    m_args->FetchIntValue(kCurrentTrickTime, &comboTime, 1);
    if (m_stopwatch <= comboTime)
        return BaseUpdate(this);
    m_stopwatch = 0;
    int count = 1;
    if (IsCombo())
        m_args->FetchIntValue(kChainLength, &count, 1);
    AddTricks(count);
    return BaseUpdate(this);
}

// 0x00570F60
void CFallingTricksGoal::CheckTricks()
{
    int check;
    if (m_args->FetchIntValue(kNoAlphaCheck, &check, 0)) {
        if (!check)
            return;
        m_args->PutInt(kNoAlphaCheck, 0);
    }
    bool combo = IntegerOrFlag(m_args, kSingleCombo) || IsCombo();
    uint8_t* score = ScoreOf(LocalSkater());
    if (combo) {
        CheckCombos();
        return;
    }
    bool soundPlayed = false;
    for (int i = 0; i < kMaxTricks; ++i) {
        StackedTrickEntry& t = m_tricks[i];
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
        DropMoveEntry(i);
    }
}

// --- TimedScoreRun -------------------------------------------------------------------------------------------------

// 0x005710B0
CTimedScoreRunGoal* CTimedScoreRunGoal::MakeNew(Scripts::CStruct* params)
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
CTimedScoreRunGoal* CTimedScoreRunGoal::Teardown(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x005710E0
int CTimedScoreRunGoal::IsUnderway() { return IsOperative() && !IsFrozen() ? 1 : 0; }

// --- TrickBeat -----------------------------------------------------------------------------------------------------

// 0x00574960
void CTrickBeatGoal::SetBeatPattern(Scripts::CArray* pattern, int tempo)
{
    Scripts::DuplicateArray(m_pattern, pattern);
    m_beatLength = static_cast<float>(g_trickGoalsBeatMilliseconds / static_cast<double>(tempo)); // 15000 / tempo
}

// 0x00574990
void CTrickBeatGoal::ClearTricks()
{
    m_trickTotal = 0;
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
    m_args->RemoveFlag(kTricksAdded);
}

// 0x005749F0
void CTrickBeatGoal::Disengage(int a, int b)
{
    ClearTricks();
    reinterpret_cast<CObjective*>(this)->Disengage(reinterpret_cast<bool&>(a), reinterpret_cast<bool&>(b)); // (the bytes as they are)
}

namespace {
int GameClock() { return FrameClock::GameClockElapsed() + GameObjects::g_clockOffset; }
} // namespace

// 0x00574A10
void CTrickBeatGoal::PrepareAll()
{
    m_lastTime = GameClock();
    m_trickTotal = 0;
    m_beatTime = 0;
    m_completed = 0;
    m_args->FetchIntValue(kBeatWindowTime, &m_windowTime, 1);
    m_args->FetchIntValue(kTotalToWin, &m_totalToWin, 1);
    m_args->PutInt(kTricksDoneCount, 0);
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
void CTrickBeatGoal::TrickLanded(Scripts::CStruct* trick)
{
    Scripts::CStruct* params = trick;
    trick->ReadSubStruct(kParams, &params, 1);
    const char* name;
    params->FetchText(kName, &name, 1);
    uint32_t checksum = Checksums::HashFromText(name);
    if (!m_inWindow)
        return;
    void* manager = UiTreeHandle();
    float alpha = Scripts::FetchFloat(kBeatFadedAlpha);
    for (BeatTrick& t : m_tricks) {
        if (t.free || t.landed || t.trick != checksum)
            continue;
        t.landed = true;
        ChangeOpacity(ElementById(manager, t.element), alpha);
    }
}

// 0x00574B90
void CTrickBeatGoal::ClearLanded()
{
    void* manager = UiTreeHandle();
    float alpha = Scripts::FetchFloat(kBeatUnfadedAlpha);
    for (BeatTrick& t : m_tricks) {
        if (t.free || !t.landed)
            continue;
        t.landed = false;
        ChangeOpacity(ElementById(manager, t.element), alpha);
    }
}

// 0x00574C20
void CTrickBeatGoal::ResetClock() { m_lastTime = GameClock(); }

// 0x00574C40
CTrickBeatGoal* CTrickBeatGoal::MakeNew(Scripts::CStruct* params)
{
    ConstructGoal(this, params);
    m_vtable = kTrickBeatVtable;
    Scripts::CArray* memory = Scripts::NewArray();
    m_pattern = memory ? memory->PrepareAll() : nullptr;
    m_pattern->ConfigureSizeKind(kBeats, Scripts::QBVAL_STRUCT);
    Scripts::CArray* pattern;
    params->FetchList(kBeatPattern, &pattern, 1);
    int tempo;
    params->FetchIntValue(kTempo, &tempo, 1);
    Scripts::DuplicateArray(m_pattern, pattern);
    m_beatLength = static_cast<float>(g_trickGoalsBeatMilliseconds / static_cast<double>(tempo));
    return this;
}

// 0x00574D50
bool CTrickBeatGoal::Activate()
{
    m_inWindow = false;
    if (IsOperative())
        return false;
    PrepareAll();
    return BaseActivate(this);
}

// 0x00574D80
void CTrickBeatGoal::PulseTricks()
{
    for (BeatTrick& t : m_tricks) {
        if (t.free || t.landed)
            continue;
        MenuSystem::CUiWidget* element = ElementById(UiTreeHandle(), t.element);
        Scripts::CStruct* params = SpawnParamTable();
        float window = static_cast<float>(m_windowTime);
        params->PutInt(kWaitTime, Ftol(window / static_cast<double>(FrameClock::GetTimeScale())));
        Scripts::CScript* script = Spawn(kPulseTrick, params);
        DeleteStructure(params);
        RunOn(script, element);
    }
}

// 0x00574F00
void CTrickBeatGoal::DropMoveEntry(int index)
{
    uint32_t id = m_tricks[index].element;
    ElementPtr element;
    Hub(UiTreeHandle())->FetchPart(&element, id, 1);
    Scripts::CStruct* params = SpawnParamTable();
    params->PutNameKey(kId, id);
    Run(kBeatResetContainer, params);
    DeleteStructure(params);
    int red = 0;
    m_args->FetchIntValue(kRedTricks, &red, 0); // (unused)
    Scripts::CStruct* removeParams = SpawnParamTable();
    removeParams->PutNameKey(0u, kTrickTris);
    RunOn(Spawn(kBeatRemoveTrick, removeParams), element.Pull());
    DeleteStructure(removeParams);
    int completed = 0;
    m_args->FetchIntValue(kTricksDoneCount, &completed, 0);
    ++completed;
    int interval = 0;
    m_args->FetchIntValue(kAccelerationInterval, &interval, 0); // (unused)
    m_args->PutInt(kTricksDoneCount, completed);
    --m_trickTotal;
    m_tricks[index].free = true;
}

namespace {
// A sound's volume (SoundFx::volumeRef, 0x2C bytes; 0x0047B030 constructs it, 0x0047AD60 silences it; its destructor is
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
    Scripts::ReleaseArrayStorage(m_pattern);
    m_pattern->Clear();
    if (m_pattern) {
        m_pattern->Clear();
        Scripts::DeleteArray(m_pattern);
    }
    DestroyGoal(this);
}

// 0x005751C0
void CTrickBeatGoal::UpdateBeat()
{
    float slomo = FrameClock::GetTimeScale();
    int last = m_lastTime;
    int elapsed = FrameClock::GameClockElapsed() + (GameObjects::g_clockOffset - last);
    m_beatTime += elapsed;
    m_lastTime += elapsed;
    double time = static_cast<double>(m_beatTime);
    if (m_beatTime < 0)
        time += Gfx2D::g_twoTo32; // (unsigned)
    double inverse = 1.0 / slomo;
    float scale = static_cast<float>(inverse); // (kept as a float; the beat's length uses the unrounded value)
    float beat = static_cast<float>(inverse * m_beatLength);
    if (!(time <= beat)) {
        // A beat: its sounds, then on to the next.
        auto* step = reinterpret_cast<Scripts::CStruct*>(m_pattern->FetchPart(static_cast<uint32_t>(m_beat)));
        Scripts::CArray* sounds;
        if (step->FetchList(kSounds, &sounds, 0)) {
            int count = static_cast<int>(sounds->m_extent);
            for (int i = 0; i < count; ++i) {
                auto* sound = reinterpret_cast<Scripts::CStruct*>(sounds->FetchPart(static_cast<uint32_t>(i)));
                uint32_t name;
                sound->ReadHashValue(0u, &name, 1);
                float volume = 100.0f;
                float pitch = 100.0f;
                sound->FetchFloat(kVol, &volume, 0);
                sound->FetchFloat(kPitch, &pitch, 0);
                pitch = static_cast<float>(static_cast<double>(pitch) * slomo);
                SoundVolume level;
                auto* volumeOf = reinterpret_cast<SoundFx::volumeRef*>(&level);
                volumeOf->PrepareAll();
                volumeOf->ChangeMuted();
                level.channels[0] = volume;
                level.channels[1] = volume;
                SoundFx::g_audioMixer->EmitSound(name, volumeOf, pitch, 0, nullptr, 0);
                DestroySoundVolume(&level);
            }
        }
        if (++m_beat > kBeats - 1) {
            m_beat = 0;
            m_beatTime = 0;
        } else {
            m_beatTime -= Ftol(static_cast<double>(scale) * m_beatLength);
        }
        step = reinterpret_cast<Scripts::CStruct*>(m_pattern->FetchPart(static_cast<uint32_t>(m_beat)));
        m_pulse = step->HasBareFlag(kPulse);
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
        void* manager = UiTreeHandle();
        Scripts::CArray* keyCombos = nullptr;
        m_args->FetchList(kBeatKeyCombos, &keyCombos, 0);
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
            Scripts::CArray* tricks = nullptr;
            m_args->FetchList(kBeatTricks, &tricks, 1);
            auto* trick = reinterpret_cast<Scripts::CStruct*>(tricks->FetchPart(Maths::Rnd(static_cast<int>(tricks->m_extent))));
            const char* name;
            trick->FetchText(kTrick, &name, 1);
            if (trick->HasBareFlag(kGrind)) {
                t.trick = PrefixedChecksum("FS ", name);
                t.trick2 = PrefixedChecksum("BS ", name);
            } else {
                t.trick = Checksums::HashFromText(name);
            }
            const char* trickText;
            trick->FetchText(kText, &trickText, 1);
            std::strcpy(text, trickText);
            if (!trick->ReadHashValue(kKeyCombo, &keyCombo, 0))
                trick->FetchText(kKeyCombo, &keyComboText, 0);
        } else {
            int index = reinterpret_cast<CObjective*>(this)->PickKeyCombo(keyCombos);
            if (index == -1)
                return;
            if (keyCombos->m_kindTag == Scripts::QBVAL_NAME) {
                keyCombo = keyCombos->FetchPart(static_cast<uint32_t>(index));
            } else {
                auto* entry = reinterpret_cast<Scripts::CStruct*>(keyCombos->FetchPart(static_cast<uint32_t>(index)));
                entry->ReadHashValue(kKeyCombo, &keyCombo, 1);
                int spin = 0;
                if (entry->FetchIntValue(kRandomSpin, &spin, 0)) {
                    if (spin < 360)
                        spin = 360;
                    t.spin = Maths::Rnd(spin / 180) + 1;
                    if (t.spin == 1)
                        t.spin = 0;
                }
                if (entry->FetchIntValue(kSpin, &spin, 0))
                    t.spin = spin / 180;
                if (entry->HasBareFlag(kPerfect))
                    t.perfect = true;
                if (entry->FetchIntValue(kNumTaps, &taps, 0))
                    t.taps = taps;
            }
            void* profile = GameModules::Skate::Sole()->FetchAccount(static_cast<Scripts::CStruct*>(nullptr));
            auto* mapping = static_cast<GameObjects::CRiderIdentity*>(profile)->FetchDetailsParams(kTrickMapping);
            const char* trickText = nullptr;
            if (!mapping->ReadHashValue(keyCombo, &trickName, 0)) {
                int specialSlot;
                mapping->FetchIntValue(keyCombo, &specialSlot, 1);
                uint8_t* skater = LocalSkater();
                if (skater) {
                    uint8_t* special = *reinterpret_cast<uint8_t**>(skater + 0x17C + specialSlot * 4);
                    (*reinterpret_cast<Scripts::CStruct**>(special + 4))->FetchText(kName, &trickText, 1);
                    t.taps = 1;
                }
            }
            if (!trickText) {
                Scripts::CStruct* trick = Scripts::ReadSubStruct(trickName);
                if (!trick)
                    trick = reinterpret_cast<Scripts::CStruct*>(Scripts::FetchList(trickName)->FetchPart(0));
                Scripts::CStruct* trickParams;
                trick->ReadSubStruct(kParams, &trickParams, 1);
                while (taps > 1) {
                    --taps;
                    uint32_t extra;
                    if (!trickParams->ReadHashValue(kExtraTricks, &extra, 0)) {
                        taps = 1;
                        t.taps = 1;
                        break;
                    }
                    auto* extraTrick = reinterpret_cast<Scripts::CStruct*>(Scripts::FetchList(extra)->FetchPart(0));
                    extraTrick->ReadSubStruct(kParams, &trickParams, 1);
                }
                trickParams->FetchText(kName, &trickText, true);
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
            t.trick = Checksums::HashFromText(trickText); // (TrickLanded matches by the trick's name)
        }

        Scripts::CStruct* params = SpawnParamTable();
        void* container = NewContainer();
        SetId(container, GameObjects::kUnassignedId);
        AddToManager(manager, container);
        params->InsertPair(kDims, 100.0f, 20.0f);
        ChangeTraits(container, params);
        MenuSystem::CUiWidget* menu = ElementById(manager, kBeatMenu);
        menu->SetFocusSent(0);
        ChangeOwner(manager, menu, container);
        DeleteStructure(params);

        params = SpawnParamTable();
        params->AppendWord(kText, text);
        params->PutNameKey(kFont, kNewTrickFont);
        params->PutNameKey(0u, kNotFocusable);
        void* textElement = NewText();
        SetId(textElement, GameObjects::kUnassignedId);
        AddToManager(manager, textElement);
        ChangeTraits(textElement, params);
        ChangeOwner(manager, container, textElement);
        DeleteStructure(params);

        if (keyComboText || keyCombo) {
            Scripts::CStruct* texts = Scripts::ReadSubStruct(t.taps == 2 ? kTrickTextDoubleTap : kTrickText);
            if (!keyComboText)
                texts->FetchText(keyCombo, &keyComboText, 0);
            params = SpawnParamTable();
            params->AppendWord(kText, keyComboText);
            params->PutNameKey(kFont, kSmall);
            params->PutNameKey(0u, kNotFocusable);
            void* keyText = NewText();
            SetId(keyText, GameObjects::kUnassignedId);
            AddToManager(manager, keyText);
            ChangeTraits(keyText, params);
            ChangeOwner(manager, container, keyText);
            DeleteStructure(params);
        }
        t.element = *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(container) + 0x14);
        ++m_trickTotal;
        params = SpawnParamTable();
        if (!keyComboText)
            params->PutNameKey(0u, kNoKeyCombo);
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
            DropMoveEntry(i);
        }
    }
}

// 0x00576210
CTrickBeatGoal* CTrickBeatGoal::Teardown(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00576240
bool CTrickBeatGoal::TickFrame()
{
    int wait = 0;
    m_args->FetchIntValue(kTrickAddDelay, &wait, 0);
    if (wait)
        return BaseUpdate(this);
    if (IsOperative() && !IsFrozen() && m_completed >= m_totalToWin) {
        void* manager = ObjectiveBoard();
        CompleteObjective(manager, GoalId(this));
        return true;
    }
    if (IsOperative())
        IsFrozen(); // (the result isn't used)
    if (IsOperative() && ((m_flagBits & 0x10) || static_cast<double>(m_secondsRemaining) > 0.0) && !IsFrozen()) {
        UpdateBeat();
        int used = 0;
        for (const BeatTrick& t : m_tricks) {
            if (!t.free)
                ++used;
        }
        int blockSize;
        m_args->FetchIntValue(kBlockSize, &blockSize, 1);
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
CTrickObjectsGoal* CTrickObjectsGoal::MakeNew(Scripts::CStruct* params)
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
CTrickObjectsGoal* CTrickObjectsGoal::Teardown(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00576400
void CTrickObjectsGoal::EvaluatePoints()
{
    void* manager = ObjectiveBoard();
    if (!reinterpret_cast<CObjective*>(this)->HasEveryBit()) {
        FailObjective(manager, GoalId(this));
        return;
    }
    int required;
    if (!m_args->FetchIntValue(kRequiredScore, &required, 0)) {
        CompleteObjective(manager, GoalId(this));
        return;
    }
    uint8_t* skater = LocalSkater();
    if (!skater)
        return;
    uint8_t* score = ScoreOf(skater);
    // The points of the trick in progress (+0x14, times the multiplier +0x60) count too.
    int pending = Ftol(static_cast<double>(*reinterpret_cast<int*>(score + 0x14)) * *reinterpret_cast<float*>(score + 0x60));
    if (TotalScore(score) + pending < required)
        FailObjective(manager, GoalId(this));
    else
        CompleteObjective(manager, GoalId(this));
}

// 0x005764B0
Scripts::CStruct* CTrickObjectsGoal::GetObjectByCluster(uint32_t cluster)
{
    Scripts::CArray* objects;
    m_args->FetchList(kGoalObjects, &objects, 1);
    int count = static_cast<int>(objects->m_extent);
    uint32_t found = cluster; // (kept from the last entry with a cluster_id: an entry without one matches that)
    for (int i = 0; i < count; ++i) {
        auto* object = reinterpret_cast<Scripts::CStruct*>(objects->FetchPart(static_cast<uint32_t>(i)));
        object->ReadHashValue(kClusterId, &found, 1);
        if (found == cluster)
            return object;
    }
    return nullptr;
}

// 0x00576520
void CTrickObjectsGoal::ObjectHit(uint32_t cluster)
{
    Scripts::CStruct* object = GetObjectByCluster(cluster);
    if (!object || Virtual<bool>(this, 0x54) || reinterpret_cast<CObjective*>(this)->IsLatched())
        return;
    uint8_t* skater = LocalSkater();
    if (!skater)
        return;
    auto component = [skater](uint32_t type) {
        return reinterpret_cast<uint8_t*>(reinterpret_cast<GameObjects::CGameEntity*>(skater)->FetchModule(type));
    };
    uint8_t* state = component(kSkaterState);
    uint8_t* scoreComponent = component(kSkaterScore);
    uint8_t* score = *reinterpret_cast<uint8_t**>(scoreComponent + 0x18);
    // Only while the skater is in the air (+0xE0) or has points pending.
    if (!state[0xE0] &&
        Ftol(static_cast<double>(*reinterpret_cast<int*>(score + 0x14)) * *reinterpret_cast<float*>(score + 0x60)) <= 0)
        return;
    if (!IsOperative()) {
        void* manager = ObjectiveBoard();
        static_cast<CGoalManager*>(manager)->EngageObjective(GoalId(this), false);
    }
    uint32_t flag;
    object->ReadHashValue(kFlag, &flag, 1);
    if (!reinterpret_cast<CObjective*>(this)->BitIsOn(flag, 0))
        Virtual<void>(this, 0x4C, flag, 1, 0);
}

} // namespace Game
