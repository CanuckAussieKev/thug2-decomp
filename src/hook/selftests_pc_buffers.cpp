// Self-tests for the PC vertex and index buffers (src/game/gfx/pc_buffers.cpp, RFX-24): the original and ours over
// buffers made, locked and unlocked every way, made again, taken off their lists; meshes' buffers, arenas and
// coloured pixel shaders; the skin scaling; the pixel shaders by index - a recording stand-in device and buffers,
// the C runtime's heap stood in.

#include <windows.h>

#include <d3d9.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/gfx/mesh.h"
#include "game/gfx/pc_scene.h"
#include "game/gfx/render_state.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;
using Nx::sIndexBuffer;
using Nx::sMesh;
using Nx::sVertexBuffer;

constexpr uint32_t kDevice = 0xD3D00000;

float RandomFloat(Lcg& random, float low, float high)
{
    return low + (high - low) * static_cast<float>(random.Below(100000)) / 99999.0f;
}

uint8_t* g_card;       // what the stand-in buffers' Lock hands out
uint32_t g_lockResult; // and returns

__declspec(naked) uint32_t __cdecl CallStdcall1(void* /*code*/, uint32_t /*argument*/)
{
    __asm {
        push dword ptr [esp + 8]
        call dword ptr [esp + 8]
        ret
    }
}

void TestPcBuffers()
{
    GameMemory memory({{0x007D69E0, 4}, {0x007865CC, 0x70}, {0x00786ECC, 0x60}, {0x007CA920, 0x48}});
    StandIns& s = *new StandIns();
    FakeComSet com(&s);
    FakeCom* device = com.Device(kDevice);
    FakeCom* vertexBuffer = com.Buffer(0x7B000000);
    FakeCom* indexBuffer = com.Buffer(0x1B000000);
    com.On(kDevice, 26, [vertexBuffer](Regs& r) { // CreateVertexBuffer
        *reinterpret_cast<FakeCom**>(r.args[5]) = vertexBuffer;
        return 0u;
    });
    com.On(kDevice, 27, [indexBuffer](Regs& r) { // CreateIndexBuffer
        *reinterpret_cast<FakeCom**>(r.args[5]) = indexBuffer;
        return 0u;
    });
    for (uint32_t tag : {0x7B000000u, 0x1B000000u}) {
        com.On(tag, 11, [](Regs& r) { // Lock
            *reinterpret_cast<uint8_t**>(r.args[3]) = g_card;
            return g_lockResult;
        });
        com.On(tag, 12, [](Regs&) { // Unlock: what was written
            Record({HashBytes(g_card, 0x200)});
            return 0x5A5Au;
        });
    }
    s.At({0x00624031}, 0, [](Regs& r) { // malloc
        Record({0x4031, r.args[0]});
        return Bump(r.args[0] ? r.args[0] : 4);
    });
    s.At({0x0062411B}, 0, [](Regs& r) { // free
        Record({0x411B, Relocate(r.args[0])});
        return 0u;
    });
    s.At({0x004DAAF0, AddressOf(&Nx::SetPixelShaderWithConstants)}, 0, [](Regs& r) {
        Record({0xAAF0, r.args[0], r.args[1], HashBytes(&GameVariable<uint8_t>(0x00786ECC), 0x50),
                GameVariable<uint8_t>(0x00786F24)});
        return 0u;
    });
    UseStandardRelocation(com);
    auto standard = g_relocate;
    g_relocate = [standard](uint32_t word) { return ArenaRelocate(standard(word)); };
    const uint32_t lockFlags[5] = {0, D3DLOCK_READONLY, D3DLOCK_DISCARD, D3DLOCK_DISCARD | D3DLOCK_READONLY,
                                   D3DLOCK_NOOVERWRITE};
    bool ok = true, reported = false;
    for (int round = 0; round < 16; ++round) {
        ok &= CompareTwoRuns("pc buffers", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0x3FF0 + round);
            GameVariable<FakeCom*>(0x007D69E0) = device;
            GameVariable<uint32_t>(0x007865E4) = 0;
            GameVariable<uint32_t>(0x007865E8) = 0;
            Lcg random(0x3FF00 + round);
            g_card = reinterpret_cast<uint8_t*>(Bump(0x400));
            std::memset(g_card, 0, 0x400);
            g_lockResult = 0;
            std::vector<uint32_t> trace;
            // vertex buffers
            std::vector<sVertexBuffer*> vbs;
            for (int k = 0; k < 6; ++k) {
                uint32_t bytes = 0x20 + 4 * random.Below(0x40);
                uint32_t flags = random.Below(6);
                void* at = reinterpret_cast<void*>(Bump(sizeof(sVertexBuffer)));
                auto* vb = Fn<sVertexBuffer*(__thiscall*)(void*, uint32_t, uint32_t, uint32_t)>(original, 0x004D3FF0)(
                    at, bytes, random.Below(2) ? 0x208 : 0, flags);
                trace.push_back(ArenaRelocate(reinterpret_cast<uint32_t>(vb)));
                if (vb->data)
                    for (uint32_t i = 0; i < bytes; ++i)
                        vb->data[i] = static_cast<uint8_t>(random.Next());
                if (vb->flags & 4)
                    vb->arenaOffset = 4 * random.Below(16);
                if ((vb->flags & 4) && random.Below(2))
                    vb->buffer = vertexBuffer;
                vbs.push_back(vb);
            }
            for (int k = 0; k < 14; ++k) {
                sVertexBuffer* vb = vbs[random.Below(vbs.size())];
                if (!vb->data)
                    continue;
                g_lockResult = random.Below(5) == 0 ? 0x8876086Cu : 0u;
                uint8_t* out = nullptr;
                uint32_t offset = 4 * random.Below(8);
                uint32_t bytes = random.Below(3) ? 4 * random.Below(8) : 0;
                uint32_t flags = lockFlags[random.Below(5)];
                if (!vb->buffer)
                    flags &= ~static_cast<uint32_t>(D3DLOCK_DISCARD); // (a share without a buffer can't be)
                trace.push_back(Fn<uint32_t(__thiscall*)(void*, uint32_t, uint32_t, uint8_t**, uint32_t)>(
                    original, 0x004D4090)(vb, offset, bytes, &out, flags));
                trace.push_back(ArenaRelocate(reinterpret_cast<uint32_t>(out)));
                if (random.Below(4) == 0 && (vb->flags & 4))
                    vb->buffer = random.Below(2) ? vertexBuffer : nullptr;
                trace.push_back(Fn<uint32_t(__thiscall*)(void*)>(original, 0x004D40E0)(vb));
            }
            for (sVertexBuffer* vb : vbs) {
                if (!vb->buffer)
                    vb->buffer = vertexBuffer;
                g_lockResult = random.Below(4) == 0 ? 0x8876086Cu : 0u;
                if (vb->data || (vb->flags & 1))
                    Fn<void(__thiscall*)(void*)>(original, 0x004D41E0)(vb);
            }
            // index buffers
            std::vector<sIndexBuffer*> ibs;
            for (int k = 0; k < 4; ++k) {
                uint32_t bytes = 2 + 2 * random.Below(0x80);
                void* at = reinterpret_cast<void*>(Bump(sizeof(sIndexBuffer)));
                auto* ib = Fn<sIndexBuffer*(__thiscall*)(void*, uint32_t, uint32_t, uint32_t)>(original, 0x004D4270)(
                    at, bytes, random.Below(2) ? 0x200 : 0, D3DFMT_INDEX16);
                trace.push_back(ArenaRelocate(reinterpret_cast<uint32_t>(ib)));
                for (uint32_t i = 0; i < bytes / 2; ++i)
                    ib->indices[i] = static_cast<uint16_t>(random.Next());
                uint16_t* out = nullptr;
                trace.push_back(Fn<uint32_t(__thiscall*)(void*, uint32_t, uint32_t, uint16_t**, uint32_t)>(
                    original, 0x004D42F0)(ib, 4 * random.Below(4), 8, &out, 0));
                trace.push_back(ArenaRelocate(reinterpret_cast<uint32_t>(out)));
                g_lockResult = random.Below(4) == 0 ? 0x8876086Cu : 0u;
                trace.push_back(Fn<uint32_t(__thiscall*)(void*)>(original, 0x004D4300)(ib));
                ibs.push_back(ib);
            }
            // off their lists (in any order; one twice, one never listed)
            for (int k = 0; k < 6; ++k) {
                sVertexBuffer* vb = k == 5 ? reinterpret_cast<sVertexBuffer*>(Bump(sizeof(sVertexBuffer)))
                                           : vbs[random.Below(vbs.size())];
                Fn<void(__thiscall*)(void*)>(original, 0x004D3BE0)(vb);
                trace.push_back(ArenaRelocate(GameVariable<uint32_t>(0x007865E4)));
            }
            for (int k = 0; k < 4; ++k) {
                Fn<void(__thiscall*)(void*)>(original, 0x004D3C40)(ibs[random.Below(ibs.size())]);
                trace.push_back(ArenaRelocate(GameVariable<uint32_t>(0x007865E8)));
            }
            // meshes: buffers by index, the arena, the coloured pixel shader
            auto* mesh = reinterpret_cast<sMesh*>(Bump(sizeof(sMesh)));
            std::memset(mesh, 0, sizeof(sMesh));
            for (int k = 0; k < 3; ++k)
                Fn<void(__thiscall*)(void*, int, void*)>(original, 0x004D3FD0)(mesh, k, vbs[(k + round) % vbs.size()]);
            for (int k = 0; k < 3; ++k)
                trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, int)>(original, 0x004D3FC0)(mesh, k)));
            Nx::sVertexArena arena{vertexBuffer, 4 * random.Below(8), 0x400, reinterpret_cast<void*>(Bump(0x800))};
            for (int k = 0; k < 3; ++k) {
                sVertexBuffer* vb = vbs[random.Below(vbs.size())];
                if (!vb->data)
                    continue;
                if (random.Below(3) == 0)
                    vb->buffer = nullptr;
                mesh->vertexBuffers[0] = vb;
                Fn<void(__thiscall*)(void*, void*)>(original, 0x004D3E50)(mesh, &arena);
                trace.push_back(arena.used);
            }
            uint8_t* material = reinterpret_cast<uint8_t*>(Bump(0x110));
            std::memset(material, 0, 0x110);
            mesh->material = material;
            *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(mesh) + 0x44) = 0x95000000 + round;
            for (int k = 0; k < 3; ++k) {
                *reinterpret_cast<uint32_t*>(material + 8) = random.Below(5);
                for (int pass = 0; pass < 4; ++pass)
                    material[0x20 + 4 * pass] = static_cast<uint8_t>(random.Below(3) ? 0x10 : 0x90);
                for (float& c : mesh->color)
                    c = RandomFloat(random, 0, 1);
                auto* constants = &GameVariable<float>(0x00786ECC);
                for (int i = 0; i < 20; ++i)
                    constants[i] = RandomFloat(random, -1, 2);
                GameVariable<uint32_t>(0x00786F1C) = random.Below(2);
                Fn<void(__thiscall*)(void*)>(original, 0x004D3D10)(mesh);
            }
            // the skin scaling, the pixel shaders by index
            uint32_t rect[4] = {random.Next(), random.Next(), random.Next(), random.Next()};
            Fn<void(__cdecl*)(const uint32_t*)>(original, 0x004D3CA0)(rect);
            trace.push_back(HashBytes(&GameVariable<uint8_t>(0x007865CC), 0x18));
            Fn<void(__cdecl*)()>(original, 0x004D3CE0)();
            trace.push_back(HashBytes(&GameVariable<uint8_t>(0x007865CC), 0x18));
            for (uint32_t index = 0; index < 0x16; ++index)
                trace.push_back(CallStdcall1(Code(original, 0x004D3ED0), index == 0x15 ? 0x1234 : index));
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "pc buffers: made, locked / unlocked, remade, off their lists; mesh buffers, arena, coloured shader");
}

} // namespace

SELFTEST(Early, TestPcBuffers, RemoveStandIns);
