// The game's online identity: its GameSpy game name and secret key (see online_identity.h).
//
// Where the game passes them (THUG2 PC, the same in the reference build and THUG2.exe):
//   - the game name "thps6pc" (plain text at 0x0064B1CC) is pushed as an address by: WinMain's availability check,
//     qr2_init (0x0053BE86), the qr2 / peer server key callbacks' "gamename" key (0x0053BA1F, 0x0053F936) and the
//     stats SDK's copy into 0x007D7A78 (StatsMan::LogInThread) and peerSetTitle's title and server-browsing name
//     (CFunc_LobbyConnect, lobby.cpp: once pushed at 0x00542F8B / 0x00542FA0) - all source now, using GameName()
//     itself: nothing of the name is written into the exe any more.
//   - the secret key "AdLWaZ" is stored twice bitwise-NOT (6 bytes each, decoded onto the stack at the call): at
//     0x0067F120 for qr2_init, at 0x0067F154 for peerSetTitle (both keys); and once plain at 0x0064D58C for the
//     stats SDK (copied into 0x007D7B78 by StatsMan::LogInThread from there).
// Not changed here (THUGPRO doesn't either): the online service task's own availability check with "mototrax"
// (0x0064CDF0, Net::ServiceTask - the SDK keeps the last checked name for NAT negotiation, GP logins and the
// MOTD URL), the product id 10368 (the MOTD), the CD key game id 1003, GP's "10thug211" location strings and the
// stats path "thug2pc/%s".
//
// The SDK is linked (src/game/net/gamespy.h): its callers pass GameKey() to it.

#include "game/net/online_identity.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "game/sys/config.h"
#include "game/sys/debug_log.h"

namespace Net {

namespace {

constexpr const char* kOwnName = "thps6pc";
constexpr const char* kOwnKey = "AdLWaZ";
constexpr size_t kMaxName = 31;
constexpr size_t kKeyLength = 6;


bool ValidName(const char* name)
{
    size_t length = std::strlen(name);
    if (length == 0 || length > kMaxName)
        return false;
    for (size_t i = 0; i < length; ++i)
        if (!std::isalnum(static_cast<unsigned char>(name[i])))
            return false;
    return true;
}

bool ValidKey(const char* key)
{
    if (std::strlen(key) != kKeyLength)
        return false;
    for (size_t i = 0; i < kKeyLength; ++i)
        if (!std::isgraph(static_cast<unsigned char>(key[i])))
            return false;
    return true;
}

char s_nameSetting[64];

// OnlineGameName, or the game's own (an invalid one too: SetUpOnlineIdentity reports it); never freed.
const char* ReadGameName()
{
    Config::Init(); // (WinMain needs the name before the PC set-up has read the settings)
    Config::GetString(Config::kMiscellaneous, "OnlineGameName", "", s_nameSetting, sizeof(s_nameSetting));
    return s_nameSetting[0] != '\0' && ValidName(s_nameSetting) ? s_nameSetting : kOwnName;
}

char s_keySetting[64];

// OnlineGameKey, or the game's own (an invalid one too); never freed.
const char* ReadGameKey()
{
    Config::Init();
    Config::GetString(Config::kMiscellaneous, "OnlineGameKey", "", s_keySetting, sizeof(s_keySetting));
    return s_keySetting[0] != '\0' && ValidKey(s_keySetting) ? s_keySetting : kOwnKey;
}

} // namespace

const char* GameName()
{
    static const char* name = ReadGameName();
    return name;
}

const char* GameKey()
{
    static const char* key = ReadGameKey();
    return key;
}


void SetUpOnlineIdentity()
{
    const char* name = GameName();
    if (s_nameSetting[0] != '\0' && name != s_nameSetting)
        DebugLog::Printf(DebugLog::Level::kWarn, "OnlineGameName \"%s\" ignored (1-31 letters or digits)\n",
                         s_nameSetting);
    char key[64];
    Config::GetString(Config::kMiscellaneous, "OnlineGameKey", "", key, sizeof(key));
    const char* useKey = nullptr;
    if (key[0] != '\0') {
        if (ValidKey(key))
            useKey = key;
        else
            DebugLog::Printf(DebugLog::Level::kWarn, "OnlineGameKey \"%s\" ignored (6 characters)\n", key);
    }
    if (useKey && std::strcmp(useKey, kOwnKey) == 0)
        useKey = nullptr;
    DebugLog::Printf(DebugLog::Level::kInfo, "ONLINE IDENTITY: game name %s%s, key %s\n", name,
                     std::strcmp(name, kOwnName) == 0 ? "" : " (OnlineGameName)",
                     useKey ? "OnlineGameKey" : "the game's own");
}

} // namespace Net
