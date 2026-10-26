// Self-tests for the PC scene and mesh loaders (src/game/gfx/scene_load.cpp, RFX-2): the original and ours with
// stand-ins for the scene, sector, geom, mesh and material code they call, the file functions over a stand-in
// stream and allocations from an arena (failing on request); traces and the arena compared.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "game/core/crc.h"
#include "game/core/pool.h"
#include "game/file/file.h"
#include "game/gfx/mesh_list.h"
#include "game/gfx/pc_mesh.h"
#include "game/gfx/scene_load.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;

// The geometry: a 12-byte header, 8 bytes of materials, the sectors (each a word: bit 0 whether it loads, bits
// 2-3 how many words more it takes), the hierarchy's count and its nodes.
std::vector<uint32_t> MakeGeometry(Lcg& random, int sectors, int nodes)
{
    std::vector<uint32_t> words = {0x57, 0x58, 0x59, 0x4A7E0001, 0x4A7E0002, static_cast<uint32_t>(sectors)};
    for (int i = 0; i < sectors; ++i) {
        uint32_t w = random.Next() & 0xFFFF;
        words.push_back(w);
        for (uint32_t k = 0; k < ((w >> 2) & 3); ++k)
            words.push_back(random.Next());
    }
    words.push_back(static_cast<uint32_t>(nodes));
    for (int i = 0; i < nodes * 0x14; ++i)
        words.push_back(random.Next());
    return words;
}

const uint8_t* g_stream; // the stand-in file's bytes and where it is
size_t g_streamAt, g_streamSize;
uint32_t g_nextSector;

void StandInLoaders(StandIns& s)
{
    StandInMemory(s);
    StandInThis(s, {0x004DE300, AddressOf(&Nx::sScene::Construct)}, 0);
    StandInThis(s, {0x004DE340, AddressOf(&Nx::sScene::UpdateSphere)}, 0);
    StandInThis(s, {0x004C6130}, 1);
    s.At({0x0049CA10}, 0, [](Regs& r) { // the scene's new sector
        Record({0xCA10, Relocate(r.ecx)});
        return 0x5EC70000u + g_nextSector++;
    });
    StandInThis(s, {0x0049E660}, 1);
    StandInThis(s, {0x004B4460}, 0);
    StandInThis(s, {0x004B5F60}, 0);
    StandInThis(s, {0x0049D4E0}, 1);
    StandInThis(s, {0x0049C910}, 2);
    StandInCdecl(s, {0x004D3CE0}, 0);
    StandInThis(s, {0x004B7D50, AddressOf(&Nx::CPcMesh::ConstructFromFile)}, 1);
    StandInThis(s, {0x004B7B30, AddressOf(&Nx::CPcMesh::Construct)}, 0);
    StandInThis(s, {0x004B7EB0, AddressOf(&Nx::CPcMesh::LoadSkin)}, 1);
    StandInThis(s, {0x004B7ED0, AddressOf(&Nx::CPcMesh::SetScene)}, 1);
    StandInThis(s, {0x0048CF70}, 0);
    // the materials: from memory (the cursor moved past them) and from the file (its bytes read)
    s.At({0x004D2CB0}, 0, [](Regs& r) {
        auto** p = reinterpret_cast<const uint8_t**>(r.args[0]);
        Record({0x2CB0, Relocate(r.args[1]), *reinterpret_cast<const uint32_t*>(*p)});
        *p += 8;
        return 0x3A7E0000u;
    });
    s.At({0x004D3390}, 0, [](Regs& r) {
        Record({0x3390, r.args[0], Relocate(r.args[1])});
        g_streamAt += 8;
        return 0x3A7E1000u;
    });
    // a sector's load: from memory and from the file
    s.At({0x004C6650}, 4, [](Regs& r) {
        auto** p = reinterpret_cast<const uint8_t**>(r.args[0]);
        uint32_t w = *reinterpret_cast<const uint32_t*>(*p);
        Record({0x6650, r.ecx, w});
        *p += 4 + 4 * ((w >> 2) & 3);
        return w & 1;
    });
    s.At({0x004C6B10}, 8, [](Regs& r) {
        uint32_t w;
        std::memcpy(&w, g_stream + g_streamAt, 4);
        Record({0x6B10, r.ecx, r.args[0], r.args[1], w});
        g_streamAt += 4 + 4 * ((w >> 2) & 3);
        return (w & 1) | 0x100;
    });
    // the files
    s.At({0x005B9E60, AddressOf(&File::Open)}, 0, [](Regs& r) {
        Record({0x9E60, Hash(reinterpret_cast<const char*>(r.args[0])), Hash(reinterpret_cast<const char*>(r.args[1]))});
        g_streamAt = 0;
        return g_stream ? 0xF11E0000u : 0u;
    });
    s.At({0x005B9EE0, AddressOf(&File::Read)}, 0, [](Regs& r) {
        uint32_t bytes = r.args[1] * r.args[2];
        Record({0x9EE0, Relocate(r.args[0]), r.args[1], r.args[2], r.args[3]});
        if (r.args[0] && g_streamAt + bytes <= g_streamSize)
            std::memcpy(reinterpret_cast<void*>(r.args[0]), g_stream + g_streamAt, bytes);
        g_streamAt += bytes;
        return bytes;
    });
    s.At({0x005B9EA0, AddressOf(&File::Close)}, 0, [](Regs& r) {
        Record({0x9EA0, r.args[0]});
        return 0u;
    });
    s.At({0x00472E60, AddressOf(&Crc::GenerateCRCFromString)}, 0, [](Regs& r) {
        Record({0x2E60, Hash(reinterpret_cast<const char*>(r.args[0]))});        return 0xC0C00000u ^ Hash(reinterpret_cast<const char*>(r.args[0]));
    });
    s.At({0x006234FE}, 0, StandInSprintf); // (the game's C runtime, for the original)
    s.At({0x005BA3D0, AddressOf(&Mem::Pool::Allocate)}, 0, [](Regs& r) {
        Record({0xA3D0, Relocate(r.ecx)});
        return Bump(0x214);
    });
}

// Hierarchy nodes: each constructed over dirty memory.
void TestSceneHierarchyNode()
{
    alignas(16) static uint8_t nodes[2][0x50];
    for (int run = 0; run < 2; ++run) {
        std::memset(nodes[run], 0xA5, sizeof(nodes[run]));
        void* result = Fn<void*(__thiscall*)(void*)>(run == 0, 0x004B2D30)(nodes[run]);
        Check(result == nodes[run], "scene load: the hierarchy node's constructor returns this");
    }
    Check(std::memcmp(nodes[0], nodes[1], sizeof(nodes[0])) == 0, "scene load: the hierarchy node's constructor");
}

// The loaders: from memory and from files (none there, no sectors / nodes, several, a failing hierarchy
// allocation), the mesh makers with and without skin data, the frame source from a pool or the heap.
void TestSceneLoaders()
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}, {0x006FA7F4, 0x10}});
    StandIns& s = *new StandIns();
    StandInLoaders(s);
    g_relocate = ArenaRelocate;
    static uint8_t textures[0x20];
    bool ok = true, reported = false;
    for (int round = 0; round < 11; ++round) {
        Lcg seeds(0x5CE0 + round);
        // (2100 sectors: past the hash table's 12-bit cap)
        int sectors = round == 0 ? 0 : round == 10 ? 2100 : static_cast<int>(seeds.Below(round < 5 ? 4 : 40));
        int nodes = round == 1 ? 0 : static_cast<int>(seeds.Below(6));
        Lcg random(0x10AD + round);
        std::vector<uint32_t> geometry = MakeGeometry(random, sectors, nodes);
        ok &= CompareTwoRuns("scene load: loaders", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            g_nextSector = 0;
            memory.Fill(0x77 + round);
            GameVariable<uint8_t>(0x007CC979) = 1;
            GameVariable<uint32_t>(0x006FA7F4) = round & 1;
            GameVariable<uint32_t>(0x006FA7FC + 4 * (round & 1)) = (round & 2) ? 0x9001u : 0u;
            for (int i = 0; i < 0x20; ++i)
                textures[i] = static_cast<uint8_t>(0x40 + i);
            std::vector<uint32_t> trace;
            int flags = round * 0x101;
            // from memory (a failed hierarchy allocation copies to null: not tested)
            void* scene = Fn<void*(__cdecl*)(const void*, void*, int, int, int)>(original, 0x004B3490)(
                geometry.data(), textures, flags & 0x101, (flags >> 1) & 0x101, (flags >> 2) & 0x101);
            trace.push_back(ArenaRelocate(reinterpret_cast<uint32_t>(scene)));
            // from a file (round 3: none)
            g_stream = round == 3 ? nullptr : reinterpret_cast<const uint8_t*>(geometry.data());
            g_streamSize = geometry.size() * 4;
            if (round == 8) { // the hierarchy's: after the list, the scene and the sectors' geoms
                g_failAllocations.assign(2 + sectors, false);
                g_failAllocations.push_back(true);
            }
            scene = Fn<void*(__cdecl*)(const char*, void*, int, int, int, int)>(original, 0x004B36D0)(
                "levels\\x\\x.scn", textures, flags & 0x101, (flags >> 3) & 0x101, (flags >> 1) & 0x101, 0x5A + round);
            trace.push_back(ArenaRelocate(reinterpret_cast<uint32_t>(scene)));
            if (round != 3) {
                g_stream = reinterpret_cast<const uint8_t*>(geometry.data());
                void* mesh = Fn<void*(__cdecl*)(const char*, void*)>(original, 0x004B3A20)("models\\m.mdl", textures);
                trace.push_back(ArenaRelocate(reinterpret_cast<uint32_t>(mesh)));
            }
            void* mesh = Fn<void*(__cdecl*)(uint32_t, const void*, int, void*, void*)>(original, 0x004B3AD0)(
                round == 4 ? 0x80000001u : 12345u * round, geometry.data(), 0x77, round & 1 ? textures + 4 : nullptr,
                textures);
            trace.push_back(ArenaRelocate(reinterpret_cast<uint32_t>(mesh)));
            g_failAllocations = {round == 5};
            trace.push_back(ArenaRelocate(Fn<uint32_t(__cdecl*)()>(original, 0x004B3990)()));
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    g_failAllocations.clear();
    g_stream = nullptr;
    delete &s;
    g_relocate = nullptr;
    Check(ok, "scene load: scenes from memory and files, meshes, frame sources");
}

} // namespace

SELFTEST(Early, TestSceneHierarchyNode);
SELFTEST(Early, TestSceneLoaders, RemoveStandIns);
