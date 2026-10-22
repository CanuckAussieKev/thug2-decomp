#pragma once

// More stand-ins for the tests of the online profile / buddies, content vault and stats code
// (selftests_net_buddies.cpp, selftests_net_vault.cpp, selftests_net_stats.cpp), on GameNetKit's arena: script
// structures (the getters answer from tables), preferences, script arrays, the C runtime the originals call (done by
// ours: strtok, atol, strchr...), the clock, tasks and threads. A test runs the original and ours (RunBoth) on the
// same arena and compares the calls, the results and the whole arena; pointers to our own functions (callbacks
// handed to GameSpy, task callbacks) are mapped to the original's addresses first (MapCode).

#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "game/core/task.h"
#include "game/script/preferences.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/gamenet_testkit.h"

namespace Net4Kit {

using namespace GameNetKit;
using Script::CArray;
using Script::CStruct;

// Our functions' addresses -> the original's (for callbacks recorded or left in the arena).
inline std::map<uint32_t, uint32_t> g_codeMap;
template <typename F>
void MapCode(F ours, uint32_t original)
{
    static_assert(sizeof(F) == sizeof(void*), "a plain code pointer");
    void* code;
    std::memcpy(&code, &ours, sizeof(code));
    g_codeMap[reinterpret_cast<uint32_t>(code)] = original; // (an incremental-linking jump, as the code takes it)
    g_codeMap[Resolve(code)] = original;
}
inline uint32_t Norm(uint32_t word)
{
    auto it = g_codeMap.find(word);
    if (it != g_codeMap.end())
        return it->second;
    return Rel(word);
}
inline void RecN(uint32_t tag, std::initializer_list<uint32_t> values)
{
    g_trace.push_back(tag);
    for (uint32_t v : values)
        g_trace.push_back(Norm(v));
}

// Answers of the script getters, by (structure, name): the name's hash (const char* names) or checksum.
using Key = std::pair<uint32_t, uint32_t>;
inline std::map<Key, int> g_ints;
inline std::map<Key, const char*> g_strings;
inline std::set<Key> g_flags;
inline std::map<Key, uint32_t> g_checksums;
inline std::map<Key, uint32_t> g_structures;
inline Key KeyOf(uint32_t structure, const char* name) { return {Rel(structure), Hash(name)}; }
inline Key KeyOf(uint32_t structure, uint32_t checksum) { return {Rel(structure), checksum}; }
// Preference answers: arrays by name, strings and checksums by (name, field).
inline std::map<uint32_t, uint32_t> g_prefArrays;
inline std::map<uint32_t, uint32_t> g_prefStructs;
inline std::map<Key, const char*> g_prefStrings;
inline std::map<Key, uint32_t> g_prefChecksums;
// Array elements by (array, index).
inline std::map<Key, uint32_t> g_elements;
inline uint32_t g_now;
inline bool g_taskInList;

inline void ResetAnswers()
{
    g_ints.clear();
    g_strings.clear();
    g_flags.clear();
    g_checksums.clear();
    g_structures.clear();
    g_prefArrays.clear();
    g_prefStructs.clear();
    g_prefStrings.clear();
    g_prefChecksums.clear();
    g_elements.clear();
    g_answers.clear();
    g_now = 0;
    g_taskInList = false;
}

// A getter (name, out, assert): the answer from `table` written to *out (4 bytes) if there is one.
template <typename T>
Handler Getter(uint32_t tag, std::map<Key, T>& table, bool byName)
{
    return [tag, &table, byName](Regs& r) {
        Key key = byName ? KeyOf(r.ecx, reinterpret_cast<const char*>(r.args[0])) : KeyOf(r.ecx, r.args[0]);
        auto it = table.find(key);
        bool found = it != table.end();
        if (found) {
            T value = it->second;
            std::memcpy(reinterpret_cast<void*>(r.args[1]), &value, 4);
        }
        g_trace.insert(g_trace.end(), {tag, Rel(r.ecx), key.second, r.args[2], found ? 1u : 0u});
        return found ? 1u : 0u;
    };
}

// Script structures and arrays, preferences.
inline void InstallScriptStandIns(StandIns& s)
{
    InstallCommon(s);
    s.At({0x004758F0, AddressOf(static_cast<void (*)(const char*, CStruct*, Obj::CObject*, bool)>(&Script::RunScript))},
         0, [](Regs& r) {
             Rec(0x58F0, {Str(r.args[0]), r.args[1], r.args[2], r.args[3] & 0xFF});
             return 0u;
         });
    s.At({0x00477A90, AddressOf(static_cast<void (CStruct::*)(const char*, const char*)>(&CStruct::AddString))}, 8,
         [](Regs& r) {
             Rec(0x7A90, {r.ecx, Str(r.args[0]), Str(r.args[1])});
             return 0u;
         });
    s.At({0x00477C30, AddressOf(static_cast<void (CStruct::*)(const char*, int)>(&CStruct::AddInteger))}, 8,
         [](Regs& r) {
             Rec(0x7C30, {r.ecx, Str(r.args[0]), r.args[1]});
             return 0u;
         });
    s.At({0x00477DF0, AddressOf(static_cast<void (CStruct::*)(const char*, uint32_t)>(&CStruct::AddChecksum))}, 8,
         [](Regs& r) {
             Rec(0x7DF0, {r.ecx, Str(r.args[0]), r.args[1]});
             return 0u;
         });
    s.At({0x00477D10, AddressOf(static_cast<void (CStruct::*)(const char*, float)>(&CStruct::AddFloat))}, 8,
         [](Regs& r) {
             Rec(0x7D10, {r.ecx, Str(r.args[0]), r.args[1]});
             return 0u;
         });
    s.At({0x00478770, AddressOf(static_cast<void (CStruct::*)(const char*, CStruct*)>(&CStruct::AddStructure))}, 8,
         [](Regs& r) {
             Rec(0x8770, {r.ecx, Str(r.args[0]), r.args[1]});
             return 0u;
         });
    s.At({0x00478650, AddressOf(&CStruct::Assign)}, 4, [](Regs& r) {
        Rec(0x8650, {r.ecx, r.args[0]});
        return r.ecx;
    });
    s.At({0x00476BA0, AddressOf(static_cast<bool (CStruct::*)(const char*)>(&CStruct::ContainsFlag))}, 4,
         [](Regs& r) {
             bool found = g_flags.count(KeyOf(r.ecx, reinterpret_cast<const char*>(r.args[0]))) != 0;
             Rec(0x6BA0, {r.ecx, Str(r.args[0]), found});
             return static_cast<uint32_t>(found);
         });
    s.At({0x00476B40, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsFlag))}, 4, [](Regs& r) {
        bool found = g_flags.count(KeyOf(r.ecx, r.args[0])) != 0;
        Rec(0x6B40, {r.ecx, r.args[0], found});
        return static_cast<uint32_t>(found);
    });
    // (each with the original's bool-assert wrapper of it: 0x00476C60...)
    s.At({0x004765F0, 0x00476C80,
          AddressOf(static_cast<bool (CStruct::*)(const char*, const char**, int)>(&CStruct::GetString))},
         12, Getter(0x65F0, g_strings, true));
    s.At({0x00476C60, AddressOf(static_cast<bool (CStruct::*)(uint32_t, const char**, int)>(&CStruct::GetString))},
         12, Getter(0x6C60, g_strings, false));
    s.At({0x00476680, 0x00476D00, AddressOf(static_cast<bool (CStruct::*)(const char*, int*, int)>(&CStruct::GetInteger))},
         12, Getter(0x6680, g_ints, true));
    s.At({0x00476630, 0x00476CB0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))},
         12, Getter(0x6630, g_ints, false));
    s.At({0x00476950, 0x00476EC0,
          AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))},
         12, Getter(0x6950, g_checksums, false));
    s.At({0x00476EE0, AddressOf(static_cast<bool (CStruct::*)(const char*, uint32_t*, int)>(&CStruct::GetChecksum))},
         12, Getter(0x6EE0, g_checksums, true));
    s.At({0x00476810, AddressOf(static_cast<bool (CStruct::*)(const char*, CStruct**, int)>(&CStruct::GetStructure))},
         12, Getter(0x6810, g_structures, true));
    s.At({0x0046C4A0, AddressOf(&Prefs::Preferences::GetPreference)}, 4, [](Regs& r) {
        auto it = g_prefStructs.find(r.args[0]);
        uint32_t value = it == g_prefStructs.end() ? 0u : it->second;
        Rec(0xC4A0, {r.ecx, r.args[0]});
        return value;
    });

    using Prefs::Preferences;
    s.At({0x0046C4D0, AddressOf(&Preferences::GetPreferenceArray)}, 4, [](Regs& r) {
        auto it = g_prefArrays.find(r.args[0]);
        uint32_t array = it == g_prefArrays.end() ? 0u : it->second;
        Rec(0xC4D0, {r.ecx, r.args[0], array});
        return array;
    });
    s.At({0x0046C610, AddressOf(&Preferences::SetPreferenceArray)}, 8, [](Regs& r) {
        Rec(0xC610, {r.ecx, r.args[0], r.args[1]});
        return 1u;
    });
    s.At({0x0046C5D0, AddressOf(&Preferences::SetPreference)}, 8, [](Regs& r) {
        Rec(0xC5D0, {r.ecx, r.args[0], r.args[1]});
        return 1u;
    });
    s.At({0x0046C510, AddressOf(&Preferences::GetPreferenceString)}, 8, [](Regs& r) {
        auto it = g_prefStrings.find({r.args[0], r.args[1]});
        const char* text = it == g_prefStrings.end() ? "" : it->second;
        Rec(0xC510, {r.ecx, r.args[0], r.args[1]});
        return reinterpret_cast<uint32_t>(text);
    });
    s.At({0x0046C590, AddressOf(&Preferences::GetPreferenceChecksum)}, 8, [](Regs& r) {
        auto it = g_prefChecksums.find({r.args[0], r.args[1]});
        uint32_t value = it == g_prefChecksums.end() ? 0u : it->second;
        Rec(0xC590, {r.ecx, r.args[0], r.args[1]});
        return value;
    });
    s.At({0x0046CC50, AddressOf(&CArray::SetSizeAndType)}, 8, [](Regs& r) {
        Rec(0xCC50, {r.ecx, r.args[0], r.args[1] & 0xFF});
        return 0u;
    });
    s.At({0x0046CC30, AddressOf(&CArray::GetElement)}, 4, [](Regs& r) {
        auto it = g_elements.find({Rel(r.ecx), r.args[0]});
        uint32_t value = it == g_elements.end() ? 0u : it->second;
        Rec(0xCC30, {r.ecx, r.args[0]});
        return value;
    });
    s.At({0x0046CB10, AddressOf(&CArray::SetElement)}, 8, [](Regs& r) {
        Rec(0xCB10, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
}

// The C runtime the originals call (not started offline), done by ours; not recorded.
inline void InstallCrtStandIns(StandIns& s)
{
    s.At({0x00625171}, 0, [](Regs& r) {
        static char* position = nullptr;
        return reinterpret_cast<uint32_t>(
            strtok_s(reinterpret_cast<char*>(r.args[0]), reinterpret_cast<const char*>(r.args[1]), &position));
    });
    s.At({0x00623B10}, 0, [](Regs& r) {
        return reinterpret_cast<uint32_t>(std::strstr(reinterpret_cast<char*>(r.args[0]), reinterpret_cast<const char*>(r.args[1])));
    });
    s.At({0x00624E52}, 0, [](Regs& r) {
        return static_cast<uint32_t>(std::atol(reinterpret_cast<const char*>(r.args[0])));
    });
    s.At({0x00624540}, 0, [](Regs& r) {
        return reinterpret_cast<uint32_t>(std::strchr(reinterpret_cast<char*>(r.args[0]), static_cast<int>(r.args[1])));
    });
    s.At({0x00624EFE}, 0, [](Regs& r) {
        char* text = reinterpret_cast<char*>(r.args[0]);
        for (char* c = text; *c; ++c)
            if (*c >= 'a' && *c <= 'z')
                *c = static_cast<char>(*c - 'a' + 'A');
        return reinterpret_cast<uint32_t>(text);
    });
}

// The clock, tasks, threads.
inline void InstallSystemStandIns(StandIns& s)
{
    s.At({0x005BE970, AddressOf(&Tmr::GetTime)}, 0, [](Regs&) {
        Rec(0xE970, {});
        return g_now;
    });
    s.At({0x004068F0, AddressOf(&Tsk::BaseTask::Remove)}, 0, [](Regs& r) {
        Rec(0x68F0, {r.ecx});
        return 0u;
    });
    s.At({0x00406850, AddressOf(&Tsk::BaseTask::InList)}, 0, [](Regs& r) {
        Rec(0x6850, {r.ecx});
        return static_cast<uint32_t>(g_taskInList);
    });
    s.At({0x00406C90, AddressOf(&Tsk::Stack::AddTask)}, 4, [](Regs& r) {
        Rec(0x6C90, {r.ecx, r.args[0]});
        return 0u;
    });
    StandInApi(s, 0x006450C8, reinterpret_cast<const void*>(&CreateThread), 24, [](Regs& r) {
        RecN(0xC7EA, {r.args[0], r.args[1], r.args[2], r.args[3], r.args[4]});
        *reinterpret_cast<DWORD*>(r.args[5]) = 0x4321;
        return 0x1234u;
    });
    StandInApi(s, 0x006450E0, reinterpret_cast<const void*>(&ResumeThread), 4, [](Regs& r) {
        Rec(0x7E5, {r.args[0]});
        return 0u;
    });
}

// The semaphore calls (only the test's own thread recorded: the stand-in is the Windows function itself).
inline DWORD g_testThread;
inline void InstallSemaphoreStandIns(StandIns& s)
{
    g_testThread = GetCurrentThreadId();
    StandInApi(s, 0x006450BC, reinterpret_cast<const void*>(&WaitForSingleObject), 8, [](Regs& r) {
        if (GetCurrentThreadId() == g_testThread)
            Rec(0x50BC, {r.args[0], r.args[1]});
        return 0u; // WAIT_OBJECT_0
    });
    StandInApi(s, 0x006450E4, reinterpret_cast<const void*>(&ReleaseSemaphore), 12, [](Regs& r) {
        if (GetCurrentThreadId() == g_testThread)
            Rec(0x50E4, {r.args[0], r.args[1], r.args[2]});
        return 1u;
    });
    StandInApi(s, 0x006450E8, reinterpret_cast<const void*>(&CreateSemaphoreA), 16, [](Regs& r) {
        if (GetCurrentThreadId() == g_testThread)
            Rec(0x50E8, {r.args[0], r.args[1], r.args[2], r.args[3]});
        return 0x5E3Au;
    });
}

// The arena after the trace, our code addresses mapped.
inline std::vector<uint32_t> FinishMapped()
{
    std::vector<uint32_t> trace = g_trace;
    for (uint32_t i = 0; i < kArenaSize; i += 4) {
        uint32_t word = Word<uint32_t>(i);
        auto it = g_codeMap.find(word);
        trace.push_back(it != g_codeMap.end() ? it->second : ArenaWordOrHigh(g_storage.Get(), kArenaSize, word));
    }
    return trace;
}

// Run `body(original)` for the original and ours, each on a fresh arena (`setUp(run)` after SetUpArena), and compare.
// `install` puts the stand-ins in (once, for both runs).
inline void RunBoth(const char* what, const std::function<void(StandIns&)>& install,
                    const std::function<void(bool original)>& body, std::initializer_list<Saved> extra = {})
{
    SaveGlobals(extra);
    std::vector<uint32_t> traces[2];
    auto* s = new StandIns;
    install(*s);
    for (int run = 0; run < 2; ++run) {
        SetUpArena(run);
        body(run == 0);
        traces[run] = FinishMapped();
    }
    Cleanup();
    Compare(traces[0], traces[1], what);
}

} // namespace Net4Kit
