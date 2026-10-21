#pragma once

// Shared pieces of the self-tests for the engine's script commands (0x00445B50-0x004489B0,
// selftests_engcmd*.cpp): hand-built parameter structures (no memory manager needed), a stand-in calling script,
// and calling a command (original or ours) with its result recorded.

#include <cstdint>
#include <cstring>

#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/standins.h"

namespace CFuncTests {

using Script::CComponent;
using Script::CStruct;
using Script::ESymbolType;

// A script structure built by hand: components chained in the order added.
struct Params {
    alignas(16) uint8_t storage[8];
    CComponent components[16];
    int count = 0;
    Params() { std::memset(storage, 0, sizeof(storage)); std::memset(components, 0, sizeof(components)); }
    CStruct* Struct() { return reinterpret_cast<CStruct*>(storage); }
    Params& Add(ESymbolType type, uint32_t name, uint32_t raw)
    {
        CComponent& c = components[count];
        c.typeAndFlag = static_cast<uint8_t>(type << 1);
        c.name = name;
        c.raw = raw;
        if (count)
            components[count - 1].next = &c;
        else
            reinterpret_cast<CComponent**>(storage)[1] = &c;
        ++count;
        return *this;
    }
    Params& Checksum(uint32_t name, uint32_t value) { return Add(Script::ESYMBOLTYPE_NAME, name, value); }
    Params& Flag(uint32_t flag) { return Add(Script::ESYMBOLTYPE_NAME, 0, flag); }
    Params& Integer(uint32_t name, int value) { return Add(Script::ESYMBOLTYPE_INTEGER, name, static_cast<uint32_t>(value)); }
    Params& Float(uint32_t name, float value)
    {
        uint32_t raw;
        std::memcpy(&raw, &value, 4);
        return Add(Script::ESYMBOLTYPE_FLOAT, name, raw);
    }
    Params& String(uint32_t name, const char* value) { return Add(Script::ESYMBOLTYPE_STRING, name, reinterpret_cast<uint32_t>(value)); }
    Params& Structure(uint32_t name, CStruct* value) { return Add(Script::ESYMBOLTYPE_STRUCTUREPOINTER, name, reinterpret_cast<uint32_t>(value)); }
    Params& Array(uint32_t name, void* value) { return Add(Script::ESYMBOLTYPE_ARRAYPOINTER, name, reinterpret_cast<uint32_t>(value)); }
    Params& Vector(uint32_t name, void* value) { return Add(Script::ESYMBOLTYPE_VECTOR, name, reinterpret_cast<uint32_t>(value)); }
    Params& Pair(uint32_t name, void* value) { return Add(Script::ESYMBOLTYPE_PAIR, name, reinterpret_cast<uint32_t>(value)); }
};

// The calling script (its params at +0x14 point to `scriptParams`).
inline uint8_t g_script[0x100];
inline uint8_t g_scriptParams[8];
inline uint32_t ScriptParams() { return reinterpret_cast<uint32_t>(g_scriptParams); }
inline uint32_t Bits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return bits;
}

// An empty symbol table (0x006F3624) for the getters' look-ups of names (save and restore the pointer around it).
inline void UseEmptySymbolTable()
{
    static Script::CSymbolTableEntry* table[0x1000];
    std::memset(table, 0, sizeof(table));
    GameVariable<Script::CSymbolTableEntry**>(0x006F3624) = table;
}

using CFunc = bool(__cdecl*)(CStruct*, Script::CScript*);

// Call the command at `address` (the original) or `ours` and record its result.
inline void Call(bool original, uint32_t address, CFunc ours, Params& params)
{
    std::memset(g_script, 0, sizeof(g_script));
    reinterpret_cast<uint32_t*>(g_script)[0x14 / 4] = ScriptParams();
    auto* script = reinterpret_cast<Script::CScript*>(g_script);
    bool result = original ? Original<CFunc>(address)(params.Struct(), script) : ours(params.Struct(), script);
    TestStandIns::Record({0xC0DE, address, result ? 1u : 0u});
}

} // namespace CFuncTests
