// modules::Contest: 0x0054FFD0-0x005517BF (see contest.h).

#include "game/skate/contest.h"

#include <cstdint>
#include <new>
#include <array>

#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/maths/random.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/skate/profile_manager.h"
#include "game/goals/goal_manager.h"
#include "engine/script/token_reader.h"
#include "engine/objects/anim_movies.h"
#include "engine/gfx/tod_manager.h"
#include "engine/gfx/loading_bar.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/raw_view.h"
#include "engine/core/checked_index.h"

namespace modules {

float g_competition_tiny{0.0001f};
namespace {
float g_competition_two{2.0f};
float g_competition_third{0.33333334f};
float g_competition_three{3.0f};
float g_competition_eighth{0.125f};
float g_competition_nearly_half{0.499999f};
float g_competition_ninety_nine{99.0f};
float g_competition_over_half{0.500099f};
float g_competition_max_judge{99.85f};
float g_competition_max_points{99.9f};

constexpr uint32_t kNoName = 0;                    // (an unnamed component: a flag)
constexpr uint32_t kLeaderBoardNames = 0x1CC6F48D; // leader_board_names
constexpr uint32_t kFirstPlaceName = 0xFAF19012;   // first_place_name
constexpr uint32_t kName = 0xA1DC81F9;             // Name
constexpr uint32_t kScore = 0xCD66C8AE;            // score
constexpr uint32_t kPlayer = 0x67E6859A;           // player
constexpr uint32_t kTopJudge = 0x7B1B959F;         // top_judge
constexpr uint32_t kRankingList = 0x370B9420;      // leader_board
constexpr uint32_t kAddLeaderBoard = 0xCE76F8C1;   // goal_comp_add_leader_board
constexpr std::array<uint32_t, Contest::kJudges> kJudgeNames = {{
    0x84F93830, 0x1DF0698A, 0x6AF7591C, 0xF493CCBF, 0x8394FC29, // comp_str_Judge1..5
}};

// The constants, as the original reads them.
float tenth() { return script::g_parse_nudge; }           // 0.1
float eighth() { return g_competition_eighth; }           // 0.125
float two() { return g_competition_two; }                 // 2.0
float nearly_half() { return g_competition_nearly_half; } // 0.4999990 (rounding to the nearest)
float hundred() { return gfx::g_hundred; }                // 100
float max_points() { return g_competition_max_points; }   // 99.9
float get_half() { return gfx::g_half; }                  // 0.5
float get_zero() { return objects::g_float_zero; }        // 0
float max_judge() { return g_competition_max_judge; }     // 99.85
float three() { return g_competition_three; }             // 3
float ten() { return render::g_float_ten; }               // 10
float tiny() { return g_competition_tiny; }               // 0.0001
float over_half() { return g_competition_over_half; }     // 0.500099
float ninety_nine() { return g_competition_ninety_nine; } // 99
float get_third() { return g_competition_third; }         // 1/3
constexpr float kScoreSpread = 1.9f;                      // (0x3FF33333, pushed as an immediate)

// The x87 code's conversions: truncation toward zero (_ftol).
int truncate(double value) { return static_cast<int>(value); }
// A score rounded to the nearest (the original adds 0.499999 and truncates).
int current_round(float value) { return truncate(static_cast<double>(value) + nearly_half()); }

objects::ProfileRoster* roster_handle() { return objects::g_profile_roster.get(); }
// The profile manager's template at `index` (0x00557FB0) and its count (0x00469490: the table's).
objects::RiderIdentity* template_at(objects::ProfileRoster* manager, int index)
{
    // (0x00557FB0 is the lookup table's by-index code, shared with the goal manager's: its first member is the table too)
    return raw::as<objects::RiderIdentity>(raw::as<goals::GoalManager>(manager)->get_objective_at(index));
}

// The goal manager (0x005591B0) and its active competition goal (0x00558D90; null: none).
bool competition_goal_active() { return goals::get_objective_board()->get_active_competition() != nullptr; }

script::Struct* spawn_param_table()
{
    script::Struct* structure = script::spawn_param_bag();
    return structure ? memory::construct_in<script::Struct>(structure) : nullptr;
}
void delete_structure(script::Struct* structure)
{
    if (structure) {
        structure->get_clear();
        script::delete_struct(structure);
    }
}
script::Array* alloc_script_array()
{
    script::Array* array = script::alloc_script_array();
    return array ? memory::construct_in<script::Array>(array) : nullptr;
}
// An array handed to a structure (which copied it): its contents and itself freed.
void free_array(script::Array* array)
{
    script::release_array_storage(array);
    if (array) {
        array->get_clear();
        script::delete_array(array);
    }
}

} // namespace

int Contest::Leader::best_two(int count) const
{
    int best = 0;
    int second = 0;
    for (int i = 0; i < count; ++i) {
        const int score = core::at(scores_, i);
        if (score > best) {
            second = best;
            best = score;
        } else if (score > second) {
            second = score;
        }
    }
    return best + second;
}

int Contest::Leader::best_two_of_four_words() const
{
    int best = 0;
    int second = 0;
    for (int i = 0; i < 4; ++i) {
        const int score = get_word(i);
        if (score > best) {
            second = best;
            best = score;
        } else if (score > second) {
            second = score;
        }
    }
    return best + second;
}

// 0x0054FFD0
// 0x00550010
void Contest::rewrite_args(script::Struct* params) { args_->append_structure_if_any(params); }

// 0x00550020
void Contest::begin_timed_session()
{
    ++runs_;
    path_vertices_ = 0.0f;
    crash_penalty_ = 0.0f;
}

// 0x00550040
bool Contest::is_leading_scorer(int judge)
{
    // The judges by score, highest first (a selection sort; equal scores keep their order).
    std::array<int, kJudges> order = {{0, 1, 2, 3, 4}};
    for (int i = 0; i < kJudges; ++i) {
        for (int j = i; j < kJudges; ++j) {
            if (core::at(judge_scores_, core::at(order, i)) < core::at(judge_scores_, core::at(order, j))) {
                const int swap = core::at(order, i);
                core::at(order, i) = core::at(order, j);
                core::at(order, j) = swap;
            }
        }
    }
    return core::at(order, 0) == judge || core::at(order, 1) == judge || core::at(order, 2) == judge;
}

// 0x005501A0
bool Contest::place_is(int place) { return place == standing_; }

// 0x005501C0
bool Contest::round_is(int round) { return round == runs_ + 1; }

// 0x005501E0
void Contest::get_end() { ended_ = true; }

// 0x005501F0
bool Contest::is_ended() { return ended_; }

// 0x00550200
void Contest::sort_leaders()
{
    const int count = (ended_ ? 2 : runs_) + 1;
    for (int i = 0; i < kLeaders - 1; ++i) {
        int best = i;
        for (int j = i + 1; j < kLeaders; ++j) {
            const int score = core::at(leaders_, j).best_two(count);
            const int best_score = core::at(leaders_, best).best_two(count);
            if (score > best_score || (core::at(leaders_, j).ident_ == 0 && score == best_score))
                best = j;
        }
        if (best != i) {
            // (a leader's copy: the name, the id, the scores and unknown1c_ - not the padding)
            Leader& a = core::at(leaders_, best);
            Leader& b = core::at(leaders_, i);
            const text::Chars name(a.label_);
            const int id = a.ident_;
            const std::array<int, kRuns> scores = {{core::at(a.scores_, 0), core::at(a.scores_, 1), core::at(a.scores_, 2)}};
            const bool unknown = a.unknown1c_;
            a.label_ = b.label_;
            a.ident_ = b.ident_;
            for (int k = 0; k < kRuns; ++k)
                core::at(a.scores_, k) = core::at(b.scores_, k);
            a.unknown1c_ = b.unknown1c_;
            b.label_ = name;
            b.ident_ = id;
            for (int k = 0; k < kRuns; ++k)
                core::at(b.scores_, k) = core::at(scores, k);
            b.unknown1c_ = unknown;
        }
    }
}

// 0x005504B0
// (the leaders made first by their constructors - the compiler's array-constructor helper, 0x0062388B; destroyed by
// the implicit destructor, 0x0054FFF0, through its array-destructor helper 0x0062382B: last to first)
Contest::Contest() : args_(spawn_param_table()) {}

// 0x00550560
void Contest::kickoff(float bronze, float silver, float gold, float bronze_score, float silver_score, float gold_score,
                      float bail_penalty)
{
    third_mark_ = bronze;
    second_mark_ = silver;
    top_mark_ = gold;
    bronze_score_ = bronze_score;
    silver_score_ = silver_score;
    gold_score_ = gold_score;
    bail_penalty_ = bail_penalty;
    ended_ = false;

    objects::ProfileRoster* manager = roster_handle();
    core::at(leaders_, 0).label_ = manager->fetch_active_account()->fetch_shown_name();

    // The pros that can compete: not hidden (0xF3149AC5) and not the player, in a random order.
    std::array<int, 96> pros{}; // (the original's stack room)
    int pro_count = 0;
    const int templates = static_cast<int>(manager->templates_.num_);
    for (int i = 0; i < templates; ++i) {
        objects::RiderIdentity* profile = template_at(manager, i);
        if (profile->is_pro() && !profile->has_flag_f_3149a_c5() &&
            checksums::hash_from_text(profile->fetch_shown_name()) !=
                checksums::hash_from_text(core::at(leaders_, 0).label_.c_str()))
            core::at(pros, pro_count++) = i;
    }
    for (int i = 0; i < pro_count; ++i) {
        const int other = maths::roll_side_stream(pro_count);
        const int swap = core::at(pros, i);
        core::at(pros, i) = core::at(pros, other);
        core::at(pros, other) = swap;
    }
    std::array<int, kLeaders - 1> ids = {{1, 2, 3, 4, 5, 6, 7}};
    for (int& id : ids) {
        const int other = maths::roll_side_stream(kLeaders - 1);
        const int swap = id;
        id = core::at(ids, other);
        core::at(ids, other) = swap;
    }

    core::at(leaders_, 0).ident_ = 0;
    for (int& score : core::at(leaders_, 0).scores_)
        score = 0;
    bronze = static_cast<float>(maths::plus_or_minus_exact(two()) + bronze);
    silver = static_cast<float>(maths::plus_or_minus_exact(two()) + silver);
    gold = static_cast<float>(maths::plus_or_minus_exact(two()) + gold);
    if (!(silver < gold))
        silver = static_cast<float>(static_cast<double>(gold) - tenth());
    if (!(bronze < silver))
        bronze = static_cast<float>(static_cast<double>(silver) - tenth());

    for (int i = 0; i < kLeaders - 1; ++i) {
        Leader& leader = core::at(leaders_, i + 1);
        leader.ident_ = core::at(ids, i);
        script::Array* names = nullptr;
        const char* name = nullptr;
        if (args_->fetch_list(kLeaderBoardNames, &names, 0))
            name = names->find_inline_text(static_cast<uint32_t>(i));
        else
            name = manager->get_template(manager->get_template_name(core::at(pros, i)))->fetch_shown_name();
        leader.label_ = name;

        // The first three pros near the medal points, the others near the bronze.
        float points = 0.0f;
        if (i == 0) {
            points = gold;
        } else if (i == 1) {
            points = silver;
        } else if (i == 2) {
            points = bronze;
        } else {
            auto spread = static_cast<float>(static_cast<double>(bronze) * eighth());
            const double random = maths::plus_or_minus_exact(spread);
            points = static_cast<float>(static_cast<double>(bronze) - spread + random);
            if (!(points < bronze))
                points = static_cast<float>(static_cast<double>(bronze) - tenth());
        }
        // Two judges' scores either side of the points, the third a little lower.
        auto high = static_cast<float>(maths::plus_or_minus_exact(two()) + points);
        float low = high;
        auto mirrored = static_cast<float>(static_cast<double>(points) + points - high);
        if (mirrored < high)
            low = mirrored;
        const int first = maths::roll_side_stream(3);
        int second = 0;
        do {
            second = maths::roll_side_stream(3);
        } while (second == first);
        const float base = low - two();
        for (int& score : leader.scores_)
            score = truncate(maths::plus_or_minus_exact(kScoreSpread) + base + nearly_half());
        core::at(leader.scores_, first) = current_round(high);
        core::at(leader.scores_, second) = current_round(mirrored);
    }

    debug::printf("\n\n");
    for (int i = 0; i < kLeaders; ++i) {
        const Leader& leader = core::at(leaders_, i);
        debug::printf("%d: pos %d, name %12s, %3d, %3d, %3d, %3d\n", i, leader.ident_, leader.label_.c_str(),
                      leader.get_word(0), leader.get_word(1), leader.get_word(2), leader.best_two_of_four_words());
    }
    sort_leaders();
    const char* first_place = nullptr;
    if (args_->fetch_text(kFirstPlaceName, &first_place, 0))
        core::at(leaders_, 0).label_ = first_place;
    debug::printf("\nSorted\n");
    for (int i = 0; i < kLeaders; ++i) {
        const Leader& leader = core::at(leaders_, i);
        debug::printf("%d: pos %d, name %12s, %3d, %3d, %3d, %3d\n", i, leader.ident_, leader.label_.c_str(),
                      leader.get_word(0), leader.get_word(1), leader.get_word(2), leader.best_two_of_four_words());
    }
    for (int i = 0; i < kJudges; ++i)
        core::at(judge_names_, i) = script::fetch_text(core::at(kJudgeNames, i));
    runs_ = -1;
}

// 0x00550B50
void Contest::show_leader_board()
{
    if (!competition_goal_active())
        return;
    script::Struct* params = spawn_param_table();
    script::Array* board = alloc_script_array();
    board->configure_size_kind(5, script::kQbvalStruct);
    debug::printf("\nSetupJudgeText\n");
    for (int i = 0; i < kLeaders; ++i) {
        const Leader& leader = core::at(leaders_, i);
        debug::printf("%d: pos %d, name %12s, %3d, %3d, %3d, %3d\n", i, leader.ident_, leader.label_.c_str(),
                      leader.get_word(0), leader.get_word(1), leader.get_word(2), leader.best_two_of_four_words());
    }
    for (int i = 0; i < 5; ++i) {
        const Leader& leader = core::at(leaders_, i);
        script::Struct* entry = spawn_param_table();
        entry->append_word(kName, leader.label_.c_str());
        debug::printf("m_current_round: %i\n", runs_);
        entry->put_int(kScore, leader.best_two(runs_ + 1));
        if (leader.ident_ == 0)
            entry->put_name_key(kNoName, kPlayer);
        board->set_element(static_cast<uint32_t>(i), cell::of(entry));
    }
    params->append_array_value(kRankingList, board);
    free_array(board);
    script::spawn_script(kAddLeaderBoard, params, 0, nullptr, 0, 0, false, false, false, false);
    delete_structure(params);
}

// 0x00550EF0
void Contest::end_run(int score)
{
    if (ended_)
        return;
    debug::printf("Ending Competition with score of %d\n", score);
    debug::printf("Bronze %f, silver %f, gold %f\n", static_cast<double>(bronze_score_),
                  static_cast<double>(silver_score_), static_cast<double>(gold_score_));

    // The points: linear between the medal scores (above gold: towards 100 at twice the gold score).
    const double x = score;
    float points = 0.0f;
    if (x < bronze_score_)
        points = static_cast<float>(static_cast<double>(third_mark_) / bronze_score_ * x);
    else if (x < silver_score_)
        points = static_cast<float>((static_cast<double>(second_mark_) - third_mark_) * (x - bronze_score_) /
                                        (static_cast<double>(silver_score_) - bronze_score_) +
                                    third_mark_);
    else if (x < gold_score_)
        points = static_cast<float>((static_cast<double>(top_mark_) - second_mark_) * (x - silver_score_) /
                                        (static_cast<double>(gold_score_) - silver_score_) +
                                    second_mark_);
    else
        points = static_cast<float>((x - gold_score_) * (static_cast<double>(hundred()) - top_mark_) /
                                        (static_cast<double>(gold_score_) + gold_score_ - gold_score_) +
                                    top_mark_);
    debug::printf("points calculated at %f\n", static_cast<double>(points));
    points = points - crash_penalty_;
    debug::printf("points decremented by %f bail points to %f\n", static_cast<double>(crash_penalty_),
                  static_cast<double>(points));

    const float max = max_points();
    path_vertices_ = points;
    if (!(points <= max))
        path_vertices_ = max;
    auto spread = static_cast<float>((static_cast<double>(max) - path_vertices_) * tenth());
    path_vertices_ = static_cast<float>(maths::plus_or_minus_exact(static_cast<float>(
                                            (static_cast<double>(max) - path_vertices_) * tenth() * get_half())) +
                                        path_vertices_);
    if (!(path_vertices_ <= max))
        path_vertices_ = max;
    if (path_vertices_ < get_zero())
        path_vertices_ = 0.0f;

    // The judges: two near the points, the third making the average.
    float* judges = judge_scores_.data();
    for (int i = 0; i < 2; ++i) {
        judges[i] = static_cast<float>(maths::plus_or_minus_exact(spread) + path_vertices_);
        if (!(judges[i] <= max_judge()))
            judges[i] = max;
        if (judges[i] < get_zero())
            judges[i] = 0.0f;
    }
    judges[2] = static_cast<float>(static_cast<double>(path_vertices_) * three() -
                                   (static_cast<double>(judges[1]) + judges[0]));
    if (judges[2] < get_zero())
        judges[2] = 0.0f;
    if (!(judges[2] <= max_judge()))
        judges[2] = max;
    // Each to a tenth (the third with a nudge up), then to a whole score.
    judges[2] =
        static_cast<float>(truncate(static_cast<double>(judges[2]) * ten()) * static_cast<double>(tenth()) + tiny());
    judges[0] = static_cast<float>(
        truncate(truncate(static_cast<double>(judges[0]) * ten()) * static_cast<double>(tenth()) + over_half()));
    const float first = judges[0];
    const double second =
        truncate(truncate(static_cast<double>(judges[1]) * ten()) * static_cast<double>(tenth()) + over_half());
    judges[1] = static_cast<float>(second);
    judges[2] = static_cast<float>(truncate(static_cast<double>(judges[2]) + nearly_half()));
    const float limit = ninety_nine();
    if (!(first <= limit))
        judges[0] = limit;
    if (!(second <= limit))
        judges[1] = limit;
    if (!(judges[2] <= limit))
        judges[2] = limit;
    path_vertices_ = static_cast<float>(
        truncate((static_cast<double>(judges[1]) + judges[2] + judges[0]) * get_third() + nearly_half()));
    if (!(path_vertices_ <= limit))
        path_vertices_ = limit;
    core::at(run_totals_, runs_) = truncate(path_vertices_);
    debug::printf("3 scores are %f,%f,%f, total is %f\n", static_cast<double>(judges[0]),
                  static_cast<double>(judges[1]), static_cast<double>(judges[2]), static_cast<double>(path_vertices_));

    // The player's score of the run, the standings, the place.
    for (Leader& leader : leaders_) {
        if (leader.ident_ == 0)
            core::at(leader.scores_, runs_) = core::at(run_totals_, runs_);
    }
    sort_leaders();
    for (int i = 0; i < kLeaders; ++i) {
        if (core::at(leaders_, i).ident_ == 0)
            standing_ = i + 1;
    }

    // The two other judges near the lowest of the three.
    float lowest = judges[0];
    if (judges[1] < lowest)
        lowest = judges[1];
    if (judges[2] < lowest)
        lowest = judges[2];
    debug::printf("Lowest = %f\n", static_cast<double>(lowest));
    const float base = lowest - spread;
    for (int i = 3; i < kJudges; ++i) {
        const double judge = maths::plus_or_minus_exact(spread) + base;
        judges[i] = static_cast<float>(judge);
        debug::printf("Score %d is %f\n", i, judge); // (the unrounded value)
    }
    for (int i = 0; i < kJudges; ++i) {
        if (judges[i] < get_zero())
            judges[i] = 0.0f;
    }
    // The first four shuffled.
    for (int i = 0; i < kJudges - 1; ++i) {
        const int other = maths::roll_side_stream(kJudges);
        const float swap = judges[i];
        judges[i] = judges[other];
        judges[other] = swap;
    }

    if (!competition_goal_active())
        return;
    script::Struct* params = spawn_param_table();
    script::Array* scores = alloc_script_array();
    scores->configure_size_kind(5, script::kQbvalStruct);
    debug::printf("\nJudgement Day\n\n");
    for (int i = 0; i < kJudges; ++i) {
        script::Struct* entry = spawn_param_table();
        entry->put_int("score", current_round(judges[i])); // "score"
        if (is_leading_scorer(i))
            entry->put_name_key(kNoName, kTopJudge);
        scores->set_element(static_cast<uint32_t>(i), cell::of(entry));
        debug::printf("%d: %f Top=%d\n", i, static_cast<double>(judges[i]), static_cast<int>(is_leading_scorer(i)));
    }
    params->append_array_value("scores", scores);                  // "scores"
    params->put_int("total_score", current_round(path_vertices_)); // "total_score"
    free_array(scores);
    script::spawn_script("goal_comp_show_run_scores", params, 0, nullptr, 0, 0, false, false, false,
                         false); // goal_comp_show_run_scores
    delete_structure(params);
}

} // namespace modules
