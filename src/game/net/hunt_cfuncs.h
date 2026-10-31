#pragma once

#include <cstdint>

namespace Scripts {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) of the network game in 0x005B1EE0-0x005B5320: the hunt (players placing objects for
// the others to find - the positions kept in the player's PlayerRecord, announced and reported as messages), entering
// vehicles, projectiles, the lobby's keyboard chat. All are cdecl (params, script) and return whether the script's
// `if` should be taken.
namespace ScriptCommands {

bool __cdecl CFunc_PlaceObject(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_AnnounceHuntStarted(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_AnnounceNeedToPlaceObjects(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_AtLeastOneObjectPlaced(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_ReportObjectPickup(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_PlacedLessThan(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_BroadcastEnterVehicle(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_LobbyCheckKeyboard(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_BroadcastObjectPlacements(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_ReportObjectPlacement(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_BroadcastProjectile(Scripts::CStruct* params, Scripts::CScript* script);

} // namespace ScriptCommands
