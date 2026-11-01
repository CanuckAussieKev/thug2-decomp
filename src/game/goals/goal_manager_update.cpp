// goals::CGoalManager: its update, the timer on screen, unbeating, the view goals menu's list, the active goals'
// questions (0x0055DD10-0x0055E78D, 0x0055E790-0x0055EB3F) and the commands among them (see goal_manager.h,
// goal_manager_cfuncs.h).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <array>

#include "engine/core/str.h"
#include "frontend/screen_element.h"
#include "frontend/screen_element_manager.h"
#include "frontend/text_element.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_manager_cfuncs.h"
#include "game/goals/goal_names.h"
#include "engine/net/gamenet_manager.h"
#include "engine/script/cscript.h"
#include "engine/script/game_cfuncs.h"
#include "engine/script/param_struct.h"
#include "game/skate/trick_goals.h"
#include "engine/core/arena_alloc.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"

namespace goals {

namespace {

using namespace goal_names;

constexpr uint32_t kTheTime = 0xA6343CD4;        // the_time
constexpr uint32_t kShowGoalPoints = 0x888EF05E; // GoalManager_ShowGoalPoints
constexpr uint32_t kViewGoalsOrder = 0x09B048FC; // (name unknown: the goal's place in the view goals list, from 1)
constexpr uint32_t kText = 0xC4745838;           // text
constexpr uint32_t kRecordText = 0xBB48AF8B;     // (name unknown: the record's text)
constexpr uint32_t kNoRecord = 0x0C80E196;       // (name unknown: the unnamed checksum without a record type)
constexpr uint32_t kViewGoalsMenuAddItem = 0x380AEA65; // view_goals_menu_add_item
constexpr uint32_t kCountAll = 0xA21884B5;             // count_all
constexpr uint32_t kTrickBeat = 0xD0DD047B;            // TrickBeat

online::Hub* net_session_slot() { return online::g_online_hub; }


// The active list's nodes, counted (as get_engaged_objective walks them).
int count_active(memory::ListNode* head)
{
    const memory::ListNode* node = head->next;
    if (node->data == memory::ListNode::head_marker() || !node)
        return 0;
    int count = 0;
    do {
        node = node->next;
        ++count;
    } while (node->data != memory::ListNode::head_marker() && node);
    return count;
}

} // namespace

// 0x0055DD10
void GoalManager::update_time_display()
{
    std::array<char, 64> text{};
    core::at(text, 0) = 0;
    const int count = count_active(&active_goals_.list_link_);
    for (int i = 0; i < count; ++i) {
        Objective* goal = get_engaged_objective(i);
        if (!goal->shows_clock())
            continue;
        const int time = goal->get_seconds_remaining();
        int minutes = 0, seconds = 0;
        if (time > 0) {
            minutes = time / 60000;
            seconds = (time - minutes * 60000) / 1000;
        }
        std::snprintf(text.data(), sizeof(text), "%2i:%.2i", minutes, seconds); // "%2i:%.2i"
        break;
    }
    frontend::ElementPtr element;
    frontend::UiTree::sole()->fetch_part(&element, kTheTime, 0);
    if (element)
        core::down_cast<frontend::LabelWidget>(element.owner_object_)->change_caption(text.data());
}

// 0x0055DEA0
bool GoalManager::has_active_gap_goal()
{
    const int count = count_active(&active_goals_.list_link_);
    for (int i = 0; i < count; ++i) {
        Objective* goal = get_engaged_objective(i);
        if (!goal->is_operative())
            continue;
        script::Array* gaps = nullptr;
        if (goal->fetch_args()->fetch_list(kNeededGapList, &gaps, 0))
            return true;
    }
    return false;
}

// 0x0055DF90
void GoalManager::revoke_every_clear()
{
    online::Hub* net = net_session_slot();
    for (int i = 0; i < goals_.num_; ++i) {
        Objective* goal = goals_.get_by_index(i);
        if (goal->un_beat() && !net->in_net_game()) {
            --goals_beaten_;
            --objective_score_;
            --total_goal_points_;
        }
    }
    if (!net->in_net_game())
        goal_data_->get_clear();
    if (count_live_goals(false) == 0 && !static_cast<uint8_t>(commands::cfunc_in_multiplayer_game(nullptr, nullptr)))
        script::launch_script(kShowGoalPoints, nullptr, nullptr, false, nullptr);
}

// 0x0055E050
void GoalManager::append_objective_list_entries()
{
    const int count = goals_.num_; // (read once)
    script::Array* ordered = script::new_array();
    script::Array* unordered = script::new_array();
    ordered->configure_size_kind(static_cast<uint32_t>(count), script::kQbvalName);
    unordered->configure_size_kind(static_cast<uint32_t>(count), script::kQbvalName);
    // The goals with a place in the list first, then the others.
    int num_ordered = 0, num_unordered = 0;
    for (int i = 0; i < count; ++i) {
        uint32_t key = 0;
        Objective* goal = goals_.get_by_index(i, &key);
        if (goal->fetch_args()->has_entry_named(kViewGoalsOrder))
            ordered->set_element(static_cast<uint32_t>(num_ordered++), goal->get_objective_key());
        else
            unordered->set_element(static_cast<uint32_t>(num_unordered++), goal->get_objective_key());
    }
    // Sorted by their places: for each slot the goal whose place is the slot's number (a goal not in the table is
    // read through null, as the original).
    for (int slot = 0; slot < num_ordered; ++slot) {
        for (int k = slot; k < num_ordered; ++k) {
            const uint32_t id = ordered->fetch_part(static_cast<uint32_t>(k));
            int place = 0;
            goals_.get_pull(id)->fetch_args()->fetch_int_value(kViewGoalsOrder, &place, 1);
            if (place - 1 != slot)
                continue;
            if (k != slot) {
                const uint32_t other = ordered->fetch_part(static_cast<uint32_t>(slot));
                int unused = 0;
                goals_.get_pull(other)->fetch_args()->fetch_int_value(kViewGoalsOrder, &unused, 1);
                ordered->set_element(static_cast<uint32_t>(slot), id);
                ordered->set_element(static_cast<uint32_t>(k), other);
            }
            break;
        }
    }
    for (int i = 0; i < num_unordered; ++i)
        ordered->set_element(static_cast<uint32_t>(num_ordered + i), unordered->fetch_part(static_cast<uint32_t>(i)));
    // A menu item for each goal with view_goals_text (not pro goals until the cheat, not network goals): its id,
    // its text, its record if won.
    for (int i = 0; i < count; ++i) {
        Objective* goal = goals_.get_pull(ordered->fetch_part(static_cast<uint32_t>(i)));
        script::Struct* params = goal->fetch_args();
        const char* text = nullptr;
        goal->get_objective_list_caption(&text);
        if (!text)
            continue;
        if (!all_unlocked_ && params->has_bare_flag(kProGoal))
            continue;
        if (goal->is_online_objective())
            continue;
        script::Struct* item = script::new_struct();
        item->put_name_key(kGoalIdChecksum, goal->get_objective_key());
        item->append_word(kText, text);
        if (goal->has_won()) {
            std::array<char, 128> record{};
            int value = 0;
            if (goal->is_contest_objective()) {
                params->fetch_int_value(kWinRecord, &value, 0);
                std::snprintf(record.data(), sizeof(record), "%i", value); // "%i"
                const char* suffix = value == 1   ? "st place"      // "st place"
                                     : value == 2 ? "nd place"      // "nd place"
                                     : value == 3 ? "rd place"      // "rd place"
                                                  : "";
                std::size_t length = std::strlen(record.data());
                std::snprintf(record.data() + length, sizeof(record) - length, "%s", suffix);
                item->append_word(kRecordText, record.data());
            } else {
                uint32_t type = 0;
                if (params->read_hash_value(kRecordType, &type, 0)) {
                    if (type == kScore) {
                        params->fetch_int_value(kWinRecord, &value, 0);
                        std::snprintf(record.data(), sizeof(record), "%s", text::format_with_separators(value)); // "%s"
                        item->append_word(kRecordText, record.data());
                    } else if (type == kTime) {
                        int ms = 0;
                        params->fetch_int_value(kWinRecord, &ms, 0);
                        const int total_seconds = ms / 1000;
                        const int minutes = total_seconds / 60;
                        std::snprintf(record.data(), sizeof(record), "%i:%02i.%02i", minutes, total_seconds - minutes * 60,
                                      (ms % 1000) / 10); // "%i:%02i.%02i"
                        item->append_word(kRecordText, record.data());
                    }
                } else {
                    item->put_name_key(0u, kNoRecord);
                }
            }
        }
        script::launch_script(kViewGoalsMenuAddItem, item, nullptr, false, nullptr);
        script::free_struct(item);
    }
    script::release_array_storage(ordered);
    script::release_array_storage(unordered);
    for (script::Array* array : {ordered, unordered}) {
        if (array) {
            array->get_clear();
            script::delete_array(array);
        }
    }
}

// 0x0055E790
void GoalManager::trick_landed(script::Struct* trick)
{
    const int count = count_active(&active_goals_.list_link_);
    for (int i = 0; i < count; ++i) {
        Objective* goal = get_engaged_objective(i);
        if (goal->fetch_args()->has_bare_flag(kTrickBeat))
            core::down_cast<TrickBeatGoal>(goal)->trick_landed(trick);
    }
}

// 0x0055E980
Objective* GoalManager::get_active_net_goal()
{
    const int count = count_active(&active_goals_.list_link_);
    for (int i = 0; i < count; ++i) {
        Objective* goal = get_engaged_objective(i);
        if (goal->is_online_objective())
            return goal;
    }
    return nullptr;
}

// 0x0055EA40
void GoalManager::tick_frame()
{
    for (int i = 0; i < goals_.num_; ++i) {
        Objective* goal = goals_.get_by_index(i);
        if (goal->is_operative())
            goal->tick_frame();
        if (goal->state02()) {
            goals_.drop_out(goal->get_objective_key());
            delete goal;
        }
    }
    update_time_display();
}

// 0x0055E860: active goals (count_all: network goals too, and those not active).
bool cfunc_goal_manager_has_active_goals(script::Struct* params, script::Script*)
{
    GoalManager* manager = get_objective_board();
    const bool all = params->has_bare_flag(kCountAll);
    return manager->count_live_goals(all) > 0;
}

// 0x0055E8A0
bool cfunc_goal_manager_un_beat_all_goals(script::Struct*, script::Script*)
{
    get_objective_board()->revoke_every_clear();
    return true;
}

// 0x0055E8C0
bool cfunc_goal_manager_add_view_goals_list(script::Struct*, script::Script*)
{
    get_objective_board()->append_objective_list_entries();
    return true;
}

// 0x0055E8E0: an active goal is expired (the count asked again after each).
bool cfunc_goal_manager_goal_should_expire(script::Struct*, script::Script*)
{
    GoalManager* manager = get_objective_board();
    if (!manager)
        return false;
    for (int i = 0; i < manager->count_live_goals(true); ++i) {
        if ((manager->get_engaged_objective(i))->has_run_out())
            return true;
    }
    return false;
}

// 0x0055EAC0
bool cfunc_goal_manager_update_all_goals(script::Struct*, script::Script*)
{
    get_objective_board()->tick_frame();
    return true;
}

// 0x0055EAE0: the active network goal's time left (ms) as the script's time_left.
bool cfunc_goal_manager_get_time_left_in_net_game(script::Struct*, script::Script* script)
{
    if (Objective* goal = get_objective_board()->get_active_net_goal())
        script->args_->put_int("time_left", goal->get_seconds_remaining()); // "time_left"
    return true;
}

// 0x0055EB20: the active network goal has no time limit (true without one).
bool cfunc_goal_manager_get_unlimited_time_in_net_game(script::Struct*, script::Script*)
{
    Objective* goal = get_objective_board()->get_active_net_goal();
    return goal ? goal->has_no_time_limit() : true;
}

} // namespace goals
