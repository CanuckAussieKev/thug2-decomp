#pragma once

// Based on PartyMod (c) 2024 PARTYMANX, MIT - see LICENSES/partymod.txt

// The online service's host names: GameSpy's servers (*.gamespy.com) replaced by [Miscellaneous] OnlineDomain
// in partymod.ini (openspy.net by default), as the PC port has it.
namespace Net {

// Make the host names for the domain and write them into the GameSpy SDK's code (once, at start-up).
void SetUpOnlineHosts();

// "gamestats.<OnlineDomain>" once SetUpOnlineHosts has run; null before (RetrieveTopStats then uses the
// original's gamestats.gamespy.com).
const char*& GamestatsHost();

} // namespace Net
