// The GameSpy SDK in the standalone build: IGN's open-source release (December 2007; BSD-3, see
// THIRD_PARTY_NOTICES.md), adapted to the calls the game makes (gamespy.h; what differs from the SDK version the game
// was built with: docs/gamespy.md). The hooked build calls the exe's own copy (gamespy.h): only FixNick is built there.

#include "game/net/gamespy.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace GameSpy {

void FixNick(char* newNick, const char* oldNick)
{
    static const char kValid[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789[]\\`_^{|}-";
    if (std::isdigit(static_cast<unsigned char>(*oldNick)) || *oldNick == '-')
        *newNick++ = '_';
    for (char c; (c = *oldNick++) != '\0';)
        *newNick++ = std::strchr(kValid, c) ? c : '_';
    *newNick = '\0';
}

} // namespace GameSpy


#pragma warning(push)
#pragma warning(disable : 4005) // (gsPlatform.h defines WIN32_LEAN_AND_MEAN again)
#include "GP/gp.h"
#include "Peer/peer.h"
#include "common/gsAvailable.h"
#include "gcdkey/gcdkeyc.h"
#include "gcdkey/gcdkeys.h"
#include "ghttp/ghttp.h"
#include "gstats/gpersist.h"
#include "gstats/gstats.h"
#include "natneg/natneg.h"
#include "pt/pt.h"
#include "qr2/qr2.h"
#include "serverbrowsing/sb_serverbrowsing.h"
#pragma warning(pop)

// Host-name overrides the SDK keeps without declaring them in a header (pt/ptMain.c), and the one the build adds
// (Peer/peerOperations.c, patched by cmake/gamespy_sdk.cmake: the chat server's name was a constant).
extern "C" {
extern char gPTAVercheckURL[];
extern char gPTAMOTDURL[];
extern char peerChatServerAddress[64];
extern char* Matchup3Hostname; // (natneg.c; natneg.h declares only the first two)
}
// (gsPlatform.h maps snprintf to the old _snprintf, which doesn't always end the string: ours is the standard one)
#undef snprintf

static_assert(sizeof(PEERCallbacks) == sizeof(GameSpy::PeerCallbacks), "PEERCallbacks: 28 callbacks + param");
static_assert(sizeof(GPBuddyStatus) == GameSpy::kGpBuddyStatusSize, "GPBuddyStatus with quietModeFlags");
static_assert(sizeof(SOCKET) == sizeof(uint32_t), "a 32-bit build");

namespace GameSpy {

namespace {

char s_domain[64] = "gamespy.com";

template <typename Fn>
Fn As(Callback callback)
{
    return reinterpret_cast<Fn>(const_cast<void*>(callback.code));
}

void SetHost(char* out, size_t size, const char* format, const char* name)
{
    std::snprintf(out, size, format, name, s_domain);
}

// The server-browsing master for a game name: "<name>.ms<n>.<domain>", n from the SDK's own hash of the name
// (sb_serverlist.c's StringHash over NUM_MASTER_SERVERS = 20) - the SDK's override takes a whole host name.
char s_masterServer[128];
void SetMasterServer(const char* gameName)
{
    uint32_t hash = 0;
    for (const char* c = gameName; *c; ++c)
        hash = static_cast<uint32_t>(static_cast<int>(hash) * -1664117991 + std::tolower(static_cast<unsigned char>(*c)));
    std::snprintf(s_masterServer, sizeof(s_masterServer), "%s.ms%d.%s", gameName, static_cast<int>(hash % 20),
                  s_domain);
    SBOverrideMasterServer = s_masterServer;
}

// Names that depend on the game name (qr2's master, the browsing master): set when a name is given to the SDK.
void SetGameHosts(const char* gameName)
{
    SetHost(qr2_hostname, sizeof(qr2_hostname), "%s.master.%s", gameName);
    SetMasterServer(gameName);
}

char s_natNeg1[96];
char s_natNeg2[96];
char s_natNeg3[96];

// The 2007 gcd SDK asks back for re-authentication; the game's SDK had no such request (nothing to do).
void __cdecl IgnoreRefreshAuth(int, int, int, char*, void*) {}

} // namespace

void Msleep(unsigned milliseconds) { msleep(milliseconds); }
char* Strdup(const char* text) { return goastrdup(text); }
void StartAvailableCheck(const char* gameName)
{
    // (the override is a whole host name: made for each check - the online service also checks "mototrax")
    std::snprintf(GSIACHostname, sizeof(GSIACHostname), "%s.available.%s", gameName, s_domain);
    GSIStartAvailableCheck(gameName);
}
int AvailableCheckThink() { return static_cast<int>(GSIAvailableCheckThink()); }
const char* UniqueId() { return GOAGetUniqueID(); }

void SetOnlineDomain(const char* domain)
{
    std::snprintf(s_domain, sizeof(s_domain), "%s", domain);
    std::snprintf(s_natNeg1, sizeof(s_natNeg1), "natneg1.%s", s_domain);
    std::snprintf(s_natNeg2, sizeof(s_natNeg2), "natneg2.%s", s_domain);
    std::snprintf(s_natNeg3, sizeof(s_natNeg3), "natneg3.%s", s_domain);
    Matchup1Hostname = s_natNeg1;
    Matchup2Hostname = s_natNeg2;
    Matchup3Hostname = s_natNeg3;
    std::snprintf(peerChatServerAddress, sizeof(peerChatServerAddress), "peerchat.%s", s_domain);
    std::snprintf(GPConnectionManagerHostname, sizeof(GPConnectionManagerHostname), "gpcm.%s", s_domain);
    std::snprintf(GPSearchManagerHostname, sizeof(GPSearchManagerHostname), "gpsp.%s", s_domain);
    std::snprintf(StatsServerHostname, sizeof(StatsServerHostname), "gamestats.%s", s_domain);
    constexpr size_t kPtaUrlSize = 2048; // (PTA_MAX_STRING_SIZE)
    std::snprintf(gPTAMOTDURL, kPtaUrlSize, "http://motd.%s/motd/motd.asp", s_domain);
    std::snprintf(gPTAVercheckURL, kPtaUrlSize, "http://motd.%s/motd/vercheck.asp", s_domain);
    // (gcd_hostname stays the SDK's key.gamespy.com, as in the hooked build: the game never changed it)
}

// --- CD key ---

int CdKeyInit(int gameId) { return gcd_init(gameId); }
void CdKeyShutdown() { gcd_shutdown(); }
void CdKeyThink() { gcd_think(); }
void CdKeyAuthenticateUser(int gameId, int localId, uint32_t ip, const char* challenge, const char* response,
                           Callback callback, void* instance)
{
    gcd_authenticate_user(gameId, localId, ip, challenge, response, As<AuthCallBackFn>(callback), &IgnoreRefreshAuth,
                          instance);
}
void CdKeyDisconnectUser(int gameId, int localId) { gcd_disconnect_user(gameId, localId); }
void CdKeyDisconnectAll(int gameId) { gcd_disconnect_all(gameId); }
void CdKeyComputeResponse(const char* cdKey, const char* challenge, char* response)
{
    // (the game's SDK had only the normal response: method 0)
    gcd_compute_response(const_cast<char*>(cdKey), const_cast<char*>(challenge), response, CDResponseMethod_NEWAUTH);
}

// --- qr2 ---

int Qr2Init(void* qrec, const char* ip, int basePort, const char* gameName, const char* secretKey, int isPublic,
            int natNegotiate, Callback serverKey, Callback playerKey, Callback teamKey, Callback keyList,
            Callback count, Callback addError, void* userData)
{
    SetGameHosts(gameName);
    return static_cast<int>(qr2_init(static_cast<qr2_t*>(qrec), ip, basePort, gameName, secretKey, isPublic,
                                     natNegotiate, As<qr2_serverkeycallback_t>(serverKey),
                                     As<qr2_playerteamkeycallback_t>(playerKey),
                                     As<qr2_playerteamkeycallback_t>(teamKey), As<qr2_keylistcallback_t>(keyList),
                                     As<qr2_countcallback_t>(count), As<qr2_adderrorcallback_t>(addError), userData));
}
void Qr2Think(void* qrec) { qr2_think(static_cast<qr2_t>(qrec)); }
void Qr2SendStateChanged(void* qrec) { qr2_send_statechanged(static_cast<qr2_t>(qrec)); }
void Qr2Shutdown(void* qrec) { qr2_shutdown(static_cast<qr2_t>(qrec)); }
void Qr2RegisterKey(int keyId, const char* key) { qr2_register_key(keyId, key); }
void Qr2BufferAdd(void* buffer, const char* text) { qr2_buffer_add(static_cast<qr2_buffer_t>(buffer), text); }
void Qr2BufferAddInt(void* buffer, int value) { qr2_buffer_add_int(static_cast<qr2_buffer_t>(buffer), value); }
void Qr2KeyBufferAdd(void* keyBuffer, int keyId) { qr2_keybuffer_add(static_cast<qr2_keybuffer_t>(keyBuffer), keyId); }

// --- NAT negotiation ---

int NatBeginWithSocket(uint32_t socket, int cookie, int clientIndex, Callback progress, Callback completed,
                       void* userData)
{
    return static_cast<int>(NNBeginNegotiationWithSocket(static_cast<SOCKET>(socket), cookie, clientIndex,
                                                         As<NegotiateProgressFunc>(progress),
                                                         As<NegotiateCompletedFunc>(completed), userData));
}
void NatThink() { NNThink(); }
void NatProcessData(char* data, int length, void* from)
{
    NNProcessData(data, length, static_cast<sockaddr_in*>(from));
}
void NatCancel(int cookie) { NNCancel(cookie); }
void NatFreeNegotiations() { NNFreeNegotiateList(); }

// --- peer ---

void* PeerInitialize(const PeerCallbacks callbacks)
{
    PEERCallbacks sdk;
    std::memcpy(&sdk, callbacks, sizeof(sdk));
    return peerInitialize(&sdk);
}
int PeerSetTitle(void* peer, const char* title, const char* qrSecretKey, const char* sbName, const char* sbSecretKey,
                 int sbGameVersion, int sbMaxUpdates, int natNegotiate, const int* pingRooms,
                 const int* crossPingRooms)
{
    SetGameHosts(title);
    SetMasterServer(sbName);
    return static_cast<int>(peerSetTitle(peer, title, qrSecretKey, sbName, sbSecretKey, sbGameVersion, sbMaxUpdates,
                                         static_cast<PEERBool>(natNegotiate),
                                         reinterpret_cast<PEERBool*>(const_cast<int*>(pingRooms)),
                                         reinterpret_cast<PEERBool*>(const_cast<int*>(crossPingRooms))));
}
void PeerClearTitle(void* peer) { peerClearTitle(peer); }
void PeerConnect(void* peer, const char* nick, int profile, Callback nickError, Callback connected, void* param,
                 int blocking)
{
    peerConnect(peer, nick, profile, As<peerNickErrorCallback>(nickError), As<peerConnectCallback>(connected), param,
                static_cast<PEERBool>(blocking));
}
void PeerRetryWithNick(void* peer, const char* nick) { peerRetryWithNick(peer, nick); }
void PeerFixNick(char* newNick, const char* oldNick) { FixNick(newNick, oldNick); } // (the game's rules, not peerFixNick's)
int PeerIsConnected(void* peer) { return static_cast<int>(peerIsConnected(peer)); }
bool PeerHasChat(void* peer) { return peerGetChat(peer) != nullptr; }
void PeerDisconnect(void* peer) { peerDisconnect(peer); }
void PeerShutdown(void* peer) { peerShutdown(peer); }
void PeerThink(void* peer) { peerThink(peer); }
const char* PeerGetNick(void* peer) { return peerGetNick(peer); }
uint32_t PeerGetPublicIp(void* peer) { return peerGetPublicIP(peer); }
uint32_t PeerGetPrivateIp(void* peer) { return peerGetPrivateIP(peer); }
void PeerSetQuietMode(void* peer, int quiet) { peerSetQuietMode(peer, static_cast<PEERBool>(quiet)); }
void PeerParseQuery(void* peer, char* query, int length, void* sender)
{
    peerParseQuery(peer, query, length, static_cast<sockaddr*>(sender));
}
void PeerSendNatNegotiateCookie(void* peer, uint32_t ip, int port, int cookie)
{
    peerSendNatNegotiateCookie(peer, ip, static_cast<unsigned short>(port), cookie);
}
void PeerListGroupRooms(void* peer, const char* fields, Callback callback, void* param, int blocking)
{
    peerListGroupRooms(peer, fields, As<peerListGroupRoomsCallback>(callback), param, static_cast<PEERBool>(blocking));
}
void PeerJoinGroupRoom(void* peer, int groupId, Callback callback, void* param, int blocking)
{
    peerJoinGroupRoom(peer, groupId, As<peerJoinRoomCallback>(callback), param, static_cast<PEERBool>(blocking));
}
void PeerSetGroupId(void* peer, int groupId) { peerSetGroupID(peer, groupId); }
void PeerLeaveRoom(void* peer, int roomType, const char* reason)
{
    peerLeaveRoom(peer, static_cast<RoomType>(roomType), reason);
}
void PeerMessageRoom(void* peer, int roomType, const char* message, int messageType)
{
    peerMessageRoom(peer, static_cast<RoomType>(roomType), message, static_cast<MessageType>(messageType));
}
void PeerEnumPlayers(void* peer, int roomType, Callback callback, void* param)
{
    peerEnumPlayers(peer, static_cast<RoomType>(roomType), As<peerEnumPlayersCallback>(callback), param);
}
int PeerGetPlayerInfoNoWait(void* peer, const char* nick, uint32_t* ip, int* profile)
{
    return static_cast<int>(peerGetPlayerInfoNoWait(peer, nick, reinterpret_cast<unsigned int*>(ip), profile));
}
void PeerSetRoomKeys(void* peer, int roomType, const char* nick, int count, const char** keys, const char** values)
{
    peerSetRoomKeys(peer, static_cast<RoomType>(roomType), nick, count, keys, values);
}
void PeerGetRoomKeys(void* peer, int roomType, const char* nick, int count, const char** keys, Callback callback,
                     void* param, int blocking)
{
    peerGetRoomKeys(peer, static_cast<RoomType>(roomType), nick, count, keys, As<peerGetRoomKeysCallback>(callback),
                    param, static_cast<PEERBool>(blocking));
}
void PeerStartListingGames(void* peer, const uint8_t* fields, int count, const char* filter, Callback callback,
                           void* param)
{
    peerStartListingGames(peer, fields, count, filter, As<peerListingGamesCallback>(callback), param);
}
void PeerStopListingGames(void* peer) { peerStopListingGames(peer); }
void PeerUpdateGame(void* peer, void* server, int fullUpdate)
{
    peerUpdateGame(peer, static_cast<SBServer>(server), static_cast<PEERBool>(fullUpdate));
}
int PeerStartReportingWithSocket(void* peer, uint32_t socket, int port)
{
    return static_cast<int>(
        peerStartReportingWithSocket(peer, static_cast<SOCKET>(socket), static_cast<unsigned short>(port)));
}
void PeerStopGame(void* peer) { peerStopGame(peer); }
void PeerStateChanged(void* peer) { peerStateChanged(peer); }

// --- a listed server ---

namespace {
SBServer Sb(void* server) { return static_cast<SBServer>(server); }
} // namespace
const char* ServerGetStringValue(void* server, const char* key, const char* fallback)
{
    return SBServerGetStringValue(Sb(server), key, fallback);
}
int ServerGetIntValue(void* server, const char* key, int fallback) { return SBServerGetIntValue(Sb(server), key, fallback); }
int ServerGetBoolValue(void* server, const char* key, int fallback)
{
    return static_cast<int>(SBServerGetBoolValue(Sb(server), key, static_cast<SBBool>(fallback)));
}
const char* ServerGetPlayerStringValue(void* server, int player, const char* key, const char* fallback)
{
    return SBServerGetPlayerStringValue(Sb(server), player, key, fallback);
}
int ServerGetPlayerIntValue(void* server, int player, const char* key, int fallback)
{
    return SBServerGetPlayerIntValue(Sb(server), player, key, fallback);
}
int ServerHasBasicKeys(void* server) { return static_cast<int>(SBServerHasBasicKeys(Sb(server))); }
int ServerHasFullKeys(void* server) { return static_cast<int>(SBServerHasFullKeys(Sb(server))); }
int ServerDirectConnect(void* server) { return static_cast<int>(SBServerDirectConnect(Sb(server))); }
int ServerHasPrivateAddress(void* server) { return static_cast<int>(SBServerHasPrivateAddress(Sb(server))); }
uint32_t ServerGetPublicAddress(void* server) { return SBServerGetPublicInetAddress(Sb(server)); }
uint32_t ServerGetPrivateAddress(void* server) { return SBServerGetPrivateInetAddress(Sb(server)); }
uint32_t ServerGetPublicQueryPort(void* server) { return SBServerGetPublicQueryPort(Sb(server)); }
int ServerGetPing(void* server) { return SBServerGetPing(Sb(server)); }

// --- HTTP ---

int HttpGetEx(const char* url, const char* headers, char* buffer, int size, void* post, int throttle, int blocking,
              Callback progress, Callback completed, void* param)
{
    return ghttpGetEx(url, headers, buffer, size, static_cast<GHTTPPost>(post), static_cast<GHTTPBool>(throttle),
                      static_cast<GHTTPBool>(blocking), As<ghttpProgressCallback>(progress),
                      As<ghttpCompletedCallback>(completed), param);
}
int HttpPostEx(const char* url, const char* headers, void* post, int throttle, int blocking, Callback progress,
               Callback completed, void* param)
{
    return ghttpPostEx(url, headers, static_cast<GHTTPPost>(post), static_cast<GHTTPBool>(throttle),
                       static_cast<GHTTPBool>(blocking), As<ghttpProgressCallback>(progress),
                       As<ghttpCompletedCallback>(completed), param);
}
void* HttpNewPost() { return ghttpNewPost(); }
int HttpPostAddString(void* post, const char* name, const char* value)
{
    return static_cast<int>(ghttpPostAddString(static_cast<GHTTPPost>(post), name, value));
}
int HttpPostAddFileFromMemory(void* post, const char* name, const char* buffer, int size, const char* reportName,
                              const char* contentType)
{
    return static_cast<int>(
        ghttpPostAddFileFromMemory(static_cast<GHTTPPost>(post), name, buffer, size, reportName, contentType));
}
void HttpThink() { ghttpThink(); }
void HttpCancelRequest(int request) { ghttpCancelRequest(request); }
void HttpCleanup() { ghttpCleanup(); }

int TrackUsage(int userId, int productId, const char* versionId, int distributionId, int blocking)
{
    return static_cast<int>(ptTrackUsage(userId, productId, versionId, distributionId, static_cast<PTBool>(blocking)));
}

// --- presence ---

namespace {
GPConnection* Gp(void* connection) { return static_cast<GPConnection*>(connection); }
} // namespace
int GpInitialize(void* connection, int productId, int namespaceId)
{
    // (the 2007 SDK also takes the account system: GameSpy's own, as the game's SDK always used)
    return static_cast<int>(gpInitialize(Gp(connection), productId, namespaceId, GP_PARTNERID_GAMESPY));
}
int GpProcess(void* connection) { return static_cast<int>(gpProcess(Gp(connection))); }
int GpSetCallback(void* connection, int type, Callback callback, void* param)
{
    return static_cast<int>(gpSetCallback(Gp(connection), static_cast<GPEnum>(type), As<GPCallback>(callback), param));
}
int GpConnect(void* connection, const char* nick, const char* email, const char* password, int firewall, int blocking,
              Callback callback, void* param)
{
    return static_cast<int>(gpConnect(Gp(connection), nick, email, password, static_cast<GPEnum>(firewall),
                                      static_cast<GPEnum>(blocking), As<GPCallback>(callback), param));
}
int GpConnectNewUser(void* connection, const char* nick, const char* uniqueNick, const char* email,
                     const char* password, const char* cdKey, int firewall, int blocking, Callback callback,
                     void* param)
{
    return static_cast<int>(gpConnectNewUser(Gp(connection), nick, uniqueNick, email, password, cdKey,
                                             static_cast<GPEnum>(firewall), static_cast<GPEnum>(blocking),
                                             As<GPCallback>(callback), param));
}
void GpDisconnect(void* connection) { gpDisconnect(Gp(connection)); }
int GpSetStatus(void* connection, int status, const char* text, const char* location)
{
    return static_cast<int>(gpSetStatus(Gp(connection), static_cast<GPEnum>(status), text, location));
}
int GpSendBuddyRequest(void* connection, int profile, const char* reason)
{
    return static_cast<int>(gpSendBuddyRequest(Gp(connection), profile, reason));
}
int GpAuthBuddyRequest(void* connection, int profile) { return static_cast<int>(gpAuthBuddyRequest(Gp(connection), profile)); }
int GpDeleteBuddy(void* connection, int profile) { return static_cast<int>(gpDeleteBuddy(Gp(connection), profile)); }
int GpSendBuddyMessage(void* connection, int profile, const char* message)
{
    return static_cast<int>(gpSendBuddyMessage(Gp(connection), profile, message));
}
int GpGetBuddyStatus(void* connection, int index, void* status)
{
    return static_cast<int>(gpGetBuddyStatus(Gp(connection), index, static_cast<GPBuddyStatus*>(status)));
}

// --- stats ---

char* StatsGameName() { return gcd_gamename; }
char* StatsSecretKey() { return gcd_secret_key; }
int StatsInitConnection(int gamePort) { return ::InitStatsConnection(gamePort); }
int StatsIsConnected() { return ::IsStatsConnected(); }
void StatsCloseConnection() { ::CloseStatsConnection(); }
int StatsPersistThink() { return ::PersistThink(); }
void* StatsNewGame(int useBuckets) { return NewGame(useBuckets); }
void StatsFreeGame(void* game) { FreeGame(static_cast<statsgame_t>(game)); }
const char* StatsGetChallenge(void* game) { return GetChallenge(static_cast<statsgame_t>(game)); }
void StatsNewPlayer(void* game, int number, const char* name)
{
    NewPlayer(static_cast<statsgame_t>(game), number, const_cast<char*>(name));
}
void StatsRemovePlayer(void* game, int number) { RemovePlayer(static_cast<statsgame_t>(game), number); }
int StatsSendSnapShot(void* game, const char* snapshot, int final)
{
    return ::SendGameSnapShot(static_cast<statsgame_t>(game), snapshot, final);
}
void StatsGetPersistDataValues(int localId, int profile, int type, int index, const char* keys, Callback callback,
                          void* instance)
{
    ::GetPersistDataValues(localId, profile, static_cast<persisttype_t>(type), index, const_cast<char*>(keys),
                           As<PersDataCallbackFn>(callback), instance);
}
void StatsGenerateAuth(const char* challenge, const char* password, char* response)
{
    ::GenerateAuth(challenge, password, response);
}
void SetServerIntBucket(void* game, const char* key, int value)
{
    BucketIntOp(static_cast<statsgame_t>(game), const_cast<char*>(key), bo_set, value, bl_server, 0);
}
void SetServerStringBucket(void* game, const char* key, const char* value)
{
    BucketStringOp(static_cast<statsgame_t>(game), const_cast<char*>(key), bo_set, const_cast<char*>(value), bl_server,
                   0);
}
void SetPlayerIntBucket(void* game, const char* key, int value, int player)
{
    BucketIntOp(static_cast<statsgame_t>(game), const_cast<char*>(key), bo_set, value, bl_player, player);
}
void SetPlayerStringBucket(void* game, const char* key, const char* value, int player)
{
    BucketStringOp(static_cast<statsgame_t>(game), const_cast<char*>(key), bo_set, const_cast<char*>(value),
                   bl_player, player);
}

} // namespace GameSpy

