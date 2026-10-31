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
#include "game/script/struct.h"
#include "game/skate/game_mode.h"
#include "game/skater/skater.h"

namespace Mdl {

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

Game::CGameMode* GameMode() { return Game::g_gameMode; }
GameNet::Manager* NetManager() { return GameNet::g_gameNetManager; }
bool KingOfTheHill() { return GameMode()->GetNameChecksum() == kNetKing || GameMode()->GetNameChecksum() == kKing; }

struct PlayerSearch {
    void** m_vtable = Mdl::kPlayerSearchVtable;
    void* m_node = nullptr;
};
GameNet::PlayerInfo* AsPlayer(void* player) { return static_cast<GameNet::PlayerInfo*>(player); }

// The ranking's list (Lst::Head<ScoreEntry>, the entries' vtable 0x0064CCF8): highest priority (score) first.
struct RankingList {
    Mem::ListNode m_head;
    RankingList()
    {
        m_head.vtable = GameNet::kScoreEntryVtable;
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
    static GameNet::ScoreEntry* After(Mem::ListNode* node)
    {
        Mem::ListNode* next = node->next;
        return next->data == reinterpret_cast<void*>(-1) ? nullptr : static_cast<GameNet::ScoreEntry*>(next->data);
    }
};

GameNet::ScoreEntry* NewEntry()
{
    auto* entry = static_cast<GameNet::ScoreEntry*>(Mem::Calloc(sizeof(GameNet::ScoreEntry)));
    if (!entry)
        return nullptr;
    entry->data = entry;
    entry->priority = 0;
    entry->next = entry;
    entry->prev = entry;
    entry->vtable = GameNet::kScoreEntryVtable;
    return entry;
}
void DeleteEntry(GameNet::ScoreEntry* entry)
{
    reinterpret_cast<void(__thiscall*)(GameNet::ScoreEntry*, int)>(entry->vtable[0])(entry, 1);
}

Script::CStruct* NewStructure()
{
    Script::CStruct* made = Script::NewStruct();
    return made ? made->Init() : nullptr;
}
void DeleteStructure(Script::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Script::DeleteStruct(structure);
    }
}

// A player's (or team's) line in the player list.
void AddPlayerLine(const char* text, bool teamScore)
{
    Script::CStruct* params = NewStructure();
    params->AddString(kText, text);
    if (teamScore)
        params->AddChecksum(0u, kTeamScore);
    params->AddChecksum(kParent, kPlayerListMenu);
    Script::RunScript(kPlayerMenuAddItem, params, nullptr, false, nullptr);
    DeleteStructure(params);
}

// Its score's line in the rankings list (a team's players': the font before the id) - the text written by `format`
// once the line's structure is made, as the original.
template <typename Format>
void AddScoreLine(Format format, uint32_t id, bool teamScore, bool fontFirst = false)
{
    Script::CStruct* params = NewStructure();
    char text[0x40];
    format(text, sizeof(text));
    params->AddString(kText, text);
    if (fontFirst)
        params->AddChecksum(kFont, kDialog);
    params->AddChecksum(kId, id);
    if (!fontFirst)
        params->AddChecksum(kFont, kDialog);
    if (teamScore)
        params->AddChecksum(0u, kTeamScore);
    params->AddChecksum(kParent, kRankingsListMenu);
    Script::RunScript(kScoreMenuAddItem, params, nullptr, false, nullptr);
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
    GameNet::Manager* net = NetManager();
    PlayerSearch search;
    RankingList list;
    Mem::PushContext(Mem::Manager::Get()->m_frontEndHeap);
    bool resetScores = GameMode()->IsTrue(kShouldResetScores);
    for (void* player = net->FirstPlayerInfo(&search, false); player; player = net->NextPlayerInfo(&search, false)) {
        GameNet::PlayerInfo* info = AsPlayer(player);
        GameNet::ScoreEntry* entry = NewEntry();
        const char* name = info->m_skater->GetDisplayName();
        char* out = entry->m_text;
        while ((*out++ = *name++) != 0) {
        }
        entry->m_teamScore = resetScores ? info->GetAccumulatedScore() : net->GetPlayerScore(info->m_skater->m_id);
        entry->m_colour = static_cast<uint8_t>(GameMode()->IsTeamGame()) ? info->m_team + 2 : 0;
        entry->priority = info->m_eliminatedTime ? static_cast<int>(info->m_eliminatedTime) : 100000;
        reinterpret_cast<Lst::Head*>(&list.m_head)->AddNode(entry); // (inlined there)
    }
    uint32_t id = kFirstLine;
    for (GameNet::ScoreEntry* entry = RankingList::After(&list.m_head); entry; ++id) {
        GameNet::ScoreEntry* next = RankingList::After(entry);
        char text[0x40];
        std::snprintf(text, sizeof(text), "\\c%d%s", entry->m_colour, entry->m_text); // "\c%d%s"
        AddPlayerLine(text, false);
        AddScoreLine([&](char* score, size_t size) {
            std::snprintf(score, size, "%d", entry->m_teamScore); // "%d"
        }, id, false);
        DeleteEntry(entry);
        entry = next;
    }
    Mem::PopContext();
}

// 0x00568120
bool __cdecl Skate::CFunc_FillRankingScreen(Script::CStruct*, Script::CScript*)
{
    GameNet::Manager* net = NetManager();
    PlayerSearch search;
    RankingList list;
    if (GameMode()->GetNameChecksum() == kElimiskate || GameMode()->GetNameChecksum() == kNetElimiskate) {
        FillEliminationRanking();
        return true;
    }
    Mem::PushContext(Mem::Manager::Get()->m_frontEndHeap);
    bool goalAttack = GameMode()->GetNameChecksum() == kNetGoalAttack;
    // A player's score: the goals beaten in goal attack.
    auto scoreOf = [&](GameNet::PlayerInfo* info) -> uint32_t {
        if (goalAttack)
            return static_cast<uint32_t>(Game::GetGoalManager()->NumGoalsBeatenBy(static_cast<int>(info->m_skater->m_id)));
        return static_cast<uint32_t>(net->GetPlayerScore(info->m_skater->m_id));
    };
    if (static_cast<uint8_t>(GameMode()->IsTeamGame())) {
        // The teams with players, by their scores; under each its players, then its total.
        for (int team = 0; team < GameMode()->NumTeams(); ++team) {
            if (!net->CountTeamPlayers(team))
                continue;
            GameNet::ScoreEntry* entry = NewEntry();
            entry->m_king = false;
            entry->m_colour = team + 2;
            entry->m_unknown44 = static_cast<uint32_t>(team);
            entry->m_teamScore = net->GetTeamScore(team);
            char name[0x40];
            std::snprintf(name, sizeof(name), "team_%d_name", team + 1); // "team_%d_name"
            const char* total = Script::GetString("total_str"); // "total_str"
            const char* teamName = Script::GetString(name);
            std::snprintf(entry->m_text, sizeof(GameNet::ScoreEntry) - offsetof(GameNet::ScoreEntry, m_text),
                          "\\c%d%s %s", entry->m_colour, teamName, total); // "\c%d%s %s" (unbounded there)
            entry->priority = net->GetTeamScore(team);
            reinterpret_cast<Lst::Head*>(&list.m_head)->AddNode(entry);
        }
        uint32_t line = 0;
        for (GameNet::ScoreEntry* entry = RankingList::After(&list.m_head); entry;) {
            GameNet::ScoreEntry* next = RankingList::After(entry);
            for (void* player = net->FirstPlayerInfo(&search, false); player; player = net->NextPlayerInfo(&search, false)) {
                GameNet::PlayerInfo* info = AsPlayer(player);
                if (static_cast<uint32_t>(info->m_team) != entry->m_unknown44)
                    continue;
                uint32_t score = scoreOf(info);
                char text[0x40];
                std::snprintf(text, sizeof(text),
                              (static_cast<uint8_t>(info->IsLocalPlayer()) ? "\\c%d> %s" : "\\c%d%s"), // "\c%d> %s" / "\c%d%s"
                              entry->m_colour, info->m_name);
                AddPlayerLine(text, false);
                AddScoreLine([&](char* shown, size_t size) { FormatScore(shown, size, score, 0, false); },
                             kFirstLine + line, false, true);
                ++line;
            }
            AddPlayerLine(entry->m_text, true);
            AddScoreLine([&](char* total, size_t size) {
                FormatScore(total, size, static_cast<uint32_t>(entry->priority), entry->m_colour, true);
            }, kFirstLine + line, true);
            DeleteEntry(entry);
            ++line;
            entry = next;
        }
    } else {
        for (void* player = net->FirstPlayerInfo(&search, false); player; player = net->NextPlayerInfo(&search, false)) {
            GameNet::PlayerInfo* info = AsPlayer(player);
            uint32_t score = scoreOf(info);
            GameNet::ScoreEntry* entry = NewEntry();
            const char* name = info->m_skater->GetDisplayName();
            char* out = entry->m_text;
            while ((*out++ = *name++) != 0) {
            }
            entry->priority = static_cast<int>(score);
            reinterpret_cast<Lst::Head*>(&list.m_head)->AddNode(entry);
        }
        uint32_t id = kFirstLine;
        for (GameNet::ScoreEntry* entry = RankingList::After(&list.m_head); entry; ++id) {
            GameNet::ScoreEntry* next = RankingList::After(entry);
            AddPlayerLine(entry->m_text, false);
            AddScoreLine([&](char* shown, size_t size) {
                FormatScore(shown, size, static_cast<uint32_t>(entry->priority), 0, false);
            }, id, false);
            DeleteEntry(entry);
            entry = next;
        }
    }
    Mem::PopContext();
    return true;
}

} // namespace Mdl
