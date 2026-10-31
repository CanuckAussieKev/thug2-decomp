// The end of a network game's rankings (FillRankingScreen, 0x00567C70 / 0x00568120): the players (or the teams and
// their players) sorted by score into the player_list_menu / rankings_list_menu, a line each (player_menu_add_item /
// score_menu_add_item; the score line's ids from 0x00039447 on).

#include "game/skate/skate_module.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/goals/goal_manager.h"
#include "game/net/gamenet_manager.h"
#include "game/net/player_info.h"
#include "game/net/score_list.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"
#include "game/skate/game_mode.h"
#include "game/skater/rider.h"

namespace GameModules {

namespace {

constexpr uint32_t kShouldResetScores = 0x0B803C5E;   // should_reset_scores
constexpr uint32_t kText = 0xC4745838;               // text
constexpr uint32_t kParent = 0xC2719FB0;             // parent
constexpr uint32_t kPlayerListMenu = 0x67382FBC;     // player_list_menu
constexpr uint32_t kPlayerMenuAddItem = 0x17AD3E33;  // player_menu_add_item
constexpr uint32_t kId = 0x40C698AF;                 // id
constexpr uint32_t kFont = 0x2F6BF72D;               // font
constexpr uint32_t kDialog = 0xBA9E279D;             // dialog
constexpr uint32_t kRankingsListMenu = 0x5E5BF0E6;   // rankings_list_menu
constexpr uint32_t kScoreMenuAddItem = 0x2C638965;   // score_menu_add_item
constexpr uint32_t kTeamScore = 0x0A6081EE;          // team_score
constexpr uint32_t kElimiskate = 0xF6198005;         // elimiskate
constexpr uint32_t kNetElimiskate = 0x10F84276;      // netelimiskate
constexpr uint32_t kNetGoalAttack = 0xEC200EAA;      // netgoalattack
constexpr uint32_t kNetKing = 0x6EF8FDA0;            // netking
constexpr uint32_t kKing = 0x5D32129C;               // king
constexpr uint32_t kFirstLine = 0x00039447;          // the first score line's id

GoalSystem::CMatchRules* GameMode() { return GoalSystem::g_gameMode; }
OnlinePlay::Hub* NetManager() { return OnlinePlay::g_gameNetManager; }
bool KingOfTheHill() { return GameMode()->GetNameKey() == kNetKing || GameMode()->GetNameKey() == kKing; }

struct PlayerSearch {
    void** m_vtable = GameModules::kPlayerSearchVtable;
    void* m_listLink = nullptr;
};
OnlinePlay::PlayerRecord* AsPlayer(void* player) { return static_cast<OnlinePlay::PlayerRecord*>(player); }

// The ranking's list (ListKit::Foremost<ScoreEntry>, the entries' vtable 0x0064CCF8): highest priority (score) first.
struct RankingList {
    MemSys::ListNode m_head;
    RankingList()
    {
        m_head.vtable = OnlinePlay::kScoreEntryVtable;
        m_head.data = reinterpret_cast<void*>(-1);
        m_head.priority = 0;
        m_head.next = &m_head;
        m_head.prev = &m_head;
    }
    ~RankingList()
    {
        m_head.prev->next = m_head.next; // (the head out of the list: as its destructor)
        m_head.next->prev = m_head.prev;
    }
    // The entry after `node` (null at the head).
    static OnlinePlay::ScoreEntry* After(MemSys::ListNode* node)
    {
        MemSys::ListNode* next = node->next;
        return next->data == reinterpret_cast<void*>(-1) ? nullptr : static_cast<OnlinePlay::ScoreEntry*>(next->data);
    }
};

OnlinePlay::ScoreEntry* NewEntry()
{
    auto* entry = static_cast<OnlinePlay::ScoreEntry*>(MemSys::AllocZeroed(sizeof(OnlinePlay::ScoreEntry)));
    if (!entry)
        return nullptr;
    entry->data = entry;
    entry->priority = 0;
    entry->next = entry;
    entry->prev = entry;
    entry->vtable = OnlinePlay::kScoreEntryVtable;
    return entry;
}
void DeleteEntry(OnlinePlay::ScoreEntry* entry)
{
    reinterpret_cast<void(__thiscall*)(OnlinePlay::ScoreEntry*, int)>(entry->vtable[0])(entry, 1);
}

Scripts::CStruct* NewStructure()
{
    Scripts::CStruct* made = Scripts::NewStruct();
    return made ? made->PrepareAll() : nullptr;
}
void DeleteStructure(Scripts::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Scripts::DeleteStruct(structure);
    }
}

// A player's (or team's) line in the player list.
void AddPlayerLine(const char* text, bool teamScore)
{
    Scripts::CStruct* params = NewStructure();
    params->AppendWord(kText, text);
    if (teamScore)
        params->PutNameKey(0u, kTeamScore);
    params->PutNameKey(kParent, kPlayerListMenu);
    Scripts::LaunchScript(kPlayerMenuAddItem, params, nullptr, false, nullptr);
    DeleteStructure(params);
}

// Its score's line in the rankings list (a team's players': the font before the id) - the text written by `format`
// once the line's structure is made, as the original.
template <typename Format>
void AddScoreLine(Format format, uint32_t id, bool teamScore, bool fontFirst = false)
{
    Scripts::CStruct* params = NewStructure();
    char text[0x40];
    format(text, sizeof(text));
    params->AppendWord(kText, text);
    if (fontFirst)
        params->PutNameKey(kFont, kDialog);
    params->PutNameKey(kId, id);
    if (!fontFirst)
        params->PutNameKey(kFont, kDialog);
    if (teamScore)
        params->PutNameKey(0u, kTeamScore);
    params->PutNameKey(kParent, kRankingsListMenu);
    Scripts::LaunchScript(kScoreMenuAddItem, params, nullptr, false, nullptr);
    DeleteStructure(params);
}

// A score as shown: a time (minutes:seconds) in king of the hill, else the number.
void FormatScore(char* out, size_t size, uint32_t score, int colour, bool coloured)
{
    if (KingOfTheHill()) {
        uint32_t seconds = score / 1000;
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
void __cdecl FillEliminationRanking()
{
    OnlinePlay::Hub* net = NetManager();
    PlayerSearch search;
    RankingList list;
    MemSys::EnterHeapScope(MemSys::Hub::Pull()->m_frontEndHeap);
    bool resetScores = GameMode()->IsTrue(kShouldResetScores);
    for (void* player = net->BeginPlayerWalk(&search, false); player; player = net->StepPlayerWalk(&search, false)) {
        OnlinePlay::PlayerRecord* info = AsPlayer(player);
        OnlinePlay::ScoreEntry* entry = NewEntry();
        const char* name = info->m_skater->FetchShownName();
        char* out = entry->m_text;
        while ((*out++ = *name++) != 0) {
        }
        entry->m_teamScore = resetScores ? info->GetAccumulatedScore() : net->GetEntrantPoints(info->m_skater->m_ident);
        entry->m_shade = static_cast<uint8_t>(GameMode()->IsSquadMatch()) ? info->m_team + 2 : 0;
        entry->priority = info->m_eliminatedTime ? static_cast<int>(info->m_eliminatedTime) : 100000;
        reinterpret_cast<ListKit::Foremost*>(&list.m_head)->AppendVertex(entry); // (inlined there)
    }
    uint32_t id = kFirstLine;
    for (OnlinePlay::ScoreEntry* entry = RankingList::After(&list.m_head); entry; ++id) {
        OnlinePlay::ScoreEntry* next = RankingList::After(entry);
        char text[0x40];
        std::snprintf(text, sizeof(text), "\\c%d%s", entry->m_shade, entry->m_text); // "\c%d%s"
        AddPlayerLine(text, false);
        AddScoreLine([&](char* score, size_t size) {
            std::snprintf(score, size, "%d", entry->m_teamScore); // "%d"
        }, id, false);
        DeleteEntry(entry);
        entry = next;
    }
    MemSys::LeaveHeapScope();
}

// 0x00568120
bool __cdecl Skate::CFunc_FillRankingScreen(Scripts::CStruct*, Scripts::CScript*)
{
    OnlinePlay::Hub* net = NetManager();
    PlayerSearch search;
    RankingList list;
    if (GameMode()->GetNameKey() == kElimiskate || GameMode()->GetNameKey() == kNetElimiskate) {
        FillEliminationRanking();
        return true;
    }
    MemSys::EnterHeapScope(MemSys::Hub::Pull()->m_frontEndHeap);
    bool goalAttack = GameMode()->GetNameKey() == kNetGoalAttack;
    // A player's score: the goals beaten in goal attack.
    auto scoreOf = [&](OnlinePlay::PlayerRecord* info) -> uint32_t {
        if (goalAttack)
            return static_cast<uint32_t>(GoalSystem::GetObjectiveBoard()->CountClearedByPlayer(static_cast<int>(info->m_skater->m_ident)));
        return static_cast<uint32_t>(net->GetEntrantPoints(info->m_skater->m_ident));
    };
    if (static_cast<uint8_t>(GameMode()->IsSquadMatch())) {
        // The teams with players, by their scores; under each its players, then its total.
        for (int team = 0; team < GameMode()->NumTeams(); ++team) {
            if (!net->CountTeamPlayers(team))
                continue;
            OnlinePlay::ScoreEntry* entry = NewEntry();
            entry->m_king = false;
            entry->m_shade = team + 2;
            entry->m_unknown44 = static_cast<uint32_t>(team);
            entry->m_teamScore = net->GetSquadPoints(team);
            char name[0x40];
            std::snprintf(name, sizeof(name), "team_%d_name", team + 1); // "team_%d_name"
            const char* total = Scripts::FetchText("total_str"); // "total_str"
            const char* teamName = Scripts::FetchText(name);
            std::snprintf(entry->m_text, sizeof(OnlinePlay::ScoreEntry) - offsetof(OnlinePlay::ScoreEntry, m_text),
                          "\\c%d%s %s", entry->m_shade, teamName, total); // "\c%d%s %s" (unbounded there)
            entry->priority = net->GetSquadPoints(team);
            reinterpret_cast<ListKit::Foremost*>(&list.m_head)->AppendVertex(entry);
        }
        uint32_t line = 0;
        for (OnlinePlay::ScoreEntry* entry = RankingList::After(&list.m_head); entry;) {
            OnlinePlay::ScoreEntry* next = RankingList::After(entry);
            for (void* player = net->BeginPlayerWalk(&search, false); player; player = net->StepPlayerWalk(&search, false)) {
                OnlinePlay::PlayerRecord* info = AsPlayer(player);
                if (static_cast<uint32_t>(info->m_team) != entry->m_unknown44)
                    continue;
                uint32_t score = scoreOf(info);
                char text[0x40];
                std::snprintf(text, sizeof(text),
                              (static_cast<uint8_t>(info->IsOwnPlayer()) ? "\\c%d> %s" : "\\c%d%s"), // "\c%d> %s" / "\c%d%s"
                              entry->m_shade, info->m_label);
                AddPlayerLine(text, false);
                AddScoreLine([&](char* shown, size_t size) { FormatScore(shown, size, score, 0, false); },
                             kFirstLine + line, false, true);
                ++line;
            }
            AddPlayerLine(entry->m_text, true);
            AddScoreLine([&](char* total, size_t size) {
                FormatScore(total, size, static_cast<uint32_t>(entry->priority), entry->m_shade, true);
            }, kFirstLine + line, true);
            DeleteEntry(entry);
            ++line;
            entry = next;
        }
    } else {
        for (void* player = net->BeginPlayerWalk(&search, false); player; player = net->StepPlayerWalk(&search, false)) {
            OnlinePlay::PlayerRecord* info = AsPlayer(player);
            uint32_t score = scoreOf(info);
            OnlinePlay::ScoreEntry* entry = NewEntry();
            const char* name = info->m_skater->FetchShownName();
            char* out = entry->m_text;
            while ((*out++ = *name++) != 0) {
            }
            entry->priority = static_cast<int>(score);
            reinterpret_cast<ListKit::Foremost*>(&list.m_head)->AppendVertex(entry);
        }
        uint32_t id = kFirstLine;
        for (OnlinePlay::ScoreEntry* entry = RankingList::After(&list.m_head); entry; ++id) {
            OnlinePlay::ScoreEntry* next = RankingList::After(entry);
            AddPlayerLine(entry->m_text, false);
            AddScoreLine([&](char* shown, size_t size) {
                FormatScore(shown, size, static_cast<uint32_t>(entry->priority), 0, false);
            }, id, false);
            DeleteEntry(entry);
            entry = next;
        }
    }
    MemSys::LeaveHeapScope();
    return true;
}

} // namespace Mdl
