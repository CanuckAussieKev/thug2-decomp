#pragma once

// Shared pieces of the self-tests for the goals & career range (0x00550040-0x0056C91F, selftests_goals_*.cpp):
// on top of the skate commands' kit (a stand-in skate module, memory from a buffer reset before each run, the
// structure adders recorded), recording stand-ins for the debug output, spawning and running scripts, arrays, and
// structure getters answered from a table. The original and ours run on the same stand-in state, so traces and
// state compare word for word.

#include <windows.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/goals/goal_manager.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/skate_cfunc_testkit.h"
#include "hook/standins.h"

namespace GoalsTests {

using namespace SkateCFuncTests;
using Script::CArray;
using Script::CStruct;

// Save one more global (put back by Restore).
inline void SaveGlobal(uint32_t address, uint32_t size)
{
    g_saved.push_back({address, size, {}});
    g_saved.back().bytes.resize(size);
    std::memcpy(g_saved.back().bytes.data(), reinterpret_cast<void*>(address), size);
}

// The skate commands' StandInMemory without its Mem::New stand-in: the originals often inline Mem::New (the
// manager made ready, then its Allocate), ours call it - both reach the manager's Allocate, recorded.
inline void StandInGoalMemory(StandIns& s)
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
    // (a structure freed straight into its pool, as the originals' inlined delete does, records as DeleteStruct)
    s.At({0x005BA3B0, AddressOf(&Mem::Pool::Contains)}, 4, [](Regs&) { return 1u; });
    s.At({0x005BA400, AddressOf(&Mem::Pool::Free)}, 4, [](Regs& r) {
        Record({0x9070, r.args[0]});
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
// Dbg::Printf (the original's uses the game's C runtime): the message recorded (formatted by ours).
inline void StandInPrintf(StandIns& s)
{
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        char message[1024];
        std::vsnprintf(message, sizeof(message), reinterpret_cast<const char*>(r.args[0]),
                       reinterpret_cast<va_list>(&r.args[1]));
        if (g_gs3Trace)
            Log("  printf: %s", message);
        Record({0x1C30, r.args[0], Hash(message)});
        return 0xFFFFFFFFu;
    });
}

// Spawning and running scripts: recorded (spawn returns a recognisable script).
inline uint32_t g_spawnResult = 0x5C1E0000;
inline void StandInScripts(StandIns& s)
{
    using SpawnSum = Script::CScript* (*)(uint32_t, CStruct*, uint32_t, CStruct*, int, uint32_t, bool, bool, bool,
                                          bool);
    using SpawnName = Script::CScript* (*)(const char*, CStruct*, uint32_t, CStruct*, int, uint32_t, bool, bool, bool,
                                           bool);
    s.At({0x00475420, AddressOf(static_cast<SpawnSum>(&Script::SpawnScript))}, 0, [](Regs& r) {
        if (g_gs3Trace) Log("  standin %X", r.ret);
        Record({0x5420, r.args[0], r.args[1], r.args[2], r.args[3], r.args[4], r.args[5], r.args[6] & 0xFF,
                r.args[7] & 0xFF, r.args[8] & 0xFF, r.args[9] & 0xFF});
        return g_spawnResult;
    });
    s.At({0x00475670, AddressOf(static_cast<SpawnName>(&Script::SpawnScript))}, 0, [](Regs& r) {
        if (g_gs3Trace) Log("  standin %X", r.ret);
        Record({0x5670, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1], r.args[2], r.args[3], r.args[4],
                r.args[5], r.args[6] & 0xFF, r.args[7] & 0xFF, r.args[8] & 0xFF, r.args[9] & 0xFF});
        return g_spawnResult;
    });
}

// The elements the SetElement stand-in was given, by (array, index) (a test may read them back).
inline std::map<std::pair<uint32_t, uint32_t>, uint32_t> g_arrayElements;

// Arrays: allocated from the stand-in heap (the array pool pair 0x006F22B4 emptied: the manager's Allocate), their
// methods recorded.
inline void StandInArrays(StandIns& s)
{
    SaveGlobal(0x006F22B4, 16);
    SaveGlobal(0x007CC979, 1);
    std::memset(reinterpret_cast<void*>(0x006F22B4), 0, 16);
    GameVariable<bool>(0x007CC979) = true;
    s.At({0x0046CC50, AddressOf(&CArray::SetSizeAndType)}, 8, [](Regs& r) {
        if (g_gs3Trace) Log("  standin %X", r.ret);
        Record({0xCC50, r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x0046CB10, AddressOf(&CArray::SetElement)}, 8, [](Regs& r) {
        if (g_gs3Trace) Log("  standin %X", r.ret);
        Record({0xCB10, r.ecx, r.args[0], r.args[1]});
        g_arrayElements[{r.ecx, r.args[0]}] = r.args[1];
        return 0u;
    });
    s.At({0x00477140, AddressOf(&Script::CleanUpArray)}, 0, [](Regs& r) {
        if (g_gs3Trace) Log("  standin %X", r.ret);
        Record({0x7140, r.args[0]});
        return 0u;
    });
    s.At({0x0046CAB0, AddressOf(&CArray::Clear)}, 0, [](Regs& r) {
        if (g_gs3Trace) Log("  standin %X", r.ret);
        Record({0xCAB0, r.ecx});
        return 0u;
    });
    using AddArraySum = void (CStruct::*)(uint32_t, CArray*);
    using AddArrayName = void (CStruct::*)(const char*, CArray*);
    s.At({0x00478B00, AddressOf(static_cast<AddArraySum>(&CStruct::AddArray))}, 8, [](Regs& r) {
        if (g_gs3Trace) Log("  standin %X", r.ret);
        Record({0x8B00, r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00478C50, AddressOf(static_cast<AddArrayName>(&CStruct::AddArray))}, 8, [](Regs& r) {
        if (g_gs3Trace) Log("  standin %X", r.ret);
        Record({0x8C50, r.ecx, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1]});
        return 0u;
    });
    using AddIntName = void (CStruct::*)(const char*, int);
    s.At({0x00477C30, AddressOf(static_cast<AddIntName>(&CStruct::AddInteger))}, 8, [](Regs& r) {
        if (g_gs3Trace) Log("  standin %X", r.ret);
        Record({0x7C30, r.ecx, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1]});
        return 0u;
    });
}

// Structure getters answered from tables keyed by (structure, name): found or not, and the value written.
inline std::map<std::pair<uint32_t, uint32_t>, uint32_t> g_getAnswers;
// (g_typedAnswers first: answers for one getter only, keyed by its tag - 0x6630 GetInteger, 0x6950 GetChecksum...)
inline std::map<std::pair<uint32_t, uint32_t>, std::map<uint32_t, uint32_t>> g_typedAnswers;
inline bool Answer(Regs& r, uint32_t tag = 0)
{
    auto typed = g_typedAnswers.find({r.ecx, r.args[0]});
    if (typed != g_typedAnswers.end()) {
        auto value = typed->second.find(tag);
        if (value == typed->second.end())
            return false;
        if (r.args[1])
            *reinterpret_cast<uint32_t*>(r.args[1]) = value->second;
        return true;
    }
    auto it = g_getAnswers.find({r.ecx, r.args[0]});
    if (it == g_getAnswers.end())
        return false;
    if (r.args[1])
        *reinterpret_cast<uint32_t*>(r.args[1]) = it->second;
    return true;
}
inline void StandInGetters(StandIns& s)
{
    using GetStr = bool (CStruct::*)(uint32_t, const char**, int);
    using GetArr = bool (CStruct::*)(uint32_t, CArray**, int);
    using GetInt = bool (CStruct::*)(uint32_t, int*, int);
    using GetFlt = bool (CStruct::*)(uint32_t, float*, int);
    using GetSum = bool (CStruct::*)(uint32_t, uint32_t*, int);
    using GetSt = bool (CStruct::*)(uint32_t, CStruct**, int);
    struct Getter {
        uint32_t address;
        uint32_t ours;
    };
    for (Getter g : {Getter{0x00476590, AddressOf(static_cast<GetStr>(&CStruct::GetString))},
                     Getter{0x00476860, AddressOf(static_cast<GetArr>(&CStruct::GetArray))},
                     Getter{0x00476630, AddressOf(static_cast<GetInt>(&CStruct::GetInteger))},
                     Getter{0x004766D0, AddressOf(static_cast<GetFlt>(&CStruct::GetFloat))},
                     Getter{0x00476950, AddressOf(static_cast<GetSum>(&CStruct::GetChecksum))},
                     Getter{0x004767C0, AddressOf(static_cast<GetSt>(&CStruct::GetStructure))}}) {
        uint32_t tag = g.address & 0xFFFF;
        s.At({g.address, g.ours}, 12, [tag](Regs& r) {
            if (g_gs3Trace) Log("  standin %X", r.ret);
            Record({tag, r.ecx, r.args[0], r.args[2] & 0xFF});
            return Answer(r, tag) ? 1u : 0u;
        });
    }
    // The overloads taking the assert flag as a bool (the originals look the component up themselves): recorded
    // as the others.
    struct BoolGetter {
        uint32_t address;
        uint32_t tag;
    };
    for (BoolGetter g : {BoolGetter{0x00476CB0, 0x6630}, BoolGetter{0x00476EC0, 0x6950}, BoolGetter{0x00476C60, 0x6590}}) {
        uint32_t tag = g.tag;
        s.At({g.address}, 12, [tag](Regs& r) {
            Record({tag, r.ecx, r.args[0], r.args[2] & 0xFF});
            return Answer(r, tag) ? 1u : 0u;
        });
    }
}

// ContainsFlag / ContainsComponentNamed answered from a set of (structure, name).
inline std::set<std::pair<uint32_t, uint32_t>> g_hasFlag;
inline std::set<std::pair<uint32_t, uint32_t>> g_hasComponent;
inline void StandInContains(StandIns& s)
{
    using Has = bool (CStruct::*)(uint32_t);
    s.At({0x00476B40, AddressOf(static_cast<Has>(&CStruct::ContainsFlag))}, 4, [](Regs& r) {
        Record({0x6B40, r.ecx, r.args[0]});
        return g_hasFlag.count({r.ecx, r.args[0]}) ? 1u : 0u;
    });
    s.At({0x00476AF0, AddressOf(static_cast<Has>(&CStruct::ContainsComponentNamed))}, 4, [](Regs& r) {
        Record({0x6AF0, r.ecx, r.args[0]});
        return g_hasComponent.count({r.ecx, r.args[0]}) ? 1u : 0u;
    });
    using HasName = bool (CStruct::*)(const char*);
    s.At({0x00476B10, AddressOf(static_cast<HasName>(&CStruct::ContainsComponentNamed))}, 4, [](Regs& r) {
        uint32_t name = Crc::GenerateCRCFromString(reinterpret_cast<const char*>(r.args[0]));
        Record({0x6AF0, r.ecx, name});
        return g_hasComponent.count({r.ecx, name}) ? 1u : 0u;
    });
}

// The structure's other changes recorded: RemoveFlag, RemoveComponent, AppendStructureIfAny, IsEmpty (answered by
// g_emptyAnswer), AddStructure, AddStructurePointer, AddArrayPointer.
inline bool g_emptyAnswer;
inline void StandInStructChanges(StandIns& s)
{
    using Remove = void (CStruct::*)(uint32_t);
    using AddSt = void (CStruct::*)(uint32_t, CStruct*);
    using AddAr = void (CStruct::*)(uint32_t, CArray*);
    s.At({0x00477860, AddressOf(&CStruct::RemoveFlag)}, 4, [](Regs& r) {
        Record({0x7860, r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x004776B0, AddressOf(static_cast<Remove>(&CStruct::RemoveComponent))}, 4, [](Regs& r) {
        Record({0x76B0, r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00478540, AddressOf(&CStruct::AppendStructureIfAny)}, 4, [](Regs& r) {
        Record({0x8540, r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00476420, AddressOf(&CStruct::IsEmpty)}, 0, [](Regs& r) {
        Record({0x6420, r.ecx});
        return g_emptyAnswer ? 1u : 0u;
    });
    s.At({0x00478670, AddressOf(static_cast<AddSt>(&CStruct::AddStructure))}, 8, [](Regs& r) {
        Record({0x8670, r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x004781B0, AddressOf(static_cast<AddSt>(&CStruct::AddStructurePointer))}, 8, [](Regs& r) {
        Record({0x81B0, r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x004780D0, AddressOf(static_cast<AddAr>(&CStruct::AddArrayPointer))}, 8, [](Regs& r) {
        Record({0x80D0, r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
}

// Compare, then (THUG2_GS3_TRACE set) both traces around the first difference.
inline void CompareTraces(const std::vector<uint32_t>& expected, const std::vector<uint32_t>& actual, const char* what)
{
    Compare(expected, actual, what);
    char dump[MAX_PATH];
    if (GetEnvironmentVariableA("THUG2_GOALS_DUMP", dump, sizeof(dump))) { // (both traces, a word a line)
        for (int k = 0; k < 2; ++k) {
            std::string path = std::string(dump) + (k ? ".ours.txt" : ".original.txt");
            FILE* f = nullptr;
            if (fopen_s(&f, path.c_str(), "w") == 0 && f) {
                for (uint32_t word : k ? actual : expected)
                    std::fprintf(f, "%08X\n", word);
                std::fclose(f);
            }
        }
    }
    if (!g_gs3Trace)
        return;
    size_t first = 0;
    while (first < expected.size() && first < actual.size() && expected[first] == actual[first])
        ++first;
    for (size_t i = first > 24 ? first - 24 : 0; i < first + 12; ++i)
        Log("  %6u: %08X %08X", static_cast<unsigned>(i), i < expected.size() ? expected[i] : 0xDEADDEAD,
            i < actual.size() ? actual[i] : 0xDEADDEAD);
}

// The goal manager (0x005591B0 returns this stand-in's address).
alignas(16) inline uint8_t g_goalManager[0x200];

// A stand-in vtable for goals: each of its 30 slots records the call (slot, this, arguments) and answers from
// g_virtualAnswers[slot] (popping each slot's arguments).
inline uint32_t g_virtualAnswers[30];
inline void* g_goalVtable[30];
inline void MakeGoalVtable(StandIns& s)
{
    static const uint16_t kPops[30] = {4, 0, 0, 0, 0, 0, 4, 4, 0, 0, 0, 0, 0, 8, 0,
                                       0, 0, 0, 0, 12, 4, 0, 4, 0, 0, 0, 0, 4, 4, 0};
    for (uint32_t slot = 0; slot < 30; ++slot) {
        uint16_t pop = kPops[slot];
        g_goalVtable[slot] = s.Thunk(pop, [slot, pop](Regs& r) {
            Record({0xF7000000u | slot * 4, r.ecx});
            for (uint32_t i = 0; i < pop / 4u; ++i)
                g_trace.push_back(r.args[i] & (slot == 13 ? 0xFFu : 0xFFFFFFFFu)); // (Deactivate's bools: AL)
            return g_virtualAnswers[slot];
        });
    }
}

// The goal manager's lookups: GetGoal (0x00557F90) answers from g_goalsById, IsGroupLocked (0x00559110) from
// g_groupLocked.
inline std::map<uint32_t, uint32_t> g_goalsById;
inline bool g_groupLocked;
inline void StandInGoalManager(StandIns& s)
{
    s.At({0x005591B0, AddressOf(&Game::GetGoalManager)}, 0, [](Regs&) {
        Record({0x91B0});
        return reinterpret_cast<uint32_t>(g_goalManager);
    });
    s.At({0x00557F90, AddressOf(&Game::CGoalManager::GetGoal)}, 8, [](Regs& r) {
        Record({0x7F90, r.ecx, r.args[0], r.args[1]});
        auto it = g_goalsById.find(r.args[0]);
        return it == g_goalsById.end() ? 0u : it->second;
    });
    s.At({0x00559110, AddressOf(&Game::CGoalManager::IsGroupLocked)}, 8, [](Regs& r) {
        Record({0x9110, r.ecx, r.args[0], r.args[1]});
        return g_groupLocked ? 1u : 0u;
    });
}

} // namespace GoalsTests
