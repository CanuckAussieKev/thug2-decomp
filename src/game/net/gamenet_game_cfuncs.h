#pragma once

// The network game's script commands (CFunctions, cdecl (params, script); 0x0053B450-0x0053D900): capture the flag
// (took / captured / retrieved / has), the teams, surveyor mode, the host mode, kicking and banning, cancelling a
// join. They act on the network manager (GameNet::Manager, the singleton 0x007CCE14).

namespace Script {
class CScript;
class CStruct;
} // namespace Script

namespace GameNet {

// 0x0053B450: the player of object id <player> took team <flag_team>'s flag: message 0x71 (its id and the team) to
// every remote player's connection through the server; the local player is told directly (0x00543F00). True.
bool __cdecl CFunc_PlayerTookFlag(Script::CStruct* params, Script::CScript* script);
// 0x0053B5A0: the player of object id <player> captured the flag it carries (unless its flag bit 0x8000): message 0x72
// (its id and the flag's team) to the remote players; on this machine its skater's captures go up by one and it's told
// directly (0x00543A90). True.
bool __cdecl CFunc_PlayerCapturedFlag(Script::CStruct* params, Script::CScript* script);
// 0x0053B710: the player "player" retrieved flag "flag_team" - sent as a capture (message 0x72, 0x00543A90 for the
// local player): the original's own mix-up, kept. True.
bool __cdecl CFunc_PlayerRetrievedFlag(Script::CStruct* params, Script::CScript* script);
// 0x0053B860: whether player "player" carries a flag (true for an unknown player).
bool __cdecl CFunc_PlayerHasFlag(Script::CStruct* params, Script::CScript* script);
// 0x0053B8A0: <num_members> = the players on "team". True.
bool __cdecl CFunc_GetNumPlayersOnTeam(Script::CStruct* params, Script::CScript* script);
// 0x0053B8F0: <team> = the team of the player of object id <objId> (nothing for an unknown one). True.
bool __cdecl CFunc_GetPlayersTeam(Script::CStruct* params, Script::CScript* script);
// 0x0053B940: out of surveyor mode: the camera back on the local player, the surveyor task and the observer's input
// handler deleted, the surveying flag (0x10000) off. False without a local player.
bool __cdecl CFunc_ExitSurveyorMode(Script::CStruct* params, Script::CScript* script);
// 0x0053D400: the host mode (the unnamed integer: 0 play, 1 auto-serve, 2 first come first served). True.
bool __cdecl CFunc_SetHostMode(Script::CStruct* params, Script::CScript* script);
// 0x0053D440: a network game entered: the server reported to GameSpy (with the internet flag 0x007CCA82: qr2 set up),
// two of the manager's tasks (+0xD4, +0xC8) started, the flags 0x007CCDF8 and +0x46 set. True.
bool __cdecl CFunc_EnteredNetworkGame(Script::CStruct* params, Script::CScript* script);
// 0x0053D490 / 0x0053D5E0: kick / ban the player marked for it (flag 0x400): on the server it's dropped (0x0053CB70,
// reason 4 kicked / 3 banned) and its skater removed; on a client message 0x61 asks the server (0 kick / 1 ban, the
// player's place in the list, its name). Ban allocates from the network heap. True.
bool __cdecl CFunc_KickPlayer(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_BanPlayer(Script::CStruct* params, Script::CScript* script);
// 0x0053D760: the join cancelled (0x0053C6E0), the join state 10. True.
bool __cdecl CFunc_CancelJoinServer(Script::CStruct* params, Script::CScript* script);
// 0x0053D780: into surveyor mode (not when the local player observes or surveys already - false): the surveyor task
// (0x0053BED0) on the logic tasks, an input handler for controller 0 (0x00536890), the surveying flag. True.
bool __cdecl CFunc_EnterSurveyorMode(Script::CStruct* params, Script::CScript* script);
// 0x0053EA20: the players not ready dropped (Manager::DropPlayersNotReady). True.
bool __cdecl CFunc_DropPendingPlayers(Script::CStruct* params, Script::CScript* script);

} // namespace GameNet
