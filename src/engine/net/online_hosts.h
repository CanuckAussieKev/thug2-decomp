#pragma once

// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

// The online service's host names: GameSpy's servers (*.gamespy.com) replaced by [Miscellaneous] OnlineDomain
// in thug2-decomp.ini (openspy.net by default), as PartyMod has it.
namespace net {

// Make the host names for the domain and write them into the GameSpy SDK's code (once, at start-up).
void set_up_online_hosts();

// "gamestats.<OnlineDomain>" once set_up_online_hosts has run; null before (RetrieveTopStats then uses the
// original's gamestats.gamespy.com).
const char*& gamestats_host();

} // namespace Net
