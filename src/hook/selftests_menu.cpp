// Self-tests of the front end module (src/game/front/front_end.cpp) and the competition pieces next to it
// (src/game/skate/competition.cpp): each original against ours on the same stand-in front end. Everything they
// call is stood in for by recording code (structures and their getters, allocation, the script manager, the
// keyboard, the input manager, the skate module, the tracker...), so the two are compared call by call, then the
// front end's bytes and the globals they set.

#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/core/string.h"
#include "game/core/task.h"
#include "game/front/screen_element_manager.h"
#include "game/front/front_end.h"
#include "game/obj/compositemgr.h"
#include "game/obj/tracker.h"
#include "game/pcm/music.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sfx/sfx.h"
#include "game/sio/device.h"
#include "game/sio/inpman.h"
#include "game/sys/game_clock.h"
#include "game/sys/module.h"
#include "game/skate/competition.h"
#include "game/script/game_cfuncs.h"
#include "game/skate/skate_module.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Mdl::FrontEnd;
using Script::CArray;
using Script::CStruct;

constexpr int kStructs = 24;
alignas(16) uint8_t g_structs[kStructs][16]; // stand-in structures (never looked into: their calls are stood in)
alignas(16) uint8_t g_frontEnd[0x180];
alignas(16) uint8_t g_skate[0x400];
alignas(16) uint8_t g_nodes[5][0x10];        // the skate module's skater list
alignas(16) uint8_t g_skaters[4][0x10];      // +0 vtable
void* g_skaterVtable[16];
void* g_nodeVtable[4];
void* g_frontEndVtable[4];
alignas(16) uint8_t g_padDevices[2][sizeof(Sio::Device)];
bool g_scriptFound;                   // FindScriptWithUniqueId: found
alignas(16) uint8_t g_mlp[0xD0];
alignas(16) uint8_t g_manager[0x600];
alignas(16) uint8_t g_input[0x200];          // the input manager
alignas(16) uint8_t g_devices[2][0x10];
alignas(16) uint8_t g_script[0xC0];          // SpawnScript's answer
alignas(16) uint8_t g_tracker[0x10];
alignas(16) uint8_t g_objects[0x10];         // 0x006F85A8
alignas(16) uint8_t g_eventManager[0x10];    // 0x006F0FB8
alignas(16) uint8_t g_screens[0x10];         // 0x00701440
alignas(16) uint8_t g_sio[0x1C];
alignas(16) uint8_t g_blocks[6][0x40];       // Mem::Calloc's answers
alignas(16) uint8_t g_tasks[2][0x20];
alignas(16) uint8_t g_handlers[2][0x2C];
alignas(16) uint8_t g_controlData[0x40];
Mem::Pool* const kStructPool = reinterpret_cast<Mem::Pool*>(0x00001120); // never dereferenced (stood in)

int g_nextStruct;
int g_nextBlock;
bool g_callocFails;
uint32_t g_lastId[kStructs];          // the last id added to each structure (ObjectExists' answer depends on it)
std::vector<uint32_t> g_existing;     // the ids ObjectExists says exist
std::string g_keys;                   // GetKeyboardChars' characters
std::vector<int> g_flags;             // ContainsFlag's answers, in turn
std::vector<uint32_t> g_globals;      // Script::GetInteger's answers, in turn
std::vector<uint32_t> g_repeats;      // 0x0054ECE0's answers, in turn
bool g_getterFound;                   // GetArray / GetPair / GetInteger: found
CArray* g_array;                      // GetArray's array
float g_pair[2];
int g_integer;
void* g_skaterAt[4];                  // 0x005665C0's answers
void* g_localSkater;                  // 0x00566520's answer
uint32_t g_stackLow, g_stackHigh;
std::vector<Saved> g_saved;
bool g_saving;

template <typename T, size_t N, size_t M>
bool Inside(uint32_t p, T (&array)[N][M], uint32_t& index)
{
    auto base = reinterpret_cast<uint32_t>(array);
    if (p < base || p >= base + sizeof(array))
        return false;
    index = (p - base) / sizeof(array[0]);
    return true;
}
// Pointers as stable ids (code pointers of ours as the game's).
uint32_t Id(uint32_t p)
{
    uint32_t i;
    if (Inside(p, g_structs, i))
        return 0x57000000 + i * 0x100 + (p - reinterpret_cast<uint32_t>(g_structs[i]));
    if (Inside(p, g_nodes, i))
        return 0x40000000 + i * 0x100 + (p - reinterpret_cast<uint32_t>(g_nodes[i]));
    if (Inside(p, g_skaters, i))
        return 0x5C000000 + i * 0x100 + (p - reinterpret_cast<uint32_t>(g_skaters[i]));
    if (Inside(p, g_blocks, i))
        return 0xB1000000 + i * 0x100 + (p - reinterpret_cast<uint32_t>(g_blocks[i]));
    if (Inside(p, g_tasks, i))
        return 0x7A000000 + i * 0x100 + (p - reinterpret_cast<uint32_t>(g_tasks[i]));
    if (Inside(p, g_handlers, i))
        return 0x4A000000 + i * 0x100 + (p - reinterpret_cast<uint32_t>(g_handlers[i]));
    if (Inside(p, g_devices, i))
        return 0xDE000000 + i * 0x100 + (p - reinterpret_cast<uint32_t>(g_devices[i]));
    struct One {
        uint8_t* base;
        size_t size;
        uint32_t id;
    };
    const One ones[] = {{g_frontEnd, sizeof(g_frontEnd), 0xFE000000}, {g_skate, sizeof(g_skate), 0x5A000000},
                        {g_mlp, sizeof(g_mlp), 0x31000000},          {g_manager, sizeof(g_manager), 0x3A000000},
                        {g_input, sizeof(g_input), 0x19000000},      {g_script, sizeof(g_script), 0x5D000000},
                        {g_tracker, sizeof(g_tracker), 0x7C000000},  {g_objects, sizeof(g_objects), 0x0B000000},
                        {g_eventManager, sizeof(g_eventManager), 0xE0000000},
                        {g_screens, sizeof(g_screens), 0x5E000000},  {g_sio, sizeof(g_sio), 0x51000000},
                        {g_controlData, sizeof(g_controlData), 0xCD000000}};
    for (const One& one : ones) {
        auto base = reinterpret_cast<uint32_t>(one.base);
        if (p >= base && p < base + one.size)
            return one.id + (p - base);
    }
    if (p >= g_stackLow && p < g_stackHigh)
        return 0x57AC0000; // a local
    // (ours may be stored as their incremental-linking jumps)
    if (p == AddressOf(&FrontEnd::LogicTask) || p == reinterpret_cast<uint32_t>(&FrontEnd::LogicTask))
        return 0x0054FCC0;
    if (p == AddressOf(&FrontEnd::KeyboardTask) || p == reinterpret_cast<uint32_t>(&FrontEnd::KeyboardTask))
        return 0x0054F200;
    if (p == AddressOf(&FrontEnd::InputLogic) || p == reinterpret_cast<uint32_t>(&FrontEnd::InputLogic))
        return 0x0054FB40;
    return p;
}
uint32_t StructIndex(uint32_t p)
{
    uint32_t i = 0;
    Inside(p, g_structs, i);
    return i;
}
template <typename T>
T Next(std::vector<T>& answers, T otherwise)
{
    if (answers.empty())
        return otherwise;
    T value = answers.front();
    answers.erase(answers.begin());
    return value;
}
uint32_t Bits(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}
uint32_t NewStandInStruct()
{
    int i = g_nextStruct++ % (kStructs - 1); // (the last one is the script commands' parameters)
    g_lastId[i] = 0;
    return reinterpret_cast<uint32_t>(g_structs[i]);
}

std::vector<Saved> SavedGlobals()
{
    return {{0x007CE464, 8, {}}, {0x007CE46C, 4, {}}, {0x007CE4A0, 1, {}}, {0x006FA7D4, 1, {}}, {0x007CE478, 4, {}},
            {0x006F0FC0, 4, {}}, {0x006F0FB8, 4, {}}, {0x00701440, 4, {}}, {0x006F85A8, 4, {}}, {0x00696D68, 4, {}},
            {0x007D1800, 4, {}}, {0x007D1E20, 4, {}}, {0x006F3614, 16, {}}, {0x007CC979, 1, {}}, {0x00690FA0, 4, {}},
            {0x00696D40, 4, {}}, {0x00684FC8, 4, {}}};
}

// (run even if a test crashes)
void RestoreMenuTest()
{
    RemoveStandIns();
    if (g_saving)
        RestoreAll(g_saved);
    g_saving = false;
}

// Every call the front end makes.
void InstallStandIns(StandIns& s, bool autoRepeat)
{
    // Structures: allocation, construction, the calls on them.
    s.At({0x005BA3D0, AddressOf(&Mem::Pool::Allocate)}, 0, [](Regs& r) {
        Record({0xA11C, r.ecx});
        return NewStandInStruct();
    });
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Record({0xA110, Id(r.ecx), r.args[0], r.args[1], r.args[2]});
        return NewStandInStruct();
    });
    s.At({0x00476410, AddressOf(&CStruct::Init)}, 0, [](Regs& r) {
        Record({0x1417, Id(r.ecx)});
        return r.ecx;
    });
    s.At({0x00477090, 0x00477130, AddressOf(&CStruct::Clear)}, 0, [](Regs& r) {
        Record({0xC1EA, Id(r.ecx)});
        return 0u;
    });
    s.At({0x00409070, AddressOf(&Script::DeleteStruct)}, 0, [](Regs& r) {
        Record({0xDE1, Id(r.args[0])});
        return 0u;
    });
    s.At({0x00477D40, AddressOf(static_cast<void (CStruct::*)(uint32_t, uint32_t)>(&CStruct::AddChecksum))}, 8,
         [](Regs& r) {
             Record({0xADDC, Id(r.ecx), r.args[0], r.args[1]});
             if (r.args[0] == 0x40C698AF)
                 g_lastId[StructIndex(r.ecx)] = r.args[1];
             return 0u;
         });
    s.At({0x004779D0, AddressOf(static_cast<void (CStruct::*)(uint32_t, const char*)>(&CStruct::AddString))}, 8,
         [](Regs& r) {
             Record({0xADD5, Id(r.ecx), r.args[0], Hash(reinterpret_cast<const char*>(r.args[1]))});
             return 0u;
         });
    s.At({0x00477B80, AddressOf(static_cast<void (CStruct::*)(uint32_t, int)>(&CStruct::AddInteger))}, 8,
         [](Regs& r) {
             Record({0xADD1, Id(r.ecx), r.args[0], r.args[1]});
             return 0u;
         });
    s.At({0x00476B40, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsFlag))}, 4, [](Regs& r) {
        Record({0xF1A9, Id(r.ecx), r.args[0]});
        return static_cast<uint32_t>(Next(g_flags, 0));
    });
    auto getArray = [](Regs& r) {
        Record({0x6A, Id(r.ecx), r.args[0] < 0x10000 ? r.args[0] : Hash(reinterpret_cast<const char*>(r.args[0])),
                Id(r.args[1]), r.args[2] & 0xFF});
        if (g_getterFound)
            *reinterpret_cast<CArray**>(r.args[1]) = g_array;
        return static_cast<uint32_t>(g_getterFound);
    };
    s.At({0x004768B0, AddressOf(static_cast<bool (CStruct::*)(const char*, CArray**, int)>(&CStruct::GetArray))}, 12,
         getArray);
    s.At({0x00476F10, AddressOf(static_cast<bool (CStruct::*)(uint32_t, CArray**, int)>(&CStruct::GetArray))}, 12,
         getArray);
    s.At({0x00476DF0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, Script::CPair*, bool)>(&CStruct::GetPair))},
         12, [](Regs& r) {
             auto* pair = reinterpret_cast<Script::CPair*>(r.args[1]);
             Record({0x6B, Id(r.ecx), r.args[0], Id(r.args[1]), r.args[2] & 0xFF, pair->unknown0 & 0xFF,
                     Bits(pair->x), Bits(pair->y)});
             if (g_getterFound) {
                 pair->x = g_pair[0];
                 pair->y = g_pair[1];
             }
             return static_cast<uint32_t>(g_getterFound);
         });
    s.At({0x00476CB0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))}, 12,
         [](Regs& r) {
             Record({0x6C, Id(r.ecx), r.args[0], Id(r.args[1]), r.args[2] & 0xFF,
                     *reinterpret_cast<uint32_t*>(r.args[1])});
             if (g_getterFound)
                 *reinterpret_cast<int*>(r.args[1]) = g_integer;
             return static_cast<uint32_t>(g_getterFound);
         });
    s.At({0x00478F60, AddressOf(static_cast<int (*)(uint32_t)>(&Script::GetInteger))}, 0, [](Regs& r) {
        Record({0x61, r.args[0]});
        return Next(g_globals, 0u);
    });
    s.At({0x00462340, AddressOf(&Obj::CFunc_ObjectExists)}, 0, [](Regs& r) { // ObjectExists
        uint32_t id = g_lastId[StructIndex(r.args[0])];
        Record({0x0E, Id(r.args[0]), Id(r.args[1]), id});
        for (uint32_t existing : g_existing)
            if (existing == id)
                return 1u;
        return 0u;
    });
    // Scripts.
    s.At({0x00475790, AddressOf(static_cast<void (*)(uint32_t, CStruct*, Obj::CObject*, bool, const char*)>(
                          &Script::RunScript))},
         0, [](Regs& r) {
             Record({0x5C41, r.args[0], Id(r.args[1]), Id(r.args[2]), r.args[3] & 0xFF, r.args[4]});
             return 0u;
         });
    s.At({0x00475420, AddressOf(static_cast<Script::CScript* (*)(uint32_t, CStruct*, uint32_t, CStruct*, int, uint32_t,
                                                                 bool, bool, bool, bool)>(&Script::SpawnScript))},
         0, [](Regs& r) {
             Record({0x59A, r.args[0], Id(r.args[1]), r.args[2], Id(r.args[3]), r.args[4], r.args[5], r.args[6] & 0xFF,
                     r.args[7] & 0xFF, r.args[8] & 0xFF, r.args[9] & 0xFF});
             return reinterpret_cast<uint32_t>(g_script);
         });
    s.At({0x00475920, AddressOf(&Script::UpdateSpawnedScripts)}, 0, [](Regs&) {
        Record({0x0959});
        return 0u;
    });
    s.At({0x00472D80, AddressOf(&Script::PauseSpawnedScripts)}, 0, [](Regs& r) {
        Record({0x9A5, r.args[0] & 0xFF});
        return 0u;
    });
    // The keyboard and the controllers' motors.
    s.At({0x005BCC40, AddressOf(&Sio::GetKeyboardChars)}, 0, [](Regs& r) {
        Record({0x6EC, Id(r.args[0]), static_cast<uint32_t>(g_keys.size())});
        std::memcpy(reinterpret_cast<char*>(r.args[0]), g_keys.data(), g_keys.size());
        return static_cast<uint32_t>(g_keys.size());
    });
    s.At({0x005BCC50, AddressOf(&Sio::ClearKeyboardChars)}, 0, [](Regs&) {
        Record({0xC1C});
        return 0u;
    });
    s.At({0x005BE550, AddressOf(&Sio::Manager::PauseDevices)}, 0, [](Regs& r) {
        Record({0x9A0, Id(r.ecx)});
        return 0u;
    });
    s.At({0x005BE5E0, AddressOf(&Sio::Manager::UnpauseDevices)}, 0, [](Regs& r) {
        Record({0x9A1, Id(r.ecx)});
        return 0u;
    });
    // Memory.
    s.At({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, [](Regs& r) {
        Record({0x9054, Id(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [](Regs& r) {
        Record({0x90B, Id(r.ecx)});
        return 0u;
    });
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Record({0xCA1, r.args[0]});
        if (g_callocFails || g_nextBlock >= 6)
            return 0u;
        std::memset(g_blocks[g_nextBlock], 0, sizeof(g_blocks[0]));
        return reinterpret_cast<uint32_t>(g_blocks[g_nextBlock++]);
    });
    s.At({0x00401D40, AddressOf(static_cast<void* (*)(unsigned int, Mem::Heap*, int)>(&Mem::Calloc))}, 0,
         [](Regs& r) {
             Record({0xCA3, r.args[0], r.args[1], r.args[2]});
             if (g_callocFails)
                 return 0u;
             std::memset(g_frontEnd, 0xCD, sizeof(g_frontEnd));
             return reinterpret_cast<uint32_t>(g_frontEnd);
         });
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        if (r.args[0] == 0x0064D82C) // (the other one has no arguments)
            Record({0x9F, r.args[0], r.args[1], r.args[2]});
        else
            Record({0x9F, r.args[0]});
        return 0u;
    });
    s.At({0x00452910, AddressOf(&Pcm::Update)}, 0, [](Regs&) {
        Record({0x3C});
        return 0u;
    });
    // The engine (without source yet).
    // (`ours`: our version, which the front end now calls directly - stood in for too)
    auto thiscall0 = [&s](uint32_t address, uint32_t tag, uint32_t (*answer)(), uint32_t ours = 0) {
        s.At({address, ours ? ours : address}, 0, [tag, answer](Regs& r) {
            Record({tag, Id(r.ecx)});
            return answer ? answer() : 0u;
        });
    };
    auto thiscall1 = [&s](uint32_t address, uint32_t tag, uint32_t (*answer)(uint32_t), uint32_t ours = 0) {
        s.At({address, ours ? ours : address}, 4, [tag, answer](Regs& r) {
            Record({tag, Id(r.ecx), Id(r.args[0])});
            return answer ? answer(r.args[0]) : 0u;
        });
    };
    // (the voices' states: the original calls it with the sound manager in ECX, which it doesn't use - ours calls
    // it without: ECX not recorded)
    s.At({0x0047B000, AddressOf(&Sfx::UpdateVoiceStates)}, 0, [](Regs&) {
        Record({0xB000});
        return 0u;
    });
    thiscall0(0x0047C560, 0xC560, nullptr, AddressOf(&Sfx::CSfxManager::UpdatePositionalSounds));
    thiscall0(0x00564D60, 0x4D60, nullptr, AddressOf(&Mdl::Skate::UpdateGameFlow));
    thiscall0(0x00566520, 0x6520, [] { return reinterpret_cast<uint32_t>(g_localSkater); },
              AddressOf(&Mdl::Skate::GetLocalSkater));
    thiscall0(0x004ABD00, 0xBD00, nullptr, AddressOf(&Front::CScreenElementManager::Update));
    s.At({0x00462B70, AddressOf(&Obj::CTracker::LogFrame)}, 0, [](Regs& r) { Record({0x2B70, Id(r.ecx)}); return 0u; });
    thiscall0(0x004068F0, 0x68F0, nullptr, AddressOf(&Tsk::BaseTask::Remove));
    thiscall1(0x00564F60, 0x4F60, nullptr, AddressOf(&Mdl::Skate::Pause));
    thiscall1(0x005665C0, 0x65C0,
              [](uint32_t i) { return i < 4 ? reinterpret_cast<uint32_t>(g_skaterAt[i]) : 0u; },
              AddressOf(&Mdl::Skate::GetSkater));
    s.At({0x0045F5D0, AddressOf(&Obj::CCompositeObjectManager::Pause)}, 4, [](Regs& r) {
        Record({0xF5D0, Id(r.ecx), r.args[0] & 0xFF});
        return 0u;
    });
    thiscall1(0x004AAEE0, 0xAEE0, nullptr, AddressOf(&Front::CScreenElementManager::SetPausedAll));
    thiscall1(0x0044E610, 0xE610, nullptr, AddressOf(&Inp::Manager::SetOverridePadWithStick));
    thiscall1(0x00406C90, 0x6C90, nullptr, AddressOf(&Tsk::Stack::AddTask));
    thiscall1(0x00406890, 0x6890, nullptr, AddressOf(&Tsk::BaseTask::Construct));
    s.At({0x0044F7E0, AddressOf(&Mdl::Module::Construct)}, 0, [](Regs& r) { // Mdl::Module's constructor
        Record({0xF7E0, Id(r.ecx)});
        std::memset(reinterpret_cast<void*>(r.ecx), 0x11, 0x14);
        return r.ecx;
    });
    s.At({0x0044ED10, AddressOf(&Tmr::PauseGameClock)}, 0, [](Regs& r) {
        Record({0xED10, r.args[0] & 0xFF});
        return 0u;
    });
    s.At({0x0059FD90, AddressOf(CFuncs::CFunc_SetViewMode)}, 0, [](Regs& r) { // SetViewMode
        Record({0xFD90, Id(r.args[0]), Id(r.args[1])});
        return 1u;
    });
    if (autoRepeat) {
        s.At({0x0054ECE0, AddressOf(&FrontEnd::AutoRepeat)}, 8, [](Regs& r) {
            Record({0xECE0, Id(r.ecx), r.args[0], r.args[1]});
            return Next(g_repeats, 0u);
        });
    }
    s.At({0x004734B0, AddressOf(&Script::FindScriptWithUniqueId)}, 0, [](Regs& r) {
        Record({0x34B0, r.args[0]});
        return g_scriptFound ? reinterpret_cast<uint32_t>(g_script) : 0u;
    });
    s.At({0x00472DC0, AddressOf(&Script::UnpauseSpawnedScript)}, 0, [](Regs& r) {
        Record({0x2DC0, Id(r.args[0])});
        return 0u;
    });
    s.At({0x0044F790, AddressOf(&Mdl::Module::DestroyBody)}, 0, [](Regs& r) { // Mdl::Module's destructor body
        Record({0xF790, Id(r.ecx)});
        return 0u;
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xF4EE, Id(r.ecx), Id(r.args[0])});
        return 0u;
    });
    s.At({0x00462F00, AddressOf(&Obj::CTracker::LaunchEvent)}, 0x18, [](Regs& r) { // LaunchEvent
        Record({0x2F00, Id(r.ecx), r.args[0], r.args[1], r.args[2], Id(r.args[3]), r.args[4], r.args[5]});
        return 0u;
    });
    s.At({0x0044E7F0, AddressOf(&Inp::CreateManager)}, 0, [](Regs& r) {
        Record({0xE7F0, r.args[0] & 0xFF});
        return reinterpret_cast<uint32_t>(g_input);
    });
    s.At({0x0044E6B0, AddressOf(&Inp::ReleaseManager)}, 0, [](Regs&) {
        Record({0xE6B0});
        return 0u;
    });
    // Skaters: vtable +0x2C pause, +0x30 unpause.
    for (void*& entry : g_skaterVtable)
        entry = nullptr;
    g_skaterVtable[0x2C / 4] = s.Thunk(0, [](Regs& r) {
        Record({0x5C2C, Id(r.ecx)});
        return 0u;
    });
    g_skaterVtable[0x30 / 4] = s.Thunk(0, [](Regs& r) {
        Record({0x5C30, Id(r.ecx)});
        return 0u;
    });
    // Task list nodes: slot 2 the scalar deleting destructor; the front end's own slot 0.
    for (void*& entry : g_nodeVtable)
        entry = nullptr;
    g_nodeVtable[2] = s.Thunk(4, [](Regs& r) {
        Record({0xDE7, Id(r.ecx), r.args[0]});
        return r.ecx;
    });
    for (void*& entry : g_frontEndVtable)
        entry = nullptr;
    g_frontEndVtable[0] = s.Thunk(4, [](Regs& r) {
        Record({0xFED0, Id(r.ecx), r.args[0]});
        return r.ecx;
    });
}

FrontEnd* Fe() { return reinterpret_cast<FrontEnd*>(g_frontEnd); }

// A fresh world for one run: the front end dirty, the stand-in engine objects in place.
void BeginRun()
{
    g_trace.clear();
    g_nextStruct = 0;
    g_nextBlock = 0;
    g_callocFails = false;
    std::memset(g_lastId, 0, sizeof(g_lastId));
    std::memset(g_structs, 0, sizeof(g_structs));
    g_existing.clear();
    g_keys.clear();
    g_flags.clear();
    g_globals.clear();
    g_repeats.clear();
    g_scriptFound = false;
    GameVariable<float>(0x00684FC8) = 1.0f;
    GameVariable<void*>(0x00696D40) = g_input;
    for (size_t i = 0; i < sizeof(g_frontEnd); ++i)
        g_frontEnd[i] = static_cast<uint8_t>(0x30 + i * 7);
    std::memset(g_skate, 0, sizeof(g_skate));
    std::memset(g_mlp, 0xEE, sizeof(g_mlp));
    std::memset(g_manager, 0, sizeof(g_manager));
    *reinterpret_cast<uint32_t*>(g_manager + 0x114) = 0xB0770111; // the bottom-up heap
    *reinterpret_cast<uint32_t*>(g_manager + 0x11C) = 0xF2011111; // the front end heap
    std::memset(g_input, 0, sizeof(g_input));
    std::memset(g_handlers, 0x22, sizeof(g_handlers));
    std::memset(g_script, 0, sizeof(g_script));
    *reinterpret_cast<uint32_t*>(g_script + 0xB0) = 0x12345678;
    GameVariable<FrontEnd*>(0x007CE464) = Fe();
    GameVariable<uint32_t>(0x007CE468) = 1;
    GameVariable<uint32_t>(0x007CE46C) = 0xA5A5A5A5;
    GameVariable<uint8_t>(0x007CE4A0) = 0;
    GameVariable<uint8_t>(0x006FA7D4) = 0x77;
    GameVariable<uint8_t*>(0x007CE478) = g_skate;
    GameVariable<void*>(0x006F0FC0) = g_tracker;
    GameVariable<void*>(0x006F0FB8) = g_eventManager;
    GameVariable<void*>(0x00701440) = g_screens;
    GameVariable<void*>(0x006F85A8) = g_objects;
    GameVariable<void*>(0x00696D68) = g_mlp;
    GameVariable<void*>(0x007D1800) = g_manager;
    GameVariable<void*>(0x007D1E20) = g_sio;
    auto& pools = GameVariable<Mem::PoolPair>(0x006F3614);
    pools.current = 0;
    pools.unknown4 = 0;
    pools.pools[0] = kStructPool;
    pools.pools[1] = nullptr;
    GameVariable<bool>(0x007CC979) = true;
    // The skater list: three skaters, then the sentinel (-1 at +4).
    for (int i = 0; i < 5; ++i) {
        std::memset(g_nodes[i], 0, sizeof(g_nodes[i]));
        *reinterpret_cast<uint8_t**>(g_nodes[i] + 0xC) = i < 4 ? g_nodes[i + 1] : nullptr;
    }
    *reinterpret_cast<int*>(g_nodes[3] + 4) = -1;
    *reinterpret_cast<uint8_t**>(g_skate + 0x34) = g_nodes[0];
    for (int i = 0; i < 4; ++i) {
        *reinterpret_cast<void***>(g_skaters[i]) = g_skaterVtable;
        g_skaterAt[i] = g_skaters[i];
    }
    g_skaterAt[1] = nullptr;
    g_localSkater = g_skaters[3];
}

// The front end and the globals the code sets.
void RecordState()
{
    for (size_t i = 0; i < sizeof(g_frontEnd); i += 4)
        g_trace.push_back(Id(*reinterpret_cast<uint32_t*>(g_frontEnd + i)));
    uint32_t instance = GameVariable<uint32_t>(0x007CE464);
    Record({GameVariable<uint32_t>(0x007CE46C), GameVariable<uint8_t>(0x007CE4A0), GameVariable<uint8_t>(0x006FA7D4),
            *reinterpret_cast<uint32_t*>(g_mlp + 8), instance ? Id(instance) : 0, GameVariable<uint32_t>(0x007CE468)});
}

// Runs `steps` once with the originals and once with ours (inside the stand-ins) and compares the traces.
void CompareRuns(const char* what, const std::function<void(bool)>& steps, bool standInAutoRepeat = true)
{
    g_saved = SavedGlobals();
    SaveAll(g_saved);
    g_saving = true;
    uint32_t local = reinterpret_cast<uint32_t>(&local);
    g_stackLow = local - 0x10000;
    g_stackHigh = local + 0x1000;
    std::vector<uint32_t> traces[2];
    {
        auto* s = new StandIns;
        InstallStandIns(*s, standInAutoRepeat);
        for (int run = 0; run < 2; ++run) {
            BeginRun();
            steps(run == 0);
            RecordState();
            traces[run] = g_trace;
        }
        RemoveStandIns();
    }
    RestoreAll(g_saved);
    g_saving = false;
    Compare(traces[0], traces[1], what);
}

template <typename Fn>
Fn Orig(uint32_t address) { return Original<Fn>(address); }

using CFunc = bool(__cdecl*)(CStruct*, Script::CScript*);
CStruct* Params() { return reinterpret_cast<CStruct*>(g_structs[kStructs - 1]); }

void CallCFunc(bool original, uint32_t address, CFunc ours)
{
    bool result = (original ? Orig<CFunc>(address) : ours)(Params(), reinterpret_cast<Script::CScript*>(g_script));
    Record({0xCF, result});
}

// Arrays for the getters.
struct ArrayBuilder {
    CArray arrays[64];
    uint32_t elements[64][40];
    int used = 0;
    CArray* Make(std::initializer_list<uint32_t> values)
    {
        CArray* a = &arrays[used];
        uint32_t* e = elements[used++];
        std::memset(a, 0, sizeof(*a));
        a->m_size = static_cast<uint32_t>(values.size());
        int i = 0;
        for (uint32_t v : values)
            e[i++] = v;
        if (values.size() == 1)
            a->m_single = e[0];
        else
            a->m_elements = e;
        return a;
    }
    // [[button event] ...]; a button "#hex" is that checksum.
    CArray* Pairs(std::initializer_list<std::pair<const char*, uint32_t>> pairs)
    {
        CArray* a = &arrays[used];
        uint32_t* e = elements[used++];
        int i = 0;
        for (auto& [name, event] : pairs) {
            uint32_t button = name[0] == '#' ? static_cast<uint32_t>(std::strtoul(name + 1, nullptr, 16))
                                             : Crc::GenerateCRCFromString(name);
            e[i++] = reinterpret_cast<uint32_t>(Make({button, event}));
        }
        std::memset(a, 0, sizeof(*a));
        a->m_size = static_cast<uint32_t>(pairs.size());
        if (pairs.size() == 1)
            a->m_single = e[0];
        else
            a->m_elements = e;
        return a;
    }
};
ArrayBuilder g_arrays;

void RecordTimes()
{
    Record({static_cast<uint32_t>(Fe()->m_autoRepeatDelay), static_cast<uint32_t>(Fe()->m_autoRepeatRate)});
}

// --- the script commands -------------------------------------------------------------------------------------

void TestFrontEndMappingCommands()
{
    CompareRuns("front end: SetMenuAutoRepeatTimes, SetMenuPadMappings, SetButtonEventMappings, "
                "SetAnalogStickActiveForMenus",
                [](bool o) {
                    // Auto-repeat times: seconds to ms, truncated (negative too); a missing pair is 0 0.
                    const float pairs[][2] = {{0.3f, 0.05f}, {1.2345f, 0.0071f}, {-0.5f, 2.9999f}, {0.0019f, -1.75f}};
                    g_getterFound = true;
                    for (auto& p : pairs) {
                        g_pair[0] = p[0];
                        g_pair[1] = p[1];
                        CallCFunc(o, 0x0054F0F0, Mdl::CFunc_SetMenuAutoRepeatTimes);
                        RecordTimes();
                    }
                    g_getterFound = false;
                    CallCFunc(o, 0x0054F0F0, Mdl::CFunc_SetMenuAutoRepeatTimes);
                    RecordTimes();

                    // Pad mappings.
                    g_arrays.used = 0;
                    g_getterFound = true;
                    const std::pair<uint32_t, uint32_t> mappings[] = {{0x742CF11F, 0xB4E103FD}, {0xAD6C73EE, 0x12345},
                                                                      {0xB4E103FD, 0xAD6C73EE}, {0, 0x742CF11F}};
                    for (auto& m : mappings) {
                        g_array = g_arrays.Make({m.first, m.second, 0xAD6C73EE});
                        CallCFunc(o, 0x0054F150, Mdl::CFunc_SetMenuPadMappings);
                        RecordState();
                    }

                    // Button events, on a table that starts with some pairs (the rest free).
                    for (int i = 0; i < FrontEnd::kMaxButtonEvents; ++i) {
                        Fe()->m_buttonEvents[i].m_button = static_cast<uint32_t>(i % 5);
                        Fe()->m_buttonEvents[i].m_event = i < 3 ? 0xE0000000u + i : (i % 4 == 0 ? 0xE1000000u + i : 0u);
                    }
                    g_array = g_arrays.Pairs({{"a", 0xAA000001}, {"b", 0xAA000002}, {"x", 0xAA000003},
                                              {"white", 0xAA000004}, {"black", 0xAA000005}, {"back", 0xAA000006},
                                              {"left_trigger_full", 0xAA000007}, {"not_a_button", 0xAA000008},
                                              {"right_stick_button", 0xAA000009}, {"#E0000001", 0xAA00000A},
                                              {"y", 0xE1000004}, {"right_trigger_full", 0xAA00000B},
                                              {"left_trigger_half", 0xAA00000C}, {"left_stick_button", 0xAA00000D}});
                    g_flags = {1};
                    CallCFunc(o, 0x0054F1B0, Mdl::CFunc_SetButtonEventMappings);
                    RecordState();
                    // A null button: the event's pairs go (then it takes a free slot as button 0); a null event:
                    // the button's pairs go (then a free slot gets (button, null)).
                    g_array = g_arrays.Pairs({{"null", 0xAA000002}, {"y", 0xDA3403B0}, {"a", 0xAA000010},
                                              {"#DA3403B0", 0xE0000000}, {"x", 0xDA3403B0}});
                    g_flags = {0, 1};
                    CallCFunc(o, 0x0054F1B0, Mdl::CFunc_SetButtonEventMappings);
                    RecordState();
                    // One pair (the array holds it itself); then more pairs than free slots.
                    g_array = g_arrays.Pairs({{"right_trigger_half", 0xAB000001}});
                    g_flags = {0, 0};
                    CallCFunc(o, 0x0054F1B0, Mdl::CFunc_SetButtonEventMappings);
                    RecordState();
                    g_array = g_arrays.Pairs({{"left_trigger_half", 0xAC000001}, {"right_trigger_full", 0xAC000002},
                                              {"left_stick_button", 0xAC000003}, {"back", 0xAC000004},
                                              {"a", 0xAC000005}, {"b", 0xAC000006}, {"x", 0xAC000007},
                                              {"y", 0xAC000008}, {"white", 0xAC000009}, {"black", 0xAC00000A},
                                              {"a", 0xAC00000B}, {"b", 0xAC00000C}, {"x", 0xAC00000D},
                                              {"y", 0xAC00000E}, {"white", 0xAC00000F}, {"black", 0xAC000010},
                                              {"a", 0xAC000011}, {"b", 0xAC000012}, {"x", 0xAC000013},
                                              {"y", 0xAC000014}, {"white", 0xAC000015}, {"black", 0xAC000016},
                                              {"a", 0xAC000017}, {"b", 0xAC000018}, {"x", 0xAC000019},
                                              {"y", 0xAC00001A}, {"white", 0xAC00001B}, {"black", 0xAC00001C},
                                              {"a", 0xAC00001D}, {"b", 0xAC00001E}, {"x", 0xAC00001F},
                                              {"y", 0xAC000020}, {"white", 0xAC000021}, {"black", 0xAC000022}});
                    g_flags = {1};
                    CallCFunc(o, 0x0054F1B0, Mdl::CFunc_SetButtonEventMappings);
                    RecordState();
                    // No xbox array.
                    g_getterFound = false;
                    g_flags = {0, 0};
                    CallCFunc(o, 0x0054F1B0, Mdl::CFunc_SetButtonEventMappings);
                    RecordState();

                    // The analog stick: missing (1), 0, 5.
                    for (int value : {-1, 0, 5}) {
                        g_getterFound = value >= 0;
                        g_integer = value;
                        CallCFunc(o, 0x0054FDC0, Mdl::CFunc_SetAnalogStickActiveForMenus);
                    }
                });
}

// --- pad events ----------------------------------------------------------------------------------------------

void SetEventTable()
{
    const uint32_t buttons[] = {6, 5, 33, 4, 6, 11, 31, 0, 7, 6};
    for (int i = 0; i < FrontEnd::kMaxButtonEvents; ++i) {
        Fe()->m_buttonEvents[i].m_button = i < 10 ? buttons[i] : 16 + (i & 1);
        Fe()->m_buttonEvents[i].m_event = i == 2 || i == 11 ? 0u : 0xEE000000u + i;
    }
    Fe()->m_buttonEvents[12].m_button = 1;
}

uint32_t Grab(bool o, uint32_t makes, int index, int device, uint32_t held)
{
    uint32_t event = o ? Orig<uint32_t(__thiscall*)(FrontEnd*, uint32_t, int, int, uint32_t)>(0x0054F600)(
                             Fe(), makes, index, device, held)
                       : Fe()->GrabControlEvent(makes, index, device, held);
    Record({0x6AB, event});
    return event;
}

void TestFrontEndGrabControlEvent()
{
    CompareRuns("front end: GrabControlEvent (button pairs, Start, the PC keyboard)", [](bool o) {
        SetEventTable();
        GameVariable<uint8_t>(0x007CE46E) = 0;
        // The pairs: the index-th match, button indexes masked to 5 bits, free slots skipped.
        for (uint32_t makes : {0x40u, 0x60u, 0x2u, 0x80000000u, 0x30001u, 0xFFFFFFFFu, 0u})
            for (int index : {0, 1, 2, 3, 40})
                Grab(o, makes, index, 1, 0);
        // Start: pad_start for what's left of the index being 0, unless 0x100 is held and the global is set.
        for (uint32_t held : {0u, 0x100u, 0x1FFu, 0x200u})
            for (uint32_t global : {0u, 1u})
                for (uint32_t makes : {0x800u, 0x840u, 0x1000u})
                    for (int index : {0, 1}) {
                        g_globals = {global};
                        Grab(o, makes, index, 1, held);
                    }
        // A negative index (no pair for Start's button): never pad_start.
        Fe()->m_buttonEvents[5].m_event = 0;
        for (int index : {-1, 0})
            Grab(o, 0x800, index, 1, 0);
        Fe()->m_buttonEvents[5].m_event = 0xEE000005;
        // The keyboard: only for device 0 with the on-screen keyboard up.
        struct Case {
            const char* keys;
            std::vector<uint32_t> existing;
        };
        const uint32_t kKeyboard = 0x31631B98, kMenu = 0xF53D1D83, kDialog = 0x3B56E746, kOther = 0x487FBC86;
        const Case cases[] = {
            {"", {}},
            {"ab", {kMenu}},
            {"\x1A", {kMenu}},
            {"\x1A", {}},
            {"q\x1A", {kDialog}},
            {"\x19", {kMenu}},
            {"\x19", {kDialog}},
            {"\x19", {}},
            {" ", {kMenu}},
            {" ", {kDialog}},
            {" ", {kKeyboard, kMenu}},
            {"\x19", {kKeyboard}},
            {" ", {kOther, kMenu}},
            {"\x19", {kOther, kDialog}},
            {"\x19 x\x19", {kMenu}},
            {"\x19\x1A", {kDialog}},
            {"\x18\x19", {kOther}},
        };
        for (int device : {0, 1})
            for (int keyboardUp : {0, 1})
                for (const Case& c : cases) {
                    GameVariable<uint8_t>(0x007CE46E) = static_cast<uint8_t>(keyboardUp);
                    g_keys = c.keys;
                    g_existing = c.existing;
                    g_globals.clear();
                    Grab(o, 0x1000, 0, device, 0);
                }
        // Start pressed with keys waiting: Start wins. A pair matched but the index not used up: the keyboard.
        GameVariable<uint8_t>(0x007CE46E) = 1;
        g_keys = "\x19";
        g_existing = {kMenu};
        Grab(o, 0x800, 0, 0, 0);
        Grab(o, 0x840, 1, 0, 0);
        Grab(o, 0x40, 5, 0, 0);
    });
}

void TestFrontEndKeyboardTask()
{
    CompareRuns("front end: the keyboard task (handle_keyboard_input per key)", [](bool o) {
        Mdl::FrontEndTask task = {};
        task.m_data = Fe();
        Fe()->m_keyboardDevice = 3;
        const char* const sequences[] = {"", "aZ~ 0", "\x19\x18\x1A\x7F", "\x80\xFF\x01\x1F\x17", "x\x19y\x18"};
        for (const char* keys : sequences) {
            g_keys = keys;
            (o ? Orig<void(__cdecl*)(Mdl::FrontEndTask*)>(0x0054F200) : &FrontEnd::KeyboardTask)(&task);
            Fe()->m_keyboardDevice += 2;
        }
    });
}

// --- per frame -----------------------------------------------------------------------------------------------

void TestFrontEndUpdate()
{
    CompareRuns("front end: Update and the logic task (menus up, music, scripts)", [](bool o) {
        const uint32_t kKeyboard = 0x31631B98, kMenu = 0xF53D1D83, kDialog = 0x3B56E746,
                       kQuit = Crc::GenerateCRCFromString("quit_dialog_anchor");
        const std::vector<uint32_t> anchors[] = {{}, {kKeyboard}, {kMenu}, {kDialog}, {kQuit}, {kKeyboard, kQuit}};
        int variant = 0;
        for (uint32_t level : {0u, 0xE92ECAFEu})
            for (int override : {0, 1})
                for (const auto& existing : anchors)
                    for (bool paused : {false, true}) {
                        ++variant;
                        *reinterpret_cast<uint32_t*>(g_skate + 0x374) = level;
                        GameVariable<uint8_t>(0x007CE46D) = static_cast<uint8_t>(override);
                        GameVariable<uint8_t>(0x007CE4A0) = variant % 3 == 0;
                        g_localSkater = variant % 2 ? g_skaters[2] : nullptr;
                        g_existing = existing;
                        if (variant % 4 == 1) { // through the logic task
                            Fe()->m_paused = paused;
                            Mdl::FrontEndTask task = {};
                            task.m_data = Fe();
                            (o ? Orig<void(__cdecl*)(Mdl::FrontEndTask*)>(0x0054FCC0) : &FrontEnd::LogicTask)(&task);
                        } else if (o) {
                            Orig<void(__thiscall*)(FrontEnd*, bool)>(0x0054F380)(Fe(), paused);
                        } else {
                            Fe()->Update(paused);
                        }
                        Record({GameVariable<uint32_t>(0x007CE46C), GameVariable<uint8_t>(0x007CE4A0)});
                    }
    });
}

void TestFrontEndPauseGame()
{
    CompareRuns("front end: PauseGame (devices, scripts, skaters, the view mode back)", [](bool o) {
        auto pause = [o](bool p) {
            if (o)
                Orig<void(__thiscall*)(FrontEnd*, bool)>(0x0054F960)(Fe(), p);
            else
                Fe()->PauseGame(p);
            RecordState();
        };
        Fe()->m_paused = false;
        pause(false); // already: nothing
        g_globals = {7};
        pause(true);
        pause(true);
        g_globals = {9};
        pause(false); // a local skater: SetViewMode
        g_localSkater = nullptr;
        pause(true);
        pause(false); // none
        // No skaters (the head is the sentinel); a list of one.
        *reinterpret_cast<int*>(g_nodes[0] + 4) = -1;
        pause(true);
        *reinterpret_cast<int*>(g_nodes[0] + 4) = 0;
        *reinterpret_cast<int*>(g_nodes[1] + 4) = -1;
        g_localSkater = g_skaters[0];
        g_globals = {0x55};
        pause(false);
        pause(true);
    });
}

void TestFrontEndInputLogic()
{
    CompareRuns("front end: InputLogic (pad events to the menus)", [](bool o) {
        SetEventTable();
        Mdl::FrontEndInputHandler handler = {};
        handler.m_data = Fe();
        handler.m_controlData = reinterpret_cast<Mdl::InputControlData*>(g_controlData);
        handler.m_device = g_devices[1];
        *reinterpret_cast<int*>(g_devices[1] + 0xC) = 6;
        GameVariable<uint8_t>(0x007CE46E) = 0;
        Fe()->m_padMappings[0].m_mapping = 2;
        Fe()->m_padMappings[1].m_mapping = 1;
        struct Case {
            int index;
            uint32_t makes, held;
            std::vector<uint32_t> repeats;
            std::vector<uint32_t> globals;
            bool check;
            uint32_t mapping;
            bool block;
        };
        const Case cases[] = {
            {0, 0x40, 0x10, {0xF00D}, {1}, true, 2, false}, // the startup script spawned; a repeat, 3 pair events
            {1, 0x40, 0x10, {0}, {0}, true, 1, false},      // not spawned
            {1, 0x60, 0x0, {0}, {}, false, 1, false},
            {0, 0x0, 0x0, {0xBEEF}, {}, false, 1, false},   // only the repeat
            {0, 0x0, 0x0, {0}, {}, false, 1, false},        // nothing
            {1, 0x800, 0x0, {0}, {}, false, 0, false},      // inactive: nothing sent
            {0, 0x40, 0x0, {0x77}, {}, false, 1, true},     // blocked
            {1, 0x880, 0x100, {0}, {0}, false, 1, false},   // Start after a pair
        };
        for (const Case& c : cases) {
            handler.m_index = c.index;
            *reinterpret_cast<uint32_t*>(g_controlData + 0x20) = c.makes;
            *reinterpret_cast<uint32_t*>(g_controlData + 0x1C) = c.held;
            g_repeats = c.repeats;
            g_globals = c.globals;
            Fe()->m_checkStartupScript = c.check;
            Fe()->m_padMappings[c.index].m_mapping = c.mapping;
            Fe()->m_blockMenuInput = c.block;
            (o ? Orig<void(__cdecl*)(Mdl::FrontEndInputHandler*)>(0x0054FB40) : &FrontEnd::InputLogic)(&handler);
            RecordState();
        }
    });
}

// --- construction and the input manager ----------------------------------------------------------------------

void TestFrontEndConstruction()
{
    CompareRuns("front end: constructor, Create, UpdateInputMappings, AddKeyboardHandler", [](bool o) {
        auto create = [o](bool c) {
            FrontEnd* fe = (o ? Orig<FrontEnd*(__cdecl*)(bool)>(0x0054FF80) : &Mdl::CreateFrontEnd)(c);
            Record({0xC4EA, fe ? Id(reinterpret_cast<uint32_t>(fe)) : 0});
            RecordState();
        };
        // Exists already; none and not asked; made; allocation fails (the blocks too, in a plain constructor).
        create(true);
        GameVariable<FrontEnd*>(0x007CE464) = nullptr;
        create(false);
        create(true);
        for (int i = 0; i < 2; ++i)
            for (size_t j = 0; j < 0x2C; j += 4)
                g_trace.push_back(Id(*reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(Fe()->m_inputHandlers[i]) + j)));
        for (Mdl::FrontEndTask* task : {Fe()->m_logicTask, Fe()->m_keyboardTask})
            for (size_t j = 0; j < 0x20; j += 4)
                g_trace.push_back(Id(*reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(task) + j)));
        GameVariable<FrontEnd*>(0x007CE464) = nullptr;
        g_callocFails = true;
        create(true);
        std::memset(g_frontEnd, 0xCD, sizeof(g_frontEnd));
        FrontEnd* constructed = o ? Orig<FrontEnd*(__thiscall*)(FrontEnd*)>(0x0054FE00)(Fe()) : Fe()->Construct();
        Record({Id(reinterpret_cast<uint32_t>(constructed))});
        RecordState();
        g_callocFails = false;

        // The handlers moved to the controllers' lists (missing ones skipped).
        auto update = [o] {
            if (o)
                Orig<void(__thiscall*)(FrontEnd*)>(0x0054FD00)(Fe());
            else
                Fe()->UpdateInputMappings();
            RecordState();
        };
        for (int d = 0; d < 2; ++d) {
            *reinterpret_cast<uint8_t**>(g_input + 4 + d * 0x84) = g_devices[d];
            Fe()->m_inputHandlers[d] = reinterpret_cast<Mdl::FrontEndInputHandler*>(g_handlers[d]);
        }
        Fe()->m_deviceServerMap[0] = 0;
        Fe()->m_deviceServerMap[1] = 1;
        update();
        Fe()->m_deviceServerMap[0] = 1;
        Fe()->m_deviceServerMap[1] = 0;
        update();
        Fe()->m_inputHandlers[0] = nullptr;
        update();
        for (int d = 0; d < 2; ++d)
            for (size_t i = 0; i < 0x2C; i += 4)
                g_trace.push_back(Id(*reinterpret_cast<uint32_t*>(g_handlers[d] + i)));

        // The keyboard task joins the logic tasks.
        Fe()->m_keyboardTask = reinterpret_cast<Mdl::FrontEndTask*>(g_tasks[1]);
        for (int device : {0, 5}) {
            if (o)
                Orig<void(__thiscall*)(FrontEnd*, int)>(0x0054EF60)(Fe(), device);
            else
                Fe()->AddKeyboardHandler(device);
            Record({static_cast<uint32_t>(Fe()->m_keyboardDevice)});
        }
        // SetAnalogStickActiveForMenus directly.
        for (bool active : {true, false}) {
            if (o)
                Orig<void(__thiscall*)(FrontEnd*, bool)>(0x0054F900)(Fe(), active);
            else
                Fe()->SetAnalogStickActiveForMenus(active);
        }
    });
}

// --- the auto-repeat ------------------------------------------------------------------------------------------

uint32_t Repeat(bool o, uint32_t held, int device)
{
    uint32_t event = o ? Orig<uint32_t(__thiscall*)(FrontEnd*, uint32_t, int)>(0x0054ECE0)(Fe(), held, device)
                       : Fe()->AutoRepeat(held, device);
    Record({0xA7, event});
    for (int i = 0; i < 2; ++i)
        Record({Fe()->m_padMappings[i].m_repeatState, Fe()->m_padMappings[i].m_repeatTimer});
    return event;
}

void TestFrontEndAutoRepeat()
{
    CompareRuns("front end: the auto-repeat (directions, the PC arrows, delay and rate, the startup script)",
                [](bool o) {
        for (int i = 0; i < 2; ++i) {
            Fe()->m_padMappings[i].m_repeatState = 0;
            Fe()->m_padMappings[i].m_repeatTimer = 0x1234;
        }
        Fe()->m_startupScriptOn = false;
        Fe()->m_startupScriptId = 0xABCD0123;
        Fe()->m_autoRepeatDelay = 300;
        Fe()->m_autoRepeatRate = 50;
        GameVariable<uint8_t>(0x007CE46E) = 0;
        // Each direction (and its priority over the others), held through the delay and some repeats, with
        // frames of 1/60, 1/20 (exactly the rate) and 1/3 s (past the timer), then let go.
        const uint32_t helds[] = {0x8000, 0x2000, 0x1000, 0x4000, 0xF000, 0x7000, 0x5000, 0x4FFF};
        const float slomos[] = {1.0f, 3.0f, 20.0f, 7.3f};
        int frame = 0;
        for (uint32_t held : helds) {
            for (int device : {0, 1}) {
                for (int i = 0; i < 9; ++i) {
                    GameVariable<float>(0x00684FC8) = slomos[(frame++ / 3) % 4];
                    Repeat(o, held, device);
                }
                Repeat(o, 0x0800, device); // let go (another button)
                Repeat(o, 0, device);
            }
        }
        // Odd times: a negative delay (a huge unsigned timer), a rate of 0.
        Fe()->m_autoRepeatDelay = -5;
        Fe()->m_autoRepeatRate = 0;
        GameVariable<float>(0x00684FC8) = 3.0f;
        for (int i = 0; i < 4; ++i)
            Repeat(o, 0x1000, 1);
        // A state that isn't 1 stays as it is when the timer runs out.
        Fe()->m_padMappings[1].m_repeatState = 7;
        Fe()->m_padMappings[1].m_repeatTimer = 10;
        Repeat(o, 0x1000, 1);
        // Long frames against a long delay (the milliseconds' rounding).
        Fe()->m_autoRepeatDelay = 2000;
        GameVariable<float>(0x00684FC8) = 59.97f;
        Repeat(o, 0, 0);
        for (int i = 0; i < 3; ++i)
            Repeat(o, 0x8000, 0);
        GameVariable<float>(0x00684FC8) = 3.0f;
        Fe()->m_autoRepeatDelay = 120;
        Fe()->m_autoRepeatRate = 70;
        // The PC arrows: controller 0 only, the on-screen keyboard up, no direction held; the first arrow.
        const char* const keys[] = {"\x14", "\x15", "\x16", "\x17", "ab\x17\x14", "\x13\x18", "", "\x16\x15",
                                    "\x14\x17"};
        for (int keyboardUp : {0, 1})
            for (int device : {0, 1})
                for (uint32_t held : {0u, 0x2000u})
                    for (const char* k : keys) {
                        GameVariable<uint8_t>(0x007CE46E) = static_cast<uint8_t>(keyboardUp);
                        g_keys = k;
                        Repeat(o, held, device);
                        Repeat(o, held, device);
                        g_keys.clear();
                        Repeat(o, 0, device);
                    }
        GameVariable<uint8_t>(0x007CE46E) = 0;
        // The startup script: controller 0 gets nothing while it runs (unpaused each frame), then it's over.
        Fe()->m_startupScriptOn = true;
        for (bool found : {true, true, false, false}) {
            g_scriptFound = found;
            Repeat(o, 0x8000, 1);
            Repeat(o, 0x8000, 0);
            Record({Fe()->m_startupScriptOn});
        }
    }, false);
}

// --- the module: start, stop, destruction --------------------------------------------------------------------

void SetModuleNodes()
{
    Fe()->m_logicTask = reinterpret_cast<Mdl::FrontEndTask*>(g_tasks[0]);
    Fe()->m_keyboardTask = reinterpret_cast<Mdl::FrontEndTask*>(g_tasks[1]);
    for (int i = 0; i < 2; ++i) {
        *reinterpret_cast<void***>(g_tasks[i]) = g_nodeVtable;
        *reinterpret_cast<void***>(g_handlers[i]) = g_nodeVtable;
        Fe()->m_inputHandlers[i] = reinterpret_cast<Mdl::FrontEndInputHandler*>(g_handlers[i]);
    }
}

void TestFrontEndModule()
{
    CompareRuns("front end: v_start_cb, v_stop_cb, PadsPluggedIn, RemoveKeyboardHandler, destructor, Release",
                [](bool o) {
        SetModuleNodes();
        for (int d = 0; d < 2; ++d)
            *reinterpret_cast<uint8_t**>(g_input + 4 + d * 0x84) = g_devices[d];
        // Start: each handler to its own index's controller (the map is ignored), the logic task.
        Fe()->m_deviceServerMap[0] = 1;
        Fe()->m_deviceServerMap[1] = 0;
        Fe()->m_inputHandlers[0]->m_index = 1;
        Fe()->m_inputHandlers[1]->m_index = 0;
        auto start = [o] {
            if (o)
                Orig<void(__thiscall*)(FrontEnd*)>(0x0054EEA0)(Fe());
            else
                Fe()->Start();
            RecordState();
            for (int d = 0; d < 2; ++d)
                for (size_t i = 0; i < 0x2C; i += 4)
                    g_trace.push_back(Id(*reinterpret_cast<uint32_t*>(g_handlers[d] + i)));
        };
        start();
        Fe()->m_inputHandlers[0]->m_index = 0;
        start();
        // Stop; the keyboard handler out.
        if (o)
            Orig<void(__thiscall*)(FrontEnd*)>(0x0054EF30)(Fe());
        else
            Fe()->Stop();
        for (int task : {1, 0}) {
            Fe()->m_keyboardTask = reinterpret_cast<Mdl::FrontEndTask*>(g_tasks[task]);
            if (o)
                Orig<void(__thiscall*)(FrontEnd*)>(0x0054ECD0)(Fe());
            else
                Fe()->RemoveKeyboardHandler();
        }
        // PadsPluggedIn: either controller.
        for (int i = 0; i < 2; ++i)
            Fe()->m_inputHandlers[i]->m_device = g_padDevices[i];
        for (int mask = 0; mask < 4; ++mask) {
            std::memset(g_padDevices, 0x5A, sizeof(g_padDevices));
            for (int i = 0; i < 2; ++i)
                reinterpret_cast<Sio::Device*>(g_padDevices[i])->m_pluggedIn = ((mask >> i) & 1) != 0;
            bool r = o ? Orig<bool(__thiscall*)(FrontEnd*)>(0x0054ECA0)(Fe()) : Fe()->PadsPluggedIn();
            Record({0x9106, r});
        }
        // The destructor: freed or not; missing tasks and handlers.
        auto destroy = [o](unsigned int flags) {
            FrontEnd* r = o ? Orig<FrontEnd*(__thiscall*)(FrontEnd*, unsigned int)>(0x0054EE70)(Fe(), flags)
                            : Fe()->Destroy(flags);
            Record({0xDE57, Id(reinterpret_cast<uint32_t>(r))});
            RecordState();
        };
        Fe()->m_vtable = g_frontEndVtable;
        destroy(0);
        SetModuleNodes();
        Fe()->m_logicTask = nullptr;
        Fe()->m_inputHandlers[1] = nullptr;
        destroy(3);
        SetModuleNodes();
        Fe()->m_inputHandlers[0] = nullptr;
        destroy(2);
        SetModuleNodes();
        if (o)
            Orig<void(__thiscall*)(FrontEnd*)>(0x0054EC20)(Fe());
        else
            Fe()->DestroyBody();
        RecordState();
        // Release: references left; the last one; none made.
        auto release = [o] {
            (o ? Orig<void(__cdecl*)()>(0x0054EBE0) : &Mdl::ReleaseFrontEnd)();
            RecordState();
        };
        for (uint32_t refs : {3u, 2u, 1u, 0u}) {
            Fe()->m_vtable = g_frontEndVtable;
            GameVariable<FrontEnd*>(0x007CE464) = Fe();
            GameVariable<uint32_t>(0x007CE468) = refs;
            release();
        }
        GameVariable<FrontEnd*>(0x007CE464) = nullptr;
        GameVariable<uint32_t>(0x007CE468) = 1;
        release();
    });
}

// --- the competition pieces ----------------------------------------------------------------------------------

alignas(16) uint8_t g_competition[2][0x160];

void TestCompetitionPieces()
{
    g_saved = {{0x00690FA0, 4, {}}};
    SaveAll(g_saved);
    g_saving = true;
    std::vector<uint32_t> traces[2];
    {
        auto* s = new StandIns;
        s->At({0x00478540, AddressOf(&CStruct::AppendStructureIfAny)}, 4, [](Regs& r) {
            Record({0xA99E, r.ecx, r.args[0]});
            return 0u;
        });
        for (int run = 0; run < 2; ++run) {
            bool o = run == 0;
            g_trace.clear();
            GameVariable<int>(0x00690FA0) = 40;
            uint8_t* memory = g_competition[run];
            for (size_t i = 0; i < sizeof(g_competition[0]); ++i)
                memory[i] = static_cast<uint8_t>(0x50 + i * 3);
            auto* competition = reinterpret_cast<Mdl::CCompetition*>(memory);
            *reinterpret_cast<uint32_t*>(memory) = 0x00C0FFEE; // its parameters
            // The leaders constructed (the original leaves each name's capacity alone: not compared).
            for (int i = 0; i < 8; ++i) {
                auto* leader = &competition->m_leaders[i];
                auto* result = o ? Orig<Mdl::CCompetition::Leader*(__thiscall*)(void*)>(0x0054FFD0)(leader)
                                 : leader->Construct();
                Record({reinterpret_cast<uint32_t>(result) - reinterpret_cast<uint32_t>(memory)});
                *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(leader) + 8) = 0;
            }
            Record({static_cast<uint32_t>(GameVariable<int>(0x00690FA0))});
            for (uint32_t params : {0x1234u, 0u}) {
                if (o)
                    Orig<void(__thiscall*)(void*, uint32_t)>(0x00550010)(competition, params);
                else
                    competition->EditParams(reinterpret_cast<CStruct*>(params));
            }
            for (int i = 0; i < 2; ++i) {
                if (o)
                    Orig<void(__thiscall*)(void*)>(0x00550020)(competition);
                else
                    competition->StartRun();
            }
            if (o)
                Orig<void(__thiscall*)(void*)>(0x0054FFF0)(competition);
            else
                competition->DestroyBody();
            Record({static_cast<uint32_t>(GameVariable<int>(0x00690FA0))});
            for (size_t i = 0; i < sizeof(g_competition[0]); i += 4)
                g_trace.push_back(*reinterpret_cast<uint32_t*>(memory + i));
            traces[run] = g_trace;
        }
        RemoveStandIns();
    }
    RestoreAll(g_saved);
    g_saving = false;
    Compare(traces[0], traces[1], "competition: leaders' constructor and destructor, EditParams, StartRun");
}

} // namespace

SELFTEST(Early, TestFrontEndMappingCommands, RestoreMenuTest);
SELFTEST(Early, TestFrontEndGrabControlEvent, RestoreMenuTest);
SELFTEST(Early, TestFrontEndKeyboardTask, RestoreMenuTest);
SELFTEST(Early, TestFrontEndUpdate, RestoreMenuTest);
SELFTEST(Early, TestFrontEndPauseGame, RestoreMenuTest);
SELFTEST(Early, TestFrontEndInputLogic, RestoreMenuTest);
SELFTEST(Early, TestFrontEndConstruction, RestoreMenuTest);
SELFTEST(Early, TestFrontEndAutoRepeat, RestoreMenuTest);
SELFTEST(Early, TestFrontEndModule, RestoreMenuTest);
SELFTEST(Early, TestCompetitionPieces, RestoreMenuTest);
