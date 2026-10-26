# Playing online with THUGPRO players: what's missing

Research note (2026-10-21; read-only, nothing run, nothing online). The question: *what is needed before we can play
online games with THUGPRO players?* Each item says what THUGPRO does (with evidence), what we do today, how much it
matters (**blocks join**, **blocks host**, **blocks play** = crash, desync or refusal; **visible** = only looks or
plays differently) and the work to close it (S = a day or less, M = a few days, L = a week or more).

Sources: our repo (src/game/net, tools/pipeline/compat/thugpro);
THUGPRO's install (`%LOCALAPPDATA%\THUG Pro`: thugpro.dll, data\pre); Morten1337's sources in the session scratchpad
(`thugpro_proxy` = the DLL, `thugpro` = scripts and level data, `thugpro_toolchain`): used for insight only, per his
permission. Nothing here is copied from them. Addresses are the reference build's; THUGPRO's DLL patches the same layout (its
`thps6pc` string at 0x0064B1CC is the one our online_identity.cpp names). The scratchpad's proxy source is a developer
snapshot (`THUGPRO_DEVELOPER` on, placeholder `RELEASE_SCRIPTS_HASH 0xffffffff`); the installed release DLL has the same
markers (`rObOmOdOwO`, the LZHAM strings, the OpenSpy host list) and a real scripts hash (item 2).

## Short answer

Not possible today, for a protocol reason, not a level reason. THUGPRO's DLL changes the join handshake and the
compression on the wire:

1. **Join request**: a THUGPRO host reads every connection / join request as its own 48-byte struct (version, flags,
   name, machine id, **scripts hash**) instead of THUG2's 92-byte one, and refuses a client whose scripts hash isn't
   the hash of THUGPRO's release `thugpro_qb.prx`. Our client sends THUG2's struct: refused ("modifications").
2. **LZHAM compression**: THUGPRO sends player-create messages (each player's appearance) and its join requests with a
   compression method THUG2 doesn't have (method 3, LZHAM). Our HandleCompressedMessages passes an unknown method's
   buffer through unexpanded: garbage messages, a likely crash.
3. Both directions are affected: a THUGPRO player joining our host is refused as the wrong version.

Everything else (levels, CAS parts, animations, game modes, level options, chat) is parity or robustness work on top,
much of it already done for the ported levels by the compatibility register.

## 1. Lobby and identity

| | |
|---|---|
| THUGPRO | `GameSpy::InitPatches` makes the key.gamespy.com name lookup return at once (0x005F8D00 `ret`); `Net::InitPatches` writes the GameSpy host names as `*.openspy.net`, the MOTD URL (THUGPRO's own motd.dat), the game name `thugpro` over `thps6pc` (0x0064B1CC), nops gpInitialize (0x0054A4F2), shortens the NAT-negotiation timeout (0x005F88F9) and the qr2 heartbeat interval (0x005FBAF5), and patches three join-refusal paths (0x0053654F, 0x0052F70F, 0x0052F7DF: "cancel while connecting and rejoin"). `Config` replaces GetCDKey (0x0052D6A0) with a random key and derives a machine id (CRC of a motherboard-id MD5). Its NAT negotiation falls back to a direct connection when negotiation fails (Lobby.cpp `s_nat_negotiate_complete_Proxy`), and it maps ports with UPnP (miniupnpc). The secret key stays THUG2's `AdLWaZ`. |
| Ours | `OnlineDomain=openspy.net` (online_hosts.cpp, from PartyMod) and `OnlineGameName=thugpro` (online_identity.cpp, register TP-R1) give the same host names and the same lobbies ("General", "Newbies"). No CD-key change: our client sends THUG2's CD key, our host still runs THUG2's CD-key check (gcd, key host not redirected). No NAT fallback, no UPnP. |
| Impact | Lobby and server list: **done**. CD key: none for joining (THUGPRO's host ignores it); for **hosting**, THUG2's CD-key authentication of joiners goes to a host that no longer exists - unverified whether our LAN-tested host accepts internet joiners at all (THUGPRO cut it out for that reason). NAT: **visible** (joins fail more often where THUGPRO's would retry directly). |
| Work | Verify hosting's CD-key path online by hand (S); make the CD-key check a no-op for internet hosting, our own code in the decompiled ok_to_join (S, after item 9's decompilation); NAT direct-connect fallback (S); UPnP optional (M, a library). |

## 2. Version / compatibility handshake

**Update (2026-10-21, the join agent): done in the THUGPRO peer mode, and corrected.**
The installed release thugpro.dll does *not* use the 48-byte `MsgSkProConnectInfo` of thugpro_proxy's source (that
snapshot is an unreleased 0.8): its client sends THUG2's 92-byte connection request (version 0x0004000B) followed by
32 bytes - `_THUG_PRO_`, protocol version 0x00070000, release type, scripts hash, machine id, a "modified scripts"
byte - 124 bytes, LZHAM; its host (a hook on Net::Server's connection request handler) refuses a remote THUG2-version
request without the text (reason 6), with a version below 0.7 (31), "modified" while mods aren't allowed (32) or a
banned machine id (36) - the scripts hash itself isn't compared by the host. Its join request is THUG2's with the
machine id as the profile id. Its client maps reasons 31-37 to its own texts. Ours now sends exactly that (TP-R12, the
user's decision on the hash; our own release type "TUG2"), shows 31-37 in words (TP-R13), and as host skips the CD-key
check (TP-R12). The join path is source (src/game/net/gamenet_join.cpp). Hosting reads THUG2's 92 bytes of either
request; a 48-byte request is too short and ignored.

| | |
|---|---|
| THUGPRO | Replaces JoinServer 0x00534480, ReattemptJoin 0x00534AF0 / 0x00534BD0, SpawnServer 0x0053C380, SpawnClient 0x00537220, SpawnMatchClient 0x00534430 and ok_to_join 0x0052F560 (GameNet/Manager.cpp). The client's connection request (message 3) and join request (80) carry `MsgSkProConnectInfo`: version (big-endian int), flags (observer / willing to wait / broadband / developer / modified), password, name, machine id, profile, **scripts hash**; the join request adds the appearance after byte 48. Its ok_to_join refuses: banned IP; banned machine id (reason 35); full; game in progress; wrong password; version below `VERSION_MIN_COMPAT` (reason 31); **scripts hash** different from the host's release hash, or the modified flag set (reason 32, "modifications"). The scripts hash is the CRC-32 of `data\pre\thugpro_qb.prx` (fileproxy.cpp `getScriptsChecksum`; the toolchain's `calculate_qb_prx_hash.bat` runs a CRC tool on the same file). Checked: the installed `thugpro_qb.prx`'s CRC-32 is `0x09FC08B0`, and that constant is in the installed thugpro.dll (file offset 0x11B64) - the release hash. Message 159 (`MSG_ID_TP_CHANGE_MODS_ALLOWED`) lets a host drop clients with modified scripts later. There's no THUGPRO flag in the GameSpy server info. |
| Ours | THUG2's original join code (not decompiled): `MsgConnectInfo` 92 bytes, version `0x0004000B` (written at 0x005345A6), CD key at byte 18. Read by a THUGPRO host: the version passes its check by accident (byte-swapped it's larger), but the "scripts hash" it reads at byte 44 is CD-key text: **refused, reason 32**. THUG2's join-refused handler doesn't know reasons 30-37 (no sensible message). |
| Impact | **Blocks join** (refused). **Blocks host**: a THUGPRO client's 48-byte request read as THUG2's struct: version `0x00080000`-ish != `0x0004000B`, refused "wrong version" by THUG2's ok_to_join. |
| Work | (a) A per-session "THUGPRO peer" mode: send THUGPRO's request layout with a version >= its minimum, our name, a machine id and a scripts hash (S once the join path is source). (b) **A decision first**: the hash check exists so that everyone runs THUGPRO's scripts (its anti-cheat). Sending the release hash (computable from the user's own install at run time, nothing stored) while running THUG2's scripts would get us in, but defeats that check. Recommended: ask Morten1337 for an agreed identity (a release type or flag THUGPRO hosts recognise and can allow or refuse), and say so in the register. (c) Host side: accept both layouts (by length: 48 + appearance vs 92 / 120 + appearance), refuse with THUG2's reasons. (d) Map reasons 30-37 to messages (S). |

## 3. Network protocol changes

**Update (2026-10-21):** LZHAM (method 3) is done (TP-R11): expanded always, sent in the peer mode where THUG2 sent
LZSS; every received compressed message's lengths are bounded.

THUGPRO keeps THUG2's message numbering (0-154; GameMsg.h lists THUG2 PC's own messages up to 154, CAG included) and adds:

| Change | THUGPRO evidence | Ours | Impact | Work |
|---|---|---|---|---|
| **LZHAM compression**, method 3 inside THUG2's compressed message (0x12) | Encoding.cpp: CompressMessage's method test patched at 0x00454F04 / 0x00454F0E, the receiver's at 0x0045AC89 / 0x0045AC97; LZHAM 1.0 (`LZHAM_DLL_VERSION 0x1010`, MIT), dictionary 2^15, level "uber". Used for: player create (34, every player's appearance, always), the join request (80) and the connection request (3) - each sent compressed only when smaller, as THUG2 does | Our CompressMessage 0x00454E30 knows RLE (1) and LZSS (2); HandleCompressedMessages 0x0045ABF0: "any other method: the buffer is used as it is" - an unexpanded buffer read as id + length + data | **Blocks join** (we can't read the players we join) and **blocks host** (a THUGPRO join request) | Link LZHAM 1.0 (the public lzham_codec, MIT; THIRD_PARTY_NOTICES) and add method 3 to both functions with THUGPRO's parameters; self-tests round-tripping its streams (M) |
| Messages 155-159 | 155 server info (time of day, weather, 8 level options, 8 pro-set flags) sent before every start info (Application.cpp: EnqueueMessage hook on 51); 156-158 updates of each; 159 mods allowed | Unknown ids have no handler: our dispatcher skips them by their length (net_dispatcher.cpp) - harmless | **Visible**: THUGPRO's host options (level option geometry, pro sets, time of day) not applied on our side - see 5 | **Done in the THUGPRO peer mode (2026-10-21, TP-R3 / TP-R4; not tested online)**: 155-157 applied (TOD manager, rain / snow, `<level>_LEVEL_CHANGE_0n on`, the pro sets' prefixes in THUGPRO's THPS4 levels and NY), 158 ignored as THUGPRO, 159 = 0 leaves; as host 155 before each start info, 156 / 157 from `/tod` / `/weather`. Not done: options a THUGPRO host changes during a game (its flag broadcasts) |
| Change level (70) + 32-byte level name | EnqueueMessage hook appends the level's name ("Skateshop", "Custom Park"...); receivers check the load script exists and show `CreateServerMissingLevelDialog` | THUG2 reads the first 8 bytes; the extra bytes are ignored (length-prefixed) | None for receiving; a THUGPRO joiner of ours shows "Unknown" names | **Done in the THUGPRO peer mode (TP-R5)**: the name appended when hosting |
| Chat (76) with a 4-byte machine id first | `MsgChatTHUGPRO` (Manager.cpp `s_handle_chat_Proxy`, sent by its SendChatMessage replacement; mute list by machine id) | Our chat.cpp sends THUG2's layout; receiving uses THUG2's handler | **Visible**: chat garbled both ways (object id / name shifted by 4 bytes) | **Done in the THUGPRO peer mode (TP-R2)**: sent and read in its layout (the handler 0x00532050 decompiled); no mute list on our side |
| Run script (72) whitelist | Dispatcher hook: a client runs only 14 THUGPRO scripts by this message (scavenger hunt, firefight, autoserver, `_server_showmessage`, panel messages...) | THUG2 runs any | **Visible** when we host: THUG2 modes that broadcast other scripts do nothing on THUGPRO clients | Note per mode; nothing to do on our side for joining (S, docs) |
| Guards on received messages | Create / destroy special item (119/120): dropped when the details struct doesn't exist; primary anim start (41): an animation that isn't loaded is replaced by `default` | THUG2's handlers as they are | **Blocks play** (likely crash) when a THUGPRO player uses an animation or special item we haven't | The same two guards in our code (S each; the handlers need decompiling or a narrow source replacement) |
| Graffiti change mask | Net::InitPatches: CTrickObjectManager's change mask moved to a 2048-byte buffer (0x00588700 / 0x00588963 / 0x005889B4): "Fix 256 graff tag crash" | THUG2's original buffer | **Blocks play** in graffiti / observing in levels with more than 256 trick-object clusters (our THAW ports have hundreds) - offline too | Enlarge the mask in source when the trick-object manager is decompiled, or as a data fix now (S) |
| Big-level positions | `THUGPRO_SetBigLevel` (cfuncs.cpp), run from its Levels.q load in network games for levels with `IsBigLevel` (only **The Mall, z_ma**) / `IsTallLevel` (only its unreleased `beach`): skips the position scaling in the object update send / receive (0x0056C1FF, 0x0056E813, 0x00513058, 0x0051343B, 0x0056975F, 0x0056999A; y: 0x0056C1E4...) | Normal scaling everywhere | **Blocks play** in z_ma: remote skaters drawn at the wrong scale of position | Per-level flag in our object-update code (S once decompiled; else a small patch table) |
| Spawn randomisation | `RandomizeSkaterStartingPoints` 0x005359F0 rewritten (host side), RNG functions 0x00403DA0... replaced | THUG2's | None: decided on the host | - |
| Time of day in net games | `InitializeMiscPatches` enables the time-of-day system in network games (0x00573BF8, 0x00574683, 0x00574375, 0x00574410) | Off in network games (THUG2) | **Visible** | **Done in the THUGPRO peer mode**: our TOD manager's multiplayer lock lifted in its network games (SetMultiplayerUnlock; the 4th patch is what multiplayer already does) |

Sizes: THUGPRO's net library constants are THUG2's (UDP packet 1300, appearance 4096, name 15, chat 127, password 9):
no buffer-size change on the wire.

## 4. Levels

| | |
|---|---|
| How a level travels | Only its load script's checksum: change level (70), start info (51), the player-info ack request (83, `m_Data` = level, checked by THUGPRO's `s_handle_player_info_ack_req_Proxy`). Names don't travel (but THUGPRO's level-name string, above). |
| THUGPRO | Its Levels.q: THUG2's 15 (TR, BO, BA, BE, AU, NO, ST, SE, SE2, LA, SC, PH, DJ, CA, AP); THUG1's 12 (NJ NY FL SD HI VC SJ RU VN HN SC2 HH); THPS4's 10 (`practice` = the skate shop, sch, alc, sf2, kon, lon, jnk, zoo, cnv, hof); THPS3's 9 (foun rio sub tok shp si ware burn ros); THAW's 15 (skatepark, kyoto, sz, vans, z_ma, z_dn, z_bh, z_dt, z_el, z_ho, z_lv, z_oi, z_sm, z_sr, z_sv2). **THUGPRO-only, installed**: `atlanta`, `HISCH` (High School), `z_funpark`, `z_center`, `z_riod` (Rio downhill), `z_ms` (Marseille), `oil` (Oil Rig), `toystory_bedroom`, `SkProDefault`; in its source but not installed: `beach`, `nyc`, `z_training`, `z_houses`, `z_Shops`, `z_cf`, `Z_CRETEPARK`. |
| Ours | The same load names for every shared level (register TP-1 THAW, TP-20 `Load_practice`, TP-30 `Load_hh`; THPS3 / THPS4 / THUG1 use their own names, which THUGPRO uses too). GetLevelName 0x005369A0 (ours) finds the ported levels. Ours only: `thps3_ap`, `thps3_la`, `can`, `motox`, `thps3_tut`, `thps3_skateshop`, `thug1_dj` / `thug1_ph` / `thug1_sc` / `thug1_nj_skateshop`, TestLevel, our skate shop. |
| A level one side lacks | THUGPRO checks (`ScriptExists` on the load script) and leaves with a "missing level" dialog. THUG2 (ours, original code) runs the change-level script without checking: behaviour unverified (stuck load or crash). |
| Impact | Shared levels: **none** (same checksums). THUGPRO host on a THUGPRO-only level: **blocks play** until we guard it; ours-only levels: THUGPRO joiners leave cleanly. |
| Work | Missing-level guard on change level / start info / ack request with a message (S). THUGPRO-only levels: out of scope for the converters (no source game data for most - `z_riod` noted); could be read from the user's THUGPRO install by reference like `thugpro_new` (L, and a policy decision). |

## 5. Level content parity

What the register (tools/pipeline/compat/thugpro/README.md) covers, all build time, under `--compat thugpro` (the default):
TP-1..TP-41 (names, frames, spawns, CTF / team nodes, KOTH crowns, node moves / flags / renames, rails, collision and
face flags for the THAW, THPS3, THPS4 and THUG1 ports), TPN-* (THUGPRO's own meshes, nodes, scripts, models,
level-option scripts) and PC-* (our own deviations). Spawns and flags don't travel; positions do - so parity is about
both sides building the same rails, walls and spawns.

Still open:

| Item | Evidence | Impact | Work |
|---|---|---|---|
| **THUG2's own 15 levels** | Compared (TP-42, `tools/pipeline/thug2_levels/thugpro_compare.ts`): THUGPRO replaces the node arrays of TR, BO, BA, AU, LA, the collision of AP, TR, DJ, SC, and applies LevelChanges / host options after loading; NO, SE, SE2 are THUG2's. 78 entries, 29 parity (AU's secret areas in net games, BA's kill triggers and passable bridge, BO's ledge rails, AP / SC / DJ one-sided faces and wall-rides, host options' state); spawns, crowns and CTF nodes identical | Until switched on, a THUGPRO player and ours disagree on those rails, walls and kill zones | The user picks entries (`enabled` in tools/pipeline/compat/thugpro/thug2_levels.json); `thugpro_apply.ts` / make_menu_level.ts build them into the overlay; a load check per level (S each) |
| Host level options | TPN-THAW-5 / TPN-THPS4-2 / TPN-THUG1-4: we run THUGPRO's defaults at load; a host who changes them (messages 155-158) changes geometry and rails | **Blocks parity** whenever a host uses them | Item 3's 155-158 handler (M) |
| Pending hack decisions with online impact | `tok-trains-forcecreate-online` (Tokyo's trains / planes run online in THUGPRO, not in ours; THUGPRO clients disagree among themselves on timing); `collfix-overlay-collision` (Sub / Foun extra collision, copied); `sm-ferris-wheel-static-rails` (copied; whether our cars draw needs checking); `thaw-el-sm-hand-offsets` (TP-2 / TP-3 frames, copied - must stay as THUGPRO's for positions to agree); `level-option-default-by-script` (copied); `ma-collidable-light-glows` (copied); `vc-sj-death-zone-and-arrows`, `class-swaps`, `occlusion` (client-side only) | Tokyo trains: **visible** (moving hazards only THUGPRO players hit); the rest agree today because we copy them | Keep the copies until both sides change; Tokyo: decide whether to run the trains online (L, and timing never agrees) |
| Applied, never run | TP-5..TP-9, TP-13..TP-19, TP-22..TP-39: "Applied (awaits a game run)" | Unknown | Load checks per level in a workspace (S each) |
| The Mall | big-level scaling (item 3) | Blocks play there | S |
| Spawns, crowns, CTF flags, KOTH | TP-4, TP-10..12, TP-21, TP-31 match THUGPRO's places and order; KOTH picks a crown by index (CFunc_SpawnCrown 0x00538170) | Done for the ports; THUG2's own levels unchecked (row 1) | - |
| Gaps | Not sent (each client scores its own tricks; scores travel) | **Visible** only (gap names / scores may differ where THUGPRO edited gap scripts: TPN-THAW-4) | - |
| Skate shop door | TP-29 off by the user's choice: ours open, THUGPRO's closed | **Visible** (ours ride through where theirs hit a wall) | Switch on for online if wanted (S) |

## 6. Game modes and game scripts

| | |
|---|---|
| THUGPRO | gamemode.q: THUG2's list plus `gold` / `netgold` ("Pot of Gold", listed, definitions not found in the source) and `netrace` (Race, `mode_netrace`). Rules, options and menus in its own `thugpro_net.q` (2303 lines), `thugpro_server.q` (1533), `autoserver.q`; free skate itself runs as a network game (its menus host a server: register TP-41). The host sends game info (52: mode checksum and rules), start info (51), selected goals (109), goal / rail data for created parks (135 / 136), the run-script messages (72, whitelisted on its clients). |
| Ours | THUG2's game modes and scripts. A mode checksum we don't define (netrace, netgold) arrives in game info: THUG2's mode lookup fails - behaviour unverified. |
| Impact | Free skate / net lobby: **none**. THUG2's modes (trick attack, graffiti, KOTH, CTF, horse, combo mambo, slap, elimiskate, scavenger hunt, firefight, goal attack): should run, rules THUG2's on our side - **visible** differences where THUGPRO changed its scripts (score caps, timers, its option menus). Race / gold: **blocks play**. |
| Work | Refuse or leave cleanly on an unknown mode (S). Compare THUGPRO's mode scripts with THUG2's per mode for what each client runs locally (M). Created parks (`load_sk5ed_gameplay`, rail / goal data): THUGPRO extended the park editor (ParkEditor.cpp, height patch) - check the park data layout before allowing them (M). |

## 7. Skaters, CAS and boards

| | |
|---|---|
| THUGPRO | The appearance (a skater profile buffer, up to 4096 bytes) travels in the join request and player create. THUGPRO's CAS lists add ~54 part ids THUG2 hasn't (cas_skater_m/f/shared: the THUG1 pros' heads `*_thug`, Kelly Slater, Darth Maul, Jango, Doomguy, Eddie, Octodad, pumpkin, Christmas hat / beard, `skin_*`...), 55 boards (`thugpro_boards.q`), its stickers, models in `models\CAS_ITEMS` and `thugpro_models.prx`. Its file proxy substitutes a default sticker / board texture when an image is missing (fileproxy.cpp). |
| Ours | THUG2's CAS lists; skater_profile.cpp / profile_manager.cpp partly ours. Building a remote skater whose profile names a part we don't define: unverified (THUG2's lookup of an unknown desc_id). |
| Impact | Possibly **blocks play** (a crash when a THUGPRO player using one of those parts joins); at best **visible** (default parts). |
| Work | An offline test: a THUGPRO profile buffer (from `%LOCALAPPDATA%\THUG Pro\Save`) read and built by our code (S). A sanitiser for received appearances: unknown desc_ids -> the slot's default, unknown textures -> default (S-M). Parity: load THUGPRO's CAS parts and boards from the user's install by reference, like `thugpro_new` (M-L). |

## 8. Gameplay changes

THUGPRO patches physics, walking, tricks and scoring (SkaterCorePhysicsComponent / WalkComponent / TrickComponent /
Score InitPatches; its changelog: Boned Ollie, THUG1 vert BHRA, off-rail wallplant, revert / dropdown / focus options,
air shuffle...). Each client simulates its own skater and sends positions and animations, so these are **local**: no
desync. What does cross:

- **Animations**: THUGPRO players play animations from `thugpro_anims.prx` (THUG1 / THAW sets in its source). Our side
  receives their indices: the missing-animation guard (item 3) stops a crash; parity means loading its animations
  (M, by reference from the install).
- **Scores** in score modes: each client reports its own (score update 64, combo report 134). THUGPRO's scoring
  changes (base vs multiplied score option, special multipliers) make totals **visibly** different - a fairness
  question, not a sync one.
- **Skater collisions / slap**: decided by the server from positions: fine.

## 9. Our side: what is ours on the online path

- Rewritten and installed (src/game/net, 221 REPLACE lines): the whole engine net library 0x4544E0-0x45BDA0 (App, Conn,
  Client, Server, Dispatcher, compression, metrics, latency), chat (0x00537BD0, 0x005A16B0, KeyboardTask 0x005334D0),
  GetLevelName, InNetMode / InNetGame, the online service task 0x00545880, top stats 0x0054DCB0, online hosts and
  identity (data writes).
- Tested: LAN hosting / joining with two copies on one PC (batches 83-86, 2026-10-17, by the user); chat in a network
  game (2026-10-15). **Never tested over the internet** (OpenSpy, NAT negotiation, the GameSpy SDK's paths with our
  library around them), never against THUGPRO.
- Still original game code: the GameNet manager (join / host / ok_to_join / lobby, 0x0052E000-0x00540000, 24 % of that
  range rewritten) and the Skate module's network handlers (0x00569xxx-0x0056Fxxx: object updates, start info, anims,
  change level). Every handshake change in items 2-3 lands there. THUGPRO patches those addresses in place; we never
  patch rewritten code (check_code_writes), so the clean route is to decompile them first and put the THUGPRO mode in
  source.
- Risk: our net library is equivalent to the original by the self-tests, so it's no worse than THUG2's against
  THUG2; the new risk is only in what we add for THUGPRO (decoders reading untrusted lengths: bound them).

## 10. Anything else

- **Anti-cheat**: client-side only - the scripts hash and modified flag (item 2), machine-id and IP bans, the
  mods-allowed message (159). THUG2's own cheat checksum request (128 / 129) still runs on both. No server-side
  check exists: OpenSpy only lists the game. Getting past the hash is a consent question for Morten1337 (item 2b).
- **Policy**: online testing is by hand only; nothing in this plan needs an agent online - every decoder
  and layout can be tested offline from captured or constructed messages.
- Bans and mutes key on the machine id: ours should send a stable, non-identifying one (a random id kept in the ini).
- Observer mode: THUGPRO rewrote it (Observe.cpp: 0x00536890-0x00536997 nopped, ObservePlayer redirected) - check
  before allowing observers in mixed games (S).
- Server name format in the browser ("%s - %d:%d", 0x0064CC00) and the MOTD URL: cosmetic.

## Plan

**A. Join a THUGPRO player's free-skate session (the minimum)**

1. Decide with the user, and ask Morten1337, how our client identifies itself to THUGPRO hosts (item 2b). Without an
   agreement only the hash-spoofing route exists.
2. Decompile the join path: JoinServer 0x00534480, ReattemptJoin 0x00534AF0 / 0x00534BD0, s_handle_join_proceed,
   s_handle_join_refused, s_handle_player_info_ack_req, change level / start info handlers (M).
3. LZHAM method 3 in CompressMessage / HandleCompressedMessages (M) - self-tests with THUGPRO-format streams.
4. THUGPRO request layout in a per-session THUGPRO mode (`OnlineGameName=thugpro` turns it on), refusal reasons
   30-37 (S).
5. Robustness guards: missing level, unknown game mode, missing animation, missing special-item struct, appearance
   sanitiser, bounded lengths (S each, ~M together).
6. Chat layout in THUGPRO mode (S). - **Done** (TP-R2).
7. The user tests by hand: join a THUGPRO free-skate server in a **ported** level both sides share (a THPS3 / THPS4 /
   THUG1 one; not The Mall, not THUG2's own levels until 9 below), read the log.

**B. Host for THUGPRO players**

8. Decompile ok_to_join 0x0052F560, s_handle_connection, s_handle_join_request, SpawnServer 0x0053C380; accept both
   request layouts, LZHAM join requests, no CD-key authentication, the level name in change level, server info 155
   with defaults (M).
9. Hosting test by hand (THUGPRO players need a reachable host: the NAT fallback / UPnP, item 1).

**C. Full parity**

10. THUG2's own 15 levels compared with THUGPRO's levels_ug2 / `*_thugpro.prx` and entries applied (M-L).
11. Messages 155-158 applied: level options, pro sets, time of day, weather (M). - **Done** (TP-R3 / TP-R4; in-game
    option changes by a THUGPRO host still wait for the next game).
12. The Mall's big-level scaling; the graffiti change mask (S).
13. THUGPRO's CAS parts, boards and animations from the user's install by reference (M-L).
14. Game-mode parity: each mode's local scripts against THUGPRO's; race / gold or a clean refusal (M).
15. The register's "applied, awaits a game run" entries load-checked; the pending hack decisions settled (Tokyo trains).
16. Register entries for every run-time THUGPRO agreement above (TP-R2...), as the register's "Run time" section asks.
