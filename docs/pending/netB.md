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
- **Net::App** (constructor here): `struct App : AppView` - after merging wt/netA, App takes all its fields from
  netA's AppView (net_app_view.h) and adds only the methods of this range (static_assert sizeof(App) ==
  sizeof(AppView)); +0x1F00 (a double, 0.0 from the constructor, past AppView's fields) through an accessor. AppView's
  fields this range identified were renamed in net_app_view.h (and selftests_netA.cpp): m_processTask (+0x1444,
  0x456CA0, priority 0), m_sendTask (+0x1448, 0x4594A0, -2000, made by the Server's and Client's constructors),
  m_receiveTask (+0x144C, 0x4593C0, 1000), m_metricsTask (+0x1450, 0x4593E0, -2500) and the getters 0x456A50-80
  (SendTask/ReceiveTask/ProcessTask/MetricsTask), m_numConnections / m_maxConnections (+0x1454 / +0x1458, int),
  m_dispatcherApp (+0x13F8). The final unification: rename AppView to App (or fold App's methods into it) and drop
  the derivation; ConnView becomes Conn when Conn's constructor (0x455C80) is written.
  Vtables: App 0x6475B4 = [0x459660 deleting dtor, 0x459690 ProcessData, purecall ReceiveData, purecall
  SendEnqueuedMessages(Conn*), 0x457110 Init, 0x457490 Deinit]. Server 0x6475CC = [0x45B8E0, 0x459690, 0x45A300,
  0x459FD0, 0x459EC0, 0x45B820] (ctor 0x45A250), Client 0x6475EC = [0x45B8E0, 0x459690, 0x45BBD0, 0x45B920,
  0x45B7A0, 0x45B820] (ctor 0x45B830). Head vtables in between: 0x6475AC Head<Conn>, 0x6475B0 Head<BannedConn>,
  0x6475E4 / 0x6475E8 (the dispatcher's handler-list head is 0x6475E8, set by 0x457010).
- **Net::MsgDesc** (0x20; ctor 0x4549F0, not in any range): +0 id, +1 stream piece, +2 length, +4 data, +8 priority
  (0x80), +0xC queue (0 default, 1 important, 2 sequenced), +0x10 group, +0x11 singular, +0x14 delay, +0x18 forced
  sequence id, +0x1C compression (0 none, 1 RLE, 2 LZSS - 0x454E30). Defined in net_app.h.
- **Net::StreamDesc** (0x44; ctor 0x4549D0, dtor body 0x454E00) and StreamLink (0x18, vtable 0x647584, ctor
  0x455680): defined in net_app.h / used by offset.
- **Conn** = netA's ConnView (`using Conn = ConnView` in net_app.cpp); fields ConnView doesn't name are accessors by
  offset in net_app.cpp: +0x4C/+0x50 the send buffer (start / end of what's written: ProcessData copies a local
  connection's alias's - +0x3D0C, ConnView::m_aliasConn - into its own receive buffer), +0x54/+0x58 receive
  read/write positions, +0x60 status (1 ready, 4 invalid, 8 disconnecting), +0x64 flags (1 local, 2 remote,
  4 foreign), +0x68 app, +0x6C creation time, +0x78 next outgoing sequence id per group (x256), +0x878 next stream
  id, +0x1C90 Lst::Head<StreamLink>, +0x1CDC / +0x2CE8 in / out Metrics (0x100C each: +0x1008 bytes over the last
  second; update 0x454D00), +0x3D10 bandwidth type (1 = slow), +0x3D1C bandwidth limit.
- Links (by offset, ctors outside the ranges): QueuedMsg 0xC (0x454FA0), QueuedMsgSeq 0x10 (0x455040, via 0x456D90),
  MsgLink 0x1C (0x4555F0), MsgImportantLink 0x28 (0x455620), MsgSeqLink 0x2C (0x455650).

## Cross-range calls
- wt/netA (merged): direct calls now - GetConnectionByHandle, FreeMessages, NewQueuedMsg, Lst::Head::AddNodeFromTail,
  MessagesDue, DispatchQueuedSequencedMessages, DestroyBody. Still GameFunction: 0x457010 (the handler lists'
  constructor) and 0x456CA0 (the process task callback) - in netA's range but not Ghidra functions and not
  rewritten by netA: someone should write them.
- wt/netC: 0x45A830 Dispatcher::DispatchMessages(conn, flags) (2 = done, 3 = stop) - GameFunction with
  `// -> ... (wt/netC)`.
- Not in any of the three ranges (0x454xxx-0x456xxx, "Movies" in subsystems.md but really the net library):
  Conn's methods, MsgDesc/QueuedMsg/link/StreamDesc constructors, Metrics, Net::Manager getters (0x454960,
  0x4545B0). 0x6EFDD8 is the Net::Manager (its +0x128: 1 = slow connection).

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
compared, plus each connection's stream and sequenced-link counts after every stream frame (the pieces'
timing). Mutations: 68 deliberate bugs, all caught (10 more after strengthening: id 9 to all, sequenced
compression, stream frame timing/limits, exact bandwidth share, a non-local alias buffer, a lone sharer).
Equivalent (dropped): `handle != 0xFF` in the single-target id-9 rule (0xFF has the 0x80 bit, so never single
anyway); `queued >= max` vs `>` in ProcessStreamMessages (a zero budget sends nothing either way).
Safety: audit_game_function_calls (no MISMATCH; 0x457010 has no RET in Ghidra - checked by hand, plain RET),
register scan (only the known 0x4EFF92 alarm; FirstConnection / IsConnectionBanned / BandwidthExceeded /
NewConnection return values), check_code_writes (no overlaps), find_gaps (nothing skipped in the range).

## To test in the game
Network menu; host a LAN game and join it from another machine (or a second instance), play a round, chat, let
a client join mid-game (level data and big messages are streamed) and leave/disconnect; online lobby if
available. Disable with `[replacements] disable=net_app.cpp` in thug2-decomp.ini.
