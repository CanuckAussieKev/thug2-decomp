// Self-tests for the engine's model's geoms (src/game/gfx/model_geoms.cpp, RND1-2): a model constructed, geoms added
// from model files, the asset manager and as they are (allocations failing on request), removed, drawn, tested
// against bone volumes, their textures replaced and coloured from stand-in scripts, then destroyed - the original
// and ours in random sequences over stand-ins for everything they call (the geoms, scenes, textures, asset manager,
// script structures, memory), every call, result and the arena compared.

#include <windows.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/ass/asset.h"
#include "game/ass/assman.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/gfx/engine_glue.h"
#include "game/gfx/model.h"
#include "game/gfx/pc_texture.h"
#include "game/script/names.h"
#include "game/script/struct.h"
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

constexpr uint32_t kNames[] = {0x11111111, 0x22222222, 0, 0x33333333, 0x22222222, Nx::kAllGeoms};
const char* const kFiles[] = {"models/Ped_A.skin", "Skater.MDL", "x", "boards/deck.skin"};
const char* const kTextureNames[] = {"Face.PNG", "shirt", "LOGO.png.old", "a.png", ""};

void* g_modelVtable[14];
void* g_geomVtable[64];
uint32_t g_calls;
bool g_failGeom;
uint32_t g_allowMode;

uint32_t Hash(const char* text)
{
    uint32_t hash = 2166136261u;
    for (; *text; ++text)
        hash = (hash ^ static_cast<uint8_t>(*text)) * 16777619u;
    return hash;
}
// The test's and the stand-ins' own blocks (never failing: only the memory manager's allocations do).
uint8_t* Block(uint32_t size)
{
    std::vector<bool> failures = std::move(g_failAllocations);
    g_failAllocations.clear();
    uint32_t at = Bump(size);
    g_failAllocations = std::move(failures);
    return reinterpret_cast<uint8_t*>(at);
}

// A scene: +4 its textures, +0xB0 flags.
uint32_t NewScene()
{
    uint8_t* scene = Block(0xC0);
    *reinterpret_cast<uint8_t**>(scene + 4) = Block(0x10);
    return reinterpret_cast<uint32_t>(scene);
}

// The script structures: a few (name, kind, value) entries - kind 'c' checksum, 'i' integer, 'a' array.
struct FakeStruct {
    struct Entry {
        uint32_t name;
        char kind;
        uint32_t value;
    } entries[6];
    int count;
};
uint32_t Lookup(Regs& r, char kind)
{
    auto* fake = reinterpret_cast<FakeStruct*>(r.ecx);
    Record({0x5700u + static_cast<uint32_t>(kind), Relocate(r.ecx), r.args[0], r.args[2]});
    for (int i = 0; i < fake->count; ++i) {
        if (fake->entries[i].name == r.args[0] && fake->entries[i].kind == kind) {
            *reinterpret_cast<uint32_t*>(r.args[1]) = fake->entries[i].value;
            return 1;
        }
    }
    return 0;
}

void StandInGeoms(StandIns& s)
{
    StandInMemory(s);
    const uint16_t pops[14] = {4, 4, 4, 4, 16, 4, 4, 4, 8, 12, 0, 0, 4, 4};
    for (int slot = 0; slot < 14; ++slot) {
        uint16_t pop = pops[slot];
        g_modelVtable[slot] = s.Thunk(pop, [slot, pop](Regs& r) {
            Record({0xA000u + slot, Relocate(r.ecx)});
            for (int i = 0; i < pop / 4; ++i)
                Record({Relocate(r.args[i])});
            if (slot == 3) {
                Record({*reinterpret_cast<uint32_t*>(r.ecx + 0x44)}); // (the flags and the geom count at the time)
                return g_allowMode;
            }
            if (slot == 2)
                return 0x2000u + ++g_calls;
            return 0xFFFFFF01u;
        });
    }
    for (int slot = 0; slot < 64; ++slot) {
        g_geomVtable[slot] = s.Thunk(4, [slot](Regs& r) {
            Record({0xB000u + slot, Relocate(r.ecx), r.args[0]});
            return 0u;
        });
    }
    struct Call {
        uint32_t address;
        uint16_t pop;
        uint32_t byteArgs;
    };
    for (Call c : {Call{0x0048D060, 0, 0}, Call{0x0048DE30, 12, 0}, Call{0x0048DE60, 4, 1}, Call{0x0048DF20, 4, 0},
                   Call{0x0048DF30, 20, 0}, Call{0x0049F3B0, 4, 1}, Call{0x0048DDF0, 12, 4}}) {
        s.At({c.address}, c.pop, [c](Regs& r) {
            Record({c.address & 0xFFFFF, Relocate(r.ecx)});
            for (int i = 0; i < c.pop / 4; ++i)
                Record({(c.byteArgs >> i) & 1 ? r.args[i] & 0xFF : Relocate(r.args[i])});
            return (++g_calls * 0x9E3779B9u) >> 28;
        });
    }
    // The real vtable's slot 3 (the shared 0x005BC7F0, true) - what the constructor calls.
    s.At({0x005BC7F0}, 4, [](Regs& r) {
        Record({0xC7F0, Relocate(r.ecx), r.args[0], *reinterpret_cast<uint32_t*>(r.ecx + 4)});
        return 1u;
    });
    // The geom made, deleted; scenes, textures and bone volumes deleted.
    s.At({0x0048BFB0, AddressOf(&Nx::CreateGeom)}, 0, [](Regs&) {
        Record({0xBFB0});
        if (g_failGeom)
            return 0u;
        auto* geom = reinterpret_cast<uint32_t*>(Block(0x20));
        geom[0] = reinterpret_cast<uint32_t>(g_geomVtable);
        reinterpret_cast<uint8_t*>(geom)[0x10] = static_cast<uint8_t>(g_calls++ & 1);
        return reinterpret_cast<uint32_t>(geom);
    });
    for (uint32_t address : {0x0048C3C0u, 0x0048C080u, 0x0049EFD0u, 0x0048B400u}) {
        s.At({address}, 0, [address](Regs& r) {
            Record({address & 0xFFFFF, Relocate(r.args[0])});
            return 0u;
        });
    }
    // The list's erase (the game's): recorded, then done for real by the original (stood in only to record).
    s.At({0x0048B580}, 0x20, [](Regs& r) { // a line against bone volumes: the distance and the extra written
        Record({0xB580, Relocate(r.ecx), r.args[0], r.args[1], r.args[2], r.args[3], r.args[4], r.args[5],
                std::bit_cast<uint32_t>(*reinterpret_cast<float*>(r.args[6])), *reinterpret_cast<uint32_t*>(r.args[7])});
        uint32_t k = ++g_calls;
        *reinterpret_cast<float*>(r.args[6]) = k % 3 == 0 ? 0.25f : 0.001f * static_cast<float>(k % 7);
        *reinterpret_cast<uint32_t*>(r.args[7]) = 0xE000 + k;
        return k % 4 == 0 ? 0u : 0x100u + k;
    });
    s.At({0x0048BC40}, 0x10, [](Regs& r) {
        Record({0xBC40, Relocate(r.ecx), r.args[0], r.args[1], r.args[2], *reinterpret_cast<uint32_t*>(r.args[3])});
        uint32_t k = ++g_calls;
        *reinterpret_cast<float*>(r.args[3]) = 1.5f * static_cast<float>(k);
        return k % 3 == 0 ? 1u : 0u;
    });
    s.At({0x004A31E0}, 0x10, [](Regs& r) {
        Record({0x31E0, Relocate(r.ecx), Relocate(r.args[0]), r.args[1], r.args[2], r.args[3]});
        return 0u;
    });
    // Messages.
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        Record({0x1C30, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1]});
        return 0u;
    });
    s.At({0x0046D110, AddressOf(&Script::FindChecksumNameOrUnknown)}, 0, [](Regs& r) {
        Record({0xD110, r.args[0]});
        return 0x1000u + (r.args[0] & 0xFF);
    });
    s.At({0x00478F60, AddressOf(static_cast<int (*)(uint32_t)>(&Script::GetInteger))}, 0, [](Regs& r) {
        Record({0x8F60, r.args[0]});
        return 0u;
    });
    // The asset manager.
    s.At({0x00407C00, AddressOf(&Ass::CAssMan::GetAssetData)}, 8, [](Regs& r) {
        Record({0x7C00, r.args[0], r.args[1]});
        return NewScene();
    });
    s.At({0x00407C70, AddressOf(&Ass::CAssMan::GetAssetDataByName)}, 8, [](Regs& r) {
        Record({0x7C70, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1]});
        return NewScene();
    });
    s.At({0x004087B0, AddressOf(&Ass::CAssMan::LoadOrGetAsset)}, 0x24, [](Regs& r) {
        Record({0x87B0, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1], r.args[2], r.args[3],
                r.args[4] & 0xFF, r.args[5] & 0xFF, r.args[6] & 0xFF, r.args[8]});
        const auto* params = reinterpret_cast<const uint8_t*>(r.args[7]);
        Record({params[0], *reinterpret_cast<const uint32_t*>(params + 4), params[8], params[9]});
        if (std::strstr(reinterpret_cast<const char*>(r.args[0]), ".tex") == nullptr)
            Record({*reinterpret_cast<const uint32_t*>(params + 8), 0u /* (+0xC isn't cleared) */,
                    params[0x10], params[0x11]});
        return NewScene();
    });
    s.At({0x0048BFE0}, 0, [](Regs& r) { // the base name: up to the last '.'
        const char* name = reinterpret_cast<const char*>(r.args[0]);
        char* base = reinterpret_cast<char*>(r.args[1]);
        std::memcpy(base, name, std::strlen(name) + 1);
        char* dot = std::strrchr(base, '.');
        if (dot)
            *dot = 0;
        Record({0xBFE0, Hash(name)});
        return dot ? 1u : 0u;
    });
    s.At({0x0049F180}, 0, [](Regs& r) {
        Record({0xF180, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1], r.args[2] & 0xFF});
        return reinterpret_cast<uint32_t>(Block(0x10));
    });
    s.At({0x0048C3D0}, 0, [](Regs& r) {
        Record({0xC3D0, Hash(reinterpret_cast<const char*>(r.args[0])), Relocate(r.args[1]), r.args[2],
                r.args[3] & 0xFF});
        return NewScene();
    });
    // Textures.
    s.At({0x0049FB20}, 8, [](Regs& r) {
        Record({0xFB20, Relocate(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0])),
                Hash(reinterpret_cast<const char*>(r.args[1]))});
        return (++g_calls % 3) == 0 ? 1u : 0u;
    });
    s.At({0x0049F800}, 8, [](Regs& r) {
        Record({0xF800, Relocate(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0])), Relocate(r.args[1])});
        return (++g_calls % 3) == 0 ? 1u : 0u;
    });
    s.At({0x0049F540}, 4, [](Regs& r) {
        Record({0xF540, r.args[0]});
        return reinterpret_cast<uint32_t>(Block(0x10));
    });
    s.At({0x0049FA20}, 0x1C, [](Regs& r) {
        Record({0xFA20, Relocate(r.args[0]), r.args[1], r.args[2], r.args[3] & 0xFF, r.args[4] & 0xFF,
                r.args[5] & 0xFF, r.args[6] & 0xFF});
        auto* texture = reinterpret_cast<uint32_t*>(Block(0x10));
        texture[1] = 0x7E000000 + ++g_calls;
        return reinterpret_cast<uint32_t>(texture);
    });
    s.At({0x0049F9B0}, 0x10, [](Regs& r) {
        Record({0xF9B0, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1] & 0xFF, r.args[2] & 0xFF,
                r.args[3] & 0xFF});
        return reinterpret_cast<uint32_t>(Block(0x10));
    });
    s.At({0x0049F500}, 4, [](Regs& r) {
        Record({0xF500, Relocate(r.args[0])});
        return 1u;
    });
    // The face texture and the skin colour.
    s.At({0x00487540}, 0, [](Regs& r) {
        Record({0x7540, Relocate(r.ecx)});
        return 0x4420u;
    });
    s.At({0x00487530}, 0, [](Regs& r) {
        Record({0x7530, Relocate(r.ecx)});
        return reinterpret_cast<uint32_t>(Block(0x20));
    });
    s.At({0x004875B0}, 0, [](Regs& r) {
        Record({0x75B0, Relocate(r.ecx)});
        return reinterpret_cast<uint32_t>("faces/overlay.img");
    });
    s.At({0x00487550}, 4, [](Regs& r) {
        Record({0x7550, Relocate(r.ecx), Relocate(r.args[0])});
        reinterpret_cast<uint8_t*>(r.args[0])[0x20] = static_cast<uint8_t>(0x40 + ++g_calls);
        return r.args[0];
    });
    s.At({0x00479020, AddressOf(static_cast<Script::CStruct* (*)(uint32_t)>(&Script::GetStructure))}, 0, [](Regs& r) {
        Record({0x9020, r.args[0]});
        return 0x5C000000u;
    });
    for (uint32_t address : {0x004869D0u, 0x004869A0u, 0x004869C0u}) {
        s.At({address}, 0, [address](Regs& r) {
            Record({address & 0xFFFFF, Relocate(r.args[0]), address == 0x004869D0u ? r.args[1] : 0});
            return 0u;
        });
    }
    s.At({0x00486F70}, 0, [](Regs& r) {
        Record({0x6F70, Relocate(r.args[0]), Relocate(r.args[1]), r.args[2] & 0xFF, r.args[3], r.args[4]});
        return 1u;
    });
    s.At({0x004C82D0, AddressOf(&Nx::HsvToRgb)}, 0, [](Regs& r) {
        float h = std::bit_cast<float>(r.args[3]), sat = std::bit_cast<float>(r.args[4]),
              v = std::bit_cast<float>(r.args[5]);
        Record({0x82D0, r.args[3], r.args[4], r.args[5]});
        *reinterpret_cast<float*>(r.args[0]) = v * (1.0f - sat * 0.25f);
        *reinterpret_cast<float*>(r.args[1]) = h / 360.0f;
        *reinterpret_cast<float*>(r.args[2]) = sat * 0.999f;
        return 0u;
    });
    // Memory contexts.
    s.At({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, [](Regs& r) {
        Record({0xB0E0, Relocate(r.args[0])});
        return 0u;
    });
    s.At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [](Regs&) {
        Record({0xB110});
        return 0u;
    });
    // Script structures.
    s.At({0x00476950, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, uint32_t*, int)>(
                          &Script::CStruct::GetChecksum))},
         12, [](Regs& r) { return Lookup(r, 'c'); });
    s.At({0x00476630,
          AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, int*, int)>(&Script::CStruct::GetInteger))},
         12, [](Regs& r) { return Lookup(r, 'i'); });
    s.At({0x00476860, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, Script::CArray**, int)>(
                          &Script::CStruct::GetArray))},
         12, [](Regs& r) { return Lookup(r, 'a'); });
}

float RandomFloat(Lcg& random, float low, float high)
{
    return low + (high - low) * static_cast<float>(random.Below(100000)) / 99999.0f;
}

// An array of `count` values (one kept in place).
Script::CArray* MakeArray(uint32_t count, const uint32_t* values)
{
    auto* array = reinterpret_cast<Script::CArray*>(Block(sizeof(Script::CArray)));
    array->m_size = count;
    if (count == 1) {
        array->m_single = values[0];
    } else {
        array->m_elements = reinterpret_cast<uint32_t*>(Block(4 * (count + 1)));
        for (uint32_t i = 0; i < count; ++i)
            array->m_elements[i] = values[i];
    }
    return array;
}

// A colour script's parameters: name=, and materials= (structures of a material and a pass) or material= with pass=
// (an array or one) - each part there or not.
FakeStruct* MakeParams(Lcg& random)
{
    auto* params = reinterpret_cast<FakeStruct*>(Block(sizeof(FakeStruct)));
    params->count = 0;
    auto add = [](FakeStruct* s, uint32_t name, char kind, uint32_t value) {
        s->entries[s->count++] = {name, kind, value};
    };
    add(params, 0xB6F08F39, 'c', kNames[random.Below(6)]); // (always: without it the name is uninitialised)
    switch (random.Below(4)) {
    case 0: {
        uint32_t count = random.Below(4);
        uint32_t values[3];
        for (uint32_t i = 0; i < count; ++i) {
            auto* material = reinterpret_cast<FakeStruct*>(Block(sizeof(FakeStruct)));
            material->count = 0;
            if (random.Below(4) || i == 0) // (the first always: the material is uninitialised before it)
                add(material, 0, 'c', 0x7A000000 + random.Below(16));
            if (random.Below(4))
                add(material, 0, 'i', random.Below(4));
            values[i] = reinterpret_cast<uint32_t>(material);
        }
        add(params, 0x64E8E94A, 'a', reinterpret_cast<uint32_t>(MakeArray(count, values)));
        break;
    }
    case 1: {
        add(params, 0x83418A6A, 'c', 0x7B000000 + random.Below(16));
        if (random.Below(2)) {
            uint32_t values[3] = {random.Below(4), random.Below(4), random.Below(4)};
            add(params, 0x318F2BDB, 'a', reinterpret_cast<uint32_t>(MakeArray(random.Below(4), values)));
        } else if (random.Below(3)) {
            add(params, 0x318F2BDB, 'i', random.Below(4));
        }
        break;
    }
    case 2:
        if (random.Below(2))
            add(params, 0x318F2BDB, 'i', 2); // (no material: nothing for the multicolour geoms)
        break;
    default:
        break;
    }
    return params;
}

template <typename F>
F At(bool original, uint32_t address)
{
    return Fn<F>(original, address);
}

// Random sequences: a model made, geoms added, removed and cleared, drawn, tested, textures replaced, coloured, the
// model destroyed.
void TestModelGeoms()
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}, {0x006918DC, 4}, {0x00700F1C, 4}, {0x00658555, 1}});
    StandIns& s = *new StandIns();
    StandInGeoms(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 80; ++round) {
        ok &= CompareTwoRuns("model geoms", reported, [&](bool original) {
            g_trace.clear();
            g_calls = 0;
            g_failGeom = false;
            ResetArena(0xEE);
            Lcg random(0x6E0 + round);
            GameVariable<uint8_t>(0x007CC979) = 1;
            uint8_t* manager = Block(0x5E8);
            *reinterpret_cast<uint8_t**>(manager + 0x110) = Block(0x10);
            GameVariable<uint8_t*>(0x007D1800) = manager;
            uint8_t* assets = Block(0x40);
            assets[0x14] = static_cast<uint8_t>(random.Below(2));
            GameVariable<uint8_t*>(0x006918DC) = assets;
            GameVariable<uint8_t*>(0x00700F1C) = Block(0x10);
            GameVariable<uint8_t>(0x00658555) = static_cast<uint8_t>(random.Below(2));
            g_allowMode = random.Below(3) ? 0xFFFFFF01u : 0xFFFFFF00u;
            auto result = [&](uint32_t value) { Record({0xEE00, Relocate(value)}); };

            auto* model = reinterpret_cast<CModel*>(Block(sizeof(CModel)));
            for (uint32_t i = 0; i < sizeof(CModel); i += 4)
                *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(model) + i) = random.Next() ^ (random.Next() << 12);
            if (random.Below(3) == 0)
                model->m_displayMode = 6;
            if (round == 3)
                g_failAllocations = {true}; // (no list head)
            result(At<uint32_t(__thiscall*)(void*)>(original, 0x00494C40)(model));
            if (!model->m_geoms)
                return g_trace; // (nothing more can be done with it)
            model->vtable = g_modelVtable;
            uint8_t* skeleton = Block(0x18);
            *reinterpret_cast<uint8_t**>(skeleton + 4) = Block(0x40);
            *reinterpret_cast<int*>(skeleton + 0x14) = static_cast<int>(random.Below(60));

            for (int step = 0; step < 24; ++step) {
                uint32_t name = kNames[random.Below(6)] ^ (random.Below(6) ? 0 : 1);
                bool defaultHeap = random.Below(3) != 0;
                bool failEntry = !defaultHeap && random.Below(4) == 0; // (the next allocation: the node's)
                // (the geom failing on the default heap only at the last step: the original goes on with a null
                // geom, which only destroying the model survives)
                bool failGeom = (!defaultHeap || step == 23) && random.Below(4) == 0;
                switch (random.Below(16)) {
                case 0: {
                    auto* geom = reinterpret_cast<uint32_t*>(Block(0x20));
                    geom[0] = reinterpret_cast<uint32_t>(g_geomVtable);
                    reinterpret_cast<uint8_t*>(geom)[0x10] = static_cast<uint8_t>(random.Below(2));
                    g_failAllocations = {failEntry};
                    result(At<uint32_t(__thiscall*)(void*, void*, uint32_t, uint32_t)>(original, 0x004938E0)(
                               model, geom, name, defaultHeap) &
                           0xFF);
                    break;
                }
                case 1:
                case 2:
                    g_failAllocations = {failEntry};
                    g_failGeom = failGeom;
                    result(At<uint32_t(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t)>(original,
                                                                                                    0x004934A0)(
                               model, 0x4A000000 + random.Below(8), name, random.Below(3), defaultHeap) &
                           0xFF);
                    break;
                case 3:
                case 4:
                    g_failAllocations = {failEntry};
                    g_failGeom = failGeom;
                    result(At<uint32_t(__thiscall*)(void*, const char*, uint32_t, uint32_t, int, uint32_t, uint32_t,
                                                    uint32_t, uint32_t)>(original, 0x004935E0)(
                               model, kFiles[random.Below(4)], name, random.Below(2), static_cast<int>(random.Below(3)),
                               random.Below(2), random.Below(2), defaultHeap, random.Below(2)) &
                           0xFF);
                    break;
                case 5:
                    At<void(__thiscall*)(void*, uint32_t)>(original, 0x00494730)(model, name);
                    break;
                case 6:
                    if (random.Below(4) == 0)
                        At<void(__thiscall*)(void*)>(original, 0x00494BA0)(model);
                    break;
                case 7: {
                    float matrix[16];
                    for (float& f : matrix)
                        f = RandomFloat(random, -3, 3);
                    model->m_displayMode = static_cast<int>(random.Below(9));
                    if (random.Below(2))
                        model->m_displayMode = 0;
                    model->m_flags = static_cast<uint8_t>(random.Next());
                    model->m_visibleGeoms = random.Next() ^ (random.Next() << 12);
                    for (int i = 0; i < 4; ++i)
                        (&model->m_scale.x)[i] = RandomFloat(random, 0.5f, 2);
                    result(At<uint32_t(__thiscall*)(void*, float*, uint32_t, void*)>(original, 0x00493090)(
                               model, matrix, random.Below(2), random.Below(3) ? skeleton : nullptr) &
                           0xFF);
                    for (float f : matrix)
                        result(std::bit_cast<uint32_t>(f));
                    break;
                }
                case 8: {
                    uint32_t out = 0x0BAD;
                    result(At<uint32_t(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                                    uint32_t*)>(original, 0x004939E0)(
                        model, 1, 2, 3, 4, 0x55 + random.Below(3), 6, random.Below(4) ? &out : nullptr));
                    result(out);
                    float volume = At<float(__thiscall*)(void*, uint32_t, uint32_t, uint32_t)>(original, 0x00493AB0)(
                        model, 7, 8, random.Below(30));
                    result(std::bit_cast<uint32_t>(volume));
                    break;
                }
                case 9:
                    result(At<uint32_t(__thiscall*)(void*, uint32_t, const char*, const char*)>(original, 0x00493D10)(
                               model, name, kTextureNames[random.Below(5)], kTextureNames[random.Below(5)]) &
                           0xFF);
                    break;
                case 10:
                    result(At<uint32_t(__thiscall*)(void*, uint32_t, const char*, uint32_t)>(original, 0x00493E80)(
                               model, name, kTextureNames[random.Below(5)], random.Next()) &
                           0xFF);
                    break;
                case 11:
                    result(At<uint32_t(__thiscall*)(void*, uint32_t, float, float, float)>(original, 0x00494010)(
                               model, name, RandomFloat(random, 0, 360), RandomFloat(random, 0, 1),
                               RandomFloat(random, 0, 1)) &
                           0xFF);
                    break;
                case 12:
                    result(At<uint32_t(__thiscall*)(void*, void*, float, float, float)>(original, 0x00494200)(
                               model, MakeParams(random), RandomFloat(random, 0, 360), RandomFloat(random, 0, 1),
                               RandomFloat(random, 0, 1)) &
                           0xFF);
                    break;
                case 13:
                    result(At<uint32_t(__thiscall*)(void*, void*)>(original, 0x00494450)(model, MakeParams(random)) &
                           0xFF);
                    break;
                case 14:
                    result(At<uint32_t(__thiscall*)(void*, void*, const char*, uint32_t)>(original, 0x00494A00)(
                               model, Block(0x10), kTextureNames[random.Below(5)], name) &
                           0xFF);
                    break;
                default:
                    // Bone volumes on a geom (+4 what's let go).
                    for (sModelGeomNode* node = model->m_geoms->next; node != model->m_geoms; node = node->next) {
                        auto* entry = reinterpret_cast<sModelGeom*>(node->entry);
                        if (!entry->boneVolumes && random.Below(3) == 0) {
                            entry->boneVolumes = Block(0x10);
                            *reinterpret_cast<uint32_t*>(entry->boneVolumes + 4) = 0x0B0E0000 + step;
                        }
                    }
                    model->mp_lights = random.Below(2) ? Block(0x10) : nullptr;
                    break;
                }
                g_failAllocations.clear();
                g_failGeom = false;
                Record({0xEF01, static_cast<uint32_t>(model->m_numGeoms), static_cast<uint32_t>(model->m_numFileGeoms),
                        static_cast<uint32_t>(model->m_numAssetGeoms), model->m_geomListSize});
                Record({0xEF00, model->m_visibleGeoms, model->m_flags, static_cast<uint32_t>(model->m_displayMode),
                        model->m_fileName});
            }
            if (random.Below(2))
                At<void(__thiscall*)(void*)>(original, 0x00494D00)(model);
            else
                result(At<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x00494DC0)(model, random.Below(2)));
            std::vector<uint32_t> trace = g_trace;
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "model geoms: made, geoms added / removed / cleared, drawn, bone volumes, textures, script colours, "
              "destroyed");
}

} // namespace

SELFTEST(Early, TestModelGeoms, RemoveStandIns);
