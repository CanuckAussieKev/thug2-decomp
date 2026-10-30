#include "hook/game_global.h"

#include <cstdio>
#include <vector>

namespace {

std::vector<GlobalStartValue>& Globals()
{
    static std::vector<GlobalStartValue> globals; // (built by static initialisers: constructed on first use)
    return globals;
}

} // namespace

void RegisterGlobal(const GlobalStartValue& g)
{
    Globals().push_back(g);
}

int CheckGlobalStartValues(int* checked, int* total)
{
    int differ = 0;
    *checked = 0;
    *total = static_cast<int>(Globals().size());
    for (const GlobalStartValue& g : Globals()) {
        if (!g.value)
            continue;
        ++*checked;
        const auto* ours = static_cast<const uint8_t*>(g.value);
        const auto* original = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(g.address));
        if (std::memcmp(ours, original, g.size) == 0)
            continue;
        ++differ;
        uint32_t at = 0;
        while (at < g.size && ours[at] == original[at])
            ++at;
        std::printf("GLOBAL START VALUE 0x%08X %s (%s): byte %u of %u is %02X, the original's %02X\n", g.address, g.name,
                    g.file, at, g.size, ours[at], original[at]);
    }
    return differ;
}
