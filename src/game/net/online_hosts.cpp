// The online service's host names.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md
//
// The hooked build calls the exe's copy of the GameSpy SDK (library code, not decompiled - docs/library_ranges.md):
// its host names are there as the operands of the instructions that use them (and two data words), and this writes
// the names made for the domain over them (tools/check_code_writes.ts checks the addresses below stay outside
// rewritten functions). The standalone build links the SDK (src/game/net/gamespy.h): the domain is given to it
// (GameSpy::SetOnlineDomain sets the SDK's host-name variables), nothing is written.

#include "game/net/online_hosts.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game/net/gamespy.h"
#include "game/sys/config.h"
#include "game/sys/debug_log.h"

namespace Net {

namespace {

const char* s_gamestatsHost = nullptr;

// `format` with the domain in place of its %s (other % signs are doubled in `format`). Never freed.
char* HostName(const char* format, const char* domain)
{
    size_t size = std::strlen(format) + std::strlen(domain) + 1; // (enough)
    auto* name = static_cast<char*>(std::malloc(size));
    std::snprintf(name, size, format, domain);
    DebugLog::Printf(DebugLog::Level::kTrace, "ALLOC'D STRING %s\n", name);
    return name;
}


} // namespace

const char*& GamestatsHost() { return s_gamestatsHost; }

void SetUpOnlineHosts()
{
    char domain[256];
    Config::GetString(Config::kMiscellaneous, "OnlineDomain", "openspy.net", domain, sizeof(domain));

    // gamestats: also the game's own top stats request (Net::RetrieveTopStats reads GamestatsHost)
    char* gamestats = HostName("gamestats.%s", domain);
    s_gamestatsHost = gamestats;
    GameSpy::SetOnlineDomain(domain);
}

} // namespace Net
