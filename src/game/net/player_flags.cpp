// The network game's announcements about a player: fully in, capture the flag, king of the hill (see player_info.h).

#include <cstdint>
#include <cstdio>

#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_conn.h"
#include "game/net/player_info.h"
#include "game/obj/game_objects.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skater/skater.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/game_code.h"
#include "game/goals/goal.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/score.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "game/gfx/face_texture.h"
#include "game/gfx/skeleton.h"
#include "game/ed/goal_editor.h"
#include "game/ed/rail_editor.h"
#include "game/net/server_list.h"
#include "game/net/score_list.h"
#include "game/net/buddy_man.h"
#include "game/net/stats_man.h"

#pragma warning(disable : 4996) // (the original's sprintf, kept)

namespace GameNet {

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

Manager* TheManager() { return g_gameNetManager; }
PlayerInfo* Local(Manager* manager) { return reinterpret_cast<PlayerInfo*>(LocalPlayer(manager)); }
uint32_t FlagOf(int team) { return kPlayerCarryingFlag0 << (static_cast<uint32_t>(team) & 31); }

// "team_<team + 1>_name" (the original's sprintf).
void TeamKey(char* key, int team) { std::sprintf(key, "team_%d_name", team + 1); }
const char* TeamName(const char* key) { return Script::GetString(key); }

// Every player carrying `team`'s flag (its lowest) carries none.
void TakeFlagFromCarriers(Manager* manager, int team, bool stopAtFirst)
{
    PlayerSearch search;
    for (auto* p = reinterpret_cast<PlayerInfo*>(FirstPlayer(manager, &search, false)); p;
         p = reinterpret_cast<PlayerInfo*>(NextPlayer(manager, &search, false))) {
        if (p->GetFlagTeam() != team)
            continue;
        if (stopAtFirst) {
            p->m_flags &= ~kPlayerCarryingFlags;
            return;
        }
        p->m_flags &= ~FlagOf(team);
        p->m_flags &= ~kPlayerCarryingFlags;
    }
}

} // namespace

// 0x00543910
bool PlayerInfo::IsInvulnerable()
{
    if (!(m_conn->m_status & Net::Conn::kStatusReady) || !m_skater->GetFlag2C4())
        return true;
    if (Field<int>(Field<void*>(m_skater, 0x27C), 0x18) < 10) // (its state history's frames)
        return true;
    if (Tmr::GetTime() - m_joinTime < kJoinSettleTime)
        return true;
    if (GameType() == kNetFirefight) {
        auto* skater = reinterpret_cast<Obj::CCompositeObject*>(m_skater);
        if (Call<int>(GAME_CODE(0x00561B40, &Mdl::Score::GetTotalScore), Field<void*>(skater->GetComponent(kSkaterScore), 0x18)) <= 0)
            return true;
    }
    if (IsEliminated() || (Field<uint32_t>(m_skater, 0x104) >> 2) & 1)
        return true;
    if (Tmr::GetTime() - m_joinTime <= kJustJoinedTime)
        return (m_flags >> 1) & 1;
    m_flags &= ~kPlayerJustJoined;
    return false;
}

// 0x005439D0
void PlayerInfo::MarkAsFullyIn()
{
    Manager* manager = TheManager();
    m_flags |= kPlayerFullyIn;
    if (!manager->InNetGame() || !manager->OnServer() || (m_flags & kPlayerLocal))
        return;
    switch (manager->GetHostMode()) {
    case 1: {
        Net::MsgDesc desc;
        desc.Construct();
        Net::App* server = manager->m_server; // (0x005480C0, a getter folded there)
        desc.m_id = kMsgPlayerFullyIn;
        desc.m_queue = Net::MsgDesc::kQueueSequenced;
        desc.m_groupId = kGroupGame;
        server->EnqueueMessage(m_conn ? Field<int>(m_conn, 0x3C) : 0, &desc);
        break;
    }
    case 2:
        if (!manager->GetServerPlayer())
            manager->AssignFirstComeFirstServed(); // (the game starts)
        break;
    }
}

// 0x00543A90
void PlayerInfo::CapturedFlag(int team)
{
    char key[64];
    TeamKey(key, team);
    PlayerInfo* local = Local(TheManager());
    Script::CStruct* params = NewParams();
    if (m_flags & kPlayerLocal) {
        params->AddString(kString0, Script::ESYMBOLTYPE_STRING, TeamName(key));
        params->AddInteger(kTeam, team);
        Run(kCapturedFlagYou, params);
    } else if (!(local->m_flags & kPlayerObserver) && local->m_team == team) {
        params->AddString(kString0, Script::ESYMBOLTYPE_STRING, m_name);
        params->AddInteger(kTeam, team);
        Run(kHideCtfArrow);
        Run(kCapturedYourFlag, params);
    } else {
        params->AddString(kString0, Script::ESYMBOLTYPE_STRING, m_name);
        params->AddString(kString1, Script::ESYMBOLTYPE_STRING, TeamName(key));
        params->AddInteger(kTeam, team);
        Run(kCapturedFlagOther, params);
    }
    m_flags &= ~kPlayerCarryingFlags;
    DeleteParams(params);
}

// 0x00543C70
void PlayerInfo::StoleFlag(int team)
{
    Manager* manager = TheManager();
    char key[64];
    TeamKey(key, team);
    Script::CStruct* params = NewParams();
    PlayerInfo* local = Local(manager);
    if (m_flags & kPlayerLocal) {
        params->AddString(kString0, Script::ESYMBOLTYPE_STRING, TeamName(key));
        Run(kStoleFlagYou, params);
    } else {
        params->AddString(kString0, Script::ESYMBOLTYPE_STRING, m_name);
        params->AddString(kString1, Script::ESYMBOLTYPE_STRING, TeamName(key));
        bool fromLocal = !(local->m_flags & kPlayerObserver) && local->GetFlagTeam() == team;
        Run(fromLocal ? kStoleFlagFromYou : kStoleFlagOther, params);
    }
    DeleteParams(params);
    TakeFlagFromCarriers(manager, team, false);
    m_flags |= FlagOf(team);
}

// 0x00543F00
void PlayerInfo::TookFlag(int team)
{
    Manager* manager = TheManager();
    char key[64];
    TeamKey(key, team);
    Script::CStruct* params = NewParams();
    PlayerInfo* local = Local(manager);
    if (m_flags & kPlayerLocal) {
        params->AddString(kString0, Script::ESYMBOLTYPE_STRING, TeamName(key));
        Run(kTookFlagYou, params);
    } else if (!(local->m_flags & kPlayerObserver) && team == local->m_team) {
        Script::CStruct* arrow = NewParams();
        arrow->AddInteger(kTeam, local->m_team);
        Run(kShowCtfArrow, arrow);
        DeleteParams(arrow);
        params->AddString(kString0, Script::ESYMBOLTYPE_STRING, m_name);
        Run(kTookFlagYours, params);
    } else {
        params->AddString(kString0, Script::ESYMBOLTYPE_STRING, m_name);
        params->AddString(kString1, Script::ESYMBOLTYPE_STRING, TeamName(key));
        Run(kTookFlagOther, params);
    }
    DeleteParams(params);
    m_flags |= FlagOf(team);
}

// 0x00544110
void PlayerInfo::RetrievedFlag()
{
    Manager* manager = TheManager();
    char key[64];
    TeamKey(key, m_team);
    Script::CStruct* params = NewParams();
    if (m_flags & kPlayerLocal) {
        params->AddString(kString0, Script::ESYMBOLTYPE_STRING, TeamName(key));
        params->AddInteger(kTeam, m_team);
        Run(kHideCtfArrow);
        Run(kRetrievedFlagYou, params);
    } else {
        PlayerInfo* local = Local(manager);
        if (local && !(local->m_flags & kPlayerObserver) && local->m_team == m_team)
            Run(kHideCtfArrow);
        params->AddString(kString0, Script::ESYMBOLTYPE_STRING, m_name);
        params->AddString(kString1, Script::ESYMBOLTYPE_STRING, TeamName(key));
        params->AddInteger(kTeam, m_team);
        Run(kRetrievedFlagOther, params);
    }
    // (the structure is never deleted here: the original leaks it)
    TakeFlagFromCarriers(manager, m_team, true);
}

// 0x00544340
void PlayerInfo::MarkAsKing(bool king)
{
    Manager* manager = TheManager();
    if (!king) {
        if (!(m_flags & kPlayerKing))
            return;
        if ((m_flags & kPlayerLocal) && (GameType() == kNetKing || GameType() == kKing)) {
            Script::CStruct* params = NewParams();
            params->AddChecksum(kPlayer1, kPlayer1);
            params->AddChecksum(kPlayer2, kPlayer2);
            Run(kShowCrownArrow, params);
            DeleteParams(params);
        }
        m_flags &= ~kPlayerKing;
        return;
    }
    if (auto* previous = reinterpret_cast<PlayerInfo*>(KingOfTheHill(manager)))
        previous->MarkAsKing(false);
    if (!(m_flags & kPlayerKing)) {
        m_flags |= kPlayerKing;
        if (m_flags & kPlayerLocal) {
            Script::CStruct* params = NewParams();
            uint32_t player = m_skater->GetSkaterNumber() == 0 ? kPlayer1 : kPlayer2; // (its skater number)
            params->AddChecksum(player, player);
            Run(kHideCrownArrow, params);
            DeleteParams(params);
            Run(kNewKingYou);
        } else {
            Script::CStruct* params = NewParams();
            params->AddChecksum(kPlayer1, kPlayer1);
            Run(kShowCrownArrow, params);
            params->AddString(kString0, m_name);
            Run(kNewKingOther, params);
            DeleteParams(params);
        }
        // Split screen: the other screen's player is told too.
        if (!manager->InNetGame() &&
            manager->PlayerByObjectId(SkaterId(m_skater) == 0 ? 1 : 0)) {
            Script::CStruct* params = NewParams();
            params->AddString(kString0, m_skater->GetDisplayName()); // (its display name)
            Run(kNewKingOther, params);
            DeleteParams(params);
        }
        m_flags |= kPlayerKing;
    }
    if (void* crown = manager->GetCrown())
        static_cast<Obj::CCrownObject*>(crown)->PutOn(m_skater); // (worn by the skater)
}

} // namespace GameNet

using GameNet::PlayerInfo;
REPLACE(0x00543910, &PlayerInfo::IsInvulnerable);
REPLACE(0x005439D0, &PlayerInfo::MarkAsFullyIn);
REPLACE(0x00543A90, &PlayerInfo::CapturedFlag);
REPLACE(0x00543C70, &PlayerInfo::StoleFlag);
REPLACE(0x00543F00, &PlayerInfo::TookFlag);
REPLACE(0x00544110, &PlayerInfo::RetrievedFlag);
REPLACE(0x00544340, &PlayerInfo::MarkAsKing);
