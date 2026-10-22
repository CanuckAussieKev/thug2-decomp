// The network game's join (GameNet::Manager): the clients and the server made, the connection request and the join
// request both ways, ok_to_join, join proceed / refused, GameSpy's CD-key answer. See gamenet_manager.h.
//
// NOT ORIGINAL in it: the THUGPRO peer mode (thugpro_join.h) - our requests carry THUGPRO's trailer and machine id,
// THUGPRO's refusal reasons are shown in words, and as host the GameSpy CD-key check is left out; bounds on what the
// host reads from a request.

#include <cstring>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/singleton.h"
#include "game/core/task.h"
#include "game/net/chat.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/net/net_conn.h"
#include "game/net/net_dispatcher.h"
#include "game/net/net_lists.h"
#include "game/net/net_manager.h"
#include "game/net/net_server.h"
#include "game/net/thugpro_join.h"
#include "game/net/thugpro_peer.h"
#include "game/script/cscript.h"
#include "game/script/preferences.h"
#include "game/script/struct.h"
#include "game/skate/profile_manager.h"
#include "game/sys/mlp.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"

#pragma warning(disable : 4996) // (strcpy / strncpy: the original's own copies into its fixed fields)

namespace GameNet {

namespace {

using Net::App;
using Net::Conn;
using Net::MsgDesc;
using Net::MsgHandlerCode;
using Net::MsgHandlerContext;

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

// App flags (CreateNewAppClient / Server).
constexpr int kAppBroadcast = 0x01;
constexpr int kAppLocal = 0x08;
constexpr int kAppDynamicResend = 0x04;
constexpr int kAppForeign = 0x10;
constexpr int kAppSecure = 0x20;

// Handler flags.
constexpr uint32_t kHandleForeign = 1;
constexpr uint32_t kHandleLate = 2;
constexpr uint32_t kHandleCrcMismatch = 4;
constexpr int kNormalPriority = 0x80;
constexpr int kHighestPriority = 0xFF;
constexpr int kLowestPriority = 0;

// The player info being made (GameNet::NewPlayerInfo, 0x44 bytes; 0x005434D0 / 0x00543560).
struct NewPlayerInfo {
    char name[kNameSize];   // +0x00
    int8_t objId;           // +0x10
    uint8_t pad11[3];
    Conn* conn;             // +0x14
    uint32_t unknown18;
    Obj::CSkaterProfile* profile; // +0x1C
    uint32_t flags;         // +0x20 (1 local player, 4 observer, 8 pending)
    uint32_t unknown24[2];
    int32_t profileId;      // +0x2C
    int32_t rating;         // +0x30
    uint32_t unknown34[3];
    int32_t vehicleControlType; // +0x40
};
static_assert(sizeof(NewPlayerInfo) == kPlayerInfoSize && offsetof(NewPlayerInfo, flags) == 0x20 &&
              offsetof(NewPlayerInfo, vehicleControlType) == 0x40);
constexpr uint32_t kPlayerLocal = 1;
constexpr uint32_t kPlayerObserver = 4;
constexpr uint32_t kPlayerPending = 8;

// GameSpy CD-key's instance data for ClientAuthorized (12 bytes, operator new).
// (the join request being sent: the original's is on its stack)
JoinInfo g_joinInfo;

struct AuthInstance {
    App* app;
    Manager* manager;
    Conn* conn;
};

// The global passwords: the one being typed (12) and the one in use (10; resent with every request).
char* PendingPassword() { return &GameVariable<char>(0x007CCDFC); }
char* JoinPassword() { return &GameVariable<char>(0x007CCE08); }

Net::Manager* NetManager() { return Net::Manager::Instance(); }
Tsk::Stack* LogicTasks()
{
    return reinterpret_cast<Tsk::Stack*>(reinterpret_cast<uint8_t*>(GameVariable<Mlp::Manager*>(0x00696D68)) + 4);
}
int MilliSeconds() { return GameFunction<int(__cdecl*)()>(0x005BE970)(); }
uint32_t LocalAddress()
{
    return GameFunction<uint32_t(__stdcall*)(const char*)>(0x005BEAAE)(NetManager()->GetLocalIP()); // inet_addr
}

// The manager's methods that aren't source yet.
template <typename R, typename... A>
R Call(uint32_t address, const void* object, A... args)
{
    return GameFunction<R(__thiscall*)(const void*, A...)>(address)(object, args...);
}
int NumObservers(Manager* m) { return Call<int>(0x00539C70, m); }
int MaxObservers(Manager* m) { return Call<int>(0x00532D60, m); }
int NumPlayers(Manager* m) { return Call<int>(0x00539B00, m); }
int MaxPlayers(Manager* m) { return Call<int>(0x00532C50, m); }
int RankLimit(Manager* m) { return Call<int>(0x00532B30, m); }
int RankTest(Manager* m) { return Call<int>(0x00532AE0, m); }
const char* Password(Manager* m) { return Call<const char*>(0x00534310, m); }
void DeferredNewPlayer(Manager* m, NewPlayerInfo* player, int frames) { Call<void>(0x0053A310, m, player, frames); }
void SetReadyToPlay(Manager* m, bool ready) { Call<void>(0x00537140, m, ready); }
void RandomizeSkaterStartingPoints(Manager* m) { Call<void>(0x005359F0, m); }
void CancelJoinServer(Manager* m) { Call<void>(0x0053C6E0, m); }
uint32_t GameModeName() { return Call<uint32_t>(0x005532A0, GameVariable<void*>(0x007CE470)); }
void SetMaximumPlayers(int count) { Call<void>(0x005531B0, GameVariable<void*>(0x007CE470), count); }
int8_t Rank(Manager* m) { return static_cast<int8_t>(Call<int>(0x0054D2D0, m->m_statsMan)); }
int32_t Rating(Manager* m)
{
    // (the stats' +0x14 through its pointer member, 0x0054CA80 / 0x00469490)
    auto* member = Call<Spt::PointerMember*>(0x0054CA80, m->m_statsMan);
    return static_cast<int32_t>(reinterpret_cast<uintptr_t>(member->Get()));
}
bool GameInProgress(Manager* m) { return GameModeName() != kNetLobby && !m->m_gameOver; }

Prefs::Preferences* NetworkPreferences(Manager* m) { return reinterpret_cast<Prefs::Preferences*>(m->m_networkPreferences); }
const char* PreferenceText(Manager* m, uint32_t name)
{
    const char* text = nullptr;
    NetworkPreferences(m)->GetPreference(name)->GetString(kUiString, &text, 1);
    return text;
}

// The presence (GameSpy) and the lobby (not source yet).
bool PresenceLoggedIn(Manager* m) { return *(static_cast<uint8_t*>(m->m_buddyMan) + 0x10) != 0; }
void SetPresence(Manager* m, int status, const char* location, const char* text)
{
    Call<void>(0x00548320, m->m_buddyMan, status, location, text);
}
char* LobbyServerName(Manager* m) { return static_cast<char*>(m->m_lobbyMan) + 0xB4; }
char* LobbyPeerName(Manager* m) { return static_cast<char*>(m->m_lobbyMan) + 0x34; }

// GameSpy's CD-key SDK (gcd) and THUG2's key.
void GetCdKey(char* key) { GameFunction<void(__cdecl*)(char*)>(0x0052D6A0)(key); }
void ComputeCdKeyResponse(const char* key, const char* challenge, char* response)
{
    GameFunction<void(__cdecl*)(const char*, const char*, char*)>(0x005F8950)(key, challenge, response);
}
void AuthenticateUser(int localId, uint32_t ip, const char* challenge, const char* response, void* instance)
{
    using Callback = void(__cdecl*)(int, int, int, char*, void*);
    GameFunction<void(__cdecl*)(int, int, uint32_t, const char*, const char*, Callback, void*)>(0x005F9620)(
        kGameSpyGameId, localId, ip, challenge, response, &Manager::ClientAuthorized, instance);
}

void Sprintf(char* out, const char* format, uint32_t a, uint32_t b, int c, const char* d, const char* e)
{
    GameFunction<int(__cdecl*)(char*, const char*, ...)>(0x006234FE)(out, format, a, b, c, d, e);
}

MsgDesc NewDesc()
{
    MsgDesc desc;
    desc.Construct();
    return desc;
}

// The connection request (all four senders): THUG2's, or in the THUGPRO peer mode with THUGPRO's trailer (TP-R12).
// `request` must outlive the send (the message is copied when queued).
void SendConnectionRequest(Net::Client* client, const ConnectInfo& info, Thugpro::ConnectRequest* request)
{
    MsgDesc desc = NewDesc();
    if (Thugpro::PeerMode()) {
        desc.m_length = Thugpro::MakeConnectRequest(info, request);
        desc.m_data = request;
        desc.m_compression = 3; // (THUGPRO's LZHAM, as its client sends it - only if smaller)
    } else {
        request->thug2 = info;
        desc.m_data = &request->thug2;
        desc.m_length = sizeof(ConnectInfo);
    }
    desc.m_id = kMsgConnectionRequest;
    client->EnqueueMessageToServer(&desc);
}

Net::Client* AsClient(App* app) { return static_cast<Net::Client*>(app); }

// The handlers a joining client adds (0x00534480): on every client, then on client 0 only.
struct HandlerEntry {
    uint8_t id;
    uint32_t code; // (an address: a game handler, or one of ours - Handler() maps those)
    uint32_t flags;
    bool managerData;
    int priority;
};
constexpr uint32_t kOurs = 0x80000000; // (+ an index into OurHandler)
MsgHandlerCode OurHandler(uint32_t code)
{
    switch (code) {
    case kOurs + 0: return &Manager::HandleJoinRefused;
    case kOurs + 1: return &Manager::HandleJoinProceed;
    case kOurs + 2: return &GameNet::HandleChat;
    case kOurs + 3: return &Manager::HandleConnection;
    case kOurs + 4: return &Manager::HandleJoinRequest;
    default: return reinterpret_cast<MsgHandlerCode>(code);
    }
}
constexpr HandlerEntry kClientHandlers[] = {
    {0x42, 0x0052E1A0, kHandleLate, true, kNormalPriority},  // proceed to play
    {0x4D, 0x0052D800, kHandleLate, true, kNormalPriority},  // observe proceed
    {0x4E, 0x00532390, kHandleLate, true, kNormalPriority},  // observe refused
    {kMsgJoinRefused, kOurs + 0, kHandleLate, true, kNormalPriority},
    {kMsgJoinProceed, kOurs + 1, kHandleLate, true, kNormalPriority},
    {0x25, 0x0052D7B0, kHandleLate, true, kNormalPriority},  // ready query
    {0x26, 0x0052D7D0, kHandleLate, true, kNormalPriority},  // ready response
    {0x53, 0x0052DCC0, kHandleLate, true, kNormalPriority},  // player info ack request
    {0x58, 0x0052E870, 0, true, kNormalPriority},            // the king dropped the crown
    {0x75, 0x00530CB0, 0, true, kNormalPriority},            // a player dropped a flag
    {0x3F, 0x0052D950, kHandleLate, true, kNormalPriority},  // panel message
    {0x5E, 0x0052D910, kHandleLate, true, kNormalPriority},  // auto server notification
    {0x5F, 0x005300C0, kHandleLate, true, kNormalPriority},  // first-come-first-served assignment
    {0x68, 0x00531A80, kHandleLate, true, kNormalPriority},  // team change
    {0x69, 0x0052F2D0, kHandleLate, true, kNormalPriority},  // number of teams
    {0x4C, kOurs + 2, kHandleLate, false, kNormalPriority},  // chat
    {0x7C, 0x0052DAD0, kHandleLate, true, kNormalPriority},  // challenge
};
constexpr HandlerEntry kGameClientHandlers[] = {
    {0x6D, 0x00531950, 0, true, kNormalPriority},    // selected goals
    {0x34, 0x005313C0, 0, true, kNormalPriority},    // game info
    {0x7A, 0x0052D930, 0, true, kNormalPriority},    // kill the team flags
    {0x6E, 0x00531C80, 0, true, kNormalPriority},    // a goal beaten (relayed)
    {0x6F, 0x00531F10, 0, true, kNormalPriority},    // a goal started (relayed)
    {0x70, 0x0052DAC0, 0, true, kNormalPriority},    // toggle pro set (nothing)
    {0x46, 0x005311F0, 0, true, kHighestPriority},   // change level
    {0x46, 0x0052DAC0, 0, true, kHighestPriority},   // (and nothing)
    {0x6C, 0x0052EB70, 0, true, kHighestPriority},   // level data
    {0x88, 0x00530E50, kHandleCrcMismatch, true, kHighestPriority}, // rail data
    {0x87, 0x00530FF0, kHandleCrcMismatch, true, kHighestPriority}, // goals data
    {0x80, 0x0052F370, 0, true, kNormalPriority},    // cheat checksum request
};
constexpr HandlerEntry kRemoteGameClientHandlers[] = {
    {0x22, 0x0052DB80, 0, true, kNormalPriority},    // player create
    {0x79, 0x0052D830, 0, true, kNormalPriority},    // join accepted
};
constexpr HandlerEntry kGameClientHandlers2[] = {
    {0x7B, 0x0052DCA0, 0, true, kNormalPriority},    // wait n seconds
    {0x55, 0x0052E760, 0, true, kNormalPriority},    // new king
    {0x72, 0x0052D8B0, 0, true, kNormalPriority},    // captured flag
    {0x74, 0x0052D850, 0, true, kNormalPriority},    // stole flag
    {0x71, 0x0052D880, 0, true, kNormalPriority},    // took flag
    {0x73, 0x0052D8E0, 0, true, kNormalPriority},    // retrieved flag
    {0x5A, 0x0052ED80, 0, true, kNormalPriority},    // game over
    {0x6A, 0x0052EDD0, 0, true, kNormalPriority},    // end game
    {0x23, 0x0052D980, kHandleForeign | kHandleLate, true, kHighestPriority}, // object updates (their length)
    {0x8D, 0x00532280, 0, true, kNormalPriority},    // a skater eliminated
    {0x8E, 0x00532280, 0, true, kNormalPriority},
};
// The server's lobby handlers (0x0053C380; taken out once the game starts).
constexpr HandlerEntry kServerHandlers[] = {
    {kMsgConnectionRequest, kOurs + 3, kHandleForeign | kHandleLate, true, kLowestPriority},
    {0x90, 0x0052EBB0, 0, true, kHighestPriority},           // a scavenger-hunt object picked up
    {kMsgJoinRequest, kOurs + 4, kHandleLate, true, kNormalPriority},
    {0x11, 0x0052DD40, kHandleForeign | kHandleLate, true, kNormalPriority}, // find server
    {0x0B, 0x0052FCC0, kHandleLate, true, kNormalPriority},  // disconnection request
    {0x4A, 0x0052E060, kHandleLate, true, kNormalPriority},  // observe
    {0x26, 0x0052D7D0, kHandleLate, true, kNormalPriority},  // ready response
    {0x54, 0x00532460, 0, true, kNormalPriority},            // player info ack
    {0x57, 0x0052FDA0, 0, true, kNormalPriority},            // moved to a restart
    {0x59, 0x0052EC40, 0, true, kNormalPriority},            // the run ended
    {0x60, 0x005301A0, kHandleLate, true, kNormalPriority},  // first-come-first-served requests: start game,
    {0x61, 0x005301A0, kHandleLate, true, kNormalPriority},  // ban player, change level, toggle pro set, goal
    {0x62, 0x005301A0, kHandleLate, true, kNormalPriority},  // selection, end game, number of teams
    {0x63, 0x005301A0, kHandleLate, true, kNormalPriority},
    {0x64, 0x005301A0, kHandleLate, true, kNormalPriority},
    {0x65, 0x005301A0, kHandleLate, true, kNormalPriority},
    {0x66, 0x005301A0, kHandleLate, true, kNormalPriority},
    {0x67, 0x0052EE10, kHandleLate, true, kNormalPriority},  // team change request
    {0x6B, 0x0052E9E0, kHandleLate, true, kNormalPriority},  // level request
    {0x6E, 0x0052EF40, kHandleLate, true, kNormalPriority},  // a goal beaten
    {0x6F, 0x0052F1A0, kHandleLate, true, kNormalPriority},  // a goal started
    {0x6C, 0x0052EB70, 0, true, kHighestPriority},           // level data
    {0x88, 0x00530E50, 0, true, kHighestPriority},           // rail data
    {0x7D, 0x0052F3D0, 0, true, kNormalPriority},            // challenge response
    {0x81, 0x00532170, 0, true, kNormalPriority},            // cheat checksum response
    {0x8B, 0x0052F400, 0, true, kNormalPriority},            // rails / goals data requests
    {0x8A, 0x0052F400, 0, true, kNormalPriority},
};

template <size_t N>
void AddHandlers(App* app, Manager* manager, const HandlerEntry (&entries)[N])
{
    for (const HandlerEntry& entry : entries)
        app->m_dispatcher.AddHandler(entry.id, OurHandler(entry.code), entry.flags,
                                     entry.managerData ? manager : nullptr, entry.priority);
}

void SendRefused(App* app, Conn* conn, const char* data)
{
    app->SendMessageTo(kMsgJoinRefused, kJoinRefusedSize, data, conn->m_ip, conn->m_port, 0);
}

} // namespace

// 0x0052F560
bool Manager::OkToJoin(int& reason, ConnectInfo* msg, Conn* conn, bool checkPassword, bool checkVersion,
                       bool checkCdKey)
{
    if (m_server->IsConnectionBanned(conn)) {
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
    if (conn->IsRemote()) {
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
    if (!msg->willingToWait && !msg->observer && GameInProgress(this)) {
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
void Manager::JoinServer(uint32_t ip, uint16_t port, int index)
{
    Net::Manager* net = NetManager();
    const char* name = PreferenceText(this, kNetworkId); // (not used)
    (void)name;
    App* client = ClientSlot(index);
    ConnectInfo info;
    std::memset(&info, 0, sizeof(info)); // (the original sends its stack's garbage in the fields it doesn't set)
    if (client->IsLocal()) {
        // The host's own client: its connection and the server's know each other.
        Conn* serverConn = m_server->NewConnection(static_cast<int>(ip), port, Conn::kFlagLocal);
        Conn* conn = client->NewConnection(static_cast<int>(ip), port, Conn::kFlagLocal);
        client->AliasConnections(serverConn, conn);
    } else {
        AsClient(client)->ConnectToServer(ip, port);
        client->NewConnection(static_cast<int>(ip), port, Conn::kFlagRemote);
        m_joinStartTime = MilliSeconds();
        LogicTasks()->AddTask(m_joinTimeoutTask);
        m_joinState = kJoinStateConnecting;
        LogicTasks()->AddTask(m_joinStateTask);
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
    if (!client->IsLocal())
        std::memset(PendingPassword(), 0, kPasswordSize);
    info.willingToWait = false;
    info.broadband = net->GetConnectionType() != kConnectionTypeModem;
    Thugpro::ConnectRequest request;
    SendConnectionRequest(AsClient(client), info, &request);
    client->SendData(false);

    AddHandlers(client, this, kClientHandlers);
    if (index == 0) {
        AddHandlers(m_client, this, kGameClientHandlers);
        if (!m_client->IsLocal())
            AddHandlers(m_client, this, kRemoteGameClientHandlers);
        AddHandlers(m_client, this, kGameClientHandlers2);
    }
    m_observerCommand = 0;
}

// 0x00534AF0
void Manager::ReattemptJoin(App* client)
{
    ConnectInfo info;
    std::memset(&info, 0, sizeof(info));
    info.observer = m_joinMode == kJoinModeObserve;
    info.version = kVersion;
    info.willingToWait = true;
    info.password[0] = '\0';
    info.broadband = NetManager()->GetConnectionType() != kConnectionTypeModem;
    if (InInternetMode())
        info.rank = Rank(this);
    Thugpro::ConnectRequest request;
    SendConnectionRequest(AsClient(client), info, &request);
    client->SendData(false);
    m_joinStartTime = MilliSeconds();
    LogicTasks()->AddTask(m_joinTimeoutTask);
    m_joinState = kJoinStateJoining;
}

// 0x00534BD0
void Manager::ReattemptJoinWithPassword(const char* password)
{
    std::strcpy(JoinPassword(), password);
    const char* name = PreferenceText(this, kNetworkId); // (not used)
    (void)name;
    ConnectInfo info;
    std::memset(&info, 0, sizeof(info));
    info.rank = Rank(this);
    info.observer = m_joinMode == kJoinModeObserve;
    info.version = kVersion;
    info.willingToWait = true;
    info.broadband = NetManager()->GetConnectionType() != kConnectionTypeModem;
    std::strcpy(info.password, password);
    Thugpro::ConnectRequest request;
    SendConnectionRequest(AsClient(m_client), info, &request);
    m_joinStartTime = MilliSeconds();
    LogicTasks()->AddTask(m_joinTimeoutTask);
    m_joinState = kJoinStateJoiningWithPassword;
}

// 0x00537220
App* Manager::SpawnClient(bool broadcast, bool local, bool secure, int index)
{
    Net::Manager* net = NetManager();
    int flags = broadcast ? kAppBroadcast : 0;
    flags |= local ? kAppLocal : kAppDynamicResend | kAppForeign;
    if (secure)
        flags |= kAppSecure;
    App* client = net->CreateNewAppClient(index, &GameVariable<const char>(0x0064CA00), kJoinPort, // (its name)
                                          static_cast<int>(LocalAddress()), flags);
    ClientSlot(index) = client;
    if (index == 0 && client) {
        m_latestReadyQuery = 0;
        m_proSetFlags = 0;
        m_camPlayer = nullptr;
        SetReadyToPlay(this, false);
    }
    m_observerInputHandler = nullptr;
    m_gameOver = false;
    m_gamePending = false;
    return ClientSlot(index);
}

// 0x00534430
App* Manager::SpawnMatchClient()
{
    Net::Manager* net = NetManager();
    App* client = net->CreateNewAppClient(0, &GameVariable<const char>(0x0064C958), kJoinPort,
                                          static_cast<int>(LocalAddress()),
                                          kAppBroadcast | kAppDynamicResend | kAppForeign | kAppSecure);
    m_matchClient = client;
    client->m_dispatcher.AddHandler(kMsgServerResponse, reinterpret_cast<MsgHandlerCode>(0x005475C0),
                                    kHandleForeign | kHandleLate, this, kNormalPriority);
    return m_matchClient;
}

// 0x0053C380
App* Manager::SpawnServer(bool local, bool secure)
{
    Net::Manager* net = NetManager();
    int flags = kAppBroadcast | kAppDynamicResend | kAppForeign | (local ? kAppLocal : 0);
    if (secure)
        flags |= kAppSecure;
    App* server = net->CreateNewAppServer(0, &GameVariable<const char>(0x0064CA00), kMaxConnections, kHostPort,
                                          static_cast<int>(LocalAddress()), flags);
    m_server = server;
    if (m_hostMode != 0)
        m_hostMode = 0; // serve
    if (server) {
        LogicTasks()->AddTask(m_serverAddNewPlayersTask);
        AddHandlers(m_server, this, kServerHandlers);
        m_lastScoreUpdate = 0;
        m_waitingForGameToStart = false;
        RandomizeSkaterStartingPoints(this);
        m_observerInputHandler = nullptr;
    }
    return m_server;
}

// 0x0052F790
int Manager::HandleConnection(MsgHandlerContext* context)
{
    Conn* conn = context->m_Conn;
    auto* manager = static_cast<Manager*>(context->m_data);
    auto* msg = reinterpret_cast<ConnectInfo*>(context->m_Msg);
    if (!conn->IsRemote()) {
        // The host's own client: proceed at once (only the player count filled in).
        MsgDesc desc = NewDesc();
        ProceedInfo proceed;
        std::memset(&proceed, 0, sizeof(proceed));
        proceed.maxPlayers = static_cast<int8_t>(MaxPlayers(manager));
        desc.m_data = &proceed;
        desc.m_id = kMsgJoinProceed;
        desc.m_length = sizeof(proceed);
        context->m_App->EnqueueMessage(conn->m_handle, &desc);
        return Net::kHandlerContinue;
    }
    if (!(context->m_PacketFlags & kHandleForeign))
        return Net::kHandlerMsgDone;
    Net::Manager* net = NetManager();
    if (!context->m_App->AcceptsForeignConnections())
        return Net::kHandlerMsgDone;
    // NOT ORIGINAL: a request shorter than THUG2's isn't read (the original reads past it).
    if (context->m_MsgLength < sizeof(ConnectInfo))
        return Net::kHandlerMsgDone;
    Thugpro::ConnectTrailer trailer;
    if (Thugpro::PeerMode() && Thugpro::ReadConnectTrailer(msg, context->m_MsgLength, &trailer))
        Log("THUGPRO join (TP-R12): a THUGPRO client asks to join: version %08X, release %u, scripts %08X%s, machine %08X",
            trailer.version, trailer.release, trailer.scriptsHash, trailer.modifiedScripts ? " (modified)" : "",
            trailer.machineId);
    int reason = 0;
    if (!manager->OkToJoin(reason, msg, conn, true, true, true)) {
        char refused[32] = {}; // (10 sent: the reason, and the challenge for kRefusedCdKey)
        refused[0] = static_cast<char>(reason);
        if (reason == kRefusedCdKey)
            std::strcpy(refused + 1, manager->m_challenge);
        SendRefused(context->m_App, conn, refused);
        return Net::kHandlerHalt;
    }
    Conn* joined = context->m_App->NewConnection(static_cast<int>(conn->m_ip), conn->m_port, Conn::kFlagRemote);
    context->m_Conn = joined;
    joined->SetBandwidthType(msg->broadband ? Conn::kBandwidthBroadband : Conn::kBandwidthModem);
    joined->SetSendInterval(static_cast<uint32_t>(1000 / GameVariable<int>(0x00684FB4) * 2)); // two frames
    // NOT ORIGINAL: in the THUGPRO peer mode no GameSpy CD-key check (TP-R12): proceed as on a LAN.
    if (manager->InInternetMode() && !Thugpro::PeerMode()) {
        auto* instance = static_cast<AuthInstance*>(Mem::New(sizeof(AuthInstance)));
        if (instance)
            std::memset(instance, 0, sizeof(*instance));
        instance->manager = manager;
        instance->app = context->m_App;
        instance->conn = joined;
        AuthenticateUser(static_cast<int>(joined->m_id), context->m_Conn->m_ip, manager->m_challenge,
                         msg->cdKeyResponse, instance);
        return Net::kHandlerContinue;
    }
    ProceedInfo proceed;
    std::memset(&proceed, 0, sizeof(proceed)); // (the original leaves the addresses as its stack had them on a LAN)
    proceed.maxPlayers = static_cast<int8_t>(MaxPlayers(manager));
    proceed.broadband = net->GetConnectionType() != kConnectionTypeModem;
    if (manager->InInternetMode()) {
        proceed.port = manager->m_joinPort;
        proceed.privateIp = manager->m_joinPrivateIp;
        proceed.publicIp = manager->m_joinIp;
    }
    std::strncpy(proceed.serverName, PreferenceText(manager, kServerName), kServerNameSize - 1);
    proceed.serverName[kServerNameSize - 1] = '\0';
    context->m_App->SendMessageTo(kMsgJoinProceed, sizeof(proceed), &proceed, joined->m_ip, joined->m_port, 0);
    return Net::kHandlerContinue;
}

// 0x00536530
void Manager::ClientAuthorized(int /*gameId*/, int /*localId*/, int authenticated, char* /*error*/, void* data)
{
    auto* instance = static_cast<AuthInstance*>(data);
    if (!instance)
        return;
    if (!authenticated) {
        char refused[kJoinRefusedSize] = {};
        refused[0] = kRefusedCdKey;
        SendRefused(instance->app, instance->conn, refused);
        instance->conn->Invalidate();
    } else {
        Manager* manager = instance->manager;
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
        instance->app->SendMessageTo(kMsgJoinProceed, sizeof(proceed), &proceed, instance->conn->m_ip,
                                     instance->conn->m_port, 0);
    }
    Mem::Manager::Get()->Free(instance);
}

// 0x0052FA10
int Manager::HandleJoinRequest(MsgHandlerContext* context)
{
    Conn* conn = context->m_Conn;
    auto* manager = static_cast<Manager*>(context->m_data);
    auto* msg = reinterpret_cast<JoinInfo*>(context->m_Msg);
    void* skate = GameVariable<void*>(0x007CE478);
    context->m_Acknowledge = true;
    // NOT ORIGINAL: a request shorter than THUG2's header isn't read (the original reads past it).
    if (context->m_MsgLength < kJoinInfoHeaderSize)
        return Net::kHandlerMsgDone;
    int reason = 0;
    if (conn->IsRemote() && !manager->OkToJoin(reason, msg, conn, false, false, false)) {
        MsgDesc desc = NewDesc();
        char refused[kJoinRefusedSize] = {};
        refused[0] = static_cast<char>(reason);
        desc.m_data = refused;
        desc.m_length = kJoinRefusedSize;
        desc.m_id = kMsgJoinRefused;
        context->m_App->EnqueueMessage(conn->m_handle, &desc);
        context->m_Conn->Invalidate();
        return Net::kHandlerHalt;
    }
    void* memory = Mem::Calloc(kPlayerInfoSize);
    auto* player = memory ? Call<NewPlayerInfo*>(0x005434D0, memory) : nullptr;
    std::strncpy(player->name, msg->name, kNameSize - 1);
    player->name[kNameSize - 1] = '\0';
    player->objId = static_cast<int8_t>(manager->NextPlayerObjectId());
    player->conn = context->m_Conn;
    player->flags = 0;
    player->profileId = msg->profile;
    player->rating = msg->rating;
    player->vehicleControlType = msg->vehicleControlType;
    player->unknown34[0] = 0;
    if (msg->observer)
        player->flags = kPlayerObserver;
    else if (GameInProgress(manager))
        player->flags |= kPlayerPending | kPlayerObserver;
    if (context->m_Conn->IsLocal()) {
        // The host's own player: its profile copied through a buffer.
        if (!GameVariable<bool>(0x007CC979))
            Mem::EnsureManager();
        auto* buffer = static_cast<uint8_t*>(Mem::Manager::Get()->Allocate(kAppearanceSize, true, nullptr));
        auto* profile = Call<Obj::CSkaterProfile*>(0x00564FC0, skate, static_cast<int>(player->objId));
        profile->WriteToBuffer(buffer, kAppearanceSize, 0);
        player->profile->ReadFromBuffer(buffer);
        Mem::Manager::Get()->Free(buffer);
        player->flags = kPlayerLocal;
        DeferredNewPlayer(manager, player, 0);
        Call<void>(0x00543560, player);
        Mem::Manager::Get()->Free(player);
        // On the internet the host checks its own CD key too: an answer nobody reads (no instance). NOT ORIGINAL:
        // not in the THUGPRO peer mode (TP-R12).
        if (manager->InInternetMode() && !Thugpro::PeerMode()) {
            char key[0x80];
            char response[0x80];
            GetCdKey(key);
            ComputeCdKeyResponse(key, manager->m_challenge, response);
            AuthenticateUser(0, context->m_Conn->m_ip, manager->m_challenge, response, nullptr);
        }
        return Net::kHandlerContinue;
    }
    if (!msg->observer) {
        // NOT ORIGINAL: the appearance read from a copy as long as the most a request holds (a request cut short
        // reads zeros, not past the message).
        uint8_t appearance[kAppearanceSize];
        uint32_t size = context->m_MsgLength - kJoinInfoHeaderSize;
        if (size > kAppearanceSize)
            size = kAppearanceSize;
        std::memset(appearance, 0, kAppearanceSize);
        std::memcpy(appearance, msg->appearance, size);
        player->profile->ReadFromBuffer(appearance);
    }
    DeferredNewPlayer(manager, player, kDeferredJoinFrames);
    Call<void>(0x00543560, player);
    Mem::Manager::Get()->Free(player);
    // Busy until it's in: no game messages meanwhile.
    context->m_Conn->m_status = (context->m_Conn->m_status & ~Conn::kStatusReady) | Conn::kStatus2;
    return Net::kHandlerContinue;
}

// 0x0052E490
int Manager::HandleJoinProceed(MsgHandlerContext* context)
{
    Net::Manager* net = NetManager();
    MsgDesc desc = NewDesc();
    Conn* conn = context->m_Conn;
    auto* manager = static_cast<Manager*>(context->m_data);
    auto* proceed = reinterpret_cast<ProceedInfo*>(context->m_Msg);
    if (conn->IsRemote()) {
        conn->SetBandwidthType(proceed->broadband ? Conn::kBandwidthBroadband : Conn::kBandwidthModem);
        manager->m_joinIp = proceed->publicIp;
        manager->m_joinPrivateIp = proceed->privateIp;
        manager->m_joinPort = proceed->port;
        if (PresenceLoggedIn(manager)) {
            std::strcpy(LobbyServerName(manager), proceed->serverName);
            char location[0x1000];
            Sprintf(location, &GameVariable<const char>(0x0064C850), manager->m_joinIp, manager->m_joinPrivateIp,
                    manager->m_joinPort, LobbyServerName(manager), LobbyPeerName(manager));
            const char* status = Script::GetString(manager->m_joinMode == 0 ? kStatusPlaying : kStatusObserving);
            SetPresence(manager, 2, status, location);
        }
    }
    SetMaximumPlayers(proceed->maxPlayers);
    const char* name = PreferenceText(manager, kNetworkId);
    JoinInfo* info = &g_joinInfo; // (the original's is on its stack: 4 KB)
    std::memset(info, 0, kJoinInfoHeaderSize);
    // NOT ORIGINAL: in the THUGPRO peer mode the profile is our machine id, as THUGPRO's client sends it (TP-R12).
    info->profile = Thugpro::PeerMode() ? static_cast<int32_t>(Thugpro::MachineId())
                                        : *reinterpret_cast<int32_t*>(static_cast<uint8_t*>(manager->m_buddyMan) + 4);
    info->rating = Rating(manager);
    info->observer = manager->m_joinMode == kJoinModeObserve;
    info->version = kVersion;
    info->willingToWait = true;
    bool narrowband;
    if (net->GetConnectionType() == kConnectionTypeModem) {
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
        auto* profile = Call<Obj::CSkaterProfile*>(0x00564FC0, GameVariable<void*>(0x007CE478), 0);
        Net::ConnSearch search;
        search.m_vtable = Net::kConnSearchVtable;
        search.m_node = nullptr;
        if (context->m_App->FirstConnection(&search)->GetBandwidthType() == Conn::kBandwidthModem)
            narrowband = true; // (a narrowband server gets no face)
        size = profile->WriteToBuffer(info->appearance, kAppearanceSize, narrowband);
    }
    desc.m_data = info;
    desc.m_length = static_cast<uint16_t>(size + kJoinInfoHeaderSize);
    desc.m_id = kMsgJoinRequest;
    desc.m_compression = 2; // LZSS (in the peer mode it goes as THUGPRO's LZHAM: CompressMessage)
    if (context->m_Conn->IsRemote())
        desc.m_queue = MsgDesc::kQueueSequenced;
    AsClient(context->m_App)->EnqueueMessageToServer(&desc);
    manager->m_joinState = kJoinStateJoining;
    return Net::kHandlerMsgDone;
}

// 0x0052E1E0
int Manager::HandleJoinRefused(MsgHandlerContext* context)
{
    auto* manager = static_cast<Manager*>(context->m_data);
    const char* msg = context->m_Msg;
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
        if (manager->m_joinState == kJoinStateCdKey) {
            // Answered already: refused.
            manager->m_refusedReason = Crc::GenerateCRCFromString(&GameVariable<const char>(0x0064C83C));
            break;
        }
        // The host's challenge (after the reason): answered with our key, in a new connection request.
        char key[0x80];
        char response[0x80];
        // NOT ORIGINAL: the challenge read from the message's own 9 bytes (NUL-ended here).
        char challenge[kJoinRefusedSize] = {};
        uint32_t challengeSize = context->m_MsgLength > 1 ? context->m_MsgLength - 1 : 0;
        std::memcpy(challenge, msg + 1, challengeSize < kJoinRefusedSize - 1u ? challengeSize : kJoinRefusedSize - 1u);
        GetCdKey(key);
        ComputeCdKeyResponse(key, challenge, response);
        Net::Manager* net = NetManager();
        ConnectInfo info;
        std::memset(&info, 0, sizeof(info));
        std::strcpy(info.cdKeyResponse, response);
        const char* name = nullptr; // (not used)
        NetworkPreferences(manager)->GetPreference(kNetworkId)->GetString(&GameVariable<const char>(0x0064C830),
                                                                            &name, 1);
        info.rank = Rank(manager);
        info.observer = manager->m_joinMode == kJoinModeObserve;
        info.version = kVersion;
        info.willingToWait = true;
        info.broadband = net->GetConnectionType() != kConnectionTypeModem;
        std::strcpy(info.password, JoinPassword());
        Thugpro::ConnectRequest request;
        SendConnectionRequest(AsClient(manager->m_client), info, &request);
        manager->m_joinState = kJoinStateCdKey;
        return Net::kHandlerContinue;
    }
    case kRefusedPassword:
        if (manager->m_joinState == kJoinStateTryingPassword) {
            manager->m_refusedReason = kReasonWrongPassword;
            break;
        }
        // Ask for it.
        Mem::Manager::Get()->PushContext(Mem::Manager::Get()->m_frontEndHeap);
        Script::RunScript(kCreateEnterPasswordControl, nullptr, nullptr, false, nullptr);
        manager->m_joinTimeoutTask->Remove();
        Mem::Manager::Get()->PopContext();
        return Net::kHandlerContinue;
    case kRefusedInProgress:
        Script::RunScript(kCreateGameInProgressDialog, nullptr, nullptr, false, nullptr);
        manager->m_joinTimeoutTask->Remove();
        return Net::kHandlerContinue;
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
    manager->m_joinState = kJoinStateRefused;
    return Net::kHandlerMsgDestroyed;
}

} // namespace GameNet

REPLACE(0x0052F560, &GameNet::Manager::OkToJoin);
REPLACE(0x00534480, &GameNet::Manager::JoinServer);
REPLACE(0x00534AF0, &GameNet::Manager::ReattemptJoin);
REPLACE(0x00534BD0, &GameNet::Manager::ReattemptJoinWithPassword);
REPLACE(0x00537220, &GameNet::Manager::SpawnClient);
REPLACE(0x00534430, &GameNet::Manager::SpawnMatchClient);
REPLACE(0x0053C380, &GameNet::Manager::SpawnServer);
REPLACE(0x0052F790, GameNet::Manager::HandleConnection);
REPLACE(0x0052FA10, GameNet::Manager::HandleJoinRequest);
REPLACE(0x0052E490, GameNet::Manager::HandleJoinProceed);
REPLACE(0x0052E1E0, GameNet::Manager::HandleJoinRefused);
REPLACE(0x00536530, GameNet::Manager::ClientAuthorized);
