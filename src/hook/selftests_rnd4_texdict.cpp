// Self-tests for the engine's texture dictionary and the loaded dictionaries (src/game/gfx/texture_dict.cpp,
// RND4-4): the original and ours over stand-in dictionaries and textures with recording vtables, real hash tables
// in an arena (their pool items and memory stood in), the platform's dictionary loaders, the name splitter, the
// game's sprintf and the memory contexts stood in; random operations, every result, call and the arena compared.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/core/pool.h"
#include "game/gfx/engine_texture.h"
#include "game/gfx/pc_texture.h"
#include "game/gfx/texture_dict.h"
#include "game/lst/hashtable.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;
using Nx::CTexDict;
using Nx::CTexture;

void* g_dictVtable[12];
void* g_textureVtable[48];
uint32_t g_calls;
Lcg* g_random; // the stand-ins' own choices (the same sequence in both runs)
bool g_mustLoad; // the dictionary's loads succeed

uint8_t* Block(uint32_t size) { return reinterpret_cast<uint8_t*>(Bump(size)); }

const uint32_t kChecksums[] = {0x1111, 0x2222, 0x3333, 0x1119, 0x2229, 0x3339, 0x4444, 0x5555};

CTexture* MakeTexture(uint32_t checksum)
{
    auto* texture = reinterpret_cast<CTexture*>(Block(0x14));
    texture->vtable = g_textureVtable;
    texture->m_checksum = checksum;
    texture->m_flags = g_random->Next() ^ (g_random->Next() << 9);
    return texture;
}
Lst::HashTable* MakeTable(int bits)
{
    auto* table = reinterpret_cast<Lst::HashTable*>(Block(sizeof(Lst::HashTable)));
    table->m_bits = bits;
    table->m_buckets = reinterpret_cast<Lst::HashTable::Item*>(Block(12u << bits));
    std::memset(table->m_buckets, 0, 12u << bits);
    table->m_size = 0;
    table->m_iterBucket = 0x55;
    table->m_iterItem = nullptr;
    table->m_flag = false;
    return table;
}
CTexDict* MakeDict(uint32_t checksum, bool table)
{
    auto* dict = reinterpret_cast<CTexDict*>(Block(0x14));
    dict->vtable = g_dictVtable;
    dict->m_checksum = checksum;
    dict->m_refCount = 1;
    dict->mp_textures = table ? MakeTable(2) : nullptr;
    return dict;
}

void StandInTexDict(StandIns& s)
{
    StandInMemory(s);
    s.At({0x006234FE}, 0, [](Regs& r) { return StandInSprintf(r); });
    s.At({0x005BC450, AddressOf(Mem::PoolManager::CreateItem)}, 0, [](Regs& r) {
        Record({0xC450, r.args[0]});
        return Bump(12);
    });
    s.At({0x005BC4A0, AddressOf(Mem::PoolManager::FreeItem)}, 0, [](Regs& r) {
        Record({0xC4A0, r.args[0], Relocate(r.args[1])});
        return 0u;
    });
    s.At({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, [](Regs& r) {
        Record({0xB0E0, Relocate(r.ecx), Relocate(r.args[0])});
        return 0u;
    });
    s.At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [](Regs& r) {
        Record({0xB110, Relocate(r.ecx)});
        return 0u;
    });
    // the name split: base = the name up to its first '.', a skin if it has an odd length
    s.At({0x0048BFE0}, 0, [](Regs& r) {
        const char* name = reinterpret_cast<const char*>(r.args[0]);
        char* base = reinterpret_cast<char*>(r.args[1]);
        size_t i = 0;
        for (; name[i] && name[i] != '.'; ++i)
            base[i] = name[i];
        base[i] = 0;
        Record({0xBFE0, Hash(name)});
        return 0xFFFFFF00u | (std::strlen(name) & 1);
    });
    s.At({0x004B6920}, 0, [](Regs& r) {
        Record({0x6920, Relocate(r.args[0])});
        return 1u;
    });
    // the platform's dictionaries: one made (named / from memory) with its table
    s.At({0x004C7270, AddressOf(Nx::LoadTexDictFile)}, 0, [](Regs& r) {
        Record({0x7270, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return reinterpret_cast<uint32_t>(MakeDict(0xD1C7, true));
    });
    s.At({0x004C72E0, AddressOf(Nx::LoadTexDictMemory)}, 0, [](Regs& r) {
        Record({0x72E0, r.args[0], Relocate(r.args[1])});
        return reinterpret_cast<uint32_t>(MakeDict(0xD1C8, true));
    });
    // The dictionary's slots: [1] / [2] / [3] a new texture (sometimes none), [5] unload (true or false), [8] a copy.
    const uint16_t dictPops[12] = {4, 12, 24, 24, 4, 4, 4, 4, 8, 4, 4, 4};
    for (int slot = 0; slot < 12; ++slot) {
        uint16_t pop = dictPops[slot];
        g_dictVtable[slot] = s.Thunk(pop, [slot, pop](Regs& r) {
            Record({0xA000u + slot, Relocate(r.ecx)});
            for (int i = 0; i < pop / 4; ++i)
                Record({slot == 1 && i == 0 ? Hash(reinterpret_cast<const char*>(r.args[0])) : Relocate(r.args[i])});
            switch (slot) {
            case 1:
            case 2:
            case 3:
            case 8:
                if (g_random->Below(5) == 0 && !g_mustLoad)
                    return 0u;
                return reinterpret_cast<uint32_t>(MakeTexture(kChecksums[g_random->Below(8)]));
            case 5:
                return g_random->Below(4) ? 0xFFFFFF01u : 0xFFFFFF00u;
            default:
                return ++g_calls;
            }
        });
    }
    // The textures' slots: [0] destroy, [4] a copy of another (true / false), [41] an overlay.
    for (int slot = 0; slot < 48; ++slot) {
        uint16_t pop = slot == 41 ? 8 : (slot == 0 || slot == 4) ? 4 : 0;
        g_textureVtable[slot] = s.Thunk(pop, [slot, pop](Regs& r) {
            Record({0xB000u + slot, Relocate(r.ecx)});
            for (int i = 0; i < pop / 4; ++i)
                Record({Relocate(r.args[i])});
            if (slot == 0)
                return r.ecx;
            return g_random->Below(2) ? 0xFFFFFF01u : 0xFFFFFF00u;
        });
    }
}

template <typename F>
F At(bool original, uint32_t address)
{
    return Fn<F>(original, address);
}

void TestTexDict()
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}, {0x00700F04, sizeof(Lst::HashTable)}, {0x00700F20, 4}});
    StandIns& s = *new StandIns();
    StandInTexDict(s);
    g_relocate = ArenaRelocate;
    const char* names[] = {"skate.png", "dir/x_y.img", "a\\b\\c.png", "sky", "../up.png", "wall01.png", "", "abcd",
                           "abcde"};
    bool ok = true, reported = false;
    for (int round = 0; round < 40; ++round) {
        ok &= CompareTwoRuns("texture dictionary", reported, [&](bool original) {
            g_trace.clear();
            g_calls = 0;
            ResetArena(0xEE);
            Lcg random(0x7D1C + round);
            Lcg standIns(0x51A0 + round);
            g_random = &standIns;
            GameVariable<bool>(0x007CC979) = true;
            uint8_t* manager = Block(sizeof(Mem::Manager));
            *reinterpret_cast<uint32_t*>(manager + 0x110) = 0x70D0 + round; // its top-down heap
            GameVariable<uint8_t*>(0x007D1800) = manager;
            // the loaded dictionaries' table, and the particle dictionary
            Lst::HashTable* loaded = MakeTable(3);
            std::memcpy(reinterpret_cast<void*>(0x00700F04), loaded, sizeof(Lst::HashTable));
            std::vector<CTexDict*> dicts = {MakeDict(0xAAAA, true), MakeDict(0xBBBB, true)};
            GameVariable<CTexDict*>(0x00700F20) = dicts[1];
            auto result = [&](uint32_t value) { Record({0xEE00, Relocate(value)}); };
            std::vector<CTexture*> textures;
            // some textures in the dictionaries (under multiples of names' CRCs too, for FindTexture)
            for (CTexDict* dict : dicts) {
                for (int i = 0; i < 6; ++i) {
                    uint32_t key = kChecksums[random.Below(8)];
                    auto crc = [](const char* text) {
                        return GameFunction<uint32_t(__cdecl*)(const char*)>(0x00401B90)(text);
                    };
                    const char* name = names[random.Below(9)];
                    switch (random.Below(5)) {
                    case 0: // FindTexture's keys: the name's CRC times a multiplier (5 isn't one)
                        key = crc(name) * static_cast<uint32_t>(random.Pick({2, 3, 4, 6, 12, 8, 24, 5}));
                        break;
                    case 1: { // ...or its "_auto32m0.png" variant's
                        char other[64];
                        std::snprintf(other, sizeof(other), "%s", name);
                        size_t length = std::strlen(other);
                        if (length > 4)
                            std::snprintf(other + length - 4, sizeof(other) - (length - 4), "_auto32m0.png");
                        key = crc(other) * static_cast<uint32_t>(random.Pick({2, 3, 24}));
                        break;
                    }
                    case 2: { // GetTextureByName's: the CRC after the last slash
                        const char* file = name;
                        for (const char* p = name; *p; ++p) {
                            if (*p == '/' || *p == '\\')
                                file = p + 1;
                        }
                        key = crc(file);
                        break;
                    }
                    default:
                        break;
                    }
                    CTexture* texture = MakeTexture(key);
                    dict->mp_textures->PutItem(key, texture);
                    textures.push_back(texture);
                }
            }
            // every name looked up both ways in a dictionary holding each under its file name's CRC, and one under
            // the bare "_auto32m0.png"'s (a four-character name's variant)
            {
                CTexDict* dict = dicts[0];
                auto add = [&](uint32_t key) {
                    CTexture* texture = MakeTexture(key);
                    dict->mp_textures->PutItem(key, texture);
                    textures.push_back(texture);
                };
                for (const char* name : names) {
                    const char* file = name;
                    for (const char* p = name; *p; ++p) {
                        if (*p == '/' || *p == '\\')
                            file = p + 1;
                    }
                    add(GameFunction<uint32_t(__cdecl*)(const char*)>(0x00401B90)(file) ^ (round & 1));
                }
                add(GameFunction<uint32_t(__cdecl*)(const char*)>(0x00401B90)("_auto32m0.png") * 2);
                for (const char* name : names) {
                    result(At<uint32_t(__thiscall*)(void*, const char*)>(original, 0x0049F560)(dict, name));
                    result(At<uint32_t(__thiscall*)(void*, const char*)>(original, 0x0049F660)(dict, name));
                }
            }
            for (int step = 0; step < 24; ++step) {
                CTexDict* dict = dicts[random.Below(static_cast<uint32_t>(dicts.size()))];
                CTexture* texture = textures[random.Below(static_cast<uint32_t>(textures.size()))];
                const char* name = names[random.Below(9)];
                uint32_t word = random.Next();
                uint32_t byteArg = random.Pick({0u, 1u, 0x100u, 0xFFFFFF01u});
                switch (random.Below(22)) {
                case 0:
                    result(At<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x0049F540)(dict, kChecksums[word & 7]));
                    break;
                case 1:
                    result(At<uint32_t(__thiscall*)(void*, const char*)>(original, 0x0049F560)(dict, name));
                    break;
                case 2:
                    result(At<uint32_t(__thiscall*)(void*, const char*)>(original, 0x0049F660)(dict, name));
                    break;
                case 3: {
                    uint32_t added = At<uint32_t(__thiscall*)(void*, const char*, uint32_t, uint32_t, uint32_t)>(
                        original, 0x0049F9B0)(dict, name, word, 7, byteArg);
                    result(added);
                    if (added)
                        textures.push_back(reinterpret_cast<CTexture*>(added));
                    break;
                }
                case 4: {
                    uint32_t added = At<uint32_t(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                                             uint32_t, uint32_t)>(original, 0x0049FA20)(
                        dict, 1, 2, word, 4, 5, byteArg, 7);
                    result(added);
                    if (added)
                        textures.push_back(reinterpret_cast<CTexture*>(added));
                    break;
                }
                case 5: {
                    uint32_t added = At<uint32_t(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                                             uint32_t, uint32_t)>(original, 0x0049FAA0)(
                        dict, word, 2, 3, 4, 5, 6, byteArg);
                    result(added);
                    if (added)
                        textures.push_back(reinterpret_cast<CTexture*>(added));
                    break;
                }
                case 6:
                    result(At<uint32_t(__thiscall*)(void*, void*)>(original, 0x0049F500)(dict, texture) & 0xFF);
                    break;
                case 7:
                case 8:
                    At<void(__thiscall*)(void*, uint32_t)>(original, 0x0049F5C0)(dict, byteArg);
                    break;
                case 9:
                    result(At<uint32_t(__thiscall*)(void*, const char*, void*)>(original, 0x0049F800)(dict, name,
                                                                                                      texture) &
                           0xFF);
                    break;
                case 10:
                    g_mustLoad = true; // (a failed load is read through null)
                    result(At<uint32_t(__thiscall*)(void*, const char*, const char*)>(original, 0x0049FB20)(
                               dict, name, names[word % 9]) &
                           0xFF);
                    g_mustLoad = false;
                    break;
                case 11: {
                    uint32_t added = At<uint32_t(__thiscall*)(void*, uint32_t, void*)>(original, 0x0049FBE0)(
                        dict, kChecksums[word & 7], texture);
                    result(added);
                    if (added)
                        textures.push_back(reinterpret_cast<CTexture*>(added));
                    break;
                }
                case 12: {
                    uint32_t added = At<uint32_t(__thiscall*)(void*, uint32_t, void*, void*, uint32_t)>(
                        original, 0x0049FC20)(dict, kChecksums[word & 7], texture, textures[0], byteArg);
                    result(added);
                    if (added)
                        textures.push_back(reinterpret_cast<CTexture*>(added));
                    break;
                }
                case 13: {
                    auto* made = reinterpret_cast<CTexDict*>(Block(0x14));
                    std::memset(made, 0xCD, 0x14);
                    g_failAllocations = {random.Below(3) == 0}; // (the table's)
                    if (random.Below(2))
                        result(At<uint32_t(__thiscall*)(void*, uint32_t, uint32_t)>(original, 0x0049F830)(
                            made, word, byteArg));
                    else
                        result(At<uint32_t(__thiscall*)(void*, const char*, uint32_t)>(original, 0x0049F8F0)(
                            made, name, byteArg));
                    g_failAllocations.clear();
                    made->vtable = g_dictVtable;
                    if (made->mp_textures)
                        dicts.push_back(made);
                    break;
                }
                case 14:
                    if (dicts.size() > 1) {
                        dict->vtable = g_dictVtable;
                        if (random.Below(2))
                            At<void(__thiscall*)(void*)>(original, 0x0049FC70)(dict);
                        else
                            result(At<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x0049FCC0)(dict, byteArg));
                        result(reinterpret_cast<uint32_t>(dict->vtable));
                        for (size_t i = 0; i < dicts.size(); ++i) {
                            if (dicts[i] == dict) {
                                dicts.erase(dicts.begin() + static_cast<int>(i));
                                break;
                            }
                        }
                        if (GameVariable<CTexDict*>(0x00700F20) == dict)
                            GameVariable<CTexDict*>(0x00700F20) = dicts[0];
                    }
                    break;
                case 15:
                    result(At<uint32_t(__cdecl*)(uint32_t)>(original, 0x0049F010)(word % 3 ? 0xD0 + (word & 7)
                                                                                         : word));
                    break;
                case 16: {
                    for (int again = 0; again < 2; ++again) // (the second finds the first's)
                        result(At<uint32_t(__cdecl*)(const char*, int, uint32_t, uint32_t, uint32_t)>(
                            original, 0x0049F0C0)(name, 1, word & 3, 5, 6));
                    break;
                }
                case 17:
                    result(At<uint32_t(__cdecl*)(uint32_t, const void*, uint32_t, uint32_t, uint32_t, uint32_t,
                                                 uint32_t)>(original, 0x0049F050)(0xD0 + (word & 3), Block(4), 9, 1,
                                                                                  (word >> 4) & 3, 2, 3));
                    break;
                case 18:
                    result(At<uint32_t(__cdecl*)(const char*, uint32_t, uint32_t)>(original, 0x0049F180)(name,
                                                                                                        word & 3, 4));
                    break;
                case 19: {
                    // a loaded dictionary (or a fresh one counted down to nothing)
                    auto& table = GameVariable<Lst::HashTable>(0x00700F04);
                    CTexDict* found = nullptr;
                    for (int i = 0; i < (1 << table.m_bits) && !found; ++i) {
                        for (auto* item = &table.m_buckets[i]; item && !found; item = item->next)
                            found = static_cast<CTexDict*>(item->value);
                    }
                    if (!found) {
                        found = MakeDict(0x9999, false);
                        found->m_refCount = static_cast<uint16_t>(1 + (word & 1));
                    }
                    result(At<uint32_t(__cdecl*)(void*)>(original, 0x0049EFD0)(found) & 0xFF);
                    break;
                }
                case 20: {
                    char out[64];
                    std::memset(out, 0, sizeof(out));
                    At<void(__cdecl*)(char*, const char*)>(original, 0x0049EF90)(out, name);
                    Record({Hash(out)});
                    break;
                }
                default:
                    At<void(__cdecl*)(uint32_t)>(original, 0x0049EFB0)(byteArg);
                    break;
                }
            }
            for (CTexture* texture : textures)
                Record({texture->m_checksum, texture->m_flags});
            Record({0xF00F});
            GameMemory({{0x00700F04, sizeof(Lst::HashTable)}, {0x00700F20, 4}}).Dump(g_trace);
            std::vector<uint32_t> trace = g_trace;
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "texture dictionary: look-ups, loads, copies, replacements, flushes, the loaded dictionaries");
}

} // namespace

SELFTEST(Early, TestTexDict, RemoveStandIns);
