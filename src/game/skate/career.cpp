// The skater's career, 0x00584110-0x00584CF3. See career.h.

#include "game/skate/career.h"

#include <cstddef>
#include <cstdint>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "editor/level_editor_career.h"
#include "engine/maths/vec4.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/skate/gap_checklist.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "engine/net/gamenet_manager.h"

namespace objects {

// Levels left out of the totals (a byte each, up to the cheat flag at 0x007CE4FD).
static bool g_career_left_out[RiderStoryProgress::kLevels];

namespace {

bool* left_out() { return g_career_left_out; }

uint32_t get_bit(int index) { return 1u << (index & 0x1F); }
int get_word(int index) { return index >> 5; }

int pop_count(uint32_t word)
{
    int count = 0;
    for (int i = 0; i < 32; ++i, word >>= 1)
        count += static_cast<int>(word & 1);
    return count;
}

} // namespace

// 0x00584110 / 0x00584130
goals::GapTally* RiderStoryProgress::fetch_gap_list(int level) { return gaps_[level == -1 ? level_ : level]; }
int RiderStoryProgress::fetch_stage() { return level_; }

// 0x00584140
void RiderStoryProgress::set_goal(int goal, int level)
{
    if (level == -1)
        level = level_;
    goal_words()[level * 8 + get_word(goal)] |= get_bit(goal);
}

// 0x00584180
void RiderStoryProgress::un_set_goal(int goal, int level)
{
    if (level == -1)
        level = level_;
    uint32_t mask = ~get_bit(goal);
    goal_words()[level * 8 + get_word(goal)] &= mask;
    start_goals_[get_word(goal)] &= mask;
}

// 0x005841D0
bool RiderStoryProgress::get_goal(int goal, int level)
{
    if (level == -1)
        level = level_;
    return (goal_words()[level * 8 + get_word(goal)] & get_bit(goal)) != 0;
}

// 0x00584210
bool RiderStoryProgress::just_got_goal(int goal)
{
    return ((start_goals_[get_word(goal)] ^ goal_words()[level_ * 8 + get_word(goal)]) & get_bit(goal)) != 0;
}

// 0x00584250
int RiderStoryProgress::count_goals(int level)
{
    if (level == -1)
        level = level_;
    return pop_count(goals_[level][0]) + pop_count(goals_[level][1]);
}

// 0x005842A0
int RiderStoryProgress::count_total_goals()
{
    int total = 0;
    for (int level = 0; level < kLevels; ++level) {
        if (!left_out()[level])
            total += pop_count(goals_[level][1]) + pop_count(goals_[level][0]);
    }
    return total;
}

// 0x00584320
int RiderStoryProgress::count_levels_with_goals()
{
    int count = 0;
    for (int level = 0; level < kLevels; ++level) {
        if (left_out()[level])
            count += pop_count(goals_[level][1]) + pop_count(goals_[level][0]) > 0 ? 1 : 0;
    }
    return count;
}

// 0x005843A0 / 0x005843D0 / 0x00584400
void RiderStoryProgress::set_global_flag(int flag) { global_flags_[get_word(flag)] |= get_bit(flag); }
void RiderStoryProgress::un_set_global_flag(int flag) { global_flags_[get_word(flag)] &= ~get_bit(flag); }
bool RiderStoryProgress::get_global_flag(int flag) { return (global_flags_[get_word(flag)] & get_bit(flag)) != 0; }

// 0x00584430
void RiderStoryProgress::set_flag(int flag, int level)
{
    if (level == -1)
        level = level_;
    flag_words()[level * 8 + get_word(flag)] |= get_bit(flag);
}

// 0x00584470
void RiderStoryProgress::un_set_flag(int flag, int level)
{
    if (level == -1)
        level = level_;
    uint32_t mask = ~get_bit(flag);
    flag_words()[level * 8 + get_word(flag)] &= mask;
    start_flags_[get_word(flag)] &= mask;
}

// 0x005844C0
bool RiderStoryProgress::fetch_marker(int flag, int level)
{
    if (level == -1)
        level = level_;
    return (flag_words()[level * 8 + get_word(flag)] & get_bit(flag)) != 0;
}

// 0x00584500
bool RiderStoryProgress::just_got_flag(int flag)
{
    return ((start_flags_[get_word(flag)] ^ flag_words()[level_ * 8 + get_word(flag)]) & get_bit(flag)) != 0;
}

// 0x00584540 / 0x00584560
int RiderStoryProgress::get_info_integer(uint32_t name)
{
    int value = 0;
    details_->fetch_int_value(name, &value, 1);
    return value;
}
bool RiderStoryProgress::set_info_integer(uint32_t name, int value)
{
    details_->put_int(name, value);
    return true;
}

// 0x00584580
void RiderStoryProgress::add_to_info_471f_c_4d_d(int amount)
{
    int value = 0;
    details_->fetch_int_value(0x471FC4DD, &value, 1);
    details_->put_int(0x471FC4DD, value + amount);
}

// 0x005845C0
uint16_t* RiderStoryProgress::deposit_money(int amount)
{
    lifetime_funds_ = static_cast<uint16_t>(lifetime_funds_ + amount);
    funds_[level_] = static_cast<uint16_t>(funds_[level_] + amount);
    return &funds_[level_];
}

// 0x005845F0
void RiderStoryProgress::set_level_amount(int amount)
{
    uint16_t& level = amount_[level_];
    total_amount_ = static_cast<uint16_t>(total_amount_ + static_cast<uint16_t>(amount - level));
    level = static_cast<uint16_t>(amount);
}

// 0x00584620 / 0x00584640 / 0x00584650
uint8_t* RiderStoryProgress::increment_counter()
{
    uint8_t* counter = &counter_[level_];
    ++*counter;
    ++total_counter_;
    return counter;
}
uint8_t RiderStoryProgress::get_counter(int level) { return counter_[level]; }
void RiderStoryProgress::set_flag_5e_f(uint8_t value) { flag_5e_f_ = value; }

// 0x00584660
void RiderStoryProgress::set_value634(int value)
{
    value634_ = value;
    if (value > max_value634_)
        max_value634_ = value;
}

// 0x00584680
bool RiderStoryProgress::get_reset(bool reset_gaps, uint32_t part)
{
    if (reset_gaps) {
        for (auto& gap : gaps_)
            gap->get_reset();
        frontend::level_editor::career::career_replaced(); // (NOT ORIGINAL: an edited copy's baseline taken again)
    }
    level_ = 0;
    // A flag and the goal of the same number cleared (and their level-start copies).
    auto clear = [this](int bit, int level) {
        un_set_flag(bit, level);
        uint32_t mask = ~get_bit(bit);
        goal_words()[level * 8 + get_word(bit)] &= mask;
        start_goals_[get_word(bit)] &= mask;
    };
    if (part == 0 || part == 0x4DA4937B) {
        for (int level = 0; level < kLevels; ++level) {
            for (int bit = 0; bit < 200; ++bit)
                clear(bit, level);
            left_out()[level] = false;
            funds_[level] = 0;
            amount_[level] = 1000;
        }
        start_goals_[0] = 0;
        start_flags_[0] = 0;
        start_goals_[1] = 0;
        start_flags_[1] = 0;
        lifetime_funds_ = 0;
        value634_ = 0;
        max_value634_ = 0;
        total_amount_ = 6000;
    }
    if (part == 0 || part == 0x4C10DE52) {
        for (int level = 0; level < 18; ++level) {
            counter_[level] = 0;
            for (int bit = 200; bit < 256; ++bit)
                clear(bit, level);
        }
        total_counter_ = 0;
        flag_5e_f_ = 0x82;
        details_->append_structure_if_any(script::read_sub_struct(0x0486FD79));
    }
    return true;
}

// 0x00584BA0
bool RiderStoryProgress::reset_part(uint32_t part)
{
    get_reset(false, part);
    return true;
}

// 0x00584BC0
bool RiderStoryProgress::all_gaps_got()
{
    script::Array* levels = script::fetch_list(0xEB1A4BC9);
    uint32_t count = levels->extent_;
    for (int level = 0; level < kLevels; ++level) {
        uint32_t i = 0;
        for (; i < count; ++i) {
            if (level == script::fetch_int_value(levels->fetch_part(i)))
                break;
        }
        if (i == count)
            continue; // (not listed)
        if (!visited_[level] || !gaps_[level]->all_got())
            return false;
    }
    return true;
}

// 0x00584C60
void RiderStoryProgress::begin_level(int level)
{
    if (level == -1)
        level = level_;
    else
        level_ = level;
    visited_[level_] = true;
    start_goals_[0] = goals_[level][0];
    start_goals_[1] = goals_[level][1];
    start_flags_[0] = flag_bits_[level][0];
    start_flags_[1] = flag_bits_[level][1];
}

// 0x00584CC0
bool RiderStoryProgress::is_code_on(uint32_t name)
{
    int flag = script::fetch_int_value(name);
    bool on = (global_flags_[get_word(flag)] & get_bit(flag)) != 0;
    online::g_cheat_flag = on;
    return on;
}

namespace {

// The save's names.
constexpr uint32_t kCareerStructure = 0x4DA4937B;
constexpr uint32_t kGoalListName = 0xB7EF8BFC;
constexpr uint32_t kFlagsArray = 0xC0FA354F;
constexpr uint32_t kGlobalFlagsArray = 0xA888FE32;
constexpr uint32_t kGapLevelList = 0x9DD7E4A6;
constexpr uint32_t kGapLevel = 0x651533EC;
constexpr uint32_t kGapsBuilt = 0x73F8CA00;
constexpr uint32_t kGapList = 0xD76C173E;
constexpr uint32_t kGapName = 0x699FCFC0;
constexpr uint32_t kGapCount = 0x25EB70DB;
constexpr uint32_t kGapScore = 0x92AB03E8;
constexpr uint32_t kGapPos = 0xA4605544;
constexpr uint32_t kGapDir = 0x9E12C9F8;
constexpr uint32_t kVisitedArray = 0x7106C6BF;
constexpr uint32_t kCashArray = 0x414FD20F;
constexpr uint32_t kAmountArray = 0x9CC5BBF6;
constexpr uint32_t kCashBalanceKey = 0x0444419A;
constexpr uint32_t kTotalAmount = 0x95947B61;
constexpr uint32_t kCounterArray = 0xE888DD02;
constexpr uint32_t kTotalCounter = 0x6C1A5E39;
constexpr uint32_t kFlag5eF = 0x7A6BF912;
constexpr uint32_t kValue634 = 0xF884773C;
constexpr uint32_t kMaxValue634 = 0x1C6CF075;
constexpr uint32_t kCareerInfoKey = 0x55D761CF;

script::Array* alloc_script_array(uint32_t size, script::QbValueKind type)
{
    script::Array* memory = script::alloc_script_array();
    script::Array* array = memory ? new (memory) script::Array : nullptr;
    array->configure_size_kind(size, type);
    return array;
}
script::Struct* spawn_param_table()
{
    script::Struct* memory = script::spawn_param_bag();
    return memory ? new (memory) script::Struct : nullptr;
}

} // namespace

// 0x00585270
RiderStoryProgress::RiderStoryProgress()
{
    for (int level = 0; level < kLevels; ++level) {
        void* memory = memory::alloc(sizeof(goals::GapTally));
        gaps_[level] = memory ? new (memory) goals::GapTally : nullptr;
        visited_[level] = false;
    }
    details_ = spawn_param_table();
    get_reset(true, 0);
}

// 0x00585E40
RiderStoryProgress::~RiderStoryProgress()
{
    for (goals::GapTally* gaps : gaps_) {
        if (gaps) {
            gaps->~GapTally();
            memory::Hub::get_pull()->dealloc(gaps);
        }
    }
    if (script::Struct* info = details_) {
        info->get_clear();
        script::delete_struct(info);
    }
}

// 0x00584D00
void RiderStoryProgress::decode_from_struct(script::Struct* save)
{
    script::Struct* career = nullptr;
    save->read_sub_struct(kCareerStructure, &career, 0);
    script::Array* goals = nullptr;
    career->fetch_list(kGoalListName, &goals, 0);
    script::Array* flags = nullptr;
    career->fetch_list(kFlagsArray, &flags, 0);
    for (uint32_t first = 0; first < kLevels * 8; first += 8) {
        for (uint32_t i = 0; i < 8; ++i)
            goal_words()[first + i] = goals->fetch_part(first + i);
        for (uint32_t i = 0; i < 8; ++i)
            flag_words()[first + i] = flags->fetch_part(first + i);
    }
    script::Array* globals = nullptr;
    career->fetch_list(kGlobalFlagsArray, &globals, 0);
    for (uint32_t i = 0; i < 16; ++i)
        global_flags_[i] = globals->fetch_part(i);
    // The gap checklists.
    script::Array* gap_levels = nullptr;
    career->fetch_list(kGapLevelList, &gap_levels, 0);
    if (gap_levels) {
        for (int level = 0; level < kLevels; ++level) {
            goals::GapTally* checklist = gaps_[level];
            checklist->get_clear();
            auto* entry = reinterpret_cast<script::Struct*>(gap_levels->fetch_part(level));
            int saved_level = 0, built = 0;
            entry->fetch_int_value(kGapLevel, &saved_level, 1);
            entry->fetch_int_value(kGapsBuilt, &built, 1);
            if (!built)
                continue;
            script::Array* list = nullptr;
            entry->fetch_list(kGapList, &list, 0);
            int count = static_cast<int>(list->extent_);
            for (int i = 0; i < count; ++i) {
                auto* gap = reinterpret_cast<script::Struct*>(list->fetch_part(i));
                const char* name = nullptr;
                int got = 0, score = 0;
                gap->fetch_text(kGapName, &name, 0);
                gap->fetch_int_value(kGapCount, &got, 0);
                gap->fetch_int_value(kGapScore, &score, 0);
                checklist->register_gap_entry(name, got, score);
                if (got) {
                    script::VectorValue pos{}, dir{};
                    gap->fetch_vec(kGapPos, &pos, 0);
                    gap->fetch_vec(kGapDir, &dir, 0);
                    checklist->set_gap_positions(name, {pos.x, pos.y, pos.z, pos.w}, {dir.x, dir.y, dir.z, dir.w});
                }
            }
        }
    }
    script::Array* visited = nullptr;
    career->fetch_list(kVisitedArray, &visited, 0);
    if (visited) {
        for (int level = 0; level < kLevels; ++level)
            visited_[level] = visited->fetch_part(level) != 0;
    }
    script::Array* cash = nullptr;
    script::Array* amounts = nullptr;
    career->fetch_list(kCashArray, &cash, 0);
    career->fetch_list(kAmountArray, &amounts, 0);
    for (int level = 0; level < kLevels; ++level) {
        funds_[level] = static_cast<uint16_t>(cash->fetch_part(level));
        amount_[level] = static_cast<uint16_t>(amounts->fetch_part(level));
    }
    int value = 0; // (the original's is left as it was when a value is missing)
    career->fetch_int_value(kCashBalanceKey, &value, 0);
    lifetime_funds_ = static_cast<uint16_t>(value);
    career->fetch_int_value(kTotalAmount, &value, 0);
    total_amount_ = static_cast<uint16_t>(value);
    script::Array* counters = nullptr;
    career->fetch_list(kCounterArray, &counters, 0);
    for (int level = 0; level < 18; ++level)
        counter_[level] = static_cast<uint8_t>(counters->fetch_part(level));
    if (career->fetch_int_value(kTotalCounter, &value, 0)) {
        total_counter_ = static_cast<uint8_t>(value);
        career->fetch_int_value(kFlag5eF, &value, 0);
        flag_5e_f_ = static_cast<uint8_t>(value);
    } else {
        // An older save: the total from the counters (+0x5EF 0x8C once it passes it).
        total_counter_ = 0;
        flag_5e_f_ = 0x82;
        for (unsigned char level : counter_) {
            total_counter_ = static_cast<uint8_t>(total_counter_ + level);
            if (total_counter_ > flag_5e_f_)
                flag_5e_f_ = 0x8C;
        }
    }
    career->fetch_int_value(kValue634, &value634_, 0);
    career->fetch_int_value(kMaxValue634, &max_value634_, 0);
    script::Struct* info = nullptr; // (the original's is left as it was)
    career->read_sub_struct(kCareerInfoKey, &info, 0);
    details_->append_structure_if_any(info);
    frontend::level_editor::career::career_replaced(); // (NOT ORIGINAL: an edited copy's baseline taken again)
}

// 0x00585550
void RiderStoryProgress::write_to_structure(script::Struct* save)
{
    script::Array* goals = alloc_script_array(kLevels * 8, script::kQbvalInteger);
    for (uint32_t first = 0; first < kLevels * 8; first += 8) {
        for (uint32_t i = 0; i < 8; ++i)
            goals->set_element(first + i, goal_words()[first + i]);
    }
    script::Array* flags = alloc_script_array(kLevels * 8, script::kQbvalInteger);
    for (uint32_t first = 0; first < kLevels * 8; first += 8) {
        for (uint32_t i = 0; i < 8; ++i)
            flags->set_element(first + i, flag_words()[first + i]);
    }
    script::Array* globals = alloc_script_array(16, script::kQbvalInteger);
    for (uint32_t i = 0; i < 16; ++i)
        globals->set_element(i, global_flags_[i]);
    script::Array* gap_levels = alloc_script_array(kLevels, script::kQbvalStruct);
    for (int level = 0; level < kLevels; ++level) {
        goals::GapTally* checklist = gaps_[level];
        script::Struct* entry = spawn_param_table();
        entry->put_int(kGapLevel, level);
        // NOT ORIGINAL: an edited copy's checklist (level_editor_career.h) is written as before the edited copy - never
        // the editor's gaps, nor gaps landed in it.
        frontend::level_editor::career::Checklist view;
        if (frontend::level_editor::career::save_view(checklist, &view)) {
            entry->put_int(kGapsBuilt, view.built ? 1 : 0);
            if (view.built) {
                script::Array* list = alloc_script_array(static_cast<uint32_t>(view.gaps.size()), script::kQbvalStruct);
                for (size_t i = 0; i < view.gaps.size(); ++i) {
                    const frontend::level_editor::career::Gap& g = view.gaps[i];
                    script::Struct* gap = spawn_param_table();
                    gap->append_word(kGapName, g.name.c_str());
                    gap->put_int(kGapCount, g.count);
                    gap->put_int(kGapScore, g.score);
                    gap->put_vec3(kGapPos, g.pos[0], g.pos[1], g.pos[2]);
                    gap->put_vec3(kGapDir, g.dir[0], g.dir[1], g.dir[2]);
                    list->set_element(static_cast<uint32_t>(i), reinterpret_cast<uint32_t>(gap));
                }
                entry->append_array_ref(kGapList, list);
            }
        } else if (!checklist->built_) {
            entry->put_int(kGapsBuilt, 0);
        } else {
            entry->put_int(kGapsBuilt, 1);
            int count = checklist->total_of();
            script::Array* list = alloc_script_array(static_cast<uint32_t>(count), script::kQbvalStruct);
            for (int i = 0; i < count; ++i) {
                script::Struct* gap = spawn_param_table();
                gap->append_word(kGapName, checklist->get_gap_title(i));
                gap->put_int(kGapCount, checklist->count_gaps_found(i));
                gap->put_int(kGapScore, checklist->get_gap_points(i));
                maths::Vec4 pos = {0.0f, 0.0f, 0.0f, 1.0f}, dir = {0.0f, 0.0f, 0.0f, 1.0f};
                if (checklist->count_gaps_found(i))
                    checklist->get_gap_positions(i, &pos, &dir);
                gap->put_vec3(kGapPos, pos.x, pos.y, pos.z);
                gap->put_vec3(kGapDir, dir.x, dir.y, dir.z);
                list->set_element(static_cast<uint32_t>(i), reinterpret_cast<uint32_t>(gap));
            }
            entry->append_array_ref(kGapList, list);
        }
        gap_levels->set_element(static_cast<uint32_t>(level), reinterpret_cast<uint32_t>(entry));
    }
    script::Array* visited = alloc_script_array(kLevels, script::kQbvalInteger);
    for (int level = 0; level < kLevels; ++level)
        visited->set_element(static_cast<uint32_t>(level), visited_[level]);
    script::Struct* career = spawn_param_table();
    career->add_component_of_type(kGoalListName, script::kQbvalArray, reinterpret_cast<uint32_t>(goals));
    career->add_component_of_type(kFlagsArray, script::kQbvalArray, reinterpret_cast<uint32_t>(flags));
    career->add_component_of_type(kGlobalFlagsArray, script::kQbvalArray, reinterpret_cast<uint32_t>(globals));
    career->add_component_of_type(kGapLevelList, script::kQbvalArray, reinterpret_cast<uint32_t>(gap_levels));
    career->append_array_ref(kVisitedArray, visited);
    career->put_name_key(0xE3335D2F, modules::g_skate_module->level_);
    career->put_name_key(0x2CC06F5E, goals::g_game_mode->get_name_key());
    // (both made first, then sized, as the original)
    script::Array* cash_memory = script::alloc_script_array();
    script::Array* cash = cash_memory ? new (cash_memory) script::Array : nullptr;
    script::Array* amount_memory = script::alloc_script_array();
    script::Array* amounts = amount_memory ? new (amount_memory) script::Array : nullptr;
    cash->configure_size_kind(kLevels, script::kQbvalInteger);
    amounts->configure_size_kind(kLevels, script::kQbvalInteger);
    for (int level = 0; level < kLevels; ++level) {
        cash->set_element(static_cast<uint32_t>(level), funds_[level]);
        amounts->set_element(static_cast<uint32_t>(level), amount_[level]);
    }
    career->append_array_ref(kCashArray, cash);
    career->append_array_ref(kAmountArray, amounts);
    career->put_int(kCashBalanceKey, lifetime_funds_);
    career->put_int(kTotalAmount, total_amount_);
    script::Array* counters = alloc_script_array(18, script::kQbvalInteger);
    for (int level = 0; level < 18; ++level)
        counters->set_element(static_cast<uint32_t>(level), counter_[level]);
    career->append_array_ref(kCounterArray, counters);
    career->put_int(kTotalCounter, total_counter_);
    career->put_int(kFlag5eF, flag_5e_f_);
    career->put_int(kValue634, value634_);
    career->put_int(kMaxValue634, max_value634_);
    career->put_sub_struct(kCareerInfoKey, details_);
    save->add_component_of_type(kCareerStructure, 11, reinterpret_cast<uint32_t>(career));
}

// 0x00585E20 (GetCareerSaveInfo): the skate module's career written to the script's results.
bool cfunc_get_career_save_info(script::Struct* /*params*/, script::Script* script)
{
    RiderStoryProgress* career = modules::g_skate_module->career_.get();
    career->write_to_structure(script->args_);
    return true;
}

} // namespace objects
