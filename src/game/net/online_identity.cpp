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
// Until the GameSpy SDK is linked (library code: not decompiled) and the game code above
// is source, this writes the key over the data (only when the setting differs from the game's own; the name needs no
// write). tools/check_code_writes.ts checks none lands in a rewritten function.

#include "game/net/online_identity.h"

#include <cctype>
#include <cstdint>
#include <cstring>

#include "game/sys/config.h"
#include "game/sys/debug_log.h"
#include "hook/hook.h"

namespace Net {

namespace {

constexpr const char* kOwnName = "thps6pc";
constexpr const char* kOwnKey = "AdLWaZ";
constexpr size_t kMaxName = 31;
constexpr size_t kKeyLength = 6;

// 6 key bytes at `address`, stored bitwise-NOT or not.
struct KeyUse {
    uint32_t address;
    bool inverted;
};
constexpr KeyUse kKeyUses[kGameKeySites] = {
    {0x0067F120, true},  // qr2_init's key
    {0x0067F154, true},  // peerSetTitle's keys
    {0x0064D58C, false}, // the stats SDK's key (its NUL follows)
};

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

} // namespace

const char* GameName()
{
    static const char* name = ReadGameName();
    return name;
}

bool WriteOnlineIdentity(const char* key)
{
    if (key && std::strlen(key) != kKeyLength)
        return false;
    if (key) {
        for (const KeyUse& use : kKeyUses) {
            uint8_t bytes[kKeyLength];
            for (size_t i = 0; i < kKeyLength; ++i)
                bytes[i] = static_cast<uint8_t>(use.inverted ? ~key[i] : key[i]);
            WriteGameCode(use.address, bytes, sizeof(bytes));
        }
    }
    return true;
}

void GameKeyAt(int site, char out[7])
{
    const KeyUse& use = kKeyUses[site];
    const auto* bytes = reinterpret_cast<const uint8_t*>(use.address);
    for (size_t i = 0; i < kKeyLength; ++i)
        out[i] = static_cast<char>(use.inverted ? static_cast<uint8_t>(~bytes[i]) : bytes[i]);
    out[kKeyLength] = '\0';
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
    WriteOnlineIdentity(useKey);
    DebugLog::Printf(DebugLog::Level::kInfo, "ONLINE IDENTITY: game name %s%s, key %s\n", name,
                     std::strcmp(name, kOwnName) == 0 ? "" : " (OnlineGameName)",
                     useKey ? "OnlineGameKey (written)" : "the game's own");
}

} // namespace Net
