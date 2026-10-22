# Online & network game modes, first part (0x533F20-0x53A910) - NET1 agent

Branch worktree-agent-a232debf9a191af6f, workspace C:\Games\THUG2-1. All 124 functions of the range have source (116
new here, 8 were done on main). Source in src/game/net/gamenet_members.cpp, gamenet_settings.cpp, gamenet_commands.cpp
+ .h, gamenet_game_commands.cpp, gamenet_lists.cpp + .h, gamenet_players.cpp (all new); declarations added to
src/game/net/gamenet_manager.h (additive); one member added to src/game/sys/viewer.h / viewer.cpp (the input handler's
scalar deleting destructor, 0x00536670); a comment in net_manager.h (dialup_authentication's name); one call qualified in
gamenet_join.cpp (see NET1-D). Tests in src/hook/selftests_net_*.cpp (members, settings, commands, lists, players,
game_commands, counts) on the GameNet test kit (src/hook/gamenet_testkit.h). Shared files touched: CMakeLists.txt (new
files listed). Nothing here was run online (never): what needs a live connection is listed per batch.
Switch off: `[replacements] disable=gamenet_members.cpp,gamenet_settings.cpp,gamenet_commands.cpp,gamenet_game_commands.cpp,gamenet_lists.cpp,gamenet_players.cpp`
(any one of them alone; 0x00536670 is in viewer.cpp - disabling that file also switches the model viewer's code off).

- [x] Batch NET1-A (NOT run in the game yet; the manager's network mode flags, team flags, player / observer count
  preferences, password, collision and team-score rules, the server's end, the custom tag texture, the choice lists,
  18 functions): src/game/net/gamenet_members.cpp (new).
  - 0x00533F20 SetNetworkMode (flags bits 2-3: 1 LAN bit 3, 2 internet bit 2, else neither - the CFunc
    SetNetworkMode's), 0x00533F50 SetServerMode (bit 0), 0x00533F70 InLanMode / 0x00533F80 InInternetMode /
    0x00533F90 OnServer / 0x00533FA0 OnClient (bits 3 / 2 / 0 / 1), 0x00534F40 GetLevel (m_level; 4 bytes: source
    only).
  - 0x00533FB0 CreateTeamFlags (Kill_Team_Flags, then create_team_flags {num_teams} when there are teams).
  - 0x00534090 SetMaxPlayersPreference / 0x005341B0 SetMaxObserversPreference: num_players / num_observers get
    {ui_string "<n> <netmenu_str_players>" ("no observers" text used as the format, "1 <observer>", "<n> <observers>"),
    checksum crc("num_<n>")}; the parameter structure is never freed (the original's leak, kept).
  - 0x00534310 GetPassword (the password preference's ui_string; on a miss the slot it was read into: the manager's
    address, as the original).
  - 0x00534340 PlayerCollisionEnabled: false in a finished game / free skate / horse unless in the lobby; true in split
    screen, with player_collision = boolean_true, in netslap / slap. 0x005343E0 ShowTeamScores (not the lobby, a team
    game, score_display = score_teams).
  - 0x00534CF0 DestroyServer (a remote internet server's lobby reporting 0x0053F7E0 and GameSpy 0x005FBCD0 stopped
    first; Net::Manager::DestroyApp; the tasks at +0xCC and +0xE8 removed; flags bit 0 cleared even with no server),
    0x00534D50 DestroyMatchClient.
  - 0x00534D70 SendCustomTagTexture: a client that isn't on a modem with a local skater whose tagging component has a
    custom tag texture: over a connection of bandwidth type 0, the texture's palette (0x98, "CAG Palette") and pixels
    (0x97, "CAG Texture") streamed to the server (group 10), then 0x9A (4 bytes, sequenced, group 10: the CAS logos it
    has - hat_logo 1, front_logo 2, back_logo 4, deck_graphic 8). True when the skater has a custom tag at all.
  - 0x00534F50 GetChoiceName / 0x00534FC0 GetChoiceInteger (thiscall, this unused): in the global array `list`, the
    `name` / integer `field` of the last entry whose checksum is `choice`. The checksum is read into an argument's slot:
    an entry without one compares the slot's last value (at first the list's name / the choice itself), kept.
  - Test (selftests_net_members.cpp TestNetMembers): every function, original vs ours, on the GameNet kit's arena with
    every call stood in for by recorders (game mode, preferences, getters, scripts, the net library, the skater, the
    texture's virtual slots): every mode on four start flag words, flags 0-15, team counts 0/2/4/-1, counts incl. 0, 1
    and negative, the password found / missing, 48 collision combinations and 8 team-score ones, the server's four
    kinds, each tag-texture refusal and 7 logo sets, choice lists with repeats, one element, none, and entries without a
    checksum. 46 deliberate bugs caught, 1 (bandwidth type == 0 vs != 1) after adding a bandwidth type 2 case.
  - Untested without a connection: what the server does with the tag texture (the stream itself is the net library's,
    already rewritten).
- [x] Batch NET1-B (NOT run in the game yet; the network preferences handed to the net library, the set-up game's
  preferences, connecting / disconnecting, the starting points, the metrics / scores toggles, the score rule, 11
  functions): src/game/net/gamenet_settings.cpp (new), gamenet_members.cpp.
  - 0x00535040 UseNetworkPreferences (the CFunc UsePreferences'): the master server names at +0x9954 (ten of 16 bytes,
    cleared first): master_server1 / 2 unless "0.0.0.0" (the original's REPE CMPSB over 8 bytes = a string compare),
    then default_master_servers' names - or only those with use_default_master_servers (no bounds check: more than 10
    run on, kept). broadband_type ip_dhcp = DHCP. Four global strings (names not known: 0x5F6FDED5 address, 0x3E683519
    gateway, 0x676BC0CB subnet mask, 0x93CF2232 DNS server) override the preferences (set into them; a non-empty address
    means no DHCP; gateway / subnet only with an address; an empty address counts as none). Then Net::Manager:
    SetDHCP, host / domain name (DHCP) or local IP / gateway / subnet mask; DNS: three empty with auto_dns and no address
    of our own, the two defaults 205.147.0.100 / .102 with an address but no DNS override, else dns_server /
    dns_server2; the three dial-up numbers, user name, password; the device (two more globals, 0x504ACA1F /
    0xF3780073, force the PCMCIA settings): broadband PC 3/2, USB 1/2 (the spoof scripts with an address of our own),
    USB PPPoE 1/3, USB modem 2/1 and Sony modem 4/1 (both: at most 3 players, no observers), PC PPPoE 3/3, else 0/0;
    dialup_authentication -> SetFlag2E6 (the name now known: net_manager.h's comment updated). The original's stack
    slots reused for missing values are kept (one `text`, one `check` - auto_dns missing takes use_default's).
  - 0x00535AD0 GetTeamGraffitiMode, 0x00535B20 GetGoalsPreference (GetChecksum by the name "checksum"),
    0x00535B70 GetGameTypePreference: the network preferences in a network game, else the skate module's (+0x58);
    the manager's address back on a miss (the original's pushed ECX slot).
  - 0x00535C10 ConnectToInternet(online script, fail script) (+0x1C4 / +0x1C8; state +0x1A8 = -1; a failing modem /
    PPPoE connection adds the online task +0xD8), 0x00535CB0 DisconnectFromInternet(script) (0x005453E0 with 2: the
    lobby's state, NET2's range).
  - 0x005359F0 ChooseStartPoints (Mth::Rnd: offset 0-255; 8 in a network game, else the mode's max_players - 2 with
    offset 0 when the park editor's +0x40 isn't `off` and the mode isn't horse; each slot drawn again until unique;
    no bounds check; +0x68 = Rnd(10)), 0x00535BC0 ToggleNetMetrics (the byte at +0x45 XOR 1, then tested: a 0xFF byte
    becomes 0xFE - still on), 0x00535BF0 ToggleScores (the mode's accumulate_score asked and ignored, +0x46 XOR 1),
    0x00535D00 KeepsScore, 0x00535D90 SetProSetFlags (sign-extended).
  - Test (selftests_net_settings.cpp TestNetSettings): original vs ours with every call stood in for (preferences
    answered per name, the net library's setters recorded, the global strings scripted): 32 preference sets (master
    servers kept / skipped / defaults 0-4, DHCP, auto_dns, a missing auto_dns, each override alone and together, empty
    and null ones, all 7 devices with and without an address of our own, the modem's player limit, both forcing
    globals), the server names' bytes after each; the graffiti / goals / game type over net x team x found; connecting
    online / succeeding / failing on types 0-4, disconnecting with and without a script; start points for a network
    game and 1/3/4/0/9 players x editor off / horse / neither (a stand-in LCG for Rnd); toggles from 0 and 0xFF; the
    score rule over 6 modes x 8 flag sets. 54 deliberate bugs caught (3 after recording the server names after every
    call - the arena alone only kept the last case's), 1 equivalent (comparing only earlier slots in ChooseStartPoints:
    later ones are still -1).
  - Untested without a connection / hardware: the net library's use of the settings (DHCP, dial-up, PPPoE are console
    leftovers on the PC).
- [x] Batch NET1-C (NOT run in the game yet; the manager's script commands: capture the flag's task, the game's state,
  the join mode, teams, the lobby's peer, the message of the day, the first-come-first-served requests, the pro set
  flags, 24 functions): src/game/net/gamenet_commands.cpp + .h (new).
  - 0x00535DA0 StartCTFGame / 0x00535DC0 EndCTFGame (the task at +0xF8 into the logic tasks / removed),
    0x00535DE0 DisplayFlagBaseWarning (display_flag_base_warning at most every 3000 ms - unsigned difference, 0x007CCE24),
    0x00535E20 NetworkGamePending (+0x1B4), 0x00535E30 MarkGameOver, 0x00535E40 SetJoinMode (an unnamed integer:
    0 plays, anything else watches; SetJoinMode clears 0x007CCA88), 0x00535E90 GetNumTeams (the mode's 0x00553130 into
    the script's num_teams), 0x00535EB0 ConnectedToPeer (the lobby's peer 0x0053F880, GameSpy's peerIsConnected
    0x005FC730), 0x00535EE0 InGroupRoom (the lobby's byte +0x134, as it is), 0x00535EF0 IsAutoServing (network game,
    on the server, host mode 1), 0x00535F20 ChangeLevelPending (the task at +0xEC in a list), 0x00535F30
    CancelMotdDownload (+0xFE08), 0x00535F40 DownloadMotd (lobby state 6 - 0x005453E0, NET2's range),
    0x00535F50 ServicesAreAvailable (+0xFE0A = 1: true, services state 0 and lobby state 1 first unless the MOTD
    (+0xFE09) came; else services state 3, lobby state 1, false), 0x00535FA0 AlreadyGotMotd, 0x00535FB0
    JoinServerComplete (m_readyToPlay), 0x00535FC0 JustStartedNetGame (under 5000 ms since 0x007CCDF4, unsigned),
    0x00536240 ReattemptJoinServer, 0x00536460 HasSignedDisclaimer.
  - The requests to the server (sequenced, group 8): 0x00535FE0 FCFSRequestStartGame (message 0x60, 52 bytes - the
    game type from the set-up game's preferences, then time_limit, skill_level, fireball_difficulty, player_collision,
    friendly_fire, +0x18 never written, target_score, stop_at_zero, score_reset, ctf_game_type, goals,
    team_graffiti_mode: GameNet::StartGameRequest; then the game is pending), 0x00536170 FCFSRequestChangeLevel
    (level=; for the created park the park map 0x6C - 15000 bytes - and the rail editor's rails 0x88 - 4402 bytes - are
    streamed first, the rail editor looked up twice as the original does; then 0x62 with the level), 0x00536260
    FCFSRequestToggleProSet (0x63: param_id, then bit's byte), 0x005362F0 FCFSRequestToggleGoalSelection (0x64:
    name=; the original's uninitialised local goes when there's none), 0x00536360 ResetProSetFlags (7 times: SetFlag /
    UnSetFlag {flag = FLAG_PROSET1_GEO_ON + n} by m_proSetFlags' bit n).
  - Test (selftests_net_commands.cpp TestNetCommands): every command original vs ours with every call stood in for:
    the flag base warning across the 3-second edge (exactly 3000: not yet), the join modes none/0/1/7/-1, the lobby's
    peer missing / unconnected / connected, the group room byte 0/1/2 (AL compared as it is), auto serving over
    net x 4 flag bytes x host modes 0-2, services 3 states x MOTD, just started at 3000/4000 ms and a future start, the
    start request in and out of a network game (its +0x18 not compared), the level request without a level, a level
    and the created park, the pro set toggle with bit 0x1FF (its byte), the goal toggle, the pro set flags 0, 0x7F,
    0x55, 0xFFFFFF80, 0x2A. 41 deliberate bugs caught (1 after recording the game-over byte).
  - Untested without a connection: the server's handling of the requests (0x005301A0, already rewritten), the lobby /
    GameSpy peer itself.
- [x] Batch NET1-D (NOT run in the game yet; the manager's list types, its destructor, the player / joining-player
  records, the queued scripts, the server search, GameSpy's think, the panel message, 38 functions; committed together
  with NET1-E and NET1-F):
  src/game/net/gamenet_lists.cpp + .h, gamenet_players.cpp (new), gamenet_members.cpp, gamenet_settings.cpp,
  gamenet_commands.cpp (FoundServers), src/game/sys/viewer.cpp + .h (the input handler's scalar deleting destructor,
  additive). One fix outside: gamenet_join.cpp's SpawnClient called its helper SetReadyToPlay(this, false), which the
  new member of the same name hid - now qualified (GameNet::SetReadyToPlay, still through 0x00537140).
  - The list types (each node type's vtable holds only its scalar deleting destructor; heads have their own):
    Lst::Node<PlayerInfo> 0x0064C9E8 (body 0x005366A0, 0x00536760; head 0x0064C9F0: 0x00536CE0),
    Lst::Node<NewPlayerInfo> 0x0064C9E4 (0x005366E0, 0x005367E0; head 0x0064C9F4: 0x00536D10),
    Lst::Node<ServerInfo> 0x0064C9EC (0x005366C0, 0x005367A0; head 0x0064C9FC: 0x00536D40 - a ServerInfo, vtable
    0x0064CDFC, is itself a node of the server lists +0x158 / +0x16C), Lst::Node<QueuedScript> 0x0064C8EC (0x00536700,
    0x00536720) and QueuedScript 0x0064C9F8 (0x005371F0: a 0x20-byte node whose data is itself - node, object id,
    script). 0x00536670: Inp::Handler<T>'s / Inp::BaseHandler's scalar deleting destructor (vtable slot 2 of
    0x0064D7D8 and 0x00646090) - Mdl::ViewerInputHandler::Destroy.
  - 0x00536D70 Manager::DestroyBody: GameSpy shut down (0x005F9900 with 1003, 0x005F9BE0, its reporting 0x005FBB20
    if up - 0x007CCE1C cleared), the lobby / +0x14 / the presence / the stats (their destructors 0x00540D50,
    0x0054B470, 0x005488B0, 0x0054D610, then freed), ten tasks deleted through vtable slot 2 in the original's order
    (+0xD0, +0xCC, +0xC0, +0xC4, +0xD4, +0xE0 ... +0xF0), the players' nodes unlinked (0x00588B70), the five heads'
    destructors, the taunt and network preferences destroyed.
  - Players (+0x11C): 0x005372C0 FirstPlayerInfo / 0x00537010 NextPlayerInfo (search {vtable, node}; observers skipped
    unless asked - by recursion, as the original), 0x00537320 GetLocalPlayer / 0x005373D0 GetServerPlayer /
    0x00537480 GetKingOfTheHill (flags bit 0 / bit 9 / 0x00543490), 0x00537530 CountOtherPlayers (non-observers
    without flags 0x21; bit 5's meaning isn't known), 0x00537620 ResetPlayers (flags 0x20100 and +0x1C/+0x20/+0x34
    cleared, the SkaterScore component's +0x18 reset by 0x00564A10 - the component isn't checked for null),
    0x00537140 SetReadyToPlay (when it changes: the skate module's skaters - counted by its list +0x28, each by
    0x005665C0 - get virtual slot 12 (ready) or 11), 0x00537F40 ToggleProSet (m_proSetFlags ^= 1 << (bit & 31);
    message 0x70 {param, bit} to every remote player through the server).
  - Joining players (+0x130): 0x00537070 FirstNewPlayerInfo, 0x005370A0 GetNewPlayerInfoByObjectId (+0x10 as a
    signed byte against the 16-bit id: a byte 0x85 matches 0xFF85, not 0x85), 0x00536820 RemoveAllNewPlayers /
    0x00536B10 RemoveNewPlayer (the connection terminated on the server - or on client 0 when there's no server and it
    has one; node and record 0x00543560 deleted).
  - 0x005376F0 CreateNetPanelMessage (broadcast: message 0x3F, 44 bytes - CRC of the properties' name, text, time,
    two 16-byte strings copied without bounds, as the original - to every player, observers and the local one too,
    sequenced when important; locally: create_net_panel_message {text with its "%s0" replaced by string 1, id =
    net_panel_msg, msg_time}). The `object` argument is unused.
  - Queued scripts (+0x144): 0x00537A10 QueueScript (unless one of the same script is queued; allocated in the network
    heap), 0x00537B30 ClearQueuedScripts, 0x00537D90 LaunchQueuedScripts (bottom-up heap; the main volume saved and
    set to 0; PreRunQueuedScripts; each spawned at its node, its object (GetObjectByID) set as the script's, run until
    Update finishes, killed and dropped; PostRunQueuedScripts; the volume restored; the skate module's 0x00569580 /
    0x00569590 around it).
  - 0x00536490 FindServerAt (the server lists cleared - 0x00545370 / 0x005462F0 / 0x00545300, outside this range - the
    server-list client made if needed, message 0x11 with the time to ip:5153, 0x007CCA84 = ip or 0),
    0x00536640 ServiceGameSpy (from the main loop), 0x00536A40 GetGameTypeName (mode_info; the game type re-read for
    each entry; a missing name gives the type's checksum back - the slot it shares), 0x00536B90 SetLevel (m_level;
    the GameSpy peer's state-changed call when hosting; the reporting's call after it is dead code: its flag
    0x007CCE1D is cleared just before), 0x00536C10 CancelConnectToInternet (by the modem state: 0/1, 2, 5, 9; the jump
    table read).
  - 0x00537100 CFunc FoundServers (counts the server list's nodes).
  - Tests (selftests_net_lists.cpp TestNetLists, selftests_net_players.cpp TestNetPlayers): original vs ours on real
    lists in the arena: each list type's destructors on middle / last / head nodes with flags 0, 1, 2, 3; the
    destructor with every part / task present and with none; joining players removed (all; one: absent, first, last,
    without a connection; server and client); all 11 modem states; server searches with and without a client and
    address; the GameSpy flags' 8 combinations; SetLevel's four peer states; game type names in and out of a network
    game; player walks with observers first / none / all observers / empty; the id look-ups incl. a negative byte;
    ready toggles (repeats and a missing skater); the panel message broadcast (strings, none, important or not) and
    local (no slot, two slots, only a slot, "%s" alone); pro set toggles incl. bit 33; scripts queued (a repeat),
    launched (an object for one, Update twice / once / never), cleared; FoundServers with 0 / 1 / 3 servers.
    71 deliberate bugs caught (3 after strengthening: heads destroyed with flags 2, list words compared arena-relative
    instead of hashed, a one-server list); 3 equivalent (RemoveAllNewPlayers testing a null first node before the head,
    the two server heads' destruction order, GetGameTypeName's slot reuse - only seen with an uninitialised first read).
  - Untested without a connection: the server lists themselves (outside this range), GameSpy's think / peer calls, the
    late joiners' queued scripts in a real level, the panel message's display.
- [x] Batch NET1-E (NOT run in the game yet; the manager's script commands, second part: capture the flag's flags,
  the crown, starting and ending network games, teams, the host, readiness, the player list menu and its options
  dialog, 15 functions): src/game/net/gamenet_game_commands.cpp (new), declarations in gamenet_commands.h.
  (Committed together with NET1-D and NET1-F.)
  - 0x00538050 TeamFlagTaken team= (a non-observer carries it - 0x005433D0; team uninitialised in the original
    without the parameter), 0x00538170 SpawnCrown (a CCrownObject made into +0x6C even if allocation failed; the nodes
    of Type 0xAF86421B - its name isn't in the scripts - at most 10 counted without a bounds check; +0x68 modulo their
    count (none divides by zero); GetPosition, InitFromNode, the king of the hill wears it), 0x00538280
    StartNetworkGame (the lobby peer told; GameSpy's "game on" reported once - its byte 0x007CCE1E compared with 1;
    nothing more while the game start (+0xE8) or level change (+0xEC) task is listed; pending; start time now / +3000 /
    +8000 for goal attack; the start task added; in a network game the panel message net_message_auto_starting_game
    (host mode 1) or net_message_starting_game with the game type's name, netstatusprops, important, 3000 ms;
    0x7A (no data) to every player in a team game; for goal attack 0x6D: a byte 1, each selected goal's id - each told
    first through its virtual slot 29 - and 0, length truncated to 16 bits), 0x00538620 SendGameOverToObservers
    (0x5A to the observers, on the server), 0x00538720 EndNetworkGame (the "game on" report undone - the byte tested for
    non-zero; server: do_backend_retry and 0x6A to the remote players; client: 0x65 to the server).
  - Teams: 0x005388C0 GetMyTeam (team into the script's params), 0x005388F0 SetNumTeams <n> (client: 0x66 {n};
    server: the mode's count 0x005535A0 - a team game newly made has the local skater PrepareSkaterForMove'd and moved
    by 0x00566370 -; with a server app 0x69 {n} to the remote players, then every non-observer on a team >= n moved to
    team 0 and announced (0x68 {skater id, 0})), 0x00538C90 JoinTeam (blue 1 / red 0 / green 2 / else 3; 0x67 {skater
    id, team} at most every 150 ms - unsigned; false without a local player with a skater).
  - 0x00538D70 IsHost (server: host mode 0; client: the local player has flags bit 9), 0x00538DC0 AllPlayersAreReady
    (every player's connection's +0x60 bit 0, on the server; true elsewhere).
  - 0x00538E70 CreatePlayerOptions index= name= (the index'th player, observers too; its name compared case aside
    through the CRT's _stricmp; flags 0x400; create_player_options_dialog {profile, nick, allow_add_homie /
    allow_remove_homie (on the internet, the player has a profile, logged in to GameSpy - the presence's 0x005480B0 /
    0x00549CB0), name}), 0x00539470 RemovePlayer (the same command: a 5-byte jump), 0x00539480 CancelRemovePlayer
    (0x400 cleared, non-observers), 0x00539080 FillPlayerListMenu (front end heap; each non-local player:
    theme_menu_add_item {text, centered, id 123456 + its place, pad_choose_script choose_selected_player, rgba
    (128,128,128,128; a buddy 128,128,0,128; a logged-in non-buddy with a profile 0,128,0,128), unfocus_params {rgba},
    pad_choose_params {index, name}}), 0x00539570 ToggleProSet bit= param_id= (Manager::ToggleProSet).
  - Test (selftests_net_game_commands.cpp TestNetGameCommands): original vs ours on real player lists with every call
    stood in for (the CRT's _stricmp too; array elements from the arena): starting with each task busy, offline, four
    network set-ups (goal attack, host modes, team games, no goals), ending on the server (reported or not) and a client,
    game over on a client / server; the flag per team; my team with and without a local player; the team count from a
    client and in five server set-ups; joins for each colour, too soon, exactly 150 ms, no skater, no local player;
    the host offline / modes 0-2 / client cases; readiness; options by index and name (case, a first-letter difference,
    beyond the list, logged in or not, a profile or not), the flags after each; the menu logged in or not with the heaps
    told apart; cancel; the crown at two spawn points with and without a king. 56 deliberate bugs caught (6 after
    strengthening: distinct teams, exactly 150 ms, host mode 2, a name differing only in its first letter, the flags
    recorded after each options call, the two heaps told apart).
  - Untested without a connection: the server's side of each request, the menus and the crown in a real level.
- [x] Batch NET1-F (NOT run in the game yet; player counts, the clients' end, the tag texture / face exchange, the
  joining player's record, the observer camera, the carried flags, the manager's release, 11 functions):
  src/game/net/gamenet_players.cpp. (Committed together with NET1-D and NET1-E.)
  - 0x00539880 CountTeamPlayers(team), 0x00539B00 CountPlayers (quirk kept: the joining players are compared with the
    byte after the noted skater ids - zero - so only those with object id 0 are left out), 0x00539C70 CountObservers.
  - 0x005399F0 DisconnectClients (both clients destroyed - client 0's add-players task removed unless it's local -, the
    observer input handler deleted (vtable slot 2), 0x007CCDF8 cleared, GameSpy's reporting shut down, 0x005F9900(1003),
    the tasks +0xD4, +0xC8, +0xEC removed, the queued scripts dropped, not ready to play: the skaters' slot 11).
  - 0x00539DA0 SendTagTexturesTo(player) (not on a modem, not to the local player: for each other non-observer with a
    custom tag texture and tag, over a connection that isn't narrowband (1): 0x96 its skater id, the palette 0x98 and
    pixels 0x97 streamed, 0x9A the logos - read from the receiving player's profile, a quirk kept - all group 10),
    0x0053A040 PrepareLocalFace (not on a modem; the profile's face texture +0x54 with a texture (0x00487510): server -
    the local player's +0x1E8 = a byte 0 and 0x4498 bytes of it (net-misc heap); broadband client - 0x7E "Face Data",
    0x4498 bytes, group 9, streamed to the server and freed), 0x0053A1B0 SendFacesTo(player) (each other non-observer's
    face - the local one's prepared if it has none - streamed through the server, 0x7E, 0x4499 bytes, in place).
  - 0x0053A310 AddNewPlayer(info, timeout) (unless its object id is listed: a NewPlayerInfo (0x44, constructor
    0x005434D0) copied - name, +0x10, +0x14, +0x20, +0x28 ... +0x40; +0x24 the server's frame count for flag 0x10 on the
    server; the appearance through a 0x1000-byte buffer (WriteToBuffer / ReadFromBuffer) for those who will play;
    +0x18 = the app's frame count + timeout; on the server a non-local joiner announced - net_message_joining
    locally, or net_message_observing / net_message_join_pending (flag 8) to everyone, 2000 ms; its node onto the end
    of +0x130; all in the network heap).
  - 0x0053A580 NextCamPlayer (the next non-observer after m_camPlayer that 0x00543330 / 0x00543440 don't rule out;
    past the end m_camPlayer is cleared and the search starts again; from the start without a current one).
  - 0x00539610 CarriedFlagsTask (the manager's flag task's code; netctf only: a carrier's flag object TRG_CTF_Red /
    Blue / Green / Yellow by its team - none stops the task for the frame - gets the head bone's matrix
    (Skeleton's 0x004A36E0), times CreateRotateXMatrix(90) (D3DXMatrixMultiply, the library's), moved 12 along its
    own y (0x0048A260), its translation taken out (identity row), times the skater's matrix; its position the bone
    offset transformed by the skater's matrix plus the skater's position - float adds, exact as the x87's).
  - 0x005395C0 ReleaseManager (the reference count 0x007CCE18 down, or the manager destroyed and freed, both globals
    cleared).
  - Test (selftests_net_counts.cpp TestNetCounts): original vs ours on real lists with every call stood in for (the
    matrix functions and D3DX's multiply by recorders that write values depending on their inputs): counts over
    players with each flag combination and joining players of each kind (an id 0 one), host mode 0 / other, empty;
    the clients' end with both / one / no clients, a local client 0, the handler, the reporting flag, ready or not;
    tag textures to a broadband / narrowband (types 1 and 2) / local target and on a modem; faces with and without the
    local one's; the local face with no face, no texture, server, broadband / narrowband (1, 2) client, modem; joining
    players of six kinds on the server and a client plus a duplicate (the heap's records and nodes compared); the
    camera from none / after a player / wrapping / after an observer / nobody watchable; the flags task off CTF, with
    carriers, with an observer carrier and a missing flag object; releases at counts 3, 2, 1, 0. 62 deliberate bugs
    caught (17 after strengthening: a broadband target, the client's connection, bandwidth type 2 cases, an id-0 joiner,
    0x007CCDF8 recorded, PrepareLocalFace tested on its own instead of stood in, a faceless remote player, input-
    dependent matrix stand-ins, count 2).
  - Untested without a connection: the real tag textures / faces on the wire, the flag on a real skater's head (only
    the matrix calls' order and arguments are compared), the camera in a real game.
- Game runs (NET1, C:\Games\THUG2-1 under the lock, the branch's build after merging main at dc751f8; stock THUG2.exe):
  -level BO with this work's self-tests (late / engine-started: none of these names) - BO loaded, skater spawned,
  5112 of 5112 replacements installed, no exception / run-time check / debug-CRT report. -level NJ with -FullTests (all
  724 early tests in the game, then 17 late and 3 engine-started): NJ loaded, skater spawned; every NET1 test passed;
  one failure, NET4's TestNetStatsSession ("stats session ... reporting a game", entries 61853... differing) - it
  also fails alone in the game and runs before any hook is installed, so it isn't this work's (it passes offline in
  thug2_check); left for NET4 / the lead. Nothing online was opened.
- What to try online by hand (the user, once merged): host and join a LAN / internet game (the join, team counts,
  SetNumTeams, JoinTeam, the player list menu and its options dialog - kick / homies -, start and end games, capture
  the flag - the flag on the carrier's head - and king of the hill - the crown), observe a game (the observer camera,
  game over to observers), a late joiner (queued scripts, faces and custom tag textures), the network options screen
  (UsePreferences: master servers, DNS, the device / connection type), connect / disconnect / cancel connecting, the
  message of the day. Switch off with the `disable=` list at the top.
