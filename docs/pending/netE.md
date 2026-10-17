# wt/netE: engine net library 0x454FA0-0x455970 (NOT run in the game yet)

28 functions, nothing in the range skipped (find_gaps: none).

## Files
- `src/game/net/net_messages.h/.cpp` - queued messages, links, Lst::Node<T> destructors:
  - QueuedMsg::Construct 0x454FA0, SeqQueuedMsg::Construct 0x455040, QueuedMsg::DestroyBody 0x4550F0 (both kinds;
    SeqQueuedMsg::DestroyBody is an inline forwarder). Types stay in net_app_view.h (methods added).
  - Link constructors MsgNormalLink 0x4555F0, MsgImpLink 0x455620, MsgSeqLink 0x455650, StreamLink 0x455680 and their
    scalar deleting destructors 0x4557C0 / 0x4557F0 / 0x455820 / 0x455850 (each calls its destructor body
    0x455B50 / 0x455B70 / 0x455B90 / 0x455BB0 - wt/netF's range, through GameFunction). **The link structs moved
    from net_access_c.h into net_messages.h** (net_app.cpp needed them, and including net_access_c.h there made
    its anonymous-namespace helpers ambiguous); net_access_c.h includes net_messages.h. MsgSeqLink's pad16[0] is
    now `m_StreamMessage` (+0x16).
  - Lst::Node<T> scalar deleting destructors: StreamLinkNode 0x455130 (vtable 0x6474C8), MsgNormalNode 0x455170
    (0x6474CC), MsgImpNode 0x4551B0 (0x6474D0), MsgSeqNode 0x4551F0 (0x6474D4), AppNode 0x455230 (Lst::Node<App>
    0x6474D8); AppNode::DestroyBody 0x455110, Lst::Head<App>'s deleting destructor 0x4556B0 (vtable 0x647544).
- `src/game/net/net_manager_view.h/.cpp` - **Net::Manager** (0x37C, singleton 0x6EFDD8 / references 0x6EFDDC) as
  `ManagerView` (netD has the Manager's getters - fold together): Construct 0x4556E0, DestroyBody 0x4555A0,
  CreateNewAppServer 0x455270, CreateNewAppClient 0x4553F0, DestroyApp 0x455550, CreateManager 0x455880,
  ReleaseManager 0x4558D0. Layout: +0x00 servers / +0x14 clients (Lst::Head<App>, apps linked by App +0x1414),
  +0x28 per-message-id flags (ids 8 ack and 9 timestamp: no length field; the constructor clears ids 0-254 only),
  +0x128 connection type, +0x134 local IP string ("<Unknown>"), +0x148 three DNS strings, +0x178 "192.168.55.99",
  +0x188 "255.255.0.0", +0x2DC app count, +0x2E5 use the local IP, +0x2E8 = 5, +0x378 total bandwidth 400000.
  Server is 0x2308 bytes, Client 0x1F18 (the Server/Client classes are views, so the sizes are constants).
- `src/game/net/net_conn_times.cpp` - ConnView::UpdateCommTime 0x455920, GetTimeElapsedSinceCommunication
  0x455940, SetSendInterval 0x455960 (inline in net_app_view.h). +0x6C = last communication time (LastCommTime()
  accessor added to ConnView).
- Callers switched to direct calls: net_app.cpp / net_app_view.cpp / net_access_c.h helpers, main.cpp
  (Net::CreateManager / ReleaseManager), singleton.cpp (`NetManager` replaces `Singleton455880`); tests
  selftests_netA (0x455040 / 0x4550F0 stand-ins), selftests_gapsB (0x4558D0), selftests_misc (TestMain) stand in for
  ours too.

## Findings
- CreateNewAppServer/Client: the configured IP address (+0x134 via inet_addr) replaces the address argument when
  +0x2E5 is set and the connection type is neither 1 nor 3. A local app (flag 8) isn't bound; a local server, or
  one created with flags bit 0, counts as connected (+0x143C). Allocation isn't checked (crash). The log line
  prints inet_ntoa(address) and the port.
- DestroyApp checks the app for null only after using it (kept).
- The queued messages' compressor (0x454E30, netD) can change the id (to 0x12) and length; the constructors take
  them back from it. The original passes the compression argument's whole dword (only the low byte counts).
- CreateNewAppServer/Client have an SEH frame (exceptions from the constructors) - no behaviour to keep.

## Cross-range calls (for the merger)
- -> netD: 0x454E30 CompressMessage (cdecl: uint8_t* id, uint16_t* length, void* data, method), 0x454CB0 (Manager
  method that ignores `this`: the app's four tasks added to the main loop).
- -> netF: 0x455B50 / 0x455B70 / 0x455B90 / 0x455BB0 (link destructor bodies = Lst::Node<T> destructor bodies with
  the vtables above - netF may define these node types too; StreamLinkNode / MsgNormalNode / MsgImpNode /
  MsgSeqNode here are the same types).
- Conn's +0x6C time: net_app.cpp calls it CreatedTime, net_access_c.h kLastCommTime.

## Tests (src/hook/selftests_netE.cpp, 3 Early)
TestNetMessages, TestNetManager, TestNetConnTimes: original vs ours on identical stand-in objects; memory manager,
compressor, link bodies, Server/Client constructors, the apps' vtable (Init / Deinit / delete), Bind, 0x454CB0,
Dbg::Printf, inet_addr / inet_ntoa (import slots and ws2_32 itself), Tmr::GetTime stood in; call traces, object
memory and lists compared. 63 mutations: 60 caught at once, 2 after strengthening (ReleaseManager's destructor
body call - app nodes now in the instance's lists; UpdateCommTime's comparison - a last time of 0xFFFFFFFF),
1 equivalent (CreateManager's `Instance() = nullptr` on a failed allocation: it is null already there).
Register scan: CreateNewAppServer / CreateNewAppClient / StreamLink::Construct / GetTimeElapsedSinceCommunication
have callers reading EAX (all return values). Call audit: no mismatch; check_code_writes: no overlaps; find_gaps:
nothing skipped in the range, 0 thunks.

## Disable
`[replacements] disable=net_messages.cpp,net_manager_view.cpp,net_conn_times.cpp`
