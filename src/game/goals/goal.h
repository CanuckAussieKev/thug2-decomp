#pragma once

// Game::CGoal (0x70 bytes; 0x00553E00-0x00557ABF): the goal base class. Every goal of a level is one (or one of its
// types: competition, film, find gaps, horse, net, race, skate line, tetris, trick beat... - their own files): its
// parameters (the goal's script structure, also its state as flags and integers), its flags structure (the
// goal_flags that must be set to win), the goals linked to it (children it starts, the parent that started it), its
// timer, its records. The goal manager (goal_manager.h) owns them, in a lookup table by goal id, and drives them
// from its script commands and its update.
//
// Not C++ virtual: the vtable pointer is the game's (CGoal's own 0x0064DDD0, a type's another), called through the
// table (Virtual below) - the engine calls the same entries, and each is REPLACEd at its address. Every slot returns
// what the callers read (AL for the bool ones; the setters' EAX is left unread - checked at the callers).

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"
#include "hook/game_global.h"

namespace Script {
class CArray;
class CScript;
class CStruct;
} // namespace Script

namespace Game {

extern void** const kGoalNodeVtable; // 0x0064DDCC (defined in goal_life.cpp)

class CGoalPed;

// A link to another goal by id (0xC bytes): the children (+0x28) and the parents (+0x2C) are singly linked lists of
// these; a child's link is marked when that child is won (Win, 0x005576D0).
struct GoalLink {
    uint32_t m_goalId;  // +0x00 (0: the list's first link, not yet used)
    GoalLink* m_next;   // +0x04
    bool m_won;         // +0x08
    uint8_t m_pad09[3];
};
static_assert(sizeof(GoalLink) == 0xC, "layout must match the original");

class CGoal {
public:
    // The vtable slots (CGoal's own entries at 0x0064DDD0).
    enum Slot : uint32_t {
        kVtDestroy = 0x00,         // 0x005566A0 (flags; bit 0: free) - returns this
        kVtShouldExpire = 0x04,    // 0x00556230
        kVtIsExpired = 0x08,       // 0x00554CD0
        kVtIsActive = 0x0C,        // 0x00553F90
        kVtIsActive2 = 0x10,       // 0x004A10A0 (a jump to IsActive's slot: not in this range)
        kVtExpire = 0x14,          // 0x00554F40
        kVtLoadSaveData = 0x18,    // 0x00554610
        kVtGetSaveData = 0x1C,     // 0x00554770
        kVtHasTimeLimit = 0x20,    // 0x005545A0
        kVtTimerBeeps = 0x24,      // 0x005B9760 (Stubs::ReturnTrue)
        kVtSlot28 = 0x28,          // 0x005545F0
        kVtActivate = 0x2C,        // 0x00557360
        kVtSetActive = 0x30,       // 0x00553F50
        kVtDeactivate = 0x34,      // 0x00557560 (force, affectTree)
        kVtSetInactive = 0x38,     // 0x00553F60
        kVtWin = 0x3C,             // 0x005576D0
        kVtLose = 0x40,            // 0x00554C70
        kVtUpdate = 0x44,          // 0x00554DD0
        kVtIsPaused = 0x48,        // 0x00554560
        kVtSetFlag = 0x4C,         // 0x00556330 (flag, value, sub-structure)
        kVtStageFlagSet = 0x50,    // 0x00555690 (bit)
        kVtHasWon = 0x54,          // 0x00555590
        kVtNetStageFlagSet = 0x58, // 0x005556B0 (bit)
        kVtHasWonGoalOrChild = 0x5C, // 0x005542C0
        kVtIsInitialized = 0x60,   // 0x005519F0
        kVtCanRetry = 0x64,        // 0x00554460
        kVtMarkWon = 0x68,         // 0x005555E0
        kVtSetStageFlag = 0x6C,    // 0x00555650 (bit)
        kVtSetNetStageFlag = 0x70, // 0x00555670 (bit)
        kVtUnBeat = 0x74,          // 0x00555930
    };

    // +0x6C (m_flags)
    enum Flags : uint8_t {
        kFlagInitialized = 0x01,       // Init / Uninitialize
        kFlagSeen = 0x02,              // the player has started it (hasSeen)
        kFlagWon = 0x04,               // beaten (out of the goal attack / lobby net games)
        kFlagWonInNetGame = 0x08,      // beaten in a goal attack / net lobby game
        kFlagSelected = 0x10,          // chosen for a net game
        kFlagLocked = 0x20,            // locked until another goal unlocks it (unlocked_by_another, pro_goal)
        kFlagDeactivateOnExpire = 0x40,
        kFlagEndOfRunStarted = 0x80,   // the skater's end of run started by the goal (IsExpired)
    };
    // +0x6D (m_state)
    enum State : uint8_t {
        kStateActive = 0x01,
        kState02 = 0x02,           // (read by 0x005543E0, never set in this range)
        kStatePaused = 0x04,
        kStateNoTimeLimit = 0x10,
    };

    Mem::ListNode m_node;          // +0x00: the vtable, then a list node (data this, priority 0, linked to itself
                                   // while not in a list; Deactivate unlinks it)
    CGoalPed* m_ped;               // +0x14: the goal's pro (0x14 bytes)
    uint32_t m_inherited;          // +0x18: passed to the children (2: single_timer, 4: retry_stage)
    Script::CStruct* m_params;     // +0x1C
    Script::CStruct* m_goalFlags;  // +0x20: goal_flags, each an integer (1: set); skater_flags: a structure each
    int m_endRunType;              // +0x24: 0 the goal's own end of run (GoalRunDone...), 1 the normal one
    GoalLink* m_children;          // +0x28
    GoalLink* m_parents;           // +0x2C
    uint32_t m_skater;             // +0x30: skater (none 0x806FFF30)
    int m_level;                   // +0x34: the career's level when made
    int m_group;                   // +0x38: group_number (-1: none) - locked while the manager's group is
    uint32_t m_stageFlags;         // +0x3C: bits by number (SetStageFlag)
    uint32_t m_netStageFlags;      // +0x40
    int m_timesStarted;            // +0x44
    int m_bestTime;                // +0x48: ms (record_type time; 0x7FFFFFFF none)
    int m_bestScore;               // +0x4C (record_type score)
    int m_timeLeft;                // +0x50: ms
    uint32_t m_unknown54;
    int m_numFlags;                // +0x58
    int m_numFlagsSet;             // +0x5C (also the number collected without number_collected)
    int m_elapsed;                 // +0x60: ms since activated
    int m_unknown64;               // +0x64 (-1)
    int m_unknown68;               // +0x68 (-1)
    uint8_t m_flags;               // +0x6C (Flags)
    uint8_t m_state;               // +0x6D (State)
    uint8_t m_pad6E[2];

    void** Vtable() const { return m_node.vtable; }

    // 0x00553E00: this goal's inherited bits passed to every child, down the tree. 0x00553E50: the children's links'
    // won marks cleared, down the tree. (The depth argument is only passed on.)
    void PassInheritedFlags(int depth);
    void ClearChildWins();
    // 0x00553E90
    Script::CStruct* GetParams();
    // 0x00553EA0: the timer from time (120 if none) seconds, plus 20 ms - unless unlimited_Time is 1.
    void ResetTimer();
    // 0x00553F10 / 0x00553F20 / 0x00553F30: the time left set (seconds, no extra 20 ms) / read (ms) / added to.
    void SetTimeLeft(int seconds);
    int GetTimeLeft();
    void AddTime(int seconds);
    // 0x00553F50 / 0x00553F60 (vtable +0x30 / +0x38): the active bit.
    void SetActive();
    void SetInactive();
    // 0x00553F70: `params` added to the goal's parameters. False.
    bool EditParams(Script::CStruct* params);
    // 0x00553F90 (vtable +0x0C)
    bool IsActive();
    // 0x00553FA0
    bool HasSeen();
    // 0x00553FB0 / 0x00553FC0
    void Pause();
    void UnPause();
    // 0x00553FD0: goal flag `flag` (of the skater_flags structure `sub`, if any) is there and equals `value`.
    bool GoalFlagEquals(uint32_t flag, int value, uint32_t sub);
    // 0x00554030 (cdecl): every flag of `flags` but num_flags cleared.
    static void ClearFlagValues(Script::CStruct* flags);
    // 0x00554070: every goal flag cleared (each skater's structure's), none set.
    void ResetGoalFlags();
    // 0x00554110: enough flags set to win (num_flags_to_win, else all; every skater's structure's all).
    bool AllFlagsSet();
    // 0x005541E0: goal flag `flag` (of `sub`'s structure, if any) is non-zero (asserted).
    bool GoalFlagSet(uint32_t flag, uint32_t sub);
    // 0x00554220: locked (its own lock, or its group's - the goal manager's).
    bool IsLocked();
    // 0x00554250 / 0x00554260 / 0x00554270: the Graffiti, special and tetris flags.
    bool IsGraffiti();
    bool IsSpecial();
    bool IsTetris();
    // 0x00554280: goal_id (asserted).
    uint32_t GetGoalId();
    // 0x005542B0 / 0x00554300: the edited_goal flag (Create-a-Goal's).
    bool IsEditedGoal();
    bool IsEditedGoal2();
    // 0x005542C0 (vtable +0x5C): won, or one of its children's links marked.
    bool HasWonGoalOrChild();
    // 0x005542F0
    bool IsSelected();
    // 0x005519F0 (vtable +0x60): initialised (Init, until Uninitialize).
    bool IsInitialized();
    // 0x00554320: number_collected counted up (goal_update_counter run with use_hud_counter).
    void GotCounterObject();
    // 0x00554390: number_collected reached number.
    bool CounterGoalDone();
    // 0x005543E0
    bool State02();
    // 0x005543F0: proset_prefix is `prefix` (any case).
    bool HasProsetPrefix(const char* prefix);
    // 0x00554430 / 0x00554440 / 0x00554450
    void SetHasSeen();
    void Select();
    void Deselect();
    // 0x00554460 (vtable +0x64): can_retry_goal.
    bool CanRetry();
    // 0x00554470: current_time 0. 0x00554480: unless paused, current_time counted up by the frame (ms), and its
    // seconds and hundredths (current_time_seconds, current_time_fraction).
    void ResetCurrentTime();
    void UpdateCurrentTime();
    // 0x00554510 / 0x00554520: the quick_start flag.
    void SetQuickStart();
    void UnsetQuickStart();
    // 0x00554530 / 0x00554540 / 0x00554550: pro_goal, net (components), competition (flag).
    bool IsProGoal();
    bool IsNetGoal();
    bool IsCompetition();
    // 0x00554560 (vtable +0x48)
    bool IsPaused();
    // 0x00554570: view_goals_text.
    bool GetViewGoalsText(const char** text);
    // 0x00554590
    bool EndOfRunStarted();
    // 0x005545A0 (vtable +0x20): not unlimited_Time.
    bool HasTimeLimit();
    // 0x005545F0 (vtable +0x28): the 0xA302D7CD component.
    bool HasComponentA302D7CD();
    // 0x00554610 (vtable +0x18) / 0x00554770 (vtable +0x1C): the saved state (hasBeaten, isLocked, hasSeen, the
    // record, the goal flags) read / written.
    void LoadSaveData(Script::CStruct* data);
    void GetSaveData(Script::CStruct* data);
    // 0x00554890 (cdecl): `text` copied to `out` with \t<n> / \k<n> (one or two digits, from 1) replaced by trick
    // `n`'s / key combo `n`'s text. (`size` isn't used: no bounds checks, as the original.)
    static void ReplaceTrickTokens(const char* text, char* out, Script::CArray* keyCombos, Script::CArray* tricks,
                                   int size);
    // 0x005549A0: number_collected, else the flags set.
    int GetNumberCollected();
    // 0x005549D0: num_flags_to_win, else the number of flags (0: -1).
    int GetNumFlagsToWin();
    // 0x00554A00 / 0x00554A10 / 0x00554A20
    int GetTimesStarted();
    void SetEndRunType(int type);
    void SetDeactivateOnExpire(bool deactivate);
    // 0x00554A40 / 0x00554A50 / 0x00554A70: the Horse, findGaps and require_combo flags.
    bool IsHorse();
    bool IsFindGaps();
    bool RequiresCombo();
    // 0x00554A60: the no-time-limit state bit (unlimited_Time).
    bool HasNoTimeLimit();

    // 0x00554A80: the parameters' script named `name` (a checksum component) run with them. Whether there was one.
    bool RunCallbackScript(uint32_t name);
    // 0x00554AC0 / 0x00554B40: a parent / child linked (the list's first link reused while it has no goal; a child
    // only once). A failed allocation writes through null, as the original.
    void AddParent(uint32_t id);
    void AddChild(uint32_t id);
    // 0x00554BD0: uninitialized (deactivated first if active; the uninit script); with `children`, every child too.
    void Uninitialize(bool children, int depth);
    // 0x00554C70 (vtable +0x40): lost - the fail script, deactivated. False if it wasn't active.
    bool Lose();
    // 0x00554CD0 (vtable +0x08): no local skater or no players, or out of time (never without a time limit).
    bool IsExpired();
    // 0x00554D10: the same, but out of time only once the skater's run is done (the goal's or the normal one, by the
    // end of run type); out of time before that, the run is ended (`force` passed on) and false.
    bool IsExpiredAtEndOfRun(bool force);
    // 0x00556230 (vtable +0x04): IsExpiredAtEndOfRun(false).
    bool ShouldExpire();
    // 0x00554DD0 (vtable +0x44): while active and not paused: the active script, the timer (and its beeps under
    // ten seconds), require_combo's combo_started, expired. True.
    bool Update();
    // 0x00554F40 (vtable +0x14): the expire script; deactivated if it's to be on expiring.
    void Expire();
    // 0x00554F90: gap `gap` is one of required_gaps (and required_trick_name, if any, has been done: by its name or
    // as a grind's BS / FS): its flag got_<n> set (n from 1). Whether it was.
    bool GotGap(uint32_t gap);
    // 0x00555100: the first of the waypoints (0x160B3220, else the asserted 0x92C6E839) whose flag isn't set yet
    // started: paused, its time (added, with 0xFB11CB01, else the goal's), the goal's id and the first_spot flag
    // passed, spawned (the horse's or the race's script). True.
    bool NextWaypoint();
    // 0x005552D0: cluster `cluster` taken with a score above its kill_clusters entry's (else the goal's) score: the
    // cluster turned to the player's colour (0) and the entry's flag set.
    void KillCluster(uint32_t cluster, int score);
    // 0x005553C0: trick_string has been done (a grind's FS / BS name first).
    bool DidTrickString();
    // 0x00555540: the topmost parent's goal_id (this goal's if it has none).
    uint32_t GetRootGoalId();
    // 0x00555590 (vtable +0x54): beaten (in a goal attack or net lobby game: kFlagWonInNetGame).
    bool HasWon();
    // 0x005555E0 (vtable +0x68): beaten - and every parent.
    void MarkWon();
    // 0x00555650 / 0x00555670 (vtable +0x6C / +0x70): stage bit `bit` set. 0x00555690 / 0x005556B0 (vtable +0x50 /
    // +0x58): whether it is (0 / 1 in all of EAX).
    void SetStageFlag(int bit);
    void SetNetStageFlag(int bit);
    int StageFlagSet(int bit);
    int NetStageFlagSet(int bit);
    // 0x005556D0: start_height: the local skater's height now (feet).
    void SetStartHeight();
    // 0x00555710: unless paused: the skater's height above start_height (feet and inches) - a new record (above
    // 0x78FFB85C, at least 10000) kept with its feet and inches. Whether it was one.
    bool CheckHeightRecord();
    // 0x00555800: the same for the distance from the 0x8E18F3B3 object (current_distance_feet / _inches).
    bool CheckDistanceRecord();
    // 0x00555930 (vtable +0x74): not beaten any more (in a net game the stage bits cleared too). Whether it was.
    bool UnBeat();
    // 0x00555970: the end of run done (the goal's or the normal one): the skater told, the started flag cleared.
    void EndOfRunDone(Script::CScript* script);
    // 0x005559E0 / 0x00555A20: the end of run (the goal's or the normal one) done / started.
    bool RunDone();
    bool RunStarted();

    // --- goal_life.cpp ---
    // 0x00556FE0: the constructor: a copy of `params` (the goal's own), its pro, an empty flags structure, the
    // links' first entries, the career's level, group_number, the goal flags, single_timer / retry_stage (passed to
    // the children), skater, locked by unlocked_by_another / pro_goal. (m_inherited isn't cleared first, as the
    // original.) Returns this.
    CGoal* Construct(Script::CStruct* params);
    // 0x00556110: the destructor body: the destroy script, the pro, the structures, the links freed, the node
    // unlinked. 0x005566A0 (vtable +0x00): the scalar deleting destructor.
    void DestroyBody();
    CGoal* Destroy(uint8_t flags);
    // 0x005560D0: the destructor of the goal's list node (Lst::Node<CGoal>, vtable 0x0064DDCC): unlinked; freed with
    // flags bit 0.
    CGoal* DestroyNode(uint8_t flags);
    // 0x00557270: initialized: the difficulty level's parameters, the init script, the trick texts, unlimited_Time,
    // dont_deactivate_upon_expiration (in the bottom-up heap).
    void Init();
    // 0x00556F90: the difficulty level's parameters added (and the goal flags made again).
    void AppendDifficultyLevelParams();
    // 0x00557360 (vtable +0x2C): started (if not active): counted, reset_score, the timer (a child with
    // single_timer: its root's), the root uninitialized for it, initialized, the activate script, seen, active, the
    // goal manager's active list. False if it was active.
    bool Activate();
    // 0x00557560 (vtable +0x34): stopped (if active): special's key combos dropped, inactive, the goal flags reset
    // (unless kept), the deactivate script, testing_goal; not won: the skater readied, and with `resetTree` the root
    // made ready again; unlinked. (`unused` isn't read.) False if it wasn't active.
    bool Deactivate(bool unused, bool resetTree);
    // 0x005576D0 (vtable +0x3C): won. With a child: the child started instead. Otherwise the record (score - the
    // custom one, high combo or total - or time), the rewards (first win, not in a net game), the success script,
    // deactivated, marked won, the cash / chapter (career) or the counter (classic). True.
    bool Win();
    // 0x00557A40: unlocked (if locked): initialized unless won (classic / always_initialize_goal / edited_goal
    // anyway), the unlock script - not in a net game.
    void Unlock();
    // 0x00555DF0: the reward parameters into `rewards` (the goal points shown, a trick slot, goal_rewards' entry for
    // the goal, reward_stat).
    void GetRewardsStructure(Script::CStruct* rewards);
    // 0x00555FB0: Reward_Goal's goal(s) unlocked.
    void UnlockRewardGoals();
    // 0x00556050: reward_points (an array: by the difficulty level), else 0.
    int GetRewardPoints();
    // 0x00556330 (vtable +0x4C): goal flag `flag` (of skater structure `sub`, if any) set to `value` - while active,
    // or silent_activation and not won; not locked. A change counts the flags set (the flag sound, the counter); a
    // silently activated goal is won once all are set (TrickObjects aside), else Gaps shows the gap. Whether it
    // changed (or was already set).
    bool SetFlag(uint32_t flag, int value, uint32_t sub);
    // 0x005566D0: the goal flags from `params`' goal_flags (names: each 0; structures: skater_flags, one structure
    // per skater with its flags and counts); num_flags.
    void CreateGoalFlags(Script::CStruct* params);
    // 0x00556240: every player's run (by the end of run type) is done (in horse, those not playing aside).
    bool AllSkatersRunDone();

    // --- goal_tricks.cpp ---
    // 0x005565C0: goal_no_valid_key_combos run (with the goal's id).
    void NoValidKeyCombos();
    // 0x005568B0: a random key combo of `combos` that the skater has a trick on (100 tries, then each in turn - which
    // returns the last random one, as the original); -1 (NoValidKeyCombos) if none.
    int PickKeyCombo(Script::CArray* combos);
    // 0x00555B50 (cdecl): each key combo's text (goal_tetris_trick_text) and trick's name (the skater's trick
    // mapping: a special slot's trick, a premade create-a-trick, or the trick's own) into the two arrays. False if a
    // key combo has no trick.
    static bool BuildTrickTexts(Script::CArray* keyCombos, Script::CArray* keyTexts, Script::CArray* trickTexts,
                                int premadeCat);
    // 0x00556A00: the texts' \t / \k tokens replaced by the key combos' tricks (the originals kept as original_*).
    // False without key_combos or a trick for one.
    bool ReplaceTrickText();
    // 0x00555A60: a special trick slot given as a reward taken back (should_remove_trick), the key combos dropped.
    void RemoveRewardTrick();
    // 0x00555D40: the kill_clusters turned to `player`'s colour (or cleared with `clear`).
    void ColorTrickObjects(int player, bool clear);
    // 0x00557AC0: the goal's trick made a special (a free key combo of the trick type's - or the slot it already
    // has), key_combos set to it, the texts redone. Whether it was new.
    bool AwardSpecialTrick();
};
static_assert(offsetof(CGoal, m_ped) == 0x14 && offsetof(CGoal, m_params) == 0x1C &&
                  offsetof(CGoal, m_endRunType) == 0x24 && offsetof(CGoal, m_children) == 0x28 &&
                  offsetof(CGoal, m_skater) == 0x30 && offsetof(CGoal, m_group) == 0x38 &&
                  offsetof(CGoal, m_timesStarted) == 0x44 && offsetof(CGoal, m_timeLeft) == 0x50 &&
                  offsetof(CGoal, m_numFlags) == 0x58 && offsetof(CGoal, m_elapsed) == 0x60 &&
                  offsetof(CGoal, m_flags) == 0x6C && offsetof(CGoal, m_state) == 0x6D && sizeof(CGoal) == 0x70,
              "layout must match the original");

// A goal's virtual function at vtable offset `slot` (the game's table).
template <typename R, typename... A>
R Virtual(CGoal* goal, uint32_t slot, A... args)
{
    return reinterpret_cast<R(__thiscall*)(CGoal*, A...)>(goal->Vtable()[slot / 4])(goal, args...);
}

GAME_GLOBAL_EXTERN(float, g_goalLifeSecondsPerMillisecond);

} // namespace Game
