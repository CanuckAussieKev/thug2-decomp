# wt/netA: engine net library 0x456910-0x457EB0 (NOT run in the game yet)

All 47 functions in the range have source (45 Ghidra functions + the two deleting destructors it lists as
separate entries; 0 skipped), in three new files and one addition to the shared list head:

- `src/game/net/net_latency.{h,cpp}` - **Net::LatencyTest** (owned: constructor 0x456970 in range).
- `src/game/net/net_lists.{h,cpp}` - the library's **list types**: Lst::Node/Head/Search instances' destructors,
  **BannedConn**, and the Dispatcher's handler-list array destructor.
- `src/game/net/net_app_view.{h,cpp}` - **Net::App** methods (App's constructor 0x4594C0 is wt/netB's, so the
  class is written as `Net::AppView`, a view of the fields used, at their offsets; Conn's constructor 0x455C80 is
  below all three ranges, so `Net::ConnView` likewise). For the merger to fold into the real App / Conn.
- `src/game/lst/head.{h,cpp}` - `Lst::Head::AddNodeFromTail` 0x457350 (priority insert searched from the tail).
- Tests: `src/hook/selftests_netA.cpp` (4 Early tests).

## Classes found

**Net::App** (abstract base of Client/Server; vtable 0x6475B4: [0] 0x459660 deleting dtor, [1] 0x459690,
[2],[3] _purecall, [4] 0x457110 Init, [5] 0x457490 Deinit; Client/Server vtables follow at 0x6475CC / 0x6475E4).
Fields used here:

| off | field |
|---|---|
| +0x0008 | current time (ms) |
| +0x000C | Dispatcher (wt/netC) - starts with 255 Lst::Head<MsgHandler> (0x14 each, vtable 0x6475A8) |
| +0x13FC..+0x1410 | 6 counters zeroed by Init |
| +0x1414 | a list head (vtable 0x6474D8) |
| +0x1428 | SOCKET |
| +0x142C | sockaddr_in local address (getsockname after bind) |
| +0x143C | bool connected (Send refuses otherwise) |
| +0x1440 | Net::Manager* (Bind reads its +0x128 via 0x454960) |
| +0x1444/48/4C/50 | four Mlp logic tasks (0x454CB0 adds them; deleted through vtable slot 2) |
| +0x1454 | zeroed by Init |
| +0x145C | 0xA28-byte buffer for a scrambled outgoing packet |
| +0x1E84 | flags: 0x01 broadcast socket, 0x08 local (no socket of its own), 0x10 ?, 0x20 secure (checksum + XOR) |
| +0x1E88 | set by 0x456B70 |
| +0x1E8C | Lst::Head<Conn> connections (vtable 0x6475AC; nodes 0x647574 at Conn+0x3CF8) |
| +0x1EA0 | Lst::Head<BannedConn> (vtable 0x6475B0) |
| +0x1EB8 | name ("server"/"client", used in "%s: Error %d\n") |

**Net::Conn** (0x3D20 bytes, ctor 0x455C80, dtor 0x456790 - nobody's range): +0x00/+0x14/+0x28 normal /
important / sequenced outgoing queues (heads 0x647578/0x64757C/0x647580), +0x3C handle, +0x40 ip, +0x48 u16
port, +0x60/+0x64 flags, +0x70 last send time, +0x74 send interval, +0x478 u32[256] next expected sequence
number per group, +0x87C Lst::Head[256] sequenced messages received ahead of turn, +0x1CA4 LatencyTest,
+0x3CF4 bool send-now, +0x3CF8 node in App's list, +0x3D0C alias Conn*.
Link layouts (list node, data = itself, deleting dtor in vtable slot 0): normal: msg +0x14, send time +0x18;
important: last send +0x14 (0 = unsent), msg +0x18, a time +0x24; sequenced: last send +0x1C, time +0x24, msg
+0x28; received-ahead: group byte +0x15, sequence +0x20, msg +0x28.

**Queued messages**: SeqQueuedMsg (0x10, ctor 0x455040: id u8, len u16, data, byte +8, refcount +0xC) for the
sequenced queue; the normal/important queues' kind (ctor 0x454FA0) keeps its refcount at +8. Both destroyed by
0x4550F0 (frees data).

**Net::LatencyTest** (0x38): average (starts 1000), 10 times, next index, two times (+0x30, +0x34).

**BannedConn** (0x18): Lst::Node<BannedConn> + ip at +0x14 (vtable 0x6475B0, folded with Head<BannedConn>'s).

**MsgHandlerContext** (0xA4, ctor 0x454A20): msg +0, id +4, length +8, App +0xC, Conn +0x10, flags +0x14.

Other vtables (each only a scalar deleting dtor): 0x64759C Search<Conn> (0x456FB0), 0x6475A0
Search<BannedConn> (0x456FE0), 0x6475A4 Node<BannedConn> (0x4570D0), 0x6475A8 Node<MsgHandler> (0x457090),
0x6475AC Head<Conn> (0x4573A0); 0x647564/68/6C are Search vtables of the message link types (dtors in
0x455Axx, nobody's range).

## Functions (address - name)

0x456910 Encrypt(data, out, length, &outLength) / 0x456CC0 Decrypt(packet, out, length) - 16-bit checksum (CRC
halves added) + XOR with its low byte; 0x456970/0x4569B0 LatencyTest ctor/InputLatencyValue; 0x456A50/60/70/80
task getters; 0x456A90 ReportError; 0x456AF0 NetManager; 0x456B00 IsSendDue; 0x456B30 VirtualSlot5Jump (naked
`jmp [vtable+0x14]`, linker-folded across classes - callers pass different arguments); 0x456B40
AliasConnections; 0x456B60 Socket; 0x456B70 SetValue1E88; 0x456B80/0x456FB0 and 0x456B90/0x456FE0 Search
destructors; 0x456BA0 Bind; 0x456D90 NewQueuedMsg; 0x456E20 SendTo; 0x456F10 Send; 0x456F90 IsLocal;
0x456FA0 HasFlag10; 0x457030/0x457090, 0x457050/0x4573A0, 0x457070/0x4570D0/0x457460 node/head destructors;
0x457110 Init; 0x457260 HandlerLists::Destroy (Dispatcher dtor); 0x457280 SendMessageTo; 0x457300
TerminateConnection; 0x457350 Lst::Head::AddNodeFromTail; 0x4573D0 NextConnection; 0x457400 BanConnection;
0x457490 Deinit; 0x457560 DispatchQueuedSequencedMessages; 0x457710 App destructor body; 0x4578B0
GetConnectionByAddress; 0x457910 GetConnectionByHandle; 0x457970 ImportantMessagesDue; 0x457AC0 MessagesDue;
0x457C80 FreeMessages(conn, queue, id).

## Findings
- Encrypt's arguments are (data, out, length, &outLength) - Ghidra's order was misleading; found by a test crash.
- Bind's first argument is never read, and it calls the manager's +0x128 getter only to set the address to
  INADDR_ANY, which it already is - kept (call and all).
- MessagesDue treats an important/sequenced link as due when the time is *before* its +0x24 time (JC) - kept.
- DispatchQueuedSequencedMessages: handler result 1 = next message, 3 = return false touching nothing, 5 =
  return true leaving the message queued, anything else = delete it and return true. The sequence number is
  bumped before dispatch. One MsgHandlerContext is reused (its other fields carry over between messages).
- FreeMessages only deletes links whose message id matches; refcount 0 deletes the message too.
- 0x45B820 and 0x45B910 (wt/netC's range) are jumps to Deinit 0x457490 and the destructor 0x457710 (folded vtable
  entries): netC/the merger should register them as source-only "a jump to ...".

## Cross-range calls for the merger (GameFunction now)
- wt/netC: Dispatcher Init 0x45A640, Deinit 0x45A690, AddHandler 0x45A5C0 (id, handler, flags, data, priority;
  RET 0x14), DispatchMessage 0x45A750 (context; returns the handler result); handler addresses passed as
  numbers: 0x45B0B0 (ack, id 8), 0x45AF10 (id 7), 0x45ABF0 (disconnect, 0x12), 0x45B2F0 (stream 0xE/0xF),
  0x45AD60 (ping 1), 0x45AB60 (ping response 2).
- Nobody's range (below 0x456910): Conn 0x455970 (send interval), 0x455990 (send now), 0x455C30 (resend
  threshold), 0x456790 (~Conn); SeqQueuedMsg ctor 0x455040; message dtor 0x4550F0; MsgHandlerContext ctor
  0x454A20; Manager getter 0x454960.
- AppView/ConnView -> the real Net::App (wt/netB) / Net::Conn; HandlerLists -> the start of netC's Dispatcher.

## Tests
`selftests_netA.cpp`: TestNetLatency, TestNetLists (getters, flags, slot-5 jump via inline asm, aliases,
lookups and the connection search, AddNodeFromTail, all node/search/handler-list destructors, Encrypt/Decrypt
incl. bad checksums and short packets, IsSendDue with wrap, 25 ImportantMessagesDue/MessagesDue cases incl.
the three resend thresholds), TestNetSockets (Winsock functions stood in at both the game's import slots and
ws2_32 itself - no real socket - plus the game's sprintf and OutputDebugStringA: ReportError, Bind's 7 paths,
SendTo/SendMessageTo/Send secure and not with each error, Init's 8 paths, Deinit), TestNetQueues
(NewQueuedMsg incl. allocation failure, TerminateConnection, BanConnection, 15 sequenced-dispatch scenarios
over every handler result, FreeMessages for each queue and a bad one, the destructor body). Original vs ours,
full call traces and object memory compared.
MUTATIONS_PLACEHOLDER

## To test in the game
Network play: host a LAN game (Network Play / Host), join it from a second PC or instance (or online via the
GameSpy replacement hosts), play a round, chat, leave and rejoin; check thug2-decomp.log for errors.
Disable with `[replacements] disable=net_app_view.cpp,net_lists.cpp,net_latency.cpp` (and `head.cpp` for
AddNodeFromTail, which also disables AddNode).
