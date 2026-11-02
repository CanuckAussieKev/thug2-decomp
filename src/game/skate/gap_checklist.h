#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

#include "engine/core/arena_alloc.h"
#include "engine/core/text_store.h"
#include "engine/maths/vec4.h"

// The gap checklist (0x00578680-0x00579820): each level's list of the gaps it has (every EndGap the level's node
// scripts can reach), sorted by score then name, with how often each was landed. The career keeps one per level
// (0x00584110); the skate module's (0x005656C0) is the current level's. The script commands AddGapsToMenu,
// CreateGapList, GetLevelGapTotals and GiveAllGaps read it.
namespace script {
class Script;
class Struct;
} // namespace script

namespace goals {

// One gap (0x50 bytes; its own node in the checklist, which keeps the original's size).
struct GapEntry {
    // The fields other code reads and sets.
    memory::ListNode& list_link() { return list_link_; }
    [[nodiscard]] const memory::ListNode& list_link() const { return list_link_; }
    [[nodiscard]] const maths::Vec4& skater_start_pos() const { return skater_start_pos_; }
    void set_skater_start_pos(const maths::Vec4& value) { skater_start_pos_ = value; }
    [[nodiscard]] const maths::Vec4& skater_start_dir() const { return skater_start_dir_; }
    void set_skater_start_dir(const maths::Vec4& value) { skater_start_dir_ = value; }
    int& pending() { return pending_; }
    [[nodiscard]] int pending() const { return pending_; }
    int& num() { return num_; }
    [[nodiscard]] int num() const { return num_; }
    [[nodiscard]] int points_held() const { return points_held_; }
    void set_points_held(int value) { points_held_ = value; }
    void set_in_level(bool value) { in_level_ = value; }
    text::Chars& label() { return label_; }
    [[nodiscard]] const text::Chars& label() const { return label_; }

private:
    memory::ListNode list_link_{};   // data = the gap itself
    maths::Vec4 skater_start_pos_{}; // +0x14 where the gap menu puts the skater (set_gap_positions)
    maths::Vec4 skater_start_dir_{}; // +0x24
    int pending_{0};                 // +0x34 landed since the last award (register_gap_hit)
    int num_{0};                     // +0x38 times landed
    int points_held_{};            // +0x3C
    bool in_level_{true};          // +0x40 found by the last scan of the level's scripts
    std::array<uint8_t, 3> pad41{};
    text::Chars label_; // +0x44

public:

    // 0x005786E0: an unlinked node, an empty name, never landed, in the level.
    GapEntry();
    // 0x00579510: the name freed, the node unlinked (0x00578680). 0x005794E0: the scalar deleting
    // destructor.
    ~GapEntry();
    GapEntry(const GapEntry&) = delete;
    GapEntry& operator=(const GapEntry&) = delete;
    GapEntry* teardown(unsigned int flags);
};

// (lists::Elem<GapEntry>'s destructors - 0x00578680 unlinked, 0x005786A0 / 0x005794B0 scalar deleting - are
// memory::ListNode's.)

class GapTally {
public:
    // 0x00579690: no gaps, not built.
    GapTally();
    // 0x00585520: the destructor body (its list head unlinked, as a lists::Elem<GapEntry>; the gaps aren't freed).
    ~GapTally();
    GapTally(const GapTally&) = delete;
    GapTally& operator=(const GapTally&) = delete;

    // 0x00578840: every gap deleted, not built.
    void get_clear();
    // 0x00578AC0: every EndGap the nodes' TriggerScripts reach added (add_gap_from_node, through the instance
    // 0x007CE4B0), in the script heap; PrintGaps prints a header.
    void get_build();
    // 0x00578A00: an EndGap call's parameters: with a score= and a non-empty text=, the gap is added (or found
    // again); otherwise, with PrintGaps, a note that it isn't in the checklist. (`call` isn't used.)
    void add_gap_from_node(script::Struct* params, const uint8_t* call);
    // 0x00578880: the gap `name` of `score`: one already there (same name - case counts - and score) is marked
    // in the level; else a new one (`count` times landed, in the script heap) goes after the last gap of a lower
    // score, or of the same score and a name sorting first (ignoring case) - or first.
    void register_gap_entry(const char* name, int count, int score);

    // 0x00578B30: the index of the gap `name` (-1: none). 0x005793D0: the gap at `index` (null: none).
    int find_gap(const char* name);
    GapEntry* get_gap(int index);
    // 0x00579420 / 0x00579440 / 0x00579450: the gap at `index`'s name, times landed, score (no check: the index
    // must be valid). 0x00579460: its skater start position and direction.
    const char* get_gap_title(int index);
    int count_gaps_found(int index);
    int get_gap_points(int index);
    void get_gap_positions(int index, maths::Vec4* pos, maths::Vec4* dir);
    // 0x00578BC0: the gap `name`'s skater start position and direction (no such gap: nothing).
    void set_gap_positions(const char* name, maths::Vec4 pos, maths::Vec4 dir);
    // 0x00578C70: the gap `name` landed (pending until pay_out_queued_gaps).
    void register_gap_hit(const char* name);
    // 0x00578CF0: the pending landings dropped.
    void clear_pending();
    // 0x005796C0: the pending landings counted: the goal manager told of each gap landed (0x005586E0, while
    // 0x0055DEA0 says so), and for gaps landed for the first time got_gap_for_first_time runs (gap_text, and from
    // the second such gap on multiple_new_gaps = how many so far).
    void pay_out_queued_gaps();
    // 0x00578D90: every gap never landed and in the level.
    void get_reset();
    // 0x00578D20: how many gaps. 0x00578D50: whether every gap was landed (none: true).
    int total_of();
    bool all_got();

    // The script commands' work (`script`'s parameters get the results; true unless there's nothing):
    // 0x00578DD0 (GetLevelGapTotals): num_gaps, num_collected_gaps. True.
    bool get_gap_totals(script::Struct* params, script::Script* script);
    // 0x00578E40 (AddGapsToMenu): gap_menu_add_item for each gap (first_item / last_item flags, gap_score,
    // gap_name, times, gap_number, skater_start_pos, skater_start_dir, choose_script= - NullScript by default).
    // False with no gaps.
    bool add_gaps_to_menu(script::Struct* params, script::Script* script);
    // 0x00579080 (CreateGapList): array_size; GapList (gap_score, gap_name, times, gap_number, skater_start_pos,
    // skater_start_dir for each) and num_gaps_got. False with no gaps (num_gaps_got 0).
    bool create_gap_list(script::Struct* params, script::Script* script);

    bool built_{false}; // +0x00 (also set by register_gap_entry)
    std::array<uint8_t, 3> pad01{};
    memory::ListNode gaps_{}; // +0x04 the list's head (lists::Foremost<GapEntry>)
};

// 0x00578AA0 (Build's callback): add_gap_from_node on the checklist being built.
void add_gap_from_node_callback(script::Struct* params, const uint8_t* call);

// The script commands (cdecl, the Function table's):
// 0x005792E0 AddGapsToMenu / 0x00579300 CreateGapList: the skate module's checklist's.
bool cfunc_add_gaps_to_menu(script::Struct* params, script::Script* script);
bool cfunc_create_gap_list(script::Struct* params, script::Script* script);
// 0x00579320 GetLevelGapTotals: level= (required) - the career's checklist of that level's.
bool cfunc_get_level_gap_totals(script::Struct* params, script::Script* script);
// 0x00579360 GiveAllGaps: every gap of level= (default: the current level - 0x00584130) landed once more.
bool cfunc_give_all_gaps(script::Struct* params, script::Script* script);

} // namespace goals
