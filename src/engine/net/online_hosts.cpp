// The online service's host names.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md
//
// The GameSpy SDK is linked (src/engine/net/gamespy.h): the domain is given to it (GameSpy::set_online_domain sets the
// SDK's host-name variables).

#include "engine/net/online_hosts.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "engine/net/gamespy.h"
#include "platform/platform_config.h"
#include "platform/debug_log.h"

namespace net {

namespace {

const char* s_gamestats_host = nullptr;

// `format` with the domain in place of its %s (other % signs are doubled in `format`). Never freed.
char* get_host_name(const char* format, const char* domain)
{
    const size_t size = std::strlen(format) + std::strlen(domain) + 1; // (enough)
    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc): the C runtime's allocator, as the library / original wants
    auto* name = static_cast<char*>(std::malloc(size));
    std::snprintf(name, size, format, domain);
    debug_log::printf(debug_log::StageMap::kTrace, "ALLOC'D STRING %s\n", name);
    return name;
}

} // namespace

const char*& gamestats_host() { return s_gamestats_host; }

void set_up_online_hosts()
{
    char domain[256];
    build_info::fetch_text(build_info::kMiscellaneous, "OnlineDomain", "openspy.net", domain, sizeof(domain));

    // gamestats: also the game's own top stats request (NetLink::RetrieveTopStats reads gamestats_host)
    const char* gamestats = get_host_name("gamestats.%s", domain);
    s_gamestats_host = gamestats;
    game_spy::set_online_domain(domain);
}

} // namespace net
