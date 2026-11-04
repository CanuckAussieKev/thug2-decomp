// The online stats (online::RankingsManager). See stats_man.h.

#include "engine/net/stats_man.h"
#include "platform/gamespy.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <array>
#include <memory>

#include "base/core/checksums.h"
#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "base/core/str.h"
#include "engine/memory/task_list.h"
#include "engine/lists/chain_start.h"
#include "engine/net/buddy_man.h"
#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/online_identity.h"
#include "engine/net/online_text.h"
#include "engine/net/vault_man.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/preferences.h"
#include "engine/script/param_struct.h"
#include "engine/sys/stopwatch.h"
#include "engine/gfx/loading_bar.h"
#include "engine/assets/resource_registry.h"
#include "engine/sys/mlp.h"
#include "base/core/parse_number.h"
#include "engine/memory/arena_alloc.h"
#include "platform/code_address.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include <gsl/pointers>
#include "base/core/format.h"
#include "engine/net/net_game_state.h"
#include "base/core/gathered_call.h"
#include "wire/message_ids.h"

namespace {
constexpr int kStatsLevelCount = 15;
constexpr size_t kGcdBufferBytes = 0x100;
constexpr int kStatsResultLateFailure = 5; // (text unknown) failed like 1-3
constexpr int kLastPersonalStatsIndex = 14;
constexpr int kProgressComplete = 100;
constexpr uint32_t kLoggingInScript = 0x23343580U;  // text unknown
constexpr uint32_t kStatsReadyScript = 0x955801C6U; // text unknown

// Names (the checksums of their text).
constexpr uint32_t kAddStatHeaderMenuItem = 0x1AADEE21U;      // add_stat_header_menu_item
constexpr uint32_t kAddStatPersonalMenuItem = 0xA6111F30U;    // add_stat_personal_menu_item
constexpr uint32_t kAddStatPlayerMenuItem = 0x0AC09E3BU;      // add_stat_player_menu_item
constexpr uint32_t kAddStatScoreMenuItem = 0x19F7BC28U;       // add_stat_score_menu_item
constexpr uint32_t kArray = 0x5EF31148U;                      // Array
constexpr uint32_t kBestCombosArrayAllTime = 0x9FE64312U;     // best_combos_array_all_time
constexpr uint32_t kBestCombosArrayMonthly = 0x67F1AE8AU;     // best_combos_array_monthly
constexpr uint32_t kCategoryAllLevels = 0xC2283319U;          // category_all_levels
constexpr uint32_t kHighScoresArrayAllTime = 0xEF20D3A1U;     // high_scores_array_all_time
constexpr uint32_t kHighScoresArrayMonthly = 0xCB95928FU;     // high_scores_array_monthly
constexpr uint32_t kLEVEL = 0x651533ECU;                      // LEVEL
constexpr uint32_t kNAME = 0xA1DC81F9U;                       // NAME
constexpr uint32_t kPersonalStatsArray = 0xFEF84FFFU;         // personal_stats_array
constexpr uint32_t kRANK = 0x7786171AU;                       // RANK
constexpr uint32_t kRating = 0x2776D9DDU;                     // Rating
constexpr uint32_t kSCORE = 0xCD66C8AEU;                      // SCORE
constexpr uint32_t kServerName = 0x2A5CF019U;                 // server_name
constexpr uint32_t kSkaterScore = 0xD262802FU;                // SkaterScore
constexpr uint32_t kStatsLevelNames = 0xB8D3EEECU;            // stats_level_names
constexpr uint32_t kStatsLoggedIn = 0x6307C08FU;              // stats_logged_in
constexpr uint32_t kStatsLoginFailedDialog = 0xCBA113AFU;     // StatsLoginFailedDialog
constexpr uint32_t kStatsRetrievalFailedDialog = 0xD1EA7744U; // StatsRetrievalFailedDialog
constexpr uint32_t kTEXT = 0xC4745838U;                       // TEXT
constexpr uint32_t kTopPlayersArray = 0x5299291BU;            // top_players_array
constexpr uint32_t kUiString = 0x96875C0FU;                   // ui_string

} // namespace

namespace online {

namespace {

constexpr uint32_t kLoginTimeout = 10000;   // ms
constexpr uint32_t kReportInterval = 60000; // ms between reports of a game
constexpr int kPersonalStatsLevels = 15;    // GetPersistDataValues' indexes: 0 the rating, 1-15 the levels
constexpr int kMaxSorted = 50;              // lines of a sorted list into the menu
constexpr int kLevelBest = 3;               // a level's best lines into the menu
constexpr int kMaxRank = 10;
constexpr int kStatsPort = 0x1421;

// The rank's factors (0x0064CC1C, 0x00646710).
float rank_scale() { return kRankScale; }        // 0.000286
float rank_times() { return render::kFloatTen; } // 10
// rating * 0.000286 * 10 truncated, at most 10 (x87 at 53 bits: each product rounded to double).
int current_rank(double rating)
{
    const int rank = static_cast<int>(rating * rank_scale() * rank_times());
    return rank > kMaxRank ? kMaxRank : rank;
}

// The login's start (stats_task's limit).
uint32_t& login_start()
{
    static uint32_t stats_login_start{};
    return stats_login_start;
}

RankingsManager* get_attributes() { return net_game_state().online_hub()->rating_service().get(); }
memory::Hub* ram_stats() { return memory::Hub::get_pull(); }
void push_top_down() { ram_stats()->enter_heap_scope(ram_stats()->internet_top_down_heap()); }
void push_bottom_up() { ram_stats()->enter_heap_scope(ram_stats()->internet_bottom_up_heap()); }
void pop_heap() { ram_stats()->leave_heap_scope(); }
tasks::PileUp* get_logic_tasks() { return (&assets::get_main_loop_manager()->logic_tasks()); }

uint32_t get_checksums(const char* name) { return checksums::hash_from_text(name); }
void get_run(uint32_t script, script::Struct* params = nullptr)
{
    script::launch_script(script, params, nullptr, false, nullptr);
}

// GameSpy's stats and persistent storage SDK (library code; cdecl).
int is_stats_connected() { return game_spy::stats_is_connected(); }
void close_stats_connection() { game_spy::stats_close_connection(); }
int init_stats_connection(int port) { return game_spy::stats_init_connection(port); }
int persist_think() { return game_spy::stats_persist_think(); }
void* new_game(int usebuckets) { return game_spy::stats_new_game(usebuckets); }
void free_game(void* game) { game_spy::stats_free_game(game); }
const char* get_challenge(void* game) { return game_spy::stats_get_challenge(game); }
void new_player(void* game, int number, const char* name) { game_spy::stats_new_player(game, number, name); }
void remove_player_from_game(void* game, int number) { game_spy::stats_remove_player(game, number); }
void send_game_snap_shot(void* game, const char* snapshot, int final)
{
    game_spy::stats_send_snap_shot(game, snapshot, final);
}
void get_persist_data_values(int local_id, int profile, int type, int index, const char* keys, const void* callback,
                             void* instance)
{
    game_spy::stats_get_persist_data_values(local_id, profile, type, index, keys, callback, instance);
}
// The SDK's bucket "set" operations (game / player keys, ints / strings).
void set_game_int(void* game, const char* key, int value) { game_spy::set_server_int_bucket(game, key, value); }
void set_game_string(void* game, const char* key, const char* value)
{
    game_spy::set_server_string_bucket(game, key, value);
}
void set_player_int(void* game, const char* key, int value, int player)
{
    game_spy::set_player_int_bucket(game, key, value, player);
}
void set_player_string(void* game, const char* key, const char* value, int player)
{
    game_spy::set_player_string_bucket(game, key, value, player);
}
void ghttp_cleanup() { game_spy::http_cleanup(); }
// The SDK's names for the stats connection (written before InitStatsConnection).
char* gcd_game_name() { return game_spy::stats_game_name(); }
char* get_gcd_secret_key() { return game_spy::stats_secret_key(); }
const char* stats_key() { return net::game_key(); } // (OnlineGameKey or the game's own)


void get_append(memory::ListNode* head, memory::ListNode* node)
{
    node->set_prev(head->prev());
    node->set_next(head);
    head->prev()->set_next(node);
    head->set_prev(node);
}
memory::ListNode* get_next_node(memory::ListNode* node)
{
    memory::ListNode* next = node->next();
    return next->data() == memory::ListNode::head_marker() ? nullptr : next;
}
// Delete every item of a list (each one's next fetched first; the item's scalar deleting destructor, flag 1).
template <typename Item>
void delete_all(memory::ListNode* head)
{
    for (memory::ListNode* node = get_next_node(head); node;) {
        memory::ListNode* next = get_next_node(node);
        if (node->data()) {
            static_cast<Item*>(node->data())->teardown(1);
        }
        node = next;
    }
}
int total_of(const memory::ListNode* head)
{
    int count = 0;
    for (const memory::ListNode* node = head->next(); node->data() != memory::ListNode::head_marker() && node;
         node = node->next()) {
        ++count;
    }
    return count;
}

TopEntry* new_entry()
{
    auto* entry = static_cast<TopEntry*>(memory::alloc_zeroed(sizeof(TopEntry)));
    if (entry) {
        entry->list_link().make_node(entry);
    }
    return entry;
}
SortEntry* new_sort_entry()
{
    auto* entry = static_cast<SortEntry*>(memory::alloc_zeroed(sizeof(SortEntry)));
    if (entry) {
        entry->list_link().make_node(entry);
        entry->set_points_held(0);
        entry->set_level(0);
    }
    return entry;
}
// A local sorted list (lists::Foremost<SortEntry>, highest priority first), emptied and unlinked as the original does.
struct SortedList {
    SortedList(const SortedList&) = delete;
    SortedList& operator=(const SortedList&) = delete;
    // The fields other code reads and sets.
    memory::ListNode& head() { return head_; }
    [[nodiscard]] const memory::ListNode& head() const { return head_; }

private:
    memory::ListNode head_{};

public:
    SortedList() { head_.make_head(); }
    void append_one(SortEntry* entry) { lists::append_vertex(head_, &entry->list_link()); }
    ~SortedList()
    {
        delete_all<SortEntry>(&head_);
        head_.prev()->set_next(head_.next());
        head_.next()->set_prev(head_.prev());
    }
};

} // namespace

// 0x0054D6E0
TopEntry* TopEntry::teardown(unsigned int flags)
{
    list_link_.uncouple(); // (0x0054D8F0)
    if ((flags & 1) != 0U) {
        memory::Hub::dealloc(this);
    }
    return this;
}

// 0x0054D780
SortEntry* SortEntry::teardown(unsigned int flags)
{
    list_link_.uncouple(); // (0x0054EA20)
    if ((flags & 1) != 0U) {
        memory::Hub::dealloc(this);
    }
    return this;
}

// 0x0054DFE0
void LevelScores::destroy_body()
{
    delete_all<TopEntry>(&entries_);
    entries_.uncouple();
    list_link_.uncouple(); // (0x0054D400)
}

// 0x0054EBB0
LevelScores* LevelScores::teardown(unsigned int flags)
{
    destroy_body();
    if ((flags & 1) != 0U) {
        memory::Hub::dealloc(this);
    }
    return this;
}

// --- the top lists ---

// 0x0054D8B0
TopList::TopList()
{
    levels_.make_head();
    entries_.make_head();
}

// 0x0054D5C0
TopList::~TopList()
{
    entries_.uncouple();
    levels_.uncouple();
}

// 0x0054E920
void TopList::dealloc()
{
    delete_all<LevelScores>(&levels_);
    delete_all<TopEntry>(&entries_);
}

// 0x0054E0D0
void TopList::fill_level_scores()
{
    for (memory::ListNode* node = get_next_node(&levels_); node; node = get_next_node(node)) {
        auto* level = static_cast<LevelScores*>(node->data());
        if (!level) {
            break;
        }
        if (total_of(&level->entries()) > 0) {
            const script::StructPtr params = script::make_struct();
            params->append_word(kTEXT, RankingsManager::get_level_name(level->level()));
            get_run(kAddStatHeaderMenuItem, params.get());
        }
        int shown = 0;
        for (memory::ListNode* line = get_next_node(&level->entries()); line && shown < kLevelBest;
             line = get_next_node(line)) {
            auto* entry = static_cast<TopEntry*>(line->data());
            if (!entry) {
                break;
            }
            script::StructPtr params = script::make_struct();
            params->put_int(checksums::Name{kRating}, entry->skill_points());
            params->put_int(checksums::Name{kSCORE}, entry->points_held());
            // (here the rating is read as unsigned)
            params->put_int(checksums::Name{kRANK}, current_rank(static_cast<double>(static_cast<uint32_t>(entry->skill_points()))));
            params->append_word(kNAME, entry->label().data());
            params->append_word(kLEVEL, RankingsManager::get_level_name(level->level()));
            get_run(kAddStatScoreMenuItem, params.get());
            params.reset();
            ++shown;
        }
    }
    const script::StructPtr params = script::make_struct();
    params->append_word(kTEXT, script::fetch_text(kCategoryAllLevels));
    get_run(kAddStatHeaderMenuItem, params.get());
}

// 0x0054E4A0
void TopList::fill_sorted_scores(bool ratings)
{
    SortedList sorted;
    if (ratings) {
        for (memory::ListNode* node = get_next_node(&entries_); node; node = get_next_node(node)) {
            auto* entry = static_cast<TopEntry*>(node->data());
            if (!entry) {
                break;
            }
            SortEntry* line = new_sort_entry();
            line->list_link().set_priority(entry->skill_points());
            line->set_points_held(entry->points_held());
            line->set_skill_points(entry->skill_points());
            text_tools::dup_from(line->label().data(), entry->label().data());
            sorted.append_one(line);
        }
    } else {
        for (memory::ListNode* node = get_next_node(&levels_); node; node = get_next_node(node)) {
            auto* level = static_cast<LevelScores*>(node->data());
            if (!level) {
                break;
            }
            for (memory::ListNode* item = get_next_node(&level->entries()); item; item = get_next_node(item)) {
                auto* entry = static_cast<TopEntry*>(item->data());
                if (!entry) {
                    break;
                }
                SortEntry* line = new_sort_entry();
                line->list_link().set_priority(entry->points_held());
                line->set_points_held(entry->points_held());
                line->set_level(level->level());
                line->set_skill_points(entry->skill_points());
                text_tools::dup_from(line->label().data(), entry->label().data());
                sorted.append_one(line); // (inlined in the original)
            }
        }
    }
    int shown = 0;
    for (memory::ListNode* node = get_next_node(&sorted.head()); node; node = get_next_node(node)) {
        auto* line = static_cast<SortEntry*>(node->data());
        if (!line || line->points_held() == 0 || shown == kMaxSorted) {
            break;
        }
        script::StructPtr params = script::make_struct();
        params->put_int(checksums::Name{kRating}, line->skill_points());
        params->put_int(checksums::Name{kSCORE}, line->points_held());
        params->put_int(checksums::Name{kRANK}, current_rank(static_cast<double>(line->skill_points())));
        params->append_word(kNAME, line->label().data());
        if (ratings) {
            get_run(kAddStatPlayerMenuItem, params.get());
        } else {
            params->append_word(kLEVEL, RankingsManager::get_level_name(line->level()));
            get_run(kAddStatScoreMenuItem, params.get());
        }
        params.reset();
        ++shown;
    }
}

namespace {
// The level checksums the personal stats list their high scores by.
constexpr std::array<uint32_t, 15> kLevelChecksums{{0x55894059, 0x2A179957, 0xCDAFB450, 0xCAC27049, 0xFC5833EE,
                               0x86A2D65B, 0xF3AB73AB, 0x991B5359, 0xDAF9A383, 0x7078F66C,
                               0xCC877C27, 0x0C27CA5E, 0x532C99DE, 0xD4B48511, 0x8C32C761}};
} // namespace

// 0x0054DDB0
void get_fill(PersonalStats& stats)
{
    const auto& levels = kLevelChecksums;
    SortedList sorted;
    for (int i = 0; i < kStatsLevelCount; ++i) {
        const int score = core::at(stats.high_scores_, i);
        if (score == 0) {
            continue;
        }
        SortEntry* line = new_sort_entry();
        line->list_link().set_priority(score);
        line->set_points_held(score);
        line->set_level(core::at(levels, i));
        sorted.append_one(line);
    }
    for (memory::ListNode* node = get_next_node(&sorted.head()); node; node = get_next_node(node)) {
        auto* line = static_cast<SortEntry*>(node->data());
        if (!line) {
            break;
        }
        gsl::owner<script::Struct*> params = script::new_struct(); // (never freed - as in the original)
        params->put_int(checksums::Name{kSCORE}, line->points_held());
        params->append_word(kLEVEL, RankingsManager::get_level_name(line->level()));
        get_run(kAddStatPersonalMenuItem, params);
    }
}

// --- the stats manager ---

// 0x0054DBD0
// (the five top lists made first by their constructors)
RankingsManager::RankingsManager()
{
    personal_.skill_points_ = 0;
    std::memset(personal_.high_scores_.data(), 0, sizeof(personal_.high_scores_));
    std::memset(personal_.high_combos_.data(), 0, sizeof(personal_.high_combos_));
    ram_stats()->enter_heap_scope(ram_stats()->network_heap());
    task_ = std::make_unique<tasks::Task<RankingsManager>>(&stats_task, this, 0);
    pop_heap();
}

// 0x0054D610
RankingsManager::~RankingsManager()
{
    task_.reset();
    // (then the top lists by their destructors, the last first)
}

// 0x0054CBE0
void RankingsManager::log_in()
{
    push_bottom_up();
    cur_phase_ = RankingsManager::State::kLoggingIn;
    login_start() = frame_clock::get_time();
    get_run(kLoggingInScript);
    DWORD id = 0;
    HANDLE thread = CreateThread(nullptr, net::kSdkThreadStackBytes, &log_in_thread, this, CREATE_SUSPENDED, &id);
    net_game_state().online_hub()->set_room_id(id);
    ResumeThread(thread); // (never closed)
    if (!task_->is_on_task_list()) {
        get_logic_tasks()->queue_job(task_.get());
    }
    pop_heap();
}

// 0x0054C9D0
void RankingsManager::log_off()
{
    close_stats_connection();
    task_->drop_out();
    authenticated_ = false;
    cur_phase_ = RankingsManager::State::kNone;
}

// 0x0054C9B0
bool RankingsManager::update_logged_in()
{
    authenticated_ = is_stats_connected() == 1;
    return authenticated_;
}

// 0x0054CBA0
void RankingsManager::on_logged_in()
{
    push_top_down();
    authenticated_ = true;
    get_run(kStatsLoggedIn);
    pop_heap();
}

// 0x0054CE80
void RankingsManager::start_game()
{
    Hub* manager = net_game_state().online_hub();
    detail::PlayerSearch search;
    push_bottom_up();
    game_ = new_game(1);
    const char* challenge = get_challenge(game_);
    auto* server = static_cast<net::AppHost*>(manager->host());
    prefs::Preferences* prefs = manager->get_online_settings();
    const char* host_name = nullptr;
    prefs->fetch_setting(kServerName)->fetch_text(kUiString, &host_name, 1);
    const int time_limit = prefs->get_setting_value({.preference = 0xC51BF6E3, .field = 0x906B67BA});
    const int frag_limit = prefs->get_setting_value({.preference = 0x71CD53A2, .field = 0xCD66C8AE});
    set_game_string(game_, "hostname", host_name);
    set_game_string(game_, "mapname", manager->get_map_name(true));
    set_game_string(game_, "gametype", manager->get_game_type_name());
    set_game_int(game_, "mapcrc", static_cast<int>(manager->fetch_stage()));
    set_game_int(game_, "gametypecrc", static_cast<int>(manager->get_game_type_preference()));
    set_game_int(game_, "maxplayers", manager->get_player_limit());
    set_game_int(game_, "timelimit", time_limit);
    set_game_int(game_, "fraglimit", frag_limit);
    for (PlayerRecord* player = detail::start_roster_sweep(manager, &search, false); player;
         player = detail::step_roster_sweep(manager, &search, false)) {
        net::NetMessageInfo desc;
        const int id = static_cast<int>(detail::get_skater_id(detail::player_skater(player)));
        const char* name = detail::player_name(player);
        new_player(game_, id, name);
        constexpr size_t kKeySize = 64;
        std::array<char, kKeySize> key{};
        text::format_to(key, "%s", "player");
        set_player_string(game_, key.data(), name, id);
        text::format_to(key, "%s", "pid");
        const int profile = player->profile_id();
        set_player_int(game_, key.data(), profile, id);
        if (profile > 0) {
            // The player's client answers with its response to the challenge.
            desc.set_content(challenge);
            desc.set_byte_count(static_cast<uint16_t>(std::strlen(challenge) + 1));
            desc.set_ident(MessageId::kStatsChallenge);
            desc.set_queue(2);
            desc.set_group_id(MessageGroup::kGame);
            server->put_on_outbox(detail::player_handle(player), &desc);
        }
    }
    pop_heap();
}

// 0x0054D170
void RankingsManager::raise_tallies(bool final)
{
    Hub* manager = net_game_state().online_hub();
    detail::PlayerSearch search;
    if (!game_) {
        return;
    }
    push_bottom_up();
    for (const PlayerRecord* player = detail::start_roster_sweep(manager, &search, false); player;
         player = detail::step_roster_sweep(manager, &search, false)) {
        auto* skater = core::down_cast<objects::GameEntity>(detail::player_skater(player));
        skater->fetch_module(kSkaterScore); // (unused)
        const int id = static_cast<int>(detail::get_skater_id(skater));
        set_player_int(game_, "score", manager->get_entrant_points(static_cast<uint32_t>(id)), id);
        set_player_int(game_, "combo", player->best_combo(), id);
    }
    set_game_int(game_, "highscore",
                 static_cast<int>(manager->cheating_noted()) | static_cast<int>(net_game_state().cheat_flag()));
    send_game_snap_shot(game_, nullptr, static_cast<int>(final));
    if (final) {
        cur_phase_ = RankingsManager::State::kReady;
        if (game_) {
            free_game(game_);
            game_ = nullptr;
        }
    }
    last_report_ = frame_clock::get_time();
    pop_heap();
}

// 0x0054C9F0
void RankingsManager::remove_player(int id)
{
    if (cur_phase_ == RankingsManager::State::kReporting) {
        remove_player_from_game(game_, id);
    }
}

// 0x0054CA30
void RankingsManager::set_player_auth(int player, const char* auth)
{
    constexpr size_t kKeySize = 64;
    std::array<char, kKeySize> key{};
    text::format_to(key, "%s", "auth");
    set_player_string(game_, key.data(), auth, player);
}

// 0x0054CA80
PersonalStats* RankingsManager::get_personal_stats() { return &personal_; }

// 0x0054D2D0
int RankingsManager::get_rank() const { return current_rank(static_cast<double>(personal_.skill_points_)); }

// 0x0054D7B0
void RankingsManager::read_skill_ratings(char* text)
{
    push_top_down();
    for (const char* token = text_tools::get_token(text, ":"); token; token = text_tools::get_token(nullptr, ":")) {
        TopEntry* entry = new_entry();
        get_append(&core::at(top_lists_, 4).entries(), &entry->list_link());
        entry->set_points_held(text::to_int(token));
        token = text_tools::get_token(nullptr, ":");
        if (!token) {
            break;
        }
        entry->set_skill_points(text::to_int(token));
        token = text_tools::get_token(nullptr, "\n");
        if (!token) {
            break;
        }
        text_tools::dup_from(entry->label().data(), token);
    }
    pop_heap();
}

// 0x0054D910
void RankingsManager::parse_top_list(int index, char* text, bool titled)
{
    TopList& list = core::at(top_lists_, index);
    push_top_down();
    if (titled) {
        text_tools::dup_from(list.title().data(), text_tools::get_token(text, "\n"));
        text = nullptr;
    }
    const char* token = text_tools::get_token(text, ":");
    // "Level:<checksum>\n", then its lines "score:rating:name\n" until the next "Level" (anything else first ends it).
    while (token && text::compare_caseless(token, "Level") == 0) {
        const char* value = text_tools::get_token(nullptr, "\n");
        if (!value) {
            break;
        }
        const int level = text::to_int(value);
        auto* scores = static_cast<LevelScores*>(memory::alloc_zeroed(sizeof(LevelScores)));
        if (scores) {
            scores->list_link().make_node(scores);
            scores->entries().make_head();
        }
        scores->set_level(static_cast<uint32_t>(level));
        get_append(&list.levels(), &scores->list_link());
        token = text_tools::get_token(nullptr, ":");
        while (token && text::compare_caseless(token, "Level") != 0) {
            TopEntry* entry = new_entry();
            get_append(&scores->entries(), &entry->list_link());
            entry->set_points_held(text::to_int(token));
            token = text_tools::get_token(nullptr, ":");
            if (!token) {
                break;
            }
            entry->set_skill_points(text::to_int(token));
            token = text_tools::get_token(nullptr, "\n");
            if (!token) {
                break;
            }
            text_tools::dup_from(entry->label().data(), token);
            token = text_tools::get_token(nullptr, ":");
        }
    }
    pop_heap();
}

// 0x0054D4A0
void RankingsManager::stats_task(tasks::Task<RankingsManager>* task)
{
    push_top_down();
    RankingsManager* stats = task->content();
    bool failed = false;
    switch (stats->cur_phase_) {
    case RankingsManager::State::kLoggingIn:
        failed = frame_clock::get_time() - login_start() > kLoginTimeout;
        break;
    case RankingsManager::State::kAuthDone:
        stats->cur_phase_ = RankingsManager::State::kReady;
        stats->on_logged_in();
        break;
    case RankingsManager::State::kLoginFailed:
        get_run(kStatsLoginFailedDialog);
        close_stats_connection();
        stats->task_->drop_out();
        stats->authenticated_ = false;
        stats->cur_phase_ = RankingsManager::State::kNone;
        break;
    case RankingsManager::State::kGettingStats:
        failed = frame_clock::get_time() - login_start() > kLoginTimeout || persist_think() == 0;
        break;
    case RankingsManager::State::kReporting:
        if (frame_clock::get_time() - stats->last_report_ > kReportInterval) {
            stats->raise_tallies(false);
        }
        break;
    case RankingsManager::State::kNone:
    case RankingsManager::State::kReady:
        break;
    }
    if (failed) {
        get_run(kStatsRetrievalFailedDialog);
        stats->log_off();
    }
    pop_heap();
}

// 0x0054CAC0
unsigned long __stdcall RankingsManager::log_in_thread(void* param)
{
    push_bottom_up();
    RankingsManager* stats = get_attributes();
    (void)param;
    // The game name: the online identity's (OnlineGameName; the original pushes "thps6pc", which online_identity
    // used to write over). The key: what online_identity leaves in the stats key's data (OnlineGameKey).
    text::format_to(std::span(gcd_game_name(), kGcdBufferBytes), "%s", net::get_game_name());
    text::format_to(std::span(get_gcd_secret_key(), kGcdBufferBytes), "%s", stats_key());
    switch (init_stats_connection(kStatsPort)) {
    case 0: // connected
        stats->cur_phase_ = RankingsManager::State::kAuthDone;
        break;
    case 1:
    case 2:
    case 3:
    case kStatsResultLateFailure:
        stats->cur_phase_ = RankingsManager::State::kLoginFailed;
        break;
    default: // 4 and others
        break;
    }
    pop_heap();
    return 0;
}

// 0x0054CCF0
void RankingsManager::on_personal_stats(const PersistReply& reply)
{
    const auto index = reply.index;
    const auto success = reply.success;
    auto* const data = reply.data;
    if (success < 1) {
        return;
    }
    RankingsManager* stats = get_attributes();
    PersonalStats& personal = stats->personal_; // (index 0 the rating, then the high scores; the combos 15 further)
    if (index == 0) {
        if (text_tools::get_token(data, "\\")) { // "THUG2Rating"
            personal.skill_points_ = text::to_int(text_tools::get_token(nullptr, "\\"));
        }
        return;
    }
    const char* key = text_tools::get_token(data, "\\");
    if (key) {
        if (text::compare_caseless(key, "THUG2HighScore") == 0) {
            if (const char* value = text_tools::get_token(nullptr, "\\")) {
                core::at(personal.high_scores_, index - 1) = text::to_int(value);
            }
            key = text_tools::get_token(nullptr, "\\");
        }
        if (text::compare_caseless(key, "THUG2HighCombo") == 0) { // (null key: a crash, as in the original)
            if (const char* value = text_tools::get_token(nullptr, "\\")) {
                core::at(personal.high_combos_, index - 1) = text::to_int(value);
            }
        }
    }
    if (index == kLastPersonalStatsIndex) {
        get_run(kStatsReadyScript);
        stats->cur_phase_ = RankingsManager::State::kReady;
    }
}

// 0x0054DB20
int RankingsManager::on_top_stats(void* /*unused*/, int result, char* buffer, int64_t /*unused*/, void* param)
{
    auto* vault = static_cast<VaultMan*>(param);
    vault->set_progress(kProgressComplete); // (the setters folded at 0x0048DF10, 0x00428270, 0x004076E0)
    if (result != 0) {
        vault->set_outcome_val(2);
        vault->set_fault(result);
        return 1;
    }
    vault->set_outcome_val(1);
    vault->set_fault(0);
    RankingsManager* stats = get_attributes();
    switch (stats->top_file_) {
    case 0:
    case 1:
        stats->parse_top_list(stats->top_file_, buffer, false);
        break;
    case 2:
    case 3:
        stats->parse_top_list(stats->top_file_, buffer, true);
        break;
    case 4:
        stats->read_skill_ratings(buffer);
        break;
    default:
        break;
    }
    return 1;
}

// 0x0054D2F0
const char* RankingsManager::get_level_name(uint32_t level)
{
    script::Array* levels = script::fetch_list(kStatsLevelNames);
    // (the found level is read into one variable: an entry without one counts as the last one read - at first
    // the level asked for)
    uint32_t found = level;
    for (uint32_t i = 0; i < levels->extent(); ++i) {
        auto* entry = levels->struct_at(i);
        entry->read_hash_value(kLEVEL, &found, 0);
        if (found == level) {
            const char* name = nullptr;
            entry->fetch_text(kTEXT, &name, 1);
            return name;
        }
    }
    return nullptr;
}

// 0x0054CA10
void RankingsManager::generate_auth(const char* challenge, const char* password, char* response)
{
    game_spy::stats_generate_auth(challenge, password, response);
}

// --- the script commands ---

// 0x0054CC90
bool stats_logged_in(script::Struct* /*params*/, script::Script* /*script*/)
{
    return get_attributes()->update_logged_in();
}

// 0x0054CCB0
bool stats_log_in(script::Struct* /*params*/, script::Script* /*script*/)
{
    get_attributes()->log_in();
    return true;
}

// 0x0054CCC0
bool stats_log_off(script::Struct* /*params*/, script::Script* /*script*/)
{
    get_attributes()->log_off();
    return true;
}

// 0x0054CE00: the player's rating and the levels' high scores and combos from persistent storage (16 requests,
// indexes 0-15; state 5 until the answer to 14).
bool retrieve_personal_stats(script::Struct* /*params*/, script::Script* /*script*/)
{
    const Hub* manager = net_game_state().online_hub();
    const void* callback = code_address(&core::gathered<RankingsManager::PersistReply, &RankingsManager::on_personal_stats, void, int,
                                                       int, int, int, int, uint32_t, char*, int, void*>);
    int profile = manager->presence_service()->account();
    get_persist_data_values(0, profile, 2, 0, "\\THUG2Rating", callback, nullptr);
    for (int index = 1; index <= kPersonalStatsLevels; ++index) { // (1-15: the answer to 14 ends the wait)
        profile = manager->presence_service()->account();
        get_persist_data_values(0, profile, 2, index, "\\THUG2HighScore\\THUG2HighCombo", callback, nullptr);
    }
    manager->rating_service()->set_cur_phase(RankingsManager::State::kGettingStats);
    return true;
}

// 0x0054CE70
bool need_to_retrieve_top_stats(script::Struct* /*params*/, script::Script* /*script*/)
{
    return get_attributes()->need_top_stats();
}

// 0x0054D670: raise_tallies [0x132AA339: the final report]
bool raise_tallies(script::Struct* params, script::Script* /*script*/)
{
    const bool final = params->has_bare_flag(0x132AA339U);
    get_attributes()->raise_tallies(final);
    return true;
}

// 0x0054D6A0: the rank (0..10) into the script's parameters (0x7786171A).
bool get_rank(script::Struct* /*params*/, script::Script* script)
{
    script->args()->put_int(checksums::Name{kRANK}, current_rank(static_cast<double>(get_attributes()->personal().skill_points_)));
    return true;
}

// 0x0054EA40: the top lists and the stats menus' arrays emptied (once: until the top stats are needed again).
bool clean_up_top_stats(script::Struct* /*params*/, script::Script* /*script*/)
{
    RankingsManager* stats = get_attributes();
    if (stats->need_top_stats()) {
        return true;
    }
    for (TopList& list : stats->top_lists()) {
        list.dealloc();
    }
    stats->set_need_top_stats(true);
    script::release_array_storage(script::fetch_list("high_scores_array_all_time"));
    script::release_array_storage(script::fetch_list("best_combos_array_all_time"));
    script::release_array_storage(script::fetch_list("high_scores_array_monthly"));
    script::release_array_storage(script::fetch_list("best_combos_array_monthly"));
    script::release_array_storage(script::fetch_list("top_players_array"));
    script::release_array_storage(script::fetch_list("personal_stats_array"));
    ghttp_cleanup();
    return true;
}

// 0x0054EB00: FillStatsArrays <0x5EF31148: which>: the personal stats, the ratings, or a top list (its levels' best,
// then all sorted).
bool fill_stats_arrays(script::Struct* params, script::Script* /*script*/)
{
    RankingsManager* stats = get_attributes();
    uint32_t which = 0;
    params->read_hash_value(kArray, &which, 1);
    TopList* list = nullptr;
    switch (which) {
    case kHighScoresArrayAllTime:
        list = &core::at(stats->top_lists(), 0);
        break;
    case kBestCombosArrayAllTime:
        list = &core::at(stats->top_lists(), 1);
        break;
    case kHighScoresArrayMonthly:
        list = &core::at(stats->top_lists(), 2);
        break;
    case kBestCombosArrayMonthly:
        list = &core::at(stats->top_lists(), 3);
        break;
    case kTopPlayersArray:
        core::at(stats->top_lists(), 4).fill_sorted_scores(true);
        return true;
    case kPersonalStatsArray:
        get_fill(stats->personal());
        return true;
    default:
        return true;
    }
    list->fill_level_scores();
    list->fill_sorted_scores(false);
    return true;
}

} // namespace online

