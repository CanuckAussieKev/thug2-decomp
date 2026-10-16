#pragma once

namespace Script {
class CScript;
class CStruct;
}

namespace Net {

// 0x0054DCB0: the RetrieveTopStats script command: asks the stats server for one of the top-score files
// (index = the script's integer 0x7360C9EF, else the next one): http://<gamestats host>/thug2pc/<file>.
// PartyMod's host is gamestats.<OnlineDomain> (partymod.ini) instead of gamestats.gamespy.com. Returns true.
bool __cdecl RetrieveTopStats(Script::CStruct* params, Script::CScript* script);

} // namespace Net
