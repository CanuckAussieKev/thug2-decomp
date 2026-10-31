// Self-tests for a scene's mesh list and materials (appmain batch 4, src/game/gfx/mesh_list.cpp): the original
// and ours on identical stand-in scenes, meshes, materials and index buffers (the buffers, allocation and the
// meshes' vertex buffers stood in for); results, calls and memory compared.

#include <windows.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/core/manager.h"
#include "game/gfx/mesh_list.h"
#include "game/lst/hashtable.h"
#include "game/gfx/mesh.h"
#include "hook/appmain_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace AppMainTests;

template <typename F>
F Fn(bool original, uint32_t address)
{
    return reinterpret_cast<F>(Code(original, address));
}

// Everything the test builds, in one block (pointers into it made relative for the comparison).
struct World {
    alignas(16) uint8_t scene[0x100];
    uint8_t meshes[24][0x100];
    uint8_t materials[8][0x40];
    uint8_t* list[24];
    uint16_t buffers[40][200]; // index buffers' contents
    uint8_t bufferObjects[40][0x18];
    uint8_t heap[0x400];
    alignas(4) uint8_t tableBytes[sizeof(Lst::HashTable)]; // (the class has a constructor: raw storage)
    Lst::HashTable& Table() { return *reinterpret_cast<Lst::HashTable*>(tableBytes); }
    Lst::HashTable::Item items[4];
    uint32_t records[12][4];
    uint8_t extra[4 + 3 * 0x50];
};
alignas(16) World g_world;
int g_nextBuffer;

uint32_t WorldRelocate(uint32_t word)
{
    auto base = reinterpret_cast<uint32_t>(&g_world);
    if (word >= base && word < base + sizeof(g_world))
        return 0xB0000000 + (word - base);
    return OnStack(word) ? 0x57AC0000u : word;
}

void StandInBuffers(StandIns& s)
{
    s.At({0x004D42F0}, 16, [](Regs& r) { // Lock(offset, size, &data, flags): the buffer object's +0 is its contents
        Record({0x42F0, WorldRelocate(r.ecx), r.args[0], r.args[1], r.args[3]});
        *reinterpret_cast<uint32_t*>(r.args[2]) = *reinterpret_cast<uint32_t*>(r.ecx);
        return 0u;
    });
    s.At({0x004D4300}, 0, [](Regs& r) { Record({0x4300, WorldRelocate(r.ecx)}); return 0u; });
    s.At({0x004D6790}, 0, [](Regs& r) { Record({0x6790, WorldRelocate(r.ecx)}); return 0u; });
    // (the renderer's operator new is a jump to the C runtime's malloc 0x00624031: the original goes through the first,
    // ours - Crt::Malloc - to the second)
    for (uint32_t address : {0x004D3BC0u, 0x00624031u}) {
        s.At({address}, 0, [](Regs& r) {
            Record({0x3BC0, r.args[0]});
            return reinterpret_cast<uint32_t>(g_world.bufferObjects[g_nextBuffer % 40]);
        });
    }
    s.At({0x004D4270, AddressOf(&Nx::sIndexBuffer::Construct)}, 12, [](Regs& r) {
        Record({0x4270, WorldRelocate(r.ecx), r.args[0], r.args[1], r.args[2]});
        int index = g_nextBuffer++ % 40;
        std::memset(g_world.buffers[index], 0xEE, sizeof(g_world.buffers[index]));
        *reinterpret_cast<uint16_t**>(r.ecx) = g_world.buffers[index];
        return r.ecx;
    });
    s.At({0x004D3FC0}, 4, [](Regs& r) {
        Record({0x3FC0, WorldRelocate(r.ecx), r.args[0]});
        return (r.ecx >> 8) & 1 ? 0u : 0x7B000000u + (r.ecx & 0xFFF);
    });
    s.At({0x004D3FD0}, 8, [](Regs& r) { Record({0x3FD0, WorldRelocate(r.ecx), r.args[0], r.args[1]}); return 0u; });
    s.At({0x004D6720}, 0, [](Regs& r) { Record({0x6720, r.ecx}); return 0u; });
    s.At({0x004D2BD0}, 0, [](Regs& r) { Record({0x2BD0, WorldRelocate(r.ecx)}); return 0u; });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xABA0, r.ecx, WorldRelocate(r.args[0])});
        return 0u;
    });
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Record({0xAFC0, r.ecx, r.args[0], r.args[1], r.args[2]});
        std::memset(g_world.heap, 0xA5, sizeof(g_world.heap));
        return reinterpret_cast<uint32_t>(g_world.heap);
    });
    s.At({0x0049FD30, AddressOf(&Lst::HashTable::FlushAllItems)}, 0, [](Regs& r) {
        Record({0xFD30, WorldRelocate(r.ecx)});
        return 0u;
    });
}

void BuildWorld(Lcg& random)
{
    std::memset(&g_world, 0, sizeof(g_world));
    g_nextBuffer = 20;
    static const float kKeys[] = {1.0f, 2.0f, 2.0f, -3.0f, std::numeric_limits<float>::quiet_NaN()};
    for (int m = 0; m < 8; ++m) {
        uint8_t* material = g_world.materials[m];
        *reinterpret_cast<uint32_t*>(material) = 0xC0DE0000 + m; // its checksum
        *reinterpret_cast<float*>(material + 0x1C) = random.Pick(kKeys);
        material[0xC] = static_cast<uint8_t>(random.Below(2));
        material[0x20] = static_cast<uint8_t>(random.Pick({0x00u, 0x40u, 0x10u, 0x50u}));
    }
    for (int i = 0; i < 24; ++i) {
        uint8_t* mesh = g_world.meshes[i];
        *reinterpret_cast<uint8_t**>(mesh + 0x14) = (i % 11 == 10) ? nullptr : g_world.materials[random.Below(8)];
        *reinterpret_cast<uint16_t*>(mesh + 0x20) = static_cast<uint16_t>(i < 20 ? i : 20 + i); // (under 64: the original's break table has 64 entries)
        *reinterpret_cast<uint32_t*>(mesh + 0x44) = random.Below(3);
        // A strip of random indices with some repeats; its buffer object holds the contents pointer.
        uint16_t count = static_cast<uint16_t>(6 + random.Below(40));
        *reinterpret_cast<uint16_t*>(mesh + 0x24) = count;
        for (int k = 0; k < count; ++k)
            g_world.buffers[i][k] = static_cast<uint16_t>(random.Below(6) | (random.Below(9) == 0 ? 0x8000 : 0));
        *reinterpret_cast<uint16_t**>(g_world.bufferObjects[i]) = g_world.buffers[i];
        *reinterpret_cast<uint8_t**>(mesh + 0x54) = g_world.bufferObjects[i];
        g_world.list[i] = mesh;
    }
    // The material table: two buckets used.
    g_world.Table().m_bits = 2;
    g_world.Table().m_buckets = g_world.items;
    g_world.items[0] = {0x10, g_world.materials[2], nullptr};
    g_world.items[1] = {0x11, g_world.materials[5], nullptr};
    *reinterpret_cast<uint32_t*>(g_world.materials[5]) = 0; // (a checksum of 0 to be found)
    g_world.items[3] = {0x13, g_world.materials[1], nullptr};
    // The strip-break records: the third of each is in a strip of a mesh (or not), some masked out.
    for (int r = 0; r < 12; ++r) {
        uint32_t mesh = random.Below(22);
        uint32_t start = random.Below(4);
        const uint16_t* strip = g_world.buffers[mesh < 24 ? mesh : 0];
        uint32_t at = r % 3 == 1 ? start : start + 2; // (some found at the first place looked)
        uint32_t a = strip[at] & 0x7FFF, c = strip[at + 1] & 0x7FFF, b = strip[at + 2] & 0x7FFF;
        if (r % 5 == 4)
            b ^= 1;
        g_world.records[r][0] = random.Pick({1u, 2u, 3u});
        g_world.records[r][1] = (mesh << 16) | a;
        g_world.records[r][2] = (c << 16) | b;
        g_world.records[r][3] = start;
    }
}

// CompareMeshes on every pair; the scene's life: construct, count, allocate, add, sort, find, remove (also meshes
// not in the list, and a gap left), the sphere, the strip breaks, destroy / delete.
void TestMeshList()
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}, {0x007CAA40, 0x400}});
    StandIns& s = *new StandIns();
    StandInBuffers(s);
    g_relocate = WorldRelocate;
    for (uint32_t seed = 1; seed <= 4; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            Lcg random(0x3E5A0000 + seed);
            BuildWorld(random);
            std::memset(&GameVariable<uint8_t>(0x007CAA40), 0, 0x400);
            GameVariable<bool>(0x007CC979) = true;
            GameVariable<uint32_t>(0x007D1800) = 0x3A3A0000;
            std::vector<uint32_t> trace;
            // CompareMeshes.
            for (int i = 0; i < 24; ++i) {
                for (int j = 0; j < 24; ++j) {
                    if (!*reinterpret_cast<uint8_t**>(g_world.meshes[i] + 0x14) ||
                        !*reinterpret_cast<uint8_t**>(g_world.meshes[j] + 0x14))
                        continue;
                    trace.push_back(static_cast<uint32_t>(Fn<int(__cdecl*)(const void*, const void*)>(
                        original, 0x004DDD50)(&g_world.list[i], &g_world.list[j])));
                }
            }
            // Meshes without a material are left out of the list from here on.
            uint8_t* list[24];
            int count = 0;
            for (int i = 0; i < 24; ++i) {
                if (*reinterpret_cast<uint8_t**>(g_world.meshes[i] + 0x14))
                    list[count++] = g_world.list[i];
            }
            auto* scene = reinterpret_cast<Nx::sScene*>(g_world.scene);
            std::memset(g_world.scene, 0x77, sizeof(g_world.scene));
            Originals guard(original, {0x004DE300, 0x004DDDF0, 0x004DE3E0, 0x004DDE20, 0x004DDFA0, 0x004DDE60,
                                       0x004DE020, 0x004DE420, 0x004DE340, 0x004DE060, 0x004DE490, 0x004DE560,
                                       0x004DDD50, 0x004033E0});
            trace.push_back(WorldRelocate(reinterpret_cast<uint32_t>(
                Fn<Nx::sScene*(__thiscall*)(void*)>(original, 0x004DE300)(scene))));
            trace.push_back(HashBytes(g_world.scene, sizeof(g_world.scene)));
            Fn<void(__thiscall*)(void*, int, uint8_t* const*)>(original, 0x004DDDF0)(scene, count, list);
            trace.push_back(HashBytes(g_world.scene, sizeof(g_world.scene)));
            Fn<void(__thiscall*)(void*)>(original, 0x004DE3E0)(scene);
            Fn<void(__thiscall*)(void*, int, uint8_t* const*)>(original, 0x004DDE20)(scene, count, list);
            Fn<void(__thiscall*)(void*)>(original, 0x004DDFA0)(scene);
            for (uint32_t index : {0u, 3u, 7u, 19u, 90u, 0x10007u}) {
                trace.push_back(WorldRelocate(reinterpret_cast<uint32_t>(
                    Fn<uint8_t*(__thiscall*)(const void*, uint32_t)>(original, 0x004DE020)(scene, index))));
            }
            scene->materials = (seed == 3) ? nullptr : &g_world.Table();
            for (uint32_t checksum : {0xC0DE0002u, 0xC0DE0001u, 0xC0DE0007u, 0u}) {
                trace.push_back(WorldRelocate(reinterpret_cast<uint32_t>(
                    Fn<void*(__thiscall*)(const void*, uint32_t)>(original, 0x004DE420)(scene, checksum))));
            }
            // The bounds and their sphere.
            float* bounds = reinterpret_cast<float*>(g_world.scene + 0x44);
            for (int i = 0; i < 8; ++i)
                bounds[i] = static_cast<float>(static_cast<int>(random.Below(20000)) - 10000) / 77.0f;
            Fn<void(__thiscall*)(void*)>(original, 0x004DE340)(scene);
            for (int k = 0; k < 300; ++k) { // (more bounds, of every size)
                float scale = static_cast<float>(1 + random.Below(1000)) / static_cast<float>(1 + random.Below(1000));
                for (int i = 0; i < 8; ++i)
                    bounds[i] = static_cast<float>(static_cast<int>(random.Below(20001)) - 10000) * scale / 7.0f;
                Fn<void(__thiscall*)(void*)>(original, 0x004DE340)(scene);
                trace.push_back(HashBytes(g_world.scene + 0x44, 0x30));
            }
            // Strip breaks (before removing, so the indices still find their meshes).
            Fn<void(__thiscall*)(void*, uint32_t, const uint32_t*, uint32_t)>(original, 0x004DE060)(
                scene, seed == 2 ? 0 : 1 + random.Below(2), &g_world.records[0][0], seed == 4 ? 0 : 12);
            // Remove some (and one that isn't there); then a null put in to be taken as a gap.
            uint8_t* removeList[5] = {list[1], list[4], list[count - 1], g_world.meshes[10], list[1]};
            Fn<void(__thiscall*)(void*, int, uint8_t* const*)>(original, 0x004DDE60)(scene, 5, removeList);
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            g_trace.clear();
            // The end: delete (or only destroy) it.
            scene->extra = seed & 1 ? g_world.extra + 4 : nullptr;
            *reinterpret_cast<uint32_t*>(g_world.extra) = 3;
            scene->comObject = nullptr;
            if (seed & 2)
                Fn<void(__cdecl*)(void*)>(original, 0x004DE560)(scene);
            else
                Fn<void(__thiscall*)(void*)>(original, 0x004DE490)(scene);
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            for (uint32_t i = 0; i < sizeof(g_world) / 4; ++i)
                trace.push_back(WorldRelocate(reinterpret_cast<const uint32_t*>(&g_world)[i]));
            trace.push_back(HashBytes(&GameVariable<uint8_t>(0x007CAA40), 0x400));
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "appmain: a scene's mesh list and materials");
    }
    g_relocate = nullptr;
    delete &s;
}

} // namespace

SELFTEST(Early, TestMeshList, RemoveStandIns);
