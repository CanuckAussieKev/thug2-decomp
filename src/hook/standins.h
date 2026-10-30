#pragma once

// Recording stand-ins for self-tests that compare a whole function's calls (the original against ours): every
// function it calls is stood in for by code that records the call (written over the start of the game's function
// and of our version of it alike), Win32 functions included, and the two traces are compared.
// Used by selftests_misc.cpp (Main, the online service task, LoadFromMemoryCard) and selftests_winmain.cpp.

#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <vector>

#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace TestStandIns {

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
uint32_t AddressOf(F function)
{
    static_assert(sizeof(F) == sizeof(void*), "a plain code pointer");
    void* code;
    std::memcpy(&code, &function, sizeof(code));
    return Resolve(code);
}

// Our function registered (REPLACE) at a game address, or 0 if there is none: a stand-in written for the game's
// function must catch the calls ours makes straight to it too ({address, OursOf(address)}).
inline uint32_t OursOf(uint32_t address)
{
    void* ours = ReplacementFunction(address);
    return ours ? Resolve(ours) : 0;
}

// The vtable slot a virtual-slot jump stub of the original forwards to (MOV EAX,[ECX]; JMP [EAX+disp8/disp32]),
// read from its original bytes; -1 if the code at `stub` is not one. Our code calls through the slot itself, so a
// test's fake object needs that slot pointing at the stand-in the stub has.
inline int StubSlot(uint32_t stub)
{
    auto* p = static_cast<const uint8_t*>(OriginalFunction(stub));
    if (p[0] != 0x8B || p[1] != 0x01 || p[2] != 0xFF)
        return -1;
    if (p[3] == 0x60)
        return p[4] / 4;
    if (p[3] == 0xA0) {
        int32_t disp;
        std::memcpy(&disp, p + 4, 4);
        return disp / 4;
    }
    if (p[3] == 0x20)
        return 0;
    return -1;
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
    // Stand in for the functions at `addresses` (the game's and ours alike). Our function registered at a game
    // address is stood in for too (OursOf): the game source calls our own functions directly, not through the
    // original's address. AtOnly leaves ours alone (a stand-in that runs ours, or a test of ours under it).
    void* At(std::initializer_list<uint32_t> addresses, uint16_t pop, Handler handler)
    {
        std::vector<uint32_t> all;
        for (uint32_t address : addresses)
            AddOnce(all, address);
        for (uint32_t address : addresses)
            AddOnce(all, OursOf(address));
        return Patched(all, pop, std::move(handler));
    }
    void* AtOnly(std::initializer_list<uint32_t> addresses, uint16_t pop, Handler handler)
    {
        return Patched(std::vector<uint32_t>(addresses), pop, std::move(handler));
    }
    void Bytes(uint32_t address, const std::vector<uint8_t>& bytes) { m_patches.push_back(new Patch(address, bytes)); }

    static inline StandIns* s_current = nullptr;

private:
    static void AddOnce(std::vector<uint32_t>& all, uint32_t address)
    {
        if (address && std::find(all.begin(), all.end(), address) == all.end())
            all.push_back(address);
    }
    void* Patched(const std::vector<uint32_t>& addresses, uint16_t pop, Handler handler)
    {
        void* thunk = Thunk(pop, std::move(handler));
        for (uint32_t address : addresses)
            m_patches.push_back(new Patch(address, Branch(0xE9, address, thunk)));
        return thunk;
    }

    static uint32_t __cdecl Dispatch(uint32_t index, Regs* regs) { return s_current->m_handlers[index](*regs); }

    uint8_t* m_code = nullptr;
    size_t m_used = 0;
    std::vector<Handler> m_handlers;
    std::vector<Patch*> m_patches;
};

// A stand-in for the original's virtual-slot jump stub at `stub` (StubSlot): written over the stub (and ours, as
// At does) and put in `vtable`'s slot too - our code calls the object's slot itself. The vtable must have room.
inline void* AtStub(StandIns& s, void** vtable, uint32_t stub, uint16_t pop, Handler handler)
{
    int slot = StubSlot(stub);
    void* thunk = s.At({stub}, pop, std::move(handler));
    if (slot >= 0)
        vtable[slot] = thunk;
    return thunk;
}

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

// The game's C runtime sprintf (not started offline): the original's calls, done by ours.
inline uint32_t StandInSprintf(Regs& r)
{
    return static_cast<uint32_t>(std::vsnprintf(reinterpret_cast<char*>(r.args[0]), 256,
                                                reinterpret_cast<const char*>(r.args[1]),
                                                reinterpret_cast<va_list>(&r.args[2])));
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
    // (both traces side by side when THUG2_TRACE_DUMP is set and they differ: where they part)
    if (expected != actual && GetEnvironmentVariableA("THUG2_TRACE_DUMP", nullptr, 0)) {
        for (size_t i = 0; i < expected.size() || i < actual.size(); ++i)
            Log("  %5u %08X %08X%s", static_cast<unsigned>(i), i < expected.size() ? expected[i] : 0u,
                i < actual.size() ? actual[i] : 0u,
                i < expected.size() && i < actual.size() && expected[i] == actual[i] ? "" : "  <<");
    }
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

} // namespace TestStandIns
