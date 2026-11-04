// The goal types of 0x005517C0-0x00552E0F: competition, film, find gaps (see goal_types.h).

#include "game/goals/goal_types.h"

#include <cmath>
#include <cstdint>

#include "base/core/enum_ops.h"
#include "engine/memory/mem.h"
#include "game/goals/goal_calls.h"
#include "game/goals/goal_names.h"
#include "engine/gfx/view_cam.h"
#include "base/maths/vec4.h"
#include "game/online/gamenet_manager.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/goals/objective.h"
#include "game/skate/contest.h"
#include "game/skate/skate_module_calls.h"
#include "game/rider/rider.h"
#include "engine/sys/stopwatch.h"
#include "engine/gfx/model.h"
#include "engine/gfx/engine.h"
#include "game/skate/point_tally.h"
#include "engine/gfx/tod_manager.h"
#include "engine/objects/model_component.h"
#include "base/core/down_cast.h"
#include "game/online/net_game_state.h"
namespace goals {

namespace {
constexpr int kNoRecordPoints = 0x7FFFFFFF;    // no points recorded yet
constexpr int kLastPlace = 999;                // the place a contest starts out with
constexpr int kMillisecondsPerSecond = 1000;
constexpr float kSphereRadius = 6.0F; // the sphere checked against the camera

using namespace goal_calls;
using namespace goal_names;

constexpr uint32_t kJustWonGoal = 0xC309CAD1;         // just_won_goal
constexpr uint32_t kDeactivateScript = 0x01C92CAC;    // deactivate
constexpr uint32_t kSuccessScript = 0x90FF204D;       // success
constexpr uint32_t kTimeOnCamera = 0x9278CA8D;        // time_on_camera
constexpr uint32_t kTotalTimeRequired = 0x184E30FD;   // total_time_required
constexpr uint32_t kCheckpoints = 0xC808F9FB;         // (name unknown: the film checkpoints)
constexpr uint32_t kFilmTarget = 0xD262ED09;          // film_target
constexpr uint32_t kModel = 0x286A8D26;               // model
constexpr uint32_t kMaxDistanceToTarget = 0x856FAB92; // max_distance_to_target
constexpr uint32_t kLastTimeOnCamera = 0xFC7FC309;    // last_time_on_camera
constexpr uint32_t kGapsFound = 0x735AF2DD;           // (name unknown: the gaps found)
constexpr uint32_t kNumGapsFound = 0xF1E59083;        // num_gaps_found
constexpr uint32_t kNumGapsToFind = 0xE75C4BBF;       // num_gaps_to_find
constexpr uint32_t kFindGapsFoundGap = 0x851AA6B8;    // goal_find_gaps_found_gap

const char* const kGotBronze = "gotBronze";        // "gotBronze"
const char* const kGotSilver = "gotSilver";        // "gotSilver"
const char* const kGotGold = "gotGold";            // "gotGold"
const char* const kRewardGiven = "reward_given";   // "reward_given"
const char* const kWinRecordName = "win_record";   // "win_record"
const char* const kRewardParams = "reward_params"; // "reward_params"

int truncate(double value) { return static_cast<int>(value); }

modules::Contest* the_competition() { return modules::Skate::sole()->competition().get(); }

} // namespace

// --- The competition ---------------------------------------------------------------------------------------------

// 0x005517C0
ContestObjective::ContestObjective(script::Struct* params) : Objective(params)
{
    set_record_points(kNoRecordPoints);
    set_run_ending(1);
}

// 0x00551800
ContestObjective::~ContestObjective() = default;

// 0x00551810
void ContestObjective::expire() { invoke_hook_script(kExpireScript); }

// 0x00551820
void ContestObjective::change_live()
{
    is_halted_ = false;
    cur_phase() = (cur_phase() & ~Objective::State::kPaused) | Objective::State::kActive;
}

// 0x00551830
void ContestObjective::clear_engaged_bit()
{
    is_halted_ = false;
    cur_phase() &= ~(Objective::State::kActive | Objective::State::kPaused);
}

// 0x00551840
bool ContestObjective::has_won() { return core::any(flag_bits() & Objective::Flags::kWon); }

// 0x00551850
bool ContestObjective::un_beat()
{
    flag_bits() &= ~Objective::Flags::kWon;
    has_third_place_ = false;
    has_second_place_ = false;
    if (!has_won()) {
        return false;
    }
    has_top_prize_ = false;
    return true;
}

// 0x00551880
void ContestObjective::freeze_contest() { is_halted_ = true; }

// 0x00551890
void ContestObjective::thaw_contest()
{
    is_halted_ = false;
    flag_bits() &= ~Objective::Flags::kEndOfRunStarted;
}

// 0x005518A0
bool ContestObjective::is_frozen() { return core::any(cur_phase() & Objective::State::kPaused) || is_halted_; }

// 0x005518C0
void ContestObjective::restore_save_block(script::Struct* data)
{
    // (the first value's local is the argument's slot: a missing gotBronze leaves the pointer - set)
    int got = 1; // (missing: on - the original read an argument's stack slot, an address: never 0)
    data->fetch_int_value(kGotBronze, &got, 0);
    if (got != 0) {
        has_third_place_ = true;
    }
    data->fetch_int_value(kGotSilver, &got, 0);
    if (got != 0) {
        has_third_place_ = true; // (the original sets gotBronze for each medal)
    }
    data->fetch_int_value(kGotGold, &got, 0);
    if (got != 0) {
        has_third_place_ = true;
    }
    int given = 0; // (the original's local is left as it was when reward_given is missing)
    data->fetch_int_value(kRewardGiven, &given, 0);
    prize_paid_ = given;
    int record = 0;
    if (data->fetch_int_value(kWinRecordName, &record, 0)) {
        set_record_points(record);
    }
    Objective::restore_save_block(data);
}

// 0x00551970
void ContestObjective::capture_save_block(script::Struct* data)
{
    Objective::capture_save_block(data);
    data->put_int(kGotBronze, has_third_place_ ? 1 : 0);
    data->put_name_key(kGotSilver, has_second_place_ ? 1 : 0); // (the silver and gold medals saved as checksums)
    data->put_name_key(kGotGold, has_top_prize_ ? 1 : 0);
    data->put_int(kWinRecordName, record_points());
    data->put_int(kRewardGiven, prize_paid_);
}

// 0x00551A30
void ContestObjective::mark_won()
{
    const modules::Contest* competition = the_competition();
    if (competition->place_is(1)) {
        has_top_prize_ = true;
        has_second_place_ = true;
        has_third_place_ = true;
    } else if (competition->place_is(2)) {
        has_second_place_ = true;
        has_third_place_ = true;
    } else if (competition->place_is(3)) {
        has_third_place_ = true;
    }
    flag_bits() |= Objective::Flags::kWon;
}

// 0x00551A90
bool ContestObjective::get_win()
{
    args()->put_name_key(checksums::Name{kNoName}, kJustWonGoal);
    invoke_hook_script(kDeactivateScript);
    args()->remove_flag(kJustWonGoal);
    clear_engaged_bit();
    if (!online::net_game_state().online_hub()->in_net_game()) {
        const modules::Contest* competition = the_competition();
        int place = kLastPlace;
        if (competition->place_is(1)) {
            place = 1;
        } else if (competition->place_is(2)) {
            place = 2;
        } else if (competition->place_is(3)) {
            place = 3;
        }
        if (place < record_points()) {
            args()->put_int(kWinRecordName, place);
            set_record_points(place);
        }
    }
    script::StructPtr rewards = script::make_struct();
    if (!has_won()) {
        script::StructPtr own = script::make_struct();
        get_prize_struct(own.get());
        rewards->append_structure_if_any(own.get());
        own.reset();
        open_up_prize_objectives();
    } else {
        args()->erase_module(kRewardParams);
    }
    args()->put_sub_struct(kRewardParams, rewards.get());
    invoke_hook_script(kSuccessScript);
    rewards.reset();
    if (!has_won()) {
        mark_won();
    }
    return true;
}

// --- Film --------------------------------------------------------------------------------------------------------

// 0x005527D0
ShootVideoObjective::ShootVideoObjective(script::Struct* params) : Objective(params) {}

// 0x005527F0
ShootVideoObjective::~ShootVideoObjective() = default;

// 0x00552800
bool ShootVideoObjective::activate()
{
    shot_hold_time_ = 0;
    unknown7c_ = 0;
    time_needed_ = 0;
    checkpoints_ = 0;
    recording_shot_ = false;
    args()->put_int(checksums::Name{kTimeOnCamera}, 0);
    int seconds = 0;
    if (args()->fetch_int_value(kTotalTimeRequired, &seconds, 0)) {
        time_needed_ = static_cast<uint32_t>(seconds * kMillisecondsPerSecond);
        int time = 0;
        args()->fetch_int_value(kTime, &time, 1);
        unknown7c_ = unknown7c_ * static_cast<uint32_t>(time) * kMillisecondsPerSecond;
        return Objective::activate();
    }
    args()->fetch_int_value(kCheckpoints, &checkpoints_, 0);
    return Objective::activate();
}

// 0x005528D0
bool ShootVideoObjective::target_in_view()
{
    uint32_t id = 0;
    args()->read_hash_value(kFilmTarget, &id, 1);
    objects::Entity* target = objects::find_object(id);
    if (!target) {
        return false;
    }
    auto* object = core::down_cast<objects::GameEntity>(target);
    void* model = core::down_cast<objects::MeshInstanceModule>(object->fetch_module(kModel))->mesh_instance().get();
    // The model's bounding sphere (0x00492F30) in the world.
    maths::Vec4 sphere;
    const maths::Vec4* bounds = static_cast<render::RenderModel*>(model)->fetch_bound_sphere(&sphere);
    maths::Vec4 centre;
    centre[0] = sphere[0];
    centre[1] = sphere[1];
    centre[2] = sphere[2];
    centre[3] = 1.0F;
    (void)bounds;
    centre.multiply_by(object->basis_frame());
    for (int i = 0; i < 4; ++i) {
        centre[i] = centre[i] + object->at_point()[i];
    }
    // (0x0048C490: the sphere hidden from the camera)
    if (render::engine::sphere_visible_in_one_viewport(&centre, kSphereRadius) != 0) {
        return false;
    }
    auto* skater = core::down_cast<objects::Rider>(skate_calls::get_home_rider());
    maths::Vec4* camera = static_cast<gfx::ViewCam*>(skater->obtain_active_lens())->fetch_location();
    const float dx = (*camera)[0] - object->at_point()[0];
    const float dy = (*camera)[1] - object->at_point()[1];
    const float dz = (*camera)[2] - object->at_point()[2];
    int maximum = 0;
    args()->fetch_int_value(kMaxDistanceToTarget, &maximum, 1);
    const double distance =
        std::sqrt((static_cast<double>(dz) * dz) + (static_cast<double>(dy) * dy) + (static_cast<double>(dx) * dx));
    return static_cast<double>(maximum) > distance;
}

// 0x00552A10
bool ShootVideoObjective::tick_frame()
{
    args()->put_int(checksums::Name{kLastTimeOnCamera}, static_cast<int>(shot_hold_time_));
    if (!is_operative() || is_frozen()) {
        return Objective::tick_frame();
    }
    if (recording_shot_) {
        if (target_in_view()) {
            shot_hold_time_ += truncate(frame_clock::get_slomo_frame_seconds() * gfx::kThousand); // (1000)
            args()->put_int(checksums::Name{kTimeOnCamera}, static_cast<int>(shot_hold_time_));
        }
        if (shot_hold_time_ >= time_needed_) {
            void* manager = objective_board();
            return complete_objective(manager, get_objective_key());
        }
        // (no time left to film enough in)
        if (time_needed_ - shot_hold_time_ > static_cast<uint32_t>(seconds_remaining())) {
            void* manager = objective_board();
            return fail_objective(manager, get_objective_key());
        }
        return Objective::tick_frame();
    }
    if ((checkpoints_ != 0) && checkpoints_passed_ >= checkpoints_) {
        void* manager = objective_board();
        return complete_objective(manager, get_objective_key());
    }
    return Objective::tick_frame();
}

// 0x00552AF0
void ShootVideoObjective::checkpoint()
{
    if (!target_in_view()) {
        return;
    }
    if (++checkpoints_passed_ > checkpoints_) {
        void* manager = objective_board();
        complete_objective(manager, get_objective_key());
    }
}

// --- Find gaps ---------------------------------------------------------------------------------------------------

// 0x00552B30
HuntGapsObjective::HuntGapsObjective(script::Struct* params) : Objective(params) {}

// 0x00552B50
HuntGapsObjective::~HuntGapsObjective() = default;

// 0x00552B90
void HuntGapsObjective::scan_for_gaps()
{
    auto* skater = core::down_cast<objects::Rider>(skate_calls::get_home_rider());
    script::Struct* found = nullptr;
    args()->read_sub_struct(kGapsFound, &found, 1);
    int count = 0;
    args()->fetch_int_value(kNumGapsFound, &count, 1);
    auto* score = skater->get_points_total();
    const int gaps = score->tricks();
    bool found_new = false;
    for (int i = 0; i < gaps; ++i) {
        // (modules::PointTally: 0x00562740 the gap `i` is one of the run's, 0x00561660 its name)
        if (!score->is_gap(i)) {
            continue;
        }
        const uint32_t gap = score->get_trick_name(i);
        if (found->has_entry_named(gap)) {
            continue;
        }
        found->put_int(checksums::Name{gap}, 1);
        ++count;
        found_new = true;
    }
    args()->put_int(checksums::Name{kNumGapsFound}, count);
    args()->put_int(checksums::Name{kNumberCollected}, count);
    if (!found_new) {
        return;
    }
    const script::StructPtr params = script::make_struct();
    params->put_name_key(checksums::Name{kGoalIdChecksum}, get_objective_key());
    script::launch_script(kFindGapsFoundGap, params.get(), nullptr, false, nullptr);
}

// 0x00552D20
bool HuntGapsObjective::activate()
{
    args()->erase_module(kGapsFound);
    script::StructPtr found = script::make_struct();
    args()->put_sub_struct(kGapsFound, found.get());
    found.reset();
    args()->put_int(checksums::Name{kNumGapsFound}, 0);
    args()->put_int(checksums::Name{kNumberCollected}, 0);
    int to_find = 0;
    args()->fetch_int_value(kNumGapsToFind, &to_find, 1); // (read and not used)
    return Objective::activate();
}

} // namespace goals
