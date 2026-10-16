#pragma once

// The recording stand-in harness for self-tests (first written for Sys::Main's): every function the code under
// test calls is stood in for by recording code (a jump written over the start of the game's function and of our
// version of it alike), so the original and ours are compared call by call. Patches written for one run only
// (PartyMod's bytes into the original) go through RunPatch / EndRunPatches; register RemoveStandIns as the test's
// cleanup so nothing outlives a crashed test.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <vector>

#include "hook/game.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace StandInHarness {

// Our own functions may be reached through an incremental-linking jump: stand in for the function itself.
inline uint32_t Resolve(const void* code)
{
    auto* p = static_cast<const uint8_t*>(code);
    while (p[0] == 0xE9) {
        int32_t relative;
        std::memcpy(&relative, p + 1, 4);
        p += 5 + relative;
    }
    return reinterpret_cast<uint32_t>(p);
}
// The code address of a function or (non-virtual, single-inheritance) member function.
template <typename F>
inline uint32_t AddressOf(F function)
{
    static_assert(sizeof(F) == sizeof(void*), "a plain code pointer");
    void* code;
    std::memcpy(&code, &function, sizeof(code));
    return Resolve(code);
}

// Bytes overwritten for the length of a test (restored when it goes).
struct Patch {
    uint8_t* at;
    std::vector<uint8_t> saved;
    Patch(uint32_t address, const std::vector<uint8_t>& bytes)
        : at(reinterpret_cast<uint8_t*>(address)), saved(bytes.size())
    {
        DWORD old;
        VirtualProtect(at, bytes.size(), PAGE_EXECUTE_READWRITE, &old);
        std::memcpy(saved.data(), at, bytes.size());
        std::memcpy(at, bytes.data(), bytes.size());
        FlushInstructionCache(GetCurrentProcess(), at, bytes.size());
    }
    ~Patch()
    {
        std::memcpy(at, saved.data(), saved.size());
        FlushInstructionCache(GetCurrentProcess(), at, saved.size());
    }
};
inline std::vector<uint8_t> Branch(uint8_t opcode, uint32_t address, const void* to)
{
    std::vector<uint8_t> bytes(5, opcode);
    int32_t relative = static_cast<int32_t>(reinterpret_cast<uintptr_t>(to) - (address + 5));
    std::memcpy(&bytes[1], &relative, 4);
    return bytes;
}
inline std::vector<uint8_t> Dword(uint32_t value)
{
    std::vector<uint8_t> bytes(4);
    std::memcpy(bytes.data(), &value, 4);
    return bytes;
}

// The registers as PUSHAD leaves them, then the return address and the stack arguments.
struct Regs {
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t ret;
    uint32_t args[12];
};
using Handler = std::function<uint32_t(Regs&)>;

// Recording stand-ins: each is a piece of code (PUSHAD; call the handler; EAX = its result; POPAD; RET n) that a
// jump is written to over the start of the functions it stands in for. Every register but EAX comes back as it
// was (handlers may change them through Regs).
class StandIns {
public:
    StandIns()
    {
        m_code = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        s_current = this;
    }
    ~StandIns()
    {
        for (auto it = m_patches.rbegin(); it != m_patches.rend(); ++it)
            delete *it;
        VirtualFree(m_code, 0, MEM_RELEASE);
        s_current = nullptr;
    }
    StandIns(const StandIns&) = delete;
    StandIns& operator=(const StandIns&) = delete;

    void* Thunk(uint16_t pop, Handler handler)
    {
        uint32_t index = static_cast<uint32_t>(m_handlers.size());
        m_handlers.push_back(std::move(handler));
        uint8_t* p = m_code + m_used;
        std::vector<uint8_t> code = {0x60, 0x54, 0x68};
        for (int i = 0; i < 4; ++i)
            code.push_back(static_cast<uint8_t>(index >> (8 * i)));
        code.push_back(0xE8);
        int32_t relative = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&Dispatch) -
                                                (reinterpret_cast<uintptr_t>(p) + code.size() + 4));
        for (int i = 0; i < 4; ++i)
            code.push_back(static_cast<uint8_t>(relative >> (8 * i)));
        code.insert(code.end(), {0x83, 0xC4, 0x08, 0x89, 0x44, 0x24, 0x1C, 0x61});
        if (pop)
            code.insert(code.end(), {0xC2, static_cast<uint8_t>(pop), static_cast<uint8_t>(pop >> 8)});
        else
            code.push_back(0xC3);
        std::memcpy(p, code.data(), code.size());
        m_used += (code.size() + 15) & ~15u;
        return p;
    }
    // Stand in for the functions at `addresses` (the game's and ours alike).
    void* At(std::initializer_list<uint32_t> addresses, uint16_t pop, Handler handler)
    {
        void* thunk = Thunk(pop, std::move(handler));
        for (uint32_t address : addresses)
            m_patches.push_back(new Patch(address, Branch(0xE9, address, thunk)));
        return thunk;
    }
    void Bytes(uint32_t address, const std::vector<uint8_t>& bytes) { m_patches.push_back(new Patch(address, bytes)); }

    static inline StandIns* s_current = nullptr;

private:
    static uint32_t __cdecl Dispatch(uint32_t index, Regs* regs) { return s_current->m_handlers[index](*regs); }

    uint8_t* m_code = nullptr;
    size_t m_used = 0;
    std::vector<Handler> m_handlers;
    std::vector<Patch*> m_patches;
};

// Bytes written for one run only (PartyMod's patches into the original).
inline std::vector<Patch*> g_runPatches;
inline void RunPatch(uint32_t address, const std::vector<uint8_t>& bytes) { g_runPatches.push_back(new Patch(address, bytes)); }
inline void EndRunPatches()
{
    for (auto it = g_runPatches.rbegin(); it != g_runPatches.rend(); ++it)
        delete *it;
    g_runPatches.clear();
}

// (run even if a test crashes: no stand-in or patch may outlive its test)
inline void RemoveStandIns()
{
    EndRunPatches();
    delete StandIns::s_current;
}

inline std::vector<uint32_t> g_trace;
inline void Record(std::initializer_list<uint32_t> values) { g_trace.insert(g_trace.end(), values); }
inline uint32_t Hash(const char* text)
{
    if (!text)
        return 0xFFFFFFFF;
    uint32_t hash = 2166136261u;
    for (; *text; ++text)
        hash = (hash ^ static_cast<uint8_t>(*text)) * 16777619u;
    return hash;
}
inline uint32_t HashBytes(const uint8_t* bytes, size_t size)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < size; ++i)
        hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

struct Saved {
    uint32_t address;
    uint32_t size;
    std::vector<uint8_t> bytes;
};
inline void SaveAll(std::vector<Saved>& saved)
{
    for (Saved& s : saved) {
        s.bytes.resize(s.size);
        std::memcpy(s.bytes.data(), reinterpret_cast<void*>(s.address), s.size);
    }
}
inline void RestoreAll(const std::vector<Saved>& saved)
{
    for (const Saved& s : saved)
        std::memcpy(reinterpret_cast<void*>(s.address), s.bytes.data(), s.size);
}

inline void Compare(const std::vector<uint32_t>& expected, const std::vector<uint32_t>& actual, const char* what)
{
    Check(expected == actual, what);
    if (expected.size() != actual.size())
        Log("  %s trace sizes: original %u ours %u", what, static_cast<unsigned>(expected.size()),
            static_cast<unsigned>(actual.size()));
    for (size_t i = 0, shown = 0; i < expected.size() && i < actual.size() && shown < 8; ++i) {
        if (expected[i] != actual[i]) {
            Log("  %s entry %u: original %08X ours %08X", what, static_cast<unsigned>(i), expected[i], actual[i]);
            ++shown;
        }
    }
}

// Win32 functions the code calls (the original through its import table): the import slot and the function
// itself (for ours) are both stood in for.
inline void StandInApi(StandIns& s, uint32_t importSlot, const void* api, uint16_t pop, Handler handler)
{
    void* thunk = s.At({Resolve(api)}, pop, std::move(handler));
    if (GameVariable<const void*>(importSlot) != api)
        s.Bytes(importSlot, Dword(reinterpret_cast<uint32_t>(thunk)));
}

} // namespace StandInHarness
