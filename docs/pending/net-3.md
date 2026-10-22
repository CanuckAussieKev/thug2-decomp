# net-3: 0x00541870-0x00548560 (online: the lobby, server browsing, PostGame, players, buddy messages)

Agent "NET3". Branch worktree-agent-a83db81d2c24c3271, game workspace C:\Games\THUG2-3.

## The range

131 Ghidra functions, 2 rewritten before this agent (0x00545880, the empty 0x005480A0); and 9 functions Ghidra never
made (GameSpy / task callbacks and a setter, found between the Ghidra functions: 0x00542330, 0x00542B70, 0x00542B80,
0x00543110, 0x00545490, 0x00545540, 0x00545620, 0x005475C0, 0x00547750). 0x005480A1 / 0x005480B4 are PartyMod's
DLL-loading cave (not game code - see core/stubs.cpp). The range holds: the GameSpy lobby (GameNet::LobbyMan, the
manager's +0x10), the players (GameNet::PlayerInfo / NewPlayerInfo), the capture-the-flag and crown announcements,
the score list, the server list (the manager's server browsing, ServerInfo), PostGame / the MOTD download, the buddy
messages and lobby status, and some linker-folded getters.

Shared files touched (small, additive): CMakeLists.txt (new sources / tests), src/game/skate/profile_manager.{h,cpp}
(CSkaterProfile::DestroyBody, 0x00543010 - its address is in this range).

## Batches

- [x] Batch NET3-1 (NOT run in the game yet; the players): src/game/net/player_info.{h,cpp} (new),
  src/game/skate/profile_manager.{h,cpp}. 49 functions.
  - NewPlayerInfo 0x005434D0 (constructor: a skater profile on the skater info heap, null if out of memory; +0x24,
    +0x2C, +0x30, +0x38, +0x40 zeroed) / 0x00543560 (destructor: the profile destroyed - CSkaterProfile::DestroyBody
    0x00543010, the info cleared and the appearance's destructor body - and freed).
  - PlayerInfo (0x1F0 bytes, vtable 0x0064CCF4, a node of the manager's player list): constructor 0x00543740
    (`flags`, the name "<No Name>", m_lastSentSkater 7, the update records -1 - and 0x00543180 called on this + 0x50
    clears 16 words at its +0xB8 first), destructor 0x00543810 (profile, the object at +0x1EC - virtual slot 2 with
    1 -, the face texture message; then Lst::Node<PlayerInfo>'s vtable 0x0064C9E8 and out of the list), scalar
    deleting 0x00544750; SetSkater 0x005438D0 (a remote player the camera follows is followed again, 0x0053A910),
    SetProfile 0x00543270 (copied into even when the allocation failed, as the original), the face texture message
    0x005431F0 / 0x00543060 (the skater's object id, then the bytes; net misc heap), the skater's tagging component
    0x00543070 / 0x00543090 / 0x005430B0 (its custom-tag byte +0x18 through the setter / getter folded at 0x004E4000 /
    0x0051D850, its graffiti texture +0x1C through 0x00553E90), the ready query 0x005430D0 / 0x00544670 /
    0x005446C0 (the connection's status 1 / 2 swapped, resend counts cleared), the projectile hit time 0x00543110
    (not a Ghidra function) / 0x00543100 / 0x005430E0 (more than 5 s ago, unsigned), the skater update round
    0x00543120 / 0x00543130 / 0x00543140 (1 skater per update on a narrowband connection, else 2), the accumulated
    score 0x00543150 (4 bytes: source only; folded with other classes' +0x20 getters) / 0x00543160, the flags
    0x00543300-0x005434C0 (local 1, just joined 2 - with the time -, observer 4, pending observer 8, fully in 0x20,
    king 0x80, server player 0x200, carrying team n's flag 0x800 << n, face texture sent 0x8000, surveying 0x10000,
    eliminated 0x20000 - with the game mode's time, 0x00553270; IsEliminated also true in netfirefight when the
    skater's SkaterScore has no lives), the connection's handle 0x005434A0, the skater's +0x2BC 0x005434B0 (folded).
  - The score lines' list types: Lst::Search 0x00543170 / 0x00543580 (vtable 0x0064CCE0), Lst::Node 0x00544700 /
    0x005435B0 / 0x00544720 (0x0064CCE4).
  - Test TestNetPlayers (src/hook/selftests_net_players.cpp, the GameNet test kit): every function over prepared
    players (dirty memory, list neighbours, failing allocations, the skater / component / connection present or
    not, every flag pattern, hit times around the 5 s edge and wrapped, bandwidth types); the calls (memory, profile,
    components, connection, game mode), results and the arena compared. 40 deliberate bugs: all caught (3 after
    strengthening: the heap used - the stand-in memory manager's fields told apart -, and the ready query's owed id
    recorded after each call).
- [x] Batch NET3-2 (NOT run in the game yet; the announcements and the score list): src/game/net/player_flags.cpp,
  src/game/net/score_list.{h,cpp} (new); gamenet_manager.h (two declarations), player_info.h. 10 functions.
  - PlayerInfo::IsInvulnerable 0x00543910 (collisions between skaters leave it alone: connection not ready, skater
    not in play - 0x00580050 - or under 10 frames of state history, under a second since joining, out of lives in
    netfirefight, eliminated, the skater's +0x104 bit 2; else the just-joined flag for 5 s, then cleared),
    MarkAsFullyIn 0x005439D0 (flag 0x20; on the server for a remote player: host mode 1 message 0x5E sequenced in
    group 8 on the server app - 0x005480C0 is the folded +4 getter: m_server -; host mode 2 starts the game,
    0x0053AAC0, unless 0x005373D0 holds it).
  - The flag announcements, names from the game's .qb files: CapturedFlag 0x00543A90 (captured_flag_you /
    hide_ctf_arrow + captured_your_flag / captured_flag_other; String0, String1 = team_<n+1>_name, team; its flags
    cleared), StoleFlag 0x00543C70 (stole_flag_you / stole_flag_from_you - the local player carried it, not
    observing / stole_flag_other; whoever carried that flag loses it), TookFlag 0x00543F00 (took_flag_you /
    show_ctf_arrow with the local team + took_flag_yours / took_flag_other), RetrievedFlag 0x00544110 (its own
    team's flag back: hide_ctf_arrow + retrieved_flag_you / retrieved_flag_other; the first carrier of it carries
    nothing; quirk kept: the parameters structure is never freed). The shift 0x800 << team uses the low 5 bits of
    the team, as the x86 SHL (-1 shifts by 31).
  - MarkAsKing 0x00544340 (taken from the previous king - itself too -; NewKingYou with the local skater's
    hide_crown_arrow player_1 / player_2 by skater number, or show_crown_arrow + NewKingOther; in split screen the
    other screen's player told with the skater's display name; the crown worn, 0x005782B0; giving it up shows the
    local player's crown arrow in netking / king).
  - Manager::GetTeamScore 0x005435F0 / GetPlayerScore 0x005436E0 (SkaterScore sums, no null checks; net goal
    attack: the goal manager's 0x00558B10 / 0x00558AB0 unless the local player observes).
  - ScoreUpdateTask 0x00544780 (the score list task, made by 0x0053EE90): once a second (0x007CE43C) clear_scores
    and up to 8 net_score_<n> lines - per team (team modes other than the lobby, 0x005343E0) or per player (fully
    in, not eliminated, firefight players with lives), sorted by score with Lst::Head::AddNode (inlined in the
    original; the lobby appends in player order); the text: the crown abbreviation (used as a sprintf format, as
    the original) or \s0-\s3 per flag carried, \c colour (team + 2 or skater id + 2), the name ("> " for the local
    one), the score (king of the hill: m:ss / h:mm:ss from the milliseconds; scavenger hunt: 5 - the player's +0x34,
    shown only for the local player); each line's element gets SetProperties (virtual slot 1) and update_score the
    same structure; each line is deleted through its vtable (0x00544720). Our text buffers are bigger than the
    original's (16 / 64 bytes: a long crown abbreviation or the longest king of the hill line overran them).
  - Test TestNetScores (src/hook/selftests_net_scores.cpp, the GameNet test kit): every branch above over prepared
    players, teams, scores and game modes (lobby, plain, team, firefight, king / netking with times either side of an
    hour, scavenger hunt goal missing / inactive / active, goal attack, the once-a-second edge), elements present or
    missing; the calls, structures and arena compared. 54 deliberate bugs: 53 caught (9 after strengthening: a local
    player not marked fully in, the 1000 ms edge, a team flag of team 1, host mode for the local player, the players'
    flags recorded after each announcement, an observing local player for RetrievedFlag); 1 equivalent (StoleFlag's
    clearing of the stolen team's bit before clearing all four).
- [ ] Next: the lobby (GameNet::LobbyMan, 0x00541870-0x00543010 and its GameSpy callbacks).
