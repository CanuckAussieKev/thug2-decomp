// The end of a network game's rankings (FillRankingScreen, 0x00567C70 / 0x00568120): the players (or the teams and
// their players) sorted by score into the player_list_menu / rankings_list_menu, a line each (player_menu_add_item /
// score_menu_add_item; the score line's ids from 0x00039447 on).

#include "game/skate/skate_module.h"

#include "engine/core/str.h"

#include <cstddef>
#include <cstdint>
#include <array>
#include <span>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/lists/chain_start.h"
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
#include "engine/core/format.h"
#include "engine/net/net_game_state.h"

namespace modules {

namespace {
constexpr std::size_t kScoreTextBytes = 0x40;
constexpr int kSecondsPerMinute = 60;
constexpr int kNotEliminatedPriority = 100000; // the sort priority of a player still in the game

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

bool is_crown_rule_active()
{
    return goals::game_mode()->get_name_key() == kNetKing || goals::game_mode()->get_name_key() == kKing;
}

using PlayerSearch = online::PlayerSearch;
online::PlayerRecord* as_player(void* player) { return static_cast<online::PlayerRecord*>(player); }

// The ranking's list (lists::Foremost<ScoreEntry>): highest priority (score) first.
struct RankingList {
    RankingList(const RankingList&) = delete;
    RankingList& operator=(const RankingList&) = delete;
    // The fields other code reads and sets.
    memory::ListNode& head() { return head_; }
    [[nodiscard]] const memory::ListNode& head() const { return head_; }

private:
    memory::ListNode head_{};

public:
    RankingList() { head_.make_head(); }
    ~RankingList()
    {
        head_.prev()->set_next(head_.next()); // (the head out of the list: as its destructor)
        head_.next()->set_prev(head_.prev());
    }
    // The entry after `node` (null at the head).
    static online::ScoreEntry* get_after(memory::ListNode* node)
    {
        const memory::ListNode* next = node->next();
        return next->data() == memory::ListNode::head_marker() ? nullptr : static_cast<online::ScoreEntry*>(next->data());
    }
};

online::ScoreEntry* new_entry()
{
    auto* entry = static_cast<online::ScoreEntry*>(memory::alloc_zeroed(sizeof(online::ScoreEntry)));
    if (!entry) {
        return nullptr;
    }
    entry->make_node(entry);
    return entry;
}
void delete_entry(online::ScoreEntry* entry) { entry->teardown(1); }

// A player's (or team's) line in the player list.
void add_player_line(const char* text, bool team_score)
{
    const script::StructPtr params = script::make_struct();
    params->append_word(kText, text);
    if (team_score) {
        params->put_name_key(checksums::Name{0U}, kTeamScore);
    }
    params->put_name_key(checksums::Name{kParent}, kPlayerListMenu);
    script::launch_script(kPlayerMenuAddItem, params.get(), nullptr, false, nullptr);
}

// Its score's line in the rankings list (a team's players': the font before the id) - the text written by `format`
// once the line's structure is made, as the original.
template <typename Format>
void add_score_line(Format format, uint32_t id, bool team_score, bool font_first = false)
{
    const script::StructPtr params = script::make_struct();
    std::array<char, kScoreTextBytes> text{};
    format(std::span<char>(text));
    params->append_word(kText, text.data());
    if (font_first) {
        params->put_name_key(checksums::Name{kFont}, kDialog);
    }
    params->put_name_key(checksums::Name{kId}, id);
    if (!font_first) {
        params->put_name_key(checksums::Name{kFont}, kDialog);
    }
    if (team_score) {
        params->put_name_key(checksums::Name{0U}, kTeamScore);
    }
    params->put_name_key(checksums::Name{kParent}, kRankingsListMenu);
    script::launch_script(kScoreMenuAddItem, params.get(), nullptr, false, nullptr);
}

// A score as shown: a time (minutes:seconds) in king of the hill, else the number.
void format_score(std::span<char> out, uint32_t score, int colour, bool coloured)
{
    if (is_crown_rule_active()) {
        const uint32_t seconds = score / 1000;
        if (coloured) {
            text::format_to(out, "\\c%d%.2d:%.2d", colour, seconds / kSecondsPerMinute, seconds % kSecondsPerMinute); // "\c%d%.2d:%.2d"
        } else {
            text::format_to(out, "%.2d:%.2d", seconds / kSecondsPerMinute, seconds % kSecondsPerMinute); // "%.2d:%.2d"
        }
    } else {
        if (coloured) {
            text::format_to(out, "\\c%d%d", colour, score); // "\c%d%d"
        } else {
            text::format_to(out, "%d", score); // "%d"
        }
    }
}

} // namespace

// 0x00567C70: elimiskate's rankings - by the time each player was out (the last out first; still in: 100000).
void fill_elimination_ranking()
{
    online::Hub* net = online::online_hub();
    PlayerSearch search;
    RankingList list;
    memory::enter_heap_scope(memory::Hub::get_pull()->front_end_heap());
    const bool reset_scores = goals::game_mode()->is_true(kShouldResetScores);
    for (void* player = net->begin_player_walk(&search, false); player;
         player = net->step_player_walk(&search, false)) {
        const online::PlayerRecord* info = as_player(player);
        online::ScoreEntry* entry = new_entry();
        text::copy_unbounded(entry->text().data(), info->rider()->fetch_shown_name());
        entry->set_team_score(reset_scores ? info->get_accumulated_score() : net->get_entrant_points(info->skater()->ident()));
        entry->set_shade(static_cast<uint8_t>(goals::game_mode()->is_squad_match()) ? info->team() + 2 : 0);
        entry->set_priority(info->eliminated_time() ? static_cast<int>(info->eliminated_time()) : kNotEliminatedPriority);
        lists::append_vertex(list.head(), entry); // (inlined there)
    }
    uint32_t id = kFirstLine;
    for (online::ScoreEntry* entry = RankingList::get_after(&list.head()); entry; ++id) {
        online::ScoreEntry* next = RankingList::get_after(entry);
        std::array<char, kScoreTextBytes> text{};
        text::format_to(text, "\\c%d%s", entry->shade(), entry->text().data()); // "\c%d%s"
        add_player_line(text.data(), false);
        add_score_line(
            [&](std::span<char> score) {
                text::format_to(score, "%d", entry->team_score()); // "%d"
            },
            id, false);
        delete_entry(entry);
        entry = next;
    }
    memory::leave_heap_scope();
}

// 0x00568120
bool Skate::cfunc_fill_ranking_screen(script::Struct* /*params*/, script::Script* /*script*/)
{
    online::Hub* net = online::online_hub();
    PlayerSearch search;
    RankingList list;
    if (goals::game_mode()->get_name_key() == kElimiskate || goals::game_mode()->get_name_key() == kNetElimiskate) {
        fill_elimination_ranking();
        return true;
    }
    memory::enter_heap_scope(memory::Hub::get_pull()->front_end_heap());
    bool goal_attack = goals::game_mode()->get_name_key() == kNetGoalAttack;
    // A player's score: the goals beaten in goal attack.
    auto score_of = [&](online::PlayerRecord* info) -> uint32_t {
        if (goal_attack) {
            return static_cast<uint32_t>(
                goals::get_objective_board()->count_cleared_by_player(static_cast<int>(info->skater()->ident())));
        }
        return static_cast<uint32_t>(net->get_entrant_points(info->skater()->ident()));
    };
    if (static_cast<uint8_t>(goals::game_mode()->is_squad_match())) {
        // The teams with players, by their scores; under each its players, then its total.
        for (int team = 0; team < goals::game_mode()->current_num_teams(); ++team) {
            if (!net->count_team_players(team)) {
                continue;
            }
            online::ScoreEntry* entry = new_entry();
            entry->set_king(false);
            entry->set_shade(team + 2);
            entry->set_unknown44(static_cast<uint32_t>(team));
            entry->set_team_score(net->get_squad_points(team));
            std::array<char, kScoreTextBytes> name{};
            text::format_to(name, "team_%d_name", team + 1);             // "team_%d_name"
            const char* total = script::fetch_text("total_str");         // "total_str"
            const char* team_name = script::fetch_text(name.data());
            // (unbounded in the original: a text past the name's 32 bytes ran on into the entry's other fields)
            text::format_to(entry->text(), "\\c%d%s %s", entry->shade(), team_name, total); // "\c%d%s %s"
            entry->set_priority(net->get_squad_points(team));
            lists::append_vertex(list.head(), entry);
        }
        uint32_t line = 0;
        for (online::ScoreEntry* entry = RankingList::get_after(&list.head()); entry;) {
            online::ScoreEntry* next = RankingList::get_after(entry);
            for (void* player = net->begin_player_walk(&search, false); player;
                 player = net->step_player_walk(&search, false)) {
                online::PlayerRecord* info = as_player(player);
                if (ints::as_unsigned(info->team()) != entry->unknown44()) {
                    continue;
                }
                uint32_t score = score_of(info);
                std::array<char, kScoreTextBytes> text{};
                // "\c%d> %s" (the own player's) / "\c%d%s"
                text::format_to(text, "\\c%d%s%s", entry->shade(),
                                static_cast<uint8_t>(info->is_own_player()) ? "> " : "", info->label().data());
                add_player_line(text.data(), false);
                add_score_line([&](std::span<char> shown) { format_score(shown, score, 0, false); }, kFirstLine + line,
                               false, true);
                ++line;
            }
            add_player_line(entry->text().data(), true);
            add_score_line(
                [&](std::span<char> total) {
                    format_score(total, static_cast<uint32_t>(entry->priority()), entry->shade(), true);
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
            text::copy_unbounded(entry->text().data(), info->rider()->fetch_shown_name());
            entry->set_priority(static_cast<int>(score));
            lists::append_vertex(list.head(), entry);
        }
        uint32_t id = kFirstLine;
        for (online::ScoreEntry* entry = RankingList::get_after(&list.head()); entry; ++id) {
            online::ScoreEntry* next = RankingList::get_after(entry);
            add_player_line(entry->text().data(), false);
            add_score_line(
                [&](std::span<char> shown) { format_score(shown, static_cast<uint32_t>(entry->priority()), 0, false); },
                id, false);
            delete_entry(entry);
            entry = next;
        }
    }
    memory::leave_heap_scope();
    return true;
}

} // namespace modules
