// The online service's host names.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md
//
// The hooked build calls the exe's copy of the GameSpy SDK (library code, not decompiled - docs/library_ranges.md):
// its host names are there as the operands of the instructions that use them (and two data words), and this writes
// the names made for the domain over them (tools/check_code_writes.ts checks the addresses below stay outside
// rewritten functions). The standalone build links the SDK (src/game/net/gamespy.h): the domain is given to it
// (GameSpy::SetOnlineDomain sets the SDK's host-name variables), nothing is written.

#include "game/net/online_hosts.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game/net/gamespy.h"
#include "game/sys/config.h"
#include "game/sys/debug_log.h"
#include "hook/hook.h"

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

#ifndef THUG2_STANDALONE

// Where a name's address goes: an instruction at `address` whose 32-bit operand is `offset` bytes in (an offset
// of 0: a data word).
struct HostUse {
    uint32_t address;
    uint32_t offset;
};

void Use(const HostUse& use, const char* name)
{
    auto value = reinterpret_cast<uint32_t>(name);
    WriteGameCode(use.address + use.offset, &value, sizeof(value));
}

// The GameSpy SDK's uses of each host name (the original names in comments).
constexpr HostUse kGamestats[] = {{0x006025D8, 1}, {0x00602618, 1}, {0x00602650, 2}}; // gamestats.gamespy.com
constexpr HostUse kAvailable[] = {{0x005F86DD, 1}};                                    // %s.available.gamespy.com
constexpr HostUse kMaster[] = {{0x005FB03B, 1}};                                       // %s.master.gamespy.com
constexpr HostUse kNatNeg2[] = {{0x0068E380, 0}};                                      // natneg2.gamespy.com
constexpr HostUse kNatNeg1[] = {{0x0068E37C, 0}};                                      // natneg1.gamespy.com
constexpr HostUse kMotd[] = {{0x005FFD05, 1}};                                         // http://motd.gamespy.com/...
constexpr HostUse kPeerChat[] = {{0x00605A4C, 1}, {0x00605AAC, 1}, {0x00605B0E, 1}};  // peerchat.gamespy.com
constexpr HostUse kMasterServer[] = {{0x00612085, 1}};                                 // %s.ms%d.gamespy.com
constexpr HostUse kGpcm[] = {{0x00617339, 1}};                                         // gpcm.gamespy.com
constexpr HostUse kGpsp[] = {{0x006182BD, 1}};                                         // gpsp.gamespy.com

template <size_t N>
void UseAll(const HostUse (&uses)[N], const char* name)
{
    for (const HostUse& use : uses)
        Use(use, name);
}
#endif

} // namespace

const char*& GamestatsHost() { return s_gamestatsHost; }

void SetUpOnlineHosts()
{
    char domain[256];
    Config::GetString(Config::kMiscellaneous, "OnlineDomain", "openspy.net", domain, sizeof(domain));

    // gamestats: also the game's own top stats request (Net::RetrieveTopStats reads GamestatsHost)
    char* gamestats = HostName("gamestats.%s", domain);
    s_gamestatsHost = gamestats;
#ifdef THUG2_STANDALONE
    GameSpy::SetOnlineDomain(domain);
#else
    UseAll(kGamestats, gamestats);
    UseAll(kAvailable, HostName("%%s.available.%s", domain));
    UseAll(kMaster, HostName("%%s.master.%s", domain));
    UseAll(kNatNeg2, HostName("natneg2.%s", domain));
    UseAll(kNatNeg1, HostName("natneg1.%s", domain));
    UseAll(kMotd, HostName("http://motd.%s/motd/motd.asp?userid=%%d&productid=%%d&versionuniqueid=%%s&distid=%%d&"
                           "uniqueid=%%s&gamename=%%s",
                           domain));
    UseAll(kPeerChat, HostName("peerchat.%s", domain));
    UseAll(kMasterServer, HostName("%%s.ms%%d.%s", domain));
    UseAll(kGpcm, HostName("gpcm.%s", domain));
    UseAll(kGpsp, HostName("gpsp.%s", domain));
#endif
}

} // namespace Net
