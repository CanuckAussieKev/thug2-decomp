// The network game's score list (see score_list.h) and the manager's score queries.

#include "game/net/score_list.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/front/screen_element.h"
#include "game/lst/chain_start.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_manager.h"
#include "game/net/player_info.h"
#include "game/obj/entity.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"
#include "game/sys/stopwatch.h"
#include "game/core/code_address.h"
#include "game/front/screen_element_manager.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/point_tally.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "game/gfx/face_texture.h"
#include "game/gfx/rig.h"
#include "game/ed/goal_editor.h"
#include "game/ed/rail_editor.h"
#include "game/net/server_list.h"
#include "game/net/buddy_man.h"
#include "game/net/stats_man.h"

#pragma warning(disable : 4996) // (the original's sprintf, kept)

namespace OnlinePlay {

static VtableSlot kScoreEntryVtable_slots[] = {&OnlinePlay::ScoreNode::DestroyCalling};
void** const kScoreEntryVtable = reinterpret_cast<void**>(kScoreEntryVtable_slots);

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
uint32_t g_lastScoreUpdate;

uint32_t& LastScoreUpdate() { return g_lastScoreUpdate; }

// A player's SkaterScore's score object (no null checks: as the original).
void* ScoreOf(PlayerRecord* player)
{
    auto* skater = reinterpret_cast<GameObjects::CGameEntity*>(player->m_skater);
    return Prop<void*>(skater->FetchModule(kSkaterScore), 0x18);
}
int PointsOf(void* score) { return Call<int>(CodeAddress(&GameModules::PointTally::GetTotalScore), score); }

PlayerRecord* First(Hub* manager, PlayerSearch* search)
{
    return reinterpret_cast<PlayerRecord*>(FirstPlayer(manager, search, false));
}
PlayerRecord* Next(Hub* manager, PlayerSearch* search)
{
    return reinterpret_cast<PlayerRecord*>(NextPlayer(manager, search, false));
}

// A player that gets a score line: fully in (or local), not eliminated, and in netfirefight not out of lives.
bool HasScoreLine(PlayerRecord* p)
{
    if (!(p->m_flagBits & (kPlayerFullyIn | kPlayerLocal)) || (p->m_flagBits & kPlayerEliminated))
        return false;
    if (GameType() != kNetFirefight || !p->m_skater)
        return true;
    GameObjects::CObjectModule* score = reinterpret_cast<GameObjects::CGameEntity*>(p->m_skater)->FetchModule(kSkaterScore);
    return !score || PointsOf(Prop<void*>(score, 0x18)) != 0;
}

// A new line (null if out of memory: then used anyway, as the original).
ScoreEntry* NewEntry()
{
    auto* entry = static_cast<ScoreEntry*>(MemSys::AllocZeroed(sizeof(ScoreEntry)));
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
uint32_t FlagBit(PlayerRecord* p) { return 1u << (static_cast<uint32_t>(p->GetFlagTeam()) & 31); }

// At the end of the list.
void Append(ListKit::Foremost* head, ScoreEntry* entry)
{
    entry->prev = head->m_listLink.prev;
    entry->next = &head->m_listLink;
    head->m_listLink.prev->next = entry;
    head->m_listLink.prev = entry;
}

} // namespace

// 0x005435F0
int Hub::GetSquadPoints(int team)
{
    PlayerSearch search;
    int total = 0;
    PlayerRecord* local = reinterpret_cast<PlayerRecord*>(LocalPlayer(this));
    bool goalAttack = GameType() == kNetGoalAttack;
    for (PlayerRecord* p = First(this, &search); p; p = Next(this, &search)) {
        if (!(p->m_flagBits & (kPlayerFullyIn | kPlayerLocal)) || p->m_team != team)
            continue;
        if (goalAttack && !(local->m_flagBits & kPlayerObserver))
            return Call<int>(CodeAddress(&GoalSystem::CGoalManager::NumGoalsNetBeatenBy), ObjectiveBoard(), team);
        total += PointsOf(ScoreOf(p));
    }
    return total;
}

// 0x005436E0
int Hub::GetEntrantPoints(uint32_t id)
{
    PlayerRecord* local = reinterpret_cast<PlayerRecord*>(LocalPlayer(this));
    if (GameType() == kNetGoalAttack && !(local->m_flagBits & kPlayerObserver))
        return Call<int>(CodeAddress(&GoalSystem::CGoalManager::CountClearedByPlayer), ObjectiveBoard(), id);
    auto* player = reinterpret_cast<PlayerRecord*>(PlayerByObjectId(static_cast<uint16_t>(id)));
    return PointsOf(ScoreOf(player));
}

// 0x00544780
void __cdecl ScoreUpdateTask(void* task)
{
    void* elements = MenuSystem::g_screenElementManager; // (the screen element manager: unused by the lookup)
    (void)elements;
    Hub* manager = Prop<Hub*>(task, 0x1C);
    ListKit::Foremost list;
    list.m_listLink.vtable = kScoreEntryVtable;
    list.m_listLink.data = reinterpret_cast<void*>(-1);
    list.m_listLink.priority = 0;
    list.m_listLink.next = &list.m_listLink;
    list.m_listLink.prev = &list.m_listLink;
    PlayerSearch search;
    bool notLobby = GameType() != kNetLobby;
    bool kingOfTheHill = GameType() == kNetKing || GameType() == kKing;
    bool scavengerHunt = false;
    if (GameType() == kNetScavengerHunt) {
        uint8_t* goal = Call<uint8_t*>(CodeAddress(&GoalSystem::CGoalManager::GetGoal), ObjectiveBoard(), kScavenger, 0);
        if (goal && reinterpret_cast<bool(__thiscall*)(void*)>((*reinterpret_cast<void***>(goal))[3])(goal))
            scavengerHunt = true; // (the goal is active)
    }
    uint32_t now = FrameClock::GetTime();
    if (now - LastScoreUpdate() < kUpdateInterval) {
        list.m_listLink.prev->next = list.m_listLink.next; // (the head's destructor)
        list.m_listLink.next->prev = list.m_listLink.prev;
        return;
    }
    LastScoreUpdate() = now;
    Run(kClearScores);

    if (notLobby && manager->ShowTeamScores()) {
        // A line per team with players: the team's name, its players' marks.
        ScoreEntry* teams[4];
        for (int team = 0; team < Call<int>(CodeAddress(&GoalSystem::CMatchRules::NumTeams), GameMode()); ++team) {
            if (manager->CountTeamPlayers(team) <= 0)
                continue;
            uint32_t name = team < 4 ? kTeamNames[team] : 0;
            ScoreEntry* entry = NewEntry();
            teams[team] = entry;
            std::strcpy(entry->m_text, Scripts::FetchText(name));
            entry->m_king = false;
            entry->m_local = false;
            entry->m_hasFlag = false;
            entry->m_flagBits = 0;
            entry->m_shade = team + 2;
            entry->m_teamScore = manager->GetSquadPoints(team);
        }
        for (PlayerRecord* p = First(manager, &search); p; p = Next(manager, &search)) {
            if (!HasScoreLine(p))
                continue;
            if (p->m_flagBits & kPlayerLocal) {
                char marked[64];
                std::sprintf(marked, "> %s", teams[p->m_team]->m_text);
                std::strcpy(teams[p->m_team]->m_text, marked);
            }
            if (p->m_flagBits & kPlayerKing)
                teams[p->m_team]->m_king = true;
            if (p->m_flagBits & kPlayerLocal)
                teams[p->m_team]->m_local = true;
            if (p->m_flagBits & kPlayerCarryingFlags) {
                teams[p->m_team]->m_hasFlag = true;
                teams[p->m_team]->m_flagBits |= FlagBit(p);
            }
        }
        for (int team = 0; team < Call<int>(CodeAddress(&GoalSystem::CMatchRules::NumTeams), GameMode()); ++team) {
            if (manager->CountTeamPlayers(team) <= 0)
                continue;
            ScoreEntry* entry = teams[team];
            entry->priority = entry->m_teamScore;
            list.AppendVertex(entry); // (inlined in the original)
        }
    } else {
        // A line per player.
        for (PlayerRecord* p = First(manager, &search); p; p = Next(manager, &search)) {
            if (!HasScoreLine(p))
                continue;
            ScoreEntry* entry = NewEntry();
            if (p->m_flagBits & kPlayerLocal)
                std::sprintf(entry->m_text, "> %s", p->m_label);
            else
                std::strcpy(entry->m_text, p->m_label);
            entry->m_king = (p->m_flagBits >> 7) & 1;
            entry->m_local = p->m_flagBits & 1;
            if (IsSquadMatch())
                entry->m_shade = p->m_team + 2;
            else
                entry->m_shade = static_cast<int>(SkaterId(p->m_skater)) + 2;
            if (p->m_flagBits & kPlayerCarryingFlags) {
                entry->m_hasFlag = true;
                entry->m_flagBits |= FlagBit(p);
            }
            if (!notLobby) {
                Append(&list, entry);
            } else {
                entry->priority = scavengerHunt ? 5 - p->m_unknown34 : manager->GetEntrantPoints(SkaterId(p->m_skater));
                list.AppendVertex(entry);
            }
        }
    }

    // The lines, best first.
    MemSys::ListNode* cursor = list.m_listLink.next->data == reinterpret_cast<void*>(-1) ? nullptr : list.m_listLink.next;
    auto* entry = cursor ? static_cast<ScoreEntry*>(cursor->data) : nullptr;
    for (int line = 0; entry; ++line) {
        char prefix[64]; // (16 bytes in the original: a long crown abbreviation overran it)
        prefix[0] = '\0';
        if (entry->m_king) {
            std::sprintf(prefix, Scripts::FetchText(kKingAbbreviation)); // (the text is the format, as the original)
        } else if (entry->m_hasFlag) {
            if (entry->m_flagBits & 1)
                std::strcat(prefix, "\\s0");
            if (entry->m_flagBits & 2)
                std::strcat(prefix, "\\s1");
            if (entry->m_flagBits & 4)
                std::strcat(prefix, "\\s2");
            if (entry->m_flagBits & 8)
                std::strcat(prefix, "\\s3");
        } else {
            prefix[0] = '\0';
        }
        cursor = cursor->next->data == reinterpret_cast<void*>(-1) ? nullptr : cursor->next;
        auto* following = cursor ? static_cast<ScoreEntry*>(cursor->data) : nullptr;

        char text[128]; // (64 in the original: the longest king of the hill lines overran it)
        if (!notLobby) {
            std::sprintf(text, "\\c%d%s", entry->m_shade, entry->m_text);
        } else if (kingOfTheHill) {
            int seconds = static_cast<int>(static_cast<uint32_t>(entry->priority) / 1000);
            if (seconds < 3600)
                std::sprintf(text, "\\c4%s\\c%d %s \\c%d %d:%.2d", prefix, entry->m_shade, entry->m_text,
                             entry->m_shade, seconds / 60, seconds % 60);
            else
                std::sprintf(text, "\\c4%s\\c%d %s \\c%d %d:%.2d:%.2d", prefix, entry->m_shade, entry->m_text,
                             entry->m_shade, seconds / 3600, seconds % 3600 / 60, seconds % 60);
        } else if (scavengerHunt && !entry->m_local) {
            std::sprintf(text, "%s\\c%d %s", prefix, entry->m_shade, entry->m_text);
        } else {
            std::sprintf(text, "%s\\c%d %s \\c%d %d", prefix, entry->m_shade, entry->m_text, entry->m_shade,
                         entry->priority);
        }
        uint32_t id = line < 8 ? kScoreLines[line] : 0;
        Scripts::CStruct* params = NewParams();
        params->AppendWord(kText, Scripts::QBVAL_STRING, text);
        params->AddComponentOfType(kId, Scripts::QBVAL_NAME, id);
        MenuSystem::CUiWidget* element;
        {
            MenuSystem::ElementPtr found;
            MenuSystem::FetchPart(&found, id);
            element = found.m_ownerObject;
        }
        if (element) // (ChangeTraits, virtual slot 1)
            reinterpret_cast<void(__thiscall*)(void*, Scripts::CStruct*)>((*reinterpret_cast<void***>(element))[1])(
                element, params);
        Run(kUpdateScore, params);
        DeleteParams(params);
        reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(entry->vtable[0])(entry, 1); // (deleted: out of the list)
        entry = following;
    }
    list.m_listLink.prev->next = list.m_listLink.next; // (the head's destructor)
    list.m_listLink.next->prev = list.m_listLink.prev;
}

} // namespace OnlinePlay
