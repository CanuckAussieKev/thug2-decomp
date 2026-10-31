#pragma once

namespace Scripts {
class CScript;
class CStruct;
}

namespace NetLink {

// 0x0054DCB0: the RetrieveTopStats script command: asks the stats server for one of the top-score files
// (index = the script's integer 0x7360C9EF, else the next one): http://<gamestats host>/thug2pc/<file>.
// PartyMod's host is gamestats.<OnlineDomain> (thug2-decomp.ini) instead of gamestats.gamespy.com. Returns true.
bool __cdecl RetrieveTopStats(Scripts::CStruct* params, Scripts::CScript* script);

} // namespace Net
