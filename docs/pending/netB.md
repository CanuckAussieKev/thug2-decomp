# wt/netB: engine net library, 0x457EB0-0x4598C0 (Net::App) - NOT run in the game yet

## Rewritten (src/game/net/net_app.h/.cpp, tests src/hook/selftests_netB.cpp)
All 19 functions of the range (the survey's 17 plus two task callbacks Ghidra never made functions:
0x4593C0 and 0x4594A0):

| Address | Function |
|---|---|
| 0x457EB0 | App::EnqueueMessage(handle, MsgDesc*) - 93 callers |
| 0x458740 | App::StreamMessageToConn(conn, msgId, size, data, desc, group, allAtOnce, sendInPlace) |
| 0x458AB0 | App::StreamMessage(handle, ...) |
| 0x458BE0 | App::FirstConnection(search) |
| 0x458C10 | App::SendEnqueuedMessages() (virtual slot 3 per connection) |
| 0x458CA0 | App::SendData(sendInFrame) |
| 0x458D50 | App::IsConnectionBanned(conn) |
| 0x458DB0 | App::GetBandwidthUsed() |
| 0x458E60 | App::BandwidthExceeded(conn) |
| 0x458F80 | App::GetFreeHandle() |
| 0x458FB0 | App::TerminateInvalidConnections() |
| 0x4590C0 | App::ProcessStreamMessages() |
| 0x4593C0 | App::ReceiveNetworkData(task) (task callback; tail-jumps to virtual slot 2: "not checked" by thug2_check) |
| 0x4593E0 | App::NetworkMetrics(task) |
| 0x4594A0 | App::SendNetworkData(task) |
| 0x4594C0 | App::Construct(flags) (constructor) |
| 0x459660 | App::Destroy(flags) (scalar deleting destructor, App vtable slot 0) |
| 0x459690 | App::ProcessData() (virtual slot 1) |
| 0x4597C0 | App::NewConnection(ip, port, flags) |

## Classes
- **Net::App** (constructor here, so defined here): vtable 0x6475B4 = [0x459660 deleting dtor, 0x459690
  ProcessData, purecall ReceiveData, purecall SendEnqueuedMessages(Conn*), 0x457110, 0x457490]. Server vtable
  0x6475CC = [0x45B8E0, 0x459690, 0x45A300, 0x459FD0, 0x459EC0, 0x45B820] (ctor 0x45A250), Client 0x6475EC =
  [0x45B8E0, 0x459690, 0x45BBD0, 0x45B920, 0x45B7A0, 0x45B820] (ctor 0x45B830); both make the send task (+0x1448,
  priority -2000, 0x4594A0). Layout: +8 timestamp (ms), +0xC Dispatcher (255 Lst::Head<MsgHandler>, ctor 0x457010
  / dtor 0x457030, head vtable 0x6475E8; +0x13F8 its app), +0x1414 Lst::Node<App> (vtable 0x6474D8, data = app),
  +0x1440 a manager (0x4545B0 reads its +0x378 = total bandwidth), +0x1444 process task (0x456CA0, pri 0), +0x1448
  send task, +0x144C receive task (0x4593C0, pri 1000), +0x1450 metrics task (0x4593E0, pri -2500), +0x1454 /
  +0x1458 connections / maximum, +0x1E84 flags, +0x1E88, +0x1E8C Lst::Head<Conn> (vtable 0x6475AC), +0x1EA0
  Lst::Head<BannedConn> (vtable 0x6475B0; items 0x18 bytes, +0x14 the address, added by 0x457400), +0x1F00 a double
  (0.0). Size at least 0x1F08.
- **Net::MsgDesc** (0x20; ctor 0x4549F0, not in any range): +0 id, +1 stream piece, +2 length, +4 data, +8 priority
  (0x80), +0xC queue (0 default, 1 important, 2 sequenced), +0x10 group, +0x11 singular, +0x14 delay, +0x18 forced
  sequence id, +0x1C compression (0 none, 1 RLE, 2 LZSS - 0x454E30). Defined in net_app.h.
- **Net::StreamDesc** (0x44; ctor 0x4549D0, dtor body 0x454E00) and StreamLink (0x18, vtable 0x647584, ctor
  0x455680): defined in net_app.h / used by offset.
- **Conn** (0x3D20, ctor 0x455C80 outside the three ranges): forward-declared, accessors by offset in net_app.cpp:
  +0/+0x14/+0x28 default/important/sequenced queues, +0x3C handle, +0x40 address, +0x48 port, +0x54/+0x58 receive
  read/write positions, +0x60 status (1 ready, 4 invalid, 8 disconnecting), +0x64 flags (1 local, 2 remote,
  4 foreign), +0x68 app, +0x6C creation time, +0x78 next sequence id per group (x256), +0x478 expected sequence ids
  (read by 0x457560), +0x878 next stream id, +0x1C90 Lst::Head<StreamLink>, +0x1CDC / +0x2CE8 in / out
  Metrics (0x100C each: +0x1008 bytes over the last second), +0x3CF4 force-send byte, +0x3CF8 Lst::Node<Conn>,
  +0x3D0C a local peer's buffer (+0x4C/+0x50), +0x3D10 bandwidth type (1 = slow), +0x3D1C bandwidth limit.
- Links (by offset, ctors outside the ranges): QueuedMsg 0xC (0x454FA0), QueuedMsgSeq 0x10 (0x455040, via 0x456D90),
  MsgLink 0x1C (0x4555F0), MsgImportantLink 0x28 (0x455620), MsgSeqLink 0x2C (0x455650).

## Cross-range calls for the merger (GameFunction now, `// -> ... (wt/netA|wt/netC)` in net_app.cpp)
- wt/netA: 0x457910 GetConnectionByHandle, 0x457C80 remove queued messages with an id (thiscall App, RET 0xC),
  0x456D90 new QueuedMsgSeq (thiscall App, RET 0x14), 0x457350 Head<MsgLink> priority insert, 0x457AC0
  MessagesToSend(conn), 0x457560 ProcessSequencedMessages(conn), 0x457710 ~App body, 0x457010 handler-list ctor,
  0x456CA0 the process task callback.
- wt/netC: 0x45A830 Dispatcher::DispatchMessages(conn, flags) (2 = done, 3 = stop).
- Not in any of the three ranges (0x454xxx-0x456xxx, "Movies" in subsystems.md but really the net library):
  Conn's methods, MsgDesc/QueuedMsg/link/StreamDesc constructors, Metrics.

## Findings
- 0x458740 / 0x4590C0 leave the stack MsgDesc's singular (+0x11) and compression (+0x1C) uninitialised, and the
  stream start message's length is the piece + 0x30, i.e. 3 bytes beyond the copied data (struct padding /
  uninitialised stack). A non-zero compression byte other than 1/2 would turn every piece into a garbled
  "compressed" message 0x12 (0x454E30 returns a 3-byte buffer when its compressed size stays 0), so in practice it
  must be 0; ours zeroes these (the tests zero the stack before the originals).
- EnqueueMessage's sequenced broadcast gives each link the queue number (2) as its priority instead of the
  message's priority (exclude and single targets use the priority) - kept.
- Default-queue messages to a single handle also go to a not-ready connection when the id is 9.
- Nothing checks GetConnectionByHandle's result (single targets) or allocation failures - kept.
- 0x4593C0 / 0x4594A0 were not functions in Ghidra (only referenced as task callbacks).

## Tests (Early, offline)
TestNetAppMessages, TestNetAppConnections: original vs ours on a stand-in memory manager (default + network heap)
with a stand-in app and connections in a 64 KB-aligned arena; the game's own code runs for queue/link/stream
helpers, GetConnectionByHandle, metrics; recorders for Dbg::Printf, Conn ctor/dtor, MessagesToSend, the dispatcher,
ProcessSequencedMessages, ~App and the app's virtual functions. The whole arena (relocated) plus the call log is
compared. Mutations: see below.

## To test in the game
Network menu; host a LAN game and join it from another machine (or a second instance), play a round, chat, let
a client join mid-game (level data and big messages are streamed) and leave/disconnect; online lobby if
available. Disable with `[replacements] disable=net_app.cpp` in thug2-decomp.ini.
