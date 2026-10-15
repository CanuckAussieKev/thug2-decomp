#include "hook/hook.h"

#include <windows.h>

#include <vector>

#include "hook/log.h"

namespace {

struct Installed {
    Replacement replacement;
    uint8_t original_bytes[5];
    bool active;
};

// Function-local so it exists before any REPLACE() registrar in another file runs.
std::vector<Installed>& Registry()
{
    static std::vector<Installed> registry;
    return registry;
}

Installed* Find(uint32_t address)
{
    for (Installed& entry : Registry())
        if (entry.replacement.address == address)
            return &entry;
    return nullptr;
}

bool WriteCode(uint32_t address, const uint8_t* bytes, size_t size)
{
    auto* p = reinterpret_cast<uint8_t*>(address);
    DWORD old;
    if (!VirtualProtect(p, size, PAGE_EXECUTE_READWRITE, &old))
        return false;
    std::memcpy(p, bytes, size);
    VirtualProtect(p, size, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, size);
    return true;
}

bool WriteJump(uint32_t from, void* to)
{
    uint8_t jump[5] = {0xE9}; // jmp rel32
    int32_t offset = static_cast<int32_t>(reinterpret_cast<uint32_t>(to) - (from + 5));
    std::memcpy(jump + 1, &offset, sizeof(offset));
    return WriteCode(from, jump, sizeof(jump));
}

} // namespace

void RegisterReplacement(const Replacement& r)
{
    Registry().push_back({r, {}, false});
}

void InstallReplacements()
{
    int installed = 0;
    for (Installed& entry : Registry()) {
        const Replacement& r = entry.replacement;
        std::memcpy(entry.original_bytes, reinterpret_cast<const void*>(r.address), sizeof(entry.original_bytes));
        if (WriteJump(r.address, r.function)) {
            entry.active = true;
            ++installed;
            Log("replaced 0x%08X %s", r.address, r.name);
        } else {
            Log("FAILED to replace 0x%08X %s", r.address, r.name);
        }
    }
    Log("%d of %u replacements installed", installed, static_cast<unsigned>(Registry().size()));
}

void RestoreOriginal(uint32_t address)
{
    Installed* entry = Find(address);
    if (entry && entry->active && WriteCode(address, entry->original_bytes, sizeof(entry->original_bytes)))
        entry->active = false;
}

OriginalCode::OriginalCode(uint32_t address) : m_address(address)
{
    Installed* entry = Find(address);
    if (entry && entry->active)
        WriteCode(address, entry->original_bytes, sizeof(entry->original_bytes));
}

OriginalCode::~OriginalCode()
{
    Installed* entry = Find(m_address);
    if (entry && entry->active)
        WriteJump(m_address, entry->replacement.function);
}
