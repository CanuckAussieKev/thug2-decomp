// Self-tests for the PC scene, sector and screen effects (src/game/gfx/pc_scene.cpp, RFX-13): the original and ours
// over stand-in scenes (a hand-built sector table), a recording stand-in device and vertex buffer, the engine's
// constructors / destructors, the geoms' and mesh list's code stood in.

#include <windows.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/ass/asset.h"
#include "game/core/manager.h"
#include "game/file/file.h"
#include "game/gfx/geom.h"
#include "game/gfx/mesh.h"
#include "game/gfx/mesh_list.h"
#include "game/gfx/pc_scene.h"
#include "game/lst/hashtable.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;

constexpr uint32_t kDevice = 0xD3D00000;
constexpr uint32_t kBuffer = 0xB0F00000;

Lst::HashTable* MakeSectorTable(int count)
{
    auto* table = reinterpret_cast<Lst::HashTable*>(Bump(sizeof(Lst::HashTable)));
    std::memset(table, 0, sizeof(Lst::HashTable));
    table->m_bits = 4;
    table->m_buckets = reinterpret_cast<Lst::HashTable::Item*>(Bump(16 * sizeof(Lst::HashTable::Item)));
    std::memset(table->m_buckets, 0, 16 * sizeof(Lst::HashTable::Item));
    for (int i = 0; i < count; ++i) {
        auto* sector = reinterpret_cast<uint8_t*>(Bump(0x40));
        *reinterpret_cast<uint8_t**>(sector + 0x14) = reinterpret_cast<uint8_t*>(Bump(0x64)); // (its geom)
        table->m_buckets[(i * 5) & 15] = {static_cast<uint32_t>(0x5EC0 + i), sector, nullptr};
        ++table->m_size;
    }
    return table;
}

void TestPcScene()
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}, {0x007D69E0, 4}, {0x00705720, 0x30}, {0x0067E97C, 0x1C}});
    StandIns& s = *new StandIns();
    FakeComSet com(&s);
    FakeCom* device = com.Device(kDevice);
    FakeCom* buffer = com.Buffer(kBuffer);
    StandInMemory(s);
    com.On(kDevice, kCreateVertexBuffer, [buffer](Regs& r) {
        *reinterpret_cast<FakeCom**>(r.args[5]) = buffer;
        return 0u;
    });
    com.On(kBuffer, 11, [](Regs& r) { // Lock
        *reinterpret_cast<uint32_t*>(r.args[3]) = 0xDA7A0000;
        return 0u;
    });
    for (uint32_t engine : {0x0049D390u, 0x0049E900u, 0x0049E2F0u}) {
        uint16_t args = engine == 0x0049D390 ? 4 : 0;
        s.At({engine}, args, [engine, args](Regs& r) {
            // (the stack word past a function that takes none is the caller's, not an argument: not recorded)
            Record({engine, Relocate(r.ecx), args ? r.args[0] : 0u});
            return r.ecx;
        });
    }
    StandInThis(s, {0x0049DD10}, 0);
    StandInThis(s, {0x0049E630}, 0);
    StandInThis(s, {0x0049E300}, 0);
    s.At({0x00490DC0}, 0, [](Regs& r) {
        Record({0x0DC0, Relocate(r.args[0])});
        return 0u;
    });
    s.At({0x004A2BA0}, 0, [](Regs& r) { return *reinterpret_cast<uint32_t*>(r.ecx + 0x14); });
    StandInThis(s, {0x004B5360, AddressOf(&Nx::CPcGeom::CreateMeshArray)}, 1);
    StandInThis(s, {0x004B44D0, AddressOf(&Nx::CPcGeom::AddToMeshList)}, 1);
    StandInThis(s, {0x004DE3E0, AddressOf(&Nx::sScene::AllocateMeshList)}, 0);
    StandInThis(s, {0x004DDFA0, AddressOf(&Nx::sScene::SortMeshes)}, 0);
    UseStandardRelocation(com);
    auto standard = g_relocate;
    g_relocate = [standard](uint32_t word) { return ArenaRelocate(standard(word)); };
    bool ok = true, reported = false;
    for (int round = 0; round < 8; ++round) {
        ok &= CompareTwoRuns("pc scene", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0x5CE0 + round);
            GameVariable<uint8_t>(0x007CC979) = 1;
            GameVariable<FakeCom*>(0x007D69E0) = device;
            std::vector<uint32_t> trace;
            auto* scene = reinterpret_cast<uint8_t*>(Bump(0x284));
            trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, int)>(original, 0x004C6130)(scene, 10 + round)));
            *reinterpret_cast<Lst::HashTable**>(scene + 0xD0) = MakeSectorTable(round == 0 ? 0 : round * 2);
            auto* list = reinterpret_cast<Nx::sScene*>(Bump(sizeof(Nx::sScene)));
            std::memset(list, 0, sizeof(Nx::sScene));
            *reinterpret_cast<Nx::sScene**>(scene + 0x280) = list;
            Fn<void(__thiscall*)(void*)>(original, 0x004C6200)(scene);
            Nx::sVertexArena arena = {nullptr, 77, 0x4000u + round, nullptr};
            Fn<void(__thiscall*)(void*, void*)>(original, 0x004C6270)(scene, round & 1 ? &arena : nullptr);
            trace.push_back(ArenaRelocate(reinterpret_cast<uint32_t>(arena.buffer)));
            trace.push_back(arena.used);
            trace.push_back(reinterpret_cast<uint32_t>(arena.data));
            if (round == 3)
                g_failAllocations = {true};
            uint32_t sector = Fn<uint32_t(__thiscall*)(void*)>(original, 0x004C6190)(scene);
            trace.push_back(ArenaRelocate(sector));
            if (sector) {
                *reinterpret_cast<uint32_t*>(sector + 0x14) = round & 2 ? 0x6E0E0000 : 0;
                Fn<void(__thiscall*)(void*, bool)>(original, 0x004C6530)(reinterpret_cast<void*>(sector), round & 4);
                trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, int, int)>(original, 0x004C65C0)(
                    reinterpret_cast<void*>(sector), 1, 2)));
                trace.push_back(Fn<uint32_t(__thiscall*)(void*)>(original, 0x004C64E0)(reinterpret_cast<void*>(sector)));
                trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, unsigned int)>(original, 0x004C6580)(
                    reinterpret_cast<void*>(sector), round & 1)));
            }
            trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, unsigned int)>(original, 0x004C6150)(scene, round & 2)));
            // the screen effects
            auto* effects = reinterpret_cast<uint8_t*>(Bump(8));
            Fn<void*(__thiscall*)(void*)>(original, 0x004C6480)(effects);
            Fn<void(__thiscall*)(void*, int)>(original, 0x004C63C0)(effects, round & 1 ? 7 : 0);
            Fn<void(__thiscall*)(void*, uint32_t)>(original, 0x004C63E0)(effects, 0x11223344 + round);
            Fn<void(__thiscall*)(void*, uint32_t, uint32_t)>(original, 0x004C63F0)(effects, 0x55 + round, 0x66 + round);
            Fn<void(__thiscall*)(void*, uint32_t)>(original, 0x004C6410)(effects, 0x990 + round);
            Fn<void(__thiscall*)(void*, int)>(original, 0x004C6420)(effects, round & 2 ? -3 : 0);
            Fn<void(__thiscall*)(void*, uint32_t)>(original, 0x004C6460)(effects, 0x994 + round);
            Fn<void(__thiscall*)(void*, uint32_t)>(original, 0x004C6470)(effects, 0x984 + round);
            Fn<void(__thiscall*)(void*, uint32_t)>(original, 0x004C6440)(effects, 0x988 + round);
            Fn<void(__thiscall*)(void*, uint32_t)>(original, 0x004C6450)(effects, 0x5728 + round);
            trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, unsigned int)>(original, 0x004C64B0)(effects, round & 1)));
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "pc scene: made, its sectors, post-load packing, sectors made / cloned / destroyed, screen effects");
}

float RandomFloatScene(Lcg& random, float low, float high)
{
    return low + (high - low) * static_cast<float>(random.Below(100000)) / 99999.0f;
}

// The sector loaders over a made-up sector record (in memory, and the same read through a stand-in file), the mesh
// code they call stood in.
std::vector<uint8_t> g_file;
bool g_fileMode;
size_t g_fileAt;

void Put(std::vector<uint8_t>& out, uint32_t value)
{
    out.insert(out.end(), reinterpret_cast<uint8_t*>(&value), reinterpret_cast<uint8_t*>(&value) + 4);
}
void PutFloat(std::vector<uint8_t>& out, float value) { Put(out, std::bit_cast<uint32_t>(value)); }

void TestPcSectorLoad()
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}});
    StandIns& s = *new StandIns();
    StandInMemory(s);
    s.At({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, [](Regs& r) {
        Record({0xB0E0, Relocate(r.args[0])});
        return 0u;
    });
    s.At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [](Regs&) {
        Record({0xB110});
        return 0u;
    });
    s.At({0x00407700, AddressOf(&Ass::CAsset::SetChecksum)}, 4, [](Regs& r) {
        Record({0x7700, Relocate(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x004D4BE0}, 0, [](Regs& r) {
        Record({0x4BE0, Relocate(r.ecx)});
        std::memset(reinterpret_cast<void*>(r.ecx), 0, 0xD8);
        return r.ecx;
    });
    s.At({0x004D5CC0}, 16, [](Regs& r) { // the mesh's vertices from memory: 8 bytes taken
        auto** cursor = reinterpret_cast<uint8_t**>(r.args[0]);
        uint32_t v[2];
        std::memcpy(v, *cursor, 8);
        Record({0x5CC0, Relocate(r.ecx), Relocate(r.args[0]), r.args[1], Relocate(r.args[2]), r.args[3], v[0], v[1]});
        *cursor += 8;
        return 1u;
    });
    s.At({0x005B9EE0, AddressOf(&File::Read)}, 0, [](Regs& r) {
        uint32_t bytes = r.args[1] * r.args[2];
        Record({0x9EE0, r.args[1], r.args[2], Relocate(r.args[3])});
        std::memcpy(reinterpret_cast<void*>(r.args[0]), g_file.data() + g_fileAt, bytes);
        g_fileAt += bytes;
        return r.args[2];
    });
    s.At({0x004D6100}, 20, [](Regs& r) { // and from the file
        uint32_t v[2];
        std::memcpy(v, g_file.data() + g_fileAt, 8);
        g_fileAt += 8;
        // (the last a byte: the original passes a masked word, ours a bool)
        Record({0x6100, Relocate(r.ecx), Relocate(r.args[0]), r.args[1], Relocate(r.args[2]), r.args[3], r.args[4] & 0xFF,
                v[0], v[1]});
        return 1u;
    });
    s.At({0x004D5900}, 16, [](Regs& r) {
        const auto* c = reinterpret_cast<const uint32_t*>(r.args[0]);
        const auto* lo = reinterpret_cast<const uint32_t*>(r.args[2]);
        const auto* hi = reinterpret_cast<const uint32_t*>(r.args[3]);
        Record({0x5900, Relocate(r.ecx), c[0], c[1], c[2], r.args[1], lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]});
        return 0u;
    });
    s.At({0x004D7580, AddressOf(&Nx::sMesh::MakeBillboard)}, 12, [](Regs& r) {
        const auto* pivot = reinterpret_cast<const uint32_t*>(r.args[1]);
        const auto* axis = reinterpret_cast<const uint32_t*>(r.args[2]);
        Record({0x7580, Relocate(r.ecx), r.args[0], pivot[0], pivot[1], pivot[2], axis[0], axis[1], axis[2]});
        if (g_fileMode) // (the file loader's w are 0; the memory one's are what its stack held)
            Record({pivot[3], axis[3]});
        return 0u;
    });
    s.At({0x004CEBE0}, 4, [](Regs& r) {
        Record({0xEBE0, r.ecx, Relocate(r.args[0])});
        return 0u;
    });
    s.At({0x004B52F0, AddressOf(&Nx::CPcGeom::AddMesh)}, 4, [](Regs& r) {
        Record({0x52F0, Relocate(r.ecx), Relocate(r.args[0])});
        return 0u;
    });
    s.At({0x004D0D30}, 0, [](Regs& r) {
        Record({0x0D30, Relocate(r.args[0]), Relocate(r.args[1])});
        return 0u;
    });
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 16; ++round) {
        // the record
        Lcg random(0x5EC0 + round);
        std::vector<uint8_t> blob;
        uint32_t flags = (random.Next() & 0x5FFFFF) | (round & 1 ? 0x800000u : 0u) | (round & 2 ? 0x200000u : 0u) |
                         (round & 4 ? 0x80000000u : 0u);
        uint32_t meshes = round == 4 ? 0 : 1 + random.Below(3);
        Put(blob, 0x5EC70000 + round);
        Put(blob, random.Next());
        Put(blob, flags);
        Put(blob, meshes);
        for (int k = 0; k < 10; ++k)
            PutFloat(blob, RandomFloatScene(random, -100, 100));
        if (flags & 0x800000) {
            Put(blob, 1 + random.Below(2));
            for (int k = 0; k < 9; ++k)
                PutFloat(blob, RandomFloatScene(random, -5, 5));
        }
        for (uint32_t m = 0; m < meshes; ++m) {
            for (int k = 0; k < 10; ++k)
                PutFloat(blob, RandomFloatScene(random, -50, 50));
            Put(blob, (random.Next() & 0xFFFF) | (random.Below(2) ? 0x400u : 0u) | (random.Below(2) ? 0x20000u : 0u));
            Put(blob, random.Next());
            uint32_t lists = random.Below(4);
            Put(blob, lists);
            for (uint32_t j = 0; j < lists; ++j) {
                uint32_t count = random.Below(6);
                Put(blob, count);
                for (uint32_t k = 0; k < count; ++k)
                    blob.push_back(static_cast<uint8_t>(random.Next())), blob.push_back(static_cast<uint8_t>(random.Next()));
            }
            Put(blob, random.Next());
            Put(blob, random.Next());
        }
        for (bool fromFile : {false, true}) {
            ok &= CompareTwoRuns(fromFile ? "sector load (file)" : "sector load (memory)", reported, [&](bool original) {
                g_trace.clear();
                ResetArena(0xEE);
                memory.Fill(0x5EC0 + round);
                GameVariable<uint8_t>(0x007CC979) = 1;
                std::vector<uint32_t> trace;
                auto* manager = reinterpret_cast<Mem::Manager*>(Bump(sizeof(Mem::Manager)));
                std::memset(manager, 0, sizeof(Mem::Manager));
                manager->m_topDownHeap = reinterpret_cast<Mem::Heap*>(Bump(16));
                GameVariable<Mem::Manager*>(0x007D1800) = manager;
                // the sector, its geom (a load list of a few), the scene's mesh list and its box
                auto* sector = reinterpret_cast<Nx::CPcSector*>(Bump(sizeof(Nx::CPcSector)));
                auto* geom = reinterpret_cast<Nx::CPcGeom*>(Bump(sizeof(Nx::CPcGeom)));
                std::memset(geom, 0, sizeof(Nx::CPcGeom));
                sector->geom = geom;
                sector->flags = 0;
                auto* scene = reinterpret_cast<uint8_t*>(Bump(0x284));
                auto* list = reinterpret_cast<uint8_t*>(Bump(0x80));
                auto* box = reinterpret_cast<float*>(list + 0x44);
                for (int k = 0; k < 8; ++k)
                    box[k] = k < 4 ? 1000.0f : -1000.0f;
                *reinterpret_cast<uint8_t**>(scene + 0x280) = list;
                geom->scene = scene;
                int nodes = static_cast<int>(round % 4);
                auto* head = reinterpret_cast<Mem::ListNode*>(Bump(sizeof(Mem::ListNode) * (nodes + 1)));
                for (int k = 0; k <= nodes; ++k) {
                    head[k].data = k == 0 ? reinterpret_cast<void*>(-1) : reinterpret_cast<void*>(Bump(4));
                    head[k].next = &head[(k + 1) % (nodes + 1)];
                }
                geom->loadList = head;
                g_fileMode = fromFile;
                if (fromFile) {
                    g_file = blob;
                    g_fileAt = 0;
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*, void*, uint32_t)>(original, 0x004C6B10)(
                                        sector, reinterpret_cast<void*>(0xF11E0000), 0xAB00 + round) & 0xFF);
                    trace.push_back(static_cast<uint32_t>(g_fileAt));
                } else {
                    auto* data = reinterpret_cast<uint8_t*>(Bump(static_cast<uint32_t>(blob.size()) + 16));
                    std::memcpy(data, blob.data(), blob.size());
                    uint8_t* cursor = data;
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*, uint8_t**)>(original, 0x004C6650)(sector, &cursor) & 0xFF);
                    trace.push_back(static_cast<uint32_t>(cursor - data));
                }
                trace.push_back(sector->flags);
                trace.push_back(geom->meshCount);
                trace.insert(trace.end(), g_trace.begin(), g_trace.end());
                memory.Dump(trace);
                DumpArena(trace);
                return trace;
            });
        }
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "pc sectors: loaded from memory and from a file (billboards, index lists, the meshes counted)");
}

} // namespace

SELFTEST(Early, TestPcScene, RemoveStandIns);
SELFTEST(Early, TestPcSectorLoad, RemoveStandIns);
