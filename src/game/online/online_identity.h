#pragma once

#include <cstddef>

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
namespace net {

// THUG2 PC's ids with GameSpy (whatever the game name): its game id (the CD-key SDK) and its product id (the
// presence / profile SDK and the usage report).
constexpr int kGameSpyGameId = 1003;
constexpr int kGameSpyProductId = 10368; // 0x2880
// The stack of the threads that run the GameSpy SDK's blocking calls (the profile, rooms, service, stats and vault
// connections).
constexpr std::size_t kSdkThreadStackBytes = 0x8000;

// The game name: OnlineGameName, or "thps6pc". Read once (it's needed before the PC set-up: WinMain's availability
// check).
const char* get_game_name();

// The secret key: OnlineGameKey, or "AdLWaZ". Read once (given to the SDK).
const char* game_key();

// Report the identity when the settings differ from the game's own (once, at start-up).
void set_up_online_identity();

} // namespace net
