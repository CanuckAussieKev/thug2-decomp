# wt/netC: engine net library 0x4598C0-0x45BDA0 (NOT run in the game yet)

All 36 functions in the range have source (30 Ghidra functions + 6 Ghidra never made functions: the message
handlers 0x45AA30, 0x45AA70, 0x45ABC0, 0x45ABD0, 0x45ABF0, 0x45B2F0, only referenced as pushed constants);
0x45A540 (Lst::Head::AddNode) was ours already. Nothing skipped.

Files:
- `src/game/net/net_dispatcher.{h,cpp}` - **Net::Dispatcher** (App +0x0C), **MsgHandler**, **MsgHandlerContext**,
  the MsgHandler search's destructors.
- `src/game/net/net_client.{h,cpp}` - **Net::Client** (constructor 0x45A250 here), with the deleting destructor
  and the two jump thunks Client and Server share.
- `src/game/net/net_server.{h,cpp}` - **Net::Server** (constructor 0x45B830 here).
- `src/game/net/net_app_messages.{h,cpp}` - **Net::App** methods in this range (BundleMessages and the handlers
  App::Init registers). `class App` here is declared with only these members (no fields); the real App
  (constructor 0x4594C0) is wt/netB's - the merger folds these in.
- `src/game/net/net_access_c.h` - this range's view of everything defined elsewhere: App / Conn field offsets,
  the queue link and queued-message layouts, MsgDesc, message ids, and inline wrappers calling the other ranges'
  functions by address (each marked `-> Name (wt/netA)` / `(wt/netB)`; ones in nobody's range unmarked).
- `src/game/lst/head.{h,cpp}` - `Lst::Head::AddUniqueSequence` 0x45AEB0 (also called by game code 0x567257,
  0x567465).
- Tests: `src/hook/selftests_netC.cpp` (9 Early tests).

## Functions

| address | function |
|---|---|
| 0x4598C0 | App::BundleMessages(Conn*, queue, resendsOnly) - one queue's due messages into the packet |
| 0x459DC0 | Client::StreamMessageToServer(7 args) -> App::StreamMessage 0x458740 on the first connection |
| 0x459E50 | Client::EnqueueMessageToServer(MsgDesc*) |
| 0x459EC0 | Client::Init (vtable slot 4) |
| 0x459F40 | Client::ConnectToServer(ip, port) - connect(), retried on WSAEWOULDBLOCK |
| 0x459FD0 | Client::SendData(Conn*) (slot 3) |
| 0x45A250 | Client::Client(params) |
| 0x45A300 | Client::ReceiveData (slot 2) - recvfrom / recv |
| 0x45A500 / 0x45A510 | Lst::Search<MsgHandler> destructor body / scalar deleting (vtable 0x6475E4) |
| 0x45A590 | MsgHandler scalar deleting destructor (vtable 0x6475E8) |
| 0x45A5C0 | Dispatcher::AddHandler(id, code, flags, data, priority) - network heap |
| 0x45A640 | Dispatcher::Init - deletes every handler (node by node) |
| 0x45A690 | Dispatcher::Deinit - deletes every handler (item by item) |
| 0x45A750 | Dispatcher::DispatchMsg(context) (Neversoft's DispatchMessage; renamed: windows.h macro) |
| 0x45A830 | Dispatcher::DispatchMsgHandlers(Conn*, flags) - a received packet, then its acks |
| 0x45AA30 | Server::HandleTimestamp (id 9) |
| 0x45AA70 | Client::HandleTimestamp (id 9) - lost / late / duplicate packet counts |
| 0x45AB60 | App::HandleLatencyResponse (id 2) |
| 0x45ABC0 | Client::HandleConnectionAccepted (id 4) - only sets "acknowledge" |
| 0x45ABD0 | Client::HandleConnectionRefused (ids 5, 6) - TerminateConnection, halt |
| 0x45ABF0 | App::HandleCompressedMessages (id 0x12) - method 1 (0x454C20) or 2 (Lzss::Decode) |
| 0x45AD60 | App::HandleLatencyTest (id 1) |
| 0x45ADB0 | Server::HandleConnectionRequest (id 3) |
| 0x45AE50 | Server::HandleDisconnRequest (id 0xB) - answers 0xC once |
| 0x45AEB0 | Lst::Head::AddUniqueSequence |
| 0x45AF10 | App::HandleSequencedMessages (id 7) |
| 0x45B0B0 | App::HandleAck (id 8) |
| 0x45B2F0 | App::HandleStreamMessages (ids 0xE start, 0xF data) |
| 0x45B7A0 | Server::Init (slot 4) |
| 0x45B820 | thunk: jmp 0x457490 (App slot 5, Deinit) - Client/Server slot 5; REPLACE_SOURCE_ONLY |
| 0x45B830 | Server::Server(params) |
| 0x45B8E0 | Client/Server scalar deleting destructor (slot 0, both vtables) |
| 0x45B910 | thunk: jmp 0x457710 (App::~App) - Client/Server destructor; REPLACE_SOURCE_ONLY |
| 0x45B920 | Server::SendData(Conn*) (slot 3) |
| 0x45BBD0 | Server::ReceiveData (slot 2) |

## Classes

**Client** vtable 0x6475CC, **Server** 0x6475EC (6 slots each): [0] 0x45B8E0 deleting dtor, [1] 0x459690 (App's,
wt/netB), [2] ReceiveData, [3] SendData, [4] Init, [5] 0x45B820 -> App::Deinit. (App's own vtable 0x6475B4 has
_purecall in [2], [3].) Neither adds fields to App; both constructors build the App's task at App+0x1448
(Tsk::Task, 0x20 bytes, priority -2000, vtable 0x6476F0, code 0x4594A0, data the App).

**Dispatcher** (0x13EC, at App+0x0C): 255 Lst::Head<MsgHandler> (0x14 each), one per message id.

**MsgHandler** (0x20, network heap): Lst::Node (vtable 0x6475E8, data itself, priority) + code +0x14
(`int __cdecl(MsgHandlerContext*)`), data +0x18, packet-flags mask +0x1C (a handler gets a message when
`(mask & packetFlags) == packetFlags`).

**MsgHandlerContext** (0xA4, ctor 0x454A20): msg +0, id +4 (u8), length +8, App +0xC, Conn +0x10, packet flags
+0x14 (1 foreign, 2 late timestamp, 4 stream checksum mismatch), handler data +0x18, u32[32] timestamps to ack
+0x1C, count +0x9C, bool acknowledge +0xA0.

Handler results (DispatchMsg): 0 error, 1 continue, 2 halt, 3 conn destroyed, 4 done (DispatchMsg returns 1),
5 keep (stream buffers kept). DispatchMsgHandlers: 0/2 -> resets the read buffer, returns 2; 3 -> returns 3
at once (nothing touched); else 1.

Message ids (the library's own): 1 latency test, 2 latency response, 3 connection request, 4 accepted,
5 refused, 6 terminated, 7 sequenced (group u8, sequence u32, id u8, data), 8 ack (u16 timestamp), 9 timestamp
(u16), 0xB disconnect request, 0xC disconnect accepted, 0xE stream start (name[0x20], size, stream id,
checksum, msg id, up to 0x100 bytes), 0xF stream data (stream id, piece), 0x12 compressed (method u8, size u16).
Registered by App::Init 0x457110: 8, 7, 0x12, 0xE, 0xF, 1, 2; Client::Init: 4, 5, 6, 9; Server::Init: 3, 9, 0xB.

**App** fields used here (offsets in net_access_c.h): +0x8 time, +0x13FC/+0x1400 bytes out/in, +0x1404 lost,
+0x1408 late, +0x140C duplicate packets, +0x1428 SOCKET, +0x143C connected (send/recv), +0x1440 Net::Manager
(its per-id message flags at +0x28: bit 0 = no length field; read through 0x456AF0 / 0x454980), +0x1448 task,
+0x1458 (Server::Init clears), +0x1970 receive buffer (0x514), +0x1E84 flags (4 latency tests, 8 no socket,
0x10 accepts foreign connections, 0x20 secure: two bytes fewer counted per packet), +0x1E88 foreign-packet
callback `(char*, int, sockaddr*)`, +0x1E8C Lst::Head<Conn>, +0x1F08 the server's sockaddr_in (client).

**Conn** fields used here: queues +0x00/+0x14/+0x28, handle +0x3C, ip +0x40, port +0x48, packet being built
+0x4C/+0x50, received data +0x54/+0x58 (0xA28), status +0x60 (bit 0: acks measure latency; 8: disconnect
accepted), flags +0x64 (1 local, 2 remote, 4 foreign; NewConnection(.., 6) = remote|foreign), App +0x68, last
receive +0x6C, last send +0x70, next sequence per group +0x478, received-ahead heads per group +0x87C, streams
being received +0x1C7C, LatencyTest +0x1CA4 (+0 current value), latency test sent/answered +0x1CD4/+0x1CD8,
receive metrics +0x1CDC, send metrics +0x2CE8, newest timestamp in +0x3D14, timestamp out +0x3D18, bandwidth
+0x3D1C. Link layouts: MsgLink (msg +0x14, send time +0x18), MsgImpLink (last sent +0x14, msg +0x18,
packetstamp +0x1C, resends +0x20, send time +0x24), MsgSeqLink (resends +0x14, group +0x15, packetstamp +0x18,
last sent +0x1C, sequence +0x20, send time +0x24, msg +0x28). Stream: StreamDesc 0x44 (ctor 0x4549D0: size,
id, msg id +8, name +9, data +0x2C, write pointer +0x30, checksum +0x3C), StreamLink 0x18 (desc +0x14).

## Cross-range calls for the merger

wt/netA: 0x456A90 (socket error), 0x456AF0 (GetManager), 0x456B00, 0x456CC0 (read a received packet),
0x456E20 (SendTo), 0x456F10 (Send), 0x456F90, 0x456FA0, 0x4569B0 (LatencyTest::InputLatencyValue), 0x457030
(Lst::Node<MsgHandler> dtor), 0x457110 (App::Init), 0x457280 (send a message to an address), 0x457300
(TerminateConnection), 0x457490 (App slot 5), 0x457710 (App::~App).
wt/netB: 0x4594C0 (App::App), 0x4594A0 (the App task's code), 0x457970, 0x457AC0, 0x457EB0 (EnqueueMessage),
0x4578B0 (connection by address), 0x458740 (StreamMessage), 0x458E60, 0x4597C0 (NewConnection), 0x459690.
Nobody's (0x454xxx-0x456xxx, Conn and messages): 0x454550, 0x454980, 0x4549D0, 0x4549F0, 0x454A20, 0x454C20,
0x454D90, 0x454E00, 0x455040, 0x4550F0, 0x455650, 0x455680, 0x455980, 0x4559B0, 0x4559C0, 0x455A10, 0x455A20,
0x455A30, 0x455C10, 0x455C20, 0x455C30, 0x456540, 0x4566D0, 0x456790; 0x406890 (Tsk::Task ctor).
The names in net_access_c.h for netA/netB functions are guesses from use; prefer the owners' names.

## Findings / quirks kept

- DispatchMsgHandlers: a message without a length field (Manager flag bit 0) copies a whole 0x514-byte buffer
  and the read advances by the previous message's length (the length isn't reset). Handlers may change the
  length (timestamp and ack handlers set 2) and the advance uses the changed value.
- Timestamps to ack are kept in a 32-entry array with no bounds check.
- Client::ReceiveData looks the sender up by the *server's* stored address, not the packet's source; in
  connected mode it never creates connections. Server::ReceiveData passes the connection's read pointer (not the
  packet) to the foreign-packet callback when a known connection's packet doesn't read.
- Packet size checks are 16-bit (`length + 3`, `length + 9` wrap): a 0xFFFE-byte message "fits" and overruns.
- HandleCompressedMessages with an unknown method uses the undecompressed buffer as is.
- Stream start with all data present dispatches without the checksum check; a stream piece longer than what's
  left overruns the buffer.
- ConnectToServer loops forever while connect keeps failing with WSAEWOULDBLOCK.
- Failed allocations in AddHandler / HandleSequencedMessages / HandleStreamMessages crash (no null checks).
- Client::ReceiveData's sockaddr is zero-initialised (the original leaves it uninitialised; only seen by the
  foreign-packet callback in connected mode).
- Client and Server share the deleting destructor and both jump thunks (identical code folded).

## Tests

`src/hook/selftests_netC.cpp`, Early: original vs ours on identical stand-in worlds (App, three Conns, an arena
for the stand-in memory manager); everything called outside the range stood in by recording code (the rest of the
net library, Conn's functions, the memory manager, Tmr::GetTime, WS2_32's recvfrom/recv/connect/WSAGetLastError/
htons/ntohs through their import slots), small Conn/App accessors run for real; traces, return values and all
memory compared per case. Cases: dispatcher (handler order and masks, every result code, Init/Deinit,
destructors, AddUniqueSequence), DispatchMsgHandlers (14), BundleMessages (26: each queue, send times,
resends/thresholds, full packets at the exact boundary, 16-bit wraps, bandwidth), every handler (latency,
timestamps incl. wrap-around windows, sequenced incl. duplicates and out-of-memory, acks, compressed methods,
streams incl. checksums and kept results), Client/Server construct/destroy/init/connect/enqueue, ReceiveData
(10 each), SendData (18 each).
MUTATIONS

## What to test in the game

The network menu; hosting a LAN game (Server) and joining it from a second machine or instance (Client):
connecting, chat (goes through EnqueueMessageToServer), playing a game mode, leaving (disconnect request).
Online play too if available. If something breaks: `[replacements] disable=net_client.cpp,net_server.cpp,
net_dispatcher.cpp,net_app_messages.cpp` (or one at a time; head.cpp has AddUniqueSequence too).
