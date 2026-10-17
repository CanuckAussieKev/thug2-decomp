#pragma once

// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

// The online service's host names: GameSpy's servers (*.gamespy.com) replaced by [Miscellaneous] OnlineDomain
// in thug2-decomp.ini (openspy.net by default), as PartyMod has it.
namespace Net {

// Make the host names for the domain and write them into the GameSpy SDK's code (once, at start-up).
void SetUpOnlineHosts();

// "gamestats.<OnlineDomain>" once SetUpOnlineHosts has run; null before (RetrieveTopStats then uses the
// original's gamestats.gamespy.com).
const char*& GamestatsHost();

} // namespace Net
