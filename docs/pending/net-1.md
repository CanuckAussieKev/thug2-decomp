# Online & network game modes, first part (0x533F20-0x53A910) - NET1 agent

Branch worktree-agent-a232debf9a191af6f, workspace C:\Games\THUG2-1. Source in src/game/net/gamenet_members.cpp,
gamenet_settings.cpp (new); declarations added to src/game/net/gamenet_manager.h (additive). Tests in
src/hook/selftests_net_*.cpp on the GameNet test kit (src/hook/gamenet_testkit.h). Shared files touched: CMakeLists.txt
(new files listed), gamenet_manager.h (members added). Nothing here was run online (never): what needs a live
connection is listed per batch.
Switch off: `[replacements] disable=gamenet_members.cpp` (and gamenet_settings.cpp, ...).

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
