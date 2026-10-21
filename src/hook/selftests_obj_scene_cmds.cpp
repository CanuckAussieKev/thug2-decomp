// Self-tests for the engine's script commands 0x00445000-0x00445B30 (cfuncs_scene.cpp: terrain sounds, models,
// scenes and collision, animations and assets, the configuration, the sun): each original against ours on the same
// params; every game function they call is stood in for by recording code (the asset and memory managers by stand-in
// objects), so they're compared call by call.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/ass/assman.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/file/async.h"
#include "game/gfx/lens_flare.h"
#include "game/sfx/terrain.h"
#include "game/script/cfuncs_scene.h"
#include "game/script/struct.h"
#include "hook/cfunc_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using namespace CFuncTests;

// Stand-in objects: the asset manager, the memory manager, scenes, an asset, a model.
alignas(16) uint8_t g_assets[0x40];
alignas(16) uint8_t g_manager[0x600];
alignas(16) uint8_t g_scenes[2][0x100];
alignas(16) uint8_t g_asset[0x40];
alignas(16) uint8_t g_model[0x40];
// What the stand-ins answer.
bool g_sceneFound;   // 0x0048C750 finds the scene
bool g_firstScene;   // there's a first scene in the table
bool g_assetFound;   // 0x00407B90 finds the asset
uint32_t g_loadResult;
const char g_hardware[] = "hardware";
const char g_display[] = "display";
const char g_language[] = "language";
const char g_territory[] = "territory";

uint32_t Addr(const void* p) { return reinterpret_cast<uint32_t>(p); }
// A string argument: which of the test's strings it is (by content).
uint32_t Text(uint32_t p) { return Hash(reinterpret_cast<const char*>(p)); }

void StandInCalls(StandIns& s)
{
    s.At({0x00444EB0, AddressOf(&Terrain::LoadTerrainSounds)}, 0, [](Regs& r) { Record({0x4EB0, r.args[0]}); return 1u; });
    s.At({0x0048BFA0}, 0, [](Regs& r) { Record({0xBFA0, r.args[0]}); return Addr(g_model); });
    // <name>.mdl: a String in the caller's storage (no text: copying it allocates nothing)
    s.At({0x004879D0}, 0, [](Regs& r) {
        Record({0x79D0, Text(r.args[1]), Text(r.args[2])});
        auto* string = reinterpret_cast<uint32_t*>(r.args[0]);
        string[0] = 0x5E41;
        string[1] = 0;
        string[2] = 0;
        return r.args[0];
    });
    s.At({0x004935E0}, 32, [](Regs& r) {
        Record({0x35E0, r.ecx == Addr(g_model), r.args[0]});
        for (int i = 1; i < 8; ++i)
            Record({r.args[i] & 0xFF});
        return 0u;
    });
    s.At({0x0048C3C0}, 0, [](Regs& r) { Record({0xC3C0, r.args[0] == Addr(g_model)}); return 0u; });
    s.At({0x0048C810}, 0, [](Regs&) { Record({0xC810}); return 0u; });
    s.At({0x0048C750}, 0, [](Regs& r) {
        Record({0xC750, Text(r.args[0])});
        return g_sceneFound ? Addr(g_scenes[0]) : 0u;
    });
    s.At({0x0048C770}, 0, [](Regs&) { Record({0xC770}); return g_firstScene ? Addr(g_scenes[1]) : 0u; });
    s.At({0x0049DF70}, 8, [](Regs& r) { Record({0xDF70, r.ecx, Text(r.args[0]), r.args[1] & 0xFF}); return 0u; });
    s.At({0x0049DFD0}, 4, [](Regs& r) { Record({0xDFD0, r.ecx, Text(r.args[0])}); return 1u; });
    s.At({0x0048CEF0}, 0, [](Regs& r) { Record({0xCEF0, Text(r.args[0])}); return 0u; });
    s.At({0x0048C690}, 0, [](Regs& r) { Record({0xC690, r.args[0], r.args[1]}); return 0u; });
    s.At({0x0049EFD0}, 0, [](Regs& r) { Record({0xEFD0, r.args[0]}); return 0u; });
    s.At({0x0048CDD0}, 0, [](Regs& r) { Record({0xCDD0, Text(r.args[0]), Text(r.args[1])}); return 0u; });
    // the asset manager (the game's and ours)
    using Ass::CAssMan;
    s.At({0x00407B90, AddressOf(&CAssMan::GetAsset)}, 8, [](Regs& r) {
        Record({0x7B90, r.ecx == Addr(g_assets), r.args[0], r.args[1]});
        return g_assetFound ? Addr(g_asset) : 0u;
    });
    s.At({0x00407B20, AddressOf(&CAssMan::UnloadAsset)}, 4, [](Regs& r) {
        Record({0x7B20, r.ecx == Addr(g_assets), r.args[0] == Addr(g_asset)});
        return 0u;
    });
    s.At({0x004087B0, AddressOf(&CAssMan::LoadOrGetAsset)}, 36, [](Regs& r) {
        Record({0x87B0, r.ecx == Addr(g_assets), Text(r.args[0]), r.args[1], r.args[2], r.args[3], r.args[4] & 0xFF,
                r.args[5] & 0xFF, r.args[6] & 0xFF, r.args[7], r.args[8]});
        return g_loadResult;
    });
    s.At({0x00408870, AddressOf(&CAssMan::LoadAnim)}, 20, [](Regs& r) {
        Record({0x8870, r.ecx == Addr(g_assets), Text(r.args[0]), r.args[1], r.args[2], r.args[3] & 0xFF,
                r.args[4] & 0xFF});
        return g_loadResult;
    });
    s.At({0x00407A70, AddressOf(&CAssMan::SetUnknown18)}, 4, [](Regs& r) {
        Record({0x7A70, r.ecx == Addr(g_assets), r.args[0]});
        return 0u;
    });
    s.At({0x00407A50, AddressOf(&CAssMan::SetPermanentByDefault)}, 4, [](Regs& r) {
        Record({0x7A50, r.ecx == Addr(g_assets), r.args[0] & 0xFF});
        return 0u;
    });
    using PushFn = void (Mem::Manager::*)(Mem::Allocator*);
    s.At({0x005BB0E0, AddressOf(static_cast<PushFn>(&Mem::Manager::PushContext))}, 4, [](Regs& r) {
        Record({0xB0E0, r.ecx == Addr(g_manager), r.args[0]});
        return 0u;
    });
    s.At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [](Regs& r) {
        Record({0xB110, r.ecx == Addr(g_manager)});
        return 0u;
    });
    s.At({0x005B9DF0, AddressOf(&File::AsyncInit)}, 0, [](Regs&) { Record({0x9DF0}); return 0u; });
    using GetIntegerFn = int (*)(uint32_t);
    s.At({0x00478F60, AddressOf(static_cast<GetIntegerFn>(&Script::GetInteger))}, 0, [](Regs& r) {
        Record({0x8F60, r.args[0]});
        return 0u;
    });
    // the debug output: the format, then its strings and numbers
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        const char* format = reinterpret_cast<const char*>(r.args[0]);
        Record({0x1C30, Hash(format)});
        int arg = 1;
        for (const char* p = format; *p; ++p) {
            if (p[0] == '%' && p[1] == 's')
                Record({Text(r.args[arg++])});
            else if (p[0] == '%' && p[1] == 'd')
                Record({r.args[arg++]});
        }
        return 0u;
    });
    s.At({0x005B5FC0}, 0, [](Regs&) { Record({0x5FC0}); return Addr(g_hardware); });
    s.At({0x005B6020}, 0, [](Regs& r) { Record({0x6020, r.args[0]}); return Addr(g_display); });
    s.At({0x005B5F20}, 0, [](Regs&) { Record({0x5F20}); return Addr(g_language); });
    s.At({0x005B5F90}, 0, [](Regs&) { Record({0x5F90}); return Addr(g_territory); });
    s.At({0x004429D0, AddressOf(&Sun::Init)}, 0, [](Regs& r) {
        Record({0x29D0});
        for (int i = 0; i < 7; ++i)
            Record({r.args[i]});
        return 0u;
    });
    s.At({0x00442D70, AddressOf(&Sun::UpdateFlare)}, 0, [](Regs& r) { Record({0x2D70, r.args[0], r.args[1]}); return 0u; });
}

void RecordConfig()
{
    Record({GameVariable<uint32_t>(0x007CEFA0), GameVariable<uint8_t>(0x00684FA8), GameVariable<uint8_t>(0x007CEFAC),
            GameVariable<uint32_t>(0x00684FB0), GameVariable<uint32_t>(0x00684FB4)});
}

void TestSceneCommands()
{
    std::vector<Saved> saved = {{0x006F3624, 4, {}}, {0x006918DC, 4, {}}, {0x007D1800, 4, {}}, {0x00690FA0, 4, {}},
                                {0x007CEFA0, 4, {}}, {0x00684FA8, 1, {}}, {0x007CEFAC, 2, {}}, {0x00684FB0, 8, {}}};
    SaveAll(saved);
    UseEmptySymbolTable();
    auto* standIns = new StandIns;
    StandInCalls(*standIns);

    static const char kScene[] = "scene_a";
    static const char kAdd[] = "scene_b";
    static const char kFile[] = "anims\\file.ska";
    const uint32_t name = 0xA1DC81F9, scene = 0x26861025, add = 0x02E58C18;
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        auto call = [&](uint32_t address, CFunc ours, Params& params) {
            Call(original, address, ours, params);
            RecordConfig();
        };
        std::memset(g_assets, 0, sizeof(g_assets));
        g_assets[0x14] = 1;                                   // permanent by default
        *reinterpret_cast<uint32_t*>(g_assets + 0x18) = 0x7700; // the reference checksum
        std::memset(g_manager, 0, sizeof(g_manager));
        *reinterpret_cast<uint32_t*>(g_manager + 0x154) = 0xDEB6;
        std::memset(g_scenes, 0, sizeof(g_scenes));
        *reinterpret_cast<uint32_t*>(g_scenes[0] + 0x88) = 0x7E40;
        GameVariable<void*>(0x006918DC) = g_assets;
        GameVariable<void*>(0x007D1800) = g_manager;
        GameVariable<int>(0x00690FA0) = 3;
        GameVariable<uint32_t>(0x007CEFA0) = 9;
        GameVariable<uint8_t>(0x00684FA8) = 0;
        GameVariable<uint8_t>(0x007CEFAC) = 0;
        GameVariable<uint8_t>(0x007CEFAD) = 0;
        GameVariable<uint32_t>(0x00684FB0) = 7;
        GameVariable<uint32_t>(0x00684FB4) = 50;

        // LoadTerrainSounds: a terrain; none
        {
            Params a; a.Checksum(0x3789AC4E, 0x7E1).Integer(0x11, 2);
            call(0x00445000, CFuncs::CFunc_LoadTerrainSounds, a);
            Params none; none.Checksum(0x3789AC4F, 0x7E2);
            call(0x00445000, CFuncs::CFunc_LoadTerrainSounds, none);
        }
        // PreloadModel: with and without AllowReplaceTex
        {
            Params a; a.String(name, "veh\\car").Flag(0x6C37FDC7);
            call(0x00445030, CFuncs::CFunc_PreloadModel, a);
            Params b; b.Flag(0x6C37FDC8).String(name, "ped\\guy");
            call(0x00445030, CFuncs::CFunc_PreloadModel, b);
        }
        {
            Params none;
            call(0x00445120, CFuncs::CFunc_UnloadAllLevelGeometry, none);
        }
        // LoadCollision / AddCollision / QuickReload / AddScene / UnloadScene: with and without their names
        g_sceneFound = true;
        g_firstScene = true;
        for (int variant = 0; variant < 4; ++variant) {
            Params a;
            if (variant & 1)
                a.String(scene, kScene);
            if (variant & 2)
                a.String(add, kAdd);
            if (variant == 3)
                a.Flag(0x417149C8);
            call(0x00445320, CFuncs::CFunc_LoadCollision, a);
            call(0x00445380, CFuncs::CFunc_AddCollision, a);
            call(0x004453F0, CFuncs::CFunc_QuickReload, a);
            call(0x00445720, CFuncs::CFunc_AddScene, a);
            call(0x004457B0, CFuncs::CFunc_UnloadScene, a);
        }
        // UnloadScene: found without a dictionary; not found (the first scene, with and without one)
        {
            Params a; a.String(scene, kAdd);
            *reinterpret_cast<uint32_t*>(g_scenes[0] + 0x88) = 0;
            call(0x004457B0, CFuncs::CFunc_UnloadScene, a);
            g_sceneFound = false;
            *reinterpret_cast<uint32_t*>(g_scenes[1] + 0x88) = 0x7E41;
            call(0x004457B0, CFuncs::CFunc_UnloadScene, a);
            g_firstScene = false;
            call(0x004457B0, CFuncs::CFunc_UnloadScene, a);
            g_sceneFound = true;
        }
        // UnloadAnim: by descChecksum + refChecksum, by descChecksum + the manager's, by name; found or not
        for (bool found : {true, false}) {
            g_assetFound = found;
            Params a; a.String(name, kFile).Checksum(0x4D20D794, 0x120).Checksum(0x91CC92C8, 0x3400);
            call(0x00445430, CFuncs::CFunc_UnloadAnim, a);
            Params b; b.Checksum(0x4D20D794, 0x121).String(name, kFile);
            call(0x00445430, CFuncs::CFunc_UnloadAnim, b);
            Params c; c.String(name, kFile).Checksum(0x91CC92C8, 0x3401);
            call(0x00445430, CFuncs::CFunc_UnloadAnim, c);
        }
        // LoadAsset: everything; an unnamed string; loaded or not (the assert's integer)
        for (uint32_t result : {0x1234u, 0u}) {
            g_loadResult = result;
            Params a; a.String(name, kFile).Checksum(0xC2719FB0, 0x9A).Flag(0x90E07C79).Checksum(0xF44A53AB, 0xDE5);
            call(0x004454E0, CFuncs::CFunc_LoadAsset, a);
            Params b; b.String(0, kAdd);
            call(0x004454E0, CFuncs::CFunc_LoadAsset, b);
        }
        // LoadAnim: flags each way; the result passed on
        for (uint32_t result : {0x101u, 0x7F00u}) {
            g_loadResult = result;
            Params a; a.String(name, kFile).Checksum(0x4D20D794, 0x55).Flag(0x90E07C79);
            call(0x004455A0, CFuncs::CFunc_LoadAnim, a);
            Params b; b.Flag(0x23028E29).String(name, kScene).Checksum(0x4D20D794, 0x56);
            call(0x004455A0, CFuncs::CFunc_LoadAnim, b);
            g_assets[0x14] = 0;
            *reinterpret_cast<uint32_t*>(g_assets + 0x18) = 0x7800;
        }
        // LoadSkeleton, SetReferenceChecksum (a checksum, an integer), SetDefaultPermanent
        {
            Params a; a.String(name, kFile).Checksum(0, 0x5E1E);
            call(0x00445640, CFuncs::CFunc_LoadSkeleton, a);
            g_assets[0x14] = 1;
            call(0x00445640, CFuncs::CFunc_LoadSkeleton, a);
            Params b; b.Checksum(0, 0xCAFE).Integer(0, 4);
            call(0x004456A0, CFuncs::CFunc_SetReferenceChecksum, b);
            Params c; c.Integer(0, 0x1234);
            call(0x004456A0, CFuncs::CFunc_SetReferenceChecksum, c);
            Params d; d.Integer(0, 2);
            call(0x004456F0, CFuncs::CFunc_SetDefaultPermanent, d);
            Params e; e.Integer(0, 0);
            call(0x004456F0, CFuncs::CFunc_SetDefaultPermanent, e);
        }
        // SetConfig: every language (and an unknown one), the flags in each state, the frame rate
        {
            const uint32_t languages[] = {0xA8469630, 0xD37CFDFF, 0xCBE70ACB, 0x12345678, 0x5DCBBF5E, 0x508A31A1};
            for (uint32_t language : languages) {
                Params a; a.Checksum(0x2B248E4A, language);
                call(0x00445820, CFuncs::CFunc_SetConfig, a);
            }
            Params b; b.Flag(0xBA297025).Flag(0x38D23FD4).Flag(0x6CAD3928).Integer(0xEE2BC65F, 30);
            call(0x00445820, CFuncs::CFunc_SetConfig, b);
            call(0x00445820, CFuncs::CFunc_SetConfig, b); // (CD already set: no async init)
            call(0x00445980, CFuncs::CFunc_PrintConfig, b);
            Params c; c.Flag(0x2B6D7B32).Flag(0xAF11B50F).Flag(0x86137A88);
            call(0x00445820, CFuncs::CFunc_SetConfig, c);
            call(0x00445980, CFuncs::CFunc_PrintConfig, c);
            Params d; d.Flag(0xBA297025).Flag(0x2B6D7B32).Flag(0x38D23FD4).Flag(0xAF11B50F).Flag(0x6CAD3928)
                .Flag(0x86137A88);
            call(0x00445820, CFuncs::CFunc_SetConfig, d);
            Params none;
            call(0x00445820, CFuncs::CFunc_SetConfig, none);
            call(0x00445A30, CFuncs::CFunc_Bootstrap, none);
            GameVariable<uint8_t>(0x007CEFAD) = 1;
            call(0x00445A30, CFuncs::CFunc_Bootstrap, none);
        }
        // InitSun (everything; nothing), UpdateLensFlare
        {
            Params a;
            a.Float(0xF4E775F6, 1.5f).Float(0xCD0576EE, -0.25f).Float(0xC48391A5, 900.0f).Float(0x083FDB95, 3.0f)
                .Checksum(0x7D99F28D, 0x7E7).Checksum(0xEB1CCC76, 0xC0C0).Checksum(0x04CBD07F, 0x1F1F);
            call(0x00445A40, CFuncs::CFunc_InitSun, a);
            Params none;
            call(0x00445A40, CFuncs::CFunc_InitSun, none);
            call(0x00445B30, CFuncs::CFunc_UpdateLensFlare, a);
        }
        Record({GameVariable<uint32_t>(0x00690FA0)});
        traces[run] = g_trace;
    }
    RemoveStandIns();
    RestoreAll(saved);
    Check(traces[0].size() > 300, "scene commands: the original reached its calls");
    Compare(traces[0], traces[1], "scene commands (0x00445000-0x00445B30)");
}

} // namespace

SELFTEST(Early, TestSceneCommands, RemoveStandIns);
