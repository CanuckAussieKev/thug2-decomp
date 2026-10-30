// Self-tests for the pixel shaders (appmain batch 5, src/game/gfx/pixel_shaders.cpp): the original and ours with
// D3DXAssembleShader, the device's CreatePixelShader, the material's pass flags, the cache's PutItem, allocation and
// the game's sprintf stood in; every shader text assembled (hashed), handle and cache entry compared.

#include <windows.h>

#include <d3d9.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "game/core/manager.h"
#include "game/file/file.h"
#include "game/gfx/simple_material.h"
#include "game/gfx/pixel_shaders.h"
#include "game/lst/hashtable.h"
#include "game/gfx/texture_dict.h"
#include "hook/appmain_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace AppMainTests;

constexpr uint32_t kDevice = 0xD3D00000;
constexpr uint32_t kBuffer = 0xB0F00000;
constexpr uint32_t kErrors = 0xE7700000;
// ID3DXBuffer: QueryInterface, AddRef, Release, GetBufferPointer, GetBufferSize.
const uint8_t kD3dxBufferArgs[5] = {2, 0, 0, 0, 0};

template <typename F>
F Fn(bool original, uint32_t address)
{
    return reinterpret_cast<F>(Code(original, address));
}

alignas(16) uint8_t g_heap[0x2000];
uint32_t g_heapUsed;
uint32_t g_shaders;
long g_assembleResult;
uint32_t g_flags;

uint32_t HeapRelocate(uint32_t word)
{
    auto base = reinterpret_cast<uint32_t>(g_heap);
    if (word >= base && word < base + sizeof(g_heap))
        return 0x4EA00000 + (word - base);
    return OnStack(word) ? 0x57AC0000u : word;
}

struct Fixture {
    StandIns& s;
    FakeComSet com;
    FakeCom* device;
    FakeCom* buffer;
    FakeCom* errors;
    Fixture() : s(*new StandIns()), com(&s)
    {
        device = com.Device(kDevice);
        buffer = com.Make(kBuffer, kD3dxBufferArgs, 5);
        errors = com.Make(kErrors, kD3dxBufferArgs, 5);
        g_relocate = [this](uint32_t word) {
            auto it = com.tags.find(word);
            return it != com.tags.end() ? it->second : HeapRelocate(word);
        };
        // GetBufferPointer: a tag (the shader's "bytecode").
        com.On(kBuffer, 3, [](Regs&) { return 0xC0DE0000u + g_shaders; });
        com.On(kErrors, 3, [](Regs&) { return 0xE770C0DEu; });
        com.On(kDevice, kCreatePixelShader, [](Regs& r) {
            *reinterpret_cast<uint32_t*>(r.args[2]) = 0x5AD00000 + ++g_shaders;
            return 0u;
        });
        s.At({0x005C2FBD}, 28, [this](Regs& r) { // D3DXAssembleShader
            auto* text = reinterpret_cast<const char*>(r.args[0]);
            Record({0x2FBD, Hash(text), r.args[1], r.args[2], r.args[3], r.args[4]});
            *reinterpret_cast<FakeCom**>(r.args[5]) = buffer;
            *reinterpret_cast<FakeCom**>(r.args[6]) = g_assembleResult ? errors : nullptr;
            return static_cast<uint32_t>(g_assembleResult);
        });
        s.At({0x004D1D90}, 0, [](Regs& r) {
            Record({0x1D90, HeapRelocate(r.ecx)});
            return g_flags;
        });
        s.At({0x006234FE}, 0, StandInSprintf); // (the game's sprintf isn't started offline)
        s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
            Record({0xAFC0, r.args[0], r.args[1], r.args[2]});
            uint32_t at = reinterpret_cast<uint32_t>(g_heap) + g_heapUsed;
            g_heapUsed += (r.args[0] + 15) & ~15u;
            return at;
        });
        s.At({0x00462740, AddressOf(&Lst::HashTable::PutItem)}, 8, [](Regs& r) {
            uint32_t key = *reinterpret_cast<uint32_t*>(r.args[0]);
            Record({0x2740, r.ecx, key, r.args[1]});
            auto* table = reinterpret_cast<Lst::HashTable*>(r.ecx);
            Lst::HashTable::Item& bucket = table->m_buckets[key & ((1u << table->m_bits) - 1)];
            if (!bucket.value)
                bucket = {key, reinterpret_cast<void*>(r.args[1]), nullptr};
            return 1u;
        });
    }
    ~Fixture()
    {
        g_relocate = nullptr;
        delete &s;
    }
};

// CreatePixelShaders with and without pixel shaders (and once more: nothing the second time), the caster table
// made.
void TestFixedPixelShaders()
{
    GameMemory memory({{0x007CA920, 0x60}, {0x007CAA30, 1}, {0x00786AB8, 1}, {0x007CC979, 1}, {0x007D1800, 4},
                       {0x007D69E0, 4}});
    Fixture f;
    for (int variant = 0; variant < 3; ++variant) {
        auto run = [&](bool original) {
            g_trace.clear();
            memory.Fill(0xF1C50000 + variant);
            std::memset(g_heap, 0xCC, sizeof(g_heap));
            g_heapUsed = 0;
            g_shaders = 0;
            g_assembleResult = variant == 2 ? E_FAIL : 0;
            GameVariable<FakeCom*>(0x007D69E0) = f.device;
            GameVariable<bool>(0x007CAA30) = false;
            GameVariable<bool>(0x00786AB8) = variant != 1;
            GameVariable<bool>(0x007CC979) = true;
            GameVariable<uint32_t>(0x007D1800) = 0x3A3A0000;
            Originals guard(original, {0x004DD7F0, 0x0049FD90});
            Fn<void(__cdecl*)()>(original, 0x004DD7F0)();
            Fn<void(__cdecl*)()>(original, 0x004DD7F0)(); // (done already)
            std::vector<uint32_t> trace = g_trace;
            memory.Dump(trace);
            for (uint32_t i = 0; i < sizeof(g_heap) / 4; ++i)
                trace.push_back(HeapRelocate(reinterpret_cast<uint32_t*>(g_heap)[i]));
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "appmain: the fixed pixel shaders");
    }
}

// Generated shaders for materials of 1-4 passes with every blend mode (0-14) in each later pass, the pass flags
// all ways, cache hits (the same configuration twice) and the assembler failing; MaterialPixelShader's fixed
// choices (no texture, mode 9 or not, flags or not).
void TestGeneratedPixelShaders()
{
    GameMemory memory({{0x007CA920, 0x60}, {0x007CA9C0, 0x18}, {0x007D69E0, 4}});
    Fixture f;
    static Lst::HashTable::Item buckets[16];
    alignas(16) static uint8_t material[0x100];
    for (uint32_t seed = 1; seed <= 3; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            memory.Fill(0x6E4E0000 + seed);
            g_shaders = 0;
            GameVariable<FakeCom*>(0x007D69E0) = f.device;
            std::memset(buckets, 0, sizeof(buckets));
            auto& cache = GameVariable<Lst::HashTable>(0x007CA9C0);
            cache.m_bits = 4;
            cache.m_buckets = buckets;
            Lcg random(0x9E4E0000 + seed);
            std::vector<uint32_t> trace;
            Originals guard(original, {0x004DD2C0, 0x004DDC00});
            for (int round = 0; round < 160; ++round) {
                std::memset(material, 0, sizeof(material));
                uint32_t passes = 1 + random.Below(4);
                if (round % 37 == 36)
                    passes = 0;
                *reinterpret_cast<uint32_t*>(material + 8) = passes;
                *reinterpret_cast<uint32_t*>(material + 0x30) = random.Below(4) ? 0x7E0 : 0;
                for (int pass = 0; pass < 4; ++pass)
                    *reinterpret_cast<uint32_t*>(material + 0x80 + 4 * pass) =
                        (random.Below(15)) | (random.Below(256) << 24);
                if (round % 5 == 0)
                    *reinterpret_cast<uint32_t*>(material + 0x80) = 9;
                if (round == 80 || round == 120) { // (cached keys left without a shader: made again)
                    for (Lst::HashTable::Item& item : buckets) {
                        if (item.value) {
                            item.value = nullptr;
                            break;
                        }
                    }
                }
                g_flags = random.Below(3) == 0 ? 0 : random.Below(16);
                g_assembleResult = random.Below(9) == 0 ? E_FAIL : 0;
                if (random.Below(3) == 0) {
                    trace.push_back(Fn<uint32_t(__cdecl*)(const void*)>(original, 0x004DD2C0)(material));
                } else {
                    void* shader = reinterpret_cast<void*>(0xBAD);
                    Fn<void(__cdecl*)(const void*, void**)>(original, 0x004DDC00)(material, &shader);
                    trace.push_back(reinterpret_cast<uint32_t>(shader));
                }
            }
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            trace.push_back(HashBytes(reinterpret_cast<const uint8_t*>(buckets), sizeof(buckets)));
            memory.Dump(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "appmain: generated pixel shaders and a material's shader");
    }
}

// BlendModeFromName for every known name and others; sSimpleMaterial with a texture found (plain and indirect)
// or not, each blend mode, fixed alphas, then destroyed; the chunk skippers.
void TestSimpleMaterial()
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}, {0x00700F20, 4}, {0x007CA940, 0x28}});
    StandIns& s = *new StandIns();
    g_relocate = HeapRelocate;
    alignas(16) static uint8_t entries[3][0x20];
    static uint32_t indirect = 0x7E7E0002;
    s.At({0x0049F540, AddressOf(&Nx::CTexDict::GetTexture)}, 4, [](Regs& r) {
        Record({0xF540, r.ecx, r.args[0]});
        uint32_t which = r.args[0] % 4;
        return which == 3 ? 0u : reinterpret_cast<uint32_t>(entries[which]);
    });
    s.At({0x004D1A90}, 0, [](Regs& r) {
        Record({0x1A90, HeapRelocate(r.ecx)});
        std::memset(reinterpret_cast<void*>(r.ecx), 0x6D, 0x110);
        return r.ecx;
    });
    uint32_t randState = 0;
    s.At({0x00623BA3}, 0, [&](Regs&) {
        randState = randState * 214013u + 2531011u;
        return (randState >> 16) & 0x7FFF;
    });
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Record({0xAFC0, r.args[0], r.args[1], r.args[2]});
        uint32_t at = reinterpret_cast<uint32_t>(g_heap) + g_heapUsed;
        g_heapUsed = (g_heapUsed + 0x110 + 15) & ~15u;
        return at;
    });
    s.At({0x004D2BD0}, 0, [](Regs& r) { Record({0x2BD0, HeapRelocate(r.ecx)}); return 0u; });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xABA0, r.ecx, HeapRelocate(r.args[0])});
        return 0u;
    });
    s.At({0x005B9EE0, AddressOf(&File::Read)}, 0, [](Regs& r) {
        Record({0x9EE0, r.args[1], r.args[2], r.args[3]});
        *reinterpret_cast<uint32_t*>(r.args[0]) = 0x40 + r.args[3];
        return 4u;
    });
    s.At({0x005B9FA0, AddressOf(&File::Seek)}, 0, [](Regs& r) {
        Record({0x9FA0, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    static const uint32_t kNames[] = {0x515E298E, 0x806FFF30, 0x02E58C18, 0xA86285A1, 0xDEA7E576, 0xA7FD7D23,
                                      0x0D7A749A, 0x0EEA99FF, 0x54628ED7, 0x18B98905, 0x40F44B8A, 0x90B93703,
                                      0x68E77F40, 0xB8AA03C9, 0x12345678, 0x68E77F41, 0, 0xFFFFFFFF};
    auto run = [&](bool original) {
        g_trace.clear();
        memory.Fill(0x51A70000);
        std::memset(g_heap, 0xCC, sizeof(g_heap));
        g_heapUsed = 0;
        randState = 7;
        GameVariable<bool>(0x007CC979) = true;
        GameVariable<uint32_t>(0x007D1800) = 0x3A3A0000;
        GameVariable<uint32_t>(0x00700F20) = 0x7D1C7000;
        std::memset(entries, 0, sizeof(entries));
        *reinterpret_cast<uint32_t*>(entries[0] + 0x10) = 0x7E7E0001;
        entries[1][8] = 1;
        *reinterpret_cast<uint32_t**>(entries[1] + 0x10) = &indirect;
        entries[2][8] = 2;
        *reinterpret_cast<uint32_t*>(entries[2] + 0x10) = 0;
        std::vector<uint32_t> trace;
        for (uint32_t name : kNames)
            trace.push_back(Fn<uint32_t(__cdecl*)(uint32_t)>(original, 0x004D9CF0)(name));
        Originals guard(original, {0x004D9B80, 0x004D9CF0, 0x004D9CD0});
        for (int i = 0; i < 18; ++i) { // (every name, also brighten / fixbrighten)
            Nx::sSimpleMaterial pair = {reinterpret_cast<uint8_t*>(0x11), reinterpret_cast<void*>(0x22)};
            int alpha = (i * 37) % 256 - (i == 5 ? 300 : 0);
            auto* result = Fn<Nx::sSimpleMaterial*(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t, int,
                                                                 uint32_t, uint32_t)>(original, 0x004D9B80)(
                &pair, 1, 2, 0x100 + i, kNames[i % 18], alpha, 5, 6);
            trace.push_back(result == &pair);
            trace.push_back(HeapRelocate(reinterpret_cast<uint32_t>(pair.material)));
            trace.push_back(reinterpret_cast<uint32_t>(pair.shader));
            if (i % 3)
                Fn<void(__thiscall*)(void*)>(original, 0x004D9CD0)(&pair);
        }
        Nx::sSimpleMaterial none = {nullptr, nullptr};
        Fn<void(__thiscall*)(void*)>(original, 0x004D9CD0)(&none);
        trace.push_back(static_cast<uint32_t>(Fn<int(__cdecl*)(void*)>(original, 0x004DA6D0)(reinterpret_cast<void*>(0xF11E))));
        uint8_t chunk[16] = {1, 0, 0, 0, 5, 0, 0, 0};
        uint8_t* at = chunk;
        trace.push_back(static_cast<uint32_t>(Fn<int(__cdecl*)(uint8_t**)>(original, 0x004DA710)(&at)));
        trace.push_back(static_cast<uint32_t>(at - chunk));
        trace.insert(trace.end(), g_trace.begin(), g_trace.end());
        for (uint32_t i = 0; i < sizeof(g_heap) / 4; ++i)
            trace.push_back(HeapRelocate(reinterpret_cast<uint32_t*>(g_heap)[i]));
        memory.Dump(trace);
        return trace;
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    Compare(expected, actual, "appmain: simple materials, blend names, chunk skips");
    g_relocate = nullptr;
    delete &s;
}

} // namespace

SELFTEST(Early, TestFixedPixelShaders, RemoveStandIns);
SELFTEST(Early, TestGeneratedPixelShaders, RemoveStandIns);
SELFTEST(Early, TestSimpleMaterial, RemoveStandIns);
