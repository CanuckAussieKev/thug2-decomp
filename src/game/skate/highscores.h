#pragma once

#include <cstddef>
#include <span>
#include <cstdint>
#include <memory>

#include <gsl/pointers>
#include "engine/core/mem.h"

// The records (0x0057ECD0-0x0057FE27): each level's high scores, best combos and (some levels) arcade scores - tables
// of records kept sorted, best first - and its longest combo, grind, lip trick and manual; the game's records are
// every level's plus the default initials. They go into and come out of the save as structures (records' value,
// number and initials). One BestResults, made by Create (0x0057EED0) and released by Release (0x0057FDB0), counted,
// at 0x007CE4BC / 0x007CE4C0.
namespace script {
class Struct;
class Script;
} // namespace script

namespace highscores {
constexpr int kDefaultScoreValue = 1000; // a new entry's score before it is set

// A record (0x10 bytes): its initials (a 4-byte buffer of its own), value, number and whether it was just set.
class BestEntry {
public:
    // 0x0057EF20 (the constructor): initials XXX, value 1000, number 0 (new left as it was).
    BestEntry();
    // A record tried: its value and its number.
    struct RecordTry {
        int value{};
        int number{};
    };

    // 0x0057ECD0: set to `value` and `number` and marked new if `value` beats it; else not new. Whether it did.
    bool try_beat_record(RecordTry record_try);
    // 0x0057ED00: value, number (0 if missing) and initials (XXX if missing) from `params`.
    void decode_from_struct(script::Struct* params);
    // The initials set (three characters).
    void set_player_tag(const char* initials);

    // The fields other code reads and sets.
    [[nodiscard]] char* tag() const { return tag_; }
    [[nodiscard]] int val() const { return val_; }
    void set_val(int value) { val_ = value; }
    [[nodiscard]] int slot_no() const { return slot_no_; }
    void set_slot_no(int value) { slot_no_ = value; }
    [[nodiscard]] bool is_fresh() const { return is_fresh_; }
    void set_is_fresh(bool value) { is_fresh_ = value; }

private:
    char* tag_;     // +0x00
    int val_{kDefaultScoreValue}; // +0x04
    int slot_no_{0};  // +0x08
    bool is_fresh_{}; // +0x0C
};

// A table of records (8 bytes): their count and the records (an array allocated with its count before it).
class BestEntryList {
public:
    // 0x0057EF60 (the constructor): `count` records, constructed.
    explicit BestEntryList(int count);
    // 0x0057EFE0: none new; `value` put where it beats a record (those after it moved down one, the last dropped),
    // with `number`, initials XXX, marked new. Its index, or -1 if it beats none.
    int try_insert_entry(BestEntry::RecordTry record_try);
    // 0x0057ED80: the new records' initials set.
    void update_initials(const char* initials);
    // 0x0057F0C0: the records from `params`' RecordTable array (only if it has as many).
    void decode_from_struct(script::Struct* params);
    // 0x0057F4E0: the records written to `params` as its recordtable array of structures (value, number, initials).
    void write_to_structure(script::Struct* params);
    // The records freed and the table (the destructor, inlined by its owners).
    void erase_one();

    // NOT ORIGINAL - the records as a span of their count.
    [[nodiscard]] std::span<BestEntry> records() const { return {records_, static_cast<size_t>(num_)}; }

private:
    int num_;            // +0x00
    BestEntry* records_; // +0x04
};

// A level's records (0x1C bytes).
class StageBests {
public:
    // 0x0057F120 (the constructor): high scores and best combos (5 each), arcade scores (10), and the four longest
    // records (made in the order longest combo, grind, lip trick, manual; their values 0).
    StageBests();
    // 0x0057F670: the destructor body (the records, then the tables: best combos, high scores, arcade scores).
    void destroy_body();
    // 0x0057EDD0: the new records' initials set (the longest ones: grind... as the original's order; then the
    // tables).
    void update_initials(const char* initials);
    // 0x0057F340: the records from `params` (highscores, bestcombos, arcadescores if there, longestcombo /
    // longestgrind / longestliptrick / longestmanual).
    void decode_from_struct(script::Struct* params);
    // 0x0057F7B0: the records written to `params` as structures of those names.
    void write_to_structure(script::Struct* params);

    // The fields other code reads and sets.
    [[nodiscard]] BestEntryList* high_scores() const { return high_scores_; }
    [[nodiscard]] BestEntryList* best_combos() const { return best_combos_; }
    [[nodiscard]] BestEntryList* arcade_scores() const { return arcade_scores_; }
    [[nodiscard]] BestEntry* grind_record() const { return grind_record_; }
    [[nodiscard]] BestEntry* longest_manual() const { return longest_manual_; }
    [[nodiscard]] BestEntry* longest_lip_trick() const { return longest_lip_trick_; }
    [[nodiscard]] BestEntry* biggest_chain() const { return biggest_chain_; }

private:
    // The constructor's step after the tables: the four longest records, made in the original's order (not the
    // order they are declared in).
    void make_longest_records();

    BestEntryList* high_scores_;   // +0x00
    BestEntryList* best_combos_;   // +0x04
    BestEntryList* arcade_scores_; // +0x08
    BestEntry* grind_record_{};    // +0x0C
    BestEntry* longest_manual_{};  // +0x10
    BestEntry* longest_lip_trick_{}; // +0x14
    BestEntry* biggest_chain_{};     // +0x18
};

// The game's records (0xC bytes).
class BestResults {
public:
    // 0x0057EE70 (the constructor): no levels, default initials ABC (the levels pointer left as it was). Returns
    // this.
    BestResults();
    // 0x0057EEA0: the default initials set. 0x0057EEC0: a level's records.
    void set_stock_initials(const char* initials);
    StageBests* get_level_records(int level);
    // 0x0057FBC0: `count` levels' records, constructed.
    void init_levels(int count);
    // 0x0057F440: every level's records from `params`' GameRecords array (only if it has as many), then the
    // DefaultInitials (ABC if missing).
    void decode_from_struct(script::Struct* params);
    // 0x0057FC50: every level's records written to `params` (gamerecords array), and the default initials.
    void write_to_structure(script::Struct* params);

    // 0x0057EED0: the instance (made first, if `create`); counted. 0x0057FDB0: one count released; the last
    // deletes it.
    static BestResults* fabricate(bool create);
    static void get_release();

    // NOT ORIGINAL - the levels' records as a span of their count.
    [[nodiscard]] std::span<StageBests> levels() const { return {levels_, static_cast<size_t>(level_count_)}; }

    // The fields other code reads and sets.
    [[nodiscard]] char* default_initials() const { return default_initials_; }

private:
    int level_count_{0};     // +0x00
    char* default_initials_; // +0x04
    StageBests* levels_{};   // +0x08
};

// 0x005AEBC0: the script command UpdateRecords (the end of a high score run, classic mode's and the 2 player modes'
// runs): the skate module's 0x00566F40 - the current level's records from the run just ended (its high score and best
// combo tables, longest combo / grind / lip trick / manual; the skate module's +0x384 says whether any was new). True.
// NOT ORIGINAL (the user's rule, 2026-10-21): while the level is the level editor's edited copy
// (editor/level_editor_layout.h) nothing is recorded - +0x384 cleared as the original does first, so no new
// record is announced - and logged. `edited_copy`: Layout::editing_copy() (a parameter, for the self-test).
bool update_records(bool edited_copy);
bool cfunc_update_records(script::Struct* params, script::Script* script);

// 0x005AEBD0: UpdateArcadeScores [level=<n>] <RecordTable> - the level's (the current career level's) arcade scores
// read from the parameters. True.
bool cfunc_update_arcade_scores(script::Struct* params, script::Script* script);
// 0x005AFAF0: UpdateInitials - the current level's new records given the default initials. True.
bool cfunc_update_initials(script::Struct* params, script::Script* script);
// 0x005AFB30: NewRecord - whether the last UpdateRecords set a record (the skate module's +0x384).
bool cfunc_new_record(script::Struct* params, script::Script* script);

highscores::BestResults* game_records();

} // namespace highscores
