// Self-tests for the engine's scene lights (src/game/gfx/scene_lights.cpp): original and ours on scene lights in the
// game's array, sectors with geoms (recording vtables) in an arena, the main scene, the script structures, nodes and
// memory stood in; the calls, results and memory compared.

#include <windows.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/engine_geom.h"
#include "game/gfx/engine_scene.h"
#include "game/gfx/engine_scene_list.h"
#include "game/gfx/light_manager.h"
#include "game/gfx/scene_lights.h"
#include "game/gfx/sector.h"
#include "game/lst/hashtable.h"
#include "game/nx/dynamic_light.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;
using Nx::sSceneLight;

void* g_geomVt[51];
uint8_t* g_mainScene;
uint32_t g_params; // which keys the stand-in structures have (bits)
Lcg* g_random;
uint32_t* g_fakeArrays; // three CArrays (+4 the size)

struct FakeSector { // a CSector's first 0x1C bytes, then its box
    uint32_t vtable, checksum, flags, terrain, rotY;
    Nx::CGeom* geom;
    uint32_t coll;
    uint32_t pad;
    float box[8];
};

uint32_t FloatBits(float f) { return std::bit_cast<uint32_t>(f); }

void StandInSceneLightCalls(StandIns& s)
{
    StandInMemory(s);
    s.At({0x005BBAE0, AddressOf(&Mem::Malloc)}, 0, [](Regs& r) {
        Record({0xBAE0, r.args[0]});
        return Bump(r.args[0]);
    });
    s.At({0x005BBB60, AddressOf(&Mem::FreeMalloc)}, 0, [](Regs& r) {
        Record({0xBB60, Relocate(r.args[0])});
        return 0u;
    });
    s.At({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, [](Regs&) { return 0u; });
    s.At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [](Regs&) { return 0u; });
    s.At({0x0049FD30, AddressOf(&Lst::HashTable::FlushAllItems)}, 0, [](Regs& r) {
        Record({0xFD30, Relocate(r.ecx)});
        return 0u;
    });
    s.At({0x00462740, AddressOf(&Lst::HashTable::PutItem)}, 8, [](Regs& r) {
        Record({0x2740, Relocate(r.ecx), Relocate(*reinterpret_cast<uint32_t*>(r.args[0])), Relocate(r.args[1])});
        return 1u;
    });
    s.At({0x0048C770, AddressOf(&Nx::GetMainScene)}, 0, [](Regs&) {
        Record({0xC770});
        return reinterpret_cast<uint32_t>(g_mainScene);
    });
    s.At({0x0049E6A0, AddressOf(&Nx::CSector::GetBoundingBox)}, 0, [](Regs& r) {
        return reinterpret_cast<uint32_t>(reinterpret_cast<FakeSector*>(r.ecx)->box);
    });
    s.At({0x0041CB90, AddressOf(&Nx::CDynamicLight::SetRadius)}, 4, [](Regs& r) {
        Record({0xCB90, Relocate(r.ecx), r.args[0]});
        return 0u;
    });
    // The script structures: bit n of the structure's value says whether key n is there; values from the key.
    auto has = [](uint32_t st, uint32_t key) { // (green and blue always there: the original reads stack garbage)
        return key == 0x2F6511DE || key == 0x61C9354B ? 1u : (st >> (key % 7)) & 1;
    };
    s.At({0x004766D0, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, float*, int)>(&Script::CStruct::GetFloat))},
         12, [has](Regs& r) {
             Record({0x66D0, r.ecx, r.args[0]});
             if (!has(r.ecx, r.args[0]))
                 return 0u;
             *reinterpret_cast<float*>(r.args[1]) = static_cast<float>(static_cast<int>(r.ecx % 300)) - 50.0f;
             return 1u;
         });
    auto integer = [has](Regs& r) {
        Record({0x6630, r.ecx, r.args[0], r.args[2] & 0xFF});
        if (!has(r.ecx, r.args[0]))
            return 0u;
        *reinterpret_cast<int*>(r.args[1]) = static_cast<int>(r.args[0] ^ r.ecx);
        return 1u;
    };
    s.At({0x00476630, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, int*, int)>(&Script::CStruct::GetInteger))},
         12, integer);
    s.At({0x00476CB0}, 12, integer);
    s.At({0x00476860, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, Script::CArray**, int)>(&Script::CStruct::GetArray))},
         12, [has](Regs& r) {
             Record({0x6860, r.ecx, r.args[0]});
             if (!has(r.ecx, r.args[0]))
                 return 0u;
             *reinterpret_cast<uint32_t*>(r.args[1]) = reinterpret_cast<uint32_t>(g_fakeArrays + 4 * (r.args[0] % 3));
             return 1u;
         });
    s.At({0x00476950, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, uint32_t*, int)>(&Script::CStruct::GetChecksum))},
         12, [has](Regs& r) {
             Record({0x6950, r.ecx, r.args[0]});
             if (!has(r.ecx, r.args[0]))
                 return 0u;
             *reinterpret_cast<uint32_t*>(r.args[1]) = 0x485A0CDB; // (another type: a null dynamic light, which DeleteSceneLight reads through)
             return 1u;
         });
    s.At({0x00476B40, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t)>(&Script::CStruct::ContainsFlag))}, 4,
         [has](Regs& r) {
             Record({0x6B40, r.ecx, r.args[0]});
             return has(r.ecx, r.args[0]);
         });
    // An array's elements: some match sectors' checksums.
    s.At({0x0046CC30, AddressOf(&Script::CArray::GetElement)}, 4, [](Regs& r) {
        Record({0xCC30, Relocate(r.ecx), r.args[0]});
        return 0x5EC00000u + (r.args[0] + (r.ecx >> 4)) % 4;
    });
    s.At({0x0045BE40, AddressOf(&SkateScript::GetNodeName)}, 0, [](Regs& r) {
        Record({0xBE40, r.args[0]});
        return 0x100 + (r.args[0] & 3);
    });
    s.At({0x0045C050, AddressOf(static_cast<bool (*)(Script::CStruct*, Mth::Vector*)>(&SkateScript::GetPosition))}, 0,
         [](Regs& r) {
             Record({0xC050, r.args[0]});
             auto* v = reinterpret_cast<float*>(r.args[1]);
             v[0] = 1.0f;
             v[1] = 2.0f;
             v[2] = static_cast<float>(r.args[0] & 7);
             v[3] = 1.0f;
             return 1u;
         });
    s.At({0x0045C1B0, AddressOf(&SkateScript::GetNode)}, 0, [](Regs& r) {
        Record({0xC1B0, r.args[0], r.args[1] & 0xFF});
        return 0x7B000000u + r.args[0];
    });
    s.At({0x00476410, AddressOf(&Script::CStruct::Init)}, 0, [](Regs& r) {
        Record({0x6410, Relocate(r.ecx)});
        return r.ecx;
    });
    s.At({0x00478540, AddressOf(&Script::CStruct::AppendStructureIfAny)}, 4, [](Regs& r) {
        Record({0x8540, Relocate(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x00477130, 0x00477090, AddressOf(&Script::CStruct::Clear)}, 0, [](Regs& r) {
        Record({0x7130, Relocate(r.ecx)});
        return 0u;
    });
    s.At({0x00409070, AddressOf(&Script::DeleteStruct)}, 0, [](Regs& r) {
        Record({0x9070, Relocate(r.args[0])});
        return 0u;
    });
    // The geoms: 0-5 vertices around the origin, colours from their address.
    static const uint16_t pops[51] = {};
    for (int slot = 0; slot < 51; ++slot) {
        uint16_t pop = slot == 37 || slot == 38 || slot == 40 ? 4 : 0;
        (void)pops;
        g_geomVt[slot] = s.Thunk(pop, [slot](Regs& r) {
            auto* geom = reinterpret_cast<uint8_t*>(r.ecx);
            uint32_t count = geom[0x11];
            Record({0xE00u + static_cast<uint32_t>(slot), Relocate(r.ecx)});
            if (slot == 36)
                return count;
            if (slot == 37) {
                auto* v = reinterpret_cast<float*>(r.args[0]);
                for (uint32_t i = 0; i < count; ++i) {
                    v[4 * i] = static_cast<float>(static_cast<int>(i * 7 % 11) - 5) * 1.1f;
                    v[4 * i + 1] = static_cast<float>(i % 3);
                    v[4 * i + 2] = static_cast<float>(static_cast<int>(i * 5 % 9) - 4);
                    v[4 * i + 3] = 1.0f;
                }
            }
            if (slot == 38) {
                for (uint32_t i = 0; i < count; ++i)
                    reinterpret_cast<uint32_t*>(r.args[0])[i] = 0x80F01020u + i * 0x0B0D0F;
            }
            if (slot == 40) {
                for (uint32_t i = 0; i < count; ++i)
                    Record({reinterpret_cast<uint32_t*>(r.args[0])[i]});
            }
            return 0u;
        });
    }
}

// A hand-built table (4 buckets) of `values` keyed by their addresses.
Lst::HashTable* MakeTable(const std::vector<void*>& values)
{
    auto* table = reinterpret_cast<Lst::HashTable*>(Bump(sizeof(Lst::HashTable)));
    std::memset(table, 0, sizeof(Lst::HashTable));
    table->m_bits = 2;
    table->m_buckets = reinterpret_cast<Lst::HashTable::Item*>(Bump(4 * sizeof(Lst::HashTable::Item)));
    std::memset(table->m_buckets, 0, 4 * sizeof(Lst::HashTable::Item));
    for (void* value : values) {
        uint32_t key = reinterpret_cast<uint32_t>(value);
        Lst::HashTable::Item* bucket = &table->m_buckets[key & 3];
        if (bucket->value) {
            auto* item = reinterpret_cast<Lst::HashTable::Item*>(Bump(sizeof(Lst::HashTable::Item)));
            *item = {key, value, bucket->next};
            bucket->next = item;
        } else {
            *bucket = {key, value, nullptr};
        }
    }
    return table;
}

float Coordinate(Lcg& random) { return static_cast<float>(static_cast<int>(random.Below(130)) - 60) / 10.0f; }

void TestSceneLights()
{
    GameMemory& memory = *new GameMemory({{0x006FAF80, 0x24 * 12}, {0x006FAEF0, 0x20}, {0x006FAAF0, 0x40},
                                          {0x007CC979, 1}, {0x007D1800, 4}, {0x006F3614, 16}, {0x006FA7D4, 1}});
    StandIns& s = *new StandIns();
    StandInSceneLightCalls(s);
    g_relocate = ArenaRelocate;
    alignas(16) static uint8_t manager[0x600];
    bool ok = true, reported = false;
    for (int round = 0; round < 100; ++round) {
        ok &= CompareTwoRuns("scene lights", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Zero();
            std::memset(manager, 0, sizeof(manager));
            GameVariable<uint8_t>(0x007CC979) = 1;
            GameVariable<uint8_t*>(0x007D1800) = manager;
            Lcg random(0x4700 + round);
            g_random = &random;
            std::vector<uint32_t> trace;
            // Sectors (with geoms or not) in the main scene.
            std::vector<void*> sectors;
            for (int i = static_cast<int>(random.Below(6)); i > 0; --i) {
                auto* sector = reinterpret_cast<FakeSector*>(Bump(sizeof(FakeSector)));
                std::memset(sector, 0, sizeof(FakeSector));
                sector->checksum = 0x5EC00000u + random.Below(4); // (an excluded array's elements match some)
                sector->terrain = random.Below(6) == 0 ? 0xED82767B : random.Below(6) == 0 ? 0xEC6542D3 : 1;
                if (random.Below(5)) {
                    auto* geom = reinterpret_cast<Nx::CGeom*>(Bump(0x20));
                    std::memset(geom, 0, 0x20);
                    geom->m_vtable = g_geomVt;
                    reinterpret_cast<uint8_t*>(geom)[0x11] = static_cast<uint8_t>(random.Below(6));
                    sector->geom = geom;
                }
                for (int a = 0; a < 3; ++a) {
                    float p = Coordinate(random), q = Coordinate(random);
                    sector->box[a] = p < q ? p : q;
                    sector->box[4 + a] = p < q ? q : p;
                }
                sectors.push_back(sector);
            }
            g_fakeArrays = reinterpret_cast<uint32_t*>(Bump(48));
            for (int i = 0; i < 3; ++i) {
                g_fakeArrays[4 * i] = 0;
                g_fakeArrays[4 * i + 1] = random.Below(4); // (m_size)
                g_fakeArrays[4 * i + 2] = g_fakeArrays[4 * i + 3] = 0;
            }
            g_mainScene = random.Below(8) ? reinterpret_cast<uint8_t*>(Bump(0x280)) : nullptr;
            if (g_mainScene)
                *reinterpret_cast<Lst::HashTable**>(g_mainScene + 0xD0) = random.Below(6) ? MakeTable(sectors) : nullptr;
            // Scene lights (some reaching some sectors) and dynamic lights of some of their names.
            auto* lights = &GameVariable<sSceneLight>(0x006FAF80);
            int count = static_cast<int>(random.Below(5));
            for (int i = 0; i < count; ++i) {
                sSceneLight& light = lights[i];
                light.m_pos = Mth::Vector(Coordinate(random), Coordinate(random), Coordinate(random), 1.0f);
                light.m_intensity = random.Below(5) ? static_cast<float>(random.Below(100)) / 50.0f : 0.0f;
                light.m_radius = static_cast<float>(random.Below(9));
                light.m_name = 0x100 + random.Below(4);
                light.m_colour = random.Next();
                std::vector<void*> reached;
                for (void* sector : sectors) {
                    if (random.Below(2))
                        reached.push_back(sector);
                }
                light.mp_sectors = MakeTable(reached);
            }
            for (int i = count; i < 12; ++i) // (every slot has its table: the array's static constructor's)
                lights[i].mp_sectors = MakeTable({});
            GameVariable<int>(0x006FAEF4) = count;
            auto** dynamic = &GameVariable<Nx::CDynamicLightBase*>(0x006FAAF0);
            int dynamics = static_cast<int>(random.Below(4));
            for (int i = 0; i < dynamics; ++i) {
                auto* light = reinterpret_cast<Nx::CDynamicLightBase*>(Bump(sizeof(Nx::CDynamicLightBase)));
                std::memset(light, 0, sizeof(Nx::CDynamicLightBase));
                light->vtable = reinterpret_cast<void**>(0x00649290);
                light->owner = 0x100 + random.Below(5);
                dynamic[i] = light;
            }
            GameVariable<int>(0x006FAEF0) = dynamics;
            for (int step = 0; step < 10; ++step) {
                g_trace.clear();
                uint32_t name = 0x100 + random.Below(5);
                void* sector = sectors.empty() ? nullptr : sectors[random.Below(sectors.size())];
                switch (random.Below(14)) {
                case 0:
                    if (count) {
                        sSceneLight* light = &lights[random.Below(count)];
                        Fn<void(__thiscall*)(void*, float)>(original, 0x0048F1A0)(
                            light, random.Below(3) ? static_cast<float>(random.Below(5)) : light->m_radius);
                        if (sector)
                            trace.push_back(Fn<uint32_t(__thiscall*)(void*, void*)>(original, 0x0048F200)(light, sector) & 0xFF);
                    }
                    break;
                case 1:
                    if (count)
                        Fn<void(__thiscall*)(void*, uint32_t)>(original, 0x0048FFE0)(
                            &lights[random.Below(count)],
                            random.Below(2) ? reinterpret_cast<uint32_t>(g_fakeArrays + 4 * random.Below(3)) : 0u);
                    break;
                case 2:
                case 3:
                    if (sector)
                        Fn<void(__cdecl*)(void*)>(original, 0x0048F250)(sector);
                    break;
                case 4: {
                    uint32_t n = random.Below(6);
                    auto* in = reinterpret_cast<uint32_t*>(Bump(32));
                    auto* out = reinterpret_cast<uint32_t*>(Bump(32));
                    auto* positions = reinterpret_cast<float*>(Bump(16 * 6));
                    for (uint32_t i = 0; i < 6; ++i) {
                        in[i] = random.Next();
                        for (int a = 0; a < 3; ++a)
                            positions[4 * i + a] = Coordinate(random);
                    }
                    float normal[4] = {Coordinate(random), Coordinate(random), Coordinate(random), 0.0f};
                    Fn<void(__cdecl*)(void*, void*, void*, void*, uint32_t)>(original, 0x0048F620)(
                        in, out, positions, random.Below(3) ? normal : nullptr, n);
                    break;
                }
                case 5:
                    Fn<void(__cdecl*)(void*, uint32_t)>(original, 0x0048FC00)(nullptr, random.Below(6) ? name : 0);
                    break;
                case 6:
                    Fn<void(__cdecl*)(uint32_t)>(original, 0x0048FCA0)(name);
                    count = GameVariable<int>(0x006FAEF4);
                    break;
                case 7:
                    if (random.Below(3) == 0) {
                        Fn<void(__cdecl*)()>(original, 0x0048FDA0)();
                        count = 0;
                    }
                    break;
                case 8: {
                    uint32_t nodes[4] = {random.Next() & 0xFF, 3, 7, 9};
                    Fn<void(__cdecl*)(const uint32_t*, int, int, uint32_t)>(original, 0x0048FE40)(
                        nodes, static_cast<int>(random.Below(5)), static_cast<int>(random.Below(4)), random.Next());
                    GameVariable<uint8_t>(0x006FA7D4) = static_cast<uint8_t>(random.Below(5) == 0);
                    for (int f = static_cast<int>(random.Below(6)); f > 0; --f)
                        Fn<void(__cdecl*)()>(original, 0x004904E0)();
                    break;
                }
                case 9:
                    Fn<void(__cdecl*)()>(original, 0x0048FDE0)();
                    break;
                case 10:
                    Fn<void(__cdecl*)(uint32_t, uint32_t)>(original, 0x004901D0)(name, random.Next());
                    break;
                case 11: {
                    float pos[4] = {Coordinate(random), Coordinate(random), Coordinate(random), 1.0f};
                    if (random.Below(4) == 0)
                        GameVariable<int>(0x006FAEF4) = 150;
                    trace.push_back(ArenaRelocate(
                        Fn<uint32_t(__cdecl*)(uint32_t, const float*, float, uint32_t, float, uint32_t)>(original, 0x00490400)(
                            name, pos, static_cast<float>(static_cast<int>(random.Below(200)) - 20), random.Next(),
                            static_cast<float>(random.Below(9)), random.Below(2) ? reinterpret_cast<uint32_t>(g_fakeArrays) : 0u)));
                    if (GameVariable<int>(0x006FAEF4) == 150)
                        for (int i = count; i < 12; ++i) // (every slot has its table: the array's static constructor's)
                lights[i].mp_sectors = MakeTable({});
            GameVariable<int>(0x006FAEF4) = count;
                    count = GameVariable<int>(0x006FAEF4);
                    break;
                }
                case 12:
                    Fn<void(__cdecl*)(uint32_t)>(original, 0x00490570)(random.Next());
                    count = GameVariable<int>(0x006FAEF4);
                    break;
                default: {
                    auto* light = reinterpret_cast<sSceneLight*>(Bump(sizeof(sSceneLight)));
                    trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*)>(original, 0x0048FF40)(light)));
                    Fn<void(__thiscall*)(void*)>(original, 0x0048FFD0)(light);
                    break;
                }
                }
                trace.insert(trace.end(), g_trace.begin(), g_trace.end());
                if (count > 10) // (keep within the memory saved)
                    break;
            }
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    // Crafted: 10 lights reaching one sector (the limit of 8); light ties on the vertex colours' selection (a dot of 0,
    // a distance of exactly the radius; 4 lights - the limit of 3); a light named 0; a brightness the same as the
    // light's while its dynamic light's differs.
    ok &= CompareTwoRuns("scene lights (crafted)", reported, [&](bool original) {
        g_trace.clear();
        ResetArena(0xEE);
        memory.Zero();
        std::memset(manager, 0, sizeof(manager));
        GameVariable<uint8_t>(0x007CC979) = 1;
        GameVariable<uint8_t*>(0x007D1800) = manager;
        std::vector<uint32_t> trace;
        auto* sector = reinterpret_cast<FakeSector*>(Bump(sizeof(FakeSector)));
        std::memset(sector, 0, sizeof(FakeSector));
        auto* geom = reinterpret_cast<Nx::CGeom*>(Bump(0x20));
        std::memset(geom, 0, 0x20);
        geom->m_vtable = g_geomVt;
        reinterpret_cast<uint8_t*>(geom)[0x11] = 3;
        sector->geom = geom;
        auto* lights = &GameVariable<sSceneLight>(0x006FAF80);
        for (int i = 0; i < 12; ++i) {
            lights[i].m_pos = Mth::Vector(static_cast<float>(i % 3), 0.0f, 0.0f, 1.0f);
            lights[i].m_intensity = 0.1f * static_cast<float>(i + 1);
            lights[i].m_radius = i < 10 ? 50.0f : 5.0f;
            lights[i].m_name = i == 3 ? 0 : 0x200 + i;
            lights[i].m_colour = 0x10203040u * static_cast<uint32_t>(i + 1);
            lights[i].mp_sectors = MakeTable(i < 10 ? std::vector<void*>{sector} : std::vector<void*>{});
        }
        GameVariable<int>(0x006FAEF4) = 10;
        Fn<void(__cdecl*)(void*)>(original, 0x0048F250)(sector);
        trace.insert(trace.end(), g_trace.begin(), g_trace.end());
        g_trace.clear();
        Fn<void(__cdecl*)(void*, uint32_t)>(original, 0x0048FC00)(nullptr, 0);
        trace.insert(trace.end(), g_trace.begin(), g_trace.end());
        // The vertex colours: the first position at (3, 4, 0) - 5 from light 0 at the origin (radius 5 now), the normal
        // (0, 0, 1) at right angles to every light's offset.
        for (int i = 0; i < 10; ++i) {
            lights[i].m_pos = Mth::Vector(0.0f, 0.0f, 0.0f, 1.0f);
            lights[i].m_radius = i == 0 ? 5.0f : 6.0f;
        }
        uint32_t in[2] = {0x80808080u, 0x10101010u};
        uint32_t out[2] = {1, 2};
        float positions[8] = {3.0f, 4.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f};
        float normal[4] = {0.0f, 0.0f, 1.0f, 0.0f};
        Fn<void(__cdecl*)(void*, void*, void*, void*, uint32_t)>(original, 0x0048F620)(in, out, positions, normal, 2);
        trace.push_back(out[0]);
        trace.push_back(out[1]);
        // The brightness the light already has (its dynamic light's differs).
        auto* dynamic = reinterpret_cast<Nx::CDynamicLightBase*>(Bump(sizeof(Nx::CDynamicLightBase)));
        std::memset(dynamic, 0, sizeof(Nx::CDynamicLightBase));
        dynamic->owner = 0x205;
        dynamic->intensity = 7.0f;
        GameVariable<Nx::CDynamicLightBase*>(0x006FAAF0) = dynamic;
        GameVariable<int>(0x006FAEF0) = 1;
        uint32_t params = 50; // (the brightness stand-in gives (params % 300) - 50: 0)
        while (!((params >> (0x9E497FC6u % 7)) & 1))
            params += 300;
        lights[5].m_intensity = 0.0f;
        g_trace.clear();
        Fn<void(__cdecl*)(uint32_t, uint32_t)>(original, 0x004901D0)(0x205, params);
        trace.insert(trace.end(), g_trace.begin(), g_trace.end());
        trace.push_back(FloatBits(dynamic->intensity));
        memory.Dump(trace);
        DumpArena(trace);
        return trace;
    });
    delete &s;
    delete &memory;
    g_relocate = nullptr;
    Check(ok, "scene lights: radius, sectors found and coloured, vertex colours lit, deleted, the queue, updated from "
              "parameters, added, from nodes");
}

} // namespace

SELFTEST(Early, TestSceneLights, RemoveStandIns);
