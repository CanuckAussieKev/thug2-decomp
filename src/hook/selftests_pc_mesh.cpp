// Self-tests for the PC mesh (src/game/gfx/pc_mesh.cpp, RFX-7): the original and ours with the file functions over
// a stand-in stream, the engine mesh's destructor and the scene unload stood in, allocations from an arena; the
// skin data (strip breaks, sorted - duplicate keys too) compared.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/file/file.h"
#include "game/gfx/mesh_list.h"
#include "game/gfx/pc_mesh.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;
using Nx::CPcMesh;

std::vector<uint32_t> g_file;
size_t g_fileAt;

void StandInMeshCalls(StandIns& s)
{
    StandInMemory(s);
    s.At({0x005B9E60, AddressOf(&File::Open)}, 0, [](Regs& r) {
        Record({0x9E60, Hash(reinterpret_cast<const char*>(r.args[0])), Hash(reinterpret_cast<const char*>(r.args[1]))});
        g_fileAt = 0;
        return 0xF11E0000u;
    });
    s.At({0x005B9EE0, AddressOf(&File::Read)}, 0, [](Regs& r) {
        uint32_t bytes = r.args[1] * r.args[2];
        Record({0x9EE0, Relocate(r.args[0]), r.args[1], r.args[2], r.args[3]});
        auto* data = reinterpret_cast<const uint8_t*>(g_file.data());
        if (g_fileAt + bytes <= 4 * g_file.size())
            std::memcpy(reinterpret_cast<void*>(r.args[0]), data + g_fileAt, bytes);
        g_fileAt += bytes;
        return bytes;
    });
    s.At({0x005B9EA0, AddressOf(&File::Close)}, 0, [](Regs& r) {
        Record({0x9EA0, r.args[0]});
        return 0u;
    });
    StandInThis(s, {0x00490B60}, 0);
    StandInCdecl(s, {0x0048C690}, 2);
}

std::vector<uint32_t> SkinData(Lcg& random, uint32_t version, uint32_t count)
{
    std::vector<uint32_t> words = {version};
    if (version >= 2)
        words.push_back(0xB0E0 + random.Below(50));
    words.push_back(count);
    for (uint32_t i = 0; i < count; ++i) {
        words.push_back(random.Next());
        // (keys: a few meshes, a few indices - duplicates among them)
        words.push_back((random.Below(3) << 16) | random.Below(4));
        words.push_back(random.Next());
    }
    return words;
}

// The skin from memory and from files (versions 1 and 2, no breaks, many, duplicate keys), the constructor for model
// files (with "skin" or not, its extension in upper case, a name without the extension), the scene set, the
// destructors (with breaks and a scene, without), the comparison by itself.
void TestPcMesh()
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}});
    StandIns& s = *new StandIns();
    StandInMeshCalls(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 10; ++round) {
        ok &= CompareTwoRuns("pc mesh", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0x3E5 + round);
            GameVariable<uint8_t>(0x007CC979) = 1;
            Lcg random(0x3E50 + round);
            std::vector<uint32_t> trace;
            auto* mesh = reinterpret_cast<CPcMesh*>(Bump(sizeof(CPcMesh)));
            std::memset(mesh, 0, sizeof(CPcMesh));
            Fn<void*(__thiscall*)(void*)>(original, 0x004B7B30)(mesh);
            std::vector<uint32_t> skin = SkinData(random, 1 + round % 2, round == 0 ? 0 : 1 + random.Below(round * 6));
            Fn<void(__thiscall*)(void*, const void*)>(original, 0x004B7EB0)(mesh, round == 9 ? nullptr : skin.data());
            const uint8_t* cursor = reinterpret_cast<const uint8_t*>(skin.data());
            trace.push_back(Fn<uint32_t(__thiscall*)(void*, const uint8_t**)>(original, 0x004B7C60)(mesh, &cursor) & 0xFF);
            trace.push_back(static_cast<uint32_t>(cursor - reinterpret_cast<const uint8_t*>(skin.data())));
            // from a file
            g_file = SkinData(random, 1 + (round + 1) % 2, round == 1 ? 0 : 1 + random.Below(20));
            auto* other = reinterpret_cast<CPcMesh*>(Bump(sizeof(CPcMesh)));
            std::memset(other, 0, sizeof(CPcMesh));
            const char* names[7] = {"models/a/a.skin.xbx", "models/b/B.SKIN.XBX", "models/c/c.mdl.xbx", "skinny",
                                    "Models/D/D.Skin.Xbx", "skins/E.SKIN.XBX", "skin_F.SKIN.xbx"};
            Fn<void*(__thiscall*)(void*, const char*)>(original, 0x004B7D50)(other, names[round % 7]);
            trace.push_back(Fn<uint32_t(__thiscall*)(void*, const char*)>(original, 0x004B7B50)(other, "x.cas.xbx") &
                            0xFF);
            // the scene
            auto* list = reinterpret_cast<Nx::sScene*>(Bump(sizeof(Nx::sScene)));
            list->extra = reinterpret_cast<uint8_t*>(0x4E100000 + round);
            list->unknown78 = 3 + round;
            auto* scene = reinterpret_cast<uint8_t*>(Bump(0x284));
            *reinterpret_cast<Nx::sScene**>(scene + 0x280) = list;
            Fn<void(__thiscall*)(void*, uint8_t*)>(original, 0x004B7ED0)(other, scene);
            if (round & 1)
                other->scene = nullptr;
            if (round == 3)
                other->breaks = nullptr;
            trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, unsigned int)>(original, 0x004B7F00)(
                other, round == 4 ? 2u : 1u)));
            Fn<void(__thiscall*)(void*)>(original, 0x004B7E30)(mesh);
            // the comparison
            for (int k = 0; k < 8; ++k) {
                Nx::sStripBreak a = {0, random.Pick({0x10002u, 0x10003u, 0x20001u, 0x00002u}), 0, 0};
                Nx::sStripBreak b = {0, random.Pick({0x10002u, 0x10003u, 0x20001u, 0x00002u}), 0, 0};
                trace.push_back(Fn<uint32_t(__cdecl*)(const void*, const void*)>(original, 0x004B7AF0)(&a, &b));
            }
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "pc mesh: the skin data from memory and files, the constructors, the scene, the destructors");
}

} // namespace

SELFTEST(Early, TestPcMesh, RemoveStandIns);
