// CREATE 100% CAS's script commands (frontend/career_complete.h).

#include "frontend/career_complete.h"

#include <cstdint>

#include "engine/core/checksums.h"
#include "engine/objects/entity.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "game/goals/goal_manager.h"
#include "game/skate/career.h"
#include "game/skate/gap_checklist.h"
#include "game/skate/skate_module.h"
#include "game/rider/rider.h"
#include "platform/run_log.h"
#include "engine/core/ints.h"
#include "platform/code_address.h"
#include "engine/core/raw_view.h"

namespace frontend::career_complete {

namespace {

uint32_t name_hash(const char* name) { return checksums::hash_from_text(name); }

// A goal's saved state as the goal manager keeps it (the names a goal's save writes - 0x00554770).
constexpr uint32_t kLevelNum = 0x1A4CDE06; // level_index_
constexpr uint32_t kUnlocked = 0xD3E93882;
constexpr uint32_t kK2206b1e7 = 0x2206B1E7;
constexpr uint32_t kAlreadyWon = 0x49807745; // hasBeaten
constexpr uint32_t kRecord = 0xC22A2B72;
// The bare name a goal's save adds: career for a story goal, classic for a classic mode one (its classic_mode_goal
// test - 0x00554770); GoalManager_ResetCareer <mode> (0x00557E70) drops the store's goals of a mode by it.
constexpr uint32_t kCareer = 0x4DA4937B;  // career
constexpr uint32_t kClassic = 0x4C10DE52; // classic

uint8_t* get_skate() { return raw::bytes(modules::g_skate_module); }

// The local skater's stats manager (component 0x4FF8DAC6, the skater's story stat challenges - stats_goals): its 70
// challenges from +0x1C, 0x24 bytes each, the status at +0x10 of each (0 open, 1 done - the stat point given -,
// 2 / 3 locked until StatsManager_UnlockAmGoals / unlock_pro_goals; StatsManager_GetGoalStatus 0x0051CB30 reads it,
// the stats menu counts the done ones: 3 + done = the stat's maximum) and the "just done" byte at +0x0C.
constexpr int kGoalStatCount = 70;
constexpr int kStatGoalSize = 0x24;
constexpr int kStatGoalStatus = 0x1C + 0x10;
constexpr int kStatGoalJustDone = 0x1C + 0x0C;

int complete_stat_goals()
{
    objects::GameEntity* skater = raw::as<modules::Skate>(get_skate())->get_home_rider(); // (the local one)
    auto* stats = skater ? raw::bytes(skater->fetch_module(0x4FF8DAC6)) : nullptr;
    if (!stats)
        return 0;
    for (int i = 0; i < kGoalStatCount; ++i) {
        *raw::as<int>(stats + kStatGoalStatus + i * kStatGoalSize) = 1;
        stats[kStatGoalJustDone + i * kStatGoalSize] = 0;
    }
    return kGoalStatCount;
}

// One goal put into the goal manager's store of goals that aren't loaded (+0x14) as beaten, as a goal's save writes
// it, with the bare name `mode`. A record already there (a classic score goal's best) is kept.
void store_beaten_goal(script::Struct* saved, uint32_t name, int level, uint32_t mode)
{
    int record = 0;
    script::Struct* old = nullptr;
    if (saved->read_sub_struct(name, &old, 0) && old)
        old->fetch_int_value(kRecord, &record, 0);
    script::Struct* memory = script::spawn_param_bag();
    script::Struct* goal = memory ? memory::construct_in<script::Struct>(memory) : nullptr;
    if (!goal)
        return;
    goal->put_int(kLevelNum, level);
    goal->put_int(kUnlocked, 1);
    goal->put_int(kK2206b1e7, 0);
    goal->put_int(kAlreadyWon, 1);
    goal->put_int(kRecord, record);
    goal->put_name_key(0u, mode);
    saved->put_sub_struct(name, goal); // (a copy; replaces one of the same name)
    goal->get_clear();
    script::delete_struct(goal);
}

// Every gap of `gaps` ({name score [pos]} each) put in the level's checklist and landed once, and the level visited
// (RiderStoryProgress::all_gaps_got 0x00584BC0 asks for both). A landed gap keeps where the skater started it, which the
// pause menu's VIEW GAPS puts its camera above (gap_menu_focus_show_gap); ours get `pos` (where the gap ends: the
// node leading to its EndGap) facing along z - a gap with neither stays at the origin, but never with a zero
// direction. Gaps already landed keep their count and positions. Returns the checklist's gap count.
int fill_gap_checklist(objects::RiderStoryProgress* career, int level, script::Array* gaps)
{
    goals::GapTally* checklist = career->gaps_[level];
    if (!checklist)
        return 0;
    const maths::Vec4 facing = {0.0f, 0.0f, 1.0f, 0.0f};
    for (uint32_t i = 0; i < gaps->extent_; ++i) {
        auto* gap = gaps->struct_at(i);
        const char* name = nullptr;
        int score = 0;
        if (!gap || !gap->fetch_text(name_hash("name"), &name, 0) || !name || !*name ||
            !gap->fetch_int_value(name_hash("score"), &score, 0) || !score)
            continue;
        const int index = checklist->find_gap(name);
        if (index >= 0 && checklist->count_gaps_found(index) > 0)
            continue; // (landed already)
        checklist->register_gap_entry(name, 1,
                                      score); // (also marks the checklist built; one already there is left as it is)
        script::VectorValue pos = {0.0f, 0.0f, 0.0f, 1.0f};
        gap->fetch_vec(name_hash("pos"), &pos, 0);
        checklist->set_gap_positions(name, {pos.x, pos.y, pos.z, 1.0f}, facing);
    }
    const int count = checklist->total_of();
    for (int i = 0; i < count; ++i) {
        goals::GapEntry* gap = checklist->get_gap(i);
        if (gap && !gap->num_) {
            gap->num_ = 1;
            gap->skater_start_dir_ = facing;
        }
    }
    career->visited_[level] = true;
    return count;
}

} // namespace

bool& is_switched_on()
{
    static bool on = config_int("features", "create_100_cas", 1) != 0;
    return on;
}

bool cfunc_available(script::Struct*, script::Script*) { return is_switched_on(); }

bool cfunc_apply(script::Struct* params, script::Script*)
{
    if (!is_switched_on()) {
        get_log("create 100%% cas: off ([features] create_100_cas=0)");
        return false;
    }
    uint8_t* skate = get_skate();
    script::Array* levels = nullptr;
    if (!skate || !params || !params->fetch_list(name_hash("levels"), &levels, 0) || !levels)
        return false;
    objects::RiderStoryProgress* career = raw::as<modules::Skate>(skate)->career_.get();
    const goals::GoalManager* goal_manager = raw::as<modules::Skate>(skate)->goal_manager_.get();
    script::Struct* saved = goal_manager ? goal_manager->goal_data_ : nullptr;
    if (!career || !saved)
        return false;
    int goal_count = 0, classic_count = 0, gap_levels = 0, gap_count = 0;
    for (uint32_t i = 0; i < levels->extent_; ++i) {
        auto* level = levels->struct_at(i);
        int number = -1;
        if (!level || !level->fetch_int_value(name_hash("level"), &number, 0) || number < 0 ||
            number >= objects::RiderStoryProgress::kLevels)
            continue;
        // The story's points and potential, as deposit_money (0x005845C0) and set_level_amount (0x005845F0) change them.
        int points = 0, total = 0;
        if (level->fetch_int_value(name_hash("points"), &points, 0)) {
            level->fetch_int_value(name_hash("total"), &total, 0);
            career->lifetime_funds_ =
                static_cast<uint16_t>(career->lifetime_funds_ + (points - career->funds_[number]));
            career->funds_[number] = static_cast<uint16_t>(points);
            career->total_amount_ = static_cast<uint16_t>(career->total_amount_ + (total - career->amount_[number]));
            career->amount_[number] = static_cast<uint16_t>(total);
        }
        script::Array* goals = nullptr;
        if (level->fetch_list(name_hash("goals"), &goals, 0) && goals) {
            for (uint32_t g = 0; g < goals->extent_; ++g, ++goal_count)
                store_beaten_goal(saved, goals->fetch_part(g), number, kCareer);
        }
        // Classic mode: its goals beaten, and the level's classic goal counter at their number - what each classic
        // goal's win adds (the goal win 0x005576D0 calls increment_counter 0x00584620 in classic mode);
        // GetClassicLevelGoalCount 0x005AEAE0 reads it for the level select and GAME PROGRESS.
        script::Array* classic = nullptr;
        if (level->fetch_list(name_hash("classic_goals"), &classic, 0) && classic) {
            for (uint32_t g = 0; g < classic->extent_; ++g, ++classic_count)
                store_beaten_goal(saved, classic->fetch_part(g), number, kClassic);
            if (number < ints::as_signed(sizeof(career->counter_)))
                career->counter_[number] = static_cast<uint8_t>(classic->extent_);
        }
        script::Array* gaps = nullptr;
        if (level->fetch_list(name_hash("gaps"), &gaps, 0) && gaps) {
            gap_count += fill_gap_checklist(career, number, gaps);
            ++gap_levels;
        }
    }
    // The classic total complete as the counters add up (the total possible, +0x5EF, is SetClassicTotalGoals').
    int counter_total = 0;
    for (const uint8_t counter : career->counter_)
        counter_total += counter;
    career->total_counter_ = static_cast<uint8_t>(counter_total);
    const int stat_goals = complete_stat_goals();
    get_log(
        "create 100%% cas: %u levels; %d story goals beaten, story points %u / %u; %d classic goals beaten, classic "
        "goals %u / %u; %d gaps got in %d levels, all gaps got: %d; %d stat challenges done",
        levels->extent_, goal_count, career->lifetime_funds_, career->total_amount_, classic_count,
        career->total_counter_, career->flag_5e_f_, gap_count, gap_levels, career->all_gaps_got() ? 1 : 0, stat_goals);
    return true;
}

void register_script_functions()
{
    static const script::NativeCommand kFunctions[] = {
        {"Career100_Available", code_address(&cfunc_available)},
        {"Career100_Apply", code_address(&cfunc_apply)},
    };
    script::install_script_functions(kFunctions, sizeof(kFunctions) / sizeof(kFunctions[0]));
}

} // namespace frontend::career_complete
