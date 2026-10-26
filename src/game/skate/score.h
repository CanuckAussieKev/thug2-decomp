#pragma once

// Mdl::Score (0x1550 bytes, 0x00561240-0x00564BFF): a skater's score - its total, the combo being built (the trick
// log: each trick's points, its repeats, its spin, its name and the text shown), the multiplier, the special meter,
// the balance / manual meter's on-screen positions and the "robot" detection (the same lines skated over and over
// earn less). The SkaterScore component holds one (+0x18); CSkater::GetScore returns it.
//
// A trick's points: its base times a reduction for how often it was done already (100, 75, 50, 25 then 10 percent:
// 0x0064E280), times half its spin multiplier (2 for no spin, 3 for a 180, ... 7 past 900: 0x0064E268; a no-spin
// trick - flag 0x02 - resets it; the tricks after one share the spin of the first), 1.2 times when the trick was done
// as a switch / special one (its kind, from flags 0x04 / 0x08). The combo's points are the sum times the multiplier
// (one more for each trick, a little less for tricks done too often: trick_mult_degrade_start).

#include <cstddef>
#include <cstdint>

#include "game/lst/dynamic_table.h"
#include "game/lst/lookup_table.h"
#include "game/script/struct.h"

namespace Script {
class CStruct;
}

namespace Net {
struct MsgHandlerContext;
}

namespace Mdl {

// A trick in the log (0x9C bytes).
struct STrickLog {
    int m_score;          // +0x00 its base points
    int m_repeats;        // +0x04 how often done before (the reduction's index)
    int m_spin;           // +0x08 in half turns
    int m_spinDegrees;    // +0x0C as spun (signed)
    uint32_t m_name;      // +0x10 its name's checksum
    int m_kind;           // +0x14 (the flags' 0x04 / 0x08 bits shifted; not 0: 1.2 times)
    uint32_t m_flags;     // +0x18 (kTrick* below)
    char m_text[0x40];    // +0x1C its name, spaces escaped as "\_"
    char m_shown[0x40];   // +0x5C the text shown in the trick string
};
static_assert(sizeof(STrickLog) == 0x9C, "layout must match the original");

// How often a trick was seen (a node of the score's table, by name; 0x24 bytes): by kind, in combos landed before
// and in this one. (The kind can reach 6 - flags 0x04 and 0x08 both - and the counts are then read and written past
// the arrays, as the original.)
struct STrickSeen {
    uint32_t m_unused;    // +0x00 (never set)
    int m_landed[4];      // +0x04
    int m_current[4];     // +0x14
};
static_assert(sizeof(STrickSeen) == 0x24, "layout must match the original");

// STrickLog::m_flags.
constexpr uint32_t kTrickNoSpin = 0x02;       // the spin multiplier back to none (and the next trick's spin is set)
constexpr uint32_t kTrickKindBits = 0x0C;     // its kind (0x0400: only the 0x08 bit counts)
constexpr uint32_t kTrickGap = 0x10;          // a gap
constexpr uint32_t kTrickSpecial = 0x20;      // shown in colour 2
constexpr uint32_t kTrickNotCounted = 0x50;   // (0x10 | 0x40) not counted as seen
constexpr uint32_t kTrickColour3 = 0x80;      // shown in colour 3
constexpr uint32_t kTrickSpinPlusOne = 0x100; // the spin multiplier one more than the spin's
constexpr uint32_t kTrickNoMultiplier = 0x200; // the multiplier not raised
constexpr uint32_t kTrickKindHigh = 0x400;    // see kTrickKindBits

class Score {
public:
    float m_focusTimeLimit;      // +0x00 focus_time_limit
    int m_focusMaxTimes;         // +0x04 focus_max_times_in_combo
    int16_t m_skaterId;          // +0x08 the skater's object id
    uint8_t pad0A[2];
    int m_total;                 // +0x0C the total score
    int m_potState;              // +0x10 0 none, 1 landed (the countdown to its counting), 2 counting into the total
    int m_pot;                   // +0x14 the combo's base points
    int m_potShown;              // +0x18 the pot being counted down
    int m_potCountdown;          // +0x1C frames before the counting starts
    int m_potScore;              // +0x20 the combo's points (the pot times the multiplier; captureScore)
    int m_potSpecial;            // +0x24 the points already put into the special meter
    int m_bonus;                 // +0x28
    int m_bestTricks;            // +0x2C the most tricks in a combo
    int m_bestCombo;             // +0x30 the best combo's points
    int m_bestComboSent;         // +0x34 the best combo the server was told about
    Lst::LookupTable m_seen;     // +0x38 STrickSeen by trick name
    Lst::DynamicTable m_log;     // +0x4C STrickLog*
    int m_tricks;                // +0x5C the tricks in the combo (the log's used entries)
    float m_multiplier;          // +0x60
    int m_comboTricks;           // +0x64 the tricks in this combo (for m_bestTricks)
    int m_lastNoSpin;            // +0x68 the last no-spin trick's index (-1)
    int m_spinTrick;             // +0x6C the trick the spin goes on (-1)
    int m_special;               // +0x70 the special meter (0-3000)
    bool m_specialActive;        // +0x74
    uint8_t m_barColors[3][4];   // +0x75 special_bar_colors (empty, full, flashing)
    uint8_t pad81[3];
    float m_specialPhase;        // +0x84 the flashing's phase
    float m_specialRate;         // +0x88 special_bar_interpolator_rate
    bool m_debug;                // +0x8C (0x1EDC33F1: the points printed)
    uint8_t pad8D[3];
    int m_arrowCount;            // +0x90 balance_meter_info's arrow_positions
    Script::CPair m_arrows[20];  // +0x94 (one more: the last repeated)
    float m_arrowStep;           // +0x184 1 / the arrow count
    Script::CPair m_meterOffset; // +0x188 the arrow's offset from the meter
    float m_meterRotation;       // +0x194
    Script::CPair m_bars[2];     // +0x198 bar_positions (balance, manual)
    uint8_t m_lineCounts[2500];  // +0x1B0 robot detection: how often each line was skated since the robot reset
    uint8_t m_comboLineCounts[2500]; // +0xB74 ... in this combo
    int m_robotLines;            // +0x1538
    int m_robotComboLines;       // +0x153C
    int m_robotNewLines;         // +0x1540
    int m_robotComboNewLines;    // +0x1544
    int m_robotComboFirstLines;  // +0x1548
    float m_robotFactor;         // +0x154C (1: none)

    // --- queries (score.cpp) ---
    // 0x00561240 (cdecl): the step the counting pot goes down by: 1, 10, 100, 10000, 100000 or 1000000 (none of
    // 1000).
    static int ScoreIncrement(int value);
    // 0x00561290
    void SetSkaterId(int16_t id);
    // 0x005612A0: `text` into `out` with each space as "\_" (`this` unused).
    void EscapeSpaces(char* out, const char* text);
    // 0x005612D0: special_bar_colors and special_bar_interpolator_rate.
    void SetSpecialBarColors();
    // 0x00561360 (cdecl): the trick `trick`'s name checksum after `taps` - 1 of its ExtraTricks.
    static uint32_t TrickNameAfterTaps(uint32_t trick, int taps);
    // 0x00561400 / 0x00561410 / 0x00561420
    void ResetBests();
    void AddBonus(int points);
    int GetBonus();
    // 0x00561550: the points of the log's tricks [first, last) (as captureScore, without kTrickSpinPlusOne).
    int GetScoreOfRange(int first, int last);
    // 0x00561640 / 0x00561660: the last trick's / trick `index`'s name (0: none).
    uint32_t GetLastTrickName();
    uint32_t GetTrickName(int index);
    // 0x00561670: the trick string (the_trick_text + the skater's number) set to the log's texts, or emptied; then
    // UpdateTrickText.
    void DispatchTrickSequence(bool clear);
    // 0x00561730: balance_meter_info's arrows and bar positions (the multiplayer ones in a split screen game other than
    // HORSE).
    void SetupBalanceMeter();
    // 0x00561890: the score pot text (the_score_pot_text + the skater's number): blank, the points, or the points
    // "X" the multiplier; then UpdateScorepot.
    void DispatchScorePot(int points, float multiplier);
    // 0x005619E0: in a career game, the skate module's score goals (+0x60, 64 of {score, script, goal}) the total
    // went past from `before` to `after` - each done once, its script run on the skater.
    void CheckHighScore(int before, int after);
    // 0x00561A80: the total; shown (as minutes:seconds in king of the hill) for a local skater.
    void SetTotalScore(int total);
    // 0x00561B40: the total - on the server in a game scored by clusters, the clusters the skater holds.
    int GetTotalScore();
    // 0x00562080 (cdecl): the trick mapped to key combo `keyCombo` (the profile's trick_mapping, else a special in
    // that slot) after `taps`; 0 if none.
    static uint32_t TrickFromKeyCombo(uint32_t keyCombo, int taps);
    // 0x00562150 (cdecl): the created trick mapped to `keyCombo` (its index in the skater's), else a created trick
    // special's name in that slot (as the index, as the original), else -1.
    static int CreatedTrickFromKeyCombo(uint32_t keyCombo);
    // 0x00562250: whether trick `index` is `name` (with `spin`, perfect if `perfect`: landed within
    // perfect_landing_slop degrees of it).
    bool IsTrickAt(int index, uint32_t name, int spin, bool perfect);
    // 0x005622C0: how often the combo has `name` (with the spin it shares, `spin`; perfect).
    int CountTrick(uint32_t name, int spin, bool perfect);
    // 0x00562370: how many of the combo's tricks have `text` in their name (ignoring case).
    int CountTricksNamed(const char* text);
    // 0x005623B0 / 0x00562410: whether the last named trick is `name` after `taps` / the trick mapped to `keyCombo`.
    bool LastTrickIs(uint32_t name, int spin, bool perfect, int taps);
    bool LastTrickIsKeyCombo(uint32_t keyCombo, int spin, bool perfect, int taps);
    // 0x005624C0 / 0x00562510: CountTrick of `name` after `taps` / of the trick mapped to `keyCombo`.
    int CountTrickAfterTaps(uint32_t name, int spin, bool perfect, int taps);
    int CountKeyCombo(uint32_t keyCombo, int spin, bool perfect, int taps);
    // 0x005625A0 / 0x00562620: how often the combo has the trick done again apart (`name` after `taps` / mapped to
    // `keyCombo`; with `spin`): its times, one less when the last one is still the latest (gaps between don't count).
    int CountTrickRepeats(uint32_t name, int spin, int taps);
    int CountKeyComboRepeats(uint32_t keyCombo, int spin, int taps);
    // 0x005626E0: SetupBalanceMeter (a jump to it).
    void RefreshBalanceMeter();
    // 0x00562740: whether trick `index` is a gap.
    bool IsGap(int index);
    // 0x00562760: the special on / off (SkaterEnterSpecial / SkaterExitSpecial to the skater on a change).
    void SetSpecialActive(bool active);
    // 0x005627B0: line `line` skated: its counts up (to 255), and the factor the points are cut by once it was
    // skated more than robot_kick_in_count times in the combo (1 for lines past the 2500).
    void UpdateRobotDetection(int line);
    // 0x005629D0: the log's oldest landed tricks (those up to the first unnamed one, at most to the spin trick and
    // the last no-spin one) merged into its first entry (their points, no spin).
    void TrimLog();
    // 0x00562B20 / 0x00562C10 / 0x00562D00 / 0x005638A0: trick_text_countdown / _landed / _bail / _pulse with the
    // skater number's container, trick text and score pot ids.
    void TrickTextCountdown(int skater);
    void TrickTextLanded(int skater);
    void TrickTextBail(int skater);
    void TrickTextPulse(int skater);
    // 0x00562DF0: the special on, its meter full.
    void FillSpecial();
    // 0x005633B0: the combo's tricks that aren't gaps.
    int CountNonGapTricks();

    // --- the combo (score_combo.cpp) ---
    // 0x00562E40 (captureScore): the combo's points (each trick's, the debug print), the special meter filled by the
    // new points (NewSpecial), m_potScore / m_potSpecial.
    void CaptureScore();
    // 0x00563020: the pot and the log emptied; no multiplier, no spin trick, no robot counts for the combo.
    void ResetScorePot();
    // 0x00563080: the balance meter (on / off - show_balance_meter / hide_balance_meter - its mode, the arrow's place
    // and angle for `lean`).
    void PositionBalanceMeter(bool on, float lean, bool manual);
    // 0x00563370 / 0x00563390
    void SetBalanceMeter(bool on, float lean);
    void SetManualMeter(bool on, float lean);
    // 0x00563440: the constructor. Returns this.
    Score* Construct();
    // 0x00564B80: the destructor body.
    void DestroyBody();
    // 0x00563580: one frame: the special meter running down, the pot's countdown and counting, the special bar's
    // size and colour.
    void Update();
    // 0x00563990: a trick done (`text`, `score` base points, `flags`): logged (the log trimmed when full - at 250 the
    // last one's points just grow), how often it was seen, the multiplier, the trick string and pot shown.
    void Trigger(const char* text, int score, uint32_t flags);
    // 0x00563F30: the spin trick's spin (the "FS" / "BS" and Cab names; ollies' sides swapped on odd half turns).
    void SetSpin(int degrees);
    // 0x00564270: points added to the last trick while it is being held (no more than 10 tricks since the last
    // no-spin one; a no-spin trick only up to 20000).
    void TweakTrick(int points);
    // 0x00564330: the combo landed: its points to the total (as the game mode counts them), the best combo, the
    // seen counts, the server told.
    void Land();
    // 0x00564860: the combo bailed (`clear`: the trick string emptied at once).
    void Bail(bool clear);
    // 0x00564A10: everything back to the start of a run.
    void Reset();
    // 0x00564BE0: SetSpin while the spin trick isn't a no-spin one.
    void UpdateSpin(int degrees);

    // --- the network (score_net.cpp) ---
    // 0x00561BA0: the server asked to log the trick objects the skater just scored on (message 0x3B).
    void LogTrickObjectRequest(int score);
    // 0x00561CB0: `count` trick objects scored on by skater `skaterId` (`score` points; on the server, `send`):
    // logged, the players whose clusters were taken told, the objects sent to every player and observer; each
    // object's cluster coloured for the skater (or the skater's team).
    void LogTrickObject(int skaterId, int score, uint32_t count, uint32_t* objects, bool send);
    // 0x005626F0 (cdecl, the net handler): a trick objects message for this game logged by the skater's score.
    static int __cdecl HandleScoreMessage(Net::MsgHandlerContext* context);

    // (the trick log's entry `index`; the last one)
    STrickLog* Trick(int index) { return static_cast<STrickLog*>(m_log.m_items[index]); }
    STrickLog* LastTrick() { return Trick(m_log.m_count - 1); }
};
static_assert(offsetof(Score, m_seen) == 0x38 && offsetof(Score, m_log) == 0x4C && offsetof(Score, m_special) == 0x70 &&
                  offsetof(Score, m_barColors) == 0x75 && offsetof(Score, m_specialPhase) == 0x84 &&
                  offsetof(Score, m_arrows) == 0x94 && offsetof(Score, m_arrowStep) == 0x184 &&
                  offsetof(Score, m_bars) == 0x198 && offsetof(Score, m_lineCounts) == 0x1B0 &&
                  offsetof(Score, m_comboLineCounts) == 0xB74 && offsetof(Score, m_robotLines) == 0x1538 &&
                  sizeof(Score) == 0x1550,
              "layout must match the original");

// 0x00561240's entry: Score::ScoreIncrement with EDX kept (Update relies on it).
int ScoreIncrementStub();

} // namespace Mdl
