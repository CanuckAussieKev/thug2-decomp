// The online service's host names.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md
//
// The GameSpy SDK is linked (src/engine/net/gamespy.h): the domain is given to it (GameSpy::set_online_domain sets the
// SDK's host-name variables).

#include "engine/net/online_hosts.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>
#include <span>

#include "base/core/format.h"
#include "platform/gamespy.h"
#include "platform/platform_config.h"
#include "platform/debug_log.h"
#include <gsl/pointers>
#include "engine/memory/mem.h"
#include "platform/crt.h"

namespace net {

namespace {

// `format` with the domain in place of its %s (other % signs are doubled in `format`). Never freed.
gsl::owner<char*> get_host_name(const char* format, const char* domain)
{
    const size_t size = std::strlen(format) + std::strlen(domain) + 1; // (enough)
    gsl::owner<void*> block = crt::alloc_block(size); // (the C runtime's: the library frees it)
    gsl::owner<char*> name = memory::adopt<char*>(block);
    text::format_data_to(std::span(name, size), format, domain);
    debug_log::printf(debug_log::StageMap::kTrace, "ALLOC'D STRING %s\n", name);
    return name;
}

} // namespace

const char*& gamestats_host()
{
    static const char* gamestats_host{nullptr};
    return gamestats_host;
}

void set_up_online_hosts()
{
    constexpr size_t kDomainSize = 256;
    std::array<char, kDomainSize> domain{};
    build_info::fetch_text(build_info::kMiscellaneous, "OnlineDomain", "openspy.net", domain.data(), sizeof(domain));

    // gamestats: also the game's own top stats request (NetLink::RetrieveTopStats reads gamestats_host)
    const char* gamestats = get_host_name("gamestats.%s", domain.data());
    gamestats_host() = gamestats;
    game_spy::set_online_domain(domain.data());
}

} // namespace net
