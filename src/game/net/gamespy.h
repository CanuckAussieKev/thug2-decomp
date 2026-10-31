// The GameSpy SDK calls the game makes, behind our own functions (library code, not decompiled). The original links a
// 2003-04 build of the SDK; here it is IGN's open-source release of the SDK (December 2007, common 2.06 - BSD-3, see THIRD_PARTY_NOTICES.md), built from
// source; gamespy.cpp adapts the calls where its API grew (docs/gamespy.md lists what differs). SDK objects are opaque
// here (PEER, SBServer, qr2 buffers, GHTTPPost, statsgame_t: void*); a GPConnection is the pointer-sized slot the
// game passes (BuddyMan's first word). Callbacks are passed as code pointers with the SDK's cdecl signatures.
#pragma once

#include <cstddef>
#include <cstdint>

namespace GameSpy {

// The peer SDK's global callbacks (PEERCallbacks: 28 callbacks and their parameter - the same 0x74 bytes, in the same
// order, in both SDKs). Index = byte offset / 4.
using PeerCallbacks = uint32_t[0x1D];

// A callback given to the SDK: any function (its signature the SDK's) or code address (GAME_CODE), or null.
struct Callback {
    const void* code;
    template <typename F>
    Callback(F* function) : code(reinterpret_cast<const void*>(function))
    {
    }
    Callback(std::nullptr_t) : code(nullptr) {}
};

// peerFixNick with the rules of the SDK the game was built with: a '_' before a leading digit or '-', and every
// character outside A-Z a-z 0-9 []\`_^{|}- made '_'. (The 2007 SDK's chatFixNick has other rules - another character
// set, a '_' before @ # + : only, nicks cut at 20 characters - so PeerFixNick is this.)
void FixNick(char* newNick, const char* oldNick);

// (defined in gamespy.cpp, which includes the SDK's headers)

// common
void Msleep(unsigned milliseconds);
char* Strdup(const char* text); // goastrdup: freed with free()
void StartAvailableCheck(const char* gameName);
int AvailableCheckThink(); // GSIACResult: 0 waiting, 1 available, 2 unavailable, 3 temporarily unavailable
const char* UniqueId();    // GOAGetUniqueID
// The online service's domain (OnlineDomain): the SDK's host names are made from it (online_hosts.cpp).
void SetOnlineDomain(const char* domain);

// CD key (gcd)
int CdKeyInit(int gameId);
void CdKeyShutdown();
void CdKeyThink();
// `callback`: void(int gameId, int localId, int authenticated, char* message, void* instance)
void CdKeyAuthenticateUser(int gameId, int localId, uint32_t ip, const char* challenge, const char* response,
                           Callback callback, void* instance);
void CdKeyDisconnectUser(int gameId, int localId);
void CdKeyDisconnectAll(int gameId);
void CdKeyComputeResponse(const char* cdKey, const char* challenge, char* response); // response: 73 bytes

// query and reporting (qr2); a null `qrec` is the SDK's default one
int Qr2Init(void* qrec, const char* ip, int basePort, const char* gameName, const char* secretKey, int isPublic,
            int natNegotiate, Callback serverKey, Callback playerKey, Callback teamKey, Callback keyList,
            Callback count, Callback addError, void* userData);
void Qr2Think(void* qrec);
void Qr2SendStateChanged(void* qrec);
void Qr2Shutdown(void* qrec);
void Qr2RegisterKey(int keyId, const char* key);
void Qr2BufferAdd(void* buffer, const char* text);
void Qr2BufferAddInt(void* buffer, int value);
void Qr2KeyBufferAdd(void* keyBuffer, int keyId);

// NAT negotiation
// `progress`: void(int state, void* userData); `completed`: void(int result, SOCKET, sockaddr_in*, void* userData)
int NatBeginWithSocket(uint32_t socket, int cookie, int clientIndex, Callback progress, Callback completed,
                       void* userData);
void NatThink();
void NatProcessData(char* data, int length, void* from);
void NatCancel(int cookie);
void NatFreeNegotiations();

// peer (chat lobby, game listing, hosting)
void* PeerInitialize(const PeerCallbacks callbacks);
int PeerSetTitle(void* peer, const char* title, const char* qrSecretKey, const char* sbName, const char* sbSecretKey,
                 int sbGameVersion, int sbMaxUpdates, int natNegotiate, const int* pingRooms,
                 const int* crossPingRooms);
void PeerClearTitle(void* peer);
void PeerConnect(void* peer, const char* nick, int profile, Callback nickError, Callback connected, void* param,
                 int blocking);
void PeerRetryWithNick(void* peer, const char* nick);
void PeerFixNick(char* newNick, const char* oldNick);
int PeerIsConnected(void* peer);
bool PeerHasChat(void* peer); // (its chat connection made)
void PeerDisconnect(void* peer);
void PeerShutdown(void* peer);
void PeerThink(void* peer);
const char* PeerGetNick(void* peer);
uint32_t PeerGetPublicIp(void* peer);
uint32_t PeerGetPrivateIp(void* peer);
void PeerSetQuietMode(void* peer, int quiet);
void PeerParseQuery(void* peer, char* query, int length, void* sender);
void PeerSendNatNegotiateCookie(void* peer, uint32_t ip, int port, int cookie);
void PeerListGroupRooms(void* peer, const char* fields, Callback callback, void* param, int blocking);
void PeerJoinGroupRoom(void* peer, int groupId, Callback callback, void* param, int blocking);
void PeerSetGroupId(void* peer, int groupId);
void PeerLeaveRoom(void* peer, int roomType, const char* reason);
void PeerMessageRoom(void* peer, int roomType, const char* message, int messageType);
void PeerEnumPlayers(void* peer, int roomType, Callback callback, void* param);
int PeerGetPlayerInfoNoWait(void* peer, const char* nick, uint32_t* ip, int* profile);
void PeerSetRoomKeys(void* peer, int roomType, const char* nick, int count, const char** keys, const char** values);
void PeerGetRoomKeys(void* peer, int roomType, const char* nick, int count, const char** keys, Callback callback,
                     void* param, int blocking);
void PeerStartListingGames(void* peer, const uint8_t* fields, int count, const char* filter, Callback callback,
                           void* param);
void PeerStopListingGames(void* peer);
void PeerUpdateGame(void* peer, void* server, int fullUpdate);
int PeerStartReportingWithSocket(void* peer, uint32_t socket, int port);
void PeerStopGame(void* peer);
void PeerStateChanged(void* peer);

// a listed server (SBServer)
const char* ServerGetStringValue(void* server, const char* key, const char* fallback);
int ServerGetIntValue(void* server, const char* key, int fallback);
int ServerGetBoolValue(void* server, const char* key, int fallback);
const char* ServerGetPlayerStringValue(void* server, int player, const char* key, const char* fallback);
int ServerGetPlayerIntValue(void* server, int player, const char* key, int fallback);
int ServerHasBasicKeys(void* server);
int ServerHasFullKeys(void* server);
int ServerDirectConnect(void* server);
int ServerHasPrivateAddress(void* server);
uint32_t ServerGetPublicAddress(void* server); // network byte order
uint32_t ServerGetPrivateAddress(void* server);
uint32_t ServerGetPublicQueryPort(void* server); // host byte order
int ServerGetPing(void* server);

// HTTP (ghttp); requests are ints (-1: failed)
// `progress`: void(request, int state, const char*, int64 length, int64 received, int64 total, void* param);
// `completed`: int(request, int result, char* buffer, int64 length, void* param) - nonzero: the SDK frees the buffer
int HttpGetEx(const char* url, const char* headers, char* buffer, int size, void* post, int throttle, int blocking,
              Callback progress, Callback completed, void* param);
int HttpPostEx(const char* url, const char* headers, void* post, int throttle, int blocking, Callback progress,
               Callback completed, void* param);
void* HttpNewPost();
int HttpPostAddString(void* post, const char* name, const char* value);
int HttpPostAddFileFromMemory(void* post, const char* name, const char* buffer, int size, const char* reportName,
                              const char* contentType);
void HttpThink();
void HttpCancelRequest(int request);
void HttpCleanup();

// patching and tracking (pt): the usage report (the MOTD request)
int TrackUsage(int userId, int productId, const char* versionId, int distributionId, int blocking);

// presence (GP)
int GpInitialize(void* connection, int productId, int namespaceId);
int GpProcess(void* connection);
int GpSetCallback(void* connection, int type, Callback callback, void* param);
int GpConnect(void* connection, const char* nick, const char* email, const char* password, int firewall, int blocking,
              Callback callback, void* param);
int GpConnectNewUser(void* connection, const char* nick, const char* uniqueNick, const char* email,
                     const char* password, const char* cdKey, int firewall, int blocking, Callback callback,
                     void* param);
void GpDisconnect(void* connection);
int GpSetStatus(void* connection, int status, const char* text, const char* location);
int GpSendBuddyRequest(void* connection, int profile, const char* reason);
int GpAuthBuddyRequest(void* connection, int profile);
int GpDeleteBuddy(void* connection, int profile);
int GpSendBuddyMessage(void* connection, int profile, const char* message);
// GPBuddyStatus: profile, status, status string [256], location string [256], ip, port (+ the 2007 SDK's
// quietModeFlags: `status` must hold kGpBuddyStatusSize bytes)
constexpr int kGpBuddyStatusSize = 0x214;
int GpGetBuddyStatus(void* connection, int index, void* status);

// stats and persistent storage (gstats)
char* StatsGameName();  // gcd_gamename[256]
char* StatsSecretKey(); // gcd_secret_key[256]
int StatsInitConnection(int gamePort);
int StatsIsConnected();
void StatsCloseConnection();
int StatsPersistThink();
void* StatsNewGame(int useBuckets);
void StatsFreeGame(void* game);
const char* StatsGetChallenge(void* game);
void StatsNewPlayer(void* game, int number, const char* name);
void StatsRemovePlayer(void* game, int number);
int StatsSendSnapShot(void* game, const char* snapshot, int final);
// `callback`: PersDataCallbackFn
void StatsGetPersistDataValues(int localId, int profile, int type, int index, const char* keys, Callback callback,
                          void* instance);
void StatsGenerateAuth(const char* challenge, const char* password, char* response); // response: 33 bytes
// bucket "set" operations: the server's (index 0) or a player's
void SetServerIntBucket(void* game, const char* key, int value);
void SetServerStringBucket(void* game, const char* key, const char* value);
void SetPlayerIntBucket(void* game, const char* key, int value, int player);
void SetPlayerStringBucket(void* game, const char* key, const char* value, int player);


} // namespace GameSpy
