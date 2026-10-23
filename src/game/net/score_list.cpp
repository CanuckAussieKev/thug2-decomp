// The network game's score list (see score_list.h) and the manager's score queries.

#include "game/net/score_list.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/front/screen_element.h"
#include "game/lst/head.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_manager.h"
#include "game/net/player_info.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

#pragma warning(disable : 4996) // (the original's sprintf, kept)

namespace GameNet {

using namespace Detail;

namespace {

// Names.
constexpr uint32_t kSkaterScore = 0xD262802F;      // SkaterScore (the component)
constexpr uint32_t kNetLobby = 0x1C471C60;         // netlobby
constexpr uint32_t kNetKing = 0x6EF8FDA0;          // netking
constexpr uint32_t kKing = 0x5D32129C;             // king
constexpr uint32_t kNetScavengerHunt = 0xFC13B531; // netscavengerhunt
constexpr uint32_t kNetGoalAttack = 0xEC200EAA;    // netgoalattack
constexpr uint32_t kNetFirefight = 0xBFF33600;     // netfirefight
constexpr uint32_t kScavenger = 0x1F3430CE;        // scavenger (the goal)
constexpr uint32_t kClearScores = 0x7084400A;      // clear_scores
constexpr uint32_t kUpdateScore = 0xAD022A2C;      // update_score
constexpr uint32_t kKingAbbreviation = 0xA1DD0949; // player_str_king_abbreviation
constexpr uint32_t kText = 0xC4745838;             // text
constexpr uint32_t kId = 0x40C698AF;               // id
constexpr uint32_t kTeamNames[4] = {0x3224348D, 0xB4B04623, 0x7FEC9586, 0x62E9A53E}; // team_1_name ... team_4_name
constexpr uint32_t kScoreLines[8] = {0x2B083809, 0xB20169B3, 0xC5065925, 0x5B62CC86,  // net_score_1 ... 4
                                     0x2C65FC10, 0xB56CADAA, 0xC26B9D3C, 0x52D480AD}; // net_score_5 ... 8

constexpr uint32_t kUpdateInterval = 1000; // ms
inline void** const kScoreEntryVtable = reinterpret_cast<void**>(0x0064CCF8);

uint32_t& LastScoreUpdate() { return GameVariable<uint32_t>(0x007CE43C); }

// A player's SkaterScore's score object (no null checks: as the original).
void* ScoreOf(PlayerInfo* player)
{
    auto* skater = reinterpret_cast<Obj::CCompositeObject*>(player->m_skater);
    return Field<void*>(skater->GetComponent(kSkaterScore), 0x18);
}
int PointsOf(void* score) { return Call<int>(0x00561B40, score); }

PlayerInfo* First(Manager* manager, PlayerSearch* search)
{
    return reinterpret_cast<PlayerInfo*>(FirstPlayer(manager, search, false));
}
PlayerInfo* Next(Manager* manager, PlayerSearch* search)
{
    return reinterpret_cast<PlayerInfo*>(NextPlayer(manager, search, false));
}

// A player that gets a score line: fully in (or local), not eliminated, and in netfirefight not out of lives.
bool HasScoreLine(PlayerInfo* p)
{
    if (!(p->m_flags & (kPlayerFullyIn | kPlayerLocal)) || (p->m_flags & kPlayerEliminated))
        return false;
    if (GameType() != kNetFirefight || !p->m_skater)
        return true;
    Obj::CBaseComponent* score = reinterpret_cast<Obj::CCompositeObject*>(p->m_skater)->GetComponent(kSkaterScore);
    return !score || PointsOf(Field<void*>(score, 0x18)) != 0;
}

// A new line (null if out of memory: then used anyway, as the original).
ScoreEntry* NewEntry()
{
    auto* entry = static_cast<ScoreEntry*>(Mem::Calloc(sizeof(ScoreEntry)));
    if (entry) {
        entry->data = entry;
        entry->priority = 0;
        entry->next = entry;
        entry->prev = entry;
        entry->vtable = kScoreEntryVtable;
    }
    return entry;
}

// The lowest team whose flag the player carries (-1: none - a shift by 31 then, as the original's).
uint32_t FlagBit(PlayerInfo* p) { return 1u << (static_cast<uint32_t>(p->GetFlagTeam()) & 31); }

// At the end of the list.
void Append(Lst::Head* head, ScoreEntry* entry)
{
    entry->prev = head->m_node.prev;
    entry->next = &head->m_node;
    head->m_node.prev->next = entry;
    head->m_node.prev = entry;
}

} // namespace

// 0x005435F0
int Manager::GetTeamScore(int team)
{
    PlayerSearch search;
    int total = 0;
    PlayerInfo* local = reinterpret_cast<PlayerInfo*>(LocalPlayer(this));
    bool goalAttack = GameType() == kNetGoalAttack;
    for (PlayerInfo* p = First(this, &search); p; p = Next(this, &search)) {
        if (!(p->m_flags & (kPlayerFullyIn | kPlayerLocal)) || p->m_team != team)
            continue;
        if (goalAttack && !(local->m_flags & kPlayerObserver))
            return Call<int>(0x00558B10, GoalManager(), team);
        total += PointsOf(ScoreOf(p));
    }
    return total;
}

// 0x005436E0
int Manager::GetPlayerScore(uint32_t id)
{
    PlayerInfo* local = reinterpret_cast<PlayerInfo*>(LocalPlayer(this));
    if (GameType() == kNetGoalAttack && !(local->m_flags & kPlayerObserver))
        return Call<int>(0x00558AB0, GoalManager(), id);
    auto* player = reinterpret_cast<PlayerInfo*>(PlayerByObjectId(static_cast<uint16_t>(id)));
    return PointsOf(ScoreOf(player));
}

// 0x00544780
void __cdecl ScoreUpdateTask(void* task)
{
    void* elements = GameVariable<void*>(0x00701440); // (the screen element manager: unused by the lookup)
    (void)elements;
    Manager* manager = Field<Manager*>(task, 0x1C);
    Lst::Head list;
    list.m_node.vtable = kScoreEntryVtable;
    list.m_node.data = reinterpret_cast<void*>(-1);
    list.m_node.priority = 0;
    list.m_node.next = &list.m_node;
    list.m_node.prev = &list.m_node;
    PlayerSearch search;
    bool notLobby = GameType() != kNetLobby;
    bool kingOfTheHill = GameType() == kNetKing || GameType() == kKing;
    bool scavengerHunt = false;
    if (GameType() == kNetScavengerHunt) {
        uint8_t* goal = Call<uint8_t*>(0x00557F90, GoalManager(), kScavenger, 0);
        if (goal && reinterpret_cast<bool(__thiscall*)(void*)>((*reinterpret_cast<void***>(goal))[3])(goal))
            scavengerHunt = true; // (the goal is active)
    }
    uint32_t now = Tmr::GetTime();
    if (now - LastScoreUpdate() < kUpdateInterval) {
        list.m_node.prev->next = list.m_node.next; // (the head's destructor)
        list.m_node.next->prev = list.m_node.prev;
        return;
    }
    LastScoreUpdate() = now;
    Run(kClearScores);

    if (notLobby && manager->ShowTeamScores()) {
        // A line per team with players: the team's name, its players' marks.
        ScoreEntry* teams[4];
        for (int team = 0; team < Call<int>(0x00553130, GameMode()); ++team) {
            if (manager->CountTeamPlayers(team) <= 0)
                continue;
            uint32_t name = team < 4 ? kTeamNames[team] : 0;
            ScoreEntry* entry = NewEntry();
            teams[team] = entry;
            std::strcpy(entry->m_text, Script::GetString(name));
            entry->m_king = false;
            entry->m_local = false;
            entry->m_hasFlag = false;
            entry->m_flags = 0;
            entry->m_colour = team + 2;
            entry->m_teamScore = manager->GetTeamScore(team);
        }
        for (PlayerInfo* p = First(manager, &search); p; p = Next(manager, &search)) {
            if (!HasScoreLine(p))
                continue;
            if (p->m_flags & kPlayerLocal) {
                char marked[64];
                std::sprintf(marked, "> %s", teams[p->m_team]->m_text);
                std::strcpy(teams[p->m_team]->m_text, marked);
            }
            if (p->m_flags & kPlayerKing)
                teams[p->m_team]->m_king = true;
            if (p->m_flags & kPlayerLocal)
                teams[p->m_team]->m_local = true;
            if (p->m_flags & kPlayerCarryingFlags) {
                teams[p->m_team]->m_hasFlag = true;
                teams[p->m_team]->m_flags |= FlagBit(p);
            }
        }
        for (int team = 0; team < Call<int>(0x00553130, GameMode()); ++team) {
            if (manager->CountTeamPlayers(team) <= 0)
                continue;
            ScoreEntry* entry = teams[team];
            entry->priority = entry->m_teamScore;
            list.AddNode(entry); // (inlined in the original)
        }
    } else {
        // A line per player.
        for (PlayerInfo* p = First(manager, &search); p; p = Next(manager, &search)) {
            if (!HasScoreLine(p))
                continue;
            ScoreEntry* entry = NewEntry();
            if (p->m_flags & kPlayerLocal)
                std::sprintf(entry->m_text, "> %s", p->m_name);
            else
                std::strcpy(entry->m_text, p->m_name);
            entry->m_king = (p->m_flags >> 7) & 1;
            entry->m_local = p->m_flags & 1;
            if (IsTeamGame())
                entry->m_colour = p->m_team + 2;
            else
                entry->m_colour = static_cast<int>(SkaterId(p->m_skater)) + 2;
            if (p->m_flags & kPlayerCarryingFlags) {
                entry->m_hasFlag = true;
                entry->m_flags |= FlagBit(p);
            }
            if (!notLobby) {
                Append(&list, entry);
            } else {
                entry->priority = scavengerHunt ? 5 - p->m_unknown34 : manager->GetPlayerScore(SkaterId(p->m_skater));
                list.AddNode(entry);
            }
        }
    }

    // The lines, best first.
    Mem::ListNode* cursor = list.m_node.next->data == reinterpret_cast<void*>(-1) ? nullptr : list.m_node.next;
    auto* entry = cursor ? static_cast<ScoreEntry*>(cursor->data) : nullptr;
    for (int line = 0; entry; ++line) {
        char prefix[64]; // (16 bytes in the original: a long crown abbreviation overran it)
        prefix[0] = '\0';
        if (entry->m_king) {
            std::sprintf(prefix, Script::GetString(kKingAbbreviation)); // (the text is the format, as the original)
        } else if (entry->m_hasFlag) {
            if (entry->m_flags & 1)
                std::strcat(prefix, "\\s0");
            if (entry->m_flags & 2)
                std::strcat(prefix, "\\s1");
            if (entry->m_flags & 4)
                std::strcat(prefix, "\\s2");
            if (entry->m_flags & 8)
                std::strcat(prefix, "\\s3");
        } else {
            prefix[0] = '\0';
        }
        cursor = cursor->next->data == reinterpret_cast<void*>(-1) ? nullptr : cursor->next;
        auto* following = cursor ? static_cast<ScoreEntry*>(cursor->data) : nullptr;

        char text[128]; // (64 in the original: the longest king of the hill lines overran it)
        if (!notLobby) {
            std::sprintf(text, "\\c%d%s", entry->m_colour, entry->m_text);
        } else if (kingOfTheHill) {
            int seconds = static_cast<int>(static_cast<uint32_t>(entry->priority) / 1000);
            if (seconds < 3600)
                std::sprintf(text, "\\c4%s\\c%d %s \\c%d %d:%.2d", prefix, entry->m_colour, entry->m_text,
                             entry->m_colour, seconds / 60, seconds % 60);
            else
                std::sprintf(text, "\\c4%s\\c%d %s \\c%d %d:%.2d:%.2d", prefix, entry->m_colour, entry->m_text,
                             entry->m_colour, seconds / 3600, seconds % 3600 / 60, seconds % 60);
        } else if (scavengerHunt && !entry->m_local) {
            std::sprintf(text, "%s\\c%d %s", prefix, entry->m_colour, entry->m_text);
        } else {
            std::sprintf(text, "%s\\c%d %s \\c%d %d", prefix, entry->m_colour, entry->m_text, entry->m_colour,
                         entry->priority);
        }
        uint32_t id = line < 8 ? kScoreLines[line] : 0;
        Script::CStruct* params = NewParams();
        params->AddString(kText, Script::ESYMBOLTYPE_STRING, text);
        params->AddComponentOfType(kId, Script::ESYMBOLTYPE_NAME, id);
        Front::CScreenElement* element;
        {
            Front::ElementPtr found;
            Front::GetElement(&found, id);
            element = found.mp_object;
        }
        if (element) // (SetProperties, virtual slot 1)
            reinterpret_cast<void(__thiscall*)(void*, Script::CStruct*)>((*reinterpret_cast<void***>(element))[1])(
                element, params);
        Run(kUpdateScore, params);
        DeleteParams(params);
        reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(entry->vtable[0])(entry, 1); // (deleted: out of the list)
        entry = following;
    }
    list.m_node.prev->next = list.m_node.next; // (the head's destructor)
    list.m_node.next->prev = list.m_node.prev;
}

} // namespace GameNet

REPLACE(0x005435F0, &GameNet::Manager::GetTeamScore);
REPLACE(0x005436E0, &GameNet::Manager::GetPlayerScore);
REPLACE(0x00544780, GameNet::ScoreUpdateTask);
