// Self-tests for the engine's spawned polygons and its texture splat maker (src/game/gfx/spawned_polys.cpp,
// engine_effects.cpp's CreateTextureSplatAt, RND1-5): the original and ours over stand-in tables, primitives (a
// recording vtable), pools, collision grids and objects, the splat maker of the PC code and the clock - polygons
// spawned, aged over frames (running out), killed; splats made along lines with and without something to follow.

#include <windows.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <vector>

#include "game/core/pool.h"
#include "game/gfx/engine_effects.h"
#include "game/gfx/pc_primitive.h"
#include "game/gfx/spawned_polys.h"
#include "game/gfx/texture_splats.h"
#include "game/nx/coll_grid.h"
#include "game/script/cfuncs_engine.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rnd1_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace Rnd1Tests;
using Nx::sSpawnedPoly;

void* g_primVtable[24];
uint32_t g_contains; // a bit per pool: whether it holds the item

void StandInPolys(StandIns& s)
{
    StandInTables(s);
    const uint16_t pops[24] = {4, 12, 4, 12, 0, 0, 16, 16, 8, 8, 8, 16, 8, 8, 8, 8, 12, 8, 8, 4, 4, 4, 4, 4};
    for (int slot = 0; slot < 24; ++slot) {
        uint16_t pop = pops[slot];
        g_primVtable[slot] = s.Thunk(pop, [slot, pop](Regs& r) {
            Record({0xB000u + slot, Relocate(r.ecx)});
            for (int i = 0; i < pop / 4; ++i)
                Record({Relocate(r.args[i])});
            return r.ecx;
        });
    }
    s.At({0x004C5230, AddressOf(&Nx::CPcPrimitive::Construct)}, 4, [](Regs& r) {
        Record({0x5230, Relocate(r.ecx), r.args[0]});
        std::memset(reinterpret_cast<void*>(r.ecx), 0, 0x158);
        *reinterpret_cast<void***>(r.ecx) = g_primVtable;
        return r.ecx;
    });
    s.At({0x004472D0, AddressOf(&CFuncs::FreeSpawnedPolyDetails)}, 0, [](Regs& r) {
        Record({0x72D0, Relocate(r.args[0])});
        return 0u;
    });
    s.At({0x005BA3B0, AddressOf(&Mem::Pool::Contains)}, 4, [](Regs& r) {
        uint32_t which = r.ecx == GameVariable<uint32_t>(0x00700508) ? 1u : 2u;
        Record({0xA3B0, Relocate(r.ecx), Relocate(r.args[0])});
        return (g_contains & which) ? 1u : 0u;
    });
    s.At({0x005BA400, AddressOf(&Mem::Pool::Free)}, 4, [](Regs& r) {
        Record({0xA400, Relocate(r.ecx), Relocate(r.args[0])});
        return 0u;
    });
}

void RandomVector(Lcg& random, Mth::Vector* v, float range)
{
    v->x = RandomFloat(random, -range, range);
    v->y = RandomFloat(random, -range, range);
    v->z = RandomFloat(random, -range, range);
    v->w = RandomFloat(random, 0, 1);
}

// Polygons spawned (textured or not, flagged or not), aged over frames - some running out - and killed.
void TestSpawnedPolys()
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}, {0x007004AC, 4}, {0x00700500, 0x10}});
    StandIns& s = *new StandIns();
    StandInPolys(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 40; ++round) {
        ok &= CompareTwoRuns("spawned polys", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0x9010 + round);
            GameVariable<uint8_t>(0x007CC979) = 1;
            Lcg random(0x9011 + round);
            HashTable* table = NewTable();
            GameVariable<HashTable*>(0x007004AC) = table;
            GameVariable<int>(0x00700500) = static_cast<int>(random.Below(2));
            GameVariable<uint8_t*>(0x00700508) = random.Below(3) ? Block(0x20) : nullptr;
            GameVariable<uint8_t*>(0x0070050C) = random.Below(3) ? Block(0x20) : nullptr;
            g_contains = random.Below(4);
            for (uint32_t i = 0, n = 1 + random.Below(4); i < n; ++i) {
                auto* poly = reinterpret_cast<sSpawnedPoly*>(Block(sizeof(sSpawnedPoly)));
                Record({Relocate(Fn<uint32_t(__thiscall*)(void*)>(original, 0x00491990)(poly))});
                for (uint32_t k = 0; k < sizeof(sSpawnedPoly); k += 4)
                    Record({*reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(poly) + k)});
                poly->age = RandomFloat(random, 0, 3); // (SpawnPoly starts it again)
                RandomVector(random, &poly->pos, 50);
                for (int k = 0; k < 2; ++k) {
                    poly->startDims[k] = RandomFloat(random, 0.5f, 9);
                    poly->endDims[k] = RandomFloat(random, 0.5f, 9);
                }
                for (int c = 0; c < 4; ++c) {
                    for (int k = 0; k < 4; ++k) {
                        poly->startColours[c][k] = static_cast<uint8_t>(random.Next());
                        poly->endColours[c][k] = static_cast<uint8_t>(random.Next());
                    }
                }
                poly->flags = random.Below(4);
                poly->flags &= random.Below(5) ? ~1u : ~0u; // (some already done)
                poly->lifetime = random.Pick({0.1f, 0.5f, 1.0f, 2.5f});
                poly->texture = random.Below(2) ? Block(0x10) : nullptr;
                poly->blendMode = random.Next();
                poly->justX = random.Pick({-1.0f, 0.0f, 1.0f, -0.0f, 0.5f});
                poly->justY = random.Pick({-1.0f, 0.0f, 1.0f, 0.25f});
                Mth::Vector direction, up;
                RandomVector(random, &direction, 1);
                RandomVector(random, &up, 1);
                if (random.Below(6) == 0)
                    up = direction; // (parallel)
                // (no primitive - out of memory - and both write through null)
                Fn<void(__cdecl*)(void*, Mth::Vector*, Mth::Vector*)>(original, 0x00492450)(poly, &direction, &up);
            }
            for (int frame = 0; frame < 6; ++frame) {
                g_frameSeconds = random.Pick({0.0166, 0.1, 0.4, 0.0, 1.0});
                Fn<void(__cdecl*)()>(original, 0x004919C0)();
            }
            if (random.Below(2))
                Fn<void(__cdecl*)()>(original, 0x00491E10)();
            std::vector<uint32_t> trace = g_trace;
            for (HashTable::Item& item : g_items[table])
                trace.push_back(Relocate(item.key));
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "spawned polys: spawned (turn, texture, corners), aged (colours, size, justification), run out, killed");
}

// --- the splat maker ---

uint32_t g_now;
uint32_t g_list; // the grids along the line (null-terminated; 0: none)
int g_failSplat;
std::map<uint32_t, std::vector<uint8_t*>> g_inBox; // a grid's objects
void* g_bboxVtable[24];

struct FakeObject {
    void** vtable;
    uint32_t checksum;
    uint8_t pad[8];
    Mth::BBox box; // (what its slot 21 gives)
};

void StandInSplatMaker(StandIns& s)
{
    StandInTables(s);
    s.At({0x00478F60, AddressOf(static_cast<int (*)(uint32_t)>(&Script::GetInteger))}, 0, [](Regs& r) {
        Record({0x8F60, r.args[0]});
        return 200u + g_now % 50;
    });
    s.At({0x005BE970, AddressOf(&Tmr::GetTime)}, 0, [](Regs&) {
        Record({0xE970});
        return g_now;
    });
    s.At({0x0049F540}, 4, [](Regs& r) {
        Record({0xF540, Relocate(r.ecx), r.args[0]});
        auto* texture = reinterpret_cast<uint32_t*>(Block(0x10));
        texture[1] = 0x7E000000 + (r.args[0] & 0xFF);
        return reinterpret_cast<uint32_t>(texture);
    });
    s.At({0x0048C980}, 0, [](Regs& r) {
        const auto* line = reinterpret_cast<const float*>(r.args[0]);
        Record({0xC980});
        for (int i = 0; i < 8; ++i)
            Record({std::bit_cast<uint32_t>(line[i])});
        return g_list;
    });
    s.At({0x00418710, AddressOf(&Nx::CCollGrid::ObjectsInBox)}, 4, [](Regs& r) {
        const auto* box = reinterpret_cast<const float*>(r.args[0]);
        Record({0x8710, Relocate(r.ecx)});
        for (int i = 0; i < 8; ++i)
            Record({std::bit_cast<uint32_t>(box[i])});
        std::vector<uint8_t*>& objects = g_inBox[r.ecx];
        return objects.empty() ? 0u : reinterpret_cast<uint32_t>(objects.data());
    });
    for (int slot = 0; slot < 24; ++slot) {
        g_bboxVtable[slot] = s.Thunk(0, [](Regs& r) {
            Record({0x0B0B, Relocate(r.ecx)});
            return r.ecx + offsetof(FakeObject, box);
        });
    }
    s.At({0x0048C510}, 0, [](Regs& r) {
        Record({0xC510, r.args[0]});
        return 0x5EC00000u + (r.args[0] & 0xFFFF);
    });
    s.At({0x004B9C30, AddressOf(&Nx::CreateTextureSplat)}, 0, [](Regs& r) {
        Record({0x9C30, r.args[0], r.args[1]});
        const auto* params = reinterpret_cast<const Nx::sSplatParams*>(r.args[2]);
        const auto* words = reinterpret_cast<const uint32_t*>(params);
        for (int i = 0; i < 15; ++i)
            Record({Relocate(words[i])});
        Record({Relocate(reinterpret_cast<uint32_t>(params->up)), params->unknown40});
        for (int i = 0; params->up && i < 4; ++i)
            Record({std::bit_cast<uint32_t>(params->up[i])});
        const auto* sectors = reinterpret_cast<const uint32_t*>(r.args[0]);
        const auto* objects = reinterpret_cast<const uint32_t*>(r.args[1]);
        for (int i = 0; i < 0x20 && (sectors[i] || objects[i]); ++i)
            Record({sectors[i], Relocate(objects[i])});
        if (g_failSplat)
            return 0u;
        auto* splats = reinterpret_cast<Nx::sTextureSplats*>(Block(sizeof(Nx::sTextureSplats)));
        for (int i = 0; i < 0x100; ++i) {
            splats->alive[i] = (i * 37) % 900;
            splats->correctionSteps[i] = 1000;
        }
        return reinterpret_cast<uint32_t>(splats);
    });
    s.At({0x004B87B0, AddressOf(&Nx::LightTextureSplats)}, 0, [](Regs& r) {
        Record({0x87B0, Relocate(r.args[0])});
        return 0u;
    });
}

struct RecentSplat {
    Mth::Vector pos;
    uint32_t follow;
    uint32_t time;
};

// Splats along random lines: following (recent positions near, far, old, from the future, of another id, at the
// same x and z) or not; textures given or by name; grids with objects meeting the box or not (NaN boxes), none.
void TestTextureSplatAt()
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}, {0x0067DDE0, 8}, {0x007004B4, 4},
                       {0x00700510, 0x200}, {0x00700710, 0x200}, {0x00700F20, 4}, {0x006FA7D4, 1}});
    StandIns& s = *new StandIns();
    StandInSplatMaker(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 60; ++round) {
        ok &= CompareTwoRuns("splat maker", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0x9020 + round);
            GameVariable<uint8_t>(0x007CC979) = 1;
            Lcg random(0x9021 + round);
            g_now = 100000 + random.Below(1000);
            g_frameSeconds = 0.0166;
            g_failSplat = random.Below(5) == 0;
            HashTable* recent = NewTable();
            GameVariable<HashTable*>(0x007004B4) = recent;
            Mth::Vector from, to;
            RandomVector(random, &from, 40);
            RandomVector(random, &to, 40);
            uint32_t follow = random.Below(3) && round % 6 != 1 ? random.Pick({0x11u, 0x22u}) : 0;
            for (uint32_t i = 0, n = random.Below(5); i < n; ++i) {
                auto* record = reinterpret_cast<RecentSplat*>(Block(sizeof(RecentSplat)));
                record->pos = from;
                switch (random.Below(5)) {
                case 0:
                    record->pos.x += RandomFloat(random, 40, 90); // (far, maybe)
                    break;
                case 1:
                    record->pos.y += random.Below(2) ? 3.0f : 70.0f; // (the same x and z; near or far up)
                    break;
                case 2:
                    record->pos.x += 1.0f;
                    break;
                default:
                    record->pos.z -= RandomFloat(random, 0, 60);
                    break;
                }
                record->follow = random.Pick({0x11u, 0x22u, 0x33u});
                record->time = g_now - random.Pick({0u, 100u, 500u, 501u, 2000u}) + (random.Below(8) == 0 ? 5000u : 0u);
                Put(recent, record);
            }
            // The grids along the line and their objects.
            g_inBox.clear();
            uint32_t list = 0;
            if (random.Below(8)) {
                auto* grids = reinterpret_cast<uint32_t*>(Block(0x20));
                uint32_t count = random.Below(4);
                for (uint32_t g = 0; g < count; ++g) {
                    uint8_t* grid = Block(0x10);
                    grids[g] = reinterpret_cast<uint32_t>(grid);
                    std::vector<uint8_t*>& objects = g_inBox[grids[g]];
                    for (uint32_t o = 0, m = round % 6 == 1 ? 0 : random.Below(4); o < m; ++o) {
                        auto* object = reinterpret_cast<FakeObject*>(Block(sizeof(FakeObject)));
                        object->vtable = g_bboxVtable;
                        object->checksum = random.Next();
                        Mth::Vector centre = random.Below(3) ? from : to;
                        float reach = RandomFloat(random, 0.1f, 30);
                        object->box.min = {centre.x - reach, centre.y - reach, centre.z - reach, 1};
                        object->box.max = {centre.x + reach, centre.y + reach, centre.z + reach, 1};
                        if (random.Below(4) == 0)
                            object->box.min.x += RandomFloat(random, 60, 200); // (apart, maybe)
                        if (random.Below(10) == 0)
                            object->box.max.y = std::numeric_limits<float>::quiet_NaN();
                        if (random.Below(6) == 0) { // (below on z only)
                            float drop = 300.0f + reach;
                            object->box.min.z -= drop;
                            object->box.max.z -= drop;
                        }
                        objects.push_back(reinterpret_cast<uint8_t*>(object));
                    }
                    if (!objects.empty())
                        objects.push_back(nullptr);
                }
                grids[count] = 0;
                list = reinterpret_cast<uint32_t>(grids);
            }
            g_list = list;
            uint8_t* texture = nullptr;
            if (random.Below(2)) {
                texture = Block(0x10);
                reinterpret_cast<uint32_t*>(texture)[1] = 0x7E7E0000 + round;
            }
            bool made = Fn<bool(__cdecl*)(const Mth::Vector*, const Mth::Vector*, float, float, float, float, float,
                                          const char*, uint32_t, uint32_t, uint32_t, uint8_t*)>(original, 0x004928A0)(
                &from, &to, RandomFloat(random, 0.2f, 4), RandomFloat(random, 0, 2), RandomFloat(random, 0, 3),
                RandomFloat(random, 0, 1), random.Pick({-1.0f, 2.0f, 0.5f}), "splat_texture", random.Next(), follow,
                random.Below(2) ? 0x101u : 0u, texture);
            Record({0xEE00, made ? 1u : 0u});
            std::vector<uint32_t> trace = g_trace;
            for (HashTable::Item& item : g_items[recent])
                trace.push_back(Relocate(item.key));
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "texture splat maker: following (recent positions kept, moved, dropped) or not, the objects along the "
              "line, the splat made, aged and lit");
}

} // namespace

SELFTEST(Early, TestSpawnedPolys, RemoveStandIns);
SELFTEST(Early, TestTextureSplatAt, RemoveStandIns);
