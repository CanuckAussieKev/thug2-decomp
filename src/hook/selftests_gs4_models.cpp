// Self-tests for the script commands 0x005B34A0-0x005B4260 (model swaps, create-a-graphic, 2D positions, head bone
// groups): each original against ours on identical stand-in state; the objects and their components, the skeletons,
// the models, the memory manager, the textures and the appearance's lookups are stood in for by recording code (the
// game's functions and ours alike).

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/skate/skate_module.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/cag_cfuncs.h"
#include "game/mth/math.h"
#include "game/obj/model_swap_cfuncs.h"
#include "game/obj/objman.h"
#include "game/obj/object.h"
#include "game/obj/shadow_component.h"
#include "game/obj/suspend_component.h"
#include "game/skate/profile_cfuncs.h"
#include "game/skater/skater.h"
#include "game/skater/tagging.h"
#include "game/gfx/model_appearance.h"
#include "game/gfx/texture_dict.h"
#include "game/script/struct.h"
#include "game/skate/profile_manager.h"
#include "game/gfx/model.h"
#include "game/gfx/skeleton.h"
#include "game/script/game_cfuncs.h"
#include "hook/gs4_testkit.h"

namespace {

using namespace Gs4Tests;

// --- model swaps -----------------------------------------------------------------------------------------------

// Objects 0-3 (ids 0x0B0E0000 + i): components by type in each object's table (null = none).
alignas(16) uint8_t g_mObjects[4][0x100];
alignas(16) uint8_t g_mComponents[4][8][0x220];
alignas(16) uint8_t g_mModels[4][0x40];  // the Nx models (+0x2C their lights)
alignas(16) uint8_t g_mLights[4][0x20];
alignas(16) uint8_t g_mSkeletons[4][0x20];
alignas(16) uint8_t g_mManager[0x600];
alignas(16) uint8_t g_mNew[0x40];
alignas(16) uint8_t g_mObjectManager[0x40];
alignas(16) uint8_t g_mSkate[0x100];
void* g_mVtable[16];
bool g_mHas[4][8];
uint32_t g_mModelValue[4];
bool g_mNewFails;
const uint32_t kTypes[8] = {0x5BD63B29, 0x286A8D26, 0x8A897DD2, 0x222756D5, 0xCE0CA665, 0x5B512EDC, 0, 0};

uint32_t Id(uint32_t address)
{
    auto base = reinterpret_cast<uint32_t>(g_mObjects);
    if (address >= base && address < base + sizeof(g_mObjects))
        return 0x0B000000u + (address - base);
    base = reinterpret_cast<uint32_t>(g_mComponents);
    if (address >= base && address < base + sizeof(g_mComponents))
        return 0x0C000000u + (address - base);
    base = reinterpret_cast<uint32_t>(g_mModels);
    if (address >= base && address < base + sizeof(g_mModels))
        return 0x0D000000u + (address - base);
    base = reinterpret_cast<uint32_t>(g_mSkeletons);
    if (address >= base && address < base + sizeof(g_mSkeletons))
        return 0x0E000000u + (address - base);
    base = reinterpret_cast<uint32_t>(g_mNew);
    if (address >= base && address < base + sizeof(g_mNew))
        return 0x0F000000u + (address - base);
    base = reinterpret_cast<uint32_t>(g_mManager);
    if (address >= base && address < base + sizeof(g_mManager))
        return 0x0A000000u + (address - base);
    return address;
}
int ObjectIndex(uint32_t address)
{
    for (int i = 0; i < 4; ++i)
        if (address == reinterpret_cast<uint32_t>(g_mObjects[i]))
            return i;
    return -1;
}

void SetUpObjects()
{
    std::memset(g_mObjects, 0, sizeof(g_mObjects));
    std::memset(g_mComponents, 0, sizeof(g_mComponents));
    std::memset(g_mModels, 0, sizeof(g_mModels));
    std::memset(g_mLights, 0, sizeof(g_mLights));
    std::memset(g_mSkeletons, 0, sizeof(g_mSkeletons));
    std::memset(g_mManager, 0, sizeof(g_mManager));
    std::memset(g_mNew, 0, sizeof(g_mNew));
    for (int i = 0; i < 4; ++i) {
        reinterpret_cast<uint32_t*>(g_mObjects[i])[0x14 / 4] = 0x0B0E0000u + i; // m_id
        for (int t = 0; t < 8; ++t) {
            g_mHas[i][t] = t < 5;
            reinterpret_cast<void**>(g_mComponents[i][t])[0] = g_mVtable;
        }
        g_mHas[i][0] = i == 2; // core physics: object 2 is the skater
        reinterpret_cast<void**>(g_mComponents[i][1])[0x208 / 4] = g_mModels[i];
        reinterpret_cast<void**>(g_mModels[i])[0x2C / 4] = g_mLights[i];
        reinterpret_cast<void**>(g_mComponents[i][3])[0x18 / 4] = g_mSkeletons[i];
        reinterpret_cast<uint32_t*>(g_mSkeletons[i])[2] = 0x5E1E0000u + i; // its asset
        g_mModelValue[i] = 0x3D000000u + i;
    }
    reinterpret_cast<uint32_t*>(g_mManager)[0x110 / 4] = 0x4EA70110; // the top-down heap
    for (int i = 0; i < 8; ++i)
        reinterpret_cast<uint32_t*>(g_mManager)[0x180 / 4 + i] = 0x4EA80000u + i; // the skater heaps
    g_mNewFails = false;
}

void StandInModelCalls(StandIns& s)
{
    s.At({0x004619F0, AddressOf(&Obj::CBaseManager::GetObjectByID)}, 4, [](Regs& r) {
        Record({0x19F0, r.ecx == reinterpret_cast<uint32_t>(g_mObjectManager), r.args[0]});
        uint32_t i = r.args[0] - 0x0B0E0000u;
        return i < 4 ? reinterpret_cast<uint32_t>(g_mObjects[i]) : 0u;
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        int i = ObjectIndex(r.ecx);
        Record({0xDB90, Id(r.ecx), r.args[0]});
        if (i < 0)
            return 0u;
        for (int t = 0; t < 6; ++t)
            if (kTypes[t] == r.args[0])
                return g_mHas[i][t] ? reinterpret_cast<uint32_t>(g_mComponents[i][t]) : 0u;
        return 0u;
    });
    s.At({0x005AA930, AddressOf(&CFuncs::SwapMapping)}, 0, [](Regs& r) {
        Record({0xA930, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00431780, AddressOf(&Obj::CShadowComponent::DestroyShadow)}, 0, [](Regs& r) {
        Record({0x1780, Id(r.ecx)});
        return 0u;
    });
    s.At({0x004315A0, AddressOf(&Obj::CShadowComponent::CreateShadow)}, 8, [](Regs& r) {
        Record({0x15A0, Id(r.ecx), r.args[0], Hash(reinterpret_cast<const char*>(r.args[1]))});
        return 0u;
    });
    s.At({0x004A3D30, AddressOf(&Gfx::CSkeleton::Construct)}, 4, [](Regs& r) {
        Record({0x3D30, Id(r.ecx), r.args[0]});
        return r.ecx;
    });
    s.At({0x004A2E20, AddressOf(&Gfx::CSkeleton::CopyBoneScales)}, 8, [](Regs& r) {
        Record({0x2E20, Id(r.ecx), Id(r.args[0]), r.args[1]});
        return 0u;
    });
    s.At({0x004A2CB0, AddressOf(&Gfx::CSkeleton::DestroyBody)}, 0, [](Regs& r) {
        Record({0x2CB0, Id(r.ecx)});
        return 0u;
    });
    s.At({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, [](Regs& r) {
        Record({0xB0E0, Id(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [](Regs& r) {
        Record({0xB110, Id(r.ecx)});
        return 0u;
    });
    s.At({0x004015D0, AddressOf(&Mem::New)}, 0, [](Regs& r) {
        Record({0x15D0, r.args[0]});
        return g_mNewFails ? 0u : reinterpret_cast<uint32_t>(g_mNew);
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xABA0, Id(r.ecx), Id(r.args[0])});
        return 0u;
    });
    s.At({0x00492E50}, 0, [](Regs& r) {
        Record({0x2E50, Id(r.ecx)});
        for (int i = 0; i < 4; ++i)
            if (r.ecx == reinterpret_cast<uint32_t>(g_mModels[i]))
                return g_mModelValue[i];
        return 0u;
    });
    s.At({0x00493460, AddressOf(&Nx::CModel::SetBoneMatrices)}, 4, [](Regs& r) {
        Record({0x3460, Id(r.ecx), r.args[0]});
        return 0u;
    });
    g_mVtable[14] = s.Thunk(4, [](Regs& r) {
        Record({0xF14, Id(r.ecx), Id(r.args[0])});
        // (the model component takes the model: what the original reads back afterwards)
        reinterpret_cast<uint32_t*>(r.ecx)[0x208 / 4] = r.args[0];
        return 0u;
    });
    s.At({0x00433E60, AddressOf(&Obj::CSuspendComponent::ResetCounts)}, 0, [](Regs& r) {
        Record({0x3E60, Id(r.ecx)});
        return 0u;
    });
    s.At({0x0045D9E0, AddressOf(&Obj::CCompositeObject::Suspend)}, 4, [](Regs& r) {
        Record({0xD9E0, Id(r.ecx), r.args[0] & 0xFF});
        return 0u;
    });
    s.At({0x0057FEC0, AddressOf(&Obj::CSkater::GetSkaterNumber)}, 0, [](Regs& r) {
        Record({0xFEC0, Id(r.ecx)});
        return 5u;
    });
    s.At({0x005665C0, AddressOf(&Mdl::Skate::GetSkater)}, 4, [](Regs& r) {
        Record({0x65C0, r.args[0]});
        return reinterpret_cast<uint32_t>(g_mObjects[r.args[0] & 3]);
    });
    s.At({0x00566520, AddressOf(&Mdl::Skate::GetLocalSkater)}, 0, [](Regs&) {
        Record({0x6520});
        return reinterpret_cast<uint32_t>(g_mObjects[1]);
    });
}

void TestModelSwapCFuncs()
{
    SaveGlobals({{0x006F0FB8, 4, {}}, {0x007D1800, 4, {}}, {0x007CEDE0, 60, {}}, {0x007CE478, 4, {}}});
    auto* standIns = new StandIns;
    StandInStructCalls(*standIns);
    StandInModelCalls(*standIns);

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        GameVariable<void*>(0x006F0FB8) = g_mObjectManager;
        GameVariable<void*>(0x007D1800) = g_mManager;
        GameVariable<void*>(0x007CE478) = g_mSkate;
        g_own.Reset();
        auto call = [&](uint32_t address, CFunc ours, TestParams& params) { Call(original, address, ours, params); };
        using namespace CFuncs;
        auto* swaps = &GameVariable<uint8_t>(0x007CEDE0);
        auto setSwaps = [&](std::initializer_list<uint32_t> entries) {
            std::memset(swaps, 0, 60);
            int i = 0;
            for (auto it = entries.begin(); it != entries.end(); it += 3, ++i) {
                swaps[i * 12] = static_cast<uint8_t>(it[0]);
                std::memcpy(swaps + i * 12 + 4, &it[1], 4);
                std::memcpy(swaps + i * 12 + 8, &it[2], 4);
            }
        };
        auto swap = [&](uint32_t a, uint32_t b) {
            TestParams p; p.Checksum(0x9A2DD8A1, a).Checksum(0x0324891B, b);
            call(0x005B34A0, CFunc_SwapObjectModels, p);
            Record({HashBytes(g_mObjects[0], sizeof(g_mObjects)), HashBytes(g_mComponents[0][0], sizeof(g_mComponents)),
                    HashBytes(g_mLights[0], sizeof(g_mLights))});
        };
        // a skater (2) and an object (0); the table: a matching entry (current 0), others
        SetUpObjects();
        setSwaps({1, 0x0B0E0002, 0, 0, 0x0B0E0001, 0, 1, 0x0B0E0003, 0x0B0E0002, 1, 0x0B0E0000, 0});
        swap(0x0B0E0002, 0x0B0E0000);
        // two plain objects (no skater): no table look-up, both shadows type 2
        SetUpObjects();
        swap(0x0B0E0001, 0x0B0E0003);
        // the skater second; no entry matching (its value cleared); the new skeleton failing
        SetUpObjects();
        reinterpret_cast<uint32_t*>(g_mObjects[2])[0x31C / 4] = 0x12345678;
        setSwaps({1, 0x0B0E0001, 5, 0, 0x0B0E0000, 0});
        g_mNewFails = true;
        swap(0x0B0E0001, 0x0B0E0002);
        // both skaters (the second counts)
        SetUpObjects();
        g_mHas[0][0] = true;
        swap(0x0B0E0000, 0x0B0E0002);
        // the same object, a missing one, a missing model
        SetUpObjects();
        swap(0x0B0E0001, 0x0B0E0001);
        SetUpObjects();
        swap(0x0B0E0009, 0x0B0E0001);
        SetUpObjects();
        g_mHas[3][1] = false;
        swap(0x0B0E0001, 0x0B0E0003);
        SetUpObjects();
        g_mHas[1][1] = false;
        swap(0x0B0E0001, 0x0B0E0003);
        // GetSkaterModelIsSwapped
        SetUpObjects();
        reinterpret_cast<uint32_t*>(g_mObjects[3])[0x31C / 4] = 7;
        for (int skater : {0, 3, -1}) {
            TestParams p;
            if (skater >= 0)
                p.Integer(0x5B8AB877, skater);
            call(0x005B3A30, CFunc_GetSkaterModelIsSwapped, p);
        }
        reinterpret_cast<uint32_t*>(g_mObjects[1])[0x31C / 4] = 1;
        TestParams none;
        call(0x005B3A30, CFunc_GetSkaterModelIsSwapped, none);
        traces[run] = g_trace;
    }
    RestoreGlobals();
    DumpTraces(traces[0], traces[1]);
    Compare(traces[0], traces[1], "model swaps (0x005B34A0, 0x005B3A30)");
}

// RestoreAllSwappedObjectModels, with SwapObjectModels itself stood in for (each call takes one entry out of the
// table every other time).
int g_rSwapCalls;
void TestRestoreSwapsCFunc()
{
    SaveGlobals({{0x007CEDE0, 60, {}}});
    auto* standIns = new StandIns;
    StandInStructCalls(*standIns);
    standIns->At({0x005BA3B0, AddressOf(&Mem::Pool::Contains)}, 4, [](Regs& r) {
        uint32_t id = ArrayId(r.args[0]);
        Record({0xA3B0, r.ecx == reinterpret_cast<uint32_t>(g_arrayPool), id});
        return (id >> 16) == 0xA770 ? 1u : 0u;
    });
    standIns->At({0x005BA400, AddressOf(&Mem::Pool::Free)}, 4, [](Regs& r) {
        Record({0xA400, r.ecx == reinterpret_cast<uint32_t>(g_arrayPool), ArrayId(r.args[0])});
        return 0u;
    });
    standIns->At({0x005B34A0, AddressOf(&CFuncs::CFunc_SwapObjectModels)}, 0, [](Regs& r) {
        Record({0x34A0, ArrayId(r.args[0]), r.args[1]});
        auto* swaps = &GameVariable<uint8_t>(0x007CEDE0);
        if (++g_rSwapCalls % 2 == 0) {
            for (int i = 0; i < 5; ++i) {
                if (swaps[i * 12]) {
                    swaps[i * 12] = 0;
                    break;
                }
            }
        }
        return 1u;
    });
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        g_own.Reset();
        using namespace CFuncs;
        auto* swaps = &GameVariable<uint8_t>(0x007CEDE0);
        TestParams none;
        for (int pattern : {0, 0x1F, 0x0A, 0x10}) {
            std::memset(swaps, 0, 60);
            for (int i = 0; i < 5; ++i) {
                swaps[i * 12] = static_cast<uint8_t>(pattern >> i & 1);
                uint32_t id = 0x0B0E0000u + i, current = 0x0B0E0010u + i;
                std::memcpy(swaps + i * 12 + 4, &id, 4);
                std::memcpy(swaps + i * 12 + 8, &current, 4);
            }
            g_rSwapCalls = 0;
            UseStandInArrayPool();
            UseStandInStructPool();
            Call(original, 0x005B3890, CFunc_RestoreAllSwappedObjectModels, none);
            Record({HashBytes(swaps, 60), static_cast<uint32_t>(g_rSwapCalls)});
        }
        traces[run] = g_trace;
    }
    RestoreGlobals();
    DumpTraces(traces[0], traces[1]);
    Compare(traces[0], traces[1], "restoring swapped models (0x005B3890)");
}

// --- create-a-graphic, 2D positions, head bone groups ----------------------------------------------------------

alignas(16) uint8_t g_cTextures[8][0x40]; // 0 the tag, 1 the workspace, 2 the sprite, 3-6 layer sources
alignas(16) uint8_t g_cTagging[0x40];
alignas(16) uint8_t g_cSkater[0x100];
alignas(16) uint8_t g_cProfile[0x64];
uint32_t g_cTagSize;
uint8_t g_cBitsPerPixel;
bool g_cHaveProfile;
int g_cHeadKind; // 0 none, 1 male, 2 female
bool g_cHasGroup[4];
TestParams g_cHead;
float g_cFastResult = 0.0f;

uint32_t TexId(uint32_t address)
{
    for (uint32_t i = 0; i < 8; ++i)
        if (address == reinterpret_cast<uint32_t>(g_cTextures[i]))
            return 0x7E700 + i;
    return address == 0 ? 0 : 0xBAD;
}

void __cdecl RecordFast(uint32_t which, uint32_t angle)
{
    Record({which, angle});
    g_cFastResult = which == 0x5FF0 ? 0.375f + (angle & 0xFF) / 1024.0f : -0.625f + (angle & 0xFF) / 512.0f;
}
__declspec(naked) void FastSinStandIn()
{
    __asm {
        push dword ptr [esp + 4]
        push 0x5FF0
        call RecordFast
        add esp, 8
        fld dword ptr [g_cFastResult]
        ret
    }
}
__declspec(naked) void FastCosStandIn()
{
    __asm {
        push dword ptr [esp + 4]
        push 0x6020
        call RecordFast
        add esp, 8
        fld dword ptr [g_cFastResult]
        ret
    }
}

void StandInCagCalls(StandIns& s)
{
    s.At({0x0049F540, AddressOf(&Nx::CTexDict::GetTexture)}, 4, [](Regs& r) {
        Record({0xF540, r.ecx, r.args[0]});
        if (r.args[0] == 0xAA286543)
            return reinterpret_cast<uint32_t>(g_cTextures[0]);
        if (r.args[0] >= 0x7E7E0003 && r.args[0] <= 0x7E7E0006)
            return reinterpret_cast<uint32_t>(g_cTextures[r.args[0] - 0x7E7E0000]);
        return 0u;
    });
    s.At({0x0049FAA0}, 0x1C, [](Regs& r) {
        Record({0xFAA0, r.ecx, r.args[0], r.args[1], r.args[2], r.args[3], r.args[4], r.args[5] & 0xFF, r.args[6] & 0xFF});
        return reinterpret_cast<uint32_t>(g_cTextures[r.args[0] == 0xFDA19704 ? 1 : 2]);
    });
    struct Thunk {
        uint32_t address;
        uint16_t pop;
    };
    for (Thunk t : {Thunk{0x0048D060, 8}, Thunk{0x0048DE50, 8}, Thunk{0x0048E4C0, 0}, Thunk{0x0048DE80, 8},
                    Thunk{0x0048DED0, 4}, Thunk{0x0048DEB0, 8}, Thunk{0x0048DE60, 0}, Thunk{0x0048DE70, 0},
                    Thunk{0x0049F360, 12}, Thunk{0x0048DEC0, 16}, Thunk{0x0049F420, 8}, Thunk{0x00487D50, 4}}) {
        uint32_t address = t.address;
        uint16_t pop = t.pop;
        s.At({address}, pop, [address, pop](Regs& r) {
            Record({address, TexId(r.ecx)});
            for (int i = 0; i < pop / 4; ++i)
                Record({r.args[i] > 0x10000 && TexId(r.args[i]) != 0xBAD ? TexId(r.args[i]) : r.args[i]});
            return 0u;
        });
    }
    s.At({0x0048DE30}, 8, [](Regs& r) {
        Record({0xDE30, TexId(r.ecx), r.args[0], r.args[1] != 0});
        return 0u;
    });
    s.At({0x0049F3C0}, 0, [](Regs& r) {
        Record({0xF3C0, TexId(r.ecx)});
        return g_cTagSize;
    });
    s.At({0x0049C6F0}, 0, [](Regs& r) {
        Record({0xC6F0, TexId(r.ecx)});
        return 0x5555500u | g_cBitsPerPixel;
    });
    s.At({0x0049F500, AddressOf(&Nx::CTexDict::UnloadTexture)}, 4, [](Regs& r) {
        Record({0xF500, r.ecx, TexId(r.args[0])});
        return 1u;
    });
    s.At({0x005665C0, AddressOf(&Mdl::Skate::GetSkater)}, 4, [](Regs& r) {
        Record({0x65C0, r.args[0]});
        return reinterpret_cast<uint32_t>(g_cSkater);
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Record({0xDB90, r.ecx == reinterpret_cast<uint32_t>(g_cSkater), r.args[0]});
        return reinterpret_cast<uint32_t>(g_cTagging);
    });
    s.At({0x0051D830, AddressOf(&Obj::CTaggingComponent::ApplyCustomTagTexture)}, 0, [](Regs& r) {
        Record({0xD830, r.ecx == reinterpret_cast<uint32_t>(g_cTagging), g_cTagging[0x18]});
        return 0u;
    });
    s.At({0x00564F80, AddressOf(static_cast<Obj::CSkaterProfile* (Mdl::Skate::*)(Script::CStruct*)>(&Mdl::Skate::GetProfile))}, 4, [](Regs& r) {
        Record({0x4F80, r.args[0]});
        return 0x9F0F1000u;
    });
    s.At({0x00564FC0, AddressOf(static_cast<Obj::CSkaterProfile* (Mdl::Skate::*)(int)>(&Mdl::Skate::GetProfile))}, 4, [](Regs& r) {
        Record({0x4FC0, r.args[0]});
        return g_cHaveProfile ? reinterpret_cast<uint32_t>(g_cProfile) : 0u;
    });
    s.At({0x00489050, AddressOf(&Gfx::CModelAppearance::GetPartOption)}, 4, [](Regs& r) {
        Record({0x9050, r.ecx == reinterpret_cast<uint32_t>(g_cProfile), r.args[0]});
        bool give = (g_cHeadKind == 1 && r.args[0] == 0x650FAB6D) || (g_cHeadKind == 2 && r.args[0] == 0x0FC85BAE);
        return give ? reinterpret_cast<uint32_t>(g_cHead.Struct()) : 0u;
    });
    s.At({0x004890B0, AddressOf(&Gfx::CModelAppearance::GetPart)}, 4, [](Regs& r) {
        Record({0x90B0, r.ecx == reinterpret_cast<uint32_t>(g_cProfile), r.args[0]});
        const uint32_t groups[4] = {0xA28FAB7F, 0x1DBCFAE8, 0x0442D3C1, 0xB90F08D0};
        for (int i = 0; i < 4; ++i)
            if (r.args[0] == groups[i])
                return g_cHasGroup[i] ? 0x6A0u + i : 0u;
        return 0u;
    });
    s.Bytes(0x00405FF0, Branch(0xE9, 0x00405FF0, reinterpret_cast<void*>(&FastSinStandIn)));
    s.Bytes(0x00406020, Branch(0xE9, 0x00406020, reinterpret_cast<void*>(&FastCosStandIn)));
    uint32_t sin = AddressOf(&Mth::FastSin);
    uint32_t cos = AddressOf(&Mth::FastCos);
    s.Bytes(sin, Branch(0xE9, sin, reinterpret_cast<void*>(&FastSinStandIn)));
    s.Bytes(cos, Branch(0xE9, cos, reinterpret_cast<void*>(&FastCosStandIn)));
}

void TestCagCFuncs()
{
    SaveGlobals({{0x007CE478, 4, {}}});
    auto* standIns = new StandIns;
    StandInStructCalls(*standIns);
    StandInCagCalls(*standIns);

    // layers: texture, scale, flips, rotation, position, colour (float or integer arrays), layer
    static TestParams layers[4];
    static TestArray layerList, colours[4];
    colours[0].Set(Script::ESYMBOLTYPE_FLOAT, {Bits(255.0f), Bits(128.5f), Bits(3.9f), Bits(200.0f)});
    colours[1].Set(Script::ESYMBOLTYPE_INTEGER, {10, 20, 30, 0x7F});
    colours[2].Set(Script::ESYMBOLTYPE_FLOAT, {Bits(-1.5f), Bits(300.0f), Bits(0.0f), Bits(128.0f)});
    colours[3].Set(Script::ESYMBOLTYPE_INTEGER, {1, 2, 3, 0x180});
    for (int i = 0; i < 4; ++i) {
        layers[i].Reset();
        layers[i].Checksum(0xDFB43C53, 0x7E7E0003 + i).Float(0x13B9DA7B, 0.5f + i).Integer(0xA0C7507F, i & 1)
            .Integer(0x5AC86D1C, i >> 1 & 1).Float(0xE2C6589E, 30.0f * i - 45.0f).Float(0x960061E4, 10.75f - 7 * i)
            .Float(0xE1075172, -3.5f + 2.25f * i).Array(0x3F6BCDBA, colours[i].Get()).Integer(0x15910232, i / 2);
    }

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        GameVariable<void*>(0x007CE478) = g_cSkater;
        g_own.Reset();
        auto call = [&](uint32_t address, CFunc ours, TestParams& params) { Call(original, address, ours, params); };
        using namespace CFuncs;
        struct Case {
            int layers;
            uint32_t size;
            uint8_t bits;
            int player;
            int test;
        };
        for (Case c : {Case{4, 0x80, 8, 0, 1}, Case{2, 0x10040, 8, 1, 0}, Case{1, 0x40, 4, 0, 0}, Case{0, 0x80, 8, 2, 7},
                       Case{3, 0x20080, 8, 3, 0}}) {
            switch (c.layers) {
            case 0: layerList.Set(Script::ESYMBOLTYPE_STRUCTURE, {}); break;
            case 1: layerList.Set(Script::ESYMBOLTYPE_STRUCTURE, {reinterpret_cast<uint32_t>(layers[1].Struct())}); break;
            case 2:
                layerList.Set(Script::ESYMBOLTYPE_STRUCTURE, {reinterpret_cast<uint32_t>(layers[0].Struct()),
                                                              reinterpret_cast<uint32_t>(layers[3].Struct())});
                break;
            case 3:
                layerList.Set(Script::ESYMBOLTYPE_STRUCTURE, {reinterpret_cast<uint32_t>(layers[0].Struct()),
                                                              reinterpret_cast<uint32_t>(layers[2].Struct()),
                                                              reinterpret_cast<uint32_t>(layers[3].Struct())});
                break;
            default:
                layerList.Set(Script::ESYMBOLTYPE_STRUCTURE, {reinterpret_cast<uint32_t>(layers[0].Struct()),
                                                              reinterpret_cast<uint32_t>(layers[1].Struct()),
                                                              reinterpret_cast<uint32_t>(layers[2].Struct()),
                                                              reinterpret_cast<uint32_t>(layers[3].Struct())});
                break;
            }
            g_cTagSize = c.size;
            g_cBitsPerPixel = c.bits;
            std::memset(g_cTagging, 0, sizeof(g_cTagging));
            TestParams p; p.Array(0x54AAD4BD, layerList.Get()).Integer(0x67E6859A, c.player);
            if (c.test)
                p.Integer(0x278081F3, c.test);
            call(0x005B3A90, CFunc_GenerateCAGTexture, p);
            Record({g_cTagging[0x18]});
        }
        // SyncCAGWorkspace
        for (uint32_t graffiti : {0u, reinterpret_cast<uint32_t>(g_cTextures[5])}) {
            reinterpret_cast<uint32_t*>(g_cTagging)[0x1C / 4] = graffiti;
            TestParams p; p.Integer(0x5B8AB877, 1);
            call(0x005B3ED0, CFunc_SyncCAGWorkspace, p);
        }
        // Get2DPosFrom2DVec
        for (float theta : {0.0f, 45.0f, 90.0f, 135.0f, 180.0f, 200.0f, 270.0f, 300.0f, 360.0f, 400.0f, -30.0f, 90.5f,
                            179.99f, 180.01f, 269.5f, 270.5f, 359.9f}) {
            TestParams p; p.Float(0xCA7882FE, 12.5f).Float(0xF4E775F6, theta);
            call(0x005B3F40, CFunc_Get2DPosFrom2DVec, p);
        }
        // ResolveHeadBoneScale
        g_cHead.Reset();
        static TestParams group0, group2, group3;
        g_cHead.Structure(0xA28FAB7F, group0.Struct()).Structure(0x0442D3C1, group2.Struct())
            .Structure(0xB90F08D0, group3.Struct());
        struct HeadCase {
            bool profile;
            int head;
            bool has[4];
        };
        for (HeadCase h : {HeadCase{true, 1, {false, false, false, false}}, HeadCase{true, 2, {true, false, false, true}},
                           HeadCase{true, 0, {false, false, false, false}}, HeadCase{false, 1, {false, false, false, false}},
                           HeadCase{true, 1, {false, true, true, false}}}) {
            g_cHaveProfile = h.profile;
            g_cHeadKind = h.head;
            std::memcpy(g_cHasGroup, h.has, sizeof(g_cHasGroup));
            TestParams p; p.Integer(0x67E6859A, 1);
            call(0x005B4150, CFunc_ResolveHeadBoneScale, p);
        }
        traces[run] = g_trace;
    }
    RestoreGlobals();
    DumpTraces(traces[0], traces[1]);
    Compare(traces[0], traces[1], "create-a-graphic, 2D positions, head bone groups (0x005B3A90-0x005B4260)");
}

} // namespace

SELFTEST(Early, TestModelSwapCFuncs, Gs4Tests::RestoreGlobals);
SELFTEST(Early, TestRestoreSwapsCFunc, Gs4Tests::RestoreGlobals);
SELFTEST(Early, TestCagCFuncs, Gs4Tests::RestoreGlobals);
