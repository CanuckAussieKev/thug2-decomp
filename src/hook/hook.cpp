#include "hook/hook.h"

#include <windows.h>

#include <vector>

#include "hook/log.h"

namespace {

// Function-local so it exists before any REPLACE() registrar in another file runs.
std::vector<Replacement>& Registry()
{
    static std::vector<Replacement> registry;
    return registry;
}

bool WriteJump(uint32_t from, void* to)
{
    auto* p = reinterpret_cast<uint8_t*>(from);
    DWORD old;
    if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old))
        return false;
    p[0] = 0xE9; // jmp rel32
    *reinterpret_cast<int32_t*>(p + 1) = static_cast<int32_t>(reinterpret_cast<uint32_t>(to) - (from + 5));
    VirtualProtect(p, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    return true;
}

} // namespace

void RegisterReplacement(const Replacement& r)
{
    Registry().push_back(r);
}

void InstallReplacements()
{
    int installed = 0;
    for (const Replacement& r : Registry()) {
        if (WriteJump(r.address, r.function)) {
            ++installed;
            Log("replaced 0x%08X %s", r.address, r.name);
        } else {
            Log("FAILED to replace 0x%08X %s", r.address, r.name);
        }
    }
    Log("%d of %u replacements installed", installed, static_cast<unsigned>(Registry().size()));
}
