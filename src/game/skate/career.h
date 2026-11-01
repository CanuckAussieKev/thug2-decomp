#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/core/mem.h"

// The skater's career (0x00584110-0x00584CF3; the rest of its code is later in the range): for each of 21 levels its
// goals (256 bits) and flags (256 bits), the goals and flags as they were when the level started (to tell which were
// just got), cash and a second per-level amount with their totals, a per-level counter, global flags (256 bits: the
// cheats are among them), the current level, each level's gap checklist, whether each level was visited, and an
// info structure.
namespace goals {
class GapTally;
}
namespace script {
class Script;
class Struct;
} // namespace script

namespace objects {

class RiderStoryProgress : public memory::HeapObject {
public:
    static constexpr int kLevels = 21;

    // 0x00584110: level `level`'s gap checklist (-1: the current level's). 0x00584130: the current level.
    goals::GapTally* fetch_gap_list(int level);
    int fetch_stage();
    // 0x00584140 / 0x00584180 / 0x005841D0: goal `goal` of `level` (-1: the current one) set / cleared (also from
    // the level-start copy) / asked. 0x00584210: whether the current level's goal changed since the level started.
    void set_goal(int goal, int level);
    void un_set_goal(int goal, int level);
    bool get_goal(int goal, int level);
    bool just_got_goal(int goal);
    // 0x00584250: how many of the first 64 goals of `level` are set. 0x005842A0: those of every level not left out
    // (0x007CE4E8); 0x00584320: how many of those levels have any.
    int count_goals(int level);
    int count_total_goals();
    int count_levels_with_goals();
    // 0x005843A0 / 0x005843D0 / 0x00584400: a global flag set / cleared / asked.
    void set_global_flag(int flag);
    void un_set_global_flag(int flag);
    bool get_global_flag(int flag);
    // 0x00584430 / 0x00584470 / 0x005844C0 / 0x00584500: a level flag, as the goals.
    void set_flag(int flag, int level);
    void un_set_flag(int flag, int level);
    bool fetch_marker(int flag, int level);
    bool just_got_flag(int flag);
    // 0x00584540 / 0x00584560: an integer of its info structure (by name) read / set (true).
    int get_info_integer(uint32_t name);
    bool set_info_integer(uint32_t name, int value);
    // 0x00584580: the info's 0x471FC4DD integer increased by `amount`.
    void add_to_info_471f_c_4d_d(int amount);
    // 0x005845C0: cash added to the current level's and the total; the level's amount returned (as the original: a
    // pointer to it). 0x005845F0: the current level's second amount set (the total changed by the difference).
    uint16_t* deposit_money(int amount);
    void set_level_amount(int amount);
    // 0x00584620: the current level's counter and the total counter increased; the level's returned. 0x00584640:
    // a level's counter. 0x00584650: the flag at +0x5EF set.
    uint8_t* increment_counter();
    uint8_t get_counter(int level);
    void set_flag_5e_f(uint8_t value);
    // 0x00584660: the value at +0x634 set (+0x638 keeps the highest).
    void set_value634(int value);
    // 0x00584680: reset: every gap checklist (if asked); the current level 0; then the part asked (0: both):
    // 0x4DA4937B - flags and goals 0-199 of every level, the left-out levels, cash, the second amounts (1000 each),
    // the level-start copies, the totals (the second 6000) -; 0x4C10DE52 - flags and goals 200-255 of the first 18
    // levels, their counters, the counter total, +0x5EF 0x82, the info's defaults (0x0486FD79). True.
    bool get_reset(bool reset_gaps, uint32_t part);
    // 0x00584BA0: Reset without the gaps. True.
    bool reset_part(uint32_t part);
    // 0x00584BC0: whether every level listed in 0xEB1A4BC9 was visited and has all its gaps.
    bool all_gaps_got();
    // 0x00584C60: level `level` started (-1: the current one): visited; its goals and flags copied for JustGot*.
    void begin_level(int level);
    // 0x00584CC0: the global flag a script integer names (a cheat); also left at 0x007CE4FD.
    bool is_code_on(uint32_t name);
    // 0x00585270: a gap checklist per level (none visited), an info structure, Reset(true, 0).
    RiderStoryProgress();
    // 0x00585E40 (0x00585EA0 the deleting one): the checklists and the info freed.
    ~RiderStoryProgress();
    RiderStoryProgress(const RiderStoryProgress&) = delete;
    RiderStoryProgress& operator=(const RiderStoryProgress&) = delete;
    // 0x00584D00: read from a save's career structure (0x4DA4937B): goals, flags, global flags, each level's gaps
    // (got ones with their positions), visited levels, cash / amounts and totals, counters (an older save without the
    // counter total: it's summed), +0x634 / +0x638, the info. 0x00585550: written there the same way (with the skate
    // module's +0x378 and the game mode).
    void decode_from_struct(script::Struct* save);
    void write_to_structure(script::Struct* save);

    // The goal / flag words of every level, in one run (level * 8 + word).
    uint32_t* goal_words() { return &goals_[0][0]; }
    uint32_t* flag_words() { return &flag_bits_[0][0]; }

    // (The offsets are the original's, which has a vtable at +0x000 - 0x0064EFD4 - holding only the destructor.)
    uint32_t goals_[kLevels][8];     // +0x004
    uint32_t flag_bits_[kLevels][8]; // +0x2A4
    uint32_t start_goals_[8];        // +0x544
    uint32_t start_flags_[8];        // +0x564
    uint16_t funds_[kLevels];        // +0x584
    uint16_t amount_[kLevels];       // +0x5AE
    uint16_t lifetime_funds_;        // +0x5D8
    uint16_t total_amount_;          // +0x5DA
    uint8_t counter_[18];            // +0x5DC
    uint8_t total_counter_;          // +0x5EE
    uint8_t flag_5e_f_;              // +0x5EF
    uint32_t global_flags_[16];      // +0x5F0
    int level_;                      // +0x630
    int value634_;                   // +0x634
    int max_value634_;               // +0x638
    goals::GapTally* gaps_[kLevels]; // +0x63C
    bool visited_[kLevels];          // +0x690
    script::Struct* details_;        // +0x6A8
};

// 0x00585E20 (GetCareerSaveInfo): the skate module's career (+0x20) written to the script's results.
bool cfunc_get_career_save_info(script::Struct* params, script::Script* script);

} // namespace objects
