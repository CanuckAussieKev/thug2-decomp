#pragma once

#include <cstddef>
#include <cstdint>

namespace Net {
struct App;
struct Conn;
struct MsgHandlerContext;
} // namespace Net
namespace Prefs {
class Preferences;
} // namespace Prefs

namespace Tsk {
class BaseTask;
}

// The game's network manager GameNet::Manager (0x00533F20-0x0054EF60; the singleton at 0x007CCE14): the network
// game itself - the players, the levels and modes the server picks, the lobby. Partly rewritten: the join path
// (gamenet_join.cpp) and the level name; only the fields used so far are named.
namespace GameNet {

// --- The join messages (THUG2's) ---

constexpr int32_t kVersion = 0x0004000B; // THUG2 PC's network version (both ends must match)
constexpr int kPasswordSize = 10;        // 9 characters + NUL
constexpr int kNameSize = 16;            // 15 characters + NUL
constexpr int kServerNameSize = 16;
constexpr uint32_t kAppearanceSize = 0x1000;

// The connection request (message 3, 92 bytes): sent by the joining client, read by the host's ok_to_join.
struct ConnectInfo {
    int32_t version;        // +0x00 kVersion
    bool observer;          // +0x04
    bool willingToWait;     // +0x05 a game in progress is no refusal
    char password[kPasswordSize]; // +0x06
    bool broadband;         // +0x10
    int8_t rank;            // +0x11 (the rank limit's test)
    char cdKeyResponse[74]; // +0x12 the GameSpy CD-key response (72 characters) to the host's challenge
};
static_assert(sizeof(ConnectInfo) == 92 && offsetof(ConnectInfo, cdKeyResponse) == 0x12);

// The join request (message 80): the connection request, the player and the appearance (its used length only).
struct JoinInfo : ConnectInfo {
    int32_t profile;                // +0x5C the GameSpy profile id
    int32_t rating;                 // +0x60
    int32_t vehicleControlType;     // +0x64
    char name[kNameSize];           // +0x68
    uint8_t appearance[kAppearanceSize]; // +0x78 (CSkaterProfile::WriteToBuffer)
};
static_assert(offsetof(JoinInfo, name) == 0x68 && offsetof(JoinInfo, appearance) == 0x78);
constexpr uint32_t kJoinInfoHeaderSize = offsetof(JoinInfo, appearance);

// Join proceed (message 79, 32 bytes): the host's answer to a connection request; the client then sends its join
// request.
struct ProceedInfo {
    uint32_t publicIp;   // +0x00 (internet hosts)
    uint32_t privateIp;  // +0x04
    int32_t port;        // +0x08
    char serverName[kServerNameSize]; // +0x0C
    int8_t maxPlayers;   // +0x1C
    bool broadband;      // +0x1D (the host's: the client's connection to it is narrowband if not)
    uint8_t pad1E[2];
};
static_assert(sizeof(ProceedInfo) == 32 && offsetof(ProceedInfo, maxPlayers) == 0x1C);

// Join refused (message 71, 10 bytes): the reason; with kRefusedCdKey, the host's CD-key challenge after it.
enum JoinRefusedReason : int8_t {
    kRefusedFull = 0,
    kRefusedPassword = 1,
    kRefusedVersion = 2,
    kRefusedInProgress = 3,
    kRefusedBanned = 4,
    kRefusedFullObservers = 5,
    kRefusedRankLow = 6,   // the host's "rank greater than" test
    kRefusedRankHigh = 7,  // its "rank less than" test
    kRefusedCdKey = 8,     // (and the challenge: the client answers with a new connection request)
};
constexpr uint16_t kJoinRefusedSize = 10;

// The join's state (m_joinState, 0x005331F0 / 0x00533200).
enum JoinState : int {
    kJoinStateConnecting = 1,
    kJoinStateJoining = 2,
    kJoinStateJoiningWithPassword = 3,
    kJoinStateTryingPassword = 4,
    kJoinStateCdKey = 5,     // answered the host's CD-key challenge
    kJoinStateRefused = 9,   // (the join-state task shows the refusal: CreateJoinRefusedDialog with m_refusedReason)
};

class Manager {
public:
    // 0x005369A0: the current network level's name - the `text` of its entry in the level select's list
    // (`level`, a load script's checksum, matching m_level); for the created park (load_sk5ed_gameplay) with
    // `parkName` set, the park's own name. Null when the level is in no list.
    // The original looks in level_select_menu_level_info only, THUG2's own levels: ours also looks in the lists
    // of the ported levels (tools/menu_level/level_select.ts sections()), which the original's null crashed on
    // (0x005311F0, the server's level change, adds it to a structure as a string).
    const char* GetLevelName(bool parkName);
    // 0x0044E4E0: whether the network mode bits (+0, bits 2-3) are set: 1 or 0.
    int InNetMode();
    // 0x0044E4F0: whether a network game is on: in network mode, with a server or client app that isn't local.
    bool InNetGame();

    // NOT ORIGINAL: the level select's name for `level` (a load script), in THUG2's list or the ported levels' -
    // GetLevelName's lookup for any level (null if none).
    static const char* LevelListName(uint32_t level);
    // 0x00533F90 (inline here): the server's side of the game (OnServer): flags bit 0.
    bool OnServer() const { return (m_flags & 1) != 0; }
    // 0x00533F80 (inline here): internet play (flags bit 2), not LAN.
    bool InInternetMode() const { return (m_flags & 4) != 0; }

    // --- gamenet_state.cpp: the manager's small members (0x00532AA0-0x005333F0) ---
    // 0x00532AA0 / 0x00532AC0: how the player joins (m_joinMode: 0 to play, 1 to watch - IsObserving). Setting it
    // clears the join-by-address flag (0x007CCA88, TryJoinServerIPSuccess).
    void SetJoinMode(int mode);
    int GetJoinMode();
    // 0x00532AD0: m_hostMode (1: the task at +0xF4 is removed when the player list is sent - 0x0056B890).
    int GetHostMode();
    // 0x00532AE0: the network preference rank_test: 1 rank_test_less, 2 rank_test_greater, else 0.
    int GetRankTest();
    // 0x00532B30: rank_limit: num_1 ... num_9 -> 0 ... 8, else 9.
    int GetRankLimit();
    // 0x00532BE0: skill_level: num_1 ... num_4 -> 0 ... 3, num_5 -> 4, else 2.
    int GetSkillLevel();
    // 0x00532C50: num_players: num_2 ... num_8 -> 2 ... 8, else 2.
    int GetMaxPlayers();
    // 0x00532CF0: team_mode: teams_none 0, teams_two 2, teams_three 3, teams_four (or anything else) 4.
    int GetNumTeams();
    // 0x00532D60: num_observers: num_0 ... num_8 -> 0 ... 8, else 2.
    int GetMaxObservers();
    // 0x00533080: the level preference's checksum (level).
    uint32_t GetLevelPreference();
    // 0x005330B0: fireball_difficulty: num_1 ... num_4 -> 1 ... 4, num_5 -> 5, else 3.
    int GetFireballLevel();
    // 0x00533140: show_names is boolean_true (a missing preference: false, not asserted).
    bool ShouldDrawPlayerNames();
    // 0x00533130: the player-names flag (+0x47) toggled (TogglePlayerNames).
    void TogglePlayerNames();
    // 0x00532FE0: the game mode (+0x30) has no time limit: goal attack, king of the hill, score challenge (and
    // their net versions) always; else, in a network game, a capture the flag that isn't timed_ctf, or the
    // stop_at_zero preference set.
    bool IsUntimedGame();
    // 0x00532E30: the task at +0xF4 taken off its list.
    void RemoveHostTask();
    // 0x00532E40 / 0x00532E50 / 0x00532E60 / 0x00532E70 / 0x00532E80 / 0x00532E90: the server joined: m_joinIp,
    // m_joinPrivateIp, m_joinPort.
    uint32_t GetJoinIp();
    uint32_t GetJoinPrivateIp();
    int GetJoinPort();
    void SetJoinIp(uint32_t ip);
    void SetJoinPrivateIp(uint32_t ip);
    void SetJoinPort(int port);
    // 0x00532EA0: client `index` (ClientSlot).
    Net::App* GetClient(int index);
    // 0x00532EB0: the server list's client (m_matchClient).
    Net::App* GetMatchClient();
    // 0x00532EC0: m_camPlayer.
    void* GetCamPlayer();
    // 0x00532ED0: ask for servers on the LAN: message 0x11 (the time now, 12 bytes) through the lobby client to the
    // address asked for (0x007CCA84, once), else broadcast on port 5153 and to the address from the command line.
    void FindServersOnLan();
    // 0x00532F70 / 0x00532F80: the network preferences (+0x104) / the taunt preferences (+0x110).
    Prefs::Preferences* GetNetworkPreferences();
    Prefs::Preferences* GetTauntPreferences();
    // 0x00532F90 / 0x00532FD0: the eight words at +0x48 (the start info's, message 0x34) / one of them.
    void SetStartInfoWords(const uint32_t* words);
    uint32_t GetStartInfoWord(int index);
    // 0x00533060 / 0x00533070: the current game's number (+0x38).
    void SetGameId(uint8_t id);
    uint8_t GetGameId();
    // 0x00533250: tell the server the game has ended here (message 0x59, the game's number - once every player's
    // has, the server sends 0x5A, game over) and note the number (+0x39).
    void SendGameEnded();
    // 0x005332A0: the end of the current game has been reported.
    bool GameEndedSent();
    // 0x005332B0 / 0x005332C0 / 0x005332D0: m_gameOver (the server's message 0x5A).
    bool IsGameOver();
    void SetGameOver();
    void ClearGameOver();
    // 0x005332E0 / 0x005332F0: the cheating notice has been shown (+0x3B).
    bool CheatingNoted();
    void NoteCheating();
    // 0x00533180 / 0x00533190 / 0x005331A0: the last player (+0x1D0) / team (+0x1D4) announced as the leader.
    void SetLastLeader(void* player);
    void* GetLastLeader();
    int GetLastLeadingTeam();
    // 0x005331B0 / 0x005331C0: m_lastScoreUpdate.
    void SetLastScoreUpdate(uint32_t time);
    uint32_t GetLastScoreUpdate();
    // 0x005331D0 / 0x005331E0: the final scores have come (+0x44).
    void SetFinalScoresReceived(bool received);
    bool FinalScoresReceived();
    // 0x005331F0 / 0x00533200: m_joinState.
    void SetJoinState(int state);
    int GetJoinState();
    // 0x00533210 / 0x00533220: the crown of king of the hill (+0x6C) / forgotten.
    void* GetCrown();
    void ClearCrown();
    // 0x00533230: the crown's restart point (+0x68), a signed byte.
    void SetCrownSpawnPoint(char point);
    // 0x00533240: +0x9950.
    void SetLobbyId(uint32_t id);
    // 0x00533300: message 0x26 - the ready query's id the server sent (m_latestReadyQuery, message 0x25) back to the
    // server through each client (the player's connection is ready again).
    void SendReadyResponse();
    // 0x00533360: the buffer at +0xFDF8 (its size in *size, +0xFDFC).
    void* GetPendingData(uint32_t* size);
    // 0x00533380: the goal editor has created goals (0x004E7E50 with 0).
    bool HasCreatedGoals();
    // 0x005333A0: the goal editor's goals for `level` written into the goals message (+0x1D8: the level, then the
    // goals - at most 6000 bytes); +0x1948 its size, +0x194C the level.
    void PrepareCreatedGoals(uint32_t level);
    // 0x005333E0 / 0x005333F0: the goals message / its size.
    void* GetCreatedGoalsMessage();
    uint32_t GetCreatedGoalsSize();

    // --- gamenet_members.cpp: the network mode, the team flags, preferences, the server's end (0x00533F20-0x00534FC0)
    // 0x00533F20: the network mode (flags bits 2-3): 1 LAN (bit 3), 2 internet (bit 2), else neither.
    void SetNetworkMode(int mode);
    // 0x00533F50: the server's side (flags bit 0) on or off.
    void SetServerMode(bool on);
    // 0x00533F70: LAN play (flags bit 3).
    int InLanMode();
    // 0x00533FA0: the client's side (flags bit 1).
    int OnClient();
    // 0x00533FB0: Kill_Team_Flags, then (with teams) create_team_flags {num_teams = n}.
    void CreateTeamFlags(int numTeams);
    // 0x00534090 / 0x005341B0: the network preference num_players / num_observers set to `count` - its ui_string
    // ("<n> <netmenu_str_players>"; for observers "no observers", "1 observer", "<n> observers") and checksum
    // (num_<n>). The parameters are never freed (a leak the original has).
    void SetMaxPlayersPreference(int count);
    void SetMaxObserversPreference(int count);
    // 0x00534310: the network preference password's ui_string.
    const char* GetPassword();
    // 0x00534340: whether skaters collide with each other: never in a finished game, free skate or horse (unless in
    // the lobby); always in split screen, with player_collision on, and in (net)slap.
    bool PlayerCollisionEnabled();
    // 0x005343E0: a team game's scores shown by team (score_display = score_teams; never in the lobby).
    bool ShowTeamScores();
    // 0x00534CF0: the server destroyed (an internet one's GameSpy reporting stopped first), its two tasks removed;
    // flags bit 0 cleared.
    void DestroyServer();
    // 0x00534D50: the server list's client destroyed.
    void DestroyMatchClient();
    // 0x00534D70: a client (not a modem) with a custom tag texture streams its palette (0x98) and texture (0x97) to
    // the server over a broadband connection, then tells which CAS logos it has (0x9A, 4 bytes). True if the skater
    // has a custom tag texture at all.
    bool SendCustomTagTexture();
    // 0x00534F40 (4 bytes: source only): the level's load script.
    uint32_t GetLevel();
    // 0x00534F50 / 0x00534FC0: in the global array `list` (structures), the `name` string / the integer `field` of
    // the entry whose checksum is `choice` (the last such; null / 0 if none).
    const char* GetChoiceName(uint32_t list, uint32_t choice);
    int GetChoiceInteger(uint32_t list, uint32_t choice, uint32_t field);
    // 0x005359F0: the starting points (the eight words at +0x48): a random order of 0 ... n-1 plus a random offset
    // (0-255) - n 8 in a network game, else the game mode's max_players, or 2 (offset 0) when the park editor's +0x40
    // isn't `off` and the mode isn't horse; the crown's (+0x68) random 0-9.
    void ChooseStartPoints();
    // 0x00535BC0: the net metrics (+0x45) toggled - their task (+0xC4) added to the logic tasks or removed.
    void ToggleNetMetrics();
    // 0x00535BF0: the scores' display (+0x46) toggled.
    void ToggleScores();
    // 0x00535D00: whether the game keeps a score: modes accumulating score or tracking the best combo, firefight, goal
    // attack, CTF, scavenger hunt - but not elimiskate (unless the net firefight / elimiskate modes).
    bool KeepsScore();
    // 0x00535D90: m_proSetFlags (a signed byte, widened).
    void SetProSetFlags(char flags);

    // --- gamenet_settings.cpp: the network settings and the connection (0x00535040-0x00535CB0) ---
    // 0x00535040: the network preferences handed to the net library (Net::Manager): the master servers (+0x9954, ten
    // names of 16 bytes: the two set, unless "0.0.0.0", then default_master_servers - or only those), DHCP or the
    // addresses, DNS, dial-up, the device and connection type (modems: at most 3 players, no observers),
    // dialup_authentication. Four global strings (names unknown) override the address, gateway, subnet mask and DNS
    // server (an address of our own means no DHCP and the USB adaptor set-up script).
    void UseNetworkPreferences();
    // 0x00535AD0: team_graffiti_mode in a network team game, else graffiti_individual_scoring.
    uint32_t GetTeamGraffitiMode();
    // NOT ORIGINAL (inline in both below): the network preferences in a network game, else the skate module's.
    Prefs::Preferences* GetGamePreferences();
    // 0x00535B20 / 0x00535B70: the goals / game_type preference's checksum (the manager's address on a miss).
    uint32_t GetGoalsPreference();
    uint32_t GetGameTypePreference();
    // 0x00535C10: go online (Net::Manager::ConnectToInternet unless already); then `onlineScript` runs. If it fails, a
    // modem or PPPoE connection (types 1 / 3) carries on in the online state task (+0xD8), `failScript` kept (+0x1C8).
    bool ConnectToInternet(uint32_t onlineScript, uint32_t failScript);
    // 0x00535CB0: go offline: the online state task added (state -1; `script`, if any, for when it's done), the lobby
    // left (0x005453E0 with 2), Net::Manager::DisconnectFromInternet.
    void DisconnectFromInternet(uint32_t script);
    // 0x00536C10 (the CFunc CancelConnectToInternet's; no `this`): by the net library's modem state - 0 / 1 (dialling):
    // the online task restarted, the lobby left (state 2), offline; 2: modem state 6; 5: the online task removed and
    // the fail script (+0x1C8) run; 9: the online task removed; else nothing.
    static void __cdecl CancelConnectToInternet();
    // 0x00536490: look for a server at `ip`: the server lists cleared (0x00545370, 0x005462F0, 0x00545300; the
    // join-by-address flag 0x007CCA88 too), the server-list client made if there's none, then (an address given) the
    // find-server message 0x11 (the time, 12 bytes) sent to ip:5153 and the address kept (0x007CCA84; 0 with none).
    void FindServerAt(uint32_t ip);
    // 0x00536640 (from the main loop; no `this`): GameSpy's think (0x005F9C40); in a network game with its query /
    // reporting up (0x007CCA82, 0x007CCE1C), its reporting's think (0x005FBBA0) too.
    static void __cdecl ServiceGameSpy();
    // 0x00536A40: the `name` of the set-up game's game type in mode_info (null if it isn't there).
    const char* GetGameTypeName();
    // 0x00536B90: the level (m_level); the lobby's GameSpy peer (connected, the lobby hosting - 0x0053F810) told its
    // state changed (0x005FCCF0).
    void SetLevel(uint32_t level);

    // --- gamenet_players.cpp: the players' records (0x00536820-0x00537F40) ---
    // 0x00536820: every joining player (the list at +0x130) removed: its connection terminated on the server, its node
    // and record (0x00543560) deleted.
    void RemoveAllNewPlayers();
    // 0x00536B10: the joining player `player` removed (its connection terminated on the server, or on client 0 if it
    // has one).
    void RemoveNewPlayer(void* player);
    // 0x005372C0 / 0x00537010: the first / next player of the list (+0x11C) through `search` (a Lst::Search -
    // {vtable, node}); without `observers`, observers are skipped. Null at the end.
    void* FirstPlayerInfo(void* search, bool observers);
    void* NextPlayerInfo(void* search, bool observers);
    // 0x00537070: the first joining player (+0x130) through `search` (then the lists' generic next, 0x004573D0).
    void* FirstNewPlayerInfo(void* search);
    // 0x005370A0: the joining player whose object id (+0x10, a signed byte) is `id`.
    void* GetNewPlayerInfoByObjectId(uint16_t id);
    // 0x00537140: ready to play (m_readyToPlay) or not; when it changes, every skater of the skate module (+0x28's
    // list) told - its virtual slot 12 when ready, 11 when not.
    void SetReadyToPlay(bool ready);
    // 0x00537320 / 0x005373D0 / 0x00537480: the first player (observers too) that is local (flags bit 0) / the server's
    // (bit 9) / has the crown (0x00543490). Null if none.
    void* GetLocalPlayer();
    void* GetServerPlayer();
    void* GetKingOfTheHill();
    // 0x00537530: the players (no observers) with neither flag 0x21 (bit 0 local; bit 5) set.
    int CountOtherPlayers();
    // 0x00537620: every player's flags 0x100 / 0x20000 and +0x1C, +0x20, +0x34 cleared, its skater's score reset
    // (the SkaterScore component's +0x18 - 0x00564A10).
    void ResetPlayers();
    // 0x005376F0: a panel message: with `broadcast`, message 0x3F to every player through the server (the CRC of
    // `properties`, `text`, `time`, string 1 and 2 - 16 bytes each; sequenced in group 8 when `important`); else locally:
    // create_net_panel_message {text (the global text `text`, its "%s0" replaced by string 1), id = net_panel_msg,
    // msg_time = time}. `object` isn't used.
    void CreateNetPanelMessage(bool broadcast, uint32_t text, const char* string1, const char* string2, void* object,
                               const char* properties, bool important, int time);
    // 0x00537A10: a script queued to spawn once the level is up (+0x144; in the network heap) - unless one of the same
    // name is queued already.
    void QueueScript(int node, uint32_t objectId, uint32_t script);
    // 0x00537B30: every queued script dropped.
    void ClearQueuedScripts();
    // 0x00537D90: every queued script spawned at its node on its object and run to its end (in the bottom-up heap,
    // the main volume off, between PreRunQueuedScripts and PostRunQueuedScripts and the skate module's 0x00569580 /
    // 0x00569590), then dropped.
    void LaunchQueuedScripts();
    // 0x00537F40: pro set `bit` toggled (m_proSetFlags) and told to every remote player (message 0x70: `param`, the
    // bit's byte; sequenced in group 8).
    void ToggleProSet(uint8_t bit, uint32_t param);
    // 0x00539880: the players (no observers) on `team` plus the joining players who will play (+0x20 flags 0x18, or
    // not 4) on it (+0x28).
    int CountTeamPlayers(int team);
    // 0x00539B00: the players (non-observers, and observers with flags bit 3) plus the joining players who will play
    // and aren't among them - the original's check compares each with the byte after the ids it noted (zero), so
    // only joining players with object id 0 are left out.
    int CountPlayers();
    // 0x00539C70: the observers (not flags bit 3; the local one only in host mode 0) plus the joining observers (+0x20
    // flags 4 without 8).
    int CountObservers();
    // 0x005399F0: the clients destroyed (client 0's task +0xD0 removed unless it's local), the observer input handler
    // deleted, 0x007CCDF8 cleared, GameSpy's reporting shut down (0x005FBB20) and 0x005F9900(1003), the tasks +0xD4,
    // +0xC8, +0xEC removed, the queued scripts dropped, and no longer ready to play (the skaters' slot 11).
    void DisconnectClients();
    // 0x00539DA0: `player`'s custom tag texture (and its CAS logos: 0x96 its skater id, 0x98 the palette, 0x97 the
    // pixels, 0x9A the logos - group 10) streamed through the server to every other remote player who has one... (not
    // on a modem; nothing when `player` is local) - see the source for the exact conditions.
    void SendTagTexturesTo(void* player);
    // 0x0053A040: the local skater's face texture (the profile's +0x54, if it has one - 0x00487510): on the server
    // kept on the local player (+0x1E8: a byte 0, then 0x4498 bytes); on a broadband client streamed to the server
    // (0x7E "Face Data", 0x4498 bytes). False on a modem, without a face, or a narrowband client.
    bool PrepareLocalFace();
    // 0x0053A1B0: every other player's face (+0x1E8; the local one's prepared first) streamed to `player` through the
    // server (0x7E, 0x4499 bytes), unless `player` is on a modem connection.
    void SendFacesTo(void* player);
    // 0x0053A310: a joining player's record made from `info` (unless one with its object id is listed): copied (the
    // appearance through a 0x1000-byte buffer for those who will play), its time-out base (+0x18) the app's time plus
    // `timeout`; on the server the join announced (net_message_joining locally, or net_message_observing /
    // net_message_join_pending to everyone); listed (+0x130), in the network heap.
    void AddNewPlayer(void* info, int timeout);
    // 0x0053A580: the next player the observer camera can follow after m_camPlayer (from the start if there's none or
    // it's gone; wrapping round), skipping those with 0x00543330 / 0x00543440 set. Null if none.
    void* NextCamPlayer();

    // 0x00536D70: the destructor body: GameSpy shut down (0x005F9900 with 1003, 0x005F9BE0, its reporting if up), the
    // lobby, +0x14, the presence and the stats deleted, the tasks (+0xC0 ... +0xF0) deleted, the lists emptied, the
    // two preference sets destroyed.
    void DestroyBody();

    // --- The join (gamenet_join.cpp) ---

    // 0x0052F560: whether the player of connection request `msg` from `conn` may join; if not, `reason` (a
    // JoinRefusedReason). Banned address, full (players or observers), with `checkVersion` THUG2's version, the
    // rank limit (remote players), a game in progress (unless willing to wait or observing; a lobby or a finished
    // game is none), with `checkPassword` the password, with `checkCdKey` a CD-key response of 72 characters.
    // In the THUGPRO peer mode the CD-key check is left out: THUGPRO's hosts drop it, and its players send a made-up
    // key (TP-R12).
    bool OkToJoin(int& reason, ConnectInfo* msg, Net::Conn* conn, bool checkPassword, bool checkVersion,
                  bool checkCdKey);
    // 0x00534480: join the server at ip:port with client `index` (a local client - the host's own - is connected
    // directly): the connection request sent, the client's join handlers added (index 0: the game's too).
    void JoinServer(uint32_t ip, uint16_t port, int index);
    // 0x00534AF0: the connection request again (state "joining"), on `client`.
    void ReattemptJoin(Net::App* client);
    // 0x00534BD0: the connection request again with `password` (state "joining with password").
    void ReattemptJoinWithPassword(const char* password);
    // 0x00537220: client `index` made (THUG2's join port; broadcast, local - else dynamic resend and foreign
    // connections -, secure as asked); index 0 resets the ready / pro-set state. Returns it (null if it failed).
    Net::App* SpawnClient(bool broadcast, bool local, bool secure, int index);
    // 0x00534430: the server-list client (broadcast, dynamic resend, foreign connections, secure) and its server
    // response handler.
    Net::App* SpawnMatchClient();
    // 0x0053C380: the server made (THUG2's host port, 20 connections), host mode "serve", its lobby handlers (the
    // connection and join requests among them), the starting points shuffled. Returns it (null if it failed).
    Net::App* SpawnServer(bool local, bool secure);

    // The message handlers (cdecl, data: the manager).
    // 0x0052F790 (message 3, host): a connection request - ok_to_join (password, version, CD key), then a new
    // connection and join proceed, or join refused; on the internet the CD key is checked with GameSpy first (the
    // answer comes to ClientAuthorized). In the THUGPRO peer mode that GameSpy check is left out (TP-R12).
    static int __cdecl HandleConnection(Net::MsgHandlerContext* context);
    // 0x0052FA10 (message 80, host): a join request - ok_to_join again (remote players), then the new player
    // (deferred 10 frames; the host's own at once - and on the internet its own CD key checked, an answer nobody
    // reads: left out in the peer mode).
    static int __cdecl HandleJoinRequest(Net::MsgHandlerContext* context);
    // 0x0052E490 (message 79, client): join proceed - the host's details kept, the join request sent (the
    // appearance; in the THUGPRO peer mode the profile is the machine id, as THUGPRO's - TP-R12).
    static int __cdecl HandleJoinProceed(Net::MsgHandlerContext* context);
    // 0x0052E1E0 (message 71, client): join refused - the reason shown (or the password asked for, the "game in
    // progress" dialog, the CD-key challenge answered). NOT ORIGINAL: THUGPRO's reasons 31-37 shown in words (TP-R13).
    static int __cdecl HandleJoinRefused(Net::MsgHandlerContext* context);
    // 0x00536530: GameSpy's answer to a CD-key check (gcd_authenticate_user's callback; `instance` the
    // connection's {app, manager, connection}, freed here): join proceed, or refused (reason 8).
    static void __cdecl ClientAuthorized(int gameId, int localId, int authenticated, char* error, void* instance);

    // Fields.
    Net::App*& ClientSlot(int index) { return (&m_client)[index]; }

    uint8_t m_flags;          // +0x00 (bit 0: on the server; bit 2: internet; bits 2-3: network mode)
    uint8_t m_unknown1[3];
    Net::App* m_server;       // +0x04
    Net::App* m_client;       // +0x08 client 0 (the game's)
    Net::App* m_client1;      // +0x0C client 1
    void* m_lobbyMan;         // +0x10 the GameSpy lobby (its server name +0xB4, its peer +0x34)
    uint32_t m_unknown14;
    void* m_buddyMan;         // +0x18 the GameSpy presence (logged in: +0x10)
    void* m_statsMan;         // +0x1C the player's rating (+0x14)
    char m_challenge[12];     // +0x20 the host's CD-key challenge
    Net::App* m_matchClient;  // +0x2C the server list's client
    uint32_t m_unknown30;
    uint32_t m_level;         // +0x34 the level's load script
    uint16_t m_unknown38;
    bool m_gameOver;          // +0x3A
    uint8_t m_unknown3B;
    int m_joinMode;           // +0x3C (1: observe)
    int m_hostMode;           // +0x40 (0: serve)
    uint8_t m_unknown44[0x88];
    Tsk::BaseTask* m_serverAddNewPlayersTask; // +0xCC
    Tsk::BaseTask* m_clientAddNewPlayersTask; // +0xD0
    uint8_t m_unknownD4[8];
    Tsk::BaseTask* m_joinTimeoutTask;   // +0xDC
    uint32_t m_unknownE0;
    Tsk::BaseTask* m_joinStateTask;     // +0xE4
    uint8_t m_unknownE8[0x14];
    void* m_observerInputHandler;       // +0xFC
    int m_observerCommand;              // +0x100
    uint8_t m_networkPreferences[0x80]; // +0x104 (Prefs::Preferences)
    uint32_t m_refusedReason;           // +0x184 a global text's name (CreateJoinRefusedDialog's `reason`)
    uint32_t m_latestReadyQuery;        // +0x188
    bool m_readyToPlay;                 // +0x18C
    uint8_t m_unknown18D[3];
    uint32_t m_lastScoreUpdate;         // +0x190
    uint32_t m_proSetFlags;             // +0x194
    int m_joinState;                    // +0x198 JoinState
    uint32_t m_joinIp;                  // +0x19C
    uint32_t m_joinPrivateIp;           // +0x1A0
    int m_joinPort;                     // +0x1A4
    uint32_t m_unknown1A8;
    int m_joinStartTime;                // +0x1AC
    uint8_t m_unknown1B0[4];
    bool m_gamePending;                 // +0x1B4
    bool m_waitingForGameToStart;       // +0x1B5
    uint8_t m_unknown1B6[0x16];
    void* m_camPlayer;                  // +0x1CC
};
static_assert(offsetof(Manager, m_level) == 0x34 && offsetof(Manager, m_joinMode) == 0x3C &&
                  offsetof(Manager, m_serverAddNewPlayersTask) == 0xCC && offsetof(Manager, m_joinTimeoutTask) == 0xDC &&
                  offsetof(Manager, m_joinStateTask) == 0xE4 && offsetof(Manager, m_observerCommand) == 0x100 &&
                  offsetof(Manager, m_refusedReason) == 0x184 && offsetof(Manager, m_joinState) == 0x198 &&
                  offsetof(Manager, m_joinStartTime) == 0x1AC && offsetof(Manager, m_gamePending) == 0x1B4 &&
                  offsetof(Manager, m_camPlayer) == 0x1CC,
              "layout must match the original");

// --- gamenet_players.cpp: outside the class ---
// 0x00539610: the flag task's code (registered by the manager's constructor): in capture the flag, the carried flags
// put on their carriers' heads.
void __cdecl CarriedFlagsTask(void* task);
// 0x005395C0: the manager released (its reference count 0x007CCE18; the last one destroys it).
void __cdecl ReleaseManager();

} // namespace GameNet
