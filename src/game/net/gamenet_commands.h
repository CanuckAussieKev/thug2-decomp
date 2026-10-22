#pragma once

#include <cstdint>

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// The network manager's script commands (CFunctions, GameNet::Manager, 0x00535DA0-0x00539570): capture the flag,
// the game's state, the join mode, the lobby and the message of the day, first-come-first-served requests to the server
// (start, level, pro set, goals), teams, the crown, starting and ending network games, the player list
// (gamenet_commands.cpp, ...). All are cdecl (params, script) and return whether the script's `if` should be taken.
namespace GameNet {

// --- gamenet_commands.cpp: 0x00535DA0-0x00536460 ---
bool __cdecl CFunc_StartCTFGame(Script::CStruct* params, Script::CScript* script);           // 0x00535DA0
bool __cdecl CFunc_EndCTFGame(Script::CStruct* params, Script::CScript* script);             // 0x00535DC0
bool __cdecl CFunc_DisplayFlagBaseWarning(Script::CStruct* params, Script::CScript* script); // 0x00535DE0
bool __cdecl CFunc_NetworkGamePending(Script::CStruct* params, Script::CScript* script);     // 0x00535E20
bool __cdecl CFunc_MarkGameOver(Script::CStruct* params, Script::CScript* script);           // 0x00535E30
bool __cdecl CFunc_SetJoinMode(Script::CStruct* params, Script::CScript* script);            // 0x00535E40
bool __cdecl CFunc_GetNumTeams(Script::CStruct* params, Script::CScript* script);            // 0x00535E90
bool __cdecl CFunc_ConnectedToPeer(Script::CStruct* params, Script::CScript* script);        // 0x00535EB0
bool __cdecl CFunc_InGroupRoom(Script::CStruct* params, Script::CScript* script);            // 0x00535EE0
bool __cdecl CFunc_IsAutoServing(Script::CStruct* params, Script::CScript* script);          // 0x00535EF0
bool __cdecl CFunc_ChangeLevelPending(Script::CStruct* params, Script::CScript* script);     // 0x00535F20
bool __cdecl CFunc_CancelMotdDownload(Script::CStruct* params, Script::CScript* script);     // 0x00535F30
bool __cdecl CFunc_DownloadMotd(Script::CStruct* params, Script::CScript* script);           // 0x00535F40
bool __cdecl CFunc_ServicesAreAvailable(Script::CStruct* params, Script::CScript* script);   // 0x00535F50
bool __cdecl CFunc_AlreadyGotMotd(Script::CStruct* params, Script::CScript* script);         // 0x00535FA0
bool __cdecl CFunc_JoinServerComplete(Script::CStruct* params, Script::CScript* script);     // 0x00535FB0
bool __cdecl CFunc_JustStartedNetGame(Script::CStruct* params, Script::CScript* script);     // 0x00535FC0
bool __cdecl CFunc_FCFSRequestStartGame(Script::CStruct* params, Script::CScript* script);   // 0x00535FE0
bool __cdecl CFunc_FCFSRequestChangeLevel(Script::CStruct* params, Script::CScript* script); // 0x00536170
bool __cdecl CFunc_ReattemptJoinServer(Script::CStruct* params, Script::CScript* script);    // 0x00536240
bool __cdecl CFunc_FCFSRequestToggleProSet(Script::CStruct* params, Script::CScript* script); // 0x00536260
bool __cdecl CFunc_FCFSRequestToggleGoalSelection(Script::CStruct* params, Script::CScript* script); // 0x005362F0
bool __cdecl CFunc_ResetProSetFlags(Script::CStruct* params, Script::CScript* script);       // 0x00536360
bool __cdecl CFunc_HasSignedDisclaimer(Script::CStruct* params, Script::CScript* script);    // 0x00536460
bool __cdecl CFunc_FoundServers(Script::CStruct* params, Script::CScript* script);          // 0x00537100

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

} // namespace GameNet
