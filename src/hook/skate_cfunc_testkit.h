#pragma once

// Shared pieces of the self-tests for the game's script commands 0x005AAEF0-0x005B0A40 (selftests_career_cfuncs.cpp
// and the others of that range): a stand-in skate module (0x007CE478) with its career, a stand-in game mode
// (0x007CE470), network manager (0x007CCE14) and records (0x007CE4BC), a calling script, recording stand-ins for the
// structure methods that add results, and a call that records each command's result. The original and ours run on
// the same stand-in state (reset before each run), so the traces and the state are compared word for word.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/skate/career.h"
#include "hook/cfunc_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/standins.h"

namespace SkateCFuncTests {

using namespace TestStandIns;
using CFuncTests::Params;
using Script::CArray;
using Script::CStruct;

inline const bool g_gs3Trace = GetEnvironmentVariableA("THUG2_GS3_TRACE", nullptr, 0) != 0; // (each call logged)
alignas(16) inline uint8_t g_skate[0x400];
alignas(16) inline uint8_t g_career[sizeof(Obj::CSkaterCareer)];
alignas(16) inline uint8_t g_gameMode[0x40];
alignas(16) inline uint8_t g_net[0x200];
alignas(16) inline uint8_t g_script[0x100];
alignas(16) inline uint8_t g_scriptParams[8];
inline std::map<uint32_t, uint32_t> g_modeProperties; // the game mode's integers (GameModeIsTrue's answers)
inline std::vector<Saved> g_saved;
inline Script::CSymbolTableEntry* g_table[0x1000];

// Every global the stand-ins replace, saved (and put back by Restore, also after a crash).
inline void Setup()
{
    g_saved = {{0x007CE478, 4, {}}, {0x007CE470, 4, {}}, {0x007CCE14, 4, {}}, {0x007CE4BC, 4, {}},
               {0x006F3624, 4, {}}};
    SaveAll(g_saved);
    std::memset(g_table, 0, sizeof(g_table));
    GameVariable<Script::CSymbolTableEntry**>(0x006F3624) = g_table; // (names look nothing up)
    GameVariable<uint8_t*>(0x007CE478) = g_skate;
    GameVariable<uint8_t*>(0x007CE470) = g_gameMode;
    GameVariable<uint8_t*>(0x007CCE14) = g_net;
}
inline void Restore()
{
    RemoveStandIns();
    if (!g_saved.empty())
        RestoreAll(g_saved);
    g_saved.clear();
}

// The state at the start of each run: the skate module (its career at +0x20), the game mode, the network manager.
inline void ResetState()
{
    std::memset(g_skate, 0, sizeof(g_skate));
    std::memset(g_career, 0, sizeof(g_career));
    std::memset(g_gameMode, 0, sizeof(g_gameMode));
    std::memset(g_net, 0, sizeof(g_net));
    *reinterpret_cast<uint8_t**>(g_skate + 0x20) = g_career;
    for (uint32_t i = 0; i < sizeof(g_career); ++i) // (recognisable, unequal values)
        g_career[i] = static_cast<uint8_t>(i * 7 + 3);
    auto* career = reinterpret_cast<Obj::CSkaterCareer*>(g_career);
    // (the goal and flag bits sparse: a bit set in the wrong place must show)
    std::memset(career->m_goals, 0, sizeof(career->m_goals));
    std::memset(career->m_flags, 0, sizeof(career->m_flags));
    std::memset(career->m_startGoals, 0, sizeof(career->m_startGoals) + sizeof(career->m_startFlags));
    std::memset(career->m_globalFlags, 0, sizeof(career->m_globalFlags));
    career->m_goals[5][0] = 0x00010001;
    career->m_flags[5][1] = 0x00000100;
    career->m_globalFlags[3] = 0x80000000;
    career->m_level = 5;
    reinterpret_cast<Obj::CSkaterCareer*>(g_career)->m_info = nullptr;
    for (auto*& gaps : reinterpret_cast<Obj::CSkaterCareer*>(g_career)->m_gaps)
        gaps = nullptr;
    g_modeProperties.clear();
}

// The stand-in state as words (the career, the skate module).
inline void RecordState()
{
    for (uint32_t i = 0; i < sizeof(g_career); i += 4)
        g_trace.push_back(*reinterpret_cast<uint32_t*>(g_career + i));
    for (uint32_t i = 0; i < sizeof(g_skate); i += 4)
        g_trace.push_back(*reinterpret_cast<uint32_t*>(g_skate + i));
}

// The structure methods that add a result to the script's parameters (or anywhere), recorded.
inline void StandInStructAdds(StandIns& s)
{
    using AddInt = void (CStruct::*)(uint32_t, int);
    using AddFlt = void (CStruct::*)(uint32_t, float);
    using AddStr = void (CStruct::*)(uint32_t, const char*);
    using AddSum = void (CStruct::*)(uint32_t, uint32_t);
    s.At({0x00477B80, AddressOf(static_cast<AddInt>(&CStruct::AddInteger))}, 8, [](Regs& r) {
        if (g_gs3Trace) Log("  standin %X", 0x7B80);
        Record({0x7B80, r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00477C60, AddressOf(static_cast<AddFlt>(&CStruct::AddFloat))}, 8, [](Regs& r) {
        if (g_gs3Trace) Log("  standin %X", 0x7C60);
        Record({0x7C60, r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x004779D0, AddressOf(static_cast<AddStr>(&CStruct::AddString))}, 8, [](Regs& r) {
        if (g_gs3Trace) Log("  standin %X", 0x79D0);
        Record({0x79D0, r.ecx, r.args[0], Hash(reinterpret_cast<const char*>(r.args[1]))});
        return 0u;
    });
    s.At({0x00477D40, AddressOf(static_cast<AddSum>(&CStruct::AddChecksum))}, 8, [](Regs& r) {
        if (g_gs3Trace) Log("  standin %X", 0x7D40);
        Record({0x7D40, r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
}

// The game mode's GameModeIsTrue (0x00553220): recorded, answered from g_modeProperties.
inline void StandInGameModeIsTrue(StandIns& s)
{
    s.At({0x00553220}, 4, [](Regs& r) {
        if (g_gs3Trace) Log("  standin %X", 0x3220);
        Record({0x3220, r.ecx, r.args[0]});
        auto it = g_modeProperties.find(r.args[0]);
        return it != g_modeProperties.end() && it->second ? 1u : 0u;
    });
}


// Memory the commands allocate (structures from the structure pool, strings and others from Mem::New), from one
// buffer reset before each run so both runs get the same addresses; frees recorded.
alignas(16) inline uint8_t g_heap[0x4000];
inline uint32_t g_heapUsed;
alignas(16) inline uint8_t g_structPool[0x40];
alignas(16) inline uint8_t g_manager[0x600];
inline void* HeapBlock(uint32_t size)
{
    uint8_t* block = g_heap + g_heapUsed;
    g_heapUsed += (size + 15) & ~15u;
    std::memset(block, 0xCD, size);
    return block;
}
// Also saves and sets the structure pool pair (0x006F3614), the memory manager (0x007D1800: a stand-in whose
// contexts are recorded) and the live string count (0x00690FA0).
inline void StandInMemory(StandIns& s)
{
    g_saved.push_back({0x006F3614, 16, {}});
    g_saved.push_back({0x007D1800, 4, {}});
    g_saved.push_back({0x00690FA0, 4, {}});
    for (size_t i = g_saved.size() - 3; i < g_saved.size(); ++i) {
        g_saved[i].bytes.resize(g_saved[i].size);
        std::memcpy(g_saved[i].bytes.data(), reinterpret_cast<void*>(g_saved[i].address), g_saved[i].size);
    }
    auto* pair = reinterpret_cast<uint32_t*>(0x006F3614);
    pair[0] = 0;
    pair[1] = 0;
    pair[2] = reinterpret_cast<uint32_t>(g_structPool);
    pair[3] = 0;
    std::memset(g_manager, 0, sizeof(g_manager));
    for (uint32_t offset = 0x10C; offset < 0x5E0; offset += 4) // (each region / heap pointer a value of its own)
        *reinterpret_cast<uint32_t*>(g_manager + offset) = 0x4EA00000 + offset;
    GameVariable<uint8_t*>(0x007D1800) = g_manager;
    s.At({0x005BA3D0, AddressOf(&Mem::Pool::Allocate)}, 0, [](Regs& r) {
        if (g_gs3Trace) Log("  standin %X", 0xA3D0);
        Record({0xA3D0, r.ecx});
        return reinterpret_cast<uint32_t>(HeapBlock(8));
    });
    s.At({0x004015D0, AddressOf(&Mem::New)}, 0, [](Regs& r) {
        if (g_gs3Trace) Log("  standin %X", 0x15D0);
        Record({0x15D0, r.args[0]});
        return reinterpret_cast<uint32_t>(HeapBlock(r.args[0]));
    });
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Record({0xAFC0, r.args[0], r.args[1], r.args[2]});
        return reinterpret_cast<uint32_t>(HeapBlock(r.args[0]));
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xABA0, r.args[0]});
        return 0u;
    });
    s.At({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, [](Regs& r) {
        if (g_gs3Trace) Log("  standin %X", 0xB0E0);
        Record({0xB0E0, r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [](Regs& r) {
        if (g_gs3Trace) Log("  standin %X", 0xB110);
        Record({0xB110, r.ecx});
        return 0u;
    });
    s.At({0x00477090, 0x00477130, AddressOf(&CStruct::Clear)}, 0, [](Regs& r) {
        if (g_gs3Trace) Log("  standin %X", 0x7090);
        Record({0x7090, r.ecx});
        return 0u;
    });
    s.At({0x00409070, AddressOf(&Script::DeleteStruct)}, 0, [](Regs& r) {
        if (g_gs3Trace) Log("  standin %X", 0x9070);
        Record({0x9070, r.args[0]});
        return 0u;
    });
    s.At({0x00478480, AddressOf(&CStruct::AppendStructure)}, 4, [](Regs& r) {
        if (g_gs3Trace) Log("  standin %X", 0x8480);
        Record({0x8480, r.ecx, r.args[0]});
        return r.ecx;
    });
    s.At({0x004783C0, AddressOf(&CStruct::AddComponentOfType)}, 12, [](Regs& r) {
        if (g_gs3Trace) Log("  standin %X", 0x83C0);
        Record({0x83C0, r.ecx, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    using Run = void (*)(uint32_t, CStruct*, Obj::CObject*, bool, const char*);
    s.At({0x00475790, AddressOf(static_cast<Run>(&Script::RunScript))}, 0, [](Regs& r) {
        if (g_gs3Trace) Log("  standin %X", 0x5790);
        Record({0x5790, r.args[0], r.args[1], r.args[2], r.args[3] & 0xFF, r.args[4]});
        return 0u;
    });
}
inline void ResetMemory()
{
    g_heapUsed = 0;
    std::memset(g_heap, 0, sizeof(g_heap));
    GameVariable<int>(0x00690FA0) = 0;
}

using CFunc = bool(__cdecl*)(CStruct*, Script::CScript*);

// Call the command at `address` (the original) or `ours`, from the stand-in script running on `object` (or a null
// script), and record its result (AL only: the commands return a bool).
inline void CallCommand(bool original, uint32_t address, CFunc ours, Params& params, void* object = nullptr,
                 bool nullScript = false)
{
    std::memset(g_script, 0, sizeof(g_script));
    std::memset(g_scriptParams, 0, sizeof(g_scriptParams));
    reinterpret_cast<uint8_t**>(g_script)[0x14 / 4] = g_scriptParams;
    reinterpret_cast<void**>(g_script)[0xBC / 4] = object;
    auto* script = nullScript ? nullptr : reinterpret_cast<Script::CScript*>(g_script);
    static const bool trace = GetEnvironmentVariableA("THUG2_GS3_TRACE", nullptr, 0) != 0; // (finding a crash)
    if (trace)
        Log("gs3: %08X %s", address, original ? "original" : "ours");
    bool result = original ? Original<CFunc>(address)(params.Struct(), script) : ours(params.Struct(), script);
    if (g_gs3Trace) Log("  standin %X", 0xC0DE);
    Record({0xC0DE, address, static_cast<uint32_t>(*reinterpret_cast<uint8_t*>(&result))});
}

} // namespace SkateCFuncTests
