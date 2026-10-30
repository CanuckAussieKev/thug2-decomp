// Self-tests for the script commands 0x005A5360-0x005A595F (world_cfuncs.cpp: light groups, the skaters' pause,
// the viewer, the weather and the screen effects): each original against ours on the same params; every game
// function they call is stood in for by recording code.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/skate/skate_module.h"
#include "game/core/mem.h"
#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/pool.h"
#include "game/obj/compositemgr.h"
#include "game/obj/object.h"
#include "game/script/struct.h"
#include "game/script/world_cfuncs.h"
#include "game/sys/module.h"
#include "game/sys/viewer.h"
#include "game/script/game_cfuncs.h"
#include "hook/cfunc_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using namespace CFuncTests;
using Script::CStruct;

// The skate module: its skater list's head at +0x28 (next at +0x34), nodes with data at +4 and next at +0xC.
struct FakeNode {
    uint32_t unknown0;
    intptr_t data;
    uint32_t unknown8;
    FakeNode* next;
};
alignas(16) uint8_t g_skate[0x40];
FakeNode g_nodes[4];
int g_skaterCount;
// Skaters: vtable slots 11 and 12 (pause / unpause) are recording thunks.
alignas(16) uint8_t g_skaters[3][0x110];
void* g_skaterVtable[16];
bool g_skaterMissing[3];

alignas(16) uint8_t g_gameNet[0x200];
uint32_t g_observing;
uint32_t g_nodeCommandResult;

alignas(16) uint8_t g_viewerMemory[0x60];
bool g_viewerMemoryMissing;
alignas(16) uint8_t g_structMemory[0x10];
alignas(16) uint8_t g_cameraObject[0x110];
uint32_t g_arrayResult;

void BuildSkaterList(int count)
{
    std::memset(g_skate, 0, sizeof(g_skate));
    std::memset(g_nodes, 0, sizeof(g_nodes));
    g_skaterCount = count;
    FakeNode* head = &g_nodes[0];
    head->data = -1;
    FakeNode* previous = head;
    for (int i = 1; i <= count; ++i) {
        g_nodes[i].data = 0x5000 + i;
        previous->next = &g_nodes[i];
        previous = &g_nodes[i];
    }
    previous->next = head;
    // the head inside the module: its next at +0x34
    *reinterpret_cast<FakeNode**>(g_skate + 0x34) = head->next ? head->next : head;
    if (count == 0)
        *reinterpret_cast<FakeNode**>(g_skate + 0x34) = head;
}

uint32_t SkaterIndex(uint32_t object)
{
    for (uint32_t i = 0; i < 3; ++i)
        if (object == reinterpret_cast<uint32_t>(g_skaters[i]))
            return i + 1;
    return object == reinterpret_cast<uint32_t>(g_cameraObject) ? 0xCA : object;
}

void StandInCalls(StandIns& s)
{
    s.At({0x005A4EF0, AddressOf(&CFuncs::NodeCommandByName)}, 0, [](Regs& r) {
        Record({0x4EF0, r.args[0], r.args[1] == reinterpret_cast<uint32_t>(g_script) ? 1u : r.args[1], r.args[2]});
        return g_nodeCommandResult;
    });
    s.At({0x005665C0, AddressOf(&Mdl::Skate::GetSkater)}, 4, [](Regs& r) {
        Record({0x65C0, r.ecx == reinterpret_cast<uint32_t>(g_skate) ? 1u : r.ecx, r.args[0]});
        uint32_t i = r.args[0];
        return i < 3 && !g_skaterMissing[i] ? reinterpret_cast<uint32_t>(g_skaters[i]) : 0u;
    });
    g_skaterVtable[11] = s.Thunk(0, [](Regs& r) { Record({0x2C, SkaterIndex(r.ecx)}); return 0u; });
    g_skaterVtable[12] = s.Thunk(0, [](Regs& r) { Record({0x30, SkaterIndex(r.ecx)}); return 0u; });
    s.At({0x0045D990, AddressOf(&Obj::CCompositeObject::Hide)}, 4, [](Regs& r) {
        Record({0xD990, SkaterIndex(r.ecx), r.args[0] & 0xFF});
        return 0u;
    });
    s.At({0x005A0D90, AddressOf(&CFuncs::CFunc_IsObserving)}, 0, [](Regs& r) { Record({0x0D90, r.args[0], r.args[1]}); return g_observing; });
    s.At({0x00564FC0, AddressOf(static_cast<Obj::CSkaterProfile* (Mdl::Skate::*)(int)>(&Mdl::Skate::GetProfile))}, 4, [](Regs& r) {
        Record({0x4FC0, r.ecx == reinterpret_cast<uint32_t>(g_skate) ? 1u : r.ecx, r.args[0]});
        return 0x0F0F1234u;
    });
    s.At({0x00567180, AddressOf(&Mdl::Skate::AddSkater)}, 16, [](Regs& r) {
        Record({0x7180, r.ecx == reinterpret_cast<uint32_t>(g_skate) ? 1u : r.ecx, r.args[0], r.args[1] & 0xFF,
                r.args[2], r.args[3]});
        return 0u;
    });
    // LaunchViewer's calls
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Record({0x1CC0, r.args[0]});
        return g_viewerMemoryMissing ? 0u : reinterpret_cast<uint32_t>(g_viewerMemory);
    });
    s.At({0x00577F20, AddressOf(&Mdl::Viewer::Construct)}, 0, [](Regs& r) {
        Record({0x7F20, r.ecx == reinterpret_cast<uint32_t>(g_viewerMemory) ? 1u : r.ecx});
        return r.ecx + 0x10; // (a different pointer, to see which one is used)
    });
    s.At({0x0044F3C0, AddressOf(&Mdl::Manager::RegisterModule)}, 4, [](Regs& r) {
        Record({0xF3C0, r.ecx, r.args[0] - reinterpret_cast<uint32_t>(g_viewerMemory)});
        return 0u;
    });
    s.At({0x0044F310, AddressOf(&Mdl::Manager::StartModule)}, 4, [](Regs& r) {
        Record({0xF310, r.ecx, r.args[0] - reinterpret_cast<uint32_t>(g_viewerMemory)});
        return 0u;
    });
    s.At({0x005BA3D0, AddressOf(&Mem::Pool::Allocate)}, 0, [](Regs& r) {
        Record({0xA3D0, r.ecx});
        return reinterpret_cast<uint32_t>(g_structMemory);
    });
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Record({0xAFC0, r.args[0], r.args[1] & 0xFF, r.args[2] & 0xFF});
        return reinterpret_cast<uint32_t>(g_structMemory);
    });
    using AddChecksumFn = void (CStruct::*)(const char*, uint32_t);
    s.At({0x00477DF0, AddressOf(static_cast<AddChecksumFn>(&CStruct::AddChecksum))}, 8, [](Regs& r) {
        Record({0x7DF0, r.ecx == reinterpret_cast<uint32_t>(g_structMemory) ? 1u : r.ecx,
                Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1]});
        return 0u;
    });
    using GetArrayFn = Script::CArray* (*)(const char*);
    s.At({0x00479090, AddressOf(static_cast<GetArrayFn>(&Script::GetArray))}, 0, [](Regs& r) {
        Record({0x9090, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return g_arrayResult;
    });
    s.At({0x0045F3A0, AddressOf(&Obj::CCompositeObjectManager::CreateCompositeObjectFromNode)}, 12, [](Regs& r) {
        Record({0xF3A0, r.ecx, r.args[0], r.args[1] == reinterpret_cast<uint32_t>(g_structMemory) ? 1u : r.args[1],
                r.args[2] & 0xFF});
        return reinterpret_cast<uint32_t>(g_cameraObject);
    });
    s.At({0x00477130, AddressOf(&CStruct::Clear)}, 0, [](Regs& r) {
        Record({0x7130, r.ecx == reinterpret_cast<uint32_t>(g_structMemory) ? 1u : r.ecx});
        return 0u;
    });
    s.At({0x00409070, AddressOf(&Script::DeleteStruct)}, 0, [](Regs& r) {
        Record({0x9070, r.args[0] == reinterpret_cast<uint32_t>(g_structMemory) ? 1u : r.args[0]});
        return 0u;
    });
    // The weather's and the screen effects' methods (thiscall, params and script): each records its address.
    for (uint32_t address : {0x004A10B0u, 0x004A10D0u, 0x004A1110u, 0x004A1130u, 0x004A1190u, 0x004A11B0u,
                             0x004A1240u, 0x004A1260u, 0x004A1280u, 0x004A12A0u, 0x004A12F0u, 0x004A1350u,
                             0x004A1370u, 0x004A13B0u, 0x004A13D0u, 0x004A1430u, 0x004A1450u, 0x004A14A0u,
                             0x004A14B0u, 0x0049E320u, 0x0049E310u, 0x0049E4B0u}) {
        s.At({address, OursOf(address)}, 8, [address](Regs& r) {
            Record({address, r.ecx, r.args[0], r.args[1] == reinterpret_cast<uint32_t>(g_script) ? 1u : r.args[1]});
            return (address >> 4) & 1 ? 0x101u : 0x100u; // a result that differs between the methods
        });
    }
    s.At({0x004A1090}, 0, [](Regs& r) { Record({0x1090, r.ecx}); return 0u; });
}

struct WeatherCommand {
    uint32_t address;
    CFunc ours;
};

void TestWorldCFuncs()
{
    std::vector<Saved> saved = {{0x007CE478, 4, {}}, {0x007CCE14, 4, {}}, {0x007CEBF0, 1, {}}, {0x00696D78, 4, {}},
                                {0x006F3614, 16, {}}, {0x007CC979, 1, {}}, {0x006F0FB8, 4, {}}, {0x006FA7C8, 8, {}},
                                {0x007CEFA0, 4, {}}, {0x006F3624, 4, {}}};
    SaveAll(saved);
    UseEmptySymbolTable();
    auto* standIns = new StandIns;
    StandInCalls(*standIns);

    const WeatherCommand weather[] = {
        {0x005A5690, CFuncs::CFunc_WeatherSetRainHeight},   {0x005A56B0, CFuncs::CFunc_WeatherSetRainFrames},
        {0x005A56D0, CFuncs::CFunc_WeatherSetRainLength},   {0x005A56F0, CFuncs::CFunc_WeatherSetRainBlendMode},
        {0x005A5710, CFuncs::CFunc_WeatherSetRainRate},     {0x005A5730, CFuncs::CFunc_WeatherSetRainColor},
        {0x005A5750, CFuncs::CFunc_WeatherSetSplashRate},   {0x005A5770, CFuncs::CFunc_WeatherSetSplashLife},
        {0x005A5790, CFuncs::CFunc_WeatherSetSplashSize},   {0x005A57B0, CFuncs::CFunc_WeatherSetSplashColor},
        {0x005A57D0, CFuncs::CFunc_WeatherSetSplashBlendMode}, {0x005A57F0, CFuncs::CFunc_WeatherSetSnowHeight},
        {0x005A5810, CFuncs::CFunc_WeatherSetSnowFrames},   {0x005A5830, CFuncs::CFunc_WeatherSetSnowSize},
        {0x005A5850, CFuncs::CFunc_WeatherSetSnowBlendMode}, {0x005A5870, CFuncs::CFunc_WeatherSetSnowRate},
        {0x005A5890, CFuncs::CFunc_WeatherSetSnowColor},    {0x005A58B0, CFuncs::CFunc_WeatherSetSnowActive},
        {0x005A58D0, CFuncs::CFunc_WeatherSetRainActive},   {0x005A58F0, CFuncs::CFunc_SetScreenEffectsOn},
        {0x005A5910, CFuncs::CFunc_SetScreenEffectsOff},    {0x005A5930, CFuncs::CFunc_SetScreenEffectsDOFParams},
        {0x005A5680, CFuncs::CFunc_WeatherUpdateGrid},
    };

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        auto call = [&](uint32_t address, CFunc ours, Params& params) { Call(original, address, ours, params); };
        GameVariable<void*>(0x007CE478) = g_skate;
        GameVariable<void*>(0x007CCE14) = g_gameNet;
        // (the weather: an object with a vtable whose slot 1 - UpdateGrid, called through it - is the stood-in 0x004A1090)
        static void* weatherVtable[4];
        static void* weatherObject;
        weatherVtable[1] = reinterpret_cast<void*>(0x004A1090);
        weatherObject = weatherVtable;
        GameVariable<uint32_t>(0x006FA7C8) = reinterpret_cast<uint32_t>(&weatherObject);
        GameVariable<uint32_t>(0x006FA7CC) = 0x05C2EE00;
        for (int i = 0; i < 3; ++i) {
            std::memset(g_skaters[i], 0, sizeof(g_skaters[i]));
            *reinterpret_cast<void***>(g_skaters[i]) = g_skaterVtable;
        }
        Params none;
        // SetLightGroup / IsAlive
        for (uint32_t result : {0x0u, 0x1u, 0x7701u}) {
            g_nodeCommandResult = result;
            Params a; a.Checksum(0xA1DC81F9, 0x1234);
            call(0x005A5360, CFuncs::CFunc_SetLightGroup, a);
            call(0x005A5380, CFuncs::CFunc_IsAlive, a);
        }
        // PauseSkaters (hide or not; a missing skater, one with +0xBC clear) / UnPauseSkaters
        for (int count : {0, 1, 3}) {
            BuildSkaterList(count);
            g_skaterMissing[0] = false;
            g_skaterMissing[1] = true;
            g_skaterMissing[2] = false;
            g_skaters[0][0xBC] = 1;
            g_skaters[2][0xBC] = 0;
            Params hide; hide.Flag(Crc::GenerateCRCFromString("hide"));
            call(0x005A53A0, CFuncs::CFunc_PauseSkaters, hide);
            call(0x005A53A0, CFuncs::CFunc_PauseSkaters, none);
            g_skaters[2][0xBC] = 7;
            call(0x005A53A0, CFuncs::CFunc_PauseSkaters, hide);
            call(0x005A5420, CFuncs::CFunc_UnPauseSkaters, none);
            g_skaterMissing[1] = false;
            g_skaterMissing[0] = true;
            call(0x005A5420, CFuncs::CFunc_UnPauseSkaters, none);
        }
        // SkatersAreReady: not ready; ready with skaters; ready without, observing or not
        for (int count : {0, 1, 2}) {
            BuildSkaterList(count);
            for (int ready : {0, 1}) {
                for (uint32_t observing : {0x0u, 0x1u}) {
                    g_gameNet[0x18C] = static_cast<uint8_t>(ready);
                    g_observing = observing;
                    call(0x005A5480, CFuncs::CFunc_SkatersAreReady, none);
                }
            }
        }
        // AddSkaterEarly: with no skater and with some
        for (int count : {0, 1, 2}) {
            BuildSkaterList(count);
            call(0x005A5610, CFuncs::CFunc_AddSkaterEarly, none);
        }
        // LaunchViewer: the first time (pool or heap; viewer memory or not), then again (nothing)
        for (int variant = 0; variant < 3; ++variant) {
            GameVariable<bool>(0x007CEBF0) = false;
            GameVariable<uint32_t>(0x00696D78) = 0x0D0D0000 + variant;
            GameVariable<uint32_t>(0x006F0FB8) = 0x0C0B0000 + variant;
            GameVariable<bool>(0x007CC979) = true;
            std::memset(reinterpret_cast<void*>(0x006F3614), 0, 16);
            GameVariable<uint32_t>(0x006F3614) = variant == 2 ? 1 : 0;
            if (variant != 1)
                GameVariable<uint32_t>(0x006F361C + 4 * (variant == 2 ? 1 : 0)) = 0x0B0B0000 + variant;
            g_viewerMemoryMissing = variant == 2;
            g_arrayResult = 0x0A770000 + variant;
            std::memset(g_cameraObject, 0, sizeof(g_cameraObject));
            g_cameraObject[8] = static_cast<uint8_t>(0x41 + variant);
            call(0x005A54E0, CFuncs::CFunc_LaunchViewer, none);
            Record({GameVariable<uint8_t>(0x007CEBF0), *reinterpret_cast<uint32_t*>(g_cameraObject + 8)});
            call(0x005A54E0, CFuncs::CFunc_LaunchViewer, none);
        }
        // the weather and the screen effects
        for (const WeatherCommand& command : weather) {
            Params a; a.Integer(0x1111, 5);
            call(command.address, command.ours, a);
        }
        // IsPal
        for (int setting : {0, 1, 2, 3, 5}) {
            GameVariable<int>(0x007CEFA0) = setting;
            call(0x005A5950, CFuncs::CFunc_IsPal, none);
        }
        traces[run] = g_trace;
    }
    RemoveStandIns();
    RestoreAll(saved);
    Check(traces[0].size() > 300, "world commands: the original reached its calls");
    Compare(traces[0], traces[1], "world commands (0x005A5360-0x005A5950)");
}

} // namespace

SELFTEST(Early, TestWorldCFuncs, RemoveStandIns);
