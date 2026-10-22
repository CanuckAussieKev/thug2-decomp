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
