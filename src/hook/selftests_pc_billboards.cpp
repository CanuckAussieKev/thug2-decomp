// Self-tests for the PC billboards (src/game/gfx/pc_billboards.cpp, RFX-21): the original and ours over stand-in
// billboard meshes, materials and the manager at 0x00775B80, a recording stand-in device, D3DX, qsort, the
// materials' and buffers' code stood in.

#include <windows.h>

#include <d3d9.h>

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "game/core/alloc.h"
#include "game/gfx/mesh.h"
#include "game/gfx/pc_billboards.h"
#include "game/gfx/render_state.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;
using Nx::sMesh;

float RandomFloat(Lcg& random, float low, float high)
{
    return low + (high - low) * static_cast<float>(random.Below(100000)) / 99999.0f;
}

// Calls with registers: the mesh in EBX and three stack arguments; the type in EAX and count in ECX.
__declspec(naked) void __cdecl CallWithEbx(void* /*code*/, void* /*mesh*/, const float*, const float*, const float*)
{
    __asm {
        push ebx
        mov ebx, [esp + 12]
        push dword ptr [esp + 24]
        push dword ptr [esp + 24]
        push dword ptr [esp + 24]
        mov eax, [esp + 20]
        call eax
        add esp, 12
        pop ebx
        ret
    }
}
__declspec(naked) uint32_t __cdecl CallWithEaxEcx(void* /*code*/, uint32_t /*eax*/, uint32_t /*ecx*/)
{
    __asm {
        mov eax, [esp + 8]
        mov ecx, [esp + 12]
        call dword ptr [esp + 4]
        ret
    }
}

constexpr uint32_t kDevice = 0xD3D00000;

sMesh* MakeBillboardMesh(Lcg& random, uint8_t* material, uint32_t type)
{
    auto* mesh = reinterpret_cast<sMesh*>(Bump(sizeof(sMesh)));
    std::memset(mesh, 0, sizeof(sMesh));
    mesh->material = material;
    mesh->flags = (random.Below(5) ? 0x40u : 0u) | (random.Below(3) ? 0u : 4u);
    mesh->stride = static_cast<uint8_t>(random.Below(8) == 0 ? 0x34 : 0x10 + 4 * random.Below(6));
    mesh->vertexCount = static_cast<uint16_t>(4 + random.Below(5));
    mesh->indexCounts[0] = static_cast<uint16_t>(4 + random.Below(9));
    *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(mesh) + 0x50) = 1 + random.Below(7);
    *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(mesh) + 0x38) = 0x142 + random.Below(3);
    *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(mesh) + 0x44) = 0x5ADE0000 + random.Below(16);
    auto* billboard = reinterpret_cast<Nx::sBillboard*>(Bump(sizeof(Nx::sBillboard)));
    billboard->type = type;
    for (int k = 0; k < 4; ++k) {
        billboard->pivot[k] = RandomFloat(random, -50, 50);
        billboard->axis[k] = RandomFloat(random, -1, 1);
    }
    if (random.Below(6) == 0)
        billboard->axis[0] = billboard->axis[1] = billboard->axis[2] = 0.0f; // (no axis: not normalised)
    mesh->billboard = billboard;
    auto* buffer = reinterpret_cast<Nx::sVertexBuffer*>(Bump(0x20));
    std::memset(buffer, 0, 0x20);
    *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(buffer) + 8) = 0xB0F00000 + random.Below(16);
    uint32_t bytes = mesh->stride * mesh->vertexCount;
    buffer->data = reinterpret_cast<uint8_t*>(Bump(bytes + 16));
    for (uint32_t i = 0; i < bytes / 4; ++i)
        reinterpret_cast<float*>(buffer->data)[i] = RandomFloat(random, -4, 4);
    mesh->vertexBuffers[0] = buffer;
    mesh->indexBuffers[0] = reinterpret_cast<Nx::sIndexBuffer*>(Bump(0x20));
    if (random.Below(2)) {
        mesh->perVertexBytes = reinterpret_cast<uint8_t*>(Bump(16));
        for (int i = 0; i < 16; ++i)
            mesh->perVertexBytes[i] = static_cast<uint8_t>(random.Below(3) ? random.Below(4) : 0);
    }
    return mesh;
}

uint8_t* MakeMaterial(Lcg& random)
{
    auto* material = reinterpret_cast<uint8_t*>(Bump(0x110));
    std::memset(material, 0, 0x110);
    *reinterpret_cast<uint32_t*>(material + 8) = 1 + random.Below(3);
    *reinterpret_cast<float*>(material + 0x1C) = static_cast<float>(random.Below(4));
    material[0x20] = static_cast<uint8_t>(random.Below(2) ? 0x40 : 0);
    *reinterpret_cast<uint32_t*>(material + 0x30) = random.Below(5) ? 0x7E000000 + random.Below(9) : 0;
    auto* table = reinterpret_cast<uint32_t*>(Bump(16));
    for (int i = 0; i < 4; ++i)
        table[i] = random.Next() * 2654435761u;
    *reinterpret_cast<uint32_t**>(material + 0xF8) = table;
    return material;
}

uint8_t* g_lockData;

void TestPcBillboards()
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}, {0x007D69E0, 4}, {0x00775B7C, 0x458}, {0x00786EB4, 1},
                       {0x007013D0, 4}, {0x0067E998, 0x30}, {0x00786BD0, 0x80}, {0x00786F28, 4}, {0x00786AB8, 1},
                       {0x007D6436, 8}, {0x00786CA8, 8}, {0x00787340, 8}});
    StandIns& s = *new StandIns();
    StandInMemory(s);
    FakeComSet com(&s);
    FakeCom* device = com.Device(kDevice);
    FakeCom* pixelShader = com.Texture(0x95000000);
    FakeCom* vertexShader = com.Texture(0x75000000);
    FakeCom* indices = com.Texture(0x1B000000);
    FakeCom* stream = com.Buffer(0x5E000000);
    com.On(kDevice, 101, [stream](Regs& r) { // GetStreamSource
        *reinterpret_cast<FakeCom**>(r.args[2]) = stream;
        *reinterpret_cast<uint32_t*>(r.args[3]) = 0x44;
        *reinterpret_cast<uint32_t*>(r.args[4]) = 0x18;
        return 0u;
    });
    com.On(kDevice, 108, [pixelShader](Regs& r) { // GetPixelShader
        *reinterpret_cast<FakeCom**>(r.args[1]) = pixelShader;
        return 0u;
    });
    com.On(kDevice, 93, [vertexShader](Regs& r) { // GetVertexShader
        *reinterpret_cast<FakeCom**>(r.args[1]) = vertexShader;
        return 0u;
    });
    com.On(kDevice, 105, [indices](Regs& r) { // GetIndices
        *reinterpret_cast<FakeCom**>(r.args[1]) = indices;
        return 0u;
    });
    com.On(kDevice, 90, [](Regs& r) { // GetFVF
        *reinterpret_cast<uint32_t*>(r.args[1]) = 0x152;
        return 0u;
    });
    com.On(kDevice, 94, [](Regs& r) { // SetVertexShaderConstantF
        Record({HashBytes(reinterpret_cast<const uint8_t*>(r.args[2]), 16 * static_cast<int>(r.args[3]))});
        return 0u;
    });
    s.At({0x006242A0}, 0, [](Regs& r) { // qsort, with the comparison given
        Record({0x42A0, Relocate(r.args[0]), r.args[1]});
        std::qsort(reinterpret_cast<void*>(r.args[0]), r.args[1], r.args[2],
                   reinterpret_cast<int(__cdecl*)(const void*, const void*)>(r.args[3]));
        return 0u;
    });
    s.At({0x005BFCD5}, 8, [](Regs& r) { // D3DXMatrixTranspose
        const auto* m = reinterpret_cast<const float*>(r.args[1]);
        float out[16];
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                out[i * 4 + j] = m[j * 4 + i];
        std::memcpy(reinterpret_cast<void*>(r.args[0]), out, sizeof(out));
        return r.args[0];
    });
    s.At({0x005BFA8C}, 12, [](Regs& r) { // D3DXMatrixMultiply
        const auto* a = reinterpret_cast<const float*>(r.args[1]);
        const auto* b = reinterpret_cast<const float*>(r.args[2]);
        float out[16];
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                double sum = 0;
                for (int k = 0; k < 4; ++k)
                    sum += static_cast<double>(a[i * 4 + k]) * b[k * 4 + j];
                out[i * 4 + j] = static_cast<float>(sum);
            }
        std::memcpy(reinterpret_cast<void*>(r.args[0]), out, sizeof(out));
        return r.args[0];
    });
    s.At({0x004DAA80, AddressOf(&Nx::SetPixelShader)}, 0, [](Regs& r) {
        Record({0xAA80, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00490DE0}, 0, [](Regs& r) {
        Record({0x0DE0, r.args[0] & 0xFF});
        return 0u;
    });
    StandInThis(s, {0x004D3D10}, 0);
    StandInThis(s, {0x004D24D0}, 2);
    StandInThis(s, {0x004D1F00}, 0);
    StandInThis(s, {0x004D23E0}, 0);
    s.At({0x004D4090}, 16, [](Regs& r) {
        Record({0x4090, Relocate(r.ecx), r.args[3]});
        g_lockData = reinterpret_cast<uint8_t*>(Bump(0x400));
        *reinterpret_cast<uint8_t**>(r.args[2]) = g_lockData;
        return 0u;
    });
    s.At({0x004D40E0}, 0, [](Regs& r) {
        Record({0x40E0, Relocate(r.ecx), HashBytes(g_lockData, 0x200)});
        return 0u;
    });
    s.At({0x00492E50}, 0, [](Regs& r) { return 0x1B100000 + (Relocate(r.ecx) & 0xFFFF); });
    UseStandardRelocation(com);
    auto standard = g_relocate;
    g_relocate = [standard](uint32_t word) { return ArenaRelocate(standard(word)); };
    bool ok = true, reported = false;
    for (int round = 0; round < 16; ++round) {
        ok &= CompareTwoRuns("pc billboards", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0xB10 + round);
            GameVariable<uint8_t>(0x007CC979) = 1;
            GameVariable<FakeCom*>(0x007D69E0) = device;
            GameVariable<bool>(0x00786AB8) = (round & 1) != 0;
            GameVariable<uint8_t>(0x007D643A) = static_cast<uint8_t>(round & 2);
            GameVariable<uint8_t>(0x007D6436) = 1;
            Lcg random(0xB100 + round);
            std::vector<uint32_t> trace;
            auto* manager = &GameVariable<Nx::sBillboardManager>(0x00775B80);
            manager->count = 0;
            auto* view = &GameVariable<float>(0x00786BD0);
            for (int k = 0; k < 32; ++k)
                view[k] = RandomFloat(random, -1, 1);
            if (round == 3)
                view[2] = view[10] = 0.0f; // (looking straight up or down)
            Fn<void(__thiscall*)(void*)>(original, 0x004CDAA0)(manager);
            // meshes over a few materials, added
            uint8_t* materials[3] = {MakeMaterial(random), MakeMaterial(random), MakeMaterial(random)};
            std::vector<sMesh*> meshes;
            for (int k = 0; k < 9; ++k) {
                sMesh* mesh = MakeBillboardMesh(random, materials[random.Below(3)], random.Below(3));
                if (k == 4)
                    mesh->material = nullptr;
                meshes.push_back(mesh);
                Fn<void(__thiscall*)(void*, sMesh*)>(original, 0x004CEBE0)(manager, mesh);
            }
            trace.push_back(static_cast<uint32_t>(manager->count));
            Fn<void(__thiscall*)(void*, unsigned int)>(original, 0x004CEAB0)(manager, 1 + (round % 3));
            for (int i = 0; i < manager->count; ++i)
                Fn<void(__thiscall*)(void*)>(original, 0x004CE2D0)(manager->batches[i]);
            // drawn directly, with axes of its own
            float axes[3][3];
            for (auto& axis : axes)
                for (float& v : axis)
                    v = RandomFloat(random, -1, 1);
            CallWithEbx(Code(original, 0x004CDD00), meshes[0], axes[0], axes[1], axes[2]);
            for (uint32_t type = 0; type < 8; ++type)
                trace.push_back(CallWithEaxEcx(Code(original, 0x004CD980), type, 7 + type * 3));
            // the comparison
            for (int k = 0; k < 6; ++k) {
                Nx::sBillboardBatch* pair[2] = {k == 0 || k == 5 ? nullptr : manager->count ? manager->batches[random.Below(manager->count)] : nullptr,
                                                k == 1 || k == 5 ? nullptr : manager->count ? manager->batches[random.Below(manager->count)] : nullptr};
                trace.push_back(Fn<uint32_t(__cdecl*)(const void*, const void*)>(original, 0x004CD9D0)(&pair[0], &pair[1]));
            }
            // taken out again
            for (size_t k = 0; k < meshes.size(); ++k)
                if (static_cast<int>(k % 3) != round % 3 || round > 8)
                    Fn<void(__thiscall*)(void*, sMesh*)>(original, 0x004CE9F0)(manager, meshes[k]);
            trace.push_back(static_cast<uint32_t>(manager->count));
            // the windows
            GameVariable<uint32_t>(0x007013D0) = 0;
            if (round == 5)
                g_failAllocations = {false, false, true};
            if (round != 5)
                Fn<void(__cdecl*)()>(original, 0x004CD8A0)();
            auto* window = GameVariable<Nx::CPcWindow*>(0x007013D0);
            if (window)
                trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, unsigned int)>(original, 0x004CD870)(window, round & 1)));
            Fn<void(__cdecl*)()>(original, 0x004CD940)();
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "pc billboards: axes, meshes added / drawn in batches / taken out, the comparison, the counts, windows");
}

} // namespace

SELFTEST(Early, TestPcBillboards, RemoveStandIns);
