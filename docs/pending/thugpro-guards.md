# THUGPRO guards: The Mall's positions, the graffiti mask, received-message and appearance guards (agent "guards", 2026-10-21)

The user's decision: "Skaters' parts and boards, Mall, graffiti, we should handle this." (docs/thugpro-online.md items
3, 7 and the plan's 5 / 12 / 13). NOT run in the game yet (offline self-tests only; nothing here ever went online).
Insights from Morten1337's thugpro_proxy (DispatchMessage hook, THUGPRO_SetBigLevel, Net::InitPatches), used with his
permission and written in this project's own way; nothing copied. Register entries TP-R2...TP-R5
(tools/compat/thugpro/README.md, "Run time").

## 1. The Mall: THUGPRO's big-level positions (TP-R2) - batch "thugpro-guards 1"

A skater's position travels as three int16s in the skater updates (message 0x23): THUG2 sends x / z in half units and y
in quarters (+-16384 / +-8192 units). THUGPRO's `THUGPRO_SetBigLevel` (run from its load_level in network games, for
levels with `IsBigLevel` - only The Mall, z_ma - or `IsTallLevel` - only its unreleased beach) NOPs the scaling: whole
units. Without the same on our side a THUGPRO player in z_ma is drawn at half / double the distance (wrapped).

Decompiled to source first (original code, then the scale in it):
- `src/game/skater/local_net_logic.cpp` (new): the local skater's update to the server - 0x00512EA0 GetUpdateFlags,
  0x005131A0 SendUpdate (CSkaterLocalNetLogicComponent; the rest of the component, 0x00512C60-0x005136E8, isn't yet).
- `src/game/skate/skate_net.cpp`: the server's side - 0x005696C0 GetRelayFlags, 0x005698F0 WriteObjectUpdate,
  0x0056BE60 HandleClientObjectUpdate; HandleObjectUpdates 0x0056E470 (already ours) uses the scale.
- `src/game/net/thugpro_compat.cpp` (new): PositionScale, Pack / Unpack (the original's FMUL / FADD ST0 / _ftol), and
  RefreshPositionScale - once a frame from MainLoop (mlp.cpp): in a network game, the current level's (the network
  manager's m_level, a load script) level select entry flags IsBigLevel / IsTallLevel; logged when it changes.
- The flag comes from the data: `thaw_levels.json` `net_position_scale` {big: [Z_MA]} -> `level_select.ts
  thawPositionScaleFlags` adds ` IsBigLevel` to The Mall's level select entry (the overlay's
  level_select_menu_thaw_level_info). **Needs `make_menu_level.ts` re-run** to reach a game folder.
- One deliberate difference from THUGPRO: HandleObjectUpdates' first state entry (its own copy of the skater's position,
  0x0056E70C / 0x0056E725) is written in the level's scale; THUGPRO left those two in THUG2's (its NOP list misses them).
- Tests (selftests_thugpro_guards.cpp, thugpro_testkit.h; selftests_tod4.cpp's TestSkateNetObjectUpdates now in the 4
  scales): the original **with THUGPRO's own NOPs written in** (its 14 patch sites, and the two first-entry ones) against
  ours with the same scale - TestClientObjectUpdates (0x0056BE60: every field mix, unknown / unready players, late /
  foreign packets, time stamps going back), TestRelayObjectUpdates (0x005696C0 / 0x005698F0: records equal or not, the
  60-frame refresh), TestLocalNetLogic (0x00512EA0 / 0x005131A0: server / client, the 15th frames, the send interval),
  each in THUG2's / big / tall / both scales. Mutations (out/mut_mall.ts): 41 of 43 caught; 2 equivalent (`state-80`:
  the 0x80 bit of a 4-bit write is cleared by the next write; `reader-skip-state` 0xC -> 0xB: the skipped length never
  rounds differently - every total is 5 mod 8).

## 2. The graffiti change mask (TP-R3) and the trick object manager - batch "thugpro-guards 1"

THUG2's LogTrickObjects kept "this cluster changed hands" in a 128-byte global (0x007CE508); a trick over more trick
objects wrote past it into the trick object code's globals - THUGPRO's "Fix 256 graff tag crash" (Net::InitPatches moves
it to a 2048-byte buffer). THAW zones have hundreds of clusters.
- `src/game/obj/trick_objects.cpp`: 0x005886C0 LogTrickObjects (graffiti and team-graffiti scoring, stealing, the list
  compacted) with our own 2048-entry mask; a longer list is cut to 2048 (THUGPRO's buffer would overrun there too). The
  rest of the manager's code around it, 0x00588200-0x005889E0: CTrickObject Set / ClearGraffitiColor, the node
  destructors (0x00588590 / 0x005885B0 / 0x005885F0), CTrickObjectCluster::AddObject 0x00588630, the manager's lookups
  0x00588380 / 0x005883C0 (+ its jump 0x005889E0, source only), CountClustersHeldBy 0x00588400, the walks' callbacks
  0x00588430 / 0x00588450 / 0x005884B0, GetClusterName 0x00588480, WriteGraffitiState 0x00588520 (one byte per cluster
  index: past 255 it wraps, as both games' receivers read it - nothing on the wire changes).
- Tests: TestGraffitiLog (original vs ours: 160 random worlds - free skate, graffiti, team graffiti with team / individual
  scoring, unknown players, lists up to 200 with the bytes after THUG2's mask saved and put back; ours alone with 3000
  objects: no overrun, the first 2048 logged), TestTrickObjects (the 0x00588200-0x005889E0 functions: lookups, colours
  with and without a mesh, objects added - out of memory, show_all_trick_objects -, the graffiti state, destructors).
  Mutations: out/mut_graffiti.ts 22 of 23 (`team-sum-noskater`: reordering `SkaterOf(other) && Team(other) == team` -
  no side effects, equivalent); out/mut_trick.ts 27 of 27.
- Check by hand (offline): graffiti in a THAW zone with many clusters (any THAW level with the score of a long line:
  free skate's trick objects colour as you skate them when graffiti is the mode) - single-player graffiti isn't
  reachable offline without the network menus, so this one waits for a LAN / online session.

## 3. Received-message guards (TP-R4) - batch "thugpro-guards 2"

`src/game/net/net_guards.cpp` (new), called first by our Net::Dispatcher::DispatchMsg (0x0045A750, already ours - so no
original code changes; THUGPRO checks in the same place):
- 0x29 primary animation: not in the animation cache (0x0048CF50, THPS6_human + the animation, as THUGPRO) -> `default`.
- 0x77 / 0x78 special item made / destroyed: its details structure isn't a global structure -> dropped.
- 0x33 start info (mode +0xC, level +0x10) / 0x34 game info (mode +0): a mode not in `mode_info` (THUGPRO's netrace,
  netgold) -> `netlobby` (THUG2's LoadGameType 0x00553AD0 clears the game mode and returns false for an unknown one).
- 0x33 / 0x46 change level (+0): a level whose load script doesn't exist (THUGPRO's atlanta, HISCH, z_funpark, oil...)
  -> SkateNet::HandleServerRemovedYou as for message 'V' ("the server moved on": the error box, back to the menus).
- Shorter than what a check reads -> not checked (nothing read past its end) and passed to THUG2's handlers as it is.
  Each name logged once ("network guard (THUGPRO, TP-R4): ...").
- Message layouts from THUG2's own handlers (0x0056A450, 0x0056D810, 0x005313C0, 0x005311F0 / 0x00569560).
- Tests: TestNetGuards (ours only - nothing original to compare): every guarded message known / unknown / too short /
  null, other messages untouched, then through DispatchMsg with a stand-in dispatcher (handlers run with the change,
  not run when dropped, the leave's result returned). selftests_netC.cpp's original-vs-ours dispatcher tests stand
  the guard in (it passes everything there). Mutations (out/mut_guards.ts, with section 4's) caught.

## 4. Received appearances (TP-R5) - batch "thugpro-guards 2"

`src/game/skate/appearance_guard.cpp` (new), from our CSkaterProfile::ReadFromBuffer (0x00586DC0) right after the
appearance is read (0x00489400 into the profile's +0x4C): its callers are the player create (0x0052DB80), the join
request (0x0052FA10, host) and 0x0053A310. THUG2 builds a part by finding its desc_id in the global array named after
the slot (0x00485CF0 via 0x00489050) and never checks the answer. Per slot with a desc_id: no such list (or not a list of
structures, or empty) -> slot taken out; part not in the list -> `None` if the list has it, else
appearance_custom_skater_male's part for the slot if it's in the list, else the list's first; an unknown `body` (the
full-body pros) -> the whole appearance becomes appearance_custom_skater_male. Logged ("appearance guard (TP-R5)").
- Tests: TestAppearanceGuard (hand-built structures and lists: every fallback, wrong-type / empty lists, structure
  pointers and non-part components left alone, the body reset, no defaults); TestProfileRest checks ReadFromBuffer calls
  it once on +0x4C (and stands it in for the original-vs-ours comparison). Mutations (out/mut_guards.ts): 30 of 31
  caught (32 with section 3's); `body-continue` (break -> continue after an unknown body) equivalent.

## 5. THUGPRO's own CAS parts and boards from the user's install (TP-R6) - batch "thugpro-guards 3"

`src/game/skate/thugpro_cas.cpp` (new; not a decompilation - our own code on top of THUG2's script system):
- LoadOnce, from MainLoop (next to RefreshPositionScale) until master_editable_list exists (at most 600 frames): reads
  `<install>\data\pre\thugpro_qb.prx` (its own reader and LZSS decoder - the archive format of src/game/file/pip.cpp,
  the decoder of tools/menu_level's lzssDecode), takes `qb\game\cas_skater_m.qb`, `cas_skater_f.qb`,
  `cas_skater_shared.qb`, `cas_logos.qb`, `decks.qb` and `qb\thugpro\thugpro_boards.qb`, walks their top level as ParseQB
  does (`name = [` arrays; scripts, name tables and section markers skipped), and for every array that is also one of
  our part lists (a global array of structures with desc_id) parses THUGPRO's (Script::ParseArray, in the script heap)
  and appends the entries we haven't - by desc_id - whose every path (a string with a slash: `mesh`, a texture swap's
  `with`, nested structures and arrays too) is the game's own (File::Exist) or a loose file of THUGPRO's install. They
  get `hidden` and `no_random`. One log line says how many were added / left out.
- DataOverlay::OverlayPath's last step (after the overlay's folders): FilePath serves a requested file from the install
  when its path key (lower case, '\', extensions off: .skin.xbx / .tex.xbx / .img.xbx ... all match the part's path) is
  one of those noted - never one our own parts name, never a file the game has. Published once at the end of the load
  (the file system's threads read it). Each served file logged once.
- Not covered: THUGPRO's archives (thugpro_models.prx, thugpro_images.prx...) - entries needing those are left out and
  fall back (TP-R5). THUGPRO's animations (thugpro_anims.prx) are not loaded (TP-R4 plays them as `default`).
- `[features] thugpro_parts=0` turns it off; `=2` offers THUGPRO's parts in our own CAS menus too (no `hidden` /
  `no_random`: to try them offline - a THUG2 player without them sees such a skater with THUG2's parts);
  `thugpro_folder=` another install folder (default %LOCALAPPDATA%\THUG Pro). (Not in CLAUDE.md's list of settings
  yet: for the lead.)
- Tests: TestThugproCas (ours only: the LZSS decoder on a hand-made stream - literals, an overlapping reference, the
  window's spaces, cut-off input -, the archive reader - stored / compressed / unwanted files, names as stored, cut-off
  archives -, the .qb walk, path keys, the choice of entries; nothing served before a load), TestThugproCasInstall
  (when the user's install exists, read only: its CAS files found, decompressed to their stored sizes, skater_m_head /
  board / hat among their arrays; skipped without an install). TestMainLoop stands LoadOnce in. Mutations
  (out/mut_cas.ts): 26 of 26 caught.
- The merge itself (ParseArray, Resize, the flags) and the file serving need the game: see "For the user to try".

## Workspace runs (C:\Games\THUG2-3, offline, under its lock; 2026-10-21)

- `-Level z_ma` with `logtricks; logtricks 150`: "1007 trick objects of 162 clusters in the level logged, 162 changed
  hands" - one trick over 1007 trick objects (THUG2's own mask held 128) - then 150; no crash, the game quit cleanly.
- Start-up with the user's THUGPRO install: "THUGPRO CAS (TP-R6): 129 of THUGPRO's parts added to 11 of our lists ...
  (hidden from our menus); 1 left out; 102 of its loose files to read from there" - all of them THUGPRO-only paths
  (models\boards\board_*, models\cas_items\*, models\skater_pro\*_thug, models\skater_secret\*, models\peds\ped_eric_thug,
  models\skater_male\thugpro\head_*, textures\custom_boards\*). The front end and free skate in z_ma read none of them
  (nothing of ours names them). Found on the first run: File::Exist can't be asked about arbitrary paths (its disk
  side opens the file and a missing folder is the game's fatal file error) - the check now looks in the overlay, the
  loaded archives and the data folder itself.
- Not run: a THUGPRO part actually drawn (needs CREATE-A-SKATER with `thugpro_parts=2`, or a THUGPRO player), the guards
  and the sanitiser on real THUGPRO messages (online only), The Mall's scale (network games only).

## Checks (all three)

thug2_check on THUG2PM.exe and the stock THUG2.exe: 0 MISMATCHED, all early self-tests pass. check_code_writes: no
overlaps. scan_register_assumptions: only the known false alarms; every new function whose result is read returns one.
audit_game_function_calls: no MISMATCH. find_gaps: nothing new (3 gaps / 6 thunks elsewhere, as on main).
Build note: this worktree's verify build uses embedded debug info (-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded): the
shared vc140.pdb kept failing with C1033 while mutating (mspdbsrv at 3.7 GB).

## Switching off

`[replacements] disable=` local_net_logic.cpp, skate_net.cpp (the whole file), trick_objects.cpp. The guards are in
net_dispatcher.cpp / skater_profile.cpp (disabling those files disables the guards with them). The Mall's scale: re-run
make_menu_level.ts with `--compat none` (no IsBigLevel flag). THUGPRO's parts: `[features] thugpro_parts=0`.

## For the user to try by hand

1. Re-run `node tools/menu_level/make_menu_level.ts` for the game folder (The Mall's level select entry gets IsBigLevel).
2. Offline: free skate in The Mall, a THAW zone with many trick objects - nothing should change (the scale is only on in
   network games; the log shows no "THUGPRO big level" line).
3. LAN (two copies, `allow_multiple_instances=1`): host and join in The Mall; the log of both shows "THUGPRO big level
   (TP-R2): network positions x/z in whole units"; the other skater is drawn where they are, also far out in the mall.
4. LAN: graffiti in a THAW zone, tag many clusters with one long combo (no crash, colours on both).
5. THUGPRO's parts (TP-R6), offline: start the game with THUGPRO installed; the log has "THUGPRO CAS (TP-R6): N of
   THUGPRO's parts added ...". With `[features] thugpro_parts=2`, CREATE-A-SKATER offers them (e.g. the hats / heads /
   bodies THUG2 hasn't, THUGPRO's boards): pick some, skate - "read from THUGPRO's install" lines in the log, the parts
   drawn right. Set it back to 1 (the default) afterwards.
6. Online with THUGPRO players (by hand only): join a THUGPRO free-skate server in a level both have - THUGPRO's
   handshake still refuses us (docs/thugpro-online.md item 2), so the guards can only be seen once that is solved; look
   for "network guard" / "appearance guard" lines in the log.
