// Script commands of the career and the run, 0x005AE990-0x005AF08F (see career_cfuncs.h).

#include "game/skate/career_cfuncs.h"

#include <cstdint>

#include "game/online/gamenet_manager.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/script/context.h"
#include "game/skate/career.h"
#include "game/skate/skate_module_calls.h"
#include "game/rider/rider.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "game/online/net_game_state.h"

namespace commands {

namespace {

using namespace skate_calls;

constexpr uint32_t kLevel = 0x651533EC;               // level
constexpr uint32_t kLevelScore = 0xA7BFCDA7;          // level_score
constexpr uint32_t kLevelScoreTotal = 0x756FB474;     // level_score_total
constexpr uint32_t kGetTotal = 0x580DF1BA;            // get_total
constexpr uint32_t kTotalScore = 0x0E145E9C;          // total_score
constexpr uint32_t kTotalScorePotential = 0x851B0904; // total_score_potential
constexpr uint32_t kClassicGoalCount = 0x062BB9BB;    // classic_goal_count
constexpr uint32_t kTotalGoalsComplete = 0x959DCF6E;  // total_goals_complete
constexpr uint32_t kTotalGoalsPossible = 0xA95F9E73;  // total_goals_possible
constexpr uint32_t kChapter = 0x067E4AD1;             // chapter
constexpr uint32_t kGoal = 0x032314D1;                // goal
constexpr uint32_t kFlag = 0x2E0B1465;                // flag
constexpr uint32_t kIsCareer = 0x1DED1EA4;            // is_career
constexpr uint32_t kSkaterType = 0xBAF9B341;          // OBJECT_TYPE_SKATER

// The skate module's calls about the run (no source yet): 0x00567580 every skater idle (in a horse game only the
// current horse skater counts), 0x00567690 the local skater's first trick started, 0x005676E0 completed;
// 0x0056BB70 the final scores sent to every player (true: the final ones).
bool all_skaters_idle() { return modules::Skate::sole()->all_skaters_idle(); }
bool first_trick_started() { return modules::Skate::sole()->first_trick_started(); }
bool first_trick_completed() { return modules::Skate::sole()->first_trick_completed(); }
void send_scores(bool final) { modules::Skate::sole()->broadcast_points(final); }


} // namespace

// 0x005AE990: CareerStartLevel level=<n> - the career's level n started (its goals and flags noted for JustGot*).
bool cfunc_career_start_level(script::Struct* params, script::Context /*context*/)
{
    int level = 0;
    params->fetch_int_value(kLevel, &level, 1);
    get_career()->begin_level(level);
    return true;
}

// 0x005AE9C0: CareerLevelIs <n> - whether the career's current level is n.
bool cfunc_career_level_is(script::Struct* params, script::Context /*context*/)
{
    int level = 0;
    params->fetch_int_value(0U, &level, 1);
    return get_career()->fetch_stage() == level;
}

// 0x005AE9F0: GetCareerLevelScore [level=<n>] [get_total] - the level's (the current one's) score and total score as
// level_score / level_score_total; with get_total the career's totals too (total_score, total_score_potential).
bool cfunc_get_career_level_score(script::Struct* params, script::Context context)
{
    objects::RiderStoryProgress* career = get_career();
    int level = career->fetch_stage();
    params->fetch_int_value(kLevel, &level, 0);
    context.args()->put_int(checksums::Name{kLevelScore}, core::at(career->funds(), level));
    context.args()->put_int(checksums::Name{kLevelScoreTotal}, core::at(career->amount(), level));
    if (params->has_bare_flag(kGetTotal)) {
        context.args()->put_int(checksums::Name{kTotalScore}, career->lifetime_funds());
        context.args()->put_int(checksums::Name{kTotalScorePotential}, career->total_amount());
    }
    return true;
}

// 0x005AEAA0: SetLevelTotalPoints <n> - the current level's total score.
bool cfunc_set_level_total_points(script::Struct* params, script::Context /*context*/)
{
    objects::RiderStoryProgress* career = get_career();
    int points = 0;
    params->fetch_int_value(0U, &points, 1);
    career->set_level_amount(points);
    return true;
}

// 0x005AEAE0: GetClassicLevelGoalCount [level=<n>] [get_total] - the level's (the current one's) classic goals done
// as classic_goal_count; with get_total the totals too (total_goals_complete, total_goals_possible).
bool cfunc_get_classic_level_goal_count(script::Struct* params, script::Context context)
{
    objects::RiderStoryProgress* career = get_career();
    int level = career->fetch_stage();
    params->fetch_int_value(kLevel, &level, 0);
    context.args()->put_int(checksums::Name{kClassicGoalCount}, career->get_counter(level));
    if (params->has_bare_flag(kGetTotal)) {
        context.args()->put_int(checksums::Name{kTotalGoalsComplete}, career->total_counter());
        context.args()->put_int(checksums::Name{kTotalGoalsPossible}, career->flag_5e_f());
    }
    return true;
}

// 0x005AEB80: SetClassicTotalGoals <n> - how many classic goals there are in all.
bool cfunc_set_classic_total_goals(script::Struct* params, script::Context /*context*/)
{
    objects::RiderStoryProgress* career = get_career();
    int goals = 0;
    params->fetch_int_value(0U, &goals, 1);
    career->set_flag_5e_f(static_cast<uint8_t>(goals));
    return true;
}

// 0x005AEC20: GetCurrentChapter - the career's chapter as chapter.
bool cfunc_get_current_chapter(script::Struct* /*params*/, script::Context context)
{
    context.args()->put_int(checksums::Name{kChapter}, get_career()->value634());
    return true;
}

// 0x005AEC50: SetCurrentChapter chapter=<n> - the career's chapter (the highest reached kept too).
bool cfunc_set_current_chapter(script::Struct* params, script::Context /*context*/)
{
    objects::RiderStoryProgress* career = get_career();
    int chapter = 0; // (uninitialised in the original)
    params->fetch_int_value(kChapter, &chapter, 0);
    career->set_value634(chapter);
    return true;
}

// 0x005AEC80: CareerReset [<part>] - the career reset (the part named, or all of it), its gap checklists kept.
bool cfunc_career_reset(script::Struct* params, script::Context /*context*/)
{
    objects::RiderStoryProgress* career = get_career();
    uint32_t part = 0;
    params->read_hash_value(0U, &part, 0);
    career->reset_part(part);
    return true;
}

// 0x005AECC0: SetGoal goal=<n> - the current level's goal n got (only in career mode).
bool cfunc_set_goal(script::Struct* params, script::Context /*context*/)
{
    int goal = 0;
    params->fetch_int_value("goal", &goal, 1);
    if (game_mode_is_true(kIsCareer)) {
        get_career()->set_goal(goal, -1);
    }
    return true;
}

// 0x005AED10: UnSetGoal goal=<n> - the current level's goal n not got.
bool cfunc_un_set_goal(script::Struct* params, script::Context /*context*/)
{
    int goal = 0;
    params->fetch_int_value("goal", &goal, 1);
    get_career()->un_set_goal(goal, -1);
    return true;
}

// 0x005AED40: GetGoal goal=<n> - whether the current level's goal n is got.
bool cfunc_get_goal(script::Struct* params, script::Context /*context*/)
{
    int goal = 0;
    params->fetch_int_value("goal", &goal, 1);
    return get_career()->get_goal(goal, -1);
}

// 0x005AED70: JustGotGoal goal=<n> - whether goal n was got since the level started.
bool cfunc_just_got_goal(script::Struct* params, script::Context /*context*/)
{
    int goal = 0;
    params->fetch_int_value(kGoal, &goal, 1);
    return get_career()->just_got_goal(goal);
}

// 0x005AEDA0: SetFlag flag=<n> - the current level's flag n set.
bool cfunc_set_flag(script::Struct* params, script::Context /*context*/)
{
    int flag = 0;
    params->fetch_int_value(kFlag, &flag, 1);
    get_career()->set_flag(flag, -1);
    return true;
}

// 0x005AEDD0: UnSetFlag flag=<n> [level=<l>] - level l's (the current level's) flag n cleared.
bool cfunc_un_set_flag(script::Struct* params, script::Context /*context*/)
{
    int flag = 0;
    params->fetch_int_value(kFlag, &flag, 1);
    int level = -1;
    params->fetch_int_value(kLevel, &level, 0);
    get_career()->un_set_flag(flag, level);
    return true;
}

// 0x005AEE30: fetch_marker flag=<n> [level=<l>] - whether level l's flag n is set (no level, or level 0: the current
// level's - level 0 itself can't be asked for).
bool cfunc_get_flag(script::Struct* params, script::Context /*context*/)
{
    int flag = 0;
    params->fetch_int_value(kFlag, &flag, 1);
    int level = 0;
    params->fetch_int_value(kLevel, &level, 0);
    objects::RiderStoryProgress* career = get_career();
    if (level != 0) {
        return career->fetch_marker(flag, level);
    }
    return career->fetch_marker(flag, -1);
}

// 0x005AEEA0: JustGotFlag flag=<n> - whether flag n was set since the level started.
bool cfunc_just_got_flag(script::Struct* params, script::Context /*context*/)
{
    int flag = 0;
    params->fetch_int_value(kFlag, &flag, 1);
    return get_career()->just_got_flag(flag);
}

// 0x005AEED0 / 0x005AEF00 / 0x005AEF30: SetGlobalFlag / UnSetGlobalFlag / GetGlobalFlag flag=<n> - the career's
// global flag n (the cheats are among them).
bool cfunc_set_global_flag(script::Struct* params, script::Context /*context*/)
{
    int flag = 0;
    params->fetch_int_value(kFlag, &flag, 1);
    get_career()->set_global_flag(flag);
    return true;
}

bool cfunc_un_set_global_flag(script::Struct* params, script::Context /*context*/)
{
    int flag = 0;
    params->fetch_int_value(kFlag, &flag, 1);
    get_career()->un_set_global_flag(flag);
    return true;
}

bool cfunc_get_global_flag(script::Struct* params, script::Context /*context*/)
{
    int flag = 0;
    params->fetch_int_value(kFlag, &flag, 1);
    return get_career()->get_global_flag(flag);
}

// 0x005AEF60: ProfileEquals <description> - whether the skater matches the description (is_pro, is_male, stance...):
// the script's own object if it's a skater; otherwise, in a game of more than one player, the first skater; else
// false.
bool cfunc_profile_equals(script::Struct* params, script::Context context)
{
    if (context) {
        objects::Entity* object = context.entity().get_pull();
        if (object && object->kind_tag() == kSkaterType) {
            return core::down_cast<objects::Rider>(object)->traits().matches_params(params);
        }
    }
    if (!is_multiplayer()) {
        return false;
    }
    auto* skater = core::down_cast<objects::Rider>(get_rider_at(0));
    if (!skater) {
        return false;
    }
    return skater->traits().matches_params(params);
}

// 0x005AEFC0: IsCareerMode - the game mode's is_career.
bool cfunc_is_career_mode(script::Struct* /*params*/, script::Context /*context*/)
{
    return game_mode_is_true(kIsCareer);
}

// 0x005AEFE0: EndRun - the run is to end.
bool cfunc_end_run(script::Struct* /*params*/, script::Context /*context*/)
{
    end_run_flag() = true;
    return true;
}

// 0x005AEFF0: ShouldEndRun - whether EndRun was asked for, or the game mode says the run is over (elimiskate).
bool cfunc_should_end_run(script::Struct* /*params*/, script::Context /*context*/)
{
    return end_run_flag() || game_mode_should_end_run();
}

// 0x005AF020: EndRunSelected - whether EndRun was asked for.
bool cfunc_end_run_selected(script::Struct* /*params*/, script::Context /*context*/) { return end_run_flag(); }

// 0x005AF030 / 0x005AF040 / 0x005AF050: AllSkatersAreIdle, FirstTrickStarted, FirstTrickCompleted - the skate
// module's.
bool cfunc_all_skaters_are_idle(script::Struct* /*params*/, script::Context /*context*/) { return all_skaters_idle(); }
bool cfunc_first_trick_started(script::Struct* /*params*/, script::Context /*context*/)
{
    return first_trick_started();
}
bool cfunc_first_trick_completed(script::Struct* /*params*/, script::Context /*context*/)
{
    return first_trick_completed();
}

// 0x005AF060: CalculateFinalScores - the server sends every player the final scores (true); a client: whether the
// final scores have come.
bool cfunc_calculate_final_scores(script::Struct* /*params*/, script::Context /*context*/)
{
    const online::Hub* net = online::online_hub();
    if (net->get_on_server()) {
        send_scores(true);
        return true;
    }
    return net->final_scores_received();
}

} // namespace commands
