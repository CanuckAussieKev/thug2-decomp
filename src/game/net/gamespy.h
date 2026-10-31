// The GameSpy SDK calls the game makes, behind our own functions (library code, not decompiled -
// docs/library_ranges.md).
//
// Hooked build: the exe's own copy of the SDK (a 2003-04 build linked into it), called by address. Standalone build:
// IGN's open-source release of the SDK (December 2007, common 2.06 - BSD-3, see THIRD_PARTY_NOTICES.md), built from
// source; gamespy.cpp adapts the calls where its API grew (docs/gamespy.md lists what differs). SDK objects are opaque
// here (PEER, SBServer, qr2 buffers, GHTTPPost, statsgame_t: void*); a GPConnection is the pointer-sized slot the
// game passes (BuddyMan's first word). Callbacks are passed as code pointers with the SDK's cdecl signatures.
#pragma once

#include <cstddef>
#include <cstdint>

#include "hook/game.h"

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
// set, a '_' before @ # + : only, nicks cut at 20 characters - so the standalone build's PeerFixNick is this.)
void FixNick(char* newNick, const char* oldNick);

#ifdef THUG2_STANDALONE
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

#else // the exe's copy, by address

namespace Detail {
template <typename T>
T Arg(T value)
{
    return value;
}
inline const void* Arg(Callback callback) { return callback.code; }
template <typename R = void, typename... A>
R Call(uint32_t address, A... args)
{
    return GameFunction<R(__cdecl*)(decltype(Arg(args))...)>(address)(Arg(args)...);
}
} // namespace Detail

inline void Msleep(unsigned milliseconds) { Detail::Call(0x005F8160, milliseconds); }
inline char* Strdup(const char* text) { return Detail::Call<char*>(0x005F81C0, text); }
inline void StartAvailableCheck(const char* gameName) { Detail::Call(0x005F8690, gameName); }
inline int AvailableCheckThink() { return Detail::Call<int>(0x005F8840); }
// (a function pointer the SDK keeps at 0x0068E24C)
inline const char* UniqueId() { return GameVariable<const char*(__cdecl*)()>(0x0068E24C)(); }

inline int CdKeyInit(int gameId) { return Detail::Call<int>(0x005F9540, gameId); }
inline void CdKeyShutdown() { Detail::Call(0x005F9BE0); }
inline void CdKeyThink() { Detail::Call(0x005F9C40); }
inline void CdKeyAuthenticateUser(int gameId, int localId, uint32_t ip, const char* challenge, const char* response,
                                  Callback callback, void* instance)
{
    Detail::Call(0x005F9620, gameId, localId, ip, challenge, response, callback, instance);
}
inline void CdKeyDisconnectUser(int gameId, int localId) { Detail::Call(0x005F9860, gameId, localId); }
inline void CdKeyDisconnectAll(int gameId) { Detail::Call(0x005F9900, gameId); }
inline void CdKeyComputeResponse(const char* cdKey, const char* challenge, char* response)
{
    Detail::Call(0x005F8950, cdKey, challenge, response);
}

inline int Qr2Init(void* qrec, const char* ip, int basePort, const char* gameName, const char* secretKey, int isPublic,
                   int natNegotiate, Callback serverKey, Callback playerKey, Callback teamKey,
                   Callback keyList, Callback count, Callback addError, void* userData)
{
    return Detail::Call<int>(0x005FB230, qrec, ip, basePort, gameName, secretKey, isPublic, natNegotiate, serverKey,
                             playerKey, teamKey, keyList, count, addError, userData);
}
inline void Qr2Think(void* qrec) { Detail::Call(0x005FBBA0, qrec); }
inline void Qr2SendStateChanged(void* qrec) { Detail::Call(0x005FBAD0, qrec); }
inline void Qr2Shutdown(void* qrec) { Detail::Call(0x005FBB20, qrec); }
inline void Qr2RegisterKey(int keyId, const char* key) { Detail::Call(0x005FF660, keyId, key); }
inline void Qr2BufferAdd(void* buffer, const char* text) { Detail::Call(0x005FA010, buffer, text); }
inline void Qr2BufferAddInt(void* buffer, int value) { Detail::Call(0x005FB350, buffer, value); }
inline void Qr2KeyBufferAdd(void* keyBuffer, int keyId) { Detail::Call(0x005F9FE0, keyBuffer, keyId); }

inline int NatBeginWithSocket(uint32_t socket, int cookie, int clientIndex, Callback progress,
                              Callback completed, void* userData)
{
    return Detail::Call<int>(0x005FC090, socket, cookie, clientIndex, progress, completed, userData);
}
inline void NatThink() { Detail::Call(0x005FC660); }
inline void NatProcessData(char* data, int length, void* from) { Detail::Call(0x005FC3B0, data, length, from); }
inline void NatCancel(int cookie) { Detail::Call(0x005FC130, cookie); }
inline void NatFreeNegotiations() { Detail::Call(0x005FBCD0); }

inline void* PeerInitialize(const PeerCallbacks callbacks) { return Detail::Call<void*>(0x005FCE60, callbacks); }
inline int PeerSetTitle(void* peer, const char* title, const char* qrSecretKey, const char* sbName,
                        const char* sbSecretKey, int sbGameVersion, int sbMaxUpdates, int natNegotiate,
                        const int* pingRooms, const int* crossPingRooms)
{
    return Detail::Call<int>(0x005FCF30, peer, title, qrSecretKey, sbName, sbSecretKey, sbGameVersion, sbMaxUpdates,
                             natNegotiate, pingRooms, crossPingRooms);
}
inline void PeerClearTitle(void* peer) { Detail::Call(0x005FC7B0, peer); }
inline void PeerConnect(void* peer, const char* nick, int profile, Callback nickError, Callback connected,
                        void* param, int blocking)
{
    Detail::Call(0x005FD580, peer, nick, profile, nickError, connected, param, blocking);
}
inline void PeerRetryWithNick(void* peer, const char* nick) { Detail::Call(0x005FC6F0, peer, nick); }
inline void PeerFixNick(char* newNick, const char* oldNick) { Detail::Call(0x005FD9F0, newNick, oldNick); }
inline int PeerIsConnected(void* peer) { return Detail::Call<int>(0x005FC730, peer); }
// (the exe's peerGetChat - one function with every other "first word" getter: the linker folded them)
inline bool PeerHasChat(void* peer) { return Detail::Call<int>(0x00603360, peer) != 0; }
inline void PeerDisconnect(void* peer) { Detail::Call(0x005FD670, peer); }
inline void PeerShutdown(void* peer) { Detail::Call(0x005FD130, peer); }
inline void PeerThink(void* peer) { Detail::Call(0x005FD1F0, peer); }
inline const char* PeerGetNick(void* peer) { return Detail::Call<const char*>(0x005FC920, peer); }
inline uint32_t PeerGetPublicIp(void* peer) { return Detail::Call<uint32_t>(0x005FC940, peer); }
inline uint32_t PeerGetPrivateIp(void* peer) { return Detail::Call<uint32_t>(0x005FC950, peer); }
inline void PeerSetQuietMode(void* peer, int quiet) { Detail::Call(0x005FC960, peer, quiet); }
inline void PeerParseQuery(void* peer, char* query, int length, void* sender)
{
    Detail::Call(0x005FC970, peer, query, length, sender);
}
inline void PeerSendNatNegotiateCookie(void* peer, uint32_t ip, int port, int cookie)
{
    Detail::Call(0x005FC9A0, peer, ip, port, cookie);
}
inline void PeerListGroupRooms(void* peer, const char* fields, Callback callback, void* param, int blocking)
{
    Detail::Call(0x005FD3E0, peer, fields, callback, param, blocking);
}
inline void PeerJoinGroupRoom(void* peer, int groupId, Callback callback, void* param, int blocking)
{
    Detail::Call(0x005FD220, peer, groupId, callback, param, blocking);
}
inline void PeerSetGroupId(void* peer, int groupId) { Detail::Call(0x005FC9B0, peer, groupId); }
inline void PeerLeaveRoom(void* peer, int roomType, const char* reason) { Detail::Call(0x005FD380, peer, roomType, reason); }
inline void PeerMessageRoom(void* peer, int roomType, const char* message, int messageType)
{
    Detail::Call(0x005FCA80, peer, roomType, message, messageType);
}
inline void PeerEnumPlayers(void* peer, int roomType, Callback callback, void* param)
{
    Detail::Call(0x005FCB10, peer, roomType, callback, param);
}
inline int PeerGetPlayerInfoNoWait(void* peer, const char* nick, uint32_t* ip, int* profile)
{
    return Detail::Call<int>(0x005FCBA0, peer, nick, ip, profile);
}
inline void PeerSetRoomKeys(void* peer, int roomType, const char* nick, int count, const char** keys,
                            const char** values)
{
    Detail::Call(0x005FCDF0, peer, roomType, nick, count, keys, values);
}
inline void PeerGetRoomKeys(void* peer, int roomType, const char* nick, int count, const char** keys,
                            Callback callback, void* param, int blocking)
{
    Detail::Call(0x005FD4A0, peer, roomType, nick, count, keys, callback, param, blocking);
}
inline void PeerStartListingGames(void* peer, const uint8_t* fields, int count, const char* filter,
                                  Callback callback, void* param)
{
    Detail::Call(0x005FC9D0, peer, fields, count, filter, callback, param);
}
inline void PeerStopListingGames(void* peer) { Detail::Call(0x005FCA40, peer); }
inline void PeerUpdateGame(void* peer, void* server, int fullUpdate) { Detail::Call(0x005FCA60, peer, server, fullUpdate); }
inline int PeerStartReportingWithSocket(void* peer, uint32_t socket, int port)
{
    return Detail::Call<int>(0x005FCC70, peer, socket, port);
}
inline void PeerStopGame(void* peer) { Detail::Call(0x005FCCA0, peer); }
inline void PeerStateChanged(void* peer) { Detail::Call(0x005FCCF0, peer); }

inline const char* ServerGetStringValue(void* server, const char* key, const char* fallback)
{
    return Detail::Call<const char*>(0x005FED50, server, key, fallback);
}
inline int ServerGetIntValue(void* server, const char* key, int fallback)
{
    return Detail::Call<int>(0x005FF560, server, key, fallback);
}
inline int ServerGetBoolValue(void* server, const char* key, int fallback)
{
    return Detail::Call<int>(0x005FEDD0, server, key, fallback);
}
inline const char* ServerGetPlayerStringValue(void* server, int player, const char* key, const char* fallback)
{
    return Detail::Call<const char*>(0x005FEE30, server, player, key, fallback);
}
inline int ServerGetPlayerIntValue(void* server, int player, const char* key, int fallback)
{
    return Detail::Call<int>(0x005FF5C0, server, player, key, fallback);
}
inline int ServerHasBasicKeys(void* server) { return Detail::Call<int>(0x005FEEC0, server); }
inline int ServerHasFullKeys(void* server) { return Detail::Call<int>(0x005FEEE0, server); }
inline int ServerDirectConnect(void* server) { return Detail::Call<int>(0x005FEF30, server); }
inline int ServerHasPrivateAddress(void* server) { return Detail::Call<int>(0x005FEF50, server); }
// (the exe's SBServerGetPublicInetAddress: the folded "first word" getter, as PeerHasChat)
inline uint32_t ServerGetPublicAddress(void* server) { return Detail::Call<uint32_t>(0x00603360, server); }
inline uint32_t ServerGetPrivateAddress(void* server) { return Detail::Call<uint32_t>(0x005FEF70, server); }
inline uint32_t ServerGetPublicQueryPort(void* server) { return Detail::Call<uint32_t>(0x005FEF00, server); }
inline int ServerGetPing(void* server) { return Detail::Call<int>(0x005FF410, server); }

inline int HttpGetEx(const char* url, const char* headers, char* buffer, int size, void* post, int throttle,
                     int blocking, Callback progress, Callback completed, void* param)
{
    return Detail::Call<int>(0x005FF8A0, url, headers, buffer, size, post, throttle, blocking, progress, completed,
                             param);
}
inline int HttpPostEx(const char* url, const char* headers, void* post, int throttle, int blocking,
                      Callback progress, Callback completed, void* param)
{
    return Detail::Call<int>(0x005FFA10, url, headers, post, throttle, blocking, progress, completed, param);
}
inline void* HttpNewPost() { return Detail::Call<void*>(0x005FFB40); }
inline int HttpPostAddString(void* post, const char* name, const char* value)
{
    return Detail::Call<int>(0x005FFB50, post, name, value);
}
inline int HttpPostAddFileFromMemory(void* post, const char* name, const char* buffer, int size,
                                     const char* reportName, const char* contentType)
{
    return Detail::Call<int>(0x005FFB90, post, name, buffer, size, reportName, contentType);
}
inline void HttpThink() { Detail::Call(0x005FFB10); }
inline void HttpCancelRequest(int request) { Detail::Call(0x005FFB20, request); }
inline void HttpCleanup() { Detail::Call(0x005FF860); }

inline int TrackUsage(int userId, int productId, const char* versionId, int distributionId, int blocking)
{
    return Detail::Call<int>(0x005FFCD0, userId, productId, versionId, distributionId, blocking);
}

inline int GpInitialize(void* connection, int productId, int namespaceId)
{
    return Detail::Call<int>(0x005FFD40, connection, productId, namespaceId);
}
inline int GpProcess(void* connection) { return Detail::Call<int>(0x005FFD60, connection); }
inline int GpSetCallback(void* connection, int type, Callback callback, void* param)
{
    return Detail::Call<int>(0x005FFD90, connection, type, callback, param);
}
inline int GpConnect(void* connection, const char* nick, const char* email, const char* password, int firewall,
                     int blocking, Callback callback, void* param)
{
    return Detail::Call<int>(0x005FFDE0, connection, nick, email, password, firewall, blocking, callback, param);
}
inline int GpConnectNewUser(void* connection, const char* nick, const char* uniqueNick, const char* email,
                            const char* password, const char* cdKey, int firewall, int blocking, Callback callback,
                            void* param)
{
    return Detail::Call<int>(0x005FFEF0, connection, nick, uniqueNick, email, password, cdKey, firewall, blocking,
                             callback, param);
}
inline void GpDisconnect(void* connection) { Detail::Call(0x00600020, connection); }
inline int GpSetStatus(void* connection, int status, const char* text, const char* location)
{
    return Detail::Call<int>(0x00600330, connection, status, text, location);
}
inline int GpSendBuddyRequest(void* connection, int profile, const char* reason)
{
    return Detail::Call<int>(0x00600050, connection, profile, reason);
}
inline int GpAuthBuddyRequest(void* connection, int profile) { return Detail::Call<int>(0x006001A0, connection, profile); }
inline int GpDeleteBuddy(void* connection, int profile) { return Detail::Call<int>(0x006002E0, connection, profile); }
inline int GpSendBuddyMessage(void* connection, int profile, const char* message)
{
    return Detail::Call<int>(0x006005D0, connection, profile, message);
}
constexpr int kGpBuddyStatusSize = 0x214; // (the exe's SDK fills 0x210)
inline int GpGetBuddyStatus(void* connection, int index, void* status)
{
    return Detail::Call<int>(0x006001F0, connection, index, status);
}

inline char* StatsGameName() { return &GameVariable<char>(0x007D7A78); }
inline char* StatsSecretKey() { return &GameVariable<char>(0x007D7B78); }
inline int StatsInitConnection(int gamePort) { return Detail::Call<int>(0x00602550, gamePort); }
inline int StatsIsConnected() { return Detail::Call<int>(0x00600710); }
inline void StatsCloseConnection() { Detail::Call(0x00601280); }
inline int StatsPersistThink() { return Detail::Call<int>(0x00602210); }
inline void* StatsNewGame(int useBuckets) { return Detail::Call<void*>(0x00601A70, useBuckets); }
inline void StatsFreeGame(void* game) { Detail::Call(0x00600720, game); }
inline const char* StatsGetChallenge(void* game) { return Detail::Call<const char*>(0x006012E0, game); }
inline void StatsNewPlayer(void* game, int number, const char* name) { Detail::Call(0x00600780, game, number, name); }
inline void StatsRemovePlayer(void* game, int number) { Detail::Call(0x00600850, game, number); }
inline int StatsSendSnapShot(void* game, const char* snapshot, int final)
{
    return Detail::Call<int>(0x00601CE0, game, snapshot, final);
}
inline void StatsGetPersistDataValues(int localId, int profile, int type, int index, const char* keys,
                                 Callback callback, void* instance)
{
    Detail::Call(0x006021E0, localId, profile, type, index, keys, callback, instance);
}
inline void StatsGenerateAuth(const char* challenge, const char* password, char* response)
{
    Detail::Call(0x00600640, challenge, password, response);
}
// The bucket operations: function pointers the SDK keeps in a table (bopfuncs[level][type] at 0x0068E834: server /
// team / player, int / float / string), given its "set" operator (bucketfuncs[bo_set], 0x0068E818).
namespace Detail {
using IntBucketOp = void*(__cdecl*)(void*, const char*, void*, int, int);
using StringBucketOp = void*(__cdecl*)(void*, const char*, void*, const char*, int);
inline void* SetOperator() { return GameVariable<void*>(0x0068E818); }
} // namespace Detail
inline void SetServerIntBucket(void* game, const char* key, int value)
{
    GameVariable<Detail::IntBucketOp>(0x0068E834)(game, key, Detail::SetOperator(), value, 0);
}
inline void SetServerStringBucket(void* game, const char* key, const char* value)
{
    GameVariable<Detail::StringBucketOp>(0x0068E83C)(game, key, Detail::SetOperator(), value, 0);
}
inline void SetPlayerIntBucket(void* game, const char* key, int value, int player)
{
    GameVariable<Detail::IntBucketOp>(0x0068E84C)(game, key, Detail::SetOperator(), value, player);
}
inline void SetPlayerStringBucket(void* game, const char* key, const char* value, int player)
{
    GameVariable<Detail::StringBucketOp>(0x0068E854)(game, key, Detail::SetOperator(), value, player);
}
#endif

} // namespace GameSpy
