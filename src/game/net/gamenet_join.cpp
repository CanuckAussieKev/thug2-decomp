// The network game's join (OnlinePlay::Hub): the clients and the server made, the connection request and the join
// request both ways, ok_to_join, join proceed / refused, GameSpy's CD-key answer. See gamenet_manager.h.
//
// NOT ORIGINAL in it: the THUGPRO peer mode (thugpro_join.h) - our requests carry THUGPRO's trailer and machine id,
// THUGPRO's refusal reasons are shown in words, and as host the GameSpy CD-key check is left out; bounds on what the
// host reads from a request.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "game/core/checksums.h"
#include "game/net/gamespy.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/single_instance.h"
#include "game/core/task_list.h"
#include "game/net/buddy_man.h"
#include "game/net/chat.h"
#include "game/net/gamenet_handlers.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/net/net_conn.h"
#include "game/net/net_dispatcher.h"
#include "game/net/net_lists.h"
#include "game/net/net_manager.h"
#include "game/net/net_server.h"
#include "game/net/player_info.h"
#include "game/net/server_list.h"
#include "game/net/stats_man.h"
#include "game/net/thugpro_join.h"
#include "game/net/thugpro_peer.h"
#include "game/script/cscript.h"
#include "game/script/preferences.h"
#include "game/script/param_struct.h"
#include "game/skate/profile_manager.h"
#include "game/sys/game_config.h"
#include "game/sys/mlp.h"
#include "game/sys/stopwatch.h"
#include "game/core/code_address.h"
#include "game/sys/run_log.h"
#include "game/assets/resource_registry.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/point_tally.h"
#include "game/gfx/face_texture.h"
#include "game/gfx/rig.h"
#include "game/park_editing/goal_editor.h"
#include "game/park_editing/rail_editor.h"
#include "game/net/score_list.h"
#include "game/sys/crt.h"

#pragma warning(disable : 4996) // (strcpy / strncpy: the original's own copies into its fixed fields)

namespace OnlinePlay {

char g_pendingPassword[kPasswordSize]; // (winmain.cpp: -PASSWORD when joining)

namespace {

using NetLink::AppHost;
using NetLink::Conn;
using NetLink::NetMessageInfo;
using NetLink::HandlerSlotValue;
using NetLink::InboundMsgCtx;

// Messages.
constexpr uint8_t kMsgConnectionRequest = 3;
constexpr uint8_t kMsgJoinRefused = 71;   // 0x47
constexpr uint8_t kMsgJoinProceed = 79;   // 0x4F
constexpr uint8_t kMsgJoinRequest = 80;   // 0x50
constexpr uint8_t kMsgServerResponse = 16; // 0x10 (the server list)

// Names.
constexpr uint32_t kNetworkId = 0xCBED746E;   // network_id (the player's name preference)
constexpr uint32_t kServerName = 0x2A5CF019;  // server_name
constexpr uint32_t kUiString = 0x96875C0F;    // ui_string
constexpr uint32_t kNetLobby = 0x1C471C60;    // netlobby (the lobby's game mode)
constexpr uint32_t kCreateEnterPasswordControl = 0x7721B33A;
constexpr uint32_t kCreateGameInProgressDialog = 0x89E9B82D;
constexpr uint32_t kStatusPlaying = 0xFF4ED0D9; // (the presence text when joining to play)
constexpr uint32_t kStatusObserving = 0x9424C562; // (... to observe)
constexpr uint32_t kReasonBanned = 0xE5A65DEC;       // net_reason_banned
constexpr uint32_t kReasonFull = 0x7782B5B7;         // net_reason_full
constexpr uint32_t kReasonFullObservers = 0xDDE5676F; // net_reason_full_observers
constexpr uint32_t kReasonWrongPassword = 0x3104C1F1; // net_reason_wrong_password
constexpr uint32_t kReasonRankLow = 0x922B12AD;      // net_reason_rank_limit_low
constexpr uint32_t kReasonRankHigh = 0x2AB1E499;     // net_reason_rank_limit_high
constexpr uint32_t kReasonDefault = 0xBA9CB8E8;      // net_reason_default

constexpr int kJoinModeObserve = 1;
constexpr int kRankTestGreater = 2; // GetRankTest: the joiner's rank must be above the limit
constexpr int kRankTestLess = 1;    // ... below it
constexpr int kConnectionTypeModem = 1;
constexpr uint32_t kCdKeyResponseLength = 72;
constexpr int kGameSpyGameId = 1003; // THUG2 PC's (the CD-key check)
constexpr int kDeferredJoinFrames = 10;
constexpr uint16_t kJoinPort = 0x1422; // 5154
constexpr uint16_t kHostPort = 0x1421; // 5153
constexpr int kMaxConnections = 20;
constexpr uint32_t kPlayerInfoSize = 0x44;

// App flags (MakeClientApp / Server).
constexpr int kAppBroadcast = 0x01;
constexpr int kAppLocal = 0x08;
constexpr int kAppDynamicResend = 0x04;
constexpr int kAppForeign = 0x10;
constexpr int kAppSecure = 0x20;

// Handler flags.
constexpr uint32_t kRunForForeign = 1;
constexpr uint32_t kRunWhenLate = 2;
constexpr uint32_t kRunOnBadCrc = 4;
constexpr int kMidPriority = 0x80;
constexpr int kHighestPriority = 0xFF;
constexpr int kLowestPriority = 0;

// The player info being made (OnlinePlay::JoinerRecord, player_info.h; flags: 1 local player, 4 observer, 8 pending).
static_assert(sizeof(JoinerRecord) == kPlayerInfoSize);
constexpr uint32_t kPlayerPending = kPlayerPendingObserver; // (kPlayerLocal, kPlayerObserver: player_info.h)

// GameSpy CD-key's instance data for ClientAuthorized (12 bytes, operator new).
// (the join request being sent: the original's is on its stack)
JoinInfo g_joinInfo;

struct AuthInstance {
    AppHost* app;
    Hub* manager;
    Conn* conn;
};

char g_joinPassword[kPasswordSize];

// The global passwords: the one being typed (12) and the one in use (10; resent with every request).
char* PendingPassword() { return g_pendingPassword; }
char* JoinPassword() { return g_joinPassword; }

NetLink::Hub* NetSessionSlot() { return NetLink::Hub::Sole(); }
TaskList::PileUp* LogicTasks()
{
    return reinterpret_cast<TaskList::PileUp*>(reinterpret_cast<uint8_t*>(AssetStore::g_mainLoopManager) + 4);
}
int MilliSeconds() { return static_cast<int>(FrameClock::GetTime()); }
uint32_t LocalAddress()
{
    return Crt::InetAddr(NetSessionSlot()->FetchOwnAddress());
}

// Calls into code that isn't source yet (thiscall).
template <typename R, typename... A>
R Call(uint32_t address, const void* object, A... args)
{
    return reinterpret_cast<R(__thiscall*)(const void*, A...)>(address)(object, args...);
}
template <typename R, typename... A>
R Call(void* code, const void* object, A... args)
{
    return reinterpret_cast<R(__thiscall*)(const void*, A...)>(code)(object, args...);
}
// The manager's methods.
int NumObservers(Hub* m) { return m->CountObservers(); }
int MaxObservers(Hub* m) { return m->GetSpectatorLimit(); }
int NumPlayers(Hub* m) { return m->CountPlayers(); }
int MaxPlayers(Hub* m) { return m->GetPlayerLimit(); }
int RankLimit(Hub* m) { return m->GetRankLimit(); }
int RankTest(Hub* m) { return m->GetRankTest(); }
const char* Password(Hub* m) { return m->FetchSecret(); }
void LatePlayerArrival(Hub* m, JoinerRecord* player, int frames) { m->AddJoiningPlayer(player, frames); }
void SetReadyState(Hub* m, bool ready) { m->SetReadyState(ready); }
void ShuffleSpawnSlots(Hub* m) { m->ChooseStartPoints(); }
void CancelJoinServer(Hub* m) { m->CancelJoin(); }
uint32_t ActiveRulesetLabel() { return Call<uint32_t>(CodeAddress(&GoalSystem::CMatchRules::GetNameKey), reinterpret_cast<void*>(GoalSystem::g_gameMode)); }
void SetMaximumPlayers(int count) { Call<void>(CodeAddress(&GoalSystem::CMatchRules::SetRiderCeiling), reinterpret_cast<void*>(GoalSystem::g_gameMode), count); }
int8_t Rank(Hub* m) { return static_cast<int8_t>(static_cast<RankingsManager*>(m->m_ratingService)->GetRank()); }
int32_t Rating(Hub* m)
{
    // (the original reads the stats' first word, the rating, through the folded getter 0x00469490 -
    // Singletons::PointerMember::Pull)
    return static_cast<RankingsManager*>(m->m_ratingService)->GetPersonalStats()->m_skillPoints;
}
bool MatchUnderway(Hub* m) { return ActiveRulesetLabel() != kNetLobby && !m->m_matchEnded; }

OptionStore::Preferences* NetGameOptions(Hub* m) { return reinterpret_cast<OptionStore::Preferences*>(m->m_netOptions); }
const char* PreferenceText(Hub* m, uint32_t name)
{
    const char* text = nullptr;
    NetGameOptions(m)->FetchSetting(name)->FetchText(kUiString, &text, 1);
    return text;
}

// The presence (GameSpy) and the lobby (not source yet).
bool PresenceLoggedIn(Hub* m) { return *(static_cast<uint8_t*>(m->m_presenceService) + 0x10) != 0; }
void SetPresence(Hub* m, int status, const char* text, const char* location)
{
    static_cast<FriendsManager*>(m->m_presenceService)->ChangeStanding(status, text, location);
}
char* LobbyServerName(Hub* m) { return static_cast<char*>(m->m_lobbyService) + 0xB4; }
char* LobbyPeerName(Hub* m) { return static_cast<char*>(m->m_lobbyService) + 0x34; }

// GameSpy's CD-key SDK (gcd) and THUG2's key.
void ComputeCdKeyResponse(const char* key, const char* challenge, char* response)
{
    GameSpy::CdKeyComputeResponse(key, challenge, response);
}
void AuthenticateUser(int localId, uint32_t ip, const char* challenge, const char* response, void* instance)
{
    using HookFn = void(__cdecl*)(int, int, int, char*, void*);
    GameSpy::CdKeyAuthenticateUser(
        kGameSpyGameId, localId, ip, challenge, response, &Hub::ClientAuthorized, instance);
}

void Sprintf(char* out, const char* format, uint32_t a, uint32_t b, int c, const char* d, const char* e)
{
    Crt::Sprintf(out, format, a, b, c, d, e);
}

NetMessageInfo NewDesc()
{
    NetMessageInfo desc;
    desc.MakeNew();
    return desc;
}

// The connection request (all four senders): THUG2's, or in the THUGPRO peer mode with THUGPRO's trailer (TP-R12).
// `request` must outlive the send (the message is copied when queued).
void SendConnectionRequest(NetLink::Guest* client, const ConnectInfo& info, Thugpro::ConnectRequest* request)
{
    NetMessageInfo desc = NewDesc();
    if (Thugpro::PeerMode()) {
        desc.m_byteCount = Thugpro::MakeConnectRequest(info, request);
        desc.m_content = request;
        desc.m_compression = 3; // (THUGPRO's LZHAM, as its client sends it - only if smaller)
    } else {
        request->thug2 = info;
        desc.m_content = &request->thug2;
        desc.m_byteCount = sizeof(ConnectInfo);
    }
    desc.m_ident = kMsgConnectionRequest;
    client->SendToHost(&desc);
}

NetLink::Guest* AsClient(AppHost* app) { return static_cast<NetLink::Guest*>(app); }

// The handlers a joining client adds (0x00534480): on every client, then on client 0 only.
struct HandlerEntry {
    uint8_t id;
    uint32_t code; // (an address: a game handler, or one of ours - Responder() maps those)
    uint32_t flags;
    bool managerData;
    int priority;
};
constexpr uint32_t kOurs = 0x80000000; // (+ an index into OurHandler)
HandlerSlotValue OurHandler(uint32_t code)
{
    switch (code) {
    case kOurs + 0: return &Hub::OnTurnedAwayMsg;
    case kOurs + 1: return &Hub::OnGoAheadMsg;
    case kOurs + 2: return &OnlinePlay::OnChatMsg;
    case kOurs + 3: return &Hub::OnLinkEstablished;
    case kOurs + 4: return &Hub::OnAdmissionAskMsg;
    default: return reinterpret_cast<HandlerSlotValue>(code);
    }
}
const HandlerEntry kClientHandlers[] = {
    {0x42, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleReadyToPlay)), kRunWhenLate, true, kMidPriority},  // proceed to play
    {0x4D, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::OnSpectateMsg)), kRunWhenLate, true, kMidPriority},  // observe proceed
    {0x4E, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleObserversFull)), kRunWhenLate, true, kMidPriority},  // observe refused
    {kMsgJoinRefused, kOurs + 0, kRunWhenLate, true, kMidPriority},
    {kMsgJoinProceed, kOurs + 1, kRunWhenLate, true, kMidPriority},
    {0x25, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::OnReadyAskMsg)), kRunWhenLate, true, kMidPriority},  // ready query
    {0x26, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::OnReadyReplyMsg)), kRunWhenLate, true, kMidPriority},  // ready response
    {0x53, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::OnAdmittedMsg)), kRunWhenLate, true, kMidPriority},  // player info ack request
    {0x58, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleCrownSpawn)), 0, true, kMidPriority},            // the king dropped the crown
    {0x75, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleFlagReturned)), 0, true, kMidPriority},            // a player dropped a flag
    {0x3F, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::OnHudNoticeMsg)), kRunWhenLate, true, kMidPriority},  // panel message
    {0x5E, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleAutoServerNotice)), kRunWhenLate, true, kMidPriority},  // auto server notification
    {0x5F, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleFcfsNotice)), kRunWhenLate, true, kMidPriority},  // first-come-first-served assignment
    {0x68, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleJoinedTeam)), kRunWhenLate, true, kMidPriority},  // team change
    {0x69, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleChooseTeam)), kRunWhenLate, true, kMidPriority},  // number of teams
    {0x4C, kOurs + 2, kRunWhenLate, false, kMidPriority},  // chat
    {0x7C, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleCdKeyChallenge)), kRunWhenLate, true, kMidPriority},  // challenge
};
const HandlerEntry kGameClientHandlers[] = {
    {0x6D, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleSelectedGoals)), 0, true, kMidPriority},    // selected goals
    {0x34, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleStartGame)), 0, true, kMidPriority},    // game info
    {0x7A, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleKillTeamFlags)), 0, true, kMidPriority},    // kill the team flags
    {0x6E, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleGoalCompleted)), 0, true, kMidPriority},    // a goal beaten (relayed)
    {0x6F, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleGoalStarted)), 0, true, kMidPriority},    // a goal started (relayed)
    {0x70, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleNothing)), 0, true, kMidPriority},    // toggle pro set (nothing)
    {0x46, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::OnLevelSwitchMsg)), 0, true, kHighestPriority},   // change level
    {0x46, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleNothing)), 0, true, kHighestPriority},   // (and nothing)
    {0x6C, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleParkMap)), 0, true, kHighestPriority},   // level data
    {0x88, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleRailsData)), kRunOnBadCrc, true, kHighestPriority}, // rail data
    {0x87, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::OnGoalSyncMsg)), kRunOnBadCrc, true, kHighestPriority}, // goals data
    {0x80, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleChecksumRequest)), 0, true, kMidPriority},    // cheat checksum request
};
const HandlerEntry kRemoteGameClientHandlers[] = {
    {0x22, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::OnPlayerAddedMsg)), 0, true, kMidPriority},    // player create
    {0x79, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleFlushImportant)), 0, true, kMidPriority},    // join accepted
};
const HandlerEntry kGameClientHandlers2[] = {
    {0x7B, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleKeepAlive)), 0, true, kMidPriority},    // wait n seconds
    {0x55, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::OnCrownChangeMsg)), 0, true, kMidPriority},    // new king
    {0x72, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::OnFlagScoredMsg)), 0, true, kMidPriority},    // captured flag
    {0x74, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::OnFlagSwipedMsg)), 0, true, kMidPriority},    // stole flag
    {0x71, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::OnFlagGrabbedMsg)), 0, true, kMidPriority},    // took flag
    {0x73, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::OnFlagReturnedMsg)), 0, true, kMidPriority},    // retrieved flag
    {0x5A, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::OnMatchEndMsg)), 0, true, kMidPriority},    // game over
    {0x6A, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleGameEnded)), 0, true, kMidPriority},    // end game
    {0x23, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleObjectUpdateLength)), kRunForForeign | kRunWhenLate, true, kHighestPriority}, // object updates (their length)
    {0x8D, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleEliminated)), 0, true, kMidPriority},    // a skater eliminated
    {0x8E, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleEliminated)), 0, true, kMidPriority},
};
// The server's lobby handlers (0x0053C380; taken out once the game starts).
const HandlerEntry kServerHandlers[] = {
    {kMsgConnectionRequest, kOurs + 3, kRunForForeign | kRunWhenLate, true, kLowestPriority},
    {0x90, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleTeamObject)), 0, true, kHighestPriority},           // a scavenger-hunt object picked up
    {kMsgJoinRequest, kOurs + 4, kRunWhenLate, true, kMidPriority},
    {0x11, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::OnServerQueryMsg)), kRunForForeign | kRunWhenLate, true, kMidPriority}, // find server
    {0x0B, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::OnPeerLeftMsg)), kRunWhenLate, true, kMidPriority},  // disconnection request
    {0x4A, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleObserveRequest)), kRunWhenLate, true, kMidPriority},  // observe
    {0x26, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::OnReadyReplyMsg)), kRunWhenLate, true, kMidPriority},  // ready response
    {0x54, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleSendGameState)), 0, true, kMidPriority},            // player info ack
    {0x57, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleDropCarried)), 0, true, kMidPriority},            // moved to a restart
    {0x59, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleGameEndedReport)), 0, true, kMidPriority},            // the run ended
    {0x60, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleHostOptions)), kRunWhenLate, true, kMidPriority},  // first-come-first-served requests: start game,
    {0x61, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleHostOptions)), kRunWhenLate, true, kMidPriority},  // ban player, change level, toggle pro set, goal
    {0x62, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleHostOptions)), kRunWhenLate, true, kMidPriority},  // selection, end game, number of teams
    {0x63, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleHostOptions)), kRunWhenLate, true, kMidPriority},
    {0x64, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleHostOptions)), kRunWhenLate, true, kMidPriority},
    {0x65, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleHostOptions)), kRunWhenLate, true, kMidPriority},
    {0x66, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleHostOptions)), kRunWhenLate, true, kMidPriority},
    {0x67, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::OnSwitchSquadMsg)), kRunWhenLate, true, kMidPriority},  // team change request
    {0x6B, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleRequestGameData)), kRunWhenLate, true, kMidPriority},  // level request
    {0x6E, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleGoalCompletedReport)), kRunWhenLate, true, kMidPriority},  // a goal beaten
    {0x6F, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleGoalStartedReport)), kRunWhenLate, true, kMidPriority},  // a goal started
    {0x6C, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleParkMap)), 0, true, kHighestPriority},           // level data
    {0x88, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleRailsData)), 0, true, kHighestPriority},           // rail data
    {0x7D, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleCdKeyAnswer)), 0, true, kMidPriority},            // challenge response
    {0x81, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleChecksumResponse)), 0, true, kMidPriority},            // cheat checksum response
    {0x8B, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleCreatedDataRequest)), 0, true, kMidPriority},            // rails / goals data requests
    {0x8A, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::HandleCreatedDataRequest)), 0, true, kMidPriority},
};

template <size_t N>
void AddHandlers(AppHost* app, Hub* manager, const HandlerEntry (&entries)[N])
{
    for (const HandlerEntry& entry : entries)
        app->m_dispatcher.AppendListener(entry.id, OurHandler(entry.code), entry.flags,
                                     entry.managerData ? manager : nullptr, entry.priority);
}

void SendRefused(AppHost* app, Conn* conn, const char* data)
{
    app->DeliverTo(kMsgJoinRefused, kJoinRefusedSize, data, conn->m_remoteIp, conn->m_portNo, 0);
}

} // namespace

// 0x0052F560
bool Hub::JoinsAccepted(int& reason, ConnectInfo* msg, Conn* conn, bool checkPassword, bool checkVersion,
                       bool checkCdKey)
{
    if (m_host->IsConnBlocked(conn)) {
        reason = kRefusedBanned;
        return false;
    }
    if (msg->observer) {
        if (NumObservers(this) >= MaxObservers(this)) {
            reason = kRefusedFullObservers;
            return false;
        }
    } else if (NumPlayers(this) >= MaxPlayers(this)) {
        reason = kRefusedFull;
        return false;
    }
    if (checkVersion && msg->version != kVersion) {
        reason = kRefusedVersion;
        return false;
    }
    if (conn->IsOffMachine()) {
        int limit = RankLimit(this);
        int test = RankTest(this);
        if (test == kRankTestGreater) {
            if (msg->rank <= limit) {
                reason = kRefusedRankLow;
                return false;
            }
        } else if (test == kRankTestLess && msg->rank >= limit) {
            reason = kRefusedRankHigh;
            return false;
        }
    }
    if (!msg->canQueueUp && !msg->observer && MatchUnderway(this)) {
        reason = kRefusedInProgress;
        return false;
    }
    if (checkPassword) {
        const char* password = Password(this);
        if (*password && std::strcmp(password, msg->password) != 0) {
            reason = kRefusedPassword;
            return false;
        }
    }
    // NOT ORIGINAL: no CD-key check in the THUGPRO peer mode (TP-R12) - THUGPRO's hosts skip it (its patch over this
    // test) and its players' keys are made up. (The response ends inside its field here: the original's strlen could
    // read past a message without one.)
    if (checkCdKey && !Thugpro::PeerMode() &&
        strnlen(msg->cdKeyResponse, sizeof(msg->cdKeyResponse)) != kCdKeyResponseLength) {
        reason = kRefusedCdKey;
        return false;
    }
    return true;
}

// 0x00534480
void Hub::JoinServer(uint32_t ip, uint16_t port, int index)
{
    NetLink::Hub* net = NetSessionSlot();
    const char* name = PreferenceText(this, kNetworkId); // (not used)
    (void)name;
    AppHost* client = ClientSlot(index);
    ConnectInfo info;
    std::memset(&info, 0, sizeof(info)); // (the original sends its stack's garbage in the fields it doesn't set)
    if (client->IsHostLocal()) {
        // The host's own client: its connection and the server's know each other.
        Conn* serverConn = m_host->OpenedLink(static_cast<int>(ip), port, Conn::kFlagLocal);
        Conn* conn = client->OpenedLink(static_cast<int>(ip), port, Conn::kFlagLocal);
        client->LinkDuplicateConns(serverConn, conn);
    } else {
        AsClient(client)->DialServer(ip, port);
        client->OpenedLink(static_cast<int>(ip), port, Conn::kFlagRemote);
        m_joinBeganAt = MilliSeconds();
        LogicTasks()->QueueJob(m_joinExpiryJob);
        m_joinPhase = kJoinStateConnecting;
        LogicTasks()->QueueJob(m_joinPhaseJob);
        m_joinPort = port;
        m_joinIp = ip;
        info.rank = Rank(this);
    }
    info.observer = m_joinMode == kJoinModeObserve;
    info.version = kVersion;
    if (std::strlen(PendingPassword()) != 0) {
        std::strcpy(info.password, PendingPassword());
        std::strcpy(JoinPassword(), PendingPassword());
    } else {
        std::memset(JoinPassword(), 0, kPasswordSize);
        info.password[0] = '\0';
    }
    if (!client->IsHostLocal())
        std::memset(PendingPassword(), 0, kPasswordSize);
    info.canQueueUp = false;
    info.broadband = net->GetNetLinkKind() != kConnectionTypeModem;
    Thugpro::ConnectRequest request;
    SendConnectionRequest(AsClient(client), info, &request);
    client->Transmit(false);

    AddHandlers(client, this, kClientHandlers);
    if (index == 0) {
        AddHandlers(m_clientApp, this, kGameClientHandlers);
        if (!m_clientApp->IsHostLocal())
            AddHandlers(m_clientApp, this, kRemoteGameClientHandlers);
        AddHandlers(m_clientApp, this, kGameClientHandlers2);
    }
    m_spectateRequest = 0;
}

// 0x00534AF0
void Hub::RetryEntry(AppHost* client)
{
    ConnectInfo info;
    std::memset(&info, 0, sizeof(info));
    info.observer = m_joinMode == kJoinModeObserve;
    info.version = kVersion;
    info.canQueueUp = true;
    info.password[0] = '\0';
    info.broadband = NetSessionSlot()->GetNetLinkKind() != kConnectionTypeModem;
    if (InInternetMode())
        info.rank = Rank(this);
    Thugpro::ConnectRequest request;
    SendConnectionRequest(AsClient(client), info, &request);
    client->Transmit(false);
    m_joinBeganAt = MilliSeconds();
    LogicTasks()->QueueJob(m_joinExpiryJob);
    m_joinPhase = kJoinStateJoining;
}

// 0x00534BD0
void Hub::RetryEntryWithKey(const char* password)
{
    std::strcpy(JoinPassword(), password);
    const char* name = PreferenceText(this, kNetworkId); // (not used)
    (void)name;
    ConnectInfo info;
    std::memset(&info, 0, sizeof(info));
    info.rank = Rank(this);
    info.observer = m_joinMode == kJoinModeObserve;
    info.version = kVersion;
    info.canQueueUp = true;
    info.broadband = NetSessionSlot()->GetNetLinkKind() != kConnectionTypeModem;
    std::strcpy(info.password, password);
    Thugpro::ConnectRequest request;
    SendConnectionRequest(AsClient(m_clientApp), info, &request);
    m_joinBeganAt = MilliSeconds();
    LogicTasks()->QueueJob(m_joinExpiryJob);
    m_joinPhase = kJoinStateJoiningWithPassword;
}

const char g_uiString[10]{"ui_string"};
const char g_netReasonGeneral[19]{"net_reason_general"};
const char g_presenceLocationFormat[27]{"10thug211:%d:%d:%d:%s (%s)"};
const char g_matchAppName[13]{"Match Skate6"};
const char g_skateAppName[7]{"Skate6"};

// 0x00537220
AppHost* Hub::StartClientSession(bool broadcast, bool local, bool secure, int index)
{
    NetLink::Hub* net = NetSessionSlot();
    int flags = broadcast ? kAppBroadcast : 0;
    flags |= local ? kAppLocal : kAppDynamicResend | kAppForeign;
    if (secure)
        flags |= kAppSecure;
    AppHost* client = net->MakeClientApp(index, g_skateAppName, kJoinPort, // (its name)
                                          static_cast<int>(LocalAddress()), flags);
    ClientSlot(index) = client;
    if (index == 0 && client) {
        m_lastReadyProbeId = 0;
        m_scriptFlagBits = 0;
        m_viewedPlayer = nullptr;
        OnlinePlay::SetReadyState(this, false); // (the helper: 0x00537140 through the hook)
    }
    m_spectatorPadListener = nullptr;
    m_matchEnded = false;
    m_matchBeingSetUp = false;
    return ClientSlot(index);
}

// 0x00534430
AppHost* Hub::StartMatchmakingClient()
{
    NetLink::Hub* net = NetSessionSlot();
    AppHost* client = net->MakeClientApp(0, g_matchAppName, kJoinPort,
                                          static_cast<int>(LocalAddress()),
                                          kAppBroadcast | kAppDynamicResend | kAppForeign | kAppSecure);
    m_browserClient = client;
    client->m_dispatcher.AppendListener(kMsgServerResponse, &OnServerReplyMsg,
                                    kRunForForeign | kRunWhenLate, this, kMidPriority);
    return m_browserClient;
}

// 0x0053C380
AppHost* Hub::StartHostSession(bool local, bool secure)
{
    NetLink::Hub* net = NetSessionSlot();
    int flags = kAppBroadcast | kAppDynamicResend | kAppForeign | (local ? kAppLocal : 0);
    if (secure)
        flags |= kAppSecure;
    AppHost* server = net->MakeServerApp(0, g_skateAppName, kMaxConnections, kHostPort,
                                          static_cast<int>(LocalAddress()), flags);
    m_host = server;
    if (m_hostMode != 0)
        m_hostMode = 0; // serve
    if (server) {
        LogicTasks()->QueueJob(m_hostAdmitJob);
        AddHandlers(m_host, this, kServerHandlers);
        m_scoreSyncedAt = 0;
        m_awaitingStart = false;
        ShuffleSpawnSlots(this);
        m_spectatorPadListener = nullptr;
    }
    return m_host;
}

// 0x0052F790
int Hub::OnLinkEstablished(InboundMsgCtx* context)
{
    Conn* conn = context->m_senderConn;
    auto* manager = static_cast<Hub*>(context->m_content);
    auto* msg = reinterpret_cast<ConnectInfo*>(context->m_body);
    if (!conn->IsOffMachine()) {
        // The host's own client: proceed at once (only the player count filled in).
        NetMessageInfo desc = NewDesc();
        ProceedInfo proceed;
        std::memset(&proceed, 0, sizeof(proceed));
        proceed.maxPlayers = static_cast<int8_t>(MaxPlayers(manager));
        desc.m_content = &proceed;
        desc.m_ident = kMsgJoinProceed;
        desc.m_byteCount = sizeof(proceed);
        context->m_receiverApp->PutOnOutbox(conn->m_hdl, &desc);
        return NetLink::kResultPassOn;
    }
    if (!(context->m_wireOptions & kRunForForeign))
        return NetLink::kResultConsumed;
    NetLink::Hub* net = NetSessionSlot();
    if (!context->m_receiverApp->AllowsOutsidePeers())
        return NetLink::kResultConsumed;
    // NOT ORIGINAL: a request shorter than THUG2's isn't read (the original reads past it).
    if (context->m_bodySize < sizeof(ConnectInfo))
        return NetLink::kResultConsumed;
    Thugpro::ConnectTrailer trailer;
    if (Thugpro::PeerMode() && Thugpro::ReadConnectTrailer(msg, context->m_bodySize, &trailer))
        Log("THUGPRO join (TP-R12): a THUGPRO client asks to join: version %08X, release %u, scripts %08X%s, machine %08X",
            trailer.version, trailer.release, trailer.scriptsHash, trailer.modifiedScripts ? " (modified)" : "",
            trailer.machineId);
    int reason = 0;
    if (!manager->JoinsAccepted(reason, msg, conn, true, true, true)) {
        char refused[32] = {}; // (10 sent: the reason, and the challenge for kRefusedCdKey)
        refused[0] = static_cast<char>(reason);
        if (reason == kRefusedCdKey)
            std::strcpy(refused + 1, manager->m_challenge);
        SendRefused(context->m_receiverApp, conn, refused);
        return NetLink::kResultStop;
    }
    Conn* joined = context->m_receiverApp->OpenedLink(static_cast<int>(conn->m_remoteIp), conn->m_portNo, Conn::kFlagRemote);
    context->m_senderConn = joined;
    joined->SetLinkSpeedClass(msg->broadband ? Conn::kBandwidthBroadband : Conn::kBandwidthModem);
    joined->SetTransmitPeriod(static_cast<uint32_t>(1000 / GameConfig::g_frameRate * 2)); // two frames
    // NOT ORIGINAL: in the THUGPRO peer mode no GameSpy CD-key check (TP-R12): proceed as on a LAN.
    if (manager->InInternetMode() && !Thugpro::PeerMode()) {
        auto* instance = static_cast<AuthInstance*>(MemSys::Alloc(sizeof(AuthInstance)));
        if (instance)
            std::memset(instance, 0, sizeof(*instance));
        instance->manager = manager;
        instance->app = context->m_receiverApp;
        instance->conn = joined;
        AuthenticateUser(static_cast<int>(joined->m_ident), context->m_senderConn->m_remoteIp, manager->m_challenge,
                         msg->cdKeyResponse, instance);
        return NetLink::kResultPassOn;
    }
    ProceedInfo proceed;
    std::memset(&proceed, 0, sizeof(proceed)); // (the original leaves the addresses as its stack had them on a LAN)
    proceed.maxPlayers = static_cast<int8_t>(MaxPlayers(manager));
    proceed.broadband = net->GetNetLinkKind() != kConnectionTypeModem;
    if (manager->InInternetMode()) {
        proceed.port = manager->m_joinPort;
        proceed.privateIp = manager->m_joinPrivateIp;
        proceed.publicIp = manager->m_joinIp;
    }
    std::strncpy(proceed.serverName, PreferenceText(manager, kServerName), kServerNameSize - 1);
    proceed.serverName[kServerNameSize - 1] = '\0';
    context->m_receiverApp->DeliverTo(kMsgJoinProceed, sizeof(proceed), &proceed, joined->m_remoteIp, joined->m_portNo, 0);
    return NetLink::kResultPassOn;
}

// 0x00536530
void Hub::ClientAuthorized(int /*gameId*/, int /*localId*/, int authenticated, char* /*error*/, void* data)
{
    auto* instance = static_cast<AuthInstance*>(data);
    if (!instance)
        return;
    if (!authenticated) {
        char refused[kJoinRefusedSize] = {};
        refused[0] = kRefusedCdKey;
        SendRefused(instance->app, instance->conn, refused);
        instance->conn->MarkStale();
    } else {
        Hub* manager = instance->manager;
        ProceedInfo proceed;
        std::memset(&proceed, 0, sizeof(proceed));
        proceed.maxPlayers = static_cast<int8_t>(MaxPlayers(manager));
        proceed.broadband = true;
        if (manager->InInternetMode()) {
            proceed.port = manager->m_joinPort;
            proceed.privateIp = manager->m_joinPrivateIp;
            proceed.publicIp = manager->m_joinIp;
        }
        std::strncpy(proceed.serverName, PreferenceText(manager, kServerName), kServerNameSize - 1);
        proceed.serverName[kServerNameSize - 1] = '\0';
        instance->app->DeliverTo(kMsgJoinProceed, sizeof(proceed), &proceed, instance->conn->m_remoteIp,
                                     instance->conn->m_portNo, 0);
    }
    MemSys::Hub::Pull()->Dealloc(instance);
}

// 0x0052FA10
int Hub::OnAdmissionAskMsg(InboundMsgCtx* context)
{
    Conn* conn = context->m_senderConn;
    auto* manager = static_cast<Hub*>(context->m_content);
    auto* msg = reinterpret_cast<JoinInfo*>(context->m_body);
    void* skate = reinterpret_cast<void*>(GameModules::g_skateModule);
    context->m_Acknowledge = true;
    // NOT ORIGINAL: a request shorter than THUG2's header isn't read (the original reads past it).
    if (context->m_bodySize < kJoinInfoHeaderSize)
        return NetLink::kResultConsumed;
    int reason = 0;
    if (conn->IsOffMachine() && !manager->JoinsAccepted(reason, msg, conn, false, false, false)) {
        NetMessageInfo desc = NewDesc();
        char refused[kJoinRefusedSize] = {};
        refused[0] = static_cast<char>(reason);
        desc.m_content = refused;
        desc.m_byteCount = kJoinRefusedSize;
        desc.m_ident = kMsgJoinRefused;
        context->m_receiverApp->PutOnOutbox(conn->m_hdl, &desc);
        context->m_senderConn->MarkStale();
        return NetLink::kResultStop;
    }
    void* memory = MemSys::AllocZeroed(kPlayerInfoSize);
    auto* player = memory ? static_cast<JoinerRecord*>(memory)->MakeNew() : nullptr;
    std::strncpy(player->m_label, msg->name, kNameSize - 1);
    player->m_label[kNameSize - 1] = '\0';
    player->m_entityKey = static_cast<uint8_t>(manager->ReserveObjIdForPlayer());
    player->m_conn = context->m_senderConn;
    player->m_flagBits = 0;
    player->m_profileId = msg->profile;
    player->m_skillPoints = msg->rating;
    player->m_driveControlMode = msg->driveScheme;
    player->m_unknown34 = 0;
    if (msg->observer)
        player->m_flagBits = kPlayerObserver;
    else if (MatchUnderway(manager))
        player->m_flagBits |= kPlayerPending | kPlayerObserver;
    if (context->m_senderConn->IsHostLocal()) {
        // The host's own player: its profile copied through a buffer.
        if (!MemSys::g_managerReady)
            MemSys::EnsureManager();
        auto* buffer = static_cast<uint8_t*>(MemSys::Hub::Pull()->Carve(kAppearanceSize, true, nullptr));
        auto* profile = Call<GameObjects::CRiderIdentity*>(CodeAddress(static_cast<GameObjects::CRiderIdentity* (GameModules::Skate::*)(int)>(&GameModules::Skate::FetchAccount)), skate, static_cast<int>(static_cast<int8_t>(player->m_entityKey)));
        profile->EmitIntoBuffer(buffer, kAppearanceSize, 0);
        player->m_account->DecodeFromBytes(buffer);
        MemSys::Hub::Pull()->Dealloc(buffer);
        player->m_flagBits = kPlayerLocal;
        LatePlayerArrival(manager, player, 0);
        player->DestroyBody();
        MemSys::Hub::Pull()->Dealloc(player);
        // On the internet the host checks its own CD key too: an answer nobody reads (no instance). NOT ORIGINAL:
        // not in the THUGPRO peer mode (TP-R12).
        if (manager->InInternetMode() && !Thugpro::PeerMode()) {
            char key[0x80];
            char response[0x80];
            GetCdKey(key);
            ComputeCdKeyResponse(key, manager->m_challenge, response);
            AuthenticateUser(0, context->m_senderConn->m_remoteIp, manager->m_challenge, response, nullptr);
        }
        return NetLink::kResultPassOn;
    }
    if (!msg->observer) {
        // NOT ORIGINAL: the appearance read from a copy as long as the most a request holds (a request cut short
        // reads zeros, not past the message).
        uint8_t appearance[kAppearanceSize];
        uint32_t size = context->m_bodySize - kJoinInfoHeaderSize;
        if (size > kAppearanceSize)
            size = kAppearanceSize;
        std::memset(appearance, 0, kAppearanceSize);
        std::memcpy(appearance, msg->appearance, size);
        player->m_account->DecodeFromBytes(appearance);
    }
    LatePlayerArrival(manager, player, kDeferredJoinFrames);
    player->DestroyBody();
    MemSys::Hub::Pull()->Dealloc(player);
    // Busy until it's in: no game messages meanwhile.
    context->m_senderConn->m_curStanding = (context->m_senderConn->m_curStanding & ~Conn::kConnReady) | Conn::kStatus2;
    return NetLink::kResultPassOn;
}

// 0x0052E490
int Hub::OnGoAheadMsg(InboundMsgCtx* context)
{
    NetLink::Hub* net = NetSessionSlot();
    NetMessageInfo desc = NewDesc();
    Conn* conn = context->m_senderConn;
    auto* manager = static_cast<Hub*>(context->m_content);
    auto* proceed = reinterpret_cast<ProceedInfo*>(context->m_body);
    if (conn->IsOffMachine()) {
        conn->SetLinkSpeedClass(proceed->broadband ? Conn::kBandwidthBroadband : Conn::kBandwidthModem);
        manager->m_joinIp = proceed->publicIp;
        manager->m_joinPrivateIp = proceed->privateIp;
        manager->m_joinPort = proceed->port;
        if (PresenceLoggedIn(manager)) {
            std::strcpy(LobbyServerName(manager), proceed->serverName);
            char location[0x1000];
            Sprintf(location, g_presenceLocationFormat, manager->m_joinIp, manager->m_joinPrivateIp,
                    manager->m_joinPort, LobbyServerName(manager), LobbyPeerName(manager));
            const char* status = Scripts::FetchText(manager->m_joinMode == 0 ? kStatusPlaying : kStatusObserving);
            SetPresence(manager, 2, status, location);
        }
    }
    SetMaximumPlayers(proceed->maxPlayers);
    const char* name = PreferenceText(manager, kNetworkId);
    JoinInfo* info = &g_joinInfo; // (the original's is on its stack: 4 KB)
    std::memset(info, 0, kJoinInfoHeaderSize);
    // NOT ORIGINAL: in the THUGPRO peer mode the profile is our machine id, as THUGPRO's client sends it (TP-R12).
    info->profile = Thugpro::PeerMode() ? static_cast<int32_t>(Thugpro::MachineId())
                                        : *reinterpret_cast<int32_t*>(static_cast<uint8_t*>(manager->m_presenceService) + 4);
    info->rating = Rating(manager);
    info->observer = manager->m_joinMode == kJoinModeObserve;
    info->version = kVersion;
    info->canQueueUp = true;
    bool narrowband;
    if (net->GetNetLinkKind() == kConnectionTypeModem) {
        narrowband = true;
        info->broadband = false;
    } else {
        narrowband = false;
        info->broadband = true;
    }
    std::strncpy(info->name, name, kNameSize - 1);
    info->name[kNameSize - 1] = '\0';
    std::strcpy(info->password, JoinPassword());
    int size = 0;
    if (!info->observer) {
        auto* profile = Call<GameObjects::CRiderIdentity*>(CodeAddress(static_cast<GameObjects::CRiderIdentity* (GameModules::Skate::*)(int)>(&GameModules::Skate::FetchAccount)), reinterpret_cast<void*>(GameModules::g_skateModule), 0);
        NetLink::ConnSearch search;
        search.m_vtable = NetLink::kConnSearchVtable;
        search.m_listLink = nullptr;
        if (context->m_receiverApp->BeginConnWalk(&search)->GetLinkSpeedClass() == Conn::kBandwidthModem)
            narrowband = true; // (a narrowband server gets no face)
        size = profile->EmitIntoBuffer(info->appearance, kAppearanceSize, narrowband);
    }
    desc.m_content = info;
    desc.m_byteCount = static_cast<uint16_t>(size + kJoinInfoHeaderSize);
    desc.m_ident = kMsgJoinRequest;
    desc.m_compression = 2; // LZSS (in the peer mode it goes as THUGPRO's LZHAM: CompressMessage)
    if (context->m_senderConn->IsOffMachine())
        desc.m_queue = NetMessageInfo::kLaneOrdered;
    AsClient(context->m_receiverApp)->SendToHost(&desc);
    manager->m_joinPhase = kJoinStateJoining;
    return NetLink::kResultConsumed;
}

// 0x0052E1E0
int Hub::OnTurnedAwayMsg(InboundMsgCtx* context)
{
    auto* manager = static_cast<Hub*>(context->m_content);
    const char* msg = context->m_body;
    int reason = static_cast<int8_t>(msg[0]);
    switch (reason) {
    case kRefusedBanned:
        manager->m_refusedReason = kReasonBanned;
        break;
    case kRefusedFull:
        manager->m_refusedReason = kReasonFull;
        break;
    case kRefusedFullObservers:
        manager->m_refusedReason = kReasonFullObservers;
        break;
    case kRefusedCdKey: {
        if (manager->m_joinPhase == kJoinStateCdKey) {
            // Answered already: refused.
            manager->m_refusedReason = Checksums::HashFromText(g_netReasonGeneral);
            break;
        }
        // The host's challenge (after the reason): answered with our key, in a new connection request.
        char key[0x80];
        char response[0x80];
        // NOT ORIGINAL: the challenge read from the message's own 9 bytes (NUL-ended here).
        char challenge[kJoinRefusedSize] = {};
        uint32_t challengeSize = context->m_bodySize > 1 ? context->m_bodySize - 1 : 0;
        std::memcpy(challenge, msg + 1, challengeSize < kJoinRefusedSize - 1u ? challengeSize : kJoinRefusedSize - 1u);
        GetCdKey(key);
        ComputeCdKeyResponse(key, challenge, response);
        NetLink::Hub* net = NetSessionSlot();
        ConnectInfo info;
        std::memset(&info, 0, sizeof(info));
        std::strcpy(info.cdKeyResponse, response);
        const char* name = nullptr; // (not used)
        NetGameOptions(manager)->FetchSetting(kNetworkId)->FetchText(g_uiString,
                                                                            &name, 1);
        info.rank = Rank(manager);
        info.observer = manager->m_joinMode == kJoinModeObserve;
        info.version = kVersion;
        info.canQueueUp = true;
        info.broadband = net->GetNetLinkKind() != kConnectionTypeModem;
        std::strcpy(info.password, JoinPassword());
        Thugpro::ConnectRequest request;
        SendConnectionRequest(AsClient(manager->m_clientApp), info, &request);
        manager->m_joinPhase = kJoinStateCdKey;
        return NetLink::kResultPassOn;
    }
    case kRefusedPassword:
        if (manager->m_joinPhase == kJoinStateTryingPassword) {
            manager->m_refusedReason = kReasonWrongPassword;
            break;
        }
        // Ask for it.
        MemSys::Hub::Pull()->EnterHeapScope(MemSys::Hub::Pull()->m_frontEndHeap);
        Scripts::LaunchScript(kCreateEnterPasswordControl, nullptr, nullptr, false, nullptr);
        manager->m_joinExpiryJob->DropOut();
        MemSys::Hub::Pull()->LeaveHeapScope();
        return NetLink::kResultPassOn;
    case kRefusedInProgress:
        Scripts::LaunchScript(kCreateGameInProgressDialog, nullptr, nullptr, false, nullptr);
        manager->m_joinExpiryJob->DropOut();
        return NetLink::kResultPassOn;
    case kRefusedRankLow:
        manager->m_refusedReason = kReasonRankLow;
        break;
    case kRefusedRankHigh:
        manager->m_refusedReason = kReasonRankHigh;
        break;
    default:
        // (the wrong version too.) NOT ORIGINAL: THUGPRO's reasons 31-37 in words (TP-R13).
        if (uint32_t text = Thugpro::RefusedReasonText(reason))
            manager->m_refusedReason = text;
        else
            manager->m_refusedReason = kReasonDefault;
        break;
    }
    CancelJoinServer(manager);
    manager->m_joinPhase = kJoinStateRefused;
    return NetLink::kResultMsgFreed;
}

} // namespace OnlinePlay
