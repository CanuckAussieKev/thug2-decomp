// Self-tests for the PC texture and texture dictionary (src/game/gfx/pc_texture.cpp, RFX-15): the original and ours
// over stand-in textures and dictionaries, the engine's constructors / destructors / loaders and the textures' own
// code stood in; the image copy and colour conversions over random inputs.

#include <windows.h>

#include <d3d9.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/core/manager.h"
#include "game/file/file.h"
#include "game/gfx/pc_texture.h"
#include "game/gfx/texture.h"
#include "game/lst/hashtable.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;
using Nx::CPcTexDict;
using Nx::CPcTexture;
using Nx::sTexture;
using Nx::sTextureEdit;

float RandomFloat(Lcg& random, float low, float high)
{
    return low + (high - low) * static_cast<float>(random.Below(100000)) / 99999.0f;
}

void* g_textureVtable[1];
uint8_t* g_texels;

sTexture* MakeTexture(Lcg& random)
{
    auto* texture = reinterpret_cast<sTexture*>(Bump(sizeof(sTexture)));
    auto* bytes = reinterpret_cast<uint8_t*>(texture);
    for (uint32_t i = 0; i < sizeof(sTexture); ++i)
        bytes[i] = static_cast<uint8_t>(random.Next());
    texture->imageWidth = static_cast<uint16_t>(random.Below(9));
    texture->imageHeight = static_cast<uint16_t>(random.Below(9));
    return texture;
}

void TestPcTextures()
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}});
    StandIns& s = *new StandIns();
    StandInMemory(s);
    for (uint32_t engine : {0x0049F200u, 0x0049F210u, 0x0049FC70u})
        StandInThis(s, {engine}, 0);
    s.At({0x0049F830}, 8, [](Regs& r) {
        Record({0xF830, Relocate(r.ecx), r.args[0], r.args[1]});
        return r.ecx;
    });
    s.At({0x0049F220}, 12, [](Regs& r) {
        Record({0xF220, Relocate(r.ecx), Relocate(r.args[0]), r.args[1], r.args[2]});
        return 1u;
    });
    s.At({0x0049F300}, 24, [](Regs& r) {
        Record({0xF300, Relocate(r.ecx), r.args[0], r.args[1], r.args[2], r.args[3], r.args[4], r.args[5]});
        return 1u;
    });
    s.At({0x0049F330}, 24, [](Regs& r) {
        Record({0xF330, Relocate(r.ecx), r.args[0], r.args[1], r.args[2], r.args[3], r.args[4], r.args[5]});
        return 1u;
    });
    s.At({0x004C8450, AddressOf(&CPcTexture::ConstructCopy)}, 4, [](Regs& r) { // the copy constructor: its texture made
        Record({0x8450, Relocate(r.ecx), Relocate(r.args[0])});
        auto* texture = reinterpret_cast<CPcTexture*>(r.ecx);
        texture->flags = r.args[0] & 0x10 ? 1 : 0;
        auto* engine = reinterpret_cast<sTexture*>(Bump(sizeof(sTexture)));
        if (texture->flags & 1) {
            auto* edit = reinterpret_cast<sTextureEdit*>(Bump(sizeof(sTextureEdit)));
            edit->texture = engine;
            texture->engine = edit;
        } else {
            texture->engine = engine;
        }
        return r.ecx;
    });
    s.At({0x004CB760, AddressOf(&CPcTexDict::ConstructFromFile)}, 4, [](Regs& r) {
        Record({0xB760, Relocate(r.ecx), Relocate(r.args[0])});
        return r.ecx;
    });
    s.At({0x004CB7C0, AddressOf(&CPcTexDict::LoadMemory)}, 4, [](Regs& r) {
        Record({0xB7C0, Relocate(r.ecx), Relocate(r.args[0])});
        return 1u;
    });
    StandInThis(s, {0x004E06A0, AddressOf(&sTexture::DestroyBody)}, 0);
    s.At({0x004E03A0, AddressOf(&sTexture::Lock)}, 0, [](Regs& r) {
        Record({0x03A0, Relocate(r.ecx)});
        return reinterpret_cast<uint32_t>(g_texels);
    });
    StandInThis(s, {0x004E03D0, AddressOf(&sTexture::Unlock)}, 0);
    s.At({0x004E0790, AddressOf(&Nx::LoadTextureFile)}, 0, [](Regs& r) {
        const char* name = reinterpret_cast<const char*>(r.args[0]);
        Record({0x0790, HashBytes(reinterpret_cast<const uint8_t*>(name), static_cast<int>(std::strlen(name)) + 1)});
        return name[0] == 'n' ? 0u : 0x7E000000u + static_cast<uint32_t>(std::strlen(name));
    });
    s.At({0x004E0A90, AddressOf(&Nx::LoadTextureMemory)}, 0, [](Regs& r) {
        Record({0x0A90, Relocate(r.args[0])});
        return r.args[0] & 0x10 ? 0u : 0x7E100000u;
    });
    g_textureVtable[0] = s.Thunk(4, [](Regs& r) {
        Record({0xDE57, Relocate(r.ecx), r.args[0]});
        return r.ecx;
    });
    g_relocate = ArenaRelocate;
    static const char* const kNames[] = {"textures\\board", "n", "abc", "images\\long_name_here_0123"};
    bool ok = true, reported = false;
    for (int round = 0; round < 16; ++round) {
        ok &= CompareTwoRuns("pc textures", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0x7C0 + round);
            GameVariable<uint8_t>(0x007CC979) = 1;
            Lcg random(0x7C00 + round);
            std::vector<uint32_t> trace;
            // a texture: made, its texture set, read, edited, written back, loaded
            auto* texture = reinterpret_cast<CPcTexture*>(Bump(sizeof(CPcTexture)));
            trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*)>(original, 0x004C7ED0)(texture)));
            using Getter = uint32_t(__thiscall*)(void*);
            auto getters = [&] {
                for (uint32_t getter : {0x004C7FB0u, 0x004C7FE0u})
                    trace.push_back(Fn<Getter>(original, getter)(texture) & 0xFFFF);
                for (uint32_t getter : {0x004C8010u, 0x004C8030u, 0x004C8050u, 0x004C8060u})
                    trace.push_back(Fn<Getter>(original, getter)(texture) & 0xFF);
            };
            getters();
            sTexture* engine = MakeTexture(random);
            Fn<void(__thiscall*)(void*, sTexture*)>(original, 0x004C7EF0)(texture, engine);
            texture->unknownC = static_cast<uint8_t>(random.Next());
            texture->unknownD = static_cast<uint8_t>(random.Next());
            getters();
            if (round & 1) { // an edit
                auto* edit = reinterpret_cast<sTextureEdit*>(Bump(sizeof(sTextureEdit)));
                edit->texture = engine;
                uint32_t count = engine->imageWidth * engine->imageHeight;
                edit->pixels = reinterpret_cast<uint32_t*>(Bump(4 * count + 4));
                for (uint32_t i = 0; i < count; ++i)
                    edit->pixels[i] = random.Next() * 2654435761u;
                edit->original = round & 2 ? reinterpret_cast<uint32_t*>(Bump(16)) : nullptr;
                edit->kept = round & 4 ? MakeTexture(random) : nullptr;
                texture->engine = edit;
                texture->flags |= 1;
                getters();
                g_texels = reinterpret_cast<uint8_t*>(Bump(4 * count + 4));
                trace.push_back(Fn<uint32_t(__thiscall*)(void*, int, int)>(original, 0x004C9EC0)(texture, 1, 2) & 0xFF);
                trace.push_back(HashBytes(g_texels, 4 * count));
                if (round & 8)
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*)>(original, 0x004C98B0)(texture) & 0xFF);
            }
            trace.push_back(Fn<uint32_t(__thiscall*)(void*, const char*, int, int)>(original, 0x004C7F00)(
                                texture, kNames[round % 4], 1, 2) & 0xFF);
            trace.push_back(ArenaRelocate(reinterpret_cast<uint32_t>(texture->engine)));
            trace.push_back(Fn<uint32_t(__thiscall*)(void*, uint32_t, int, int, int, int)>(original, 0x004C7F80)(
                                texture, round & 2 ? 0xDA7A0010u : 0xDA7A0000u, 5, 6, 7, 8) & 0xFF);
            texture->engine = round & 4 ? engine : nullptr;
            texture->flags &= ~1u;
            if (round & 8) { // destroyed while editing
                auto* edit = reinterpret_cast<sTextureEdit*>(Bump(sizeof(sTextureEdit)));
                std::memset(edit, 0, sizeof(sTextureEdit));
                edit->texture = engine;
                texture->engine = edit;
                texture->flags |= 1;
            }
            trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, unsigned int)>(original, 0x004CAE60)(
                texture, round & 1)));
            // a dictionary
            auto* dict = reinterpret_cast<CPcTexDict*>(Bump(sizeof(CPcTexDict)));
            trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x004C8070)(dict, 0xD1C70000 + round)));
            auto* table = reinterpret_cast<Lst::HashTable*>(Bump(sizeof(Lst::HashTable)));
            std::memset(table, 0, sizeof(Lst::HashTable));
            table->m_bits = 2;
            table->m_buckets = reinterpret_cast<Lst::HashTable::Item*>(Bump(4 * sizeof(Lst::HashTable::Item)));
            std::memset(table->m_buckets, 0, 4 * sizeof(Lst::HashTable::Item));
            uint32_t key = 0x5EED0000 + round;
            if (round & 2)
                table->m_buckets[key & 3] = {key, round & 4 ? nullptr : reinterpret_cast<void*>(Bump(sizeof(CPcTexture))), nullptr};
            dict->textures = table;
            if (round == 5)
                g_failAllocations = {true};
            trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, const char*, int, int)>(original, 0x004C9B00)(
                dict, kNames[round % 4], 9, 10)));
            trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t,
                                                                    uint32_t, uint32_t)>(original, 0x004C89B0)(
                dict, 1, 2, 3, 4, 5, 6)));
            trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t,
                                                                    uint32_t, uint32_t)>(original, 0x004C9BA0)(
                dict, key, 12, 13, 14, 15, 16)));
            auto* fake = reinterpret_cast<CPcTexture*>(Bump(sizeof(CPcTexture)));
            fake->vtable = g_textureVtable;
            trace.push_back(Fn<uint32_t(__thiscall*)(void*, void*)>(original, 0x004C8090)(dict, round & 1 ? fake : nullptr) & 0xFF);
            auto* source = reinterpret_cast<CPcTexture*>(Bump(round & 1 ? 0x20 : 0x30) + (round & 1 ? 0x10 : 0));
            trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, uint32_t, void*)>(original, 0x004C9C80)(
                dict, 0xC0B10000 + round, source))); // (the copy returned)
            trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, unsigned int)>(original, 0x004C9F60)(dict, round & 2 ? 1 : 0)));
            dict->vtable = g_textureVtable;
            Fn<void(__thiscall*)(void*)>(original, 0x004C9AF0)(dict);
            // the platform's dictionary makers
            if (round == 7)
                g_failAllocations = {true, true, true};
            trace.push_back(ArenaRelocate(Fn<uint32_t(__cdecl*)(const char*)>(original, 0x004C7270)(kNames[round % 4])));
            trace.push_back(ArenaRelocate(Fn<uint32_t(__cdecl*)(uint32_t, const uint8_t*)>(original, 0x004C72E0)(
                0xD1C7 + round, reinterpret_cast<const uint8_t*>(0xDA7A0000))));
            trace.push_back(ArenaRelocate(Fn<uint32_t(__cdecl*)(uint32_t)>(original, 0x004C7360)(0xD1C8 + round)));
            // sub-images in every depth
            static const uint16_t kBits[] = {4, 8, 16, 24, 32};
            for (int k = 0; k < 30; ++k) {
                uint16_t bits = kBits[random.Below(5)];
                uint16_t intoWidth = static_cast<uint16_t>(2 + 2 * random.Below(16));
                uint16_t intoHeight = static_cast<uint16_t>(1 + random.Below(16));
                uint16_t fromWidth = static_cast<uint16_t>(2 * random.Below(12));
                uint16_t fromHeight = static_cast<uint16_t>(random.Below(12));
                uint16_t x = static_cast<uint16_t>(random.Below(intoWidth));
                uint16_t y = static_cast<uint16_t>(random.Below(intoHeight + 3));
                uint32_t intoBytes = intoWidth * intoHeight * 4 + 16, fromBytes = fromWidth * fromHeight * 4 + 16;
                auto* into = reinterpret_cast<uint8_t*>(Bump(intoBytes));
                auto* from = reinterpret_cast<uint8_t*>(Bump(fromBytes));
                for (uint32_t i = 0; i < fromBytes; ++i)
                    from[i] = static_cast<uint8_t>(random.Next());
                Fn<void(__cdecl*)(uint8_t*, uint16_t, uint16_t, uint16_t, uint16_t, const uint8_t*, uint16_t, uint16_t,
                                  uint16_t)>(original, 0x004C80B0)(into, x, y, intoWidth, intoHeight, from, fromWidth,
                                                                   fromHeight, bits);
            }
            // colours both ways
            for (int k = 0; k < 40; ++k) {
                float c[3];
                for (float& v : c)
                    v = random.Below(6) == 0 ? static_cast<float>(random.Below(3)) * 0.5f : RandomFloat(random, 0, 1);
                if (k == 3)
                    c[0] = c[1] = c[2];
                if (k == 4)
                    c[1] = std::numeric_limits<float>::quiet_NaN();
                float hsv[3] = {9, 9, 9};
                Fn<void(__cdecl*)(float, float, float, float*, float*, float*)>(original, 0x004C8180)(
                    c[0], c[1], c[2], &hsv[0], &hsv[1], &hsv[2]);
                float h = k == 6 ? 361.0f : k == 7 ? -30.0f : random.Below(4) == 0 ? 60.0f * random.Below(7)
                                                                                     : RandomFloat(random, 0, 360);
                float sat = random.Below(5) == 0 ? 0.0f : RandomFloat(random, 0, 1);
                float rgb[3] = {9, 9, 9};
                Fn<void(__cdecl*)(float*, float*, float*, float, float, float)>(original, 0x004C82D0)(
                    &rgb[0], &rgb[1], &rgb[2], h, sat, RandomFloat(random, 0, 1));
                for (float v : hsv)
                    trace.push_back(std::bit_cast<uint32_t>(v));
                for (float v : rgb)
                    trace.push_back(std::bit_cast<uint32_t>(v));
            }
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "pc textures: made, set, read, edited / written back / ended, loaded, destroyed; dictionaries; "
              "sub-images; RGB <-> HSV");
}

// The fake Direct3D textures: each one's format, size and levels as the round says; its levels' texels made on first
// lock (the pitch wider than the row).
constexpr int kFakeTextures = 8;
struct FakeTextureState {
    uint32_t format, width, height, levels;
    uint8_t* bits[4];
};
FakeTextureState g_fake[kFakeTextures];
FakeCom* g_fakeObjects[kFakeTextures];
int g_nextFake;
uint32_t g_createResult;
constexpr uint32_t kFakeTag = 0x7E000000;

int FakeIndex(uint32_t object)
{
    for (int i = 0; i < kFakeTextures; ++i)
        if (reinterpret_cast<uint32_t>(g_fakeObjects[i]) == object)
            return i;
    return 0;
}
uint32_t FakePitch(const FakeTextureState& t, uint32_t level)
{
    return ((t.width >> level) + 4 + static_cast<uint32_t>(&t - g_fake)) * 4; // (each texture's own)
}

void TestPcTextureDevice()
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}, {0x007D69E0, 4}});
    StandIns& s = *new StandIns();
    StandInMemory(s);
    FakeComSet com(&s);
    FakeCom* device = com.Device(0xD3D00000);
    for (int i = 0; i < kFakeTextures; ++i) {
        uint32_t tag = kFakeTag + 0x100 * i;
        g_fakeObjects[i] = com.Texture(tag);
        com.On(tag, 13, [](Regs& r) { return g_fake[FakeIndex(r.args[0])].levels; }); // GetLevelCount
        com.On(tag, 17, [](Regs& r) { // GetLevelDesc
            const FakeTextureState& t = g_fake[FakeIndex(r.args[0])];
            auto* desc = reinterpret_cast<D3DSURFACE_DESC*>(r.args[2]);
            std::memset(desc, 0, sizeof(*desc));
            desc->Format = static_cast<D3DFORMAT>(t.format);
            desc->Width = t.width >> r.args[1];
            desc->Height = t.height >> r.args[1];
            return 0u;
        });
        com.On(tag, 19, [](Regs& r) { // LockRect
            FakeTextureState& t = g_fake[FakeIndex(r.args[0])];
            uint32_t level = r.args[1] & 3;
            uint32_t pitch = FakePitch(t, level);
            if (!t.bits[level]) {
                uint32_t size = pitch * ((t.height >> level) + 4) + 16 * (t.width >> level) + 64;
                t.bits[level] = reinterpret_cast<uint8_t*>(Bump(size));
                for (uint32_t k = 0; k < size; ++k)
                    t.bits[level][k] = static_cast<uint8_t>((k * 37) ^ (FakeIndex(r.args[0]) * 11 + level));
            }
            auto* rect = reinterpret_cast<D3DLOCKED_RECT*>(r.args[2]);
            rect->Pitch = static_cast<INT>(pitch);
            rect->pBits = t.bits[level];
            return 0u;
        });
    }
    com.On(0xD3D00000, kCreateTexture, [](Regs& r) {
        int index = g_nextFake < kFakeTextures - 1 ? ++g_nextFake : kFakeTextures - 1;
        FakeTextureState& t = g_fake[index];
        t.width = r.args[1] & 0xFFFF; // (the high bits some rounds pass: recorded, not kept)
        t.height = r.args[2];
        t.format = r.args[5];
        t.levels = r.args[3] ? r.args[3] : 1;
        *reinterpret_cast<FakeCom**>(r.args[7]) = g_fakeObjects[index];
        return g_createResult;
    });
    for (uint32_t engine : {0x0049F200u, 0x0049F210u})
        StandInThis(s, {engine}, 0);
    s.At({0x004E04B0, AddressOf(&sTexture::Construct)}, 0, [](Regs& r) {
        Record({0x04B0, Relocate(r.ecx)});
        std::memset(reinterpret_cast<void*>(r.ecx), 0x5A, sizeof(sTexture));
        return r.ecx;
    });
    s.At({0x004E0500, AddressOf(&sTexture::ConstructCopy)}, 4, [](Regs& r) {
        Record({0x0500, Relocate(r.ecx), Relocate(r.args[0])});
        std::memcpy(reinterpret_cast<void*>(r.ecx), reinterpret_cast<const void*>(r.args[0]), sizeof(sTexture));
        reinterpret_cast<sTexture*>(r.ecx)->checksum ^= 0xC0C0;
        return r.ecx;
    });
    UseStandardRelocation(com);
    auto standard = g_relocate;
    g_relocate = [standard](uint32_t word) { return ArenaRelocate(standard(word)); };
    static const uint32_t kFormats[] = {D3DFMT_A8R8G8B8, D3DFMT_X8R8G8B8, D3DFMT_DXT1, D3DFMT_DXT5, D3DFMT_P8};
    static const int kBits[] = {1, 2, 4, 8, 16, 32, 0, 24, 12, 40, -3, 7};
    bool ok = true, reported = false;
    for (int round = 0; round < 16; ++round) {
        ok &= CompareTwoRuns("pc texture device", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0x7D0 + round);
            GameVariable<uint8_t>(0x007CC979) = 1;
            GameVariable<FakeCom*>(0x007D69E0) = device;
            Lcg random(0x7D00 + round);
            std::memset(g_fake, 0, sizeof(g_fake));
            g_nextFake = 0;
            g_createResult = 0; // (a failure ends the game: not tested)
            g_fake[0] = {kFormats[random.Below(5)], 4u << random.Below(4), 4u << random.Below(4), 1 + random.Below(3), {}};
            std::vector<uint32_t> trace;
            // a texture on fake 0, edited
            auto* source = reinterpret_cast<CPcTexture*>(Bump(sizeof(CPcTexture)));
            Fn<void*(__thiscall*)(void*)>(original, 0x004C7ED0)(source);
            auto* engine = reinterpret_cast<sTexture*>(Bump(sizeof(sTexture)));
            std::memset(engine, 0, sizeof(sTexture));
            engine->checksum = 0xE0E00000 + round;
            engine->width = static_cast<uint16_t>(g_fake[0].width);
            engine->height = static_cast<uint16_t>(g_fake[0].height);
            engine->imageWidth = engine->width;
            engine->imageHeight = engine->height;
            engine->depth = static_cast<uint8_t>(round % 3 == 0 ? 8 : round % 3 == 1 ? 32 : 16);
            engine->texture = g_fakeObjects[0];
            source->engine = engine;
            source->unknownC = static_cast<uint8_t>(0x40 + round);
            if (round & 4) {
                trace.push_back(Fn<uint32_t(__thiscall*)(void*, bool, bool)>(original, 0x004C8A60)(
                                    source, (round & 1) != 0, (round & 2) != 0) & 0xFF);
                auto* edit = static_cast<sTextureEdit*>(source->engine);
                uint32_t bytes = engine->width * engine->height * 4;
                trace.push_back(HashBytes(reinterpret_cast<const uint8_t*>(edit->pixels), bytes));
                if (edit->original)
                    trace.push_back(HashBytes(reinterpret_cast<const uint8_t*>(edit->original), bytes));
            }
            // a copy of it
            auto* copy = reinterpret_cast<CPcTexture*>(Bump(sizeof(CPcTexture)));
            if (round == 6)
                g_failAllocations = {true};
            trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, const void*)>(original, 0x004C8450)(copy, source)));
            // a texture made in a depth
            auto* made = reinterpret_cast<CPcTexture*>(Bump(sizeof(CPcTexture)));
            Fn<void*(__thiscall*)(void*)>(original, 0x004C7ED0)(made);
            if (round & 8) {
                auto* old = reinterpret_cast<sTexture*>(Bump(sizeof(sTexture)));
                std::memset(old, 0, sizeof(sTexture));
                old->texture = round & 1 ? g_fakeObjects[kFakeTextures - 1] : nullptr;
                made->engine = old;
            }
            uint32_t width = 3 + random.Below(60), height = 3 + random.Below(60);
            trace.push_back(Fn<uint32_t(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, int, int, int)>(original,
                                                                                                       0x004C8830)(
                                made, 0xC4EA0000 + round, width | (round & 1 ? 0x10000u : 0u), height,
                                kBits[round % 12], 5, 6) & 0xFF);
            // made again as a copy of the source's (or the made one's)
            auto* into = reinterpret_cast<CPcTexture*>(Bump(sizeof(CPcTexture)));
            Fn<void*(__thiscall*)(void*)>(original, 0x004C7ED0)(into);
            auto* intoEngine = reinterpret_cast<sTexture*>(Bump(sizeof(sTexture)));
            std::memset(intoEngine, 0, sizeof(sTexture));
            intoEngine->texture = round & 2 ? g_fakeObjects[kFakeTextures - 1] : nullptr;
            into->engine = intoEngine;
            trace.push_back(Fn<uint32_t(__thiscall*)(void*, const void*)>(original, 0x004C86A0)(
                                into, round % 5 == 4 ? made : source) & 0xFF);
            for (const FakeTextureState& t : g_fake)
                for (uint32_t level = 0; level < 4; ++level)
                    if (t.bits[level])
                        trace.push_back(HashBytes(t.bits[level], FakePitch(t, level) * ((t.height >> level) + 4)));
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "pc texture device: edits begun, copies, textures made in every depth, made again from another");
}

// The image edits: textures with random pixels, sizes and palettes; their lock / palette / heap calls stood in.
uint8_t* g_lockTexels;
uint8_t* g_palette;

struct EditedTexture {
    CPcTexture* texture;
    sTexture* engine;
    sTextureEdit* edit;
};

EditedTexture MakeEdited(Lcg& random, uint32_t width, uint32_t height, uint32_t imageWidth, uint32_t imageHeight,
                         bool original, uint8_t depth)
{
    EditedTexture t{};
    t.texture = reinterpret_cast<CPcTexture*>(Bump(sizeof(CPcTexture)));
    std::memset(t.texture, 0, sizeof(CPcTexture));
    t.texture->vtable = reinterpret_cast<void**>(0x0064A770);
    t.engine = reinterpret_cast<sTexture*>(Bump(sizeof(sTexture)));
    std::memset(t.engine, 0, sizeof(sTexture));
    t.engine->width = static_cast<uint16_t>(width);
    t.engine->height = static_cast<uint16_t>(height);
    t.engine->imageWidth = static_cast<uint16_t>(imageWidth);
    t.engine->imageHeight = static_cast<uint16_t>(imageHeight);
    t.engine->depth = depth;
    t.edit = reinterpret_cast<sTextureEdit*>(Bump(sizeof(sTextureEdit)));
    t.edit->texture = t.engine;
    uint32_t count = width * height;
    t.edit->pixels = reinterpret_cast<uint32_t*>(Bump(4 * count + 64));
    for (uint32_t i = 0; i < count; ++i)
        t.edit->pixels[i] = random.Next() << 8 | (random.Next() & 0xFF);
    t.edit->original = nullptr;
    if (original) {
        t.edit->original = reinterpret_cast<uint32_t*>(Bump(4 * count + 64));
        for (uint32_t i = 0; i < count; ++i)
            t.edit->original[i] = random.Next() << 8 | (random.Next() & 0xFF);
    }
    t.edit->kept = nullptr;
    t.texture->engine = t.edit;
    t.texture->flags = 1;
    return t;
}

void TestPcTextureImages()
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
    s.At({0x00624031}, 0, [](Regs& r) { // the C runtime's malloc
        Record({0x4031, r.args[0]});
        return Bump(r.args[0] + 16);
    });
    s.At({0x0062411B}, 0, [](Regs& r) { // and free
        Record({0x411B, Relocate(r.args[0])});
        return 0u;
    });
    s.At({0x004E03A0, AddressOf(&sTexture::Lock)}, 0, [](Regs& r) {
        Record({0x03A0, Relocate(r.ecx)});
        return reinterpret_cast<uint32_t>(g_lockTexels);
    });
    StandInThis(s, {0x004E03D0, AddressOf(&sTexture::Unlock)}, 0);
    s.At({0x004E03E0, AddressOf(&sTexture::PaletteOwner)}, 0, [](Regs& r) {
        Record({0x03E0, Relocate(r.ecx)});
        return reinterpret_cast<uint32_t>(g_palette);
    });
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 32; ++round) {
        ok &= CompareTwoRuns("pc texture images", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0x7E0 + round);
            GameVariable<uint8_t>(0x007CC979) = 1;
            Lcg random(0x7E00 + round);
            std::vector<uint32_t> trace;
            auto* manager = reinterpret_cast<Mem::Manager*>(Bump(sizeof(Mem::Manager)));
            std::memset(manager, 0, sizeof(Mem::Manager));
            manager->m_topDownHeap = reinterpret_cast<Mem::Heap*>(Bump(16));
            manager->m_cutsceneTopDownHeap = round & 1 ? reinterpret_cast<Mem::Heap*>(Bump(16)) : nullptr;
            GameVariable<Mem::Manager*>(0x007D1800) = manager;
            g_palette = reinterpret_cast<uint8_t*>(Bump(0x400));
            for (int i = 0; i < 0x400; ++i)
                g_palette[i] = static_cast<uint8_t>(random.Next());
            for (int i = 0; i < 0x100; i += 3) // (ties: the first of equal entries wins)
                std::memcpy(g_palette + 4 * (i + 1), g_palette + 4 * i, 4);
            auto hash = [&](const EditedTexture& t) {
                trace.push_back(HashBytes(reinterpret_cast<const uint8_t*>(t.edit->pixels),
                                          4 * t.engine->width * t.engine->height));
            };
            uint32_t width = 4u << random.Below(3), height = 4u << random.Below(3);
            uint32_t imageWidth = width - (round & 2 ? random.Below(3) : 0);
            uint32_t imageHeight = height - (round & 4 ? random.Below(3) : 0);
            using Method = uint32_t(__thiscall*)(void*);
            auto fresh = [&] { return MakeEdited(random, width, height, imageWidth, imageHeight, round & 8, 32); };
            // the free image functions
            {
                uint16_t fw = static_cast<uint16_t>(1 + random.Below(20)), fh = static_cast<uint16_t>(1 + random.Below(20));
                uint16_t tw = static_cast<uint16_t>(1 + random.Below(20)), th = static_cast<uint16_t>(1 + random.Below(20));
                auto* from = reinterpret_cast<uint8_t*>(Bump(fw * fh * 4 + 64));
                for (int i = 0; i < fw * fh * 4; ++i)
                    from[i] = static_cast<uint8_t>(random.Next());
                uint32_t big = (fw * fh > tw * th ? fw * fh : tw * th) * 4 + 64;
                auto* into = reinterpret_cast<uint8_t*>(Bump(big));
                Fn<void(__cdecl*)(const uint8_t*, uint16_t, uint16_t, uint8_t*, uint16_t, uint16_t, uint16_t)>(
                    original, 0x004C9200)(from, fw, fh, into, tw, th, round % 3 == 0 ? 8 : 32);
                trace.push_back(HashBytes(into, big - 64));
                float zx = RandomFloat(random, 0.3f, 3.0f), zy = RandomFloat(random, 0.3f, 3.0f);
                Fn<void(__cdecl*)(const uint8_t*, uint16_t, uint16_t, uint8_t*, float, float)>(original, 0x004C9520)(
                    from, fw, fh, into, zx, zy);
                trace.push_back(HashBytes(into, fw * fh * 4));
            }
            EditedTexture a = fresh();
            // the palette's nearest entry, tinting
            for (int k = 0; k < 4; ++k) {
                a.engine->depth = static_cast<uint8_t>(k == 0 ? 0 : 1 + random.Below(8));
                trace.push_back(Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x004C97D0)(a.texture, random.Next() * 2654435761u) & 0xFF);
            }
            a.engine->depth = static_cast<uint8_t>(round & 1 ? 32 : round % 4 == 2 ? 8 : 2 + random.Below(7));
            Fn<uint32_t(__thiscall*)(void*, uint32_t, bool)>(original, 0x004C9950)(a.texture, random.Next() * 2654435761u, round & 16);
            hash(a);
            trace.push_back(HashBytes(g_palette, 0x400));
            a.engine->depth = 32;
            // resized from another, zoomed, turned
            EditedTexture b = MakeEdited(random, 4u << random.Below(3), 4u << random.Below(3), 0, 0, false, 32);
            b.engine->imageWidth = b.engine->width;
            b.engine->imageHeight = b.engine->height;
            if (round == 5)
                b.texture->flags = 0;
            trace.push_back(Fn<uint32_t(__thiscall*)(void*, void*, int)>(original, 0x004C9D80)(a.texture, b.texture, 0) & 0xFF);
            hash(a);
            Fn<uint32_t(__thiscall*)(void*, float, float)>(original, 0x004C9DE0)(a.texture, RandomFloat(random, 0.4f, 2.5f),
                                                                           RandomFloat(random, 0.4f, 2.5f));
            hash(a);
            float cx = round % 3 == 0 ? -1.0f : RandomFloat(random, 0, static_cast<float>(width));
            float cy = round % 3 == 1 ? -1.0f : RandomFloat(random, 0, static_cast<float>(height));
            Fn<uint32_t(__thiscall*)(void*, float, float, float)>(original, 0x004C8D30)(a.texture, RandomFloat(random, -7, 7), cx, cy);
            hash(a);
            // moved, bordered, stretched, squashed, mirrored
            int dx = static_cast<int>(random.Below(imageWidth)) - static_cast<int>(imageWidth / 2);
            int dy = static_cast<int>(random.Below(imageHeight)) - static_cast<int>(imageHeight / 2);
            Fn<uint32_t(__thiscall*)(void*, int, int, bool, int)>(original, 0x004CABE0)(a.texture, dx, dy, (round & 4) != 0, 0);
            hash(a);
            Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x004CADC0)(a.texture, random.Next() * 2654435761u);
            hash(a);
            for (int k = 0; k < 3; ++k) {
                int vertical = static_cast<int>(random.Below(2));
                uint32_t size = vertical ? imageHeight : imageWidth;
                uint16_t at = static_cast<uint16_t>(random.Below(size));
                int amount = static_cast<int>(random.Below(2 * size + 1)) - static_cast<int>(size);
                if (k == 0 && round == 3)
                    amount = 0;
                Fn<uint32_t(__thiscall*)(void*, uint16_t, int, int)>(original, 0x004CA220)(a.texture, at, vertical, amount);
                hash(a);
                vertical = static_cast<int>(random.Below(2));
                size = vertical ? imageHeight : imageWidth;
                at = static_cast<uint16_t>(random.Below(size));
                amount = static_cast<int>(random.Below(2 * size + 1)) - static_cast<int>(size);
                if (amount > at) // (the squashed part's size kept above 0: the game's callers' range)
                    amount = at;
                if (amount < at - static_cast<int>(size) + 1)
                    amount = at - static_cast<int>(size) + 1;
                Fn<uint32_t(__thiscall*)(void*, uint16_t, int, int, bool, uint32_t)>(original, 0x004CA410)(
                    a.texture, at, vertical, amount, random.Below(2) != 0, random.Next() * 2654435761u);
                hash(a);
            }
            Fn<Method>(original, 0x004CA7B0)(a.texture);
            hash(a);
            // the hue turned, saturation and value scaled
            Fn<uint32_t(__thiscall*)(void*, float, float, float, bool)>(original, 0x004CAA20)(
                a.texture, RandomFloat(random, -400, 400), RandomFloat(random, 0, 2), RandomFloat(random, 0, 2), round & 16);
            hash(a);
            // another drawn over it
            g_lockTexels = reinterpret_cast<uint8_t*>(Bump(4 * imageWidth * imageHeight + 64));
            for (uint32_t i = 0; i < 4 * imageWidth * imageHeight; ++i)
                g_lockTexels[i] = static_cast<uint8_t>(random.Next());
            EditedTexture over = MakeEdited(random, width, height, imageWidth, imageHeight, false, 32);
            over.texture->flags = round & 1;
            if (!(round & 1))
                over.texture->engine = over.engine;
            trace.push_back(Fn<uint32_t(__thiscall*)(void*, void*, int)>(original, 0x004CA8E0)(a.texture, over.texture, 0) & 0xFF);
            hash(a);
            // rectangles read and written: edited, and palettized
            uint16_t rx = static_cast<uint16_t>(random.Below(imageWidth)), ry = static_cast<uint16_t>(random.Below(imageHeight));
            uint16_t rw = static_cast<uint16_t>(1 + random.Below(imageWidth - rx)), rh = static_cast<uint16_t>(1 + random.Below(imageHeight - ry));
            auto* rect = reinterpret_cast<uint32_t*>(Bump(4 * imageWidth * imageHeight + 64));
            Fn<void(__thiscall*)(void*, uint32_t*, uint16_t, uint16_t, uint16_t, uint16_t)>(original, 0x004C9F90)(a.texture, rect, rx, ry, rw, rh);
            trace.push_back(HashBytes(reinterpret_cast<const uint8_t*>(rect), 4 * rw * rh));
            for (uint32_t i = 0; i < static_cast<uint32_t>(rw * rh); ++i)
                rect[i] ^= random.Next();
            Fn<void(__thiscall*)(void*, const uint32_t*, uint16_t, uint16_t, uint16_t, uint16_t)>(original, 0x004CA0D0)(a.texture, rect, rx, ry, rw, rh);
            hash(a);
            EditedTexture p = MakeEdited(random, width, height, imageWidth, imageHeight, false, 8);
            p.texture->flags = 0;
            p.texture->engine = p.engine;
            uint32_t margin = 4 * imageWidth * imageHeight;
            uint8_t* texels = reinterpret_cast<uint8_t*>(Bump(3 * margin + 64));
            for (uint32_t i = 0; i < 3 * margin; ++i)
                texels[i] = static_cast<uint8_t>(random.Next());
            g_lockTexels = texels + margin;
            Fn<void(__thiscall*)(void*, uint32_t*, uint16_t, uint16_t, uint16_t, uint16_t)>(original, 0x004C9F90)(p.texture, rect, rx, ry, rw, rh);
            trace.push_back(HashBytes(reinterpret_cast<const uint8_t*>(rect), 4 * imageWidth * imageHeight));
            p.engine->depth = 3;
            Fn<void(__thiscall*)(void*, const uint32_t*, uint16_t, uint16_t, uint16_t, uint16_t)>(original, 0x004CA0D0)(p.texture, rect, rx, ry, rw, rh);
            trace.push_back(HashBytes(texels, 3 * margin));
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "pc texture images: resized, zoomed, tinted, turned, moved, bordered, stretched, squashed, mirrored, "
              "hue-shifted, drawn over, rectangles read and written");
}

// The dictionaries' loaders over made-up .tex records (in memory, and the same through a stand-in file), fake
// Direct3D textures, the table's chained puts stood in.
std::vector<uint8_t> g_texFile;
size_t g_texFileAt;
std::vector<uint8_t*> g_levelBits;
int g_nextTexture;

void PutWord(std::vector<uint8_t>& out, uint32_t value)
{
    out.insert(out.end(), reinterpret_cast<uint8_t*>(&value), reinterpret_cast<uint8_t*>(&value) + 4);
}

std::vector<uint8_t> MakeTexRecord(Lcg& random, int count, bool withPalettes)
{
    std::vector<uint8_t> out;
    PutWord(out, 2);
    PutWord(out, static_cast<uint32_t>(count));
    static const uint32_t kDepths[] = {8, 16, 32, 24};
    static const uint32_t kCompressions[] = {0, 0, 1, 2, 5, 3};
    for (int i = 0; i < count; ++i) {
        PutWord(out, 0x7E000000 + random.Below(0x10000));
        PutWord(out, 4u << random.Below(3));
        PutWord(out, 1 + random.Below(16));
        uint32_t levels = 1 + random.Below(3);
        PutWord(out, levels);
        // (the first a known format: the original's is its stack's until one is set)
        PutWord(out, kDepths[random.Below(i == 0 ? 3 : 4)]);
        PutWord(out, random.Below(40));
        PutWord(out, kCompressions[random.Below(i == 0 ? 5 : 6)]);
        uint32_t paletteSize = random.Below(3) == 0 ? 16 * (1 + random.Below(4)) : 0;
        PutWord(out, paletteSize);
        if (withPalettes)
            for (uint32_t k = 0; k < paletteSize; ++k)
                out.push_back(static_cast<uint8_t>(random.Next()));
        for (uint32_t level = 0; level < levels; ++level) {
            uint32_t size = 4 * random.Below(16);
            PutWord(out, size);
            for (uint32_t k = 0; k < size; ++k)
                out.push_back(static_cast<uint8_t>(random.Next()));
        }
    }
    return out;
}

void TestPcTexDictLoad()
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}, {0x007D69E0, 4}});
    StandIns& s = *new StandIns();
    StandInMemory(s);
    FakeComSet com(&s);
    FakeCom* device = com.Device(0xD3D00000);
    std::vector<FakeCom*> textures;
    for (int i = 0; i < 16; ++i) {
        uint32_t tag = 0x7E000000 + 0x100 * i;
        textures.push_back(com.Texture(tag));
        com.On(tag, 19, [i](Regs& r) { // LockRect: each level its own buffer
            uint32_t level = r.args[1] & 3;
            auto* rect = reinterpret_cast<D3DLOCKED_RECT*>(r.args[2]);
            rect->Pitch = 64;
            rect->pBits = g_levelBits[4 * i + level];
            return 0u;
        });
        com.On(tag, 17, [](Regs& r) { // GetLevelDesc
            std::memset(reinterpret_cast<void*>(r.args[2]), 0, sizeof(D3DSURFACE_DESC));
            return 0u;
        });
    }
    com.On(0xD3D00000, kCreateTexture, [&textures](Regs& r) {
        int index = g_nextTexture < 15 ? g_nextTexture++ : 15;
        *reinterpret_cast<FakeCom**>(r.args[7]) = textures[index];
        return 0u;
    });
    for (uint32_t engine : {0x0049F200u})
        StandInThis(s, {engine}, 0);
    s.At({0x0049F8F0}, 8, [](Regs& r) {
        Record({0xF8F0, Relocate(r.ecx), HashBytes(reinterpret_cast<const uint8_t*>(r.args[0]), 4), r.args[1]});
        return r.ecx;
    });
    s.At({0x004E04B0, AddressOf(&sTexture::Construct)}, 0, [](Regs& r) {
        Record({0x04B0, Relocate(r.ecx)});
        std::memset(reinterpret_cast<void*>(r.ecx), 0x5A, sizeof(sTexture));
        return r.ecx;
    });
    s.At({0x00462740, AddressOf(&Lst::HashTable::PutItem)}, 8, [](Regs& r) {
        Record({0x2740, Relocate(r.ecx), *reinterpret_cast<uint32_t*>(r.args[0]), Relocate(r.args[1])});
        return 1u;
    });
    s.At({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, [](Regs& r) {
        Record({0xB0E0, Relocate(r.args[0])});
        return 0u;
    });
    s.At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [](Regs&) {
        Record({0xB110});
        return 0u;
    });
    s.At({0x005B9E60, AddressOf(&File::Open)}, 0, [](Regs& r) {
        const char* name = reinterpret_cast<const char*>(r.args[0]);
        Record({0x9E60, HashBytes(reinterpret_cast<const uint8_t*>(name), static_cast<int>(std::strlen(name))),
                HashBytes(reinterpret_cast<const uint8_t*>(r.args[1]), 3)});
        return name[0] == 'x' ? 0u : 0xF11E0000u;
    });
    s.At({0x005B9EE0, AddressOf(&File::Read)}, 0, [](Regs& r) {
        uint32_t bytes = r.args[1] * r.args[2];
        Record({0x9EE0, r.args[1], r.args[2], r.args[3]});
        std::memcpy(reinterpret_cast<void*>(r.args[0]), g_texFile.data() + g_texFileAt, bytes);
        g_texFileAt += bytes;
        return r.args[2];
    });
    s.At({0x005B9EA0, AddressOf(&File::Close)}, 0, [](Regs& r) {
        Record({0x9EA0, r.args[0]});
        return 0u;
    });
    UseStandardRelocation(com);
    auto standard = g_relocate;
    g_relocate = [standard](uint32_t word) { return ArenaRelocate(standard(word)); };
    bool ok = true, reported = false;
    for (int round = 0; round < 24; ++round) {
        Lcg random(0x7D10 + round);
        int count = round % 6 == 0 ? 0 : 1 + static_cast<int>(random.Below(4));
        std::vector<uint8_t> fileRecord = MakeTexRecord(random, count, true);
        std::vector<uint8_t> memoryRecord = MakeTexRecord(random, count, false);
        ok &= CompareTwoRuns("tex dict load", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0x7D10 + round);
            GameVariable<uint8_t>(0x007CC979) = 1;
            GameVariable<FakeCom*>(0x007D69E0) = device;
            g_nextTexture = 0;
            g_levelBits.clear();
            for (int i = 0; i < 64; ++i)
                g_levelBits.push_back(reinterpret_cast<uint8_t*>(Bump(0x400)));
            std::vector<uint32_t> trace;
            auto* manager = reinterpret_cast<Mem::Manager*>(Bump(sizeof(Mem::Manager)));
            std::memset(manager, 0, sizeof(Mem::Manager));
            GameVariable<Mem::Manager*>(0x007D1800) = manager;
            // a dictionary with no table, an empty one (remade), or one holding a texture
            auto* dict = reinterpret_cast<CPcTexDict*>(Bump(sizeof(CPcTexDict)));
            std::memset(dict, 0, sizeof(CPcTexDict));
            int kind = round % 3;
            if (kind) {
                // (a block header before it: the heap a remade table goes in)
                auto* header = reinterpret_cast<uint32_t*>(Bump(0x20) + 0x10);
                header[0] = 0x4EA90000 + round;
                auto* table = reinterpret_cast<Lst::HashTable*>(Bump(sizeof(Lst::HashTable)));
                std::memset(table, 0, sizeof(Lst::HashTable));
                table->m_bits = 2;
                table->m_buckets = reinterpret_cast<Lst::HashTable::Item*>(Bump(4 * sizeof(Lst::HashTable::Item)));
                std::memset(table->m_buckets, 0, 4 * sizeof(Lst::HashTable::Item));
                for (int k = 0; k < 4; ++k) // (keys left with no values: emptied by the remake's flush)
                    table->m_buckets[k].key = 0xE0000000u + k;
                if (kind == 2)
                    table->m_buckets[1] = {0x1234, reinterpret_cast<void*>(Bump(4)), nullptr};
                dict->textures = table;
            }
            if (round == 7) // (no table: the textures put in none - stood in here)
                dict->textures = nullptr;
            g_texFile = fileRecord;
            g_texFileAt = 0;
            if (round & 1) {
                trace.push_back(Fn<uint32_t(__thiscall*)(void*, const char*)>(original, 0x004CB380)(
                                    dict, round == 9 ? "xmissing" : "levels\\test") & 0xFF);
            } else {
                trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, const char*)>(original, 0x004CB760)(dict, "dict")));
            }
            trace.push_back(static_cast<uint32_t>(g_texFileAt));
            auto* data = reinterpret_cast<uint8_t*>(Bump(static_cast<uint32_t>(memoryRecord.size()) + 16));
            std::memcpy(data, memoryRecord.data(), memoryRecord.size());
            trace.push_back(Fn<uint32_t(__thiscall*)(void*, const uint8_t*)>(original, 0x004CB7C0)(dict, data) & 0xFF);
            trace.push_back(ArenaRelocate(reinterpret_cast<uint32_t>(dict->textures)));
            for (uint8_t* bits : g_levelBits)
                trace.push_back(HashBytes(bits, 0x400));
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "tex dicts: loaded from files and memory (tables none, empty - remade - or holding some)");
}

} // namespace

SELFTEST(Early, TestPcTextures, RemoveStandIns);
SELFTEST(Early, TestPcTextureDevice, RemoveStandIns);
SELFTEST(Early, TestPcTextureImages, RemoveStandIns);
SELFTEST(Early, TestPcTexDictLoad, RemoveStandIns);
