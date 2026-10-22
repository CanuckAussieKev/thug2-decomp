# Online & network game modes, second part (0x0053A910-0x00541870) - NET2 agent

Branch worktree-agent-a25cc356b174d99a0, workspace C:\Games\THUG2-2. Source in src/game/net/gamenet_players.cpp,
gamenet_game_cfuncs.cpp (+ .h); tests in src/hook/selftests_net_*.cpp (kit: src/hook/net2_testkit.h on
gamenet_testkit.h - the manager's real player lists in the arena, hand-built script structures, recording vtables).
Shared files touched (small, additive): CMakeLists.txt (new files listed), src/game/net/gamenet_manager.h (member
declarations added before "Fields"), src/game/net/gamenet_join.cpp (its NextPlayerObjectId helper replaced by the
member).
Switch off: `[replacements] disable=gamenet_players.cpp` (and gamenet_game_cfuncs.cpp).

- [x] Batch NET2-1 (NOT run in the game yet; the players, the observer's camera, the host mode and the network game's
  script commands, 20 functions): src/game/net/gamenet_players.cpp, gamenet_game_cfuncs.cpp (+ .h) (new).
  - GameNet::Manager: 0x0053A910 ObservePlayer (the camera object 0x967C138C's SkaterCamera / WalkCamera: someone
    else's skater - skater camera on, walk camera suspended; the local skater - both pointed at it, the walk camera
    while the physics control's state says walking (+0x118 / +0x1C), else the skater camera and the walk camera
    reset; none or no skater - both cleared; m_camPlayer), 0x0053AA70 SendObserveRequest (message 0x4A unless the
    local player observes; no null check on the local player, as the original), 0x0053AAC0
    AssignFirstComeFirstServed (the first remote non-observer with flag 0x20: flag 0x200, message 0x5F with the
    "Checksum" of network preference 0x043EE978), 0x0053AC40 NextPlayerObjectId (lowest id 0-15 not used by a playing
    player's skater nor a joining player - a joining observer counts only while pending; 16), 0x0053AE10
    PlayerByConnection (observers too), 0x0053AEA0 PlayerByObjectId (16-bit id, observers and skater-less players
    skipped), 0x0053C320 SetHostMode (1 auto-serve: observe request, time 0x007CCDEC, the task +0xF4 started; 2 first
    come first served: observe request + AssignFirstComeFirstServed).
  - Script commands: 0x0053B450 PlayerTookFlag (message 0x71 to every remote player incl. observers, 0x00543F00 for
    the local one), 0x0053B5A0 PlayerCapturedFlag (skipped with flag 0x8000; 0x72; the local machine bumps the
    capturer's score counter 0x00561B40/0x00561A80 and calls 0x00543A90), 0x0053B710 PlayerRetrievedFlag - QUIRK KEPT:
    sent as a capture (0x72 / 0x00543A90, not 0x73 / 0x00544110), 0x0053B860 PlayerHasFlag (true for an unknown
    player), 0x0053B8A0 GetNumPlayersOnTeam, 0x0053B8F0 GetPlayersTeam (objId), 0x0053B940 ExitSurveyorMode,
    0x0053D400 SetHostMode, 0x0053D440 EnteredNetworkGame (qr2 set up when 0x007CCA82; tasks +0xD4, +0xC8; flags
    0x007CCDF8, +0x46), 0x0053D490 KickPlayer / 0x0053D5E0 BanPlayer (the player with flag 0x400: the server drops it
    (0x0053CB70 reason 4 / 3) and removes a non-observer's skater; a client sends 0x61 {0 kick / 1 ban, index in the
    list, name} - ban inside the network heap), 0x0053D760 CancelJoinServer, 0x0053D780 EnterSurveyorMode (the
    surveyor task 0x0053BED0 and an input handler 0x00536890 on controller 0, flag 0x10000).
  - Test (selftests_net_players.cpp TestNetPlayers): 8 cases, original vs ours on the same arena with real player /
    joining-player lists (the originals' inlined list walks run on them); every branch above, the camera's six
    situations, host mode sequences, kick / ban on server and client with none / two different marked players.
    44 deliberate bugs: 43 caught (5 after strengthening: an observer with 0x20, a pending joining observer, a missing
    "team", the 0x007CCDF8 order around task adds, the input device); 1 equivalent (GetInteger's assert flag - asserts
    are compiled out).
- [x] Batch NET2-2 (NOT run in the game yet; the game's course, the manager's tasks and the players' lives,
  20 functions): src/game/net/gamenet_flow.cpp (new), gamenet_players.cpp, gamenet_observer.h (new: the observer's
  controls, inlined by EnterSurveyorMode and NewPlayer), gamenet_game_cfuncs.cpp (+ DropPendingPlayers).
  - Course: 0x0053AF90 ResetToLobby (server: the level preference; netlobby with the teams kept; crown and flags
    gone; +0x1B0 the lobby time), 0x0053B0D0 StartNetworkGame (message 0x34 StartGameInfo, 0x48 bytes: time limit
    from horse_time_limit / time_limit "time" - 0 for netgoalattack, king, netking, (net)scorechallenge,
    (net)firefight and an untimed netctf; target_score, fireball level, stop-at-zero = IsUntimedGame, score_reset,
    a game id 1-255 other than the last one (Mth::Rnd), the teams, "goals", 0x00535AD0, the crown's spawn point, the
    start info words; sent to every player - split screen: the first only; +0x10/+0x17/+0x25-27 never written, the
    test masks them), 0x0053C6E0 CancelJoin (not in states 8-10; message 0x0B, SendData, the apps down, presence back
    to homie_status_chatting with the lobby's name).
  - Tasks: 0x0053BED0 ObserverTask, 0x0053BF00 AutoServeTask (2 s after SetHostMode, netlobby, >= 2 players, not
    waiting, > 5 s in the lobby: LoadPendingPlayers + StartNetworkGame), 0x0053BF90 JoinStateTask (1
    CreateConnectingDialog -> 0; 2 CreateJoiningDialog unless the client is local -> 0; 3 CreateTryingPasswordDialog
    -> 4; 6 the local joiner observing: its skater removed, camera off; entered_network_game -> 7; 8 dialog_box_exit,
    both tasks off; 9 CreateJoinRefusedDialog reason=; 10 the task off), 0x0053C1F0 StartGameTask (players loading:
    the server's CreateWaitForPlayersDialog when no current_menu_anchor, a client's net_message_game_will_start, once;
    all ready (0x00538DC0) and the time 0x007CCDF4 reached: StartNetworkGame, the task off).
  - Players: 0x0053C770 NewPlayer (a local observer gets hide_panel_stuff, the camera on the first player and the
    observer's controls; a local player clears m_camPlayer - quirk kept; a remote one is watched by an observing local
    player watching nobody), 0x0053C950 DestroyPlayer, 0x0053CB70 DropPlayer (reasons 0 quit .. 6 left out with their
    net_message_player_* texts; QUIRK: the server reads the connection before its own null check), 0x0053D120
    ReAddPendingPlayers (QUIRK kept: its qr2 report tests 0x007CCE1D just after setting it - never), 0x0053D900
    DestroyAllPlayers, 0x0053E910 RemovePlayers, 0x0053E920 DropPlayersNotReady, 0x0053EE70 RemovePlayersAndApps,
    0x0053EA20 CFunc DropPendingPlayers. Not original helpers: UpdateLobbyServer, FlagReturned (inlined twice each).
  - Test (selftests_net_flow.cpp TestNetFlow): 8 cases: lobby on server / client; six start-game variants (horse,
    timed and untimed CTF, netking with a king, split screen, other); cancel in each state with presence on / off; the
    observer task, six auto-serve conditions, every join state (-1..11, local / remote client, the joiner observing /
    playing / absent); the start task on server and client through every branch; new players in all four situations;
    destroy with the camera on the player (next another / itself / none, with and without a local skater), crown,
    flag, lobby hosting / not, qr2 on / off; drop for reasons 0-7 in three variants (client; crown and flag; first
    come first served, stats off) plus an observer and a local player; pending players re-added; all destroyed; the
    not ready dropped. 62 deliberate bugs, all caught (4 after strengthening: a local player without a skater with a
    watched player, the crown object without the crown, distinct heaps, the presence logged out).
- [x] Batch NET2-3 (NOT run in the game yet; the server's qr2 reporting, the lobby's small members and hosting
  callbacks, 19 functions): src/game/net/gamenet_qr2.cpp (+ .h), gamenet_lobby.cpp (+ .h) (new; GameNet::LobbyMan,
  the manager's m_lobbyMan, layout +0x34 room name, +0xB4 server name, +0x134 in a group room, +0x136 expecting a
  disconnect, +0x137 lost connection to report, +0x138 connecting, +0x139 hosting, +0x140 the PEER, +0x150 the
  hosting task). Shared files: src/game/net/online_identity.cpp / .h (three name sites dropped, kGameNameSites 3),
  tools/check_code_writes.ts (23 writes), src/hook/selftests_pc.cpp (TestOnlineIdentity's site list). CLAUDE.md still
  says "6 push operands" for the identity (now 3) - for the lead to update.
  - qr2 (Ghidra has no functions there: reached by pointer): 0x0053B9C0 ServerKeyCallback, 0x0053BBC0
    PlayerKeyCallback (QUIRK kept: the index is never counted down, so only player 0 is ever reported - the others get
    ""), 0x0053BD90 CountCallback, 0x0053BDC0 StartQueryReporting (the level's ui_string strcpy'd into 0x0067F0A0 - the
    mapname fallback; qr2_init on port 5155 with the decoded secret key, once).
  - Lobby: 0x0053F7E0 StopHosting, 0x0053F810 IsHosting, 0x0053F820 GetRoomName (REPLACE_SOURCE_ONLY, 4 bytes),
    0x0053F830 / 0x0053F850 Set/GetServerName, 0x0053F860 / 0x0053F870 Clear/SetLostConnection, 0x0053F880 GetPeer,
    0x0053F890 InGroupRoom, 0x0053F8B0 / 0x0053F8C0 two list searches' destructor bodies (vtables 0x0064CB6C /
    0x0064CB70), 0x0053F8A0 CFunc ResetConnectionTimer, 0x0053F8D0 ServerKeyCallback (the peer SDK's: THUG2's keys
    51-56 too; mapname without a fallback; gamemode = gametype; every key sets 0x007CCE48), 0x0053FEC0 CountCallback
    (servers 16), 0x0053F620 KeyListCallback.
  - The online identity: the game name pushes at 0x0053BE86 (qr2_init), 0x0053BA1F and 0x0053F936 (the two
    "gamename" keys) are in these functions - ours report Net::GameName(), and online_identity.cpp no longer writes
    there (disabling gamenet_qr2.cpp / gamenet_lobby.cpp then reports "thps6pc" in those places). The secret key is
    still read from 0x0067F120 (data the identity writes). check_code_writes: "No overlaps".
  - Test (selftests_net_qr2.cpp TestNetQr2): every server key -1..22 and the variants (no level name, a password,
    the gamemode flags), the player keys for indexes -1..3 x 5 keys, ping local / not, an observer first, no players;
    the counts; qr2 started (failing, the name noted, once, keeping the name); the peer server keys -1..58 and
    variants; the counts, key lists; the lobby members, the searches, the timer. 37 deliberate bugs: 35 caught (1 after
    adding an observer as the first player), 2 equivalent (an index equal to the player count reported "" either way -
    the quirk; GetString's assert flag).
- [x] Batch NET2-4 (NOT run in the game yet; the GameSpy lobby's connection, hosting, NAT negotiation and its script
  commands, 20 functions): src/game/net/gamenet_lobby_peer.cpp, gamenet_lobby_cfuncs.cpp (new), gamenet_lobby.h
  (LobbyMan's +0x135 restricted hosting, +0x13C NAT negotiations, +0x144 the client's cookie, +0x148 / +0x14C / +0x150
  the think / NAT negotiation / query tasks - as the constructor 0x00541870 sets them; the packet queue 0x007CCE50).
  - Peer callbacks: 0x0053F570 DisconnectedCallback, 0x0053FEE0 NickErrorCallback (in use: retried as
    "a<time>_<network_id>"; invalid: made valid by 0x005FD9F0), 0x0053FFC0 ConnectCallback (the peer's public address
    into Net::Manager, StartLobbyList, the connected time 0x007CE434), 0x0053FB80 PlayerKeyCallback (players counted:
    playing ones and observers with flag 8, then joining ones unless observers without 0x18; player_ name / rating
    57; ping_ ""), 0x0053FDA0 PublicAddressCallback (join ip = the address given, private ip = the peer's 0x005FC950;
    presence "homie_status_hosting" with "10thug211:..."), 0x00540C90 NatNegotiateCallback, 0x00540300
    NatNegotiationCompleted (success on a client: JoinServer at the sockaddr's address / port; server: counted down;
    client: cleaned up, failure show_nat_timeout), 0x0053F6C0 QueuePacket (qr2 queries and NAT negotiation packets
    copied with the game's C runtime malloc).
  - Tasks: 0x00540040 ThinkTask, 0x005400B0 NatNegotiationTask, 0x005400E0 QueryTask (the queue handed on and freed;
    not reported: CreateNotPostedDialog after 90 s once, else the details again every 5 s). 0x00540220 StartHosting,
    0x00540750 Disconnect, 0x00540AB0 ConnectThread (stdcall, 0).
  - Script commands: 0x00540430 StartNatNegotiation, 0x005403E0 CancelNatNegotiation, 0x00540590 LeaveLobby
    (preserve_status keeps the presence), 0x00540640 CancelLobbyConnect, 0x00540660 SetQuietMode (off), 0x005406C0
    CanHostGame (restricted: ten hard-coded GameSpy profile ids).
  - Not original: our ntohs is a byte swap (the original calls ws2_32's through its import thunk 0x005BEB02); inet_addr
    is ws2_32's called directly (the original: 0x005BEAAE). The queue's copies still use the game's C runtime malloc /
    free (0x00624031 / 0x0062411B), so the original's and our code can share the queue.
  - Test (selftests_net_lobby.cpp TestNetLobby, 6 cases): every callback's branches (disconnect x3, nick error x3,
    connect ok / failed, the public address on a client / without a peer / logged out / in); the player keys for 4
    keys x 9 indexes over playing / observing / counted players and joining players of each flag kind; packets of
    every kind (queue full, a short NAT packet with the magic's last byte after it); the query task in 8 reporting
    situations; think / NAT tasks; hosting (report result 1 / 0, a non-empty queue), disconnect with / without a peer,
    the connect thread; NAT negotiation completed (client success / failure, server counting down) and asked (busy,
    failing, twice); the six commands. 58 deliberate bugs: 57 caught (7 after recording the lobby's flags after each
    callback, a padded short NAT packet, an 85 s case, distinct presence texts), 1 equivalent (NatNegotiateCallback's
    second "none going on" check - always true after its early return; kept as the original has it).
- [x] Batch NET2-5 (NOT run in the game yet; the lobby's menus, chat, group room list and its destructors,
  21 functions): src/game/net/gamenet_lobby_menus.cpp (new), gamenet_lobby.h (LobbyRoom 0x6C, LobbyPlayer, the two
  node types, Task<T>'s deleter).
  - Menus: 0x00540900 AddBuddyMenuItem (make_text_sub_menu_item, lobby_add_buddy), 0x00540FB0 AddRoomMenuItem
    (regions_menu_add_item "<name> - <players>/<most>" (+ net_lobby_full); choose_selected_lobby + index, or
    not_focusable), 0x005411E0 AddPlayerListItem (the rank: rating x 0.000285714 (0x0064CC1C) x 10 in double, truncated,
    at most 10; add_buddy_prompt / cant_add_buddy_prompt_1-3 / cant_add_self_to_buddy_prompt / already_buddy_prompt with
    their rgba), 0x00540B60 RoomMessageCallback ("<nick>: <message>"), 0x00540E00 CFunc SendMessage (system_message:
    "<System> <text>" to the console only; else to the group room), 0x00541690 GroupRoomCallback (QUIRK noted: the room's
    players are the callback's numGames and "full" compares its numWaiting with maxWaiting; the lobby asked for by
    lobby_num - the CRC of the pending data - decides the end state 15 / 16).
  - Destructors: 0x00540D50 LobbyMan::DestroyBody (its three tasks deleted, its +0x14 node and its own unlinked),
    0x00540840 / 0x00540860 the two node types' bodies, 0x00540880 / 0x005408C0 / 0x00540F80 their scalar deleting
    destructors, 0x005407E0 / 0x00540810 the list searches', 0x005407B0 Tsk::Task<T>'s (vtable 0x006476F0 slot 2 - the
    linker put it here).
  - Test (selftests_net_menus.cpp TestNetMenus, 4 cases): every player-list branch (may add / 30 buddies / itself /
    a buddy / no profile / logged out) with ratings that tell double from float maths and pass the rank's limit; the
    regions item available / full / out of range; chat and the three SendMessage paths; the room list (failure, the
    end with and without a lobby asked for / found, a name past 63 characters, above / below the rating range,
    "neversoft", players above the most); the destructors on rings of nodes, freeing or not (flags 0-3). 41 deliberate
    bugs, all caught (3 after strengthening: rating 350 - rank 0 in doubles, 1 in floats; a dirty new room for the
    name's NUL; flags 2). The kit (net2_testkit.h) now builds lists with their back links (Lst::Head::AddNode runs for
    real), stands in Mem::Malloc / FreeMalloc and an empty symbol table.
- [ ] Next: the manager's constructor 0x0053EE90 / Create 0x0053F520 and 0x0053D9D0, 0x0053E440-0x0053EB70 (the
  last of the range).
