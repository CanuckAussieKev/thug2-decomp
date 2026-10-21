#pragma once

#include <cstdint>

namespace Net {
struct App;
} // namespace Net
namespace Prefs {
class Preferences;
} // namespace Prefs

// The game's network manager GameNet::Manager (0x00533F20-0x0054EF60; the singleton at 0x007CCE14): the network
// game itself - the players, the levels and modes the server picks, the lobby. Not rewritten yet: only the fields
// used here.
namespace GameNet {

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

    // --- gamenet_state.cpp: the manager's small members (0x00532AA0-0x005333F0) ---
    // 0x00532AA0 / 0x00532AC0: how the player joins (+0x3C: 0 to play, 1 to watch - IsObserving). Setting it clears
    // the join-by-address flag (0x007CCA88, TryJoinServerIPSuccess).
    void SetJoinMode(int mode);
    int GetJoinMode();
    // 0x00532AD0: +0x40 (1: the task at +0xF4 is removed when the player list is sent - 0x0056B890).
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
    // 0x00532E40 / 0x00532E50 / 0x00532E60 / 0x00532E70 / 0x00532E80 / 0x00532E90: the three words at +0x19C,
    // +0x1A0, +0x1A4 (the server's public address and port and its private port - the join's message 0x4F).
    uint32_t GetPublicIp();
    uint32_t GetPublicPort();
    uint32_t GetPrivatePort();
    void SetPublicIp(uint32_t value);
    void SetPublicPort(uint32_t value);
    void SetPrivatePort(uint32_t value);
    // 0x00532EA0: the client app of local player `index` (+0x08, +0x0C).
    Net::App* GetClient(int index);
    // 0x00532EB0: the LAN lobby's client app (+0x2C).
    Net::App* GetMatchClient();
    // 0x00532EC0: +0x1CC.
    uint32_t GetPlayerInfoFlags();
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
    // 0x00533250: tell the server the level is loaded for the current game (message 0x59, the game's number) and
    // note the number (+0x39).
    void SendReady();
    // 0x005332A0: ready has been sent for the current game.
    bool ReadySent();
    // 0x005332B0 / 0x005332C0 / 0x005332D0: every player is ready (+0x3A, the server's message 0x5A).
    bool AllPlayersReady();
    void SetAllPlayersReady();
    void ClearAllPlayersReady();
    // 0x005332E0 / 0x005332F0: the cheating notice has been shown (+0x3B).
    bool CheatingNoted();
    void NoteCheating();
    // 0x00533180 / 0x00533190 / 0x005331A0: the last player (+0x1D0) / team (+0x1D4) announced as the leader.
    void SetLastLeader(void* player);
    void* GetLastLeader();
    int GetLastLeadingTeam();
    // 0x005331B0 / 0x005331C0: +0x190.
    void SetServerListState(uint32_t value);
    uint32_t GetServerListState();
    // 0x005331D0 / 0x005331E0: the final scores have come (+0x44).
    void SetFinalScoresReceived(bool received);
    bool FinalScoresReceived();
    // 0x005331F0 / 0x00533200: the join's progress (+0x198: 2 joining, 6 loading, 8 playing, 10 leaving...).
    void SetJoinState(int state);
    int GetJoinState();
    // 0x00533210 / 0x00533220: the crown of king of the hill (+0x6C) / forgotten.
    void* GetCrown();
    void ClearCrown();
    // 0x00533230: the crown's restart point (+0x68), a signed byte.
    void SetCrownSpawnPoint(char point);
    // 0x00533240: +0x9950.
    void SetLobbyId(uint32_t id);
    // 0x00533300: message 0x26 - the acknowledgement id the server sent (+0x188, message 0x25) - to the server through
    // each local player's client (it makes the player's connection ready again).
    void SendAck();
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

    uint8_t m_flags;          // +0x00 (bit 0: on the server; bits 2-3: network mode)
    uint8_t m_unknown1[3];
    Net::App* m_server;       // +0x04
    Net::App* m_client;       // +0x08
    Net::App* m_client2;      // +0x0C (the second local player's)
    uint8_t m_unknown10[0x1C];
    Net::App* m_matchClient;  // +0x2C the LAN lobby's client
    uint32_t m_gameMode;      // +0x30
    uint32_t m_level; // the level's load script (+0x34)
};

} // namespace GameNet
