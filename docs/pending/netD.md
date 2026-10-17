# wt/netD: engine net library, 0x4544E0-0x454FA0 (Metrics, Manager, descriptors, compression) - NOT run in the game yet

All 44 functions of the range (every one Ghidra has; the gaps between them are only padding and 0x454770's jump table - nothing skipped, none source-only - the shortest, 0x4549C0
`MOV AL,1; RET 0xC`, is exactly 5 bytes).

## Rewritten

| Address | Function | File |
|---|---|---|
| 0x4544E0 | Metrics::Construct() | net_metrics.cpp |
| 0x454520 | Metrics::GetBytesPerSec() | net_metrics.h/.cpp |
| 0x454530 | Metrics::GetTotalMessageData(msgId) | |
| 0x454540 | Metrics::GetTotalNumMessagesOfId(msgId) | |
| 0x454550 | Metrics::AddMessage(msgId, size) | |
| 0x454D00 | Metrics::CalculateUsage(time) | |
| 0x454D90 | Metrics::AddPacket(size, time) | |
| 0x454580 | Manager::ConnectToInternet() (online = true; true) | net_manager.cpp |
| 0x454590 | Manager::DisconnectFromInternet() | |
| 0x4545A0 | Manager::GetModemBaudRate() (+0x374) | |
| 0x4545B0 | Manager::GetBandwidth() (+0x378) | |
| 0x4545C0 | Manager::IsOnline() | |
| 0x4545D0 | Manager::SetDialupNumber(index, text) | |
| 0x454600 / 0x454620 | Manager::SetDialupUserName / SetDialupPassword | |
| 0x454640 / 0x4546A0 / 0x454710 | Manager::SetGateway / SetSubnetMask / SetLocalIP (copy only if different) | |
| 0x454700 | Manager::SetLocalAddress(address) | |
| 0x454770 | Manager::SetConnectionType(type) (+ bandwidth: 1 4200, 2/4 400000, 3 300000) | |
| 0x4547D0 | Manager::SetDeviceType(type) | |
| 0x4547F0 | Manager::SetDHCP(bool) | |
| 0x454810 | Manager::SetFlag2E6(bool) (pref checksum 0xE5ADE642 - name unknown) | |
| 0x454820 | Manager::SetDNSServer(index, ip) | |
| 0x454890 / 0x4548C0 | Manager::SetHostName / SetDomainName | |
| 0x4548F0 | Manager::GetLocalAddress() (+0x144, else inet_addr(local IP)) | |
| 0x454910 | Manager::GetLocalIP() (gethostbyname("localhost"), then its h_name; inet_ntoa) | |
| 0x454960 | Manager::GetConnectionType() | |
| 0x454970 | Manager::GetLastError() (+0x2E0) | |
| 0x454980 / 0x454990 | Manager::GetMessageFlags / SetMessageFlags(msgId[, flags]) | |
| 0x4549A0 / 0x4549B0 | Manager::GetModemState / SetModemState | |
| 0x4549C0 | Manager::LoadNetworkDrivers(ip, subnetMask, gateway) -> true | |
| 0x454CB0 | Manager::AddAppTasks(App*) (receive, send, process, metrics tasks into Mlp logic tasks) | |
| 0x454DE0 | Manager::GetDialupNumber() ("" for connection type 3) | |
| 0x4549D0 | StreamDesc::Construct() | net_descs.cpp |
| 0x454E00 | StreamDesc::DestroyBody() | |
| 0x4549F0 | MsgDesc::Construct() | |
| 0x454A20 | MsgHandlerContext::Construct() | |
| 0x454A60 | RleCompress(in, size, out, outSize) (cdecl; outSize unused) | net_compress.cpp |
| 0x454C20 | RleDecompress(in, size, out, int* outSize) (cdecl; 0 / -1) | |
| 0x454E30 | CompressMessage(uint8_t* msgId, uint16_t* length, data, char method) (cdecl) | |

## Classes / layouts
- **Net::Metrics** (net_metrics.h, 0x100C): Packet m_packets[256] {size, time} +0x000, m_numPackets +0x800,
  m_numMessages[256] +0x804, m_messageData[256] +0xC04, m_totalBytes +0x1004, m_bytesPerSec +0x1008. A Conn has two:
  in +0x1CDC, out +0x2CE8 (Conn's constructor 0x455C80 - wt/netF - calls 0x4544E0 for both).
- **Net::Manager** (net_manager.h, 0x37C; instance 0x6EFDD8, Manager::Instance(); made by 0x455880, constructor
  0x4556E0, destructor body 0x4555A0 - all wt/netE): +0x00 Lst::Head of servers, +0x14 of clients (vtable 0x647544),
  +0x28 message flags[256] (ctor: 8 and 9 = 1, 0..254 = 0 - entry 255 isn't cleared), +0x128 connection type,
  +0x130 device type, +0x134 local IP[16] ("<Unknown>"), +0x144 local address, +0x148 DNS servers[3][16],
  +0x178 gateway[16] ("192.168.55.99"), +0x188 subnet mask[16] ("255.255.0.0"), +0x198 dial-up number index,
  +0x19C dial-up numbers[3][32], +0x1FC dial-up user name[0x50], +0x24C password[0x50], +0x29C host name[32],
  +0x2BC domain name[32], +0x2DC apps made, +0x2E0 last error, +0x2E4 online, +0x2E5 use DHCP, +0x2E6 a flag,
  +0x2E8 modem state (5 at first), +0x2EC (0), +0x374 modem baud rate (?), +0x378 bandwidth (400000).
  Names from the network preferences GameNet copies in (0x535040; checksums matched: ip_address, gateway,
  subnet_mask, dns_server/dns_server2, auto_dns, host_name, domain_name, dialup_number/2/3, dialup_username,
  dialup_password, device_type, broadband_type == ip_dhcp -> SetDHCP, device_broadband_pc).
- Methods added to existing types (declarations only, additive): MsgDesc::Construct, StreamDesc::Construct /
  DestroyBody (net_app.h), MsgHandlerContext::Construct (net_dispatcher.h).

## Calls switched to direct calls
net_app.cpp (BytesPerSecond, UpdateMetrics, TotalBandwidth, ConnectionType, StreamDesc ctor/dtor, InitMsgDesc,
Manager::Instance() for 0x6EFDD8), net_app_view.cpp (Bind's connection type, DispatchQueuedSequencedMessages'
context), net_app_messages.cpp (context, RLE decompress), net_dispatcher.cpp (context), net_access_c.h (MsgDesc ctor,
MessageFlags, Add{Message,Packet}Metric, StreamDesc ctor/dtor), chat.cpp and script/cscript_manager.cpp (their own
MsgDesc copies: reinterpret_cast to Net::MsgDesc and Construct() - netF unifies the copies). Tests that stood in by
address now also stand in for ours: selftests_netC.cpp (0x454550, 0x454D90, 0x454C20), selftests_netB.cpp
(Dbg::Printf, which StreamDesc::DestroyBody calls).

## After merging main (netE, netF)
- netE's `ManagerView` is folded into **Net::Manager** (net_manager.h): its constructor 0x4556E0, destructor body
  0x4555A0, CreateNewAppServer / CreateNewAppClient / DestroyApp and CreateManager / ReleaseManager are Manager's
  (net_manager_view.cpp renamed net_manager_apps.cpp, net_manager_view.h removed; includes and singleton.cpp
  updated). netE's field names mapped onto this layout: m_msgFlags -> m_messageFlags (uint8_t), m_localIp ->
  m_localIP, m_useLocalIp -> m_useDHCP (set from broadband_type == ip_dhcp; CreateNewApp* then bind to the local
  IP), m_unknown2E0/2E4/2E6/2E8 -> m_lastError / m_online / m_flag2E6 / m_modemState, m_unknown198 ->
  m_dialupNumberIndex, m_totalBandwidth -> m_bandwidth; m_servers / m_clients are AppNodes.
- Direct calls now: netE's CompressMessage (net_messages.cpp) and AddAppTasks (net_manager_apps.cpp); netF's
  Metrics constructor (Conn's m_receiveMetrics / m_sendMetrics are now typed `Metrics`; callers pass their
  address) and ~StreamDesc (net_conn.cpp); chat.cpp / cscript_manager.cpp's MsgDesc constructor.
- Tests standing in for ours too: selftests_netE.cpp (0x454E30, 0x454CB0), selftests_netF.cpp (Dbg::Printf).
- After the merge: thug2_check 0 MISMATCHED, 157 early tests pass; check_code_writes no overlaps; call audit no
  MISMATCH; register scan only the known RISK.

## Cross-range calls (as found before the merge)
- netE (0x454FA0-0x455970) calls into this range: QueuedMsg ctors 0x454FA0 / 0x455040 -> CompressMessage 0x454E30;
  CreateNewAppServer 0x455270 / CreateNewAppClient 0x4553F0 -> Manager::AddAppTasks 0x454CB0 (ECX = the manager);
  the Manager's constructor 0x4556E0 / destructor body 0x4555A0 / create-release 0x455880 / 0x4558D0 belong on
  Net::Manager (net_manager.h has the full layout).
- netF (Conn): Conn's constructor 0x455C80 calls Metrics::Construct twice (+0x1CDC, +0x2CE8) - type those fields
  as Net::Metrics.
- AppView::m_manager is still `void*` (the merger can make it `Manager*`).
- This range calls nothing of netE / netF.

## Findings
- MsgDesc's constructor sets **every** field (singular and compression too): batch 85's note that
  StreamMessageToConn / ProcessStreamMessages leave them uninitialised was wrong (comments corrected in net_app.cpp,
  net_app.h, selftests_netB.cpp).
- RleCompress codes a 1-byte input to nothing (it reads the byte and returns 0); every other length round-trips.
  Runs take 2-0x81 bytes (code 0x80 + n - 2), literals 1-0x80 (code n - 1); a literal that ends in two equal bytes
  hands them to the next run. The 4th argument (the output size) is never read.
- RleDecompress: a literal longer than the code left reads on past the end (size goes negative and the loop only
  stops at 0 or a full output).
- CompressMessage: 16-bit size (length + 3 wraps for 0xFFFD up and then overflows its copy - kept); an unknown
  method codes nothing and "succeeds" with a 3-byte message when the length isn't 0; the compressed size is
  compared with the data's length (not the message's).
- Manager::SetHostName / SetDomainName with null clear the field and then crash on the copy anyway (kept).
- GetLocalIP: when the second lookup fails it reads the first hostent after the second call (Winsock's per-thread
  hostent - the same buffer); kept.

## The flaky netB test ("net app: queued and streamed messages")
Not the 3 bytes past the copied stream start - those are zeroed in both runs (the test's ZeroStack clears the
original's stack, ours zero-initialises). The six differing words "0130CD0E" are the stream starts' QueuedMsg
headers (id 0x0E, an untouched 0xCD pad byte, length 0x0130 = a full 0x100 piece + 0x30): the test's half-pointer
normalisation (any 16-bit half within the arena's segments is "relocated") treated the length 0x0130 as a stale
pointer half when our run's arena sat at 0x012A0000 (segments 0x12A..0x130 inclusive -> A006), while the original
run's arena was elsewhere. Exactly 6 such words are in each run's arena (counted). Fix: both runs use one arena
(the same address), so the pass treats both runs identically (selftests_netB.cpp Arena()). Nothing is masked; the
whole arena is still compared.

## Tests (src/hook/selftests_netD.cpp, 6 Early)
TestNetMetrics, TestNetManager (Winsock stood in at the import slots 0x6452A0 inet_addr / 0x6452A4 inet_ntoa /
0x6452A8 gethostbyname and in ws2_32 itself; Mlp task list 0x406C90 stood in), TestNetDescs, TestNetRleCompress
(~140 inputs + round trips), TestNetRleDecompress (each code with exact / short / plenty / no room, malformed code),
TestNetCompressMessage (stand-in memory manager, one arena; RLE and LZSS, both outcomes, the equal-size boundary,
other methods, empty).
Mutations: 96 deliberate bugs, 95 caught at once (several first by netB / netC tests, which run before these; checked that netD's own fail too), 1 after strengthening (the metrics totals were recorded only after the constructor cleared them). Equivalent, not run: the string setters' "copy only if different" guard, SetDeviceType / SetDHCP's compare.

## Checks
thug2_check: 0 MISMATCHED, all early tests pass. Register scan: only the known 0x4EFF92 RISK; every function in
the "read EAX/AL" list returns a value. Call audit: no MISMATCH. check_code_writes: no overlaps. find_gaps: nothing
skipped in or next to the range, 0 thunks.

## Disable
`[replacements] disable=net_metrics.cpp,net_manager.cpp,net_descs.cpp,net_compress.cpp` (netE's Manager code: net_manager_apps.cpp) in thug2-decomp.ini.
(The callers in net_app*.cpp / net_dispatcher.cpp / chat.cpp / cscript_manager.cpp now call these directly; disabling
the files only unhooks the game's own callers.)

## To test in the game
The network menu (settings shown / saved), host a LAN game and join it (second instance), chat, a big streamed
message (joining mid-game), DumpNetMessageStats if reachable; check thug2-decomp.log.
