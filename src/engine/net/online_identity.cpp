// The game's online identity: its GameSpy game name and secret key (see online_identity.h).
//
// Where the game passes them (THUG2 PC, the same in the reference build and THUG2.exe):
//   - the game name "thps6pc" (plain text at 0x0064B1CC) is pushed as an address by: WinMain's availability check,
//     qr2_init (0x0053BE86), the qr2 / peer server key callbacks' "gamename" key (0x0053BA1F, 0x0053F936) and the
//     stats SDK's copy into 0x007D7A78 (RankingsManager::log_in_thread) and peerSetTitle's title and server-browsing name
//     (cfunc_lobby_connect, rooms.cpp: once pushed at 0x00542F8B / 0x00542FA0) - all source now, using GameName()
//     itself: nothing of the name is written into the exe any more.
//   - the secret key "AdLWaZ" is stored twice bitwise-NOT (6 bytes each, decoded onto the stack at the call): at
//     0x0067F120 for qr2_init, at 0x0067F154 for peerSetTitle (both keys); and once plain at 0x0064D58C for the
//     stats SDK (copied into 0x007D7B78 by RankingsManager::log_in_thread from there).
// Not changed here (THUGPRO doesn't either): the online service task's own availability check with "mototrax"
// (0x0064CDF0, NetLink::service_task - the SDK keeps the last checked name for NAT negotiation, GP logins and the
// MOTD URL), the product id 10368 (the MOTD), the CD key game id 1003, GP's "10thug211" location strings and the
// stats path "thug2pc/%s".
//
// The SDK is linked (src/engine/net/gamespy.h): its callers pass game_key() to it.

#include "engine/net/online_identity.h"

#include <cctype>
#include <cstddef>
#include <cstring>
#include <string_view>
#include <array>

#include "platform/platform_config.h"
#include "platform/debug_log.h"
#include "engine/core/checked_index.h"

namespace net {

namespace {
constexpr size_t kNameSettingBytes = 64;
constexpr size_t kKeySettingBytes = 64;

constexpr const char* kOwnName = "thps6pc";
constexpr const char* kOwnKey = "AdLWaZ";
constexpr size_t kMaxName = 31;
constexpr size_t kKeyLength = 6;

bool valid_name(const char* name)
{
    const size_t length = std::strlen(name);
    if (length == 0 || length > kMaxName) {
        return false;
    }
    for (const char c : std::string_view(name)) {
        if (std::isalnum(static_cast<unsigned char>(c)) == 0) {
            return false;
        }
    }
    return true;
}

bool valid_key(const char* key)
{
    if (std::strlen(key) != kKeyLength) {
        return false;
    }
    for (const char c : std::string_view(key)) {
        if (std::isgraph(static_cast<unsigned char>(c)) == 0) {
            return false;
        }
    }
    return true;
}

std::array<char, kNameSettingBytes>& name_setting()
{
    constexpr size_t kNameSettingSize = 64;
    static std::array<char, kNameSettingSize> name_setting{};
    return name_setting;
}

// OnlineGameName, or the game's own (an invalid one too: set_up_online_identity reports it); never freed.
const char* read_game_name()
{
    build_info::prepare_all(); // (WinMain needs the name before the PC set-up has read the settings)
    build_info::fetch_text(build_info::kMiscellaneous, "OnlineGameName", "", name_setting().data(), sizeof(name_setting()));
    return core::at(name_setting(), 0) != '\0' && valid_name(name_setting().data()) ? name_setting().data() : kOwnName;
}

std::array<char, kKeySettingBytes>& key_setting()
{
    constexpr size_t kKeySettingSize = 64;
    static std::array<char, kKeySettingSize> key_setting{};
    return key_setting;
}

// OnlineGameKey, or the game's own (an invalid one too); never freed.
const char* read_game_key()
{
    build_info::prepare_all();
    build_info::fetch_text(build_info::kMiscellaneous, "OnlineGameKey", "", key_setting().data(), sizeof(key_setting()));
    return core::at(key_setting(), 0) != '\0' && valid_key(key_setting().data()) ? key_setting().data() : kOwnKey;
}

} // namespace

const char* get_game_name()
{
    static const char* name = read_game_name();
    return name;
}

const char* game_key()
{
    static const char* key = read_game_key();
    return key;
}

void set_up_online_identity()
{
    const char* name = get_game_name();
    if (core::at(name_setting(), 0) != '\0' && name != name_setting().data()) {
        debug_log::printf(debug_log::StageMap::kWarn, "OnlineGameName \"%s\" ignored (1-31 letters or digits)\n",
                          name_setting().data());
    }
    constexpr size_t kKeySize = 64;
    std::array<char, kKeySize> key{};
    build_info::fetch_text(build_info::kMiscellaneous, "OnlineGameKey", "", key.data(), sizeof(key));
    const char* use_key = nullptr;
    if (core::at(key, 0) != '\0') {
        if (valid_key(key.data())) {
            use_key = key.data();
        } else {
            debug_log::printf(debug_log::StageMap::kWarn, "OnlineGameKey \"%s\" ignored (6 characters)\n", key.data());
        }
    }
    if (use_key && std::strcmp(use_key, kOwnKey) == 0) {
        use_key = nullptr;
    }
    debug_log::printf(debug_log::StageMap::kInfo, "ONLINE IDENTITY: game name %s%s, key %s\n", name,
                      std::strcmp(name, kOwnName) == 0 ? "" : " (OnlineGameName)",
                      use_key ? "OnlineGameKey" : "the game's own");
}

} // namespace net
