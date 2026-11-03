// The network game's score list (see score_list.h) and the manager's score queries.

#include "engine/net/score_list.h"

#include <cstdint>
#include <cstring>
#include <array>

#include "engine/core/mem.h"
#include "frontend/screen_element.h"
#include "engine/lists/chain_start.h"
#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/player_info.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "frontend/screen_element_manager.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/point_tally.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "engine/gfx/face_texture.h"
#include "engine/gfx/rig.h"
#include "game/park_editing/goal_editor.h"
#include "game/park_editing/rail_editor.h"
#include "engine/net/server_list.h"
#include "engine/net/buddy_man.h"
#include "engine/net/stats_man.h"
#include "game/rider/skater_score.h"
#include "platform/compiler.h"
#include "engine/core/arena_alloc.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"

THUG2_MSVC_WARNING(disable : 4996) // (the original's strcpy / strcat, kept)

namespace online {

using namespace detail;

namespace {
constexpr uint32_t kBitMask = 31;
constexpr int kSecondsPerMinute = 60;
constexpr int kSecondsPerHour = 3600;
// A scavenger hunt places this many objects; the entry flags that pick the name's style codes.
constexpr int kHuntObjectCount = 5;
constexpr uint32_t kEntryStyle1 = 2;
constexpr uint32_t kEntryStyle2 = 4;
constexpr uint32_t kEntryStyle3 = 8;

// Names.
constexpr uint32_t kSkaterScore = 0xD262802F;                                         // SkaterScore (the component)
constexpr uint32_t kNetLobby = 0x1C471C60;                                            // netlobby
constexpr uint32_t kNetKing = 0x6EF8FDA0;                                             // netking
constexpr uint32_t kKing = 0x5D32129C;                                                // king
constexpr uint32_t kNetScavengerHunt = 0xFC13B531;                                    // netscavengerhunt
constexpr uint32_t kNetGoalAttack = 0xEC200EAA;                                       // netgoalattack
constexpr uint32_t kNetFirefight = 0xBFF33600;                                        // netfirefight
constexpr uint32_t kScavenger = 0x1F3430CE;                                           // scavenger (the goal)
constexpr uint32_t kClearScores = 0x7084400A;                                         // clear_scores
constexpr uint32_t kUpdateScore = 0xAD022A2C;                                         // update_score
constexpr uint32_t kKingAbbreviation = 0xA1DD0949;                                    // player_str_king_abbreviation
constexpr uint32_t kText = 0xC4745838;                                                // text
constexpr uint32_t kId = 0x40C698AF;                                                  // id
constexpr std::array<uint32_t, 4> kTeamNames = {{0x3224348D, 0xB4B04623, 0x7FEC9586, 0x62E9A53E}};  // team_1_name ... team_4_name
constexpr std::array<uint32_t, 8> kScoreLines = {{0x2B083809, 0xB20169B3, 0xC5065925, 0x5B62CC86,  // net_score_1 ... 4
                                     0x2C65FC10, 0xB56CADAA, 0xC26B9D3C, 0x52D480AD}}; // net_score_5 ... 8

constexpr uint32_t kUpdateInterval = 1000; // ms

uint32_t& score_sync_stamp_slot()
{
    static uint32_t score_refresh_stamp{};
    return score_refresh_stamp;
}

// A player's SkaterScore's score object (no null checks: as the original).
void* get_score_of(PlayerRecord* player)
{
    auto* skater = player->skater();
    return core::down_cast<objects::RiderPointKeeper>(skater->fetch_module(kSkaterScore))->points_held();
}
int points_of(void* score) { return static_cast<modules::PointTally*>(score)->get_total_score(); }


// A player that gets a score line: fully in (or local), not eliminated, and in netfirefight not out of lives.
bool has_score_line(PlayerRecord* p)
{
    if (!(p->flag_bits() & (kPlayerFullyIn | kPlayerLocal)) || (p->flag_bits() & kPlayerEliminated))
        return false;
    if (get_game_type() != kNetFirefight || !p->skater())
        return true;
    objects::ObjectModule* score = p->skater()->fetch_module(kSkaterScore);
    return !score || points_of(core::down_cast<objects::RiderPointKeeper>(score)->points_held()) != 0;
}

// A new line (null if out of memory: then used anyway, as the original).
ScoreEntry* new_entry()
{
    auto* entry = static_cast<ScoreEntry*>(memory::alloc_zeroed(sizeof(ScoreEntry)));
    if (entry)
        entry->make_node(entry);
    return entry;
}

// The lowest team whose flag the player carries (-1: none - a shift by 31 then, as the original's).
uint32_t flag_bit(PlayerRecord* p) { return 1u << (static_cast<uint32_t>(p->get_flag_team()) & kBitMask); }

// At the end of the list.
void get_append(lists::Foremost* head, ScoreEntry* entry)
{
    entry->set_prev(head->list_link().prev());
    entry->set_next(&head->list_link());
    head->list_link().prev()->set_next(entry);
    head->list_link().set_prev(entry);
}

} // namespace

// 0x005435F0
int Hub::get_squad_points(int team)
{
    PlayerSearch search;
    int total = 0;
    const PlayerRecord* local = find_own_player_record(this);
    const bool goal_attack = get_game_type() == kNetGoalAttack;
    for (PlayerRecord* p = start_roster_sweep(this, &search, false); p; p = step_roster_sweep(this, &search, false)) {
        if (!(p->flag_bits() & (kPlayerFullyIn | kPlayerLocal)) || p->team() != team)
            continue;
        if (goal_attack && !(local->flag_bits() & kPlayerObserver))
            return objective_board()->num_goals_net_beaten_by(team);
        total += points_of(get_score_of(p));
    }
    return total;
}

// 0x005436E0
int Hub::get_entrant_points(uint32_t id)
{
    const PlayerRecord* local = find_own_player_record(this);
    if (get_game_type() == kNetGoalAttack && !(local->flag_bits() & kPlayerObserver))
        return objective_board()->count_cleared_by_player(static_cast<int>(id));
    auto* player = find_player_slot_by_obj(static_cast<uint16_t>(id));
    return points_of(get_score_of(player));
}

// 0x00544780
void score_broadcast_tick(tasks::Task<Hub>* task)
{
    const void* elements = frontend::UiTree::sole(); // (the screen element manager: unused by the lookup)
    (void)elements;
    Hub* manager = task->content();
    lists::Foremost list{};
    list.list_link().make_head();
    PlayerSearch search;
    const bool not_lobby = get_game_type() != kNetLobby;
    const bool hill_mode = get_game_type() == kNetKing || get_game_type() == kKing;
    bool scavenger_hunt = false;
    if (get_game_type() == kNetScavengerHunt) {
        goals::Objective* goal = objective_board()->get_goal(kScavenger, 0);
        if (goal && goal->is_operative())
            scavenger_hunt = true; // (the goal is active)
    }
    const uint32_t now = frame_clock::get_time();
    if (now - score_sync_stamp_slot() < kUpdateInterval) {
        list.list_link().prev()->set_next(list.list_link().next()); // (the head's destructor)
        list.list_link().next()->set_prev(list.list_link().prev());
        return;
    }
    score_sync_stamp_slot() = now;
    get_run(kClearScores);

    if (not_lobby && manager->show_team_scores()) {
        // A line per team with players: the team's name, its players' marks.
        std::array<ScoreEntry*, 4> teams{};
        for (int team = 0; team < get_game_mode()->current_num_teams(); ++team) {
            if (manager->count_team_players(team) <= 0)
                continue;
            const uint32_t name = team < 4 ? core::at(kTeamNames, team) : 0;
            ScoreEntry* entry = new_entry();
            core::at(teams, team) = entry;
            std::strcpy(entry->text().data(), script::fetch_text(name));
            entry->set_king(false);
            entry->set_local(false);
            entry->set_has_flag(false);
            entry->flag_bits() = 0;
            entry->set_shade(team + 2);
            entry->set_team_score(manager->get_squad_points(team));
        }
        for (PlayerRecord* p = start_roster_sweep(manager, &search, false); p; p = step_roster_sweep(manager, &search, false)) {
            if (!has_score_line(p))
                continue;
            if (p->flag_bits() & kPlayerLocal) {
                constexpr size_t kMarkedSize = 64;
                std::array<char, kMarkedSize> marked{};
                text::format_to(marked, "> %s", core::at(teams, p->team())->text().data());
                std::strcpy(core::at(teams, p->team())->text().data(), marked.data());
            }
            if (p->flag_bits() & kPlayerKing)
                core::at(teams, p->team())->set_king(true);
            if (p->flag_bits() & kPlayerLocal)
                core::at(teams, p->team())->set_local(true);
            if (p->flag_bits() & kPlayerCarryingFlags) {
                core::at(teams, p->team())->set_has_flag(true);
                core::at(teams, p->team())->flag_bits() |= flag_bit(p);
            }
        }
        for (int team = 0; team < get_game_mode()->current_num_teams(); ++team) {
            if (manager->count_team_players(team) <= 0)
                continue;
            ScoreEntry* entry = core::at(teams, team);
            entry->set_priority(entry->team_score());
            list.append_vertex(entry); // (inlined in the original)
        }
    } else {
        // A line per player.
        for (PlayerRecord* p = start_roster_sweep(manager, &search, false); p; p = step_roster_sweep(manager, &search, false)) {
            if (!has_score_line(p))
                continue;
            ScoreEntry* entry = new_entry();
            if (p->flag_bits() & kPlayerLocal)
                text::format_to(entry->text(), "> %s", p->label().data());
            else
                std::strcpy(entry->text().data(), p->label().data());
            entry->set_king((p->flag_bits() >> kPlayerKingBit) & 1);
            entry->set_local(p->flag_bits() & 1);
            if (is_squad_match())
                entry->set_shade(p->team() + 2);
            else
                entry->set_shade(static_cast<int>(get_skater_id(p->skater())) + 2);
            if (p->flag_bits() & kPlayerCarryingFlags) {
                entry->set_has_flag(true);
                entry->flag_bits() |= flag_bit(p);
            }
            if (!not_lobby) {
                get_append(&list, entry);
            } else {
                entry->set_priority(scavenger_hunt ? kHuntObjectCount - p->num_placed() : manager->get_entrant_points(get_skater_id(p->skater())));
                list.append_vertex(entry);
            }
        }
    }

    // The lines, best first.
    const memory::ListNode* cursor =
        list.list_link().next()->data() == memory::ListNode::head_marker() ? nullptr : list.list_link().next();
    auto* entry = cursor ? static_cast<ScoreEntry*>(cursor->data()) : nullptr;
    for (int line = 0; entry; ++line) {
        constexpr size_t kPrefixSize = 64;
        std::array<char, kPrefixSize> prefix{}; // (16 bytes in the original: a long crown abbreviation overran it)
        core::at(prefix, 0) = '\0';
        if (entry->king()) {
            text::format_data_to(prefix,
                                 script::fetch_text(kKingAbbreviation)); // (the text is the format, as the original)
        } else if (entry->has_flag()) {
            if (entry->flag_bits() & 1)
                std::strcat(prefix.data(), "\\s0");
            if (entry->flag_bits() & kEntryStyle1)
                std::strcat(prefix.data(), "\\s1");
            if (entry->flag_bits() & kEntryStyle2)
                std::strcat(prefix.data(), "\\s2");
            if (entry->flag_bits() & kEntryStyle3)
                std::strcat(prefix.data(), "\\s3");
        } else {
            core::at(prefix, 0) = '\0';
        }
        cursor = cursor->next()->data() == memory::ListNode::head_marker() ? nullptr : cursor->next();
        auto* following = cursor ? static_cast<ScoreEntry*>(cursor->data()) : nullptr;

        constexpr size_t kTextSize = 128;
        std::array<char, kTextSize> text{}; // (64 in the original: the longest king of the hill lines overran it)
        if (!not_lobby) {
            text::format_to(text, "\\c%d%s", entry->shade(), entry->text().data());
        } else if (hill_mode) {
            const int seconds = static_cast<int>(static_cast<uint32_t>(entry->priority()) / 1000);
            if (seconds < kSecondsPerHour)
                text::format_to(text, R"(\c4%s\c%d %s \c%d %d:%.2d)", prefix.data(), entry->shade(), entry->text().data(),
                                entry->shade(), seconds / kSecondsPerMinute, seconds % kSecondsPerMinute);
            else
                text::format_to(text, R"(\c4%s\c%d %s \c%d %d:%.2d:%.2d)", prefix.data(), entry->shade(),
                                entry->text().data(), entry->shade(), seconds / kSecondsPerHour, seconds % kSecondsPerHour / kSecondsPerMinute, seconds % kSecondsPerMinute);
        } else if (scavenger_hunt && !entry->local()) {
            text::format_to(text, "%s\\c%d %s", prefix.data(), entry->shade(), entry->text().data());
        } else {
            text::format_to(text, "%s\\c%d %s \\c%d %d", prefix.data(), entry->shade(), entry->text().data(),
                            entry->shade(), entry->priority());
        }
        const uint32_t id = line < 8 ? core::at(kScoreLines, line) : 0;
        script::StructPtr params = script::make_struct();
        params->append_word(kText, script::kQbvalString, text.data());
        params->add_component_of_type(kId, script::kQbvalName, id);
        frontend::UiWidget* element = nullptr;
        {
            frontend::ElementPtr found;
            frontend::fetch_part(&found, id);
            element = found.get_pull();
        }
        if (element)
            element->change_traits(params.get());
        get_run(kUpdateScore, params.get());
        params.reset();
        entry->teardown(1); // (deleted: out of the list)
        entry = following;
    }
    list.list_link().prev()->set_next(list.list_link().next()); // (the head's destructor)
    list.list_link().next()->set_prev(list.list_link().prev());
}

} // namespace online
