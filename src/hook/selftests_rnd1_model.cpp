// Self-tests for the engine's model (src/game/gfx/model.cpp, RND1-1): the original and ours over a stand-in model -
// a recording vtable of its own, a geom list of stand-in geoms (the engine's geom calls stood in, recording; their
// colour slot a recording vtable), stand-in scenes, skeletons and asset manager - each member called with random
// arguments in a random order, every result, call and the arena compared.

#include <windows.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/ass/assman.h"
#include "game/gfx/model.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;
using Nx::CModel;
using Nx::sModelGeom;
using Nx::sModelGeomNode;

constexpr uint32_t kNames[] = {0x11111111, 0x22222222, 0, 0x33333333, 0x22222222, 0x44444444};

// The model's and the geoms' vtables (recording thunks), and the values the stand-ins give back.
void* g_modelVtable[14];
void* g_geomVtable[64];
uint32_t g_allowMode;     // what the model's slot 3 says
uint32_t g_sceneDefault;  // the asset manager's scene for any name
uint32_t g_calls;         // stand-in calls so far (their results vary with it)

uint32_t Varied(Regs& r, uint32_t salt) { return (Relocate(r.ecx) >> 4) + salt + ++g_calls; }

void StandInModel(StandIns& s)
{
    StandInMemory(s);
    // The model's own slots: record the slot and the arguments (slot 12 writes a sphere, slot 3 answers g_allowMode).
    const uint16_t pops[14] = {4, 4, 4, 4, 16, 4, 4, 4, 8, 12, 0, 0, 4, 4};
    for (int slot = 0; slot < 14; ++slot) {
        uint16_t pop = pops[slot];
        g_modelVtable[slot] = s.Thunk(pop, [slot, pop](Regs& r) {
            Record({0xA000u + slot, Relocate(r.ecx)});
            for (int i = 0; i < pop / 4; ++i)
                Record({Relocate(r.args[i])});
            if (slot == 3)
                return g_allowMode;
            if (slot == 12) {
                auto* out = reinterpret_cast<float*>(r.args[0]);
                out[0] = 1.5f + static_cast<float>(g_calls++);
                out[1] = -2.25f;
                out[2] = 3.0f;
                out[3] = 44.0f;
                return r.args[0];
            }
            return 0xFFFFFF01u; // (true in AL, the rest noise)
        });
    }
    for (int slot = 0; slot < 64; ++slot) {
        g_geomVtable[slot] = s.Thunk(4, [slot](Regs& r) {
            Record({0xB000u + slot, Relocate(r.ecx), r.args[0]});
            return 0u;
        });
    }
    // The engine's geom calls: tag, the geom, the arguments (bytes where the geom reads a byte); a varied result.
    struct Call {
        uint32_t address;
        uint16_t pop;
        uint32_t byteArgs; // a bit per argument read as a byte
    };
    for (Call c : {Call{0x0048D060, 0, 0}, Call{0x0048DE30, 12, 0}, Call{0x0048DE40, 4, 0}, Call{0x0048DE50, 0, 0},
                   Call{0x0048DE60, 4, 1}, Call{0x0048DF20, 4, 0}, Call{0x0048DF30, 20, 0}, Call{0x0048DF40, 8, 0},
                   Call{0x0048DF50, 8, 0}, Call{0x0048DF60, 4, 0}, Call{0x0049F3B0, 4, 1}, Call{0x0048DFE0, 16, 0},
                   Call{0x0048DFF0, 12, 0}, Call{0x0048E000, 8, 0}, Call{0x00487D50, 0, 0}}) {
        s.At({c.address}, c.pop, [c](Regs& r) {
            Record({c.address & 0xFFFFF, Relocate(r.ecx)});
            for (int i = 0; i < c.pop / 4; ++i)
                Record({(c.byteArgs >> i) & 1 ? r.args[i] & 0xFF : Relocate(r.args[i])});
            return Varied(r, c.address);
        });
    }
    s.At({0x00407C00, AddressOf(&Ass::CAssMan::GetAssetData)}, 8, [](Regs& r) {
        Record({0x7C00, r.args[0], r.args[1]});
        return r.args[0] == 0x22222222 ? 0u : g_sceneDefault + (r.args[0] & 0x30);
    });
    s.At({0x0048EA80}, 0, [](Regs&) {
        Record({0xEA80});
        return Bump(0x30);
    });
    s.At({0x0048C080}, 0, [](Regs& r) {
        Record({0xC080, Relocate(r.args[0])});
        return 0u;
    });
}

uint8_t* Block(uint32_t size) { return reinterpret_cast<uint8_t*>(Bump(size)); }

float RandomFloat(Lcg& random, float low, float high)
{
    return low + (high - low) * static_cast<float>(random.Below(100000)) / 99999.0f;
}

// A model of `count` geoms (names from kNames; some with a scene of their own, some the asset manager's; the first
// sometimes with bone volumes), random flags, bones, mask and counts.
CModel* MakeModel(Lcg& random, int count)
{
    auto* model = reinterpret_cast<CModel*>(Block(sizeof(CModel)));
    model->vtable = g_modelVtable;
    model->m_displayMode = static_cast<int>(random.Below(4)) == 3 ? 6 : static_cast<int>(random.Below(3));
    model->m_numBones = static_cast<int>(random.Below(3)) - 1;
    model->m_boneMatrices = random.Below(2) ? Block(0x40) : nullptr;
    auto* head = reinterpret_cast<sModelGeomNode*>(Block(sizeof(sModelGeomNode)));
    head->next = head->prev = head;
    head->entry = nullptr;
    model->m_geoms = head;
    model->m_geomListSize = count && random.Below(5) ? static_cast<uint32_t>(count) : random.Below(2);
    model->m_visibleGeoms = random.Next() ^ (random.Next() << 16);
    for (int i = 0; i < 4; ++i)
        reinterpret_cast<float*>(&model->m_scale)[i] = random.Below(3) ? 1.0f : RandomFloat(random, -2, 3);
    model->mp_lights = random.Below(2) ? Block(0x30) : nullptr;
    model->m_fileName = random.Next();
    for (int i = 0; i < 4; ++i)
        reinterpret_cast<float*>(&model->m_boundingSphere)[i] = RandomFloat(random, -9, 9);
    model->m_flags = static_cast<uint8_t>(random.Next());
    model->m_numGeoms = static_cast<int16_t>(count);
    model->m_numFileGeoms = static_cast<int16_t>(random.Below(3));
    model->m_numAssetGeoms = static_cast<int16_t>(random.Below(3));
    for (int i = 0; i < count; ++i) {
        auto* geom = reinterpret_cast<uint32_t*>(Block(0x20));
        geom[0] = reinterpret_cast<uint32_t>(g_geomVtable);
        geom[1] = 0x6E0 + i;
        auto* entry = reinterpret_cast<sModelGeom*>(Block(sizeof(sModelGeom)));
        entry->geom = geom;
        entry->name = kNames[random.Below(6)];
        entry->textures = Block(0x10);
        entry->scene = nullptr;
        entry->assetName = 0;
        switch (random.Below(3)) {
        case 0: { // a scene of its own
            entry->scene = Block(0xC0);
            *reinterpret_cast<uint32_t*>(entry->scene + 0xB0) = 1u << random.Below(32);
            break;
        }
        case 1:
            entry->assetName = kNames[random.Below(6)] ^ (random.Below(2) ? 0 : 0x10);
            break;
        default:
            break;
        }
        entry->boneVolumes = i == 0 && random.Below(2) ? Block(0x10) : nullptr;
        auto* node = reinterpret_cast<sModelGeomNode*>(Block(sizeof(sModelGeomNode)));
        node->entry = reinterpret_cast<void**>(entry);
        node->next = head;
        node->prev = head->prev;
        head->prev->next = node;
        head->prev = node;
    }
    return model;
}

uint8_t* MakeSkeleton(Lcg& random)
{
    uint8_t* skeleton = Block(0x18);
    *reinterpret_cast<uint8_t**>(skeleton + 4) = Block(0x40);
    *reinterpret_cast<int*>(skeleton + 0x14) = static_cast<int>(random.Below(60));
    return skeleton;
}

template <typename F>
F At(bool original, uint32_t address)
{
    return Fn<F>(original, address);
}

// Each member, called in a random order with random arguments, 30 times a round.
void TestModelMembers()
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}, {0x006918DC, 4}});
    StandIns& s = *new StandIns();
    StandInModel(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 60; ++round) {
        ok &= CompareTwoRuns("model members", reported, [&](bool original) {
            g_trace.clear();
            g_calls = 0;
            ResetArena(0xEE);
            Lcg random(0x40DE + round);
            g_allowMode = random.Below(2) ? 0xFFFFFF01u : 0xFFFFFF00u;
            uint8_t* defaultScene = Block(0x100);
            for (uint32_t i = 0; i < 0x100; i += 4)
                *reinterpret_cast<uint32_t*>(defaultScene + i) = random.Next();
            g_sceneDefault = reinterpret_cast<uint32_t>(defaultScene) & ~0x3Fu;
            int count = round % 7 == 0 ? 0 : round % 11 == 5 ? 36 + static_cast<int>(random.Below(4)) : 1 + static_cast<int>(random.Below(7));
            CModel* model = MakeModel(random, count);
            uint8_t* skeleton = MakeSkeleton(random);
            auto result = [&](uint32_t value) { Record({0xEE00, Relocate(value)}); };
            for (int step = 0; step < 30; ++step) {
                uint32_t name = random.Below(4) == 0 ? Nx::kAllGeoms : kNames[random.Below(6)] ^ (random.Below(5) ? 0 : 1);
                uint32_t byteArg = random.Pick({0u, 1u, 2u, 3u, 0x101u});
                switch (random.Below(35)) {
                case 0:
                    result(At<int(__thiscall*)(void*)>(original, 0x00492E40)(model));
                    break;
                case 1:
                    result(original ? Original<uint32_t(__thiscall*)(void*)>(0x00492E50)(model)
                                    : reinterpret_cast<uint32_t>(model->GetBoneMatrices()));
                    break;
                case 2:
                    result(At<uint32_t(__thiscall*)(void*)>(original, 0x00492FC0)(model));
                    break;
                case 3: {
                    int index = static_cast<int>(random.Below(count + 3)) - 1;
                    result(At<uint32_t(__thiscall*)(void*, int)>(original, 0x00494640)(model, index));
                    result(At<uint32_t(__thiscall*)(void*, int)>(original, 0x00494680)(model, index));
                    result(At<uint32_t(__thiscall*)(void*, int)>(original, 0x004946C0)(model, index));
                    break;
                }
                case 4:
                    result(At<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x00493CD0)(model, name));
                    break;
                case 5:
                    result(At<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x00494880)(model, name) & 0xFF);
                    break;
                case 6:
                    result(At<uint32_t(__thiscall*)(void*)>(original, 0x00494930)(model));
                    break;
                case 7:
                    result(At<uint32_t(__thiscall*)(void*)>(original, 0x00493B30)(model));
                    break;
                case 8:
                    if (model->m_numAssetGeoms == 1 && count == 0)
                        model->m_numAssetGeoms = 2; // (the original reads the empty head's entry)
                    if (model->m_numAssetGeoms == 1 && count &&
                        reinterpret_cast<sModelGeom*>(model->m_geoms->next->entry)->assetName == 0x22222222)
                        model->m_numAssetGeoms = 2; // (no scene: both read through null)
                    result(At<uint32_t(__thiscall*)(void*)>(original, 0x00492FF0)(model));
                    result(At<uint32_t(__thiscall*)(void*)>(original, 0x00493040)(model));
                    break;
                case 9: {
                    int mode = random.Pick({0, 1, 5, 6, model->m_displayMode});
                    g_allowMode = random.Below(2) ? 0xFFFFFF01u : 0xFFFFFF00u;
                    result(At<uint32_t(__thiscall*)(void*, int)>(original, 0x00492E10)(model, mode) & 0xFF);
                    break;
                }
                case 10:
                    if (random.Below(2))
                        model->m_boneMatrices = nullptr;
                    result(At<uint32_t(__thiscall*)(void*, void*)>(original, 0x00492DD0)(model, skeleton) & 0xFF);
                    break;
                case 11:
                    At<void(__thiscall*)(void*, void*)>(original, 0x00493460)(model, Block(0x10));
                    break;
                case 12:
                    At<void(__thiscall*)(void*, uint32_t)>(original, 0x00492E60)(model, byteArg);
                    At<void(__thiscall*)(void*, uint32_t)>(original, 0x00492E90)(model, random.Pick({0u, 1u, 2u, 3u}));
                    break;
                case 13: {
                    float scale[4];
                    for (float& f : scale)
                        f = random.Below(2) ? 1.0f : random.Pick({-0.0f, 2.0f, 1.0000001f});
                    result(At<uint32_t(__thiscall*)(void*, const float*)>(original, 0x00492EE0)(model, scale) & 0xFF);
                    break;
                }
                case 14: {
                    float out[4] = {9, 9, 9, 9};
                    result(At<uint32_t(__thiscall*)(void*, float*)>(original, 0x00492F30)(model, out));
                    for (float f : out)
                        result(std::bit_cast<uint32_t>(f));
                    break;
                }
                case 15: {
                    float sphere[4] = {RandomFloat(random, -5, 5), 2, 3, 4};
                    At<void(__thiscall*)(void*, const float*)>(original, 0x00492E80)(model, sphere);
                    break;
                }
                case 16: {
                    float out[4] = {9, 9, 9, 9};
                    result(At<uint32_t(__thiscall*)(void*, float*)>(original, 0x00492EB0)(model, out));
                    for (float f : out)
                        result(std::bit_cast<uint32_t>(f));
                    model->m_numBones = static_cast<int>(random.Below(3)) - 1;
                    break;
                }
                case 17:
                    At<void(__thiscall*)(void*, void*)>(original, 0x00492F90)(model, Block(0x10));
                    break;
                case 18:
                    result(At<uint32_t(__thiscall*)(void*, void*)>(original, 0x00493410)(model, skeleton) & 0xFF);
                    break;
                case 19:
                    At<void(__thiscall*)(void*, uint32_t)>(original, 0x00493B90)(model, random.Next());
                    result(At<uint32_t(__thiscall*)(void*)>(original, 0x00494B30)(model) & 0xFF);
                    break;
                case 20:
                    result(At<uint32_t(__thiscall*)(void*)>(original, 0x00493BD0)(model) & 0xFF);
                    break;
                case 21:
                    result(At<uint32_t(__thiscall*)(void*)>(original, 0x00493C10)(model) & 0xFF);
                    break;
                case 22:
                    result(At<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x00493C60)(model, byteArg) & 0xFF);
                    break;
                case 23:
                    result(At<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x00494B70)(model, byteArg) & 0xFF);
                    break;
                case 24:
                    result(At<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x00493FB0)(model, name) & 0xFF);
                    break;
                case 25: {
                    uint32_t material = random.Next();
                    int pass = static_cast<int>(random.Below(4));
                    result(At<uint32_t(__thiscall*)(void*, uint32_t, int, float, float)>(original, 0x00494110)(
                               model, material, pass, RandomFloat(random, -1, 1), RandomFloat(random, -1, 1)) &
                           0xFF);
                    float matrix[16] = {1, 2, 3};
                    result(At<uint32_t(__thiscall*)(void*, uint32_t, int, const float*)>(original, 0x00494160)(
                               model, material, pass, matrix) &
                           0xFF);
                    result(At<uint32_t(__thiscall*)(void*, uint32_t, int)>(original, 0x004941B0)(model, material,
                                                                                                    pass) &
                           0xFF);
                    break;
                }
                case 26:
                    result(At<uint32_t(__thiscall*)(void*, uint32_t, uint32_t)>(original, 0x00494800)(model, name,
                                                                                                     byteArg) &
                           0xFF);
                    break;
                case 27: {
                    uint32_t c = random.Next();
                    result(At<uint32_t(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t)>(original,
                                                                                                     0x004948D0)(
                               model, c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF, c >> 24) &
                           0xFF);
                    break;
                }
                case 28:
                    result(At<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x00494970)(model, random.Next()) &
                           0xFF);
                    break;
                case 29:
                    At<void(__thiscall*)(void*, uint32_t)>(original, 0x004949B0)(model, byteArg);
                    break;
                case 33:
                    Fn<void(__thiscall*)(void*)>(original, 0x00494700)(model);
                    break;
                case 30:
                    result(At<uint32_t(__thiscall*)(void*, int, int, int, int)>(original, 0x00492DC0)(model, 1, 2, 3,
                                                                                                     4) &
                           0xFF);
                    break;
                case 31:
                    model->m_flags = static_cast<uint8_t>(random.Next());
                    break;
                case 32:
                    model->m_numAssetGeoms = static_cast<int16_t>(random.Below(3));
                    break;
                default:
                    model->m_visibleGeoms = random.Next() ^ (random.Next() << 13);
                    break;
                }
                Record({0xEF00, model->m_visibleGeoms, model->m_flags}); // (after each step: later ones overwrite)
            }
            std::vector<uint32_t> trace = g_trace;
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "model: members over the geom list (counts, flags, bones, visibility, colours, lights, UVs)");
}

} // namespace

SELFTEST(Early, TestModelMembers, RemoveStandIns);
