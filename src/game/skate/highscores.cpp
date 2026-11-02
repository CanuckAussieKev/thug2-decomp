// The records, 0x0057ECD0-0x0057FE27, and the script commands UpdateRecords (0x005AEBC0), UpdateArcadeScores, UpdateInitials
// and NewRecord. See highscores.h.

#include "game/skate/highscores.h"

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <initializer_list>
#include <memory>
#include <span>

#include "engine/core/checked_index.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "editor/level_editor_layout.h"
#include "engine/script/cell.h"
#include "engine/script/param_struct.h"
#include "game/skate/skate_module_calls.h"
#include "game/skate/skate_module.h"
#include "platform/run_log.h"
#include <gsl/pointers>
#include "engine/core/ints.h"

namespace highscores {

memory::HeapUnique<highscores::BestResults> g_game_records;
namespace {

// Names.
constexpr uint32_t kValue = 0xE288A7CB;               // value
constexpr uint32_t kNumber = 0x696FE0AB;              // number
constexpr uint32_t kInitials = 0xA226A990;            // initials
constexpr uint32_t kRecordTable = 0xB1449176;         // recordtable
constexpr uint32_t kHighScores = 0xFA60D92F;          // highscores
constexpr uint32_t kBestCombos = 0xD91F1F72;          // bestcombos
constexpr uint32_t kArcadeScores = 0xDBE683AF;        // arcadescores
constexpr uint32_t kLongestCombo = 0x3F544EA0;        // longestcombo
constexpr uint32_t kBestGrindKey = 0x54C9597A;        // longestgrind
constexpr uint32_t kLongestLipTrick = 0x40590191;     // longestliptrick
constexpr uint32_t kLongestManual = 0x82883251;       // longestmanual
constexpr uint32_t kGameRecordsArray = 0xE764EDF2;    // (the game records' array, as written)
constexpr uint32_t kDefaultInitialsName = 0x24EB19A8; // (the default initials, as written)
// The type the records' structures are added with (11: not one of Scripts::QbValueKind's named ones).

memory::HeapUnique<highscores::BestResults>& sole() { return g_game_records; }
uint32_t& ref_tally()
{
    static uint32_t records_references{};
    return records_references;
}

void copy_initials(char* to, const char* from)
{
    const std::span<char, 4> out(to, 4);
    const std::span<const char, 3> in(from, 3); // (three characters, whether or not the text is that long, as the original)
    core::at(out, 0) = core::at(in, 0);
    core::at(out, 1) = core::at(in, 1);
    core::at(out, 2) = core::at(in, 2);
    core::at(out, 3) = 0;
}

// A 4-byte buffer for initials (zeroed if it was made).
char* new_initials()
{
    auto* text = static_cast<char*>(memory::alloc_zeroed(4));
    if (text)
        std::fill_n(text, 4, static_cast<char>(0));
    return text;
}

// A record as a structure: value, number, initials.
gsl::owner<script::Struct*> record_structure(const BestEntry* record)
{
    gsl::owner<script::Struct*> structure = script::new_struct();
    structure->add_component_of_type(kValue, script::kQbvalInteger, static_cast<uint32_t>(record->val_));
    structure->add_component_of_type(kNumber, script::kQbvalInteger, static_cast<uint32_t>(record->slot_no_));
    structure->append_word(kInitials, script::kQbvalString, record->tag_);
    return structure;
}

// A new longest record (its value 0).
BestEntry* new_longest_record()
{
    auto* record = static_cast<BestEntry*>(memory::alloc_zeroed(sizeof(BestEntry)));
    if (record)
        new (record) BestEntry;
    record->val_ = 0;
    return record;
}

// A new table of `count` records.
BestEntryList* new_table(int count)
{
    auto* table = static_cast<BestEntryList*>(memory::alloc_zeroed(sizeof(BestEntryList)));
    return table ? new (table) BestEntryList(count) : nullptr;
}

void delete_record(BestEntry* record)
{
    if (record) {
        memory::Hub::get_pull()->dealloc(record->tag_);
        memory::Hub::get_pull()->dealloc(record);
    }
}

void delete_table(BestEntryList* table)
{
    if (table)
        table->erase_one();
}

} // namespace

// 0x0057EF20
BestEntry::BestEntry() : tag_(new_initials()) { copy_initials(tag_, "XXX"); }

// 0x0057ECD0
bool BestEntry::try_beat_record(int value, int number)
{
    if (value > val_) {
        val_ = value;
        slot_no_ = number;
        is_fresh_ = true;
        return true;
    }
    is_fresh_ = false;
    return false;
}

void BestEntry::set_player_tag(const char* initials) { copy_initials(tag_, initials); }

// 0x0057ED00
void BestEntry::decode_from_struct(script::Struct* params)
{
    params->fetch_int_value(kValue, &val_, 0);
    if (!params->fetch_int_value(kNumber, &slot_no_, 0))
        slot_no_ = 0;
    const char* initials = "XXX";
    params->fetch_text(kInitials, &initials, 0);
    set_player_tag(initials);
}

// 0x0057EF60
BestEntryList::BestEntryList(int count) : num_(count)
{
    // (the original's new[] kept the count before the records; num_ holds it)
    auto* records = static_cast<BestEntry*>(memory::alloc_zeroed(count * sizeof(BestEntry)));
    if (!records) {
        records_ = nullptr;
        return;
    }
    records_ = records;
    for (BestEntry& entry : this->records())
        new (&entry) BestEntry;
}

void BestEntryList::erase_one()
{
    if (BestEntry* records = records_) {
        const std::span<BestEntry> entries = this->records();
        for (size_t i = entries.size(); i-- > 0;) // (as delete[]: the last first)
            memory::dealloc(core::at(entries, i).tag_);
        memory::Hub::get_pull()->dealloc(records);
    }
    memory::Hub::get_pull()->dealloc(this);
}

// 0x0057EFE0
int BestEntryList::try_insert_entry(int value, int number)
{
    for (int i = 0; i < num_; ++i)
        core::at(records(), i).is_fresh_ = false;
    const int count = num_;
    int index = 0;
    for (; index < count; ++index) {
        if (value > core::at(records(), index).val_)
            break;
    }
    if (index >= count)
        return -1;
    // The ones after it moved down (the last dropped).
    for (int i = count - 1; i > index; --i) {
        copy_initials(core::at(records(), i).tag_, core::at(records(), i - 1).tag_);
        core::at(records(), i).val_ = core::at(records(), i - 1).val_;
        core::at(records(), i).slot_no_ = core::at(records(), i - 1).slot_no_;
    }
    core::at(records(), index).val_ = value;
    core::at(records(), index).slot_no_ = number;
    copy_initials(core::at(records(), index).tag_, "XXX");
    core::at(records(), index).is_fresh_ = true;
    return index;
}

// 0x0057ED80
void BestEntryList::update_initials(const char* initials)
{
    for (int i = 0; i < num_; ++i) {
        if (core::at(records(), i).is_fresh_)
            core::at(records(), i).set_player_tag(initials);
    }
}

// 0x0057F0C0
void BestEntryList::decode_from_struct(script::Struct* params)
{
    script::Array* array = nullptr;
    params->fetch_list("RecordTable", &array, 0);
    if (num_ != ints::as_signed(array->extent_))
        return;
    for (int i = 0; i < num_; ++i)
        core::at(records(), i).decode_from_struct(array->struct_at(i));
}

// 0x0057F4E0
void BestEntryList::write_to_structure(script::Struct* params)
{
    gsl::owner<script::Array*> memory = script::alloc_script_array();
    gsl::owner<script::Array*> array = memory ? memory::construct_in<script::Array>(memory) : nullptr;
    array->configure_size_kind(num_, script::kQbvalStruct);
    for (int i = 0; i < num_; ++i) {
        gsl::owner<script::Struct*> record = record_structure(&core::at(records(), i)); // (the array's)
        array->set_cell(i, script::Cell::of(record));
    }
    params->append_array_ref(kRecordTable, array);
}

// 0x0057F120
StageBests::StageBests()
{
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): values made by calls: the original's order
    high_scores_ = new_table(5);
    best_combos_ = new_table(5);
    arcade_scores_ = new_table(10);
    biggest_chain_ = new_longest_record();
    grind_record_ = new_longest_record();
    longest_lip_trick_ = new_longest_record();
    longest_manual_ = new_longest_record();
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
}

// 0x0057F670
void StageBests::destroy_body()
{
    delete_record(longest_manual_);
    delete_record(longest_lip_trick_);
    delete_record(grind_record_);
    delete_record(biggest_chain_);
    delete_table(best_combos_);
    delete_table(high_scores_);
    delete_table(arcade_scores_);
}

// 0x0057EDD0
void StageBests::update_initials(const char* initials)
{
    for (BestEntry* record : {longest_manual_, longest_lip_trick_, grind_record_, biggest_chain_}) {
        if (record->is_fresh_)
            record->set_player_tag(initials);
    }
    best_combos_->update_initials(initials);
    high_scores_->update_initials(initials);
}

// 0x0057F340
void StageBests::decode_from_struct(script::Struct* params)
{
    // (one structure pointer for all: a missing one leaves the one before)
    script::Struct* structure = nullptr;
    params->read_sub_struct(kHighScores, &structure, 0);
    high_scores_->decode_from_struct(structure);
    params->read_sub_struct(kBestCombos, &structure, 0);
    best_combos_->decode_from_struct(structure);
    if (params->read_sub_struct(kArcadeScores, &structure, 0))
        arcade_scores_->decode_from_struct(structure);
    params->read_sub_struct(kLongestCombo, &structure, 0);
    biggest_chain_->decode_from_struct(structure);
    params->read_sub_struct(kBestGrindKey, &structure, 0);
    grind_record_->decode_from_struct(structure);
    params->read_sub_struct(kLongestLipTrick, &structure, 0);
    longest_lip_trick_->decode_from_struct(structure);
    params->read_sub_struct(kLongestManual, &structure, 0);
    longest_manual_->decode_from_struct(structure);
}

// 0x0057F7B0
void StageBests::write_to_structure(script::Struct* params)
{
    gsl::owner<script::Struct*> structure = script::new_struct();
    high_scores_->write_to_structure(structure);
    params->append_struct_ref(kHighScores, structure);
    structure = script::new_struct();
    best_combos_->write_to_structure(structure);
    params->append_struct_ref(kBestCombos, structure);
    if (arcade_scores_) {
        structure = script::new_struct();
        arcade_scores_->write_to_structure(structure);
        params->append_struct_ref(kArcadeScores, structure);
    }
    params->append_struct_ref(kLongestCombo, record_structure(biggest_chain_));
    params->append_struct_ref(kBestGrindKey, record_structure(grind_record_));
    params->append_struct_ref(kLongestLipTrick, record_structure(longest_lip_trick_));
    params->append_struct_ref(kLongestManual, record_structure(longest_manual_));
}

// 0x0057EE70
BestResults::BestResults() : default_initials_(static_cast<char*>(memory::alloc_zeroed(4)))
{
    copy_initials(default_initials_, "ABC");
}

// 0x0057EEA0 / 0x0057EEC0
void BestResults::set_stock_initials(const char* initials) { copy_initials(default_initials_, initials); }
StageBests* BestResults::get_level_records(int level) { return &core::at(levels(), level); }

// 0x0057FBC0
void BestResults::init_levels(int count)
{
    level_count_ = count;
    // (the original's new[] kept the count before the levels; level_count_ holds it)
    auto* levels = static_cast<StageBests*>(memory::alloc_zeroed(count * sizeof(StageBests)));
    if (!levels) {
        levels_ = nullptr;
        return;
    }
    levels_ = levels;
    for (StageBests& stage : this->levels())
        new (&stage) StageBests;
}

// 0x0057F440
void BestResults::decode_from_struct(script::Struct* params)
{
    script::Array* array = nullptr;
    params->fetch_list("GameRecords", &array, 0);
    if (level_count_ != ints::as_signed(array->extent_))
        return;
    for (int i = 0; i < level_count_; ++i)
        core::at(levels(), i).decode_from_struct(array->struct_at(i));
    const char* initials = "ABC";
    params->fetch_text("DefaultInitials", &initials, 0);
    copy_initials(default_initials_, initials);
}

// 0x0057FC50
void BestResults::write_to_structure(script::Struct* params)
{
    gsl::owner<script::Array*> memory = script::alloc_script_array();
    gsl::owner<script::Array*> array = memory ? memory::construct_in<script::Array>(memory) : nullptr;
    array->configure_size_kind(level_count_, script::kQbvalStruct);
    for (int i = 0; i < level_count_; ++i) {
        gsl::owner<script::Struct*> structure = script::new_struct();
        core::at(levels(), i).write_to_structure(structure);
        array->set_cell(i, script::Cell::of(structure));
    }
    params->append_array_ref(kGameRecordsArray, array);
    params->append_word(kDefaultInitialsName, script::kQbvalString, default_initials_);
}

// 0x0057EED0
BestResults* BestResults::fabricate(bool create)
{
    if (!sole() && create) {
        void* memory = memory::alloc_zeroed(sizeof(BestResults), nullptr, 1);
        sole() = memory::HeapUnique<BestResults>(memory ? memory::construct_in<BestResults>(memory) : nullptr);
    }
    ++ref_tally();
    return sole().get();
}

// 0x0057FDB0
void BestResults::get_release()
{
    if (ref_tally() > 1) {
        --ref_tally();
        return;
    }
    if (BestResults* records = sole().release()) { // (no destructor: its blocks given back)
        if (StageBests* levels = records->levels_) {
            const std::span<StageBests> stages = records->levels();
            for (size_t i = stages.size(); i-- > 0;) // (as delete[]: the last first)
                core::at(stages, i).destroy_body();
            memory::Hub::get_pull()->dealloc(levels);
        }
        memory::Hub::get_pull()->dealloc(records->default_initials_);
        memory::Hub::get_pull()->dealloc(records);
    }
    ref_tally() = 0;
}

bool update_records(bool edited_copy)
{
    modules::Skate* skate = modules::Skate::sole();
    if (edited_copy) {
        if (skate)
            skate->new_record_ = false; // (no record new: as 0x00566F40 sets it before comparing)
        get_log("records: not updated - the level is the level editor's edited copy (nothing done in it reaches the "
                "records or the save)");
        return true;
    }
    skate->update_records();
    return true;
}

// 0x005AEBC0
bool cfunc_update_records(script::Struct*, script::Script*)
{
    return update_records(frontend::level_editor::layout::editing_copy());
}

// 0x005AEBD0
bool cfunc_update_arcade_scores(script::Struct* params, script::Script*)
{
    int level = 0; // (uninitialised in the original; always set below)
    if (!params->fetch_int_value("level", &level, 0))
        level = skate_calls::get_career()->fetch_stage();
    sole()->get_level_records(level)->arcade_scores_->decode_from_struct(params);
    return true;
}

// 0x005AFAF0
bool cfunc_update_initials(script::Struct*, script::Script*)
{
    const int level = skate_calls::get_career()->fetch_stage();
    BestResults* records = sole().get();
    records->get_level_records(level)->update_initials(records->default_initials_);
    return true;
}

// 0x005AFB30
bool cfunc_new_record(script::Struct*, script::Script*) { return modules::Skate::sole()->new_record_; }

} // namespace highscores
