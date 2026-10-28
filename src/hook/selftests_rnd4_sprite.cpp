// Self-tests for the engine's sprite, textured quad (src/game/gfx/engine_sprite.cpp) and screen effects
// (screen_effects.cpp), RND4-2: the original and ours over stand-in objects with recording vtables, stand-in
// textures (their size getters) and script structures (the getters stood in, answering from a table), every result,
// call and the arena compared.

#include <windows.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "game/gfx/engine_sprite.h"
#include "game/gfx/screen_effects.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;

void* g_objectVtable[16];
void* g_effectsVtable[16];
void* g_textureVtable[48];
uint32_t g_calls;
uint16_t g_textureWidth, g_textureHeight;

uint8_t* Block(uint32_t size) { return reinterpret_cast<uint8_t*>(Bump(size)); }

template <typename F>
F At(bool original, uint32_t address)
{
    return Fn<F>(original, address);
}

// The objects' slots record themselves (slot 0: the destructor's flags); the textures' 33 / 34 give the size (noise
// above the word).
void StandInObjects(StandIns& s)
{
    StandInMemory(s);
    for (int slot = 0; slot < 16; ++slot) {
        uint16_t pop = slot == 0 ? 4 : 0;
        g_objectVtable[slot] = s.Thunk(pop, [slot, pop](Regs& r) {
            Record({0xA000u + slot, Relocate(r.ecx)});
            if (pop)
                Record({r.args[0]});
            return ++g_calls;
        });
    }
    // The screen effects' platform slots: one argument each (two for [4]).
    for (int slot = 0; slot < 16; ++slot) {
        uint16_t pop = slot == 4 ? 8 : 4;
        g_effectsVtable[slot] = s.Thunk(pop, [slot, pop](Regs& r) {
            Record({0xC000u + slot, Relocate(r.ecx)});
            for (int i = 0; i < pop / 4; ++i)
                Record({Relocate(r.args[i])});
            return ++g_calls;
        });
    }
    for (int slot = 0; slot < 48; ++slot) {
        g_textureVtable[slot] = s.Thunk(0, [slot](Regs& r) {
            Record({0xB000u + slot, Relocate(r.ecx)});
            if (slot == 33)
                return 0xABCD0000u | g_textureWidth;
            if (slot == 34)
                return 0x12340000u | g_textureHeight;
            return ++g_calls;
        });
    }
}
uint8_t* MakeTexture()
{
    auto* texture = reinterpret_cast<void**>(Block(0x20));
    texture[0] = g_textureVtable;
    return reinterpret_cast<uint8_t*>(texture);
}

float RandomScale(Lcg& random, float size)
{
    switch (random.Below(9)) {
    case 0: return 0.0f;
    case 1: return -0.0f;
    case 2: return -1.5f;
    case 3: return std::numeric_limits<float>::quiet_NaN();
    case 4: return size ? 1.25f / size : 0.5f;                                   // at the bound
    case 5: return size ? std::nextafter(1.25f / size, 1.0f) : 0.25f;          // just above
    case 6: return size ? 1.1666f / size : 2.0f;
    case 7: return static_cast<float>(random.Below(1000)) / 997.0f;
    default: return 1.0f / static_cast<float>(1 + random.Below(400));
    }
}

void TestEngineSprite()
{
    GameMemory memory({{0x007CC979, 1}});
    StandIns& s = *new StandIns();
    StandInObjects(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 40; ++round) {
        ok &= CompareTwoRuns("sprite", reported, [&](bool original) {
            g_trace.clear();
            g_calls = 0;
            ResetArena(0xEE);
            Lcg random(0x5B71 + round);
            auto* sprite = reinterpret_cast<Nx::CSprite*>(Block(sizeof(Nx::CSprite)));
            for (uint32_t i = 0; i < sizeof(Nx::CSprite); ++i)
                reinterpret_cast<uint8_t*>(sprite)[i] = static_cast<uint8_t>(random.Next());
            auto result = [&](uint32_t value) { Record({0xEE00, Relocate(value)}); };
            result(At<uint32_t(__thiscall*)(void*, void*)>(original, 0x0049ED60)(sprite, Block(4)));
            for (uint32_t i = 0; i < sizeof(Nx::CSprite); i += 4) // (as made: the setters overwrite it later)
                Record({Relocate(*reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(sprite) + i))});
            if (round == 0) {
                // every texture size with every kind of scale, about the bounds
                for (uint32_t width : {0u, 1u, 2u, 3u, 5u, 7u, 64u, 100u, 640u}) {
                    for (uint32_t height : {0u, 1u, 3u, 5u, 7u, 32u, 480u}) {
                        for (int kind = 0; kind < 12; ++kind) {
                            sprite->vtable = g_objectVtable;
                            sprite->mp_texture = kind == 11 ? nullptr : MakeTexture();
                            g_textureWidth = static_cast<uint16_t>(width);
                            g_textureHeight = static_cast<uint16_t>(height);
                            Lcg kinds(static_cast<uint32_t>(kind));
                            float xscale = RandomScale(kinds, sprite->mp_texture ? static_cast<float>(width) : 1.0f);
                            float yscale = RandomScale(kinds, sprite->mp_texture ? static_cast<float>(height) : 1.0f);
                            At<void(__thiscall*)(void*, float, float)>(original, 0x0049EE00)(sprite, xscale, yscale);
                            Record({std::bit_cast<uint32_t>(sprite->m_xscale), std::bit_cast<uint32_t>(sprite->m_yscale)});
                            for (int k = 4; k <= 6; ++k) {
                                kinds.state = static_cast<uint32_t>(k);
                                float both[2];
                                for (int axis = 0; axis < 2; ++axis) {
                                    float size = sprite->mp_texture ? static_cast<float>(axis ? height : width) : 1.0f;
                                    both[axis] = k == 4 ? (size ? 1.25f / size : 0.5f)
                                               : k == 5 ? (size ? std::nextafter(1.25f / size, 1.0f) : 0.25f)
                                                        : (size ? std::nextafter(1.1666f / size, 9.0f) : 2.0f);
                                }
                                At<void(__thiscall*)(void*, float, float)>(original, 0x0049EE00)(sprite, both[0], both[1]);
                                Record({std::bit_cast<uint32_t>(sprite->m_xscale), std::bit_cast<uint32_t>(sprite->m_yscale)});
                            }
                        }
                    }
                }
            }
            for (int step = 0; step < 30; ++step) {
                sprite->vtable = g_objectVtable;
                uint32_t word = random.Next() ^ (random.Next() << 11);
                uint32_t byteArg = random.Pick({0u, 1u, 0x100u, 0x101u, 0xFFFFFF00u});
                float f = static_cast<float>(static_cast<int>(word & 0xFFF) - 0x800) / 64.0f;
                switch (random.Below(17)) {
                case 0:
                    At<void(__thiscall*)(void*, uint32_t)>(original, 0x0049EBB0)(sprite, byteArg);
                    break;
                case 1:
                    g_textureWidth = static_cast<uint16_t>(random.Next());
                    g_textureHeight = static_cast<uint16_t>(random.Next());
                    At<void(__thiscall*)(void*, void*)>(original, 0x0049EBC0)(sprite, random.Below(4) ? MakeTexture()
                                                                                                     : nullptr);
                    break;
                case 2:
                    At<void(__thiscall*)(void*, uint32_t)>(original, 0x0049EC00)(sprite, word);
                    break;
                case 3:
                    At<void(__thiscall*)(void*, float, float)>(original, 0x0049EC10)(sprite, f, -f * 2);
                    break;
                case 4:
                    At<void(__thiscall*)(void*, uint32_t, uint32_t)>(original, 0x0049EC30)(sprite, word,
                                                                                         word * 7 + 0x30000);
                    break;
                case 5:
                case 6:
                case 7: {
                    // the texture (or none) and its size, then a scale about the bounds
                    if (random.Below(4) == 0) {
                        sprite->mp_texture = nullptr;
                    } else {
                        sprite->mp_texture = MakeTexture();
                        g_textureWidth = static_cast<uint16_t>(random.Pick({0u, 1u, 2u, 3u, 7u, 64u, 100u, 640u}));
                        g_textureHeight = static_cast<uint16_t>(random.Pick({0u, 1u, 3u, 5u, 32u, 480u}));
                    }
                    float width = sprite->mp_texture ? g_textureWidth : 1.0f;
                    float height = sprite->mp_texture ? g_textureHeight : 1.0f;
                    float xscale = RandomScale(random, width);
                    float yscale = RandomScale(random, height);
                    At<void(__thiscall*)(void*, float, float)>(original, 0x0049EE00)(sprite, xscale, yscale);
                    break;
                }
                case 8:
                    At<void(__thiscall*)(void*, uint32_t, uint32_t)>(original, 0x0049EC50)(sprite, byteArg, word);
                    break;
                case 9:
                    At<void(__thiscall*)(void*, float, float)>(original, 0x0049EC70)(sprite, f, f + 0.5f);
                    break;
                case 10:
                    At<void(__thiscall*)(void*, float)>(original, 0x0049EC90)(sprite, f);
                    break;
                case 11:
                    At<void(__thiscall*)(void*, float)>(original, 0x0049ECA0)(sprite, f);
                    break;
                case 12:
                    At<void(__thiscall*)(void*, uint32_t)>(original, 0x0049ECC0)(sprite, word);
                    break;
                case 13:
                    At<void(__thiscall*)(void*, uint32_t)>(original, 0x0049ECE0)(sprite, byteArg);
                    break;
                case 14:
                    At<void(__thiscall*)(void*, uint32_t)>(original, 0x0049ECF0)(sprite, word);
                    break;
                case 15:
                    result(At<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x0049EDD0)(sprite, byteArg));
                    result(reinterpret_cast<uint32_t>(sprite->vtable));
                    break;
                default:
                    At<void(__thiscall*)(void*)>(original, 0x0049EBA0)(sprite);
                    result(reinterpret_cast<uint32_t>(sprite->vtable));
                    break;
                }
            }
            std::vector<uint32_t> trace = g_trace;
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "engine sprite: construct, setters (texture size, scale bounds, flags), destroy");
}

void TestEngineQuad()
{
    GameMemory memory({{0x007CC979, 1}});
    StandIns& s = *new StandIns();
    StandInObjects(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 20; ++round) {
        ok &= CompareTwoRuns("textured quad", reported, [&](bool original) {
            g_trace.clear();
            g_calls = 0;
            ResetArena(0xEE);
            Lcg random(0x9AD + round);
            GameVariable<bool>(0x007CC979) = true; // (the memory manager ready: no set-up)
            auto result = [&](uint32_t value) { Record({0xEE00, Relocate(value)}); };
            auto* quad = reinterpret_cast<Nx::CTexturedQuad*>(Block(sizeof(Nx::CTexturedQuad)));
            for (uint32_t i = 0; i < sizeof(Nx::CTexturedQuad); ++i)
                reinterpret_cast<uint8_t*>(quad)[i] = static_cast<uint8_t>(random.Next());
            g_failAllocations = {random.Below(3) == 0}; // (the new quad's)
            result(At<uint32_t(__thiscall*)(void*)>(original, 0x0049EF30)(quad));
            result(At<uint32_t(__thiscall*)(void*, void*)>(original, 0x0049EED0)(quad, Block(4)));
            for (int step = 0; step < 20; ++step) {
                quad->vtable = g_objectVtable;
                uint32_t word = random.Next() ^ (random.Next() << 11);
                uint32_t byteArg = random.Pick({0u, 1u, 0x100u, 0x101u, 0xFFFFFF00u});
                switch (random.Below(6)) {
                case 0:
                    At<void(__thiscall*)(void*, uint32_t)>(original, 0x0049ED10)(quad, byteArg);
                    break;
                case 1:
                    At<void(__thiscall*)(void*, void*)>(original, 0x0049ED40)(quad, MakeTexture());
                    break;
                case 2:
                    At<void(__thiscall*)(void*, uint32_t)>(original, 0x0049ED00)(quad, word);
                    break;
                case 3:
                    quad->m_useDepth = true;
                    At<void(__thiscall*)(void*, float)>(original, 0x0049ED20)(quad, static_cast<float>(word & 0xFF));
                    break;
                case 4:
                    At<void(__thiscall*)(void*, uint32_t)>(original, 0x0049ED50)(quad, word);
                    break;
                default:
                    result(At<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x0049EF00)(quad, byteArg));
                    result(reinterpret_cast<uint32_t>(quad->vtable));
                    break;
                }
            }
            std::vector<uint32_t> trace = g_trace;
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "engine textured quad: construct, a new one (out of memory too), setters, destroy");
}

// --- the screen effects -----------------------------------------------------------------------------------------

struct Entry {
    uint32_t value;
};
std::map<uint32_t, Entry> g_params; // what the structure has, by name (a value, or an array's address)

void StandInStructGetters(StandIns& s)
{
    using Script::CArray;
    using Script::CStruct;
    auto getter = [](uint32_t tag) {
        return [tag](Regs& r) {
            Record({tag, r.args[0], Relocate(r.args[1]), r.args[2]});
            auto it = g_params.find(r.args[0]);
            if (it == g_params.end())
                return 0xFFFFFF00u; // (false in AL, noise above)
            *reinterpret_cast<uint32_t*>(r.args[1]) = it->second.value;
            return 0xFFFFFF01u;
        };
    };
    s.At({0x00476630, AddressOf(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))}, 12,
         getter(0x6630));
    s.At({0x004766D0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, float*, int)>(&CStruct::GetFloat))}, 12,
         getter(0x66D0));
    s.At({0x00476860, AddressOf(static_cast<bool (CStruct::*)(uint32_t, CArray**, int)>(&CStruct::GetArray))}, 12,
         getter(0x6860));
    s.At({0x0046CC30, AddressOf(&CArray::GetElement)}, 4, [](Regs& r) {
        Record({0xCC30, Relocate(r.ecx), r.args[0]});
        return reinterpret_cast<uint32_t*>(r.ecx)[r.args[0]];
    });
}

void TestScreenEffects()
{
    StandIns& s = *new StandIns();
    StandInObjects(s);
    StandInStructGetters(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    constexpr uint32_t kNames[] = {0xF649D637, 0xF68014D9, 0x6F894563, 0x29461E27, 0x99A9B716,
                                   0xE568F442, 0x3B909E50, 0xB7462B1D, 0x0F3BF443, 0xFC5C7D61};
    for (int round = 0; round < 60; ++round) {
        ok &= CompareTwoRuns("screen effects", reported, [&](bool original) {
            g_trace.clear();
            g_calls = 0;
            ResetArena(0xEE);
            Lcg random(0x5CEE + round);
            auto result = [&](uint32_t value) { Record({0xEE00, Relocate(value)}); };
            auto* effects = reinterpret_cast<Nx::CScreenEffects*>(Block(sizeof(Nx::CScreenEffects)));
            std::memset(effects, 0x5A, sizeof(Nx::CScreenEffects));
            result(At<uint32_t(__thiscall*)(void*)>(original, 0x0049E2F0)(effects));
            for (int step = 0; step < 12; ++step) {
                // a structure with some of the names: integers / floats / arrays of three (noise above the bytes)
                g_params.clear();
                for (uint32_t name : kNames) {
                    if (random.Below(3) == 0)
                        continue;
                    uint32_t value = random.Below(4) == 0 ? 0xFFFFFFFFu : random.Next() ^ (random.Next() << 9);
                    if (name == 0x29461E27 || name == 0x99A9B716 || name == 0x0F3BF443 || name == 0xFC5C7D61) {
                        auto* elements = reinterpret_cast<uint32_t*>(Block(12));
                        for (int i = 0; i < 3; ++i)
                            elements[i] = random.Next() ^ (random.Next() << 8);
                        // (0x29461E27 is a float too for the depth of field: the array's address read as one)
                        value = reinterpret_cast<uint32_t>(elements);
                    }
                    g_params[name] = {value};
                }
                if (g_params.count(0x3B909E50) && !g_params.count(0xE568F442))
                    g_params[0xE568F442] = {0x40490FDB}; // (the original passes this one's variable for both)
                effects->vtable = g_effectsVtable;
                auto* params = reinterpret_cast<Script::CStruct*>(Block(16));
                switch (random.Below(5)) {
                case 0:
                    result(At<uint32_t(__thiscall*)(void*, void*, int)>(original, 0x0049E360)(effects, params, 7) &
                           0xFF);
                    break;
                case 1:
                    result(At<uint32_t(__thiscall*)(void*, void*, void*)>(original, 0x0049E4B0)(effects, params,
                                                                                               nullptr) &
                           0xFF);
                    break;
                case 2:
                    result(At<uint32_t(__thiscall*)(void*, void*, void*)>(original, 0x0049E310)(effects, params,
                                                                                               nullptr) &
                           0xFF);
                    result(At<uint32_t(__thiscall*)(void*, void*, void*)>(original, 0x0049E320)(effects, params,
                                                                                               nullptr) &
                           0xFF);
                    result(At<uint32_t(__thiscall*)(void*, void*, void*)>(original, 0x0049E310)(effects, params,
                                                                                               nullptr) &
                           0xFF);
                    break;
                case 3:
                    result(At<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x0049E330)(effects,
                                                                                         random.Below(2) ? 1 : 2));
                    result(reinterpret_cast<uint32_t>(effects->vtable));
                    break;
                default:
                    At<void(__thiscall*)(void*)>(original, 0x0049E300)(effects);
                    result(reinterpret_cast<uint32_t>(effects->vtable));
                    break;
                }
            }
            std::vector<uint32_t> trace = g_trace;
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "screen effects: on / off, the bloom and depth-of-field structures, destroy");
}

} // namespace

SELFTEST(Early, TestEngineSprite, RemoveStandIns);
SELFTEST(Early, TestEngineQuad, RemoveStandIns);
SELFTEST(Early, TestScreenEffects, RemoveStandIns);
