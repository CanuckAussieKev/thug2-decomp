// The end of a network game's rankings (FillRankingScreen, 0x00567C70 / 0x00568120): the players (or the teams and
// their players) sorted by score into the player_list_menu / rankings_list_menu, a line each (player_menu_add_item /
// score_menu_add_item; the score line's ids from 0x00039447 on).

#include "game/skate/skate_module.h"

#include "engine/core/str.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <array>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "game/goals/goal_manager.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/player_info.h"
#include "engine/net/score_list.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/skate/game_mode.h"
#include "game/rider/rider.h"
#include "engine/core/ints.h"
#include "engine/core/arena_alloc.h"
#include "engine/core/raw_view.h"

namespace modules {

namespace {

constexpr uint32_t kShouldResetScores = 0x0B803C5E; // should_reset_scores
constexpr uint32_t kText = 0xC4745838;              // text
constexpr uint32_t kParent = 0xC2719FB0;            // parent
constexpr uint32_t kPlayerListMenu = 0x67382FBC;    // player_list_menu
constexpr uint32_t kPlayerMenuAddItem = 0x17AD3E33; // player_menu_add_item
constexpr uint32_t kId = 0x40C698AF;                // id
constexpr uint32_t kFont = 0x2F6BF72D;              // font
constexpr uint32_t kDialog = 0xBA9E279D;            // dialog
constexpr uint32_t kRankingsListMenu = 0x5E5BF0E6;  // rankings_list_menu
constexpr uint32_t kScoreMenuAddItem = 0x2C638965;  // score_menu_add_item
constexpr uint32_t kTeamScore = 0x0A6081EE;         // team_score
constexpr uint32_t kElimiskate = 0xF6198005;        // elimiskate
constexpr uint32_t kNetElimiskate = 0x10F84276;     // netelimiskate
constexpr uint32_t kNetGoalAttack = 0xEC200EAA;     // netgoalattack
constexpr uint32_t kNetKing = 0x6EF8FDA0;           // netking
constexpr uint32_t kKing = 0x5D32129C;              // king
constexpr uint32_t kFirstLine = 0x00039447;         // the first score line's id

goals::MatchRules* get_game_mode() { return goals::g_game_mode.get(); }
online::Hub* net_session_slot() { return online::g_online_hub; }
bool is_crown_rule_active()
{
    return get_game_mode()->get_name_key() == kNetKing || get_game_mode()->get_name_key() == kKing;
}

using PlayerSearch = online::PlayerSearch;
online::PlayerRecord* as_player(void* player) { return static_cast<online::PlayerRecord*>(player); }

// The ranking's list (lists::Foremost<ScoreEntry>): highest priority (score) first.
struct RankingList {
    RankingList(const RankingList&) = delete;
    RankingList& operator=(const RankingList&) = delete;
    memory::ListNode head_{};
    RankingList() { head_.make_head(); }
    ~RankingList()
    {
        head_.prev->next = head_.next; // (the head out of the list: as its destructor)
        head_.next->prev = head_.prev;
    }
    // The entry after `node` (null at the head).
    static online::ScoreEntry* get_after(memory::ListNode* node)
    {
        const memory::ListNode* next = node->next;
        return next->data == memory::ListNode::head_marker() ? nullptr : static_cast<online::ScoreEntry*>(next->data);
    }
};

online::ScoreEntry* new_entry()
{
    auto* entry = static_cast<online::ScoreEntry*>(memory::alloc_zeroed(sizeof(online::ScoreEntry)));
    if (!entry)
        return nullptr;
    entry->make_node(entry);
    return entry;
}
void delete_entry(online::ScoreEntry* entry) { entry->teardown(1); }


// A player's (or team's) line in the player list.
void add_player_line(const char* text, bool team_score)
{
    const script::StructPtr params = script::make_struct();
    params->append_word(kText, text);
    if (team_score)
        params->put_name_key(0u, kTeamScore);
    params->put_name_key(kParent, kPlayerListMenu);
    script::launch_script(kPlayerMenuAddItem, params.get(), nullptr, false, nullptr);
}

// Its score's line in the rankings list (a team's players': the font before the id) - the text written by `format`
// once the line's structure is made, as the original.
template <typename Format>
void add_score_line(Format format, uint32_t id, bool team_score, bool font_first = false)
{
    const script::StructPtr params = script::make_struct();
    std::array<char, 0x40> text{};
    format(text.data(), sizeof(text));
    params->append_word(kText, text.data());
    if (font_first)
        params->put_name_key(kFont, kDialog);
    params->put_name_key(kId, id);
    if (!font_first)
        params->put_name_key(kFont, kDialog);
    if (team_score)
        params->put_name_key(0u, kTeamScore);
    params->put_name_key(kParent, kRankingsListMenu);
    script::launch_script(kScoreMenuAddItem, params.get(), nullptr, false, nullptr);
}

// A score as shown: a time (minutes:seconds) in king of the hill, else the number.
void format_score(char* out, size_t size, uint32_t score, int colour, bool coloured)
{
    if (is_crown_rule_active()) {
        const uint32_t seconds = score / 1000;
        if (coloured)
            std::snprintf(out, size, "\\c%d%.2d:%.2d", colour, seconds / 60, seconds % 60); // "\c%d%.2d:%.2d"
        else
            std::snprintf(out, size, "%.2d:%.2d", seconds / 60, seconds % 60); // "%.2d:%.2d"
    } else {
        if (coloured)
            std::snprintf(out, size, "\\c%d%d", colour, score); // "\c%d%d"
        else
            std::snprintf(out, size, "%d", score); // "%d"
    }
}

} // namespace

// 0x00567C70: elimiskate's rankings - by the time each player was out (the last out first; still in: 100000).
void fill_elimination_ranking()
{
    online::Hub* net = net_session_slot();
    PlayerSearch search;
    RankingList list;
    memory::enter_heap_scope(memory::Hub::get_pull()->front_end_heap_);
    const bool reset_scores = get_game_mode()->is_true(kShouldResetScores);
    for (void* player = net->begin_player_walk(&search, false); player;
         player = net->step_player_walk(&search, false)) {
        online::PlayerRecord* info = as_player(player);
        online::ScoreEntry* entry = new_entry();
        text::copy_unbounded(entry->text_.data(), info->rider()->fetch_shown_name());
        entry->team_score_ =
            reset_scores ? info->get_accumulated_score() : net->get_entrant_points(info->skater_->ident_);
        entry->shade_ = static_cast<uint8_t>(get_game_mode()->is_squad_match()) ? info->team_ + 2 : 0;
        entry->priority = info->eliminated_time_ ? static_cast<int>(info->eliminated_time_) : 100000;
        lists::append_vertex(list.head_, entry); // (inlined there)
    }
    uint32_t id = kFirstLine;
    for (online::ScoreEntry* entry = RankingList::get_after(&list.head_); entry; ++id) {
        online::ScoreEntry* next = RankingList::get_after(entry);
        std::array<char, 0x40> text{};
        std::snprintf(text.data(), sizeof(text), "\\c%d%s", entry->shade_, entry->text_.data()); // "\c%d%s"
        add_player_line(text.data(), false);
        add_score_line(
            [&](char* score, size_t size) {
                std::snprintf(score, size, "%d", entry->team_score_); // "%d"
            },
            id, false);
        delete_entry(entry);
        entry = next;
    }
    memory::leave_heap_scope();
}

// 0x00568120
bool Skate::cfunc_fill_ranking_screen(script::Struct*, script::Script*)
{
    online::Hub* net = net_session_slot();
    PlayerSearch search;
    RankingList list;
    if (get_game_mode()->get_name_key() == kElimiskate || get_game_mode()->get_name_key() == kNetElimiskate) {
        fill_elimination_ranking();
        return true;
    }
    memory::enter_heap_scope(memory::Hub::get_pull()->front_end_heap_);
    bool goal_attack = get_game_mode()->get_name_key() == kNetGoalAttack;
    // A player's score: the goals beaten in goal attack.
    auto score_of = [&](online::PlayerRecord* info) -> uint32_t {
        if (goal_attack)
            return static_cast<uint32_t>(
                goals::get_objective_board()->count_cleared_by_player(static_cast<int>(info->skater_->ident_)));
        return static_cast<uint32_t>(net->get_entrant_points(info->skater_->ident_));
    };
    if (static_cast<uint8_t>(get_game_mode()->is_squad_match())) {
        // The teams with players, by their scores; under each its players, then its total.
        for (int team = 0; team < get_game_mode()->current_num_teams(); ++team) {
            if (!net->count_team_players(team))
                continue;
            online::ScoreEntry* entry = new_entry();
            entry->king_ = false;
            entry->shade_ = team + 2;
            entry->unknown44_ = static_cast<uint32_t>(team);
            entry->team_score_ = net->get_squad_points(team);
            std::array<char, 0x40> name{};
            std::snprintf(name.data(), sizeof(name), "team_%d_name", team + 1); // "team_%d_name"
            const char* total = script::fetch_text("total_str");         // "total_str"
            const char* team_name = script::fetch_text(name.data());
            std::snprintf(entry->text_.data(), sizeof(online::ScoreEntry) - offsetof(online::ScoreEntry, text_), "\\c%d%s %s",
                          entry->shade_, team_name, total); // "\c%d%s %s" (unbounded there)
            entry->priority = net->get_squad_points(team);
            lists::append_vertex(list.head_, entry);
        }
        uint32_t line = 0;
        for (online::ScoreEntry* entry = RankingList::get_after(&list.head_); entry;) {
            online::ScoreEntry* next = RankingList::get_after(entry);
            for (void* player = net->begin_player_walk(&search, false); player;
                 player = net->step_player_walk(&search, false)) {
                online::PlayerRecord* info = as_player(player);
                if (ints::as_unsigned(info->team_) != entry->unknown44_)
                    continue;
                uint32_t score = score_of(info);
                std::array<char, 0x40> text{};
                std::snprintf(
                    text.data(), sizeof(text),
                    (static_cast<uint8_t>(info->is_own_player()) ? "\\c%d> %s" : "\\c%d%s"), // "\c%d> %s" / "\c%d%s"
                    entry->shade_, info->label_.data());
                add_player_line(text.data(), false);
                add_score_line([&](char* shown, size_t size) { format_score(shown, size, score, 0, false); },
                               kFirstLine + line, false, true);
                ++line;
            }
            add_player_line(entry->text_.data(), true);
            add_score_line(
                [&](char* total, size_t size) {
                    format_score(total, size, static_cast<uint32_t>(entry->priority), entry->shade_, true);
                },
                kFirstLine + line, true);
            delete_entry(entry);
            ++line;
            entry = next;
        }
    } else {
        for (void* player = net->begin_player_walk(&search, false); player;
             player = net->step_player_walk(&search, false)) {
            online::PlayerRecord* info = as_player(player);
            const uint32_t score = score_of(info);
            online::ScoreEntry* entry = new_entry();
            text::copy_unbounded(entry->text_.data(), info->rider()->fetch_shown_name());
            entry->priority = static_cast<int>(score);
            lists::append_vertex(list.head_, entry);
        }
        uint32_t id = kFirstLine;
        for (online::ScoreEntry* entry = RankingList::get_after(&list.head_); entry; ++id) {
            online::ScoreEntry* next = RankingList::get_after(entry);
            add_player_line(entry->text_.data(), false);
            add_score_line(
                [&](char* shown, size_t size) {
                    format_score(shown, size, static_cast<uint32_t>(entry->priority), 0, false);
                },
                id, false);
            delete_entry(entry);
            entry = next;
        }
    }
    memory::leave_heap_scope();
    return true;
}

} // namespace modules
