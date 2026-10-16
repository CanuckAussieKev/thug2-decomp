// Online top stats.
// Based on PartyMod (c) 2024 PARTYMANX, MIT - see LICENSES/partymod.txt (the PC port's changes in it).

#include "game/net/stats.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/net/online_hosts.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Net {

namespace {

// A request to the stats server (0x118 bytes, filled on the stack and handed to 0x0054A910).
struct StatsRequest {
    uint32_t timeout; // 0x25800 (ms)
    char url[256];
    uint32_t callback; // 0x0054DB20
    uint32_t unknown108;
    uint32_t unknown10C; // which reply: ratings or scores
    uint32_t unknown110;
    uint32_t unknown114;
};
static_assert(sizeof(StatsRequest) == 0x118, "built in the original's stack frame");

// The top-score files, 32 bytes each (0x0067F2A0): hs_at, bc_at, hs_mo, bc_mo, ratings.
const char* StatsFile(int index) { return reinterpret_cast<const char*>(0x0067F2A0 + 32 * index); }

} // namespace

// 0x0054DCB0
bool __cdecl RetrieveTopStats(Script::CStruct* params, Script::CScript*)
{
    uint8_t* manager = GameVariable<uint8_t*>(0x007CCE14);
    uint8_t* stats = *reinterpret_cast<uint8_t**>(manager + 0x1C);
    int& current = *reinterpret_cast<int*>(stats + 0x90);
    int index = 0;
    if (params->GetInteger(0x7360C9EF, &index, 0))
        current = index;
    else
        ++current;
    StatsRequest request;
    // PC port: gamestats.<OnlineDomain> in place of gamestats.gamespy.com (0x0064D670; PartyMod's DWord at
    // 0x0054DD14)
    const char* host = GamestatsHost() ? GamestatsHost() : reinterpret_cast<const char*>(0x0064D670);
    std::snprintf(request.url, sizeof(request.url), "http://%s/thug2pc/%s", host, StatsFile(current));
    request.unknown114 = 0x1C049404;
    request.unknown108 = 0x1C049404;
    request.unknown110 = 0x74E0CD35;
    request.timeout = 0x25800;
    request.callback = 0x0054DB20;
    request.unknown10C = current == 4 ? 0x7346E8E7 : 0xD932B07B;
    GameFunction<void(__thiscall*)(void*, StatsRequest*)>(0x0054A910)(*reinterpret_cast<void**>(manager + 0x14), &request);
    stats[0x1FC] = 0;
    return true;
}

} // namespace Net

REPLACE(0x0054DCB0, Net::RetrieveTopStats);
