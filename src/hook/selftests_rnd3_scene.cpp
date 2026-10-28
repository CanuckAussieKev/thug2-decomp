// Self-tests for the engine scene's members (src/game/gfx/engine_scene.cpp): original and ours on a stand-in scene in an
// arena - its vtable, sectors (a hash table built by hand), their calls, the collision data's relocation and buffers,
// Dbg::Printf and Pip stood in; the calls, results and memory compared. The collision file name (the game's sprintf)
// is checked against the expected text.

#include <windows.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/file/pip.h"
#include "game/gfx/engine_glue.h"
#include "game/gfx/engine_scene.h"
#include "game/lst/hashtable.h"
#include "game/nx/coll_grid.h"
#include "game/nx/coll_static.h"
#include "game/nx/coll_tri.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;
using Nx::CScene;

void* g_sceneVtable[11];
void* g_sectorVtable[4];
uint32_t g_newSector; // what the scene's slot 7 gives

// A sector: +4 name, +8 flags (the game's tiny getters read them), its box at +0x40 (what 0x0049E6A0 gives here).
uint8_t* NewSector(Lcg& random, uint32_t name)
{
    auto* sector = reinterpret_cast<uint8_t*>(Bump(0x60));
    std::memset(sector, 0, 0x60);
    *reinterpret_cast<void***>(sector) = g_sectorVtable;
    *reinterpret_cast<uint32_t*>(sector + 4) = name;
    *reinterpret_cast<uint32_t*>(sector + 8) = random.Next() & 0x4C;
    auto* box = reinterpret_cast<float*>(sector + 0x40);
    for (int k = 0; k < 3; ++k) {
        float a = static_cast<float>(random.Below(11)) - 5.0f, b = static_cast<float>(random.Below(11)) - 5.0f;
        box[k] = a < b ? a : b;
        box[4 + k] = a < b ? b : a;
    }
    if (random.Below(4) == 0)
        box[random.Below(3) + 4 * random.Below(2)] = std::numeric_limits<float>::quiet_NaN();
    box[3] = box[7] = 1.0f;
    return sector;
}

void StandInSceneCalls(StandIns& s)
{
    StandInMemory(s);
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        const char* format = reinterpret_cast<const char*>(r.args[0]);
        Record({0x1C30, Hash(format)});
        if (std::strchr(format, '%'))
            Record({r.args[1], r.args[2]});
        return 0x55u;
    });
    s.At({0x005B80D0, AddressOf(&Lst::HashTable::FlushItem)}, 4, [](Regs& r) {
        Record({0x80D0, Relocate(r.ecx), *reinterpret_cast<uint32_t*>(r.args[0])});
        return 0u;
    });
    StandInThis(s, {0x0049FD30, AddressOf(&Lst::HashTable::FlushAllItems)}, 0);
    StandInThis(s, {0x004183D0, AddressOf(&Nx::CCollGrid::Clear)}, 0);
    s.At({0x0049E6A0}, 0, [](Regs& r) { return r.ecx + 0x40; });
    s.At({0x0049E9B0}, 4, [](Regs& r) {
        Record({0xE9B0, Relocate(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x005B7270, AddressOf(static_cast<void (*)(const char*)>(&Pip::Unload))}, 0, [](Regs& r) {
        Record({0x7270, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0u;
    });
    s.At({0x004169C0, AddressOf(&Nx::CCollObjTriData::Relocate)}, 24, [](Regs& r) {
        Record({0x69C0, Relocate(r.ecx)});
        for (int i = 0; i < 6; ++i)
            Record({Relocate(r.args[i])});
        return 1u;
    });
    StandInThis(s, {0x00414D30, AddressOf(&Nx::CCollObjTriData::SetUpAllFaces)}, 0);
    s.At({0x00410BC0, AddressOf(&Nx::CCollStatic::TakeBuffer)}, 8, [](Regs& r) {
        Record({0x0BC0, Relocate(r.ecx), Relocate(r.args[0]), Relocate(r.args[1])});
        return r.args[0] + 8 + (r.args[1] & 0x30);
    });
    static const uint16_t kScenePops[11] = {4, 4, 0, 4, 4, 4, 0, 0, 0, 4, 4};
    for (int slot = 0; slot < 11; ++slot)
        g_sceneVtable[slot] = s.Thunk(kScenePops[slot], [slot](Regs& r) {
            Record({0xE00u + static_cast<uint32_t>(slot), Relocate(r.ecx)});
            if (kScenePops[slot])
                Record({Relocate(r.args[0])});
            return slot == 7 ? g_newSector : 0u;
        });
    for (int slot = 0; slot < 4; ++slot)
        g_sectorVtable[slot] = s.Thunk(slot == 0 ? 4 : 0, [slot](Regs& r) {
            Record({0x5E00u + static_cast<uint32_t>(slot), Relocate(r.ecx)});
            if (slot == 0)
                Record({r.args[0]});
            return r.ecx;
        });
}

// A hash table in the arena: 2^bits buckets, some chained items.
Lst::HashTable* NewTable(Lcg& random, std::vector<uint8_t*>& sectors)
{
    auto* table = reinterpret_cast<Lst::HashTable*>(Bump(sizeof(Lst::HashTable)));
    std::memset(table, 0, sizeof(Lst::HashTable));
    table->m_bits = 1 + static_cast<int>(random.Below(3));
    int buckets = 1 << table->m_bits;
    table->m_buckets = reinterpret_cast<Lst::HashTable::Item*>(Bump(sizeof(Lst::HashTable::Item) * buckets));
    std::memset(table->m_buckets, 0, sizeof(Lst::HashTable::Item) * buckets);
    int count = static_cast<int>(random.Below(9));
    std::vector<bool> used(static_cast<size_t>(buckets));
    for (int i = 0; i < count; ++i) {
        uint32_t name = random.Below(12);
        uint8_t* sector = NewSector(random, name);
        sectors.push_back(sector);
        Lst::HashTable::Item* bucket = &table->m_buckets[name & (buckets - 1)];
        if (!used[name & (buckets - 1)]) {
            used[name & (buckets - 1)] = true;
            bucket->key = name;
            bucket->value = random.Below(8) ? sector : nullptr; // (an empty slot with a key now and then)
        } else {
            auto* item = reinterpret_cast<Lst::HashTable::Item*>(Bump(sizeof(Lst::HashTable::Item)));
            item->key = name;
            item->value = sector;
            item->next = bucket->next;
            bucket->next = item;
        }
        ++table->m_size;
    }
    return table;
}

void Fill(void* at, size_t size, Lcg& random)
{
    auto* bytes = static_cast<uint8_t*>(at);
    for (size_t i = 0; i < size; ++i)
        bytes[i] = static_cast<uint8_t>(random.Next());
}

void TestSceneMembers()
{
    GameMemory& memory = *new GameMemory({{0x007CC979, 1}, {0x007D1800, 4}});
    StandIns& s = *new StandIns();
    StandInSceneCalls(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 80; ++round) {
        ok &= CompareTwoRuns("scene members", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Zero();
            GameVariable<uint8_t>(0x007CC979) = 1;
            GameVariable<uint32_t>(0x007D1800) = 0x3A9A0000;
            Lcg random(0x4C00 + round);
            std::vector<uint32_t> trace;
            auto* scene = reinterpret_cast<CScene*>(Bump(sizeof(CScene)));
            Fill(scene, sizeof(CScene), random);
            scene->m_vtable = g_sceneVtable;
            scene->m_pipName[0] = random.Below(2) ? 'p' : 0;
            scene->m_pipName[1] = 0;
            std::vector<uint8_t*> sectors;
            scene->mp_sectors = NewTable(random, sectors);
            scene->mp_collGrid = nullptr;
            scene->mp_collStatics = nullptr;
            scene->m_collStaticCount = static_cast<int>(random.Below(4));
            auto call = [&](uint32_t address) { return Code(original, address); };
            for (int step = 0; step < 16; ++step) {
                g_trace.clear();
                uint32_t which = random.Below(16);
                switch (which >= 13 ? 9 : which) {
                case 0: { // loaded / added: the names, the data
                    const char* names[] = {"levels\\a\\a", "", "x"};
                    const char* name = names[random.Below(3)];
                    if (random.Below(2))
                        reinterpret_cast<void(__thiscall*)(void*, const char*, void*)>(call(0x0049C910))(
                            scene, name, reinterpret_cast<void*>(random.Next()));
                    else
                        reinterpret_cast<void(__thiscall*)(void*, const char*, uint32_t)>(call(0x0049C940))(
                            scene, name, random.Next());
                    break;
                }
                case 1:
                    reinterpret_cast<void(__thiscall*)(void*)>(call(0x0049C9A0))(scene);
                    trace.push_back(ArenaRelocate(reinterpret_cast<uint32_t(__thiscall*)(void*, int)>(call(0x0049C9F0))(
                        scene, static_cast<int>(random.Below(5)) - 1)));
                    break;
                case 2:
                    g_newSector = reinterpret_cast<uint32_t>(NewSector(random, 99));
                    reinterpret_cast<uint32_t*>(g_newSector)[5] = random.Next();
                    reinterpret_cast<uint32_t*>(g_newSector)[6] = random.Next();
                    trace.push_back(ArenaRelocate(reinterpret_cast<uint32_t(__thiscall*)(void*)>(call(0x0049CA10))(scene)));
                    break;
                case 3:
                case 4: // looked up / deleted by name or by pointer (one not in the table too)
                    if (random.Below(2)) {
                        trace.push_back(ArenaRelocate(reinterpret_cast<uint32_t(__thiscall*)(void*, uint32_t)>(
                            call(0x0049CB00))(scene, random.Below(14))));
                    } else if (random.Below(2) || sectors.empty()) {
                        trace.push_back(reinterpret_cast<uint32_t(__thiscall*)(void*, uint32_t)>(call(0x0049CF80))(
                                            scene, random.Below(14)) & 0xFF);
                    } else {
                        uint8_t* sector = random.Below(4) ? sectors[random.Below(sectors.size())] : NewSector(random, 5);
                        trace.push_back(reinterpret_cast<uint32_t(__thiscall*)(void*, void*)>(call(0x0049CB40))(
                                            scene, sector) & 0xFF);
                    }
                    break;
                case 5: { // the collision grid made over a box (or not, without collision)
                    if (scene->mp_collGrid)
                        break;
                    scene->m_hasCollision = random.Below(4) != 0;
                    Mth::BBox box;
                    float* f = reinterpret_cast<float*>(&box);
                    for (int k = 0; k < 3; ++k) {
                        f[k] = -100.0f - static_cast<float>(random.Below(500));
                        f[4 + k] = 100.0f + static_cast<float>(random.Below(500));
                    }
                    f[3] = f[7] = 1.0f;
                    trace.push_back(reinterpret_cast<uint32_t(__thiscall*)(void*, const void*)>(call(0x0049CE60))(
                                        scene, &box) & 0xFF);
                    if (scene->mp_collGrid && random.Below(2))
                        scene->mp_collGrid->m_cells[random.Below(2) ? 19 : random.Below(20)][random.Below(2) ? 19 : random.Below(20)].unknown28 =
                            reinterpret_cast<void*>(Bump(16));
                    break;
                }
                case 6: { // the collision data: 0-3 objects, odd counts
                    int objects = static_cast<int>(random.Below(4));
                    uint32_t size = 0x20 + objects * 0x40 + 0x400;
                    auto* data = reinterpret_cast<uint8_t*>(Bump(size + 16)) + 4 * random.Below(4); // (any 4-byte alignment)
                    Fill(data, size, random);
                    auto* header = reinterpret_cast<int*>(data);
                    header[1] = objects;
                    header[2] = static_cast<int>(random.Below(40));
                    header[3] = static_cast<int>(random.Below(10));
                    header[4] = static_cast<int>(random.Below(10));
                    header[5] = static_cast<int>(random.Below(10));
                    header[6] = static_cast<int>(random.Below(10));
                    for (int i = 0; i < objects; ++i) {
                        uint8_t* geometry = data + 0x20 + i * 0x40;
                        *reinterpret_cast<uint16_t*>(geometry + 8) = static_cast<uint16_t>(random.Below(3));
                        for (int k = 0; k < 8; ++k)
                            reinterpret_cast<float*>(geometry + 0x10)[k] = static_cast<float>(random.Below(400)) - 200.0f;
                    }
                    // the faces' size word the nodes follow (inside the blob)
                    uintptr_t at = reinterpret_cast<uintptr_t>(data) + 0x20 + objects * 0x40;
                    at = (at + 15) & ~uintptr_t(15);
                    at += 6 * (header[6] + 2 * header[5]);
                    at = (at + header[2] + 3) & ~uintptr_t(3);
                    at += (11 * header[3] + 8 * header[4] + 3) & ~3;
                    *reinterpret_cast<uint32_t*>(at) = random.Below(64);
                    int count = -1;
                    Nx::CCollStatic* statics = nullptr;
                    uint8_t* geometries = nullptr;
                    Mth::BBox box;
                    trace.push_back(reinterpret_cast<uint32_t(__thiscall*)(void*, const void*, int*, void*, void*, void*,
                                                                           int)>(call(0x0049CBB0))(
                                        scene, data, &count, &statics, &geometries, &box, 7) & 0xFF);
                    trace.push_back(static_cast<uint32_t>(count));
                    trace.push_back(ArenaRelocate(reinterpret_cast<uint32_t>(statics)));
                    trace.push_back(ArenaRelocate(reinterpret_cast<uint32_t>(geometries)));
                    for (int k = 0; k < 8; ++k)
                        trace.push_back(std::bit_cast<uint32_t>(reinterpret_cast<float*>(&box)[k]));
                    if (statics && !scene->mp_collStatics && random.Below(2)) {
                        scene->mp_collStatics = statics;
                        scene->m_collStaticCount = count;
                    }
                    break;
                }
                case 7: // the collision let go of
                    if (scene->mp_collStatics && random.Below(3) == 0)
                        reinterpret_cast<int*>(scene->mp_collStatics)[-1] = 0; // (an empty array)
                    trace.push_back(reinterpret_cast<uint32_t(__thiscall*)(void*)>(call(0x0049CFB0))(scene) & 0xFF);
                    break;
                case 8:
                    reinterpret_cast<void(__thiscall*)(void*)>(call(0x0049D030))(scene);
                    break;
                case 9: { // the sectors in a box: on or off
                    Mth::Vector box[2];
                    for (int k = 0; k < 3; ++k) {
                        float a = static_cast<float>(random.Below(13)) - 6.0f;
                        float b = static_cast<float>(random.Below(13)) - 6.0f;
                        box[0][k] = a < b ? a : b;
                        box[1][k] = a < b ? b : a;
                    }
                    if (random.Below(3) == 0)
                        box[random.Below(2)][random.Below(3)] = std::numeric_limits<float>::quiet_NaN();
                    reinterpret_cast<void(__thiscall*)(void*, const void*, uint32_t, int)>(call(0x0049D0B0))(
                        scene, box, random.Next(), 3);
                    break;
                }
                case 10: { // the stubs
                    uint8_t colour[4] = {1, 2, 3, 4};
                    reinterpret_cast<void(__thiscall*)(void*, void*)>(call(0x0049CA50))(scene, nullptr);
                    reinterpret_cast<void(__thiscall*)(void*, int)>(call(0x0049CA60))(scene, 1);
                    reinterpret_cast<void(__thiscall*)(void*)>(call(0x0049CA70))(scene);
                    for (uint32_t address : {0x0049CA80u, 0x0049CAA0u, 0x0049CAC0u})
                        trace.push_back(reinterpret_cast<uint32_t(__thiscall*)(void*, const char*)>(call(address))(
                                            scene, "n") & 0xFF);
                    trace.push_back(reinterpret_cast<uint32_t(__thiscall*)(void*)>(call(0x0049CAE0))(scene) & 0xFF);
                    trace.push_back(reinterpret_cast<uint32_t(__thiscall*)(void*)>(call(0x0049CAF0))(scene));
                    trace.push_back(OnStack(reinterpret_cast<uint32_t(__thiscall*)(void*, void*)>(call(0x0049CF60))(
                        scene, colour)));
                    uint32_t c;
                    std::memcpy(&c, colour, 4);
                    trace.push_back(c);
                    break;
                }
                case 11: { // the grid's body and the static objects' deleting destructor, alone
                    auto* grid = reinterpret_cast<Nx::CCollGrid*>(Bump(sizeof(Nx::CCollGrid)));
                    std::memset(grid, 0, sizeof(Nx::CCollGrid));
                    grid->m_cells[0][random.Below(20)].unknown28 = reinterpret_cast<void*>(Bump(16));
                    grid->m_cells[19][random.Below(20)].unknown28 = reinterpret_cast<void*>(Bump(16));
                    reinterpret_cast<void(__thiscall*)(void*)>(call(0x0049C980))(grid);
                    int n = static_cast<int>(random.Below(4));
                    auto* block = reinterpret_cast<int*>(Bump(4 + 0x18 * 4));
                    *block = n;
                    for (int i = 0; i < 4; ++i) {
                        auto* object = reinterpret_cast<Nx::CCollStatic*>(reinterpret_cast<uint8_t*>(block + 1) + i * 0x18);
                        object->Construct();
                    }
                    unsigned int flags = random.Below(4);
                    trace.push_back(ArenaRelocate(reinterpret_cast<uint32_t(__thiscall*)(void*, unsigned int)>(
                        call(0x0049CE00))(block + 1, flags)));
                    break;
                }
                default:
                    break;
                }
                trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            }
            DumpArena(trace);
            memory.Dump(trace);
            return trace;
        });
    }
    // the collision file name: ours against the expected text (the original uses the game's sprintf)
    s.At({0x0048C0C0, AddressOf(&Nx::PlatformExtension)}, 0,
         [](Regs&) { return reinterpret_cast<uint32_t>("xbx"); });
    char out[128];
    int length = Nx::MakeCollisionFileName(out, "skater");
    Check(std::string(out) == "levels\\skater\\skater.col.xbx" && length == 28, "scene: the collision file name");
    delete &s;
    delete &memory;
    g_relocate = nullptr;
    Check(ok, "scene: names, sectors (looked up, deleted, in a box, marked), collision data, grid, unloading, stubs");
}

} // namespace

SELFTEST(Early, TestSceneMembers, RemoveStandIns);
