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

// The secret key: OnlineGameKey, or "AdLWaZ". Read once. (The standalone build gives it to the SDK; the hooked build's
// SDK callers read the copies SetUpOnlineIdentity writes.)
const char* GameKey();

// Write the identity into the GameSpy SDK's callers when the settings differ from the game's own (once, at
// start-up; nothing is written for the defaults). In the standalone build it only reports the identity.
void SetUpOnlineIdentity();

#ifndef THUG2_STANDALONE

// What SetUpOnlineIdentity writes: `key` (exactly 6 characters; null = leave the key) - the name needs no write: every
// use of it is source and asks GameName(). Returns false (and writes nothing) for a key of another length.
bool WriteOnlineIdentity(const char* key);

// The key the game's code uses now (read back from where WriteOnlineIdentity writes; for the self-tests): 0 qr2,
// 1 peer, 2 stats (7 bytes out, decoded).
void GameKeyAt(int site, char out[7]);
constexpr int kGameKeySites = 3;
#endif

} // namespace Net
