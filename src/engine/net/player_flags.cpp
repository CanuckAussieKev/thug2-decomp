// The network game's announcements about a player: fully in, capture the flag, king of the hill (see player_info.h).

#include <cstdint>
#include <cstdio>

#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_conn.h"
#include "engine/net/player_info.h"
#include "engine/objects/game_objects.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/rider/rider.h"
#include "platform/stopwatch.h"
#include "engine/core/code_address.h"
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
#include "engine/net/score_list.h"
#include "engine/net/buddy_man.h"
#include "engine/net/stats_man.h"

#pragma warning(disable : 4996) // (the original's sprintf, kept)

namespace OnlinePlay {

using namespace Detail;

namespace {

// Names.
constexpr uint32_t kSkaterScore = 0xD262802F;    // SkaterScore (the component)
constexpr uint32_t kNetFirefight = 0xBFF33600;   // netfirefight
constexpr uint32_t kNetKing = 0x6EF8FDA0;        // netking
constexpr uint32_t kKing = 0x5D32129C;           // king
constexpr uint32_t kString0 = 0xA4B08520;        // String0
constexpr uint32_t kString1 = 0xD3B7B5B6;        // String1
constexpr uint32_t kTeam = 0x3B1F59E0;           // team
constexpr uint32_t kPlayer1 = 0x7A813378;        // player_1
constexpr uint32_t kPlayer2 = 0xE38862C2;        // player_2
constexpr uint32_t kHideCtfArrow = 0x1C318F8C;   // hide_ctf_arrow
constexpr uint32_t kShowCtfArrow = 0x6F25888C;   // show_ctf_arrow
constexpr uint32_t kCapturedFlagYou = 0xF77B7EF8;   // captured_flag_you
constexpr uint32_t kCapturedYourFlag = 0x79A97AD8;  // captured_your_flag
constexpr uint32_t kCapturedFlagOther = 0xB11FCD98; // captured_flag_other
constexpr uint32_t kStoleFlagYou = 0xE046F65E;      // stole_flag_you
constexpr uint32_t kStoleFlagFromYou = 0x647EC837;  // stole_flag_from_you
constexpr uint32_t kStoleFlagOther = 0xAA21CAD8;    // stole_flag_other
constexpr uint32_t kTookFlagYou = 0x2FAC0937;       // took_flag_you
constexpr uint32_t kTookFlagYours = 0x3DDE0CCE;     // took_flag_yours
constexpr uint32_t kTookFlagOther = 0x331C3A51;     // took_flag_other
constexpr uint32_t kRetrievedFlagYou = 0x60B68B40;  // retrieved_flag_you
constexpr uint32_t kRetrievedFlagOther = 0x50D47876; // retrieved_flag_other
constexpr uint32_t kShowCrownArrow = 0x1DFD5982;    // show_crown_arrow
constexpr uint32_t kHideCrownArrow = 0x8399BF35;    // hide_crown_arrow
constexpr uint32_t kNewKingYou = 0xDCA91E37;        // NewKingYou
constexpr uint32_t kNewKingOther = 0xB0CF4C93;      // NewKingOther

constexpr uint8_t kMsgPlayerFullyIn = 0x5E;
constexpr uint32_t kJoinSettleTime = 1000;  // ms
constexpr uint32_t kJustJoinedTime = 5000;  // ms

Hub* TheManager() { return g_onlineHub; }
PlayerRecord* Local(Hub* manager) { return reinterpret_cast<PlayerRecord*>(FindOwnPlayerRecord(manager)); }
uint32_t FlagOf(int team) { return kPlayerCarryingFlag0 << (static_cast<uint32_t>(team) & 31); }

// "team_<team + 1>_name" (the original's sprintf).
void TeamKey(char* key, int team) { std::sprintf(key, "team_%d_name", team + 1); }
const char* TeamName(const char* key) { return Scripts::FetchText(key); }

// Every player carrying `team`'s flag (its lowest) carries none.
void TakeFlagFromCarriers(Hub* manager, int team, bool stopAtFirst)
{
    PlayerSearch search;
    for (auto* p = reinterpret_cast<PlayerRecord*>(StartRosterSweep(manager, &search, false)); p;
         p = reinterpret_cast<PlayerRecord*>(StepRosterSweep(manager, &search, false))) {
        if (p->GetFlagTeam() != team)
            continue;
        if (stopAtFirst) {
            p->m_flagBits &= ~kPlayerCarryingFlags;
            return;
        }
        p->m_flagBits &= ~FlagOf(team);
        p->m_flagBits &= ~kPlayerCarryingFlags;
    }
}

} // namespace

// 0x00543910
bool PlayerRecord::IsInvulnerable()
{
    if (!(m_conn->m_curStanding & NetLink::Conn::kConnReady) || !m_skater->GetFlag2C4())
        return true;
    if (Prop<int>(Prop<void*>(m_skater, 0x27C), 0x18) < 10) // (its state history's frames)
        return true;
    if (FrameClock::GetTime() - m_joinTime < kJoinSettleTime)
        return true;
    if (GameType() == kNetFirefight) {
        auto* skater = reinterpret_cast<GameObjects::CGameEntity*>(m_skater);
        if (Call<int>(CodeAddress(&GameModules::PointTally::GetTotalScore), Prop<void*>(skater->FetchModule(kSkaterScore), 0x18)) <= 0)
            return true;
    }
    if (IsEliminated() || (Prop<uint32_t>(m_skater, 0x104) >> 2) & 1)
        return true;
    if (FrameClock::GetTime() - m_joinTime <= kJustJoinedTime)
        return (m_flagBits >> 1) & 1;
    m_flagBits &= ~kPlayerJustJoined;
    return false;
}

// 0x005439D0
void PlayerRecord::SetFinishedJoining()
{
    Hub* manager = TheManager();
    m_flagBits |= kPlayerFullyIn;
    if (!manager->InNetGame() || !manager->OnServer() || (m_flagBits & kPlayerLocal))
        return;
    switch (manager->GetHostingRole()) {
    case 1: {
        NetLink::NetMessageInfo desc;
        desc.MakeNew();
        NetLink::AppHost* server = manager->m_host; // (0x005480C0, a getter folded there)
        desc.m_ident = kMsgPlayerFullyIn;
        desc.m_queue = NetLink::NetMessageInfo::kLaneOrdered;
        desc.m_groupId = kGroupGame;
        server->PutOnOutbox(m_conn ? Prop<int>(m_conn, 0x3C) : 0, &desc);
        break;
    }
    case 2:
        if (!manager->GetHostPlayer())
            manager->AssignFirstComeFirstServed(); // (the game starts)
        break;
    }
}

// 0x00543A90
void PlayerRecord::ScoreFlagCapture(int team)
{
    char key[64];
    TeamKey(key, team);
    PlayerRecord* local = Local(TheManager());
    Scripts::CStruct* params = NewParams();
    if (m_flagBits & kPlayerLocal) {
        params->AppendWord(kString0, Scripts::QBVAL_STRING, TeamName(key));
        params->PutInt(kTeam, team);
        Run(kCapturedFlagYou, params);
    } else if (!(local->m_flagBits & kPlayerObserver) && local->m_team == team) {
        params->AppendWord(kString0, Scripts::QBVAL_STRING, m_label);
        params->PutInt(kTeam, team);
        Run(kHideCtfArrow);
        Run(kCapturedYourFlag, params);
    } else {
        params->AppendWord(kString0, Scripts::QBVAL_STRING, m_label);
        params->AppendWord(kString1, Scripts::QBVAL_STRING, TeamName(key));
        params->PutInt(kTeam, team);
        Run(kCapturedFlagOther, params);
    }
    m_flagBits &= ~kPlayerCarryingFlags;
    DeleteParams(params);
}

// 0x00543C70
void PlayerRecord::NoteFlagSteal(int team)
{
    Hub* manager = TheManager();
    char key[64];
    TeamKey(key, team);
    Scripts::CStruct* params = NewParams();
    PlayerRecord* local = Local(manager);
    if (m_flagBits & kPlayerLocal) {
        params->AppendWord(kString0, Scripts::QBVAL_STRING, TeamName(key));
        Run(kStoleFlagYou, params);
    } else {
        params->AppendWord(kString0, Scripts::QBVAL_STRING, m_label);
        params->AppendWord(kString1, Scripts::QBVAL_STRING, TeamName(key));
        bool fromLocal = !(local->m_flagBits & kPlayerObserver) && local->GetFlagTeam() == team;
        Run(fromLocal ? kStoleFlagFromYou : kStoleFlagOther, params);
    }
    DeleteParams(params);
    TakeFlagFromCarriers(manager, team, false);
    m_flagBits |= FlagOf(team);
}

// 0x00543F00
void PlayerRecord::NoteFlagPickup(int team)
{
    Hub* manager = TheManager();
    char key[64];
    TeamKey(key, team);
    Scripts::CStruct* params = NewParams();
    PlayerRecord* local = Local(manager);
    if (m_flagBits & kPlayerLocal) {
        params->AppendWord(kString0, Scripts::QBVAL_STRING, TeamName(key));
        Run(kTookFlagYou, params);
    } else if (!(local->m_flagBits & kPlayerObserver) && team == local->m_team) {
        Scripts::CStruct* arrow = NewParams();
        arrow->PutInt(kTeam, local->m_team);
        Run(kShowCtfArrow, arrow);
        DeleteParams(arrow);
        params->AppendWord(kString0, Scripts::QBVAL_STRING, m_label);
        Run(kTookFlagYours, params);
    } else {
        params->AppendWord(kString0, Scripts::QBVAL_STRING, m_label);
        params->AppendWord(kString1, Scripts::QBVAL_STRING, TeamName(key));
        Run(kTookFlagOther, params);
    }
    DeleteParams(params);
    m_flagBits |= FlagOf(team);
}

// 0x00544110
void PlayerRecord::NoteFlagReturned()
{
    Hub* manager = TheManager();
    char key[64];
    TeamKey(key, m_team);
    Scripts::CStruct* params = NewParams();
    if (m_flagBits & kPlayerLocal) {
        params->AppendWord(kString0, Scripts::QBVAL_STRING, TeamName(key));
        params->PutInt(kTeam, m_team);
        Run(kHideCtfArrow);
        Run(kRetrievedFlagYou, params);
    } else {
        PlayerRecord* local = Local(manager);
        if (local && !(local->m_flagBits & kPlayerObserver) && local->m_team == m_team)
            Run(kHideCtfArrow);
        params->AppendWord(kString0, Scripts::QBVAL_STRING, m_label);
        params->AppendWord(kString1, Scripts::QBVAL_STRING, TeamName(key));
        params->PutInt(kTeam, m_team);
        Run(kRetrievedFlagOther, params);
    }
    // (the structure is never deleted here: the original leaks it)
    TakeFlagFromCarriers(manager, m_team, true);
}

// 0x00544340
void PlayerRecord::GiveCrown(bool king)
{
    Hub* manager = TheManager();
    if (!king) {
        if (!(m_flagBits & kPlayerKing))
            return;
        if ((m_flagBits & kPlayerLocal) && (GameType() == kNetKing || GameType() == kKing)) {
            Scripts::CStruct* params = NewParams();
            params->PutNameKey(kPlayer1, kPlayer1);
            params->PutNameKey(kPlayer2, kPlayer2);
            Run(kShowCrownArrow, params);
            DeleteParams(params);
        }
        m_flagBits &= ~kPlayerKing;
        return;
    }
    if (auto* previous = reinterpret_cast<PlayerRecord*>(IsCrownRuleActive(manager)))
        previous->GiveCrown(false);
    if (!(m_flagBits & kPlayerKing)) {
        m_flagBits |= kPlayerKing;
        if (m_flagBits & kPlayerLocal) {
            Scripts::CStruct* params = NewParams();
            uint32_t player = m_skater->GetSkaterNumber() == 0 ? kPlayer1 : kPlayer2; // (its skater number)
            params->PutNameKey(player, player);
            Run(kHideCrownArrow, params);
            DeleteParams(params);
            Run(kNewKingYou);
        } else {
            Scripts::CStruct* params = NewParams();
            params->PutNameKey(kPlayer1, kPlayer1);
            Run(kShowCrownArrow, params);
            params->AppendWord(kString0, m_label);
            Run(kNewKingOther, params);
            DeleteParams(params);
        }
        // Split screen: the other screen's player is told too.
        if (!manager->InNetGame() &&
            manager->FindPlayerSlotByObj(SkaterId(m_skater) == 0 ? 1 : 0)) {
            Scripts::CStruct* params = NewParams();
            params->AppendWord(kString0, m_skater->FetchShownName()); // (its display name)
            Run(kNewKingOther, params);
            DeleteParams(params);
        }
        m_flagBits |= kPlayerKing;
    }
    if (void* crown = manager->GetCrownHolder())
        static_cast<GameObjects::CCrownObject*>(crown)->PutOn(m_skater); // (worn by the skater)
}

} // namespace OnlinePlay

using OnlinePlay::PlayerRecord;
