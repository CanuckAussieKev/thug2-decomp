# The GameSpy SDK

The original game links a 2003-04 build of the GameSpy SDK (0x5f8160-0x622e80 in the reference build). It is not
decompiled: the game built from this source links IGN's open-source release of the SDK (BSD-3, December 2007, common
2.06: github.com/nitrocaster/GameSpy at commit d1deb2d, fetched by `cmake/gamespy_sdk.cmake`), through
`src/platform/gamespy.h`: one function per SDK call the game makes, each calling the 2007 API
(`src/platform/gamespy.cpp`), adapted where the API changed.

## What the game calls (exe address -> 2007 API)

| Module | Calls |
|---|---|
| common | 0x5F8160 msleep, 0x5F81C0 goastrdup, 0x5F8690 GSIStartAvailableCheck, 0x5F8840 GSIAvailableCheckThink, 0x0068E24C GOAGetUniqueID (a function pointer the SDK keeps) |
| CD key (gcd) | 0x5F8950 gcd_compute_response, 0x5F9540 gcd_init, 0x5F9620 gcd_authenticate_user, 0x5F9860 gcd_disconnect_user, 0x5F9900 gcd_disconnect_all, 0x5F9BE0 gcd_shutdown, 0x5F9C40 gcd_think |
| qr2 | 0x5FB230 qr2_init, 0x5FBBA0 qr2_think, 0x5FBAD0 qr2_send_statechanged, 0x5FBB20 qr2_shutdown, 0x5FF660 qr2_register_key, 0x5FA010 qr2_buffer_add, 0x5FB350 qr2_buffer_add_int, 0x5F9FE0 qr2_keybuffer_add |
| NAT negotiation | 0x5FC090 NNBeginNegotiationWithSocket, 0x5FC660 NNThink, 0x5FC3B0 NNProcessData, 0x5FC130 NNCancel, 0x5FBCD0 NNFreeNegotiateList |
| peer | 0x5FCE60 peerInitialize, 0x5FCF30 peerSetTitle, 0x5FC7B0 peerClearTitle, 0x5FD580 peerConnect, 0x5FC6F0 peerRetryWithNick, 0x5FD9F0 peerFixNick, 0x5FC730 peerIsConnected, 0x603360 peerGetChat (folded, see below), 0x5FD670 peerDisconnect, 0x5FD130 peerShutdown, 0x5FD1F0 peerThink, 0x5FC920 peerGetNick, 0x5FC940 peerGetPublicIP, 0x5FC950 peerGetPrivateIP, 0x5FC960 peerSetQuietMode, 0x5FC970 peerParseQuery, 0x5FC9A0 peerSendNatNegotiateCookie, 0x5FD3E0 peerListGroupRooms, 0x5FD220 peerJoinGroupRoom, 0x5FC9B0 peerSetGroupID, 0x5FD380 peerLeaveRoom, 0x5FCA80 peerMessageRoom, 0x5FCB10 peerEnumPlayers, 0x5FCBA0 peerGetPlayerInfoNoWait, 0x5FCDF0 peerSetRoomKeys, 0x5FD4A0 peerGetRoomKeys, 0x5FC9D0 peerStartListingGames, 0x5FCA40 peerStopListingGames, 0x5FCA60 peerUpdateGame, 0x5FCC70 peerStartReportingWithSocket, 0x5FCCA0 peerStopGame, 0x5FCCF0 peerStateChanged |
| server browsing (SBServer) | 0x5FED50 SBServerGetStringValue, 0x5FF560 SBServerGetIntValue, 0x5FEDD0 SBServerGetBoolValue, 0x5FEE30 SBServerGetPlayerStringValue, 0x5FF5C0 SBServerGetPlayerIntValue, 0x5FEEC0 SBServerHasBasicKeys, 0x5FEEE0 SBServerHasFullKeys, 0x5FEF30 SBServerDirectConnect, 0x5FEF50 SBServerHasPrivateAddress, 0x603360 SBServerGetPublicInetAddress (folded), 0x5FEF70 SBServerGetPrivateInetAddress, 0x5FEF00 SBServerGetPublicQueryPort, 0x5FF410 SBServerGetPing |
| HTTP (ghttp) | 0x5FF8A0 ghttpGetEx, 0x5FFA10 ghttpPostEx, 0x5FFB40 ghttpNewPost, 0x5FFB50 ghttpPostAddString, 0x5FFB90 ghttpPostAddFileFromMemory, 0x5FFB10 ghttpThink, 0x5FFB20 ghttpCancelRequest, 0x5FF860 ghttpCleanup |
| patching / tracking (pt) | 0x5FFCD0 ptTrackUsage (the "MOTD" request: motd.asp?userid=...) |
| presence (GP) | 0x5FFD40 gpInitialize, 0x5FFD60 gpProcess, 0x5FFD90 gpSetCallback, 0x5FFDE0 gpConnect, 0x5FFEF0 gpConnectNewUser, 0x600020 gpDisconnect, 0x600330 gpSetStatus, 0x600050 gpSendBuddyRequest, 0x6001A0 gpAuthBuddyRequest, 0x6002E0 gpDeleteBuddy, 0x6005D0 gpSendBuddyMessage, 0x6001F0 gpGetBuddyStatus |
| stats / persistent storage (gstats) | 0x602550 InitStatsConnection, 0x600710 IsStatsConnected, 0x601280 CloseStatsConnection, 0x602210 PersistThink, 0x601A70 NewGame, 0x600720 FreeGame, 0x6012E0 GetChallenge, 0x600780 NewPlayer, 0x600850 RemovePlayer, 0x601CE0 SendGameSnapShot, 0x6021E0 GetPersistDataValues, 0x600640 GenerateAuth; the bucket operations through the SDK's tables (bopfuncs at 0x0068E834, bucketfuncs[bo_set] at 0x0068E818: BucketIntOp / BucketStringOp); gcd_gamename (0x007D7A78) and gcd_secret_key (0x007D7B78) written by the game |

0x603360 is one function for several SDK getters that return a structure's first word. The linker folded
identical functions together. Called on a PEER it is peerGetChat (whether the chat connection exists: the lobby's
think task asks before peerThink). Called on an SBServer it is SBServerGetPublicInetAddress.

Callbacks the SDK calls in the game: the 28 PEERCallbacks (RoomManager::InitPeer fills 16 of them by offset), the
listing / group-room / join / enum-players / room-keys / nick-error / connect callbacks, qr2's key / key-list / count /
add-error callbacks, NAT negotiation's completed callback, gcd's auth callback, GP's callbacks (error, buddy request,
buddy status, buddy message: the argument structures GPErrorArg, GPConnectResponseArg, GPRecvBuddyStatusArg,
GPRecvBuddyMessageArg), ghttp's completed callback, gstats' persistent-data callback.

## What differs between the exe's SDK and the 2007 one

Signatures (adapted in gamespy.cpp):
- **gpInitialize** has a fourth argument, the partner ID (account system). The game's SDK always used GameSpy's
  own: GP_PARTNERID_GAMESPY (0).
- **gcd_authenticate_user** has a RefreshAuthCallBackFn before `instance`. The 2007 SDK calls it, without a null
  check, when the key server asks for re-authentication. The game's SDK had no such request, so a callback that does
  nothing is passed.
- **gcd_compute_response** has a fourth argument, CDResponseMethod. The game's SDK had only the normal response:
  CDResponseMethod_NEWAUTH (0). The algorithm is the same ("%s%d%s" of key, random % 0xFFFF and challenge, MD5'd,
  after MD5(key) and the random number as 8 hex digits).
- Everything else the game calls has the same arguments in the same order. The GHTTPByteCount of ghttp's callbacks
  is 64-bit in both (MSVC's _INTEGRAL_MAX_BITS is 64). The news callback takes it as one int64_t (on x86 the same two
  stack words as the original's (low, high) pair; on x64 one register, where a pair would have shifted `param`).
- **PersDataCallbackFn**'s `modified` is a time_t: 32 bits in the game's SDK (VC 7.1), 64 bits in the 2007 SDK as
  built here. The persistent-data callback's record (PersistReply, game/online/stats_man.h) follows the SDK's (until
  X9 it had a 32-bit field, so on x86 `data` read the time's high half and `length` the data pointer);
  gamespy.cpp asserts the signature.

Structures and enums:
- **PEERCallbacks**: 0x74 bytes in both (28 callbacks and the parameter). Every field the game fills is at the same
  offset (checked against the original).
- **GPBuddyStatus**: the 2007 one ends with `quietModeFlags`, 0x214 bytes instead of 0x210. The game's buffer is
  0x230 bytes. profile, status, the two 256-byte strings, ip and port are at the same offsets.
- GPErrorArg, GPConnectResponseArg, GPRecvBuddyStatusArg and GPRecvBuddyMessageArg: the fields the game reads are at
  the same offsets. The GP error codes the game tests (0x107 GP_LOGIN_CONNECTION_FAILED, 0x201
  GP_NEWUSER_BAD_NICK, 0x202 GP_NEWUSER_BAD_PASSWORD, 0x603 GP_ADDBUDDY_ALREADY_BUDDY) and the GP enums
  (callbacks 0-5, status, firewall, blocking) have the same values.
- RoomType, MessageType, PEERJoinResult (as the exe's peerJoinGroupRoom sets it), the PEER_ADD... listing messages,
  qr2_key_type, GSIACResult, GHTTPResult, NegotiateResult / NegotiateError (the exe's NNBeginNegotiationWithSocket
  returns the same codes), persisttype_t, and the bucket tables (level x type, bl_server / bl_player, bt_int /
  bt_string) all have the same values.
- **qr2 buffers** (opaque to the game; the game only adds to them): the exe's reply buffer holds 0x800 bytes, the 2007
  one 1400 (MAX_DATA_SIZE). A reply that doesn't fit is cut short. The game's replies are well under 1400 bytes. The
  key buffers hold 254 keys (1-254) in both.
- **peerListingGamesCallback**: the 2007 SDK also sends PEER_COMPLETE (4) when the first listing is done. The game's
  callback ignores message types it doesn't know.

Behaviour:
- **peerFixNick**: the 2007 SDK's rules changed in 2004. It allows another character set (`"#$%&'()*+,-./:;<=>?@`
  and the others), puts a '_' only before @ # + :, and its chat SDK cuts nicks at 20 characters. The exe's rules: a
  '_' before a leading digit or '-', and A-Z a-z 0-9 []\`_^{|}- only. Ours keeps the game's rules
  (`GameSpy::fix_nick`, checked against the exe's).
- **Nick length**: the 2007 chat SDK refuses a nick of 21 characters or more before it connects (CHAT_NICK_TOO_LONG,
  which reaches the game's nick error callback as PEER_NICK_TOO_LONG = 6). The game's callback handles only "in
  use" (1) and "invalid" (2), so with a long network name the connection would stall. The game's retry nick
  "a<time>_<network id>" gets long quickly. **Open**: to check against OpenSpy, and decide whether the callback should
  treat 6 as "in use".
- **NNBeginNegotiationWithSocket**: the exe's opens a socket of its own next to the game's. The 2007 one does too
  (the NN1-3 INITs, the CONNECT_ACK and the report go through it) and sends the GP INIT and the pings through the
  game's socket (thug2_gamespy_check shows the packets). The game already passes NAT negotiation packets from that socket to
  NNProcessData (RoomManager::QueryTask). The 2007 SDK also speaks a newer NAT negotiation protocol (version 3, the
  report packets) than the exe's.
- **qr2 query challenge**: the 2007 qr2 answers a "pre-query IP verify" and, if the master server sets the option, asks
  clients for it before replying to queries. Old clients (the original game) don't send it. Only relevant if OpenSpy
  sets that option.
- **Host names** (OnlineDomain, src/game/online/online_hosts.cpp): set in the 2007 SDK's own variables: GSIACHostname (made for each availability check, which
  takes the whole host name), qr2_hostname, SBOverrideMasterServer ("<name>.ms<n>.<domain>", n from the SDK's hash
  of the game name), Matchup1/2/3Hostname, GPConnectionManagerHostname, GPSearchManagerHostname,
  StatsServerHostname, gPTAMOTDURL / gPTAVercheckURL. The chat server's name was a constant, so
  `cmake/gamespy_sdk.cmake` makes it a variable (peerChatServerAddress). gcd_hostname stays key.gamespy.com: the game
  never changed it. WinMain's first availability check runs before the PC set-up reads OnlineDomain, so it still asks
  gamespy.com.
- **Game key** (OnlineGameKey, src/game/online/online_identity.cpp): NetLink::GameKey() is passed to qr2_init, peerSetTitle
  and gcd_secret_key.

## Modules built (cmake/gamespy_sdk.cmake)

common (gsPlatform*, gsAvailable, gsCore, gsCrypt, gsLargeInt, gsSSL, gsRC4, gsSHA1, gsSoap, gsXML, gsUdpEngine,
gsMemory, gsDebug, gsAssert, gsStringUtil; darray, hashtable, md5), qr2, natneg, serverbrowsing, Chat, Peer, pinger
(peer's pings), GP and gt2 (GP's peer messages go through gsUdpEngine, which is built on gt2), ghttp, pt, gstats,
gcdkey. Not built: Voice2, sake, sc, webservices, the samples (nonport.c, common/win32/Win32Common.c). It builds as C
with MSVC, warnings off but for the pointer <-> integer ones (errors: above), for x86 and x64. MinGW-w64 isn't tried yet: the win32 platform layer is plain Winsock / Win32.

## 64-bit builds (since X9)

The SDK builds for x64 from the same source. Its x64 build was surveyed at /W4 (172 warnings in the modules built)
and every pointer <-> integer warning read:

- **Fixed: the peer's callback queue** (Peer/peerMain.h). `PEERCBType`, the type each queued operation and callback
  keeps its callback address in, is `int` there (`void*` only on PS3): every peerConnect / peerJoin... /
  peerList... / peerGetRoomKeys / nick-error / listing-games callback address was cut to 32 bits and called
  through the cut value - on x64, where the exe's code is above 4 GB, a crash. It is `intptr_t` now
  (cmake/gamespy_sdk.cmake). 97 of the 104 C4311 / C4312 warnings were this one type (Peer/peerCallbacks.c,
  peerOperations.c, peerSB.c, peerAutoMatch.c).
- **Changed for the warning only: GP's index through a pointer** (GP/gpiBuddy.c, GP/gpiProfile.c, 7 lines):
  `(void *)(unsigned long)index` and back `(int)(unsigned long)data`, a round trip that keeps the index on x64 too.
  They go through `intptr_t` so that C4311 / C4312 can be errors in the SDK's build (`/we4311 /we4312`): a new
  pointer cut fails the x64 build.
- **Read and left: the rest** - 30 C4267 and 25 C4244: `strlen` / pointer differences into `int` lengths of
  buffers of at most a few KB (MD5 input lengths, the chat / GP / HTTP buffers, darray indexes, base64), none near
  2 GB; three C5287 (enum comparisons) and the C4456 / C4459 shadowing. Sockets are `SOCKET` everywhere in the
  modules built (natneg, qr2, gt2, chat, ghttp, gcdkey); the only `(unsigned int)` of a socket is a debug log line.
  `long` is 32 bits on Win64 (LLP64), so the IP addresses and times in `unsigned long` keep their width. gt2Encode's
  `_WIN64` variadic path is used only by gt2's samples.

On x86 the three changes compile to the same code: every object of the SDK built before and after them has the same
disassembly (dumpbin /disasm, 88 of 88 objects), since `intptr_t` is `int` there and `unsigned long` the same width.

The structures and callbacks the game shares with the SDK, checked for x64 (gamespy.cpp asserts what can be):
`PEERCallbacks` (28 callbacks + param, the size of the game's PeerCallbacks on both), `GPBuddyStatus` (0x214, the
fields' offsets), GP's callback arguments (ints, then the message pointer at 8 on both), `SOCKET` (pointer-sized, the
game's `uintptr_t`), the GPConnection slot (FriendsManager's first member, a `void*`), NAT negotiation's user word (a
pointer to the integer since X8), persistent storage's `time_t` and ghttp's 64-bit byte counts (asserted), and every
callback the game registers, read against the SDK's typedef for argument widths: the peer callbacks it sets, the
peer's connect / nick error / group rooms / join / enum players / room keys / listing callbacks, qr2's six, NAT
negotiation's two, gcd's two, GP's four, ghttp's progress / completion ones and gstats' persistent-data one. Two
were wrong: the news callback (ghttp's 64-bit length taken as two 32-bit arguments - on x64 that moved `param`) and
the persistent-data callback (a 32-bit `modified` - wrong on x86 too). Both are fixed. The ghttp requests the game
declares as `void*` are `int` in the SDK and unused by the game's callbacks.

`thug2_gamespy_check` (src/tests/gamespy_check.cpp) drives the SDK and the glue without the network and prints what
each part made: the CD-key response, the stats login hash, the nick and user-name mangling, the chat and browsing
ciphers, base64, the peer's queued callbacks (a connect and a group-room listing that fail at once without a title:
the queue PEERCBType holds), and qr2's heartbeat and the answer to a query against a fake master on 127.0.0.1
(through the glue's buffer functions), and a whole NAT negotiation through the glue against fake matchup servers on
127.0.0.1-3 and a partner, ending in the completion with the game's socket and cookie. The 32-bit and 64-bit builds
print the same, line for line (but the first, which names the width); against the SDK without the PEERCBType change
the 64-bit check crashes in the peer's callbacks. Some of the SDK's sockets are bound to every interface (natneg's,
unbound until it sends), though nothing leaves the loopback: the first run from a new path can bring up the
firewall's prompt - cancel it, the loopback needs no rule.

## Proven, and what still needs the network

Offline (compared with the original's copy during the decompilation): MD5Update / MD5Final against the exe's, peerFixNick (ours against the exe's; where the 2007 one differs is
logged), qr2_buffer_add / qr2_keybuffer_add against the exe's, and the PEERCallbacks offsets the game fills. Not
offline, because the exe's functions need its C runtime, which isn't started there: GenerateAuth and
gcd_compute_response (compared by reading: the same format strings and MD5), SBServer key lookups (they need a listed
server).

Only a local OpenSpy server (packets against the original) or an online test by hand can prove the rest:
availability check, the peer chat login and lobby (title room, group rooms, room keys, nick errors and the nick
length above), game listing (the master server's protocol and its encryption, the hash-picked master host), hosting
(qr2 heartbeats, query replies, the query challenge option), NAT negotiation (protocol version 3, sharing the
game's socket), CD-key authentication of joining players (gcd), GP presence (login, buddies, status, messages), the
stats / persistent storage connection, the MOTD / usage report and the HTTP downloads (news, vault).
