// goals::Objective, the goal base class: 0x00553E00-0x00557ABF (see objective.h).

#include "game/goals/objective.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>

#include "game/goals/goal_calls.h"
#include "game/goals/goal_names.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "engine/gfx/tod_manager.h"

namespace goals {

namespace {

using namespace goal_calls;

using namespace goal_names;

// The x87 code's float to int conversion (_ftol): truncation.
int truncate(double value) { return static_cast<int>(value); }

} // namespace

// 0x00553E00
void Objective::pass_inherited_flags(int depth)
{
    void* manager = objective_board();
    for (const GoalEdge* link = sub_nodes_; link && link->objective_key_; link = link->succ_) {
        Objective* child = get_goal(manager, link->objective_key_, 1);
        if (child) {
            child->inherited_ |= inherited_;
            child->pass_inherited_flags(depth + 1);
        }
    }
}

// 0x00553E50
void Objective::clear_child_wins()
{
    void* manager = objective_board();
    for (GoalEdge* link = sub_nodes_; link && link->objective_key_; link = link->succ_) {
        link->won_ = false;
        Objective* child = get_goal(manager, link->objective_key_, 1);
        if (child)
            child->clear_child_wins();
    }
}

// 0x00553E90
script::Struct* Objective::fetch_args() { return args_.get(); }

// 0x00553EA0
void Objective::reset_timer()
{
    int unlimited = 0;
    args_->fetch_int_value(kUnlimitedTime, &unlimited, 0);
    if (unlimited == 1)
        return;
    seconds_remaining_ = 120;
    int seconds = 0;
    if (args_->fetch_int_value(kTime, &seconds, 0))
        seconds_remaining_ = seconds;
    seconds_remaining_ = seconds_remaining_ * 1000 + 20;
    cur_phase_ &= ~kStateNoTimeLimit;
}

// 0x00553F10
void Objective::set_seconds_remaining(int seconds) { seconds_remaining_ = seconds * 1000; }

// 0x00553F20
int Objective::get_seconds_remaining() { return seconds_remaining_; }

// 0x00553F30
void Objective::advance_clock(int seconds) { seconds_remaining_ += seconds * 1000 + 20; }

// 0x00553F50
void Objective::change_live() { cur_phase_ |= kStateActive; }

// 0x00553F60
void Objective::clear_engaged_bit() { cur_phase_ &= ~kStateActive; }

// 0x00553F70
bool Objective::rewrite_args(script::Struct* params)
{
    args_->append_structure_if_any(params);
    return false;
}

// 0x00553F90
bool Objective::is_operative() { return (cur_phase_ & kStateActive) != 0; }

// 0x00553FA0
bool Objective::has_seen() { return (flag_bits_ & kFlagSeen) != 0; }

// 0x00553FB0
void Objective::get_freeze() { cur_phase_ |= kStatePaused; }

// 0x00553FC0
void Objective::un_pause() { cur_phase_ &= ~kStatePaused; }

// 0x00553FD0
bool Objective::bit_matches(uint32_t flag, int value, uint32_t sub)
{
    script::Struct* flags = goal_flags_;
    if (sub) {
        // (a sub-structure that isn't there: null - the original left the name itself as the pointer; both crash)
        flags = nullptr;
        goal_flags_->read_sub_struct(sub, &flags, 1);
    }
    if (!flags->has_entry_named(flag))
        return false;
    // (the original's local is the sub-structure argument's slot - the structure's address; the flag is there, so
    // the read below always replaces it)
    int set = 0;
    flags->fetch_int_value(flag, &set, 1);
    return value == set;
}

// 0x00554030
void Objective::clear_flag_values(script::Struct* flags)
{
    script::Component* component = flags->step_to_entry(nullptr);
    while (component) {
        script::Component* next = flags->step_to_entry(component);
        // (num_flags isn't stepped over: a structure with it loops here for ever, as in the original)
        if (component->name != kNumFlags) {
            component->value.set_word(0);
            component = next;
        }
    }
}

// 0x00554070
void Objective::zero_objective_bits()
{
    if (goal_flags_->has_bare_flag(kSkaterTraits)) {
        script::Component* component = goal_flags_->step_to_entry(nullptr);
        while (component) {
            script::Component* next = goal_flags_->step_to_entry(component);
            if ((component->type_and_flag_ & 0xFE) == (script::kQbvalStruct << 1)) {
                script::Struct* skater = component->value.structure();
                script::Component* flag = skater->step_to_entry(nullptr);
                while (flag) {
                    script::Component* following = skater->step_to_entry(flag);
                    // (the same endless loop at num_flags as clear_flag_values)
                    if (flag->name != kNumFlags) {
                        flag->value.set_word(0);
                        flag = following;
                    }
                }
            }
            component = next;
        }
    } else {
        clear_flag_values(goal_flags_);
    }
    num_flags_set_ = 0;
    args_->put_int(kNumFlagsSet, 0);
}

// 0x00554110
bool Objective::has_every_bit()
{
    int to_win = num_flags_;
    args_->fetch_int_value(kNumFlagsToWin, &to_win, 0);
    if (goal_flags_->has_bare_flag(kSkaterTraits)) {
        for (script::Component* component = goal_flags_->step_to_entry(nullptr); component;) {
            script::Component* next = goal_flags_->step_to_entry(component);
            if ((component->type_and_flag_ & 0xFE) == (script::kQbvalStruct << 1)) {
                script::Struct* skater = component->value.structure();
                int set = 0;
                int count = 0;
                skater->fetch_int_value(kNumFlagsSet, &set, 1);
                skater->fetch_int_value(kNumFlags, &count, 1);
                if (set < count)
                    return false;
            }
            component = next;
        }
        return true;
    }
    if (!to_win)
        return false;
    return num_flags_set_ >= to_win;
}

// 0x005541E0
bool Objective::bit_is_on(uint32_t flag, uint32_t sub)
{
    script::Struct* flags = goal_flags_;
    if (sub) {
        script::Struct* found = nullptr; // (as bit_matches)
        flags->read_sub_struct(sub, &found, 1);
        flags = found;
    }
    // (an integer that isn't there leaves the goal's address in the original's local: true)
    int set = 1; // (missing: on - the original read an argument's stack slot, an address: never 0)
    flags->fetch_int_value(flag, &set, 1);
    return set != 0;
}

// 0x00554220
bool Objective::is_latched()
{
    void* manager = objective_board();
    return (flag_bits_ & kFlagLocked) || is_group_locked(manager, bundle_, level_);
}

// 0x00554250
bool Objective::is_graffiti() { return args_->has_bare_flag(kGraffiti); }

// 0x00554260
bool Objective::is_special() { return args_->has_bare_flag(kSpecial); }

// 0x00554270
bool Objective::is_tetris() { return args_->has_bare_flag(kTetris); }

// 0x00554280
uint32_t Objective::get_objective_key()
{
    uint32_t id = 0;
    args_->read_hash_value(kGoalIdName, &id, 1);
    return id;
}

// 0x005542B0
bool Objective::is_user_made_objective() { return args_->has_bare_flag(kEditedGoal); }

// 0x00554300
bool Objective::is_edited_goal2() { return args_->has_bare_flag(kEditedGoal); }

// 0x005542C0
bool Objective::has_won_goal_or_child()
{
    if (has_won())
        return true;
    for (const GoalEdge* link = sub_nodes_; link; link = link->succ_) {
        if (link->won_)
            return true;
    }
    return false;
}

// 0x005542F0
bool Objective::is_picked() { return (flag_bits_ & kFlagSelected) != 0; }
// 0x005519F0
bool Objective::is_prepared() { return (flag_bits_ & kFlagInitialized) != 0; }

// 0x00554320
void Objective::picked_up_countable()
{
    int collected = 0;
    args_->fetch_int_value(kNumberCollected, &collected, 0);
    ++collected;
    args_->put_int(kNumberCollected, collected);
    if (args_->has_bare_flag(kUseHudCounter))
        script::launch_script(kGoalUpdateCounter, args_.get(), nullptr, false, nullptr);
}

// 0x00554390
bool Objective::tally_objective_finished()
{
    int collected = 0;
    args_->fetch_int_value(kNumberCollected, &collected, 0);
    int number = 0;
    args_->fetch_int_value(kNumber, &number, 1);
    return collected >= number;
}

// 0x005543E0
bool Objective::state02() { return (cur_phase_ & kState02) != 0; }

// 0x005543F0
bool Objective::has_proset_prefix(const char* prefix)
{
    const char* own = nullptr;
    if (!args_->fetch_text(kProsetPrefix, &own, 0))
        return false;
    return _stricmp(own, prefix) == 0;
}

// 0x00554430
void Objective::mark_viewed() { flag_bits_ |= kFlagSeen; }

// 0x00554440
void Objective::choose_one() { flag_bits_ |= kFlagSelected; }

// 0x00554450
void Objective::unpick() { flag_bits_ &= ~kFlagSelected; }

// 0x00554460
bool Objective::may_try_again() { return args_->has_bare_flag(kCanRetryGoal); }

// 0x00554470
void Objective::reset_current_time() { args_->put_int(kCurrentTime, 0); }

// 0x00554480
void Objective::update_current_time()
{
    if (is_frozen())
        return;
    // (a current_time that isn't there leaves the goal's address in the original's local)
    int time = 0; // (missing: 0 - the original read an argument's stack slot, an address)
    args_->fetch_int_value(kCurrentTime, &time, 0);
    time += truncate(frame_clock::get_slomo_frame_seconds() * gfx::g_thousand); // (1000)
    args_->put_int(kCurrentTime, time);
    auto ms = static_cast<uint32_t>(time);
    args_->put_int(kCurrentTimeSeconds, static_cast<int>(ms / 1000));
    args_->put_int(kCurrentTimeFraction, static_cast<int>(ms / 10 % 100));
}

// 0x00554510
void Objective::set_quick_start() { args_->put_name_key(kNoName, kQuickStart); }

// 0x00554520
void Objective::unset_quick_start() { args_->remove_flag(kQuickStart); }

// 0x00554530
bool Objective::is_pro_challenge() { return args_->has_entry_named(kProGoal); }

// 0x00554540
bool Objective::is_online_objective() { return args_->has_entry_named(kNet); }

// 0x00554550
bool Objective::is_contest_objective() { return args_->has_bare_flag(kCompetition); }

// 0x00554560
bool Objective::is_frozen() { return (cur_phase_ & kStatePaused) != 0; }

// 0x00554570
bool Objective::get_objective_list_caption(const char** text) { return args_->fetch_text(kViewGoalsTextName, text, 0); }

// 0x00554590
bool Objective::end_of_run_started() { return (flag_bits_ & kFlagEndOfRunStarted) != 0; }

// 0x005545A0
bool Objective::has_time_limit()
{
    int unlimited = 0;
    args_->fetch_int_value(kUnlimitedTime, &unlimited, 0);
    if (args_->has_bare_flag(kUnlimitedTime))
        unlimited = 1;
    return unlimited == 0;
}

// 0x005545F0
bool Objective::shows_clock() { return args_->has_entry_named(kComponentA302d7cD); }

// 0x00554610
void Objective::restore_save_block(script::Struct* data)
{
    int beaten = 0;
    data->fetch_int_value(kAlreadyWon, &beaten, 1);
    if (beaten)
        mark_won();
    int locked = 1;
    data->fetch_int_value(kIsLocked, &locked, 1);
    if (!locked)
        flag_bits_ &= ~kFlagLocked;
    int seen = 0;
    data->fetch_int_value(kHasSeen, &seen, 1);
    if (seen)
        flag_bits_ |= kFlagSeen;
    int record = 0;
    data->fetch_int_value(kWinRecord, &record, 1);
    args_->put_int(kWinRecord, record);
    uint32_t type = 0;
    if (args_->read_hash_value(kRecordType, &type, 0)) {
        if (type == kTime)
            best_time_ = record;
        else if (type == kScore)
            record_points_ = record;
    }
    if (data->has_entry_named(kGoalFlags)) {
        goal_flags_->get_clear();
        script::Struct* flags = nullptr;
        data->read_sub_struct(kGoalFlags, &flags, 1);
        goal_flags_->append_structure_if_any(flags);
        num_flags_ = 0;
        num_flags_set_ = 0;
        for (script::Component* component = goal_flags_->step_to_entry(nullptr); component;) {
            script::Component* next = goal_flags_->step_to_entry(component);
            ++num_flags_;
            if (component->value.word())
                ++num_flags_set_;
            component = next;
        }
    }
}

// 0x00554770
void Objective::capture_save_block(script::Struct* data)
{
    data->put_int(kLevelNum, level_);
    void* manager = objective_board();
    const bool locked = (flag_bits_ & kFlagLocked) || is_group_locked(manager, bundle_, level_);
    data->put_int(kIsLocked, locked);
    data->put_int(kHasSeen, (flag_bits_ & kFlagSeen) ? 1 : 0);
    data->put_int(kAlreadyWon, has_won());
    int record = 0;
    uint32_t type = 0;
    if (args_->read_hash_value(kRecordType, &type, 0)) {
        if (type == kTime)
            record = best_time_;
        else if (type == kScore)
            record = record_points_;
    }
    data->put_int(kWinRecord, record);
    data->put_name_key(kNoName, args_->has_entry_named(kClassicModeGoal) ? kClassic : kCareer);
    if (args_->has_bare_flag(kSaveGoalFlags) && !goal_flags_->is_vacant())
        data->put_sub_struct(kGoalFlags, goal_flags_);
}

// 0x00554890
void Objective::replace_trick_tokens(const char* text, std::span<char> out_run, script::Array* key_combos,
                                     script::Array* tricks)
{
    core::Cursor<char> out(out_run);
    core::CStringCursor at(text);
    if (!*at) {
        *out = 0;
        return;
    }
    while (*at) {
        const char c = *at;
        script::Array* source = nullptr;
        if (c == '\\' && at[1] == 't' && at[2] <= '9' && at[2] >= '0')
            source = tricks;
        else if (c == '\\' && at[1] == 'k' && at[2] <= '9' && at[2] >= '0')
            source = key_combos;
        if (!source) {
            *out++ = c;
            ++at;
            continue;
        }
        int number = at[2] - '0';
        if (at[3] <= '9' && at[3] >= '0') {
            number = number * 10 + (at[3] - '0');
            ++at;
        }
        const char* replacement = source->find_inline_text(static_cast<uint32_t>(number - 1));
        const std::span<char> room = out.rest();
        std::snprintf(room.data(), room.size(), replacement); // (the original: sprintf, unbounded, the text as the format)
        const int length = static_cast<int>(std::strlen(replacement));
        if (length > 0)
            out += length;
        at += 3;
    }
    *out = 0;
}

// 0x005549A0
int Objective::count_picked_up()
{
    int collected = 0;
    if (!args_->fetch_int_value(kNumberCollected, &collected, 0))
        collected = num_flags_set_;
    return collected;
}

// 0x005549D0
int Objective::get_num_flags_to_win()
{
    int to_win = num_flags_;
    args_->fetch_int_value(kNumFlagsToWin, &to_win, 0);
    return to_win ? to_win : -1;
}

// 0x00554A00
int Objective::get_times_started() { return times_started_; }

// 0x00554A10
void Objective::choose_run_ending(int type) { run_ending_ = type; }

// 0x00554A20
void Objective::change_ends_on_timeout(bool deactivate)
{
    flag_bits_ =
        static_cast<uint8_t>((flag_bits_ & ~kFlagDeactivateOnExpire) | ((deactivate << 6) & kFlagDeactivateOnExpire));
}

// 0x00554A40
bool Objective::is_horse() { return args_->has_bare_flag(kHorse); }

// 0x00554A50
bool Objective::is_find_gaps() { return args_->has_bare_flag(kFindGaps); }

// 0x00554A60
bool Objective::has_no_time_limit() { return (cur_phase_ & kStateNoTimeLimit) != 0; }

// 0x00554A70
bool Objective::requires_combo() { return args_->has_bare_flag(kRequireCombo); }

} // namespace goals
