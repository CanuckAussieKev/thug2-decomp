#pragma once

#include <cstdint>

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) of the network game in 0x005B1EE0-0x005B5320: the hunt (players placing objects for
// the others to find - the positions kept in the player's PlayerInfo, announced and reported as messages), entering
// vehicles, projectiles, the lobby's keyboard chat. All are cdecl (params, script) and return whether the script's
// `if` should be taken.
namespace CFuncs {

bool __cdecl CFunc_PlaceObject(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_AnnounceHuntStarted(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_AnnounceNeedToPlaceObjects(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_AtLeastOneObjectPlaced(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_ReportObjectPickup(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_PlacedLessThan(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_BroadcastEnterVehicle(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_LobbyCheckKeyboard(Script::CStruct* params, Script::CScript* script);

} // namespace CFuncs
