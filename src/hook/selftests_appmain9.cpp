// Self-tests for textures, their render target list and the image loaders, and the key names (appmain batch 9:
// src/game/gfx/texture.cpp, texture_file.cpp, src/game/sys/key_names.cpp): the original and ours against stand-in
// Direct3D textures, allocation, files, unswizzling and palettes; calls and memory compared.

#include <windows.h>

#include <d3d9.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "game/core/manager.h"
#include "game/file/file.h"
#include "game/gfx/texture.h"
#include "game/sys/key_names.h"
#include "hook/appmain_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "game/sys/platform_input.h"
#include "hook/standins.h"

namespace {

using namespace AppMainTests;

constexpr uint32_t kDevice = 0xD3D00000;

template <typename F>
F Fn(bool original, uint32_t address)
{
    return reinterpret_cast<F>(Code(original, address));
}

struct TextureWorld {
    alignas(16) Nx::sTexture textures[8];
    uint8_t heap[0x8000];
    uint8_t texels[8][0x2000];
    uint8_t file[0x3000];
};
alignas(16) TextureWorld g_tex;
uint32_t g_texHeap;

uint32_t TexRelocate(uint32_t word)
{
    auto base = reinterpret_cast<uint32_t>(&g_tex);
    if (word >= base && word < base + sizeof(g_tex))
        return 0xA0000000 + (word - base);
    return OnStack(word) ? 0x57AC0000u : word;
}

struct TextureFixture {
    StandIns& s;
    FakeComSet com;
    FakeCom* device;
    std::vector<FakeCom*> d3dTextures, surfaces;
    size_t nextTexture = 0;
    uint32_t levelCount = 1, format = D3DFMT_A8R8G8B8, width = 16, height = 8;
    int failCreate = 0; // 1: no texture, 2: a texture but a failure code
    uint32_t fileAt = 0;
    TextureFixture() : s(*new StandIns()), com(&s)
    {
        device = com.Device(kDevice);
        for (uint32_t i = 0; i < 8; ++i) {
            uint32_t tag = 0x7E000000 + (i << 16);
            d3dTextures.push_back(com.Texture(tag));
            surfaces.push_back(com.Surface(0x5F000000 + (i << 16)));
            com.On(tag, 13, [this](Regs&) { return levelCount; }); // GetLevelCount
            com.On(tag, 17, [this](Regs& r) { // GetLevelDesc
                auto* desc = reinterpret_cast<D3DSURFACE_DESC*>(r.args[2]);
                std::memset(desc, 0, sizeof(*desc));
                desc->Format = static_cast<D3DFORMAT>(format);
                desc->Width = width >> r.args[1];
                desc->Height = height >> r.args[1];
                return 0u;
            });
            com.On(tag, 18, [this, i](Regs& r) { // GetSurfaceLevel
                *reinterpret_cast<FakeCom**>(r.args[2]) = surfaces[i];
                return 0u;
            });
            com.On(tag, 19, [i](Regs& r) { // LockRect
                auto* rect = reinterpret_cast<D3DLOCKED_RECT*>(r.args[2]);
                rect->Pitch = 0x40 + 4 * static_cast<int>(r.args[1]) + 8 * static_cast<int>(i); // (each texture its own pitch)
                rect->pBits = g_tex.texels[i] + 0x100 * r.args[1];
                return i == 6 ? static_cast<uint32_t>(E_FAIL) : 0u;
            });
        }
        com.On(kDevice, kCreateTexture, [this](Regs& r) {
            *reinterpret_cast<FakeCom**>(r.args[7]) = failCreate == 1 ? nullptr : d3dTextures[nextTexture++ % 8];
            return failCreate ? static_cast<uint32_t>(E_FAIL) : 0u;
        });
        g_relocate = [this](uint32_t word) {
            auto it = com.tags.find(word);
            return it != com.tags.end() ? it->second : TexRelocate(word);
        };
        s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
            Record({0xAFC0, r.args[0], r.args[1], r.args[2]});
            uint32_t at = reinterpret_cast<uint32_t>(g_tex.heap) + g_texHeap;
            g_texHeap = (g_texHeap + r.args[0] + 15) & ~15u;
            std::memset(reinterpret_cast<void*>(at), 0xA5, r.args[0]);
            return at;
        });
        s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
            Record({0xABA0, TexRelocate(r.args[0])});
            return 0u;
        });
        s.At({0x005B9E60, AddressOf(&File::Open)}, 0, [this](Regs& r) {
            Record({0x9E60, Hash(reinterpret_cast<const char*>(r.args[0])), Hash(reinterpret_cast<const char*>(r.args[1]))});
            fileAt = 0;
            return *reinterpret_cast<const char*>(r.args[0]) == 'x' ? 0u : 0xF11E0000u;
        });
        s.At({0x005B9EE0, AddressOf(&File::Read)}, 0, [this](Regs& r) {
            uint32_t bytes = r.args[1] * r.args[2];
            Record({0x9EE0, r.args[1], r.args[2], r.args[3], TexRelocate(r.args[0])});
            std::memcpy(reinterpret_cast<void*>(r.args[0]), g_tex.file + fileAt, bytes);
            fileAt += bytes;
            return bytes;
        });
        s.At({0x005B9EA0, AddressOf(&File::Close)}, 0, [](Regs& r) { Record({0x9EA0, r.args[0]}); return 0u; });
        s.At({0x005F5AF0, Resolve(OursAt(0x005F5AF0))}, 0, [](Regs& r) { // unswizzle
            Record({0x5AF0, TexRelocate(r.args[0]), TexRelocate(r.args[1]), r.args[2], r.args[3], r.args[4]});
            std::memset(reinterpret_cast<void*>(r.args[0]), 0x5A, 16);
            return 0u;
        });
        s.At({0x005F45E0, Resolve(OursAt(0x005F45E0))}, 0, [](Regs& r) { // through the palette
            Record({0x45E0, TexRelocate(r.args[0]), TexRelocate(r.args[1]), r.args[2],
                    HashBytes(reinterpret_cast<const uint8_t*>(r.args[3]), 0x400)});
            return 0u;
        });
    }
    ~TextureFixture()
    {
        g_relocate = nullptr;
        delete &s;
    }
};

GameMemory* TextureMemory()
{
    return new GameMemory({{0x007CC254, 4}, {0x007CC979, 1}, {0x007D1800, 4}, {0x007D69E0, 4}, {0x00786E94, 0x10}});
}

// The list (construct three, unlist the middle and a stranger), copies of 1-3 levels in A8R8G8B8 / DXT1 / DXT5,
// render targets made (and failing) and released / made again, Set / Lock (also failing) / Unlock / PaletteOwner,
// destroy (with the texture set on a stage).
void TestTextures()
{
    std::unique_ptr<GameMemory> memory(TextureMemory());
    TextureFixture f;
    static const uint32_t kFormats[] = {D3DFMT_A8R8G8B8, D3DFMT_DXT1, D3DFMT_DXT5};
    for (uint32_t seed = 1; seed <= 6; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            memory->Fill(0x7E770000 + seed);
            std::memset(&g_tex, 0, sizeof(g_tex));
            g_texHeap = 0;
            f.nextTexture = 0;
            f.failCreate = 0;
            f.format = kFormats[seed % 3];
            f.levelCount = 1 + seed % 3;
            f.width = 32;
            f.height = 16 + 4 * seed;
            for (uint32_t i = 0; i < sizeof(g_tex.texels); ++i)
                reinterpret_cast<uint8_t*>(g_tex.texels)[i] = static_cast<uint8_t>(i * 7 + (i >> 13) * 29 + seed); // (each buffer its own)
            GameVariable<FakeCom*>(0x007D69E0) = f.device;
            GameVariable<uint32_t>(0x007CC254) = 0;
            GameVariable<bool>(0x007CC979) = true;
            GameVariable<uint32_t>(0x007D1800) = 0x3A3A0000;
            Originals guard(original, {0x004E04B0, 0x004E0500, 0x004E06A0, 0x004E03F0, 0x004E0380, 0x004E03A0,
                                       0x004E03D0, 0x004E03E0, 0x004E0700, 0x004E0320, 0x004E0440, 0x004DA730});
            Nx::sTexture* t = g_tex.textures;
            for (int i = 0; i < 4; ++i)
                Fn<void*(__thiscall*)(void*)>(original, 0x004E04B0)(&t[i]);
            for (int i = 0; i < 3; ++i) {
                f.failCreate = seed == 4 && i == 1 ? 2 : 0; // (the original reads a null texture otherwise)
                Record({Fn<bool(__thiscall*)(void*, int, int, uint8_t, int)>(original, 0x004E0700)(
                    &t[i], 64 + i, 32 + i, static_cast<uint8_t>(0x10 + i), 9)});
                f.failCreate = 0;
            }
            t[3].texture = f.d3dTextures[5];
            t[3].levels = static_cast<uint8_t>(f.levelCount);
            t[3].checksum = 0xC4EC;
            t[3].palette = seed & 1 ? reinterpret_cast<void*>(0x9A1E77E) : nullptr;
            Fn<void*(__thiscall*)(void*, const void*)>(original, 0x004E0500)(&t[4], &t[3]);
            Fn<void(__thiscall*)(void*)>(original, 0x004E03F0)(&t[1]);
            Fn<void(__thiscall*)(void*)>(original, 0x004E03F0)(&t[5]); // (not on the list)
            Fn<void(__thiscall*)(void*, int)>(original, 0x004E0380)(&t[2], 1);
            Fn<void(__thiscall*)(void*, int)>(original, 0x004E0380)(&t[0], 0);
            Record({TexRelocate(Fn<uint32_t(__thiscall*)(void*)>(original, 0x004E03A0)(&t[2]))});
            t[5].texture = f.d3dTextures[6]; // (its lock fails)
            Record({TexRelocate(Fn<uint32_t(__thiscall*)(void*)>(original, 0x004E03A0)(&t[5]))});
            Fn<void(__thiscall*)(void*)>(original, 0x004E03D0)(&t[2]);
            Record({TexRelocate(reinterpret_cast<uint32_t>(Fn<void*(__thiscall*)(void*)>(original, 0x004E03E0)(&t[3]))),
                    TexRelocate(reinterpret_cast<uint32_t>(Fn<void*(__thiscall*)(void*)>(original, 0x004E03E0)(&t[2])))});
            Fn<void(__thiscall*)(void*)>(original, 0x004E03F0)(&t[3]); // (the head: unlisted)
            Fn<void*(__thiscall*)(void*)>(original, 0x004E04B0)(&t[6]); // (listed, no texture, not a render target)
            Fn<void(__cdecl*)()>(original, 0x004E0320)();
            f.failCreate = seed == 5 ? 1 : 0; // (made again: or not)
            Fn<void(__cdecl*)()>(original, 0x004E0440)();
            f.failCreate = 0;
            (&GameVariable<void*>(0x00786E94))[3] = t[2].texture; // (destroyed while on stage 3)
            Fn<void(__thiscall*)(void*)>(original, 0x004E06A0)(&t[0]);
            Fn<void(__thiscall*)(void*)>(original, 0x004E06A0)(&t[2]);
            std::vector<uint32_t> trace = g_trace;
            for (uint32_t i = 0; i < sizeof(g_tex.textures) / 4; ++i)
                trace.push_back(TexRelocate(reinterpret_cast<const uint32_t*>(g_tex.textures)[i]));
            trace.push_back(HashBytes(&g_tex.texels[0][0], sizeof(g_tex.texels)));
            for (uint32_t i = 0; i < 0x400 / 4; ++i)
                trace.push_back(TexRelocate(reinterpret_cast<const uint32_t*>(g_tex.heap)[i]));
            memory->Dump(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "appmain: textures and the render target list");
    }
}

// The loaders: every format (32, 16, 8-bit palette, a small palette format, a large one), sizes a power of two each
// way or not (or zero), the plain id 0x9B, a missing file, the Direct3D texture failing (memory loader).
void TestTextureLoaders()
{
    std::unique_ptr<GameMemory> memory(TextureMemory());
    TextureFixture f;
    struct Case {
        uint32_t id, width, height, format;
        uint16_t imageWidth, imageHeight;
        bool fail;
    };
    static const Case kCases[] = {
        {1, 16, 8, 0, 12, 7, false},    {1, 16, 8, 2, 16, 8, false},   {1, 8, 8, 0x13, 5, 5, false},
        {1, 12, 8, 0, 12, 8, false},    {1, 16, 6, 0x13, 9, 6, false}, {1, 0, 8, 2, 3, 3, false},
        {1, 16, 16, 4, 16, 16, false},  {1, 16, 16, 0x20, 4, 4, false}, {1, 16, 16, 0x10, 16, 16, false}, {0x9B, 16, 8, 0, 16, 8, false},
        {1, 16, 8, 0, 16, 8, true},
    };
    for (const Case& c : kCases) {
        for (int loader = 0; loader < 2; ++loader) {
            auto run = [&](bool original) {
                g_trace.clear();
                memory->Fill(0x10AD0000 + c.format);
                std::memset(&g_tex, 0, sizeof(g_tex));
                g_texHeap = 0;
                f.nextTexture = 0;
                f.failCreate = c.fail ? 1 : 0;
                uint32_t header[8] = {c.id, 7, c.width, c.height, c.format, 0x14,
                                      static_cast<uint32_t>(c.imageWidth) | static_cast<uint32_t>(c.imageHeight) << 16,
                                      0x400}; // (a whole palette: a shorter one leaves stack bytes in it)
                std::memcpy(g_tex.file, header, sizeof(header));
                for (uint32_t i = 0x20; i < sizeof(g_tex.file); ++i)
                    g_tex.file[i] = static_cast<uint8_t>(i * 13);
                GameVariable<FakeCom*>(0x007D69E0) = f.device;
                GameVariable<uint32_t>(0x007CC254) = 0;
                GameVariable<bool>(0x007CC979) = true;
                GameVariable<uint32_t>(0x007D1800) = 0x3A3A0000;
                Originals guard(original, {0x004E0790, 0x004E0A90, 0x004E04B0, 0x004015D0});
                void* result;
                if (loader == 0)
                    result = Fn<void*(__cdecl*)(const char*)>(original, 0x004E0790)(c.fail ? "x.img" : "textures/a.img");
                else
                    result = Fn<void*(__cdecl*)(const uint8_t*)>(original, 0x004E0A90)(g_tex.file);
                std::vector<uint32_t> trace = g_trace;
                trace.push_back(TexRelocate(reinterpret_cast<uint32_t>(result)));
                for (uint32_t i = 0; i < sizeof(g_tex.heap) / 4; ++i)
                    trace.push_back(TexRelocate(reinterpret_cast<const uint32_t*>(g_tex.heap)[i]));
                trace.push_back(HashBytes(&g_tex.texels[0][0], sizeof(g_tex.texels)));
                memory->Dump(trace);
                return trace;
            };
            std::vector<uint32_t> expected = run(true);
            std::vector<uint32_t> actual = run(false);
            Compare(expected, actual, "appmain: the image loaders");
        }
    }
}

// The key names in both languages (the names' text recorded).
void TestKeyNames()
{
    StandIns& s = *new StandIns();
    s.At({0x005F3BF0, AddressOf(&Platform::SetKeyName)}, 0, [](Regs& r) {
        uint32_t hash = 2166136261u;
        for (auto* c = reinterpret_cast<const wchar_t*>(r.args[1]); *c; ++c)
            hash = (hash ^ static_cast<uint32_t>(*c)) * 16777619u;
        Record({r.args[0], hash});
        return 0u;
    });
    for (uint32_t address : {0x004E0DA0u, 0x004E0FE0u}) {
        auto run = [&](bool original) {
            g_trace.clear();
            Fn<void(__cdecl*)()>(original, address)();
            return g_trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "appmain: the key names");
    }
    delete &s;
}

} // namespace

SELFTEST(Early, TestTextures, RemoveStandIns);
SELFTEST(Early, TestTextureLoaders, RemoveStandIns);
SELFTEST(Early, TestKeyNames, RemoveStandIns);
