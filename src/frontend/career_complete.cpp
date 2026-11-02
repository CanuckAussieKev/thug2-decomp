// CREATE 100% CAS's script commands (frontend/career_complete.h).

#include "frontend/career_complete.h"

#include <cstdint>
#include <array>

#include "engine/core/checksums.h"
#include "engine/core/mem.h"
#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "engine/core/down_cast.h"
#include "game/rider/stats_manager.h"
#include "game/goals/goal_manager.h"
#include "game/skate/career.h"
#include "game/skate/gap_checklist.h"
#include "game/skate/skate_module.h"
#include "game/rider/rider.h"
#include "platform/run_log.h"
#include "engine/core/ints.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>

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


// The local skater's stats manager (its story stat challenges - stats_goals): every challenge done (the stat point
// given; the stats menu counts the done ones: 3 + done = the stat's maximum) and none "just met".
int complete_stat_goals()
{
    objects::GameEntity* skater = modules::Skate::sole()->get_home_rider(); // (the local one)
    auto* stats = skater ? core::down_cast<objects::AttributeTracker>(skater->fetch_module(objects::AttributeTracker::kType))
                         : nullptr;
    if (!stats)
        return 0;
    for (objects::AttributeTracker::Goal& goal : stats->goals()) {
        goal.cur_standing_ = 1;
        goal.met_ = false;
    }
    return objects::AttributeTracker::kGoals;
}

// One goal put into the goal manager's store of goals that aren't loaded (+0x14) as beaten, as a goal's save writes
// it, with the bare name `mode`. A record already there (a classic score goal's best) is kept.
void store_beaten_goal(script::Struct* saved, uint32_t name, int level, uint32_t mode)
{
    int record = 0;
    script::Struct* old = nullptr;
    if (saved->read_sub_struct(name, &old, 0) && old)
        old->fetch_int_value(kRecord, &record, 0);
    gsl::owner<script::Struct*> memory = script::spawn_param_bag();
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
    goals::GapTally* checklist = core::at(career->gaps(), level);
    if (!checklist)
        return 0;
    const maths::Vec4 facing = {.x = 0.0f, .y = 0.0f, .z = 1.0f, .w = 0.0f};
    for (uint32_t i = 0; i < gaps->extent(); ++i) {
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
        script::VectorValue pos = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
        gap->fetch_vec(name_hash("pos"), &pos, 0);
        checklist->set_gap_positions(name, {.x = pos.x, .y = pos.y, .z = pos.z, .w = 1.0f}, facing);
    }
    const int count = checklist->total_of();
    for (int i = 0; i < count; ++i) {
        goals::GapEntry* gap = checklist->get_gap(i);
        if (gap && !gap->num()) {
            gap->num() = 1;
            gap->set_skater_start_dir(facing);
        }
    }
    core::at(career->visited(), level) = true;
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
    const modules::Skate* skate = modules::Skate::sole();
    script::Array* levels = nullptr;
    if (!skate || !params || !params->fetch_list(name_hash("levels"), &levels, 0) || !levels)
        return false;
    objects::RiderStoryProgress* career = skate->career().get();
    const goals::GoalManager* goal_manager = skate->goal_manager().get();
    script::Struct* saved = goal_manager ? goal_manager->goal_data() : nullptr;
    if (!career || !saved)
        return false;
    int goal_count = 0, classic_count = 0, gap_levels = 0, gap_count = 0;
    for (uint32_t i = 0; i < levels->extent(); ++i) {
        auto* level = levels->struct_at(i);
        int number = -1;
        if (!level || !level->fetch_int_value(name_hash("level"), &number, 0) || number < 0 ||
            number >= objects::RiderStoryProgress::kLevels)
            continue;
        // The story's points and potential, as deposit_money (0x005845C0) and set_level_amount (0x005845F0) change them.
        int points = 0, total = 0;
        if (level->fetch_int_value(name_hash("points"), &points, 0)) {
            level->fetch_int_value(name_hash("total"), &total, 0);
            career->set_lifetime_funds(static_cast<uint16_t>(career->lifetime_funds() + (points - core::at(career->funds(), number))));
            core::at(career->funds(), number) = static_cast<uint16_t>(points);
            career->set_total_amount(static_cast<uint16_t>(career->total_amount() + (total - core::at(career->amount(), number))));
            core::at(career->amount(), number) = static_cast<uint16_t>(total);
        }
        script::Array* goals = nullptr;
        if (level->fetch_list(name_hash("goals"), &goals, 0) && goals) {
            for (uint32_t g = 0; g < goals->extent(); ++g, ++goal_count)
                store_beaten_goal(saved, goals->fetch_part(g), number, kCareer);
        }
        // Classic mode: its goals beaten, and the level's classic goal counter at their number - what each classic
        // goal's win adds (the goal win 0x005576D0 calls increment_counter 0x00584620 in classic mode);
        // GetClassicLevelGoalCount 0x005AEAE0 reads it for the level select and GAME PROGRESS.
        script::Array* classic = nullptr;
        if (level->fetch_list(name_hash("classic_goals"), &classic, 0) && classic) {
            for (uint32_t g = 0; g < classic->extent(); ++g, ++classic_count)
                store_beaten_goal(saved, classic->fetch_part(g), number, kClassic);
            if (number < ints::as_signed(sizeof(career->counter())))
                core::at(career->counter(), number) = static_cast<uint8_t>(classic->extent());
        }
        script::Array* gaps = nullptr;
        if (level->fetch_list(name_hash("gaps"), &gaps, 0) && gaps) {
            gap_count += fill_gap_checklist(career, number, gaps);
            ++gap_levels;
        }
    }
    // The classic total complete as the counters add up (the total possible, +0x5EF, is SetClassicTotalGoals').
    int counter_total = 0;
    for (const uint8_t counter : career->counter())
        counter_total += counter;
    career->set_total_counter(static_cast<uint8_t>(counter_total));
    const int stat_goals = complete_stat_goals();
    get_log(
        "create 100%% cas: %u levels; %d story goals beaten, story points %u / %u; %d classic goals beaten, classic "
        "goals %u / %u; %d gaps got in %d levels, all gaps got: %d; %d stat challenges done",
        levels->extent(), goal_count, career->lifetime_funds(), career->total_amount(), classic_count,
        career->total_counter(), career->flag_5e_f(), gap_count, gap_levels, career->all_gaps_got() ? 1 : 0, stat_goals);
    return true;
}

void register_script_functions()
{
    static const auto kFunctions = std::to_array<script::NativeCommand>({
        {.name = "Career100_Available", .function = &cfunc_available},
        {.name = "Career100_Apply", .function = &cfunc_apply},
    });
    script::install_script_functions(kFunctions);
}

} // namespace frontend::career_complete
