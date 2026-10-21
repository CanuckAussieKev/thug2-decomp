# THUGPRO join: the join path decompiled, THUGPRO's handshake in the peer mode, LZHAM (agent "join", 2026-10-21)

NOT run online (never - by hand only). Built; `thug2_check` on THUG2PM.exe and the stock THUG2.exe: 0 MISMATCHED, all
early self-tests pass. docs/thugpro-online.md items 1-3 / plan A 2-4, B 8. The user's decision (2026-10-21): "For the
THUGPRO hash, we could hardcode it and update it as necessary" - with Morten1337's permission to use THUGPRO (its
source, binaries and protocol) as the compatibility reference. Insight from Morten1337's thugpro_proxy (cloned shallowly
into out\, uncommitted) and from the installed release thugpro.dll (read only, disassembled with capstone), written here
in this project's terms; nothing copied. Register: TP-R11 ... TP-R13 (tools/compat/thugpro/README.md).

## Finding: THUGPRO's release speaks a different handshake than its source snapshot

thugpro_proxy's source (a developer snapshot, VERSION 0x00080000, `THUGPRO_DEVELOPER`) replaces the connection / join
request with a 48-byte `MsgSkProConnectInfo`. **The installed release doesn't.** Disassembling the installed
thugpro.dll (its EnqueueMessage hook at 0x10038C60, its EnqueueMessageToServer hook at 0x10038C30, its replacement of
Net::Server's connection-request handler at 0x100394F0 - written over the handler pointer at 0x0045B7D0 -, its
join-refused handler at 0x100398F0):
- its client sends THUG2's connection request (92 bytes, version 0x0004000B) and appends 32 bytes: `_THUG_PRO_` at 92,
  its protocol version 0x00070000 at 104, its release type (0 = beta) at 108, its scripts hash at 112, its machine id at
  116, its "modified scripts" byte at 120 - 124 bytes, compression method 3 (LZHAM);
- its join request (80) is THUG2's, the machine id written over the GameSpy profile id (+0x5C), LZHAM; player create (34)
  LZHAM; change level (70) with the 32-byte name (TP-R5);
- its host, for a remote request with THUG2's version: no `_THUG_PRO_` -> refused 6; byte-swapped version < 0x700 ->
  31; release type 2 (its developers') -> accepted at once; "modified" while the host's mods setting is off -> 32; a
  banned machine id (FNV hash set) -> 36; else THUG2's own handlers (its CD-key checks patched out: 0x0052F70F,
  0x0052F7DF, GameSpy's answer 0x0053654F always "authenticated"). **The scripts hash isn't compared by the host** - its
  own client sets "modified" when its thugpro_qb.prx's CRC-32 isn't 0x09FC08B0 (the constant at file offset 0x11B64 is
  that test);
- its client shows reasons 31-37 by its tp_net_reason_* texts; <= 30 by THUG2's handler.
- Its receiver doesn't expand LZSS (its 0x0045AC89 patch makes method 2 skip the decoder) and its CompressMessage sends
  method 2 as an empty "compressed" message: THUG2's two LZSS messages (80, 34) must go as LZHAM.

So the peer mode sends what the release reads. A 48-byte request to a release host would be read as THUG2's (its first
4 bytes as the version): refused. (If a later THUGPRO release switches to the 48-byte layout, the trailer code in
thugpro_join.cpp is the one place to change.)

## 1. The join path decompiled (src/game/net/gamenet_join.cpp, gamenet_manager.h)

| Address | Function |
|---|---|
| 0x0052F560 | Manager::OkToJoin (thiscall, 6 arguments, RET 0x18) |
| 0x00534480 | Manager::JoinServer (ip, port, index) |
| 0x00534AF0 / 0x00534BD0 | ReattemptJoin (client) / ReattemptJoinWithPassword (password) |
| 0x00537220 / 0x00534430 / 0x0053C380 | SpawnClient / SpawnMatchClient / SpawnServer |
| 0x0052F790 | HandleConnection (message 3, host) |
| 0x0052FA10 | HandleJoinRequest (message 80, host) |
| 0x0052E490 | HandleJoinProceed (message 79, client) |
| 0x0052E1E0 | HandleJoinRefused (message 71, client) - the join-refused handler, a jump-table switch over reasons 0-8 |
| 0x00536530 | ClientAuthorized (GameSpy gcd_authenticate_user's callback) |

GameNet::Manager's layout extended (the join's fields: +0x10 lobby, +0x18 presence, +0x1C stats, +0x20 the CD-key
challenge, +0x2C match client, +0x3C join mode, +0x40 host mode, the tasks +0xCC / +0xDC / +0xE4, +0x184 the refusal's
text, +0x198 the join state, +0x19C... the join address); THUG2's join messages as structs (ConnectInfo 92, JoinInfo,
ProceedInfo 32, the refusal reasons, the join states).

Findings: THUG2's CD-key handshake - the host refuses a request whose CD-key response isn't 72 characters with reason 8
and its challenge after it; the client answers with a new connection request (join state 5) carrying GameSpy's response
to that challenge; on the internet the host then asks GameSpy (gcd, game id 1003) and proceeds from the callback.
JoinServer's first request has no response at all (its stack's garbage). The original sends stack garbage in every field
it doesn't set (the rank on a local join, the CD-key field, a LAN proceed's addresses...): ours zeroes them. The host's
own join (local) also asks GameSpy about its own CD key with no instance - an answer nobody reads. The connection
handler writes its `reason` into its caller's stack frame (one slot past its argument) - ours uses a local.

NOT ORIGINAL in the source (all marked): the peer mode (below); bounds - a connection request shorter than 92 bytes
and a join request shorter than its 120-byte header are not read (returned as "message done"); a join request's
appearance is read from a 4 KB copy (a short one gets zeros after it, not the bytes past the message); the CD-key
response's length counted inside its field; join refused's challenge read from the message's own bytes.

## 2. The THUGPRO peer mode's handshake (src/game/net/thugpro_join.cpp / .h) - TP-R12, TP-R13

`GameNet::Thugpro::PeerMode()` (thugpro_peer.h - `[online] thugpro_peer=1` or `OnlineGameName=thugpro`), nothing new:
- the connection request from all four senders (JoinServer, both reattempts, the CD-key answer) gets THUGPRO's trailer
  (124 bytes, LZHAM asked for): version 0x00070000, **release type 0x32475554 ("TUG2" - ours, not THUGPRO's beta /
  public / developers'; THUGPRO's host only treats 2 specially)**, the scripts hash `ScriptsHash()` (`kReleaseScriptsHash`
  0x09FC08B0 - one constant, the comment says how to update it; `[online] thugpro_scripts_hash=<hex>` overrides it), the
  machine id (`MachineId()`, TP-R2's), "modified" set exactly when the hash isn't the release's;
- the join request carries the machine id as the profile id;
- as host: no CD-key length check in ok_to_join, no GameSpy authentication of a joiner (the proceed goes at once, with
  the internet addresses) nor of the host's own key; a THUGPRO client's trailer logged ("THUGPRO join (TP-R12): a
  THUGPRO client asks to join: version ..."). Both layouts are read by length (THUG2's 92 bytes of either);
- reasons 31-37 (any mode): global texts made under THUGPRO's names (tp_net_reason_version_low ...) with our words, so
  THUG2's CreateJoinRefusedDialog shows them; logged "THUGPRO join refused (TP-R13): reason N - ...".
`node tools/compat/thugpro/scripts_hash.ts` compares the constant with the installed thugpro_qb.prx (today: up to date).

## 3. LZHAM (src/game/net/net_lzham.cpp) - TP-R11

lzham_codec (Rich Geldreich; public domain since 2020, MIT before) pinned at d379b1f with its SHA-256 in CMakeLists.txt,
built as a static library (its own sources; /W0). Its codec is THUGPRO's copy's (diffed: only licence headers and
LZHAM_FORCE_SINGLE_THREADED_PARSING differ - no bitstream change; THUGPRO's parameters leave threads off anyway).
THUGPRO's parameters: dictionary 2^15, the "uber" level, no flags, no helper threads. CompressMessage 0x00454E30: method
3 = LZHAM (a stream that fails or doesn't fit = "no smaller": sent uncompressed); in the peer mode method 2 (LZSS) goes
as 3. HandleCompressedMessages 0x0045ABF0: method 3 expanded into the header's size exactly (its adler32 checked too).
NOT ORIGINAL bounds on every received compressed message: shorter than its header, an expanded size under 3, an RLE
code past the stream's end, an LZSS stream expanding past its size or a reference cut short, an LZHAM stream that
fails, an inner length past the expanded data, an unknown method (the original dispatched uninitialised memory) - all
dropped (handler result 4) before anything is written.

## Tests

- TestGameNetJoin (selftests_gamenet_join.cpp, Early): **327 cases, the original against ours** on a stand-in world
  (the GameNet manager and its apps / connections in one allocation, every call - net library, GameNet's not-yet-source
  getters, scripts, GameSpy's CD-key SDK, memory manager, tasks, timer, sprintf - stood in by recorders; the messages'
  defined bytes, the handlers added with their ids / codes / flags / data / priorities, the manager's 0x1D0 bytes, the
  passwords and the connections compared): ok_to_join 27 cases (every refusal, each check on / off, signed ranks, both
  rank tests, local connections, a CD-key response one too long), SpawnClient 32, SpawnMatchClient, SpawnServer 8,
  JoinServer 32 (local / remote, both clients, password typed, observing, modem; each to its own port), the reattempts 16, HandleConnection 65 (local, foreign flag, foreign
  connections taken, LAN / internet, banned, the CD-key challenge), ClientAuthorized 8, HandleJoinRequest 32,
  HandleJoinProceed 64, HandleJoinRefused 42 (reasons -1..9, 30, 38, 127 in join states 0 / 4 / 5).
- TestGameNetJoinThugpro (ours): the four senders' 124-byte request checked byte for byte and against THUGPRO's host
  check as disassembled (accepted; "modified" with another hash -> 32); THUG2's 92 bytes outside the mode; the join
  request's machine id; the host without GameSpy's CD-key check (a made-up key proceeds at once, with the addresses;
  the host's own key not checked); reasons 31-37 -> THUGPRO's names (their CRC-32s), string symbols with our words,
  refused state; the bounds (short requests not read, a short appearance read with zeros, a cut challenge) in and out
  of the mode.
- TestThugproLzham / TestThugproCompressedMessages (selftests_thugpro_join.cpp): LZHAM round trips (1 B ... 64 KB,
  repetitive and random), two fixed streams (40 bytes; 4000 mixed bytes, whose stream depends on the level too -
  THUGPRO's parameters can't drift), cut / changed / too-big streams refused
  without writing past the room; CompressMessage's method 3, LZSS -> LZHAM only in the mode, RLE kept, incompressible /
  tiny data uncompressed; HandleCompressedMessages: LZHAM and LZSS messages dispatched, every malformed one dropped; each message ends
  where a no-access page begins (a read past it crashes the test), 1- and 2-byte messages of every method, inner lengths
  one past / exactly filling the data.
- selftests_netC / netD adjusted: HandleCompressedMessages' original-vs-ours cases use real streams (ours checks them
  before expanding); CompressMessage's "unknown method" case is method 4 (3 is LZHAM now).
- Mutations (out/mut_join.ts, 90; `node tools/mutate.ts out/mut_join.ts`): **86 caught**. 4 equivalent, missed: `join-index`
  (`index == 0` -> `index != 1`: the manager has two client slots, 0 and 1), `lzham-capacity` (lzham_compress_memory
  never reports more than the room it was given - it fails instead), `recv-size3` (an expanded size under 3 is dropped
  by the inner-length check anyway; only a one-byte read past the 2-byte block differs), `recv-lzss-over` (the scan's
  early exit: its final `expanded == expected` refuses the same streams).

## Checks

- thug2_check on THUG2PM.exe and on C:\Games\THUG2\Game\THUG2.exe: 3579 replacements, 0 MISMATCHED; early self-tests
  all passed (540) on both.
- check_code_writes: 26 run-time writes, no overlaps. audit_game_function_calls: no MISMATCH.
- scan_register_assumptions: nothing for this batch (SpawnClient / SpawnMatchClient's return values are read - they
  return them); its RISK lines are older (CStruct::AddVector, Ed::InitEditedGoalStub).
- find_gaps - new gaps next to this batch, left for a later batch: 0x0052F730 / 0x0052F760 (scalar deleting
  destructors of vtables 0x0064C828 / 0x0064C82C, no direct callers), 0x00536640 (calls 0x005F9C40, then 0x005FBBA0(0)
  when 0x007CCA82, the lobby 0x007CCE14's 0x0044E4F0 and 0x007CCE1C are set), 0x00536670 (scalar deleting destructor over
  0x00577280), 0x005366A0 / 0x005366C0 / 0x005366E0 / 0x00536700 (list-node destructors, vtables 0x0064C9E8 / 0x0064C9EC
  / 0x0064C9E4 / 0x0064C8EC: unlink, point at themselves) and their scalar deleting forms 0x00536720 ... 0x005367E0,
  0x00536820 (a list's clear at +0x13C: each node's virtual delete, 0x00543560 on its object, freed).

## Switching it off

`[replacements] disable=gamenet_join.cpp` (the whole join path back to the original - and with it the peer mode's
handshake). The peer mode off (the default): THUG2's own handshake (but LZHAM still expanded, bounds kept, reasons 31-37
in words). LZHAM / bounds: `disable=net_app_messages.cpp` / `net_compress.cpp` (each the whole file).

## For the user to try by hand (online - only by you)

1. Offline first: start the game normally, check thug2-decomp.log has no "RUN-TIME CHECK" / "DEBUG CRT" lines and the
   replacements of gamenet_join.cpp installed (12).
2. LAN, THUG2 vs THUG2 (two copies, `allow_multiple_instances=1`, the mode off): host and join as before - nothing should
   change (the join path is source now). Try a wrong password, a full server, joining a game in progress.
3. THUGPRO peer mode (`[online] thugpro_peer=1` or `OnlineGameName=thugpro`), join a THUGPRO player's free-skate server
   in a level both have: you should get in (no "modifications" refusal). In the log: no "THUGPRO join refused" line; if
   one appears, its reason says why (31: THUGPRO raised its minimum version - kProtocolVersion; 32: the host doesn't
   allow mods, or the scripts hash changed - run `node tools/compat/thugpro/scripts_hash.ts`; 36: banned).
4. See their skater appear (player create arrives LZHAM-compressed: a wrong decode would drop it - their skater
   missing would point there), chat both ways (TP-R2).
5. Host in the mode for a THUGPRO player: the log shows "THUGPRO join (TP-R12): a THUGPRO client asks to join: version
   00070000 ..."; they should get in without a CD-key wait.
