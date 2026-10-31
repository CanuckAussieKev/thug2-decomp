#pragma once

#include <cstdint>

namespace Scripts {
class CStruct;
class CScript;
} // namespace Script

// The network manager's script commands (CFunctions, OnlinePlay::Manager, 0x00535DA0-0x00539570): capture the flag,
// the game's state, the join mode, the lobby and the message of the day, first-come-first-served requests to the server
// (start, level, pro set, goals), teams, the crown, starting and ending network games, the player list
// (gamenet_commands.cpp, ...). All are cdecl (params, script) and return whether the script's `if` should be taken.
namespace OnlinePlay {

// --- gamenet_commands.cpp: 0x00535DA0-0x00536460 ---
bool __cdecl CFunc_StartCTFGame(Scripts::CStruct* params, Scripts::CScript* script);           // 0x00535DA0
bool __cdecl CFunc_EndCTFGame(Scripts::CStruct* params, Scripts::CScript* script);             // 0x00535DC0
bool __cdecl CFunc_DisplayFlagBaseWarning(Scripts::CStruct* params, Scripts::CScript* script); // 0x00535DE0
bool __cdecl CFunc_NetworkGamePending(Scripts::CStruct* params, Scripts::CScript* script);     // 0x00535E20
bool __cdecl CFunc_MarkGameOver(Scripts::CStruct* params, Scripts::CScript* script);           // 0x00535E30
bool __cdecl CFunc_SetJoinMode(Scripts::CStruct* params, Scripts::CScript* script);            // 0x00535E40
bool __cdecl CFunc_GetNumTeams(Scripts::CStruct* params, Scripts::CScript* script);            // 0x00535E90
bool __cdecl CFunc_ConnectedToPeer(Scripts::CStruct* params, Scripts::CScript* script);        // 0x00535EB0
bool __cdecl CFunc_InGroupRoom(Scripts::CStruct* params, Scripts::CScript* script);            // 0x00535EE0
bool __cdecl CFunc_IsAutoServing(Scripts::CStruct* params, Scripts::CScript* script);          // 0x00535EF0
bool __cdecl CFunc_ChangeLevelPending(Scripts::CStruct* params, Scripts::CScript* script);     // 0x00535F20
bool __cdecl CFunc_CancelMotdDownload(Scripts::CStruct* params, Scripts::CScript* script);     // 0x00535F30
bool __cdecl CFunc_DownloadMotd(Scripts::CStruct* params, Scripts::CScript* script);           // 0x00535F40
bool __cdecl CFunc_ServicesAreAvailable(Scripts::CStruct* params, Scripts::CScript* script);   // 0x00535F50
bool __cdecl CFunc_AlreadyGotMotd(Scripts::CStruct* params, Scripts::CScript* script);         // 0x00535FA0
bool __cdecl CFunc_JoinServerComplete(Scripts::CStruct* params, Scripts::CScript* script);     // 0x00535FB0
bool __cdecl CFunc_JustStartedNetGame(Scripts::CStruct* params, Scripts::CScript* script);     // 0x00535FC0
bool __cdecl CFunc_FCFSRequestStartGame(Scripts::CStruct* params, Scripts::CScript* script);   // 0x00535FE0
bool __cdecl CFunc_FCFSRequestChangeLevel(Scripts::CStruct* params, Scripts::CScript* script); // 0x00536170
bool __cdecl CFunc_ReattemptJoinServer(Scripts::CStruct* params, Scripts::CScript* script);    // 0x00536240
bool __cdecl CFunc_FCFSRequestToggleProSet(Scripts::CStruct* params, Scripts::CScript* script); // 0x00536260
bool __cdecl CFunc_FCFSRequestToggleGoalSelection(Scripts::CStruct* params, Scripts::CScript* script); // 0x005362F0
bool __cdecl CFunc_ResetProSetFlags(Scripts::CStruct* params, Scripts::CScript* script);       // 0x00536360
bool __cdecl CFunc_HasSignedDisclaimer(Scripts::CStruct* params, Scripts::CScript* script);    // 0x00536460
bool __cdecl CFunc_FoundServers(Scripts::CStruct* params, Scripts::CScript* script);          // 0x00537100

// --- gamenet_game_commands.cpp: 0x00538050-0x00539570 ---
bool __cdecl CFunc_TeamFlagTaken(Scripts::CStruct* params, Scripts::CScript* script);         // 0x00538050
bool __cdecl CFunc_SpawnCrown(Scripts::CStruct* params, Scripts::CScript* script);            // 0x00538170
bool __cdecl CFunc_StartNetworkGame(Scripts::CStruct* params, Scripts::CScript* script);      // 0x00538280
bool __cdecl CFunc_SendGameOverToObservers(Scripts::CStruct* params, Scripts::CScript* script); // 0x00538620
bool __cdecl CFunc_EndNetworkGame(Scripts::CStruct* params, Scripts::CScript* script);        // 0x00538720
bool __cdecl CFunc_GetMyTeam(Scripts::CStruct* params, Scripts::CScript* script);             // 0x005388C0
bool __cdecl CFunc_SetNumTeams(Scripts::CStruct* params, Scripts::CScript* script);           // 0x005388F0
bool __cdecl CFunc_JoinTeam(Scripts::CStruct* params, Scripts::CScript* script);              // 0x00538C90
bool __cdecl CFunc_IsHost(Scripts::CStruct* params, Scripts::CScript* script);                // 0x00538D70
bool __cdecl CFunc_AllPlayersAreReady(Scripts::CStruct* params, Scripts::CScript* script);    // 0x00538DC0
bool __cdecl CFunc_CreatePlayerOptions(Scripts::CStruct* params, Scripts::CScript* script);   // 0x00538E70
bool __cdecl CFunc_FillPlayerListMenu(Scripts::CStruct* params, Scripts::CScript* script);    // 0x00539080
bool __cdecl CFunc_RemovePlayer(Scripts::CStruct* params, Scripts::CScript* script);          // 0x00539470
bool __cdecl CFunc_CancelRemovePlayer(Scripts::CStruct* params, Scripts::CScript* script);    // 0x00539480
bool __cdecl CFunc_ToggleProSet(Scripts::CStruct* params, Scripts::CScript* script);          // 0x00539570

// The first-come-first-served start request (message 0x60, 52 bytes): the host's game options, from the network
// preferences (the game type from the set-up game's).
struct StartGameRequest {
    uint32_t gameType;        // +0x00 game_type
    uint32_t timeLimit;       // +0x04 time_limit
    uint32_t skillLevel;      // +0x08 skill_level
    uint32_t fireballLevel;   // +0x0C fireball_difficulty
    uint32_t playerCollision; // +0x10 player_collision
    uint32_t friendlyFire;    // +0x14 friendly_fire
    uint32_t unused18;        // +0x18 (never written: the original's stack)
    uint32_t targetScore;     // +0x1C target_score
    uint32_t stopAtZero;      // +0x20 stop_at_zero
    uint32_t scoreReset;      // +0x24 score_reset
    uint32_t ctfGameType;     // +0x28 ctf_game_type
    uint32_t goals;           // +0x2C goals
    uint32_t teamGraffiti;    // +0x30 team_graffiti_mode
};
static_assert(sizeof(StartGameRequest) == 0x34, "layout must match the original");

extern uint32_t g_netGameStartTime;

} // namespace OnlinePlay
