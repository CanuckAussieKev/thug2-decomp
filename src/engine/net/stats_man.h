#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/core/arena_alloc.h"

namespace script {
class Script;
class Struct;
} // namespace script
namespace tasks {
class SchedulerTask;
template <typename Owner>
class Task;
} // namespace tasks

// The online stats: online::Hub's rating_service_ (+0x1C), on GameSpy's stats and persistent storage SDK (gstats /
// gpersist: library code, not decompiled - called by address, some of it through function pointers the SDK keeps).
// Logging in to the stats server (a thread), reporting the host's game (the players, their scores and combos), the
// player's own rating, high scores and combos from persistent storage, and the top-score lists downloaded from
// gamestats (stats.cpp's RetrieveTopStats; VaultMan downloads them) and turned into the stats menus' arrays.
// 0x0054C9B0-0x0054EBE0.
namespace online {

// The top-score lists' items are their own list nodes (data = the item); the original's offsets were 4 more after
// each node (its table word).

// A line of a top-score list (0x3C bytes in the original): lists::Elem<TopEntry>.
struct TopEntry {
    memory::ListNode list_link_; // +0x00 data = this
    char label_[0x20];           // +0x10
    int skill_points_;           // +0x30
    int points_held_;            // +0x34

    // 0x0054D6E0 (the scalar deleting destructor): out of its list (0x0054D8F0), freed with flag 1.
    TopEntry* teardown(unsigned int flags);
};
static_assert(sizeof(TopEntry) == 0x38 && offsetof(TopEntry, skill_points_) == 0x30);

// A level's part of a top-score list (0x2C bytes in the original): lists::Elem<LevelScores> and its entries.
struct LevelScores {
    memory::ListNode list_link_; // +0x00 data = this
    uint32_t level_;             // +0x10 the level's checksum
    memory::ListNode entries_;   // +0x14 lists::Foremost<TopEntry>

    // 0x0054DFE0 (destructor body): the entries deleted, the head and the node unlinked.
    void destroy_body();
    // 0x0054EBB0 (the scalar deleting destructor)
    LevelScores* teardown(unsigned int flags);
};
static_assert(sizeof(LevelScores) == 0x24 && offsetof(LevelScores, entries_) == 0x14);

// One of the five top-score lists (0x48 bytes in the original): the levels' scores (high scores / combos, all time /
// monthly) or, for the ratings list, the entries themselves.
struct TopList {
    memory::ListNode levels_;  // +0x00 lists::Foremost<LevelScores>
    memory::ListNode entries_; // +0x10 lists::Foremost<TopEntry>: the ratings list's
    char title_[0x20];         // +0x20 the file's first line (the lists of combos)

    // 0x0054D8B0: two empty heads.
    TopList* make_new();
    // 0x0054D5C0 (destructor body): the heads unlinked.
    void destroy_body();
    // 0x0054E920: every level's scores and every entry deleted.
    void dealloc();
    // 0x0054E0D0: for each level with scores: its name (script 0x1AADEE21), then its best three (script 0x19F7BC28:
    // rating, score, rank, name, level); last the title 0xC2283319's text (script 0x1AADEE21).
    void fill_level_scores();
    // 0x0054E4A0: every entry (ratings: the list's own; else every level's) sorted by score, the best 50 with a score
    // into the menu (script 0x0AC09E3B for ratings, else 0x19F7BC28 with the level's name).
    void fill_sorted_scores(bool ratings);
};
static_assert(sizeof(TopList) == 0x40 && offsetof(TopList, title_) == 0x20);

// A sorted line for the menus (0x40 bytes in the original, on a local list): lists::Elem<SortEntry>, its priority
// the score.
struct SortEntry {
    memory::ListNode list_link_; // +0x00 data = this, priority = the sort key
    char label_[0x20];           // +0x10
    uint32_t level_;             // +0x30
    int points_held_;            // +0x34
    int skill_points_;           // +0x38

    // 0x0054D780 (the scalar deleting destructor; the node's 0x0054EA20 / 0x0054D740)
    SortEntry* teardown(unsigned int flags);
};
static_assert(sizeof(SortEntry) == 0x3C);

// (lists::Hunt<SortEntry>'s, <TopEntry>'s and <LevelScores>'s vtables - 0x0064D580 / 0x0064D584 / 0x0064D588 - hold
// only destructors that reset them: 0x0054CA90 / 0x0054D370, 0x0054CAA0 / 0x0054D3A0, 0x0054CAB0 / 0x0054D3D0.)

// The player's own stats from persistent storage (RankingsManager +0x14).
struct PersonalStats {
    int skill_points_;    // +0x00
    int high_scores_[15]; // +0x04 by level (1-14 filled: index i at high_scores_[i - 1])
    int high_combos_[15]; // +0x40

    // 0x0054DDB0: the levels' high scores (the level checksums at 0x0067F260), best first, into the menu (script
    // 0xA6111F30: score, level name; its parameters are never freed - kept).
    void get_fill();
};
static_assert(sizeof(PersonalStats) == 0x7C, "layout must match the original");

class RankingsManager {
public:
    // cur_phase_.
    enum State : int {
        kNone = 0,
        kLoggingIn = 1, // the login thread runs
        kAuthDone = 2,  // (the thread's answer: the task makes it 4)
        kLoginFailed = 3,
        kReady = 4,
        kGettingStats = 5, // RetrievePersonalStats: waiting for the 15 answers
        kReporting = 6,    // a game being reported
    };

    // 0x0054DBD0: no stats, five empty top lists, the task (stats_task) on the network heap; the top stats are
    // needed. Returns this.
    RankingsManager* make_new();
    // 0x0054D610 (destructor body): the task deleted, the top lists' heads unlinked (last first).
    void destroy_body();

    // 0x0054CBE0: log in on a thread (log_in_thread; state 1, script 0x23343580, 10 s from now); the task joins the
    // logic tasks (once).
    void log_in();
    // 0x0054C9D0: the stats connection closed, the task out of its list, logged off.
    void log_off();
    // 0x0054C9B0: authenticated_ from the SDK's connection state. Returns it.
    bool update_logged_in();
    // 0x0054CBA0: logged in (script 0x6307C08F).
    void on_logged_in();
    // 0x0054CE80: a game to report: the SDK's game (hostname, level, game type, their checksums, max players, time
    // and frag limits) and each player (name, profile; a player with a profile gets the challenge, message 0x7C).
    void start_game();
    // 0x0054D170: each player's score and best combo and the cheat flag reported (a snapshot; `final`: the game
    // freed, state 4). Nothing without a game.
    void raise_tallies(bool final);
    // 0x0054C9F0: player `id` gone from the game being reported (state 6 only).
    void remove_player(int id);
    // 0x0054CA30: player `player`'s "auth" key.
    void set_player_auth(int player, const char* auth);
    // 0x0054CA80 (4 bytes: source only): the player's own stats (the rating, then the high scores and combos).
    PersonalStats* get_personal_stats();
    // 0x0054D2D0: the rank from the rating: rating * 0.000286 * 10, at most 10.
    int get_rank();
    // 0x0054D7B0: the ratings list "rating:score:name\n"... into the fifth top list.
    void read_skill_ratings(char* text);
    // 0x0054D910: a top-score list "Level:<level>\n" then "score:rating:name\n" lines, into top list `list`
    // (`titled`: its first line is the title).
    void parse_top_list(int list, char* text, bool titled);

    // 0x0054D4A0: the task: the login's 10 s (and the persistent storage's answers'), the login's result, the
    // reports once a minute while a game is reported.
    static void __cdecl stats_task(tasks::Task<RankingsManager>* task);
    // 0x0054CAC0 (thread, stdcall): the stats SDK's game name and secret key (the online identity's - see
    // online_identity.h) and InitStatsConnection; state 2 (connected) or 3 (failed). Returns 0.
    static unsigned long __stdcall log_in_thread(void* stats);
    // 0x0054CCF0: GetPersistDataValues' answer for index `index` (0 the rating, 1-14 a level's high score and combo;
    // after 14: script 0x955801C6, state 4).
    static void __cdecl on_personal_stats(int local_id, int profile, int type, int index, int success,
                                          uint32_t modified, char* data, int length, void* instance);
    // 0x0054DB20 (ghttp's completed callback of the top stats' download; param: the VaultMan): the file parsed into
    // its top list. Returns 1.
    static int __cdecl on_top_stats(void* request, int result, char* buffer, int64_t buffer_length, void* vault);
    // 0x0054D2F0 (stdcall): the name of level `level` from the level list 0xB8D3EEEC (null if none).
    static const char* __stdcall get_level_name(uint32_t level);
    // 0x0054CA10 (stdcall): the SDK's GenerateAuth (0x00600640) for `challenge` and `password`.
    static void __stdcall generate_auth(const char* challenge, const char* password, char* response);

    tasks::SchedulerTask* task_; // +0x000 tasks::Task<RankingsManager> (stats_task)
    bool authenticated_;         // +0x004
    uint8_t pad5_[3];
    int cur_phase_;          // +0x008 State
    void* game_;             // +0x00C the SDK's game being reported
    uint32_t last_report_;   // +0x010
    PersonalStats personal_; // +0x014
    int top_file_;           // +0x090 the top-score file being downloaded (RetrieveTopStats)
    TopList top_lists_[5];   // +0x094 high scores all time, monthly, best combos all time, monthly; ratings
    bool need_top_stats_;    // +0x1D4 (the original's +0x1FC: the lists' heads had table words)
};
static_assert(offsetof(RankingsManager, personal_) == 0x14 && offsetof(RankingsManager, top_file_) == 0x90 &&
                  offsetof(RankingsManager, top_lists_) == 0x94 && offsetof(RankingsManager, need_top_stats_) == 0x1D4,
              "the original's, less the top lists' table words");

// The script commands.
bool __cdecl stats_logged_in(script::Struct* params, script::Script* script);            // 0x0054CC90
bool __cdecl stats_log_in(script::Struct* params, script::Script* script);               // 0x0054CCB0
bool __cdecl stats_log_off(script::Struct* params, script::Script* script);              // 0x0054CCC0
bool __cdecl retrieve_personal_stats(script::Struct* params, script::Script* script);    // 0x0054CE00
bool __cdecl need_to_retrieve_top_stats(script::Struct* params, script::Script* script); // 0x0054CE70
bool __cdecl raise_tallies(script::Struct* params, script::Script* script);              // 0x0054D670
bool __cdecl get_rank(script::Struct* params, script::Script* script);                   // 0x0054D6A0
bool __cdecl clean_up_top_stats(script::Struct* params, script::Script* script);         // 0x0054EA40
bool __cdecl fill_stats_arrays(script::Struct* params, script::Script* script);          // 0x0054EB00

extern float g_rank_scale;

} // namespace online
