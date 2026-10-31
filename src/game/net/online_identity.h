#pragma once

// The game's online identity: the GameSpy game name and secret key it logs into the online service with
// (thug2-decomp.ini [Miscellaneous] OnlineGameName / OnlineGameKey). Empty (the default): THUG2 PC's own,
// "thps6pc" / "AdLWaZ" - on OpenSpy its lobby is "Beenox".
//
// THUGPRO (the same exe plus its own DLL) logs in as "thugpro" with THUG2's key unchanged - on OpenSpy its lobbies are
// "General" and "Newbies". To see them:
//   [Miscellaneous]
//   OnlineGameName=thugpro
//   ; OnlineGameKey=   (leave empty: THUGPRO keeps THUG2's AdLWaZ)
// Joining THUGPRO players' sessions may not work: their scripts and DLL differ from this game's.
// Test online only by hand; tools and agents never go online.
namespace Net {

// The game name: OnlineGameName, or "thps6pc". Read once (it's needed before the PC set-up: WinMain's availability
// check).
const char* GameName();

// The secret key: OnlineGameKey, or "AdLWaZ". Read once (given to the SDK).
const char* GameKey();

// Report the identity when the settings differ from the game's own (once, at start-up).
void SetUpOnlineIdentity();


} // namespace Net
