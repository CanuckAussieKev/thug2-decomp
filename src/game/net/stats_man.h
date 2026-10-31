#pragma once

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script
namespace Tsk {
class BaseTask;
} // namespace Tsk

// The online stats: GameNet::Manager's m_statsMan (+0x1C), on GameSpy's stats and persistent storage SDK (gstats /
// gpersist: library code, not decompiled - called by address, some of it through function pointers the SDK keeps).
// Logging in to the stats server (a thread), reporting the host's game (the players, their scores and combos), the
// player's own rating, high scores and combos from persistent storage, and the top-score lists downloaded from
// gamestats (stats.cpp's RetrieveTopStats; VaultMan downloads them) and turned into the stats menus' arrays.
// 0x0054C9B0-0x0054EBE0.
namespace GameNet {

// A line of a top-score list (0x3C bytes): Lst::Node<TopEntry> (vtable 0x0064D640; the node's own 0x0064D634).
struct TopEntry {
    Mem::ListNode m_node;    // +0x00 data = this
    char m_name[0x20];       // +0x14
    int m_rating;            // +0x34
    int m_score;             // +0x38

    // 0x0054D6E0 (vtable 0x0064D640 slot 0, scalar deleting): the node destructor (0x0054D8F0), freed with flag 1.
    TopEntry* Destroy(unsigned int flags);
};
static_assert(sizeof(TopEntry) == 0x3C && offsetof(TopEntry, m_rating) == 0x34, "layout must match the original");

// A level's part of a top-score list (0x2C bytes): Lst::Node<LevelScores> (vtable 0x0064D648) and its entries.
struct LevelScores {
    Mem::ListNode m_node;    // +0x00 data = this
    uint32_t m_level;        // +0x14 the level's checksum
    Mem::ListNode m_entries; // +0x18 Lst::Head<TopEntry> (vtable 0x0064D640)

    // 0x0054DFE0 (destructor body): the entries deleted, the head and the node unlinked.
    void DestroyBody();
    // 0x0054EBB0 (vtable 0x0064D648 slot 0, scalar deleting)
    LevelScores* Destroy(unsigned int flags);
};
static_assert(sizeof(LevelScores) == 0x2C && offsetof(LevelScores, m_entries) == 0x18, "layout must match the original");

// One of the five top-score lists (0x48 bytes): the levels' scores (high scores / combos, all time / monthly) or, for
// the ratings list, the entries themselves.
struct TopList {
    Mem::ListNode m_levels;  // +0x00 Lst::Head<LevelScores> (vtable 0x0064D644)
    Mem::ListNode m_entries; // +0x14 Lst::Head<TopEntry> (vtable 0x0064D640): the ratings list's
    char m_title[0x20];      // +0x28 the file's first line (the lists of combos)

    // 0x0054D8B0: two empty heads.
    TopList* Construct();
    // 0x0054D5C0 (destructor body): the heads unlinked.
    void DestroyBody();
    // 0x0054E920: every level's scores and every entry deleted.
    void Free();
    // 0x0054E0D0: for each level with scores: its name (script 0x1AADEE21), then its best three (script 0x19F7BC28:
    // rating, score, rank, name, level); last the title 0xC2283319's text (script 0x1AADEE21).
    void FillLevelScores();
    // 0x0054E4A0: every entry (ratings: the list's own; else every level's) sorted by score, the best 50 with a score
    // into the menu (script 0x0AC09E3B for ratings, else 0x19F7BC28 with the level's name).
    void FillSortedScores(bool ratings);
};
static_assert(sizeof(TopList) == 0x48 && offsetof(TopList, m_title) == 0x28, "layout must match the original");

// A sorted line for the menus (0x40 bytes, on a local list): Lst::Node<SortEntry> (vtable 0x0064D64C; the node's
// own 0x0064D63C), its priority the score.
struct SortEntry {
    Mem::ListNode m_node;    // +0x00 data = this, priority = the sort key
    char m_name[0x20];       // +0x14
    uint32_t m_level;        // +0x34
    int m_score;             // +0x38
    int m_rating;            // +0x3C

    // 0x0054D780 (vtable 0x0064D64C slot 0, scalar deleting)
    SortEntry* Destroy(unsigned int flags);
};
static_assert(sizeof(SortEntry) == 0x40, "layout must match the original");

// The list nodes' own destructors and the searches' (as the original keeps them apart).
struct StatsNode {
    Mem::ListNode m_node;

    // 0x0054D8F0 / 0x0054D400 / 0x0054EA20: the node destructors of TopEntry (vtable 0x0064D634), LevelScores'
    // (0x0064D638) and SortEntry's (0x0064D63C): the vtable reset, unlinked.
    void DestroyEntryNode();
    void DestroyLevelNode();
    void DestroySortNode();
    // 0x0054D420 / 0x0054D460 / 0x0054D740 (those vtables' slot 0) and 0x0054D710 (the levels head's, vtable
    // 0x0064D644): the scalar deleting destructors.
    StatsNode* DeleteEntryNode(unsigned int flags);
    StatsNode* DeleteLevelNode(unsigned int flags);
    StatsNode* DeleteSortNode(unsigned int flags);
    StatsNode* DeleteLevelsHead(unsigned int flags);
};
// Lst::Search<SortEntry> (vtable 0x0064D580), <TopEntry> (0x0064D584), <LevelScores> (0x0064D588).
struct StatsSearch {
    void** m_vtable;
    Mem::ListNode* m_node;

    // 0x0054CA90 / 0x0054CAA0 / 0x0054CAB0: the destructor bodies (the vtable reset).
    void DestroySortSearch();
    void DestroyEntrySearch();
    void DestroyLevelSearch();
    // 0x0054D370 / 0x0054D3A0 / 0x0054D3D0: the scalar deleting destructors.
    StatsSearch* DeleteSortSearch(unsigned int flags);
    StatsSearch* DeleteEntrySearch(unsigned int flags);
    StatsSearch* DeleteLevelSearch(unsigned int flags);
};

// The player's own stats from persistent storage (StatsMan +0x14).
struct PersonalStats {
    int m_rating;            // +0x00
    int m_highScores[15];    // +0x04 by level (1-14 filled: index i at m_highScores[i - 1])
    int m_highCombos[15];    // +0x40

    // 0x0054DDB0: the levels' high scores (the level checksums at 0x0067F260), best first, into the menu (script
    // 0xA6111F30: score, level name; its parameters are never freed - kept).
    void Fill();
};
static_assert(sizeof(PersonalStats) == 0x7C, "layout must match the original");

class StatsMan {
public:
    // m_state.
    enum State : int {
        kNone = 0,
        kLoggingIn = 1,    // the login thread runs
        kLoggedIn = 2,     // (the thread's answer: the task makes it 4)
        kLoginFailed = 3,
        kReady = 4,
        kGettingStats = 5, // RetrievePersonalStats: waiting for the 15 answers
        kReporting = 6,    // a game being reported
    };

    // 0x0054DBD0: no stats, five empty top lists, the task (StatsTask) on the network heap; the top stats are
    // needed. Returns this.
    StatsMan* Construct();
    // 0x0054D610 (destructor body): the task deleted, the top lists' heads unlinked (last first).
    void DestroyBody();

    // 0x0054CBE0: log in on a thread (LogInThread; state 1, script 0x23343580, 10 s from now); the task joins the
    // logic tasks (once).
    void LogIn();
    // 0x0054C9D0: the stats connection closed, the task out of its list, logged off.
    void LogOff();
    // 0x0054C9B0: m_loggedIn from the SDK's connection state. Returns it.
    bool UpdateLoggedIn();
    // 0x0054CBA0: logged in (script 0x6307C08F).
    void OnLoggedIn();
    // 0x0054CE80: a game to report: the SDK's game (hostname, level, game type, their checksums, max players, time
    // and frag limits) and each player (name, profile; a player with a profile gets the challenge, message 0x7C).
    void StartGame();
    // 0x0054D170: each player's score and best combo and the cheat flag reported (a snapshot; `final`: the game
    // freed, state 4). Nothing without a game.
    void ReportStats(bool final);
    // 0x0054C9F0: player `id` gone from the game being reported (state 6 only).
    void RemovePlayer(int id);
    // 0x0054CA30: player `player`'s "auth" key.
    void SetPlayerAuth(int player, const char* auth);
    // 0x0054CA80 (4 bytes: source only): the player's own stats (the rating, then the high scores and combos).
    PersonalStats* GetPersonalStats();
    // 0x0054D2D0: the rank from the rating: rating * 0.000286 * 10, at most 10.
    int GetRank();
    // 0x0054D7B0: the ratings list "rating:score:name\n"... into the fifth top list.
    void ParseRatings(char* text);
    // 0x0054D910: a top-score list "Level:<level>\n" then "score:rating:name\n" lines, into top list `list`
    // (`titled`: its first line is the title).
    void ParseTopList(int list, char* text, bool titled);

    // 0x0054D4A0: the task: the login's 10 s (and the persistent storage's answers'), the login's result, the
    // reports once a minute while a game is reported.
    static void __cdecl StatsTask(Tsk::BaseTask* task);
    // 0x0054CAC0 (thread, stdcall): the stats SDK's game name and secret key (the online identity's - see
    // online_identity.h) and InitStatsConnection; state 2 (connected) or 3 (failed). Returns 0.
    static unsigned long __stdcall LogInThread(void* stats);
    // 0x0054CCF0: GetPersistDataValues' answer for index `index` (0 the rating, 1-14 a level's high score and combo;
    // after 14: script 0x955801C6, state 4).
    static void __cdecl OnPersonalStats(int localId, int profile, int type, int index, int success, uint32_t modified,
                                        char* data, int length, void* instance);
    // 0x0054DB20 (ghttp's completed callback of the top stats' download; param: the VaultMan): the file parsed into
    // its top list. Returns 1.
    static int __cdecl OnTopStats(void* request, int result, char* buffer, int64_t bufferLength, void* vault);
    // 0x0054D2F0 (stdcall): the name of level `level` from the level list 0xB8D3EEEC (null if none).
    static const char* __stdcall LevelName(uint32_t level);
    // 0x0054CA10 (stdcall): the SDK's GenerateAuth (0x00600640) for `challenge` and `password`.
    static void __stdcall GenerateAuth(const char* challenge, const char* password, char* response);

    Tsk::BaseTask* m_task;     // +0x000 Tsk::Task<StatsMan> (StatsTask)
    bool m_loggedIn;           // +0x004
    uint8_t m_pad5[3];
    int m_state;               // +0x008 State
    void* m_game;              // +0x00C the SDK's game being reported
    uint32_t m_lastReport;     // +0x010
    PersonalStats m_personal;  // +0x014
    int m_topFile;             // +0x090 the top-score file being downloaded (RetrieveTopStats)
    TopList m_topLists[5];     // +0x094 high scores all time, monthly, best combos all time, monthly; ratings
    bool m_needTopStats;       // +0x1FC
};
static_assert(offsetof(StatsMan, m_personal) == 0x14 &&
                  offsetof(StatsMan, m_topFile) == 0x90 && offsetof(StatsMan, m_topLists) == 0x94 &&
                  offsetof(StatsMan, m_needTopStats) == 0x1FC,
              "layout must match the original");

// The script commands.
bool __cdecl StatsLoggedIn(Script::CStruct* params, Script::CScript* script);        // 0x0054CC90
bool __cdecl StatsLogIn(Script::CStruct* params, Script::CScript* script);           // 0x0054CCB0
bool __cdecl StatsLogOff(Script::CStruct* params, Script::CScript* script);          // 0x0054CCC0
bool __cdecl RetrievePersonalStats(Script::CStruct* params, Script::CScript* script); // 0x0054CE00
bool __cdecl NeedToRetrieveTopStats(Script::CStruct* params, Script::CScript* script); // 0x0054CE70
bool __cdecl ReportStats(Script::CStruct* params, Script::CScript* script);          // 0x0054D670
bool __cdecl GetRank(Script::CStruct* params, Script::CScript* script);              // 0x0054D6A0
bool __cdecl CleanUpTopStats(Script::CStruct* params, Script::CScript* script);      // 0x0054EA40
bool __cdecl FillStatsArrays(Script::CStruct* params, Script::CScript* script);      // 0x0054EB00

extern float g_rankScale;

} // namespace GameNet
