// Self-tests for the engine's frame source, font and text (src/game/gfx/frame_source.cpp, engine_font.cpp,
// engine_text.cpp): original and ours on objects in an arena with recording vtables, the memory manager, the asset
// manager and the font calls stood in; the calls, results and memory compared.

#include <windows.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/ass/assman.h"
#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/gfx/engine_text.h"
#include "game/gfx/font.h"
#include "game/gfx/frame_source.h"
#include "game/gfx/pc_font.h"
#include "game/lst/hashtable.h"
#include "game/obj/cutscene.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;

void* g_textVtable[9];
const char* g_scriptName; // what the stand-in GetString gives (null: no name)

void StandInTextCalls(StandIns& s)
{
    StandInMemory(s);
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        Record({0x1C30, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0u;
    });
    s.At({0x00407C00, AddressOf(&Ass::CAssMan::GetAssetData)}, 8, [](Regs& r) {
        Record({0x7C00, r.ecx, r.args[0], r.args[1]});
        return r.args[0] * 3u;
    });
    s.At({0x004645E0, AddressOf(&Obj::AnimSourceFree)}, 0, [](Regs& r) {
        Record({0x45E0, Relocate(r.args[0])});
        return 0u;
    });
    s.At({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, [](Regs& r) {
        Record({0xB0E0, r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [](Regs& r) {
        Record({0xB110, r.ecx});
        return 0u;
    });
    s.At({0x005B80D0, AddressOf(&Lst::HashTable::FlushItem)}, 4, [](Regs& r) {
        Record({0x80D0, r.ecx, *reinterpret_cast<uint32_t*>(r.args[0])});
        return 0u;
    });
    s.At({0x004B4090, AddressOf(&Nx::UnloadPcFont)}, 0, [](Regs& r) {
        Record({0x4090, r.args[0]});
        return 0u;
    });
    s.At({0x00476C60, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, const char**, int)>(
                           &Script::CStruct::GetString))},
         12, [](Regs& r) {
             Record({0x6C60, r.ecx, r.args[0], r.args[2]});
             if (!g_scriptName)
                 return 0u;
             *reinterpret_cast<const char**>(r.args[1]) = g_scriptName;
             return 1u;
         });
    for (int slot = 0; slot < 9; ++slot) {
        uint16_t pop = slot == 8 ? 12 : 0;
        g_textVtable[slot] = s.Thunk(pop, [slot](Regs& r) {
            Record({0xF00u + static_cast<uint32_t>(slot), Relocate(r.ecx)});
            if (slot == 8)
                Record({r.args[0], r.args[1], r.args[2]});
            return 0x77u;
        });
    }
}

float RandomFloat(Lcg& random)
{
    switch (random.Below(10)) {
    case 0:
        return std::numeric_limits<float>::quiet_NaN();
    case 1:
        return 1.0e12f; // (out of range for 16 bits, and for 64)
    case 2:
        return -2.5f;
    case 3:
        return 1.99999f;
    default:
        return static_cast<float>(static_cast<int>(random.Below(4000)) - 2000) / 1000.0f;
    }
}

// The frame source, the font's base and the text: rounds of random calls on objects in the arena.
void TestEngineTextAndFont()
{
    GameMemory& memory = *new GameMemory({{0x007CC979, 1}, {0x007D1800, 4}, {0x006918DC, 4}, {0x006FA804, 4}});
    StandIns& s = *new StandIns();
    StandInTextCalls(s);
    g_relocate = ArenaRelocate;
    alignas(16) static uint8_t manager[0x600];
    bool ok = true, reported = false;
    for (int round = 0; round < 80; ++round) {
        ok &= CompareTwoRuns("text and font", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Zero();
            std::memset(manager, 0, sizeof(manager));
            *reinterpret_cast<uint32_t*>(manager + 0x11C) = 0x7E110000; // the front end's heap
            GameVariable<uint8_t>(0x007CC979) = 1;
            GameVariable<uint8_t*>(0x007D1800) = manager;
            GameVariable<uint32_t>(0x006918DC) = 0x7A550000;
            Lcg random(0x7E00 + round);
            std::vector<uint32_t> trace;
            // The free list: 0-3 texts.
            std::vector<Nx::CText*> texts;
            for (int i = static_cast<int>(random.Below(4)); i > 0; --i) {
                auto* text = reinterpret_cast<Nx::CText*>(Bump(sizeof(Nx::CText)));
                text->m_vtable = g_textVtable;
                text->mp_string = nullptr;
                text->mp_effects = nullptr;
                text->mp_nextFree = GameVariable<Nx::CText*>(0x006FA804);
                GameVariable<Nx::CText*>(0x006FA804) = text;
            }
            auto* source = reinterpret_cast<Nx::CFrameSource*>(Bump(sizeof(Nx::CFrameSource)));
            auto* font = reinterpret_cast<uint32_t*>(Bump(0x10));
            for (int step = 0; step < 24; ++step) {
                g_trace.clear();
                switch (random.Below(22)) {
                case 0:
                    trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*)>(original, 0x0048CF70)(source)));
                    break;
                case 1:
                    trace.push_back(ArenaRelocate(
                        Fn<uint32_t(__thiscall*)(void*, unsigned int)>(original, 0x0048D000)(source, random.Below(4))));
                    source->vtable = reinterpret_cast<void**>(0x00648A68);
                    break;
                case 2:
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*, int)>(original, 0x0048CFA0)(
                        source, random.Below(2) ? 0x100 * static_cast<int>(random.Below(3)) : 1));
                    break;
                case 3:
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x0048CFD0)(
                        source, random.Below(3) ? random.Next() : 0));
                    break;
                case 4:
                    trace.push_back(
                        Fn<uint32_t(__cdecl*)(uint32_t, int)>(original, 0x0048CF50)(random.Below(3) ? random.Next() : 0, 7));
                    break;
                case 5: // the font's base: made, unloaded, a string's size, destroyed
                    trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*)>(original, 0x0048D020)(font)));
                    trace.push_back(font[0]);
                    trace.push_back(font[1]);
                    font[0] = reinterpret_cast<uint32_t>(g_textVtable);
                    font[1] = random.Next();
                    Fn<void(__thiscall*)(void*)>(original, 0x0048D040)(font);
                    Fn<void(__thiscall*)(void*, uint32_t, uint32_t, uint32_t)>(original, 0x0048D060)(font, 1, 2, 3);
                    Fn<void(__thiscall*)(void*, int, int)>(original, 0x0048D070)(font, 1, 2);
                    Fn<void(__thiscall*)(void*, int)>(original, 0x0048D080)(font, 1);
                    if (random.Below(2)) {
                        Fn<void(__thiscall*)(void*)>(original, 0x0048D030)(font);
                    } else {
                        trace.push_back(ArenaRelocate(
                            Fn<uint32_t(__thiscall*)(void*, unsigned int)>(original, 0x0048D1F0)(font, random.Below(4))));
                    }
                    break;
                case 6: { // a text made
                    auto* text = reinterpret_cast<Nx::CText*>(Bump(sizeof(Nx::CText)));
                    trace.push_back(ArenaRelocate(
                        Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x0048D220)(text, 0x7D000000 + step)));
                    trace.push_back(reinterpret_cast<uint32_t>(text->m_vtable));
                    text->m_vtable = g_textVtable;
                    texts.push_back(text);
                    break;
                }
                case 7: { // from the free list
                    auto* text = reinterpret_cast<Nx::CText*>(Fn<uint32_t(__cdecl*)()>(original, 0x0048D1D0)());
                    trace.push_back(ArenaRelocate(reinterpret_cast<uint32_t>(text)));
                    if (text)
                        texts.push_back(text);
                    break;
                }
                case 8:
                    if (!texts.empty()) {
                        size_t i = random.Below(texts.size());
                        Fn<void(__cdecl*)(void*)>(original, 0x0048D5F0)(texts[i]);
                        texts.erase(texts.begin() + static_cast<long>(i));
                    }
                    break;
                case 9:
                    if (!texts.empty()) {
                        size_t i = random.Below(texts.size());
                        Nx::CText* text = texts[i];
                        texts.erase(texts.begin() + static_cast<long>(i));
                        if (random.Below(2))
                            Fn<void(__thiscall*)(void*)>(original, 0x0048D620)(text);
                        else
                            trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, unsigned int)>(
                                original, 0x0048D670)(text, random.Below(4))));
                    }
                    break;
                case 10:
                    if (!texts.empty())
                        Fn<void(__thiscall*)(void*)>(original, 0x0048D5A0)(texts[random.Below(texts.size())]);
                    break;
                case 11:
                    if (!texts.empty()) {
                        static const char* const strings[] = {"", "Hello", "a much longer string of text"};
                        Fn<void(__thiscall*)(void*, const char*)>(original, 0x0048D530)(
                            texts[random.Below(texts.size())], strings[random.Below(3)]);
                    }
                    break;
                case 12:
                    if (!texts.empty())
                        Fn<void(__thiscall*)(void*, int, int, int)>(original, 0x0048D280)(
                            texts[random.Below(texts.size())], static_cast<int>(random.Below(96)),
                            static_cast<int>(random.Next()), static_cast<int>(random.Next()));
                    break;
                case 13:
                    if (!texts.empty())
                        Fn<void(__thiscall*)(void*, int, float, float)>(original, 0x0048D360)(
                            texts[random.Below(texts.size())], static_cast<int>(random.Below(96)), RandomFloat(random),
                            RandomFloat(random));
                    break;
                case 14:
                    if (!texts.empty())
                        Fn<void(__thiscall*)(void*, int, float)>(original, 0x0048D450)(
                            texts[random.Below(texts.size())], static_cast<int>(random.Below(96)), RandomFloat(random));
                    break;
                default: // the setters
                    if (!texts.empty()) {
                        void* t = texts[random.Below(texts.size())];
                        int b = static_cast<int>(random.Next());
                        float f1 = RandomFloat(random), f2 = RandomFloat(random), f3 = RandomFloat(random);
                        switch (random.Below(11)) {
                        case 0:
                            Fn<void(__thiscall*)(void*, int)>(original, 0x0048D0C0)(t, b);
                            break;
                        case 1:
                            Fn<void(__thiscall*)(void*, int)>(original, 0x0048D0D0)(t, b);
                            break;
                        case 2:
                            Fn<void(__thiscall*)(void*, float, float)>(original, 0x0048D0E0)(t, f1, f2);
                            break;
                        case 3:
                            Fn<void(__thiscall*)(void*, float, float)>(original, 0x0048D100)(t, f1, f2);
                            break;
                        case 4:
                            Fn<void(__thiscall*)(void*, float, float, float)>(original, 0x0048D120)(t, f1, f2, f3);
                            break;
                        case 5:
                            Fn<void(__thiscall*)(void*, float)>(original, 0x0048D140)(t, f1);
                            break;
                        case 6:
                            Fn<void(__thiscall*)(void*, float)>(original, 0x0048D160)(t, f1);
                            break;
                        case 7:
                            Fn<void(__thiscall*)(void*, int, int)>(original, 0x0048D180)(t, static_cast<int>(random.Next()), b);
                            break;
                        case 8:
                            Fn<void(__thiscall*)(void*, int)>(original, 0x0048D1A0)(t, b);
                            break;
                        default:
                            Fn<void(__thiscall*)(void*, int)>(original, 0x0048D1B0)(t, b);
                            break;
                        }
                    }
                    break;
                }
                trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            }
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    delete &memory;
    g_relocate = nullptr;
    Check(ok, "text and font: the frame source, the font's base, texts made / freed / set (strings, character "
              "effects, every setter)");
}

// The loaded fonts' table: unloaded by name (and the command), looked up by checksum (a hash table built by hand,
// with chains and null values), the button glyphs.
void TestEngineFontTable()
{
    GameMemory& memory = *new GameMemory({{0x006FA830, 16 * 0x1C}, {0x006FA9F0, sizeof(Lst::HashTable)}, {0x006FA808, 32}});
    StandIns& s = *new StandIns();
    StandInTextCalls(s);
    g_relocate = ArenaRelocate;
    static const char* const names[] = {"small", "dialog", "title", "ButtonsXbox", "none"};
    bool ok = true, reported = false;
    for (int round = 0; round < 60; ++round) {
        ok &= CompareTwoRuns("font table", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Zero();
            Lcg random(0x7F00 + round);
            std::vector<uint32_t> trace;
            auto* slots = &GameVariable<Nx::FontSlot>(0x006FA830);
            for (int i = 0; i < 16; ++i) {
                if (random.Below(3)) {
                    std::memcpy(slots[i].name, names[random.Below(5)], 12);
                    slots[i].font = random.Below(4) ? reinterpret_cast<Nx::CFont*>(0x7F000000 + i) : nullptr;
                }
            }
            // The table: 2^2 buckets, items keyed by the names' checksums (some with no value, some chained).
            auto& table = GameVariable<Lst::HashTable>(0x006FA9F0);
            table.m_bits = 2;
            auto* buckets = reinterpret_cast<Lst::HashTable::Item*>(Bump(4 * sizeof(Lst::HashTable::Item)));
            std::memset(buckets, 0, 4 * sizeof(Lst::HashTable::Item));
            table.m_buckets = buckets; // (null buckets: the original reads through null)
            for (int i = 0; i < 16; ++i) {
                if (!random.Below(3) || !table.m_buckets)
                    continue;
                uint32_t key = Crc::GenerateCRCFromString(names[random.Below(5)]);
                Lst::HashTable::Item* bucket = &buckets[key & 3];
                if (bucket->key || bucket->value || random.Below(4) == 0) {
                    auto* item = reinterpret_cast<Lst::HashTable::Item*>(Bump(sizeof(Lst::HashTable::Item)));
                    *item = {key, random.Below(5) ? &slots[i] : nullptr, bucket->next};
                    bucket->next = item;
                } else {
                    *bucket = {key, random.Below(5) ? &slots[i] : nullptr, nullptr};
                }
            }
            for (int i = 0; i < 32; ++i)
                GameVariable<char>(0x006FA808 + i) = static_cast<char>(random.Next());
            for (int step = 0; step < 8; ++step) {
                g_trace.clear();
                const char* name = names[random.Below(5)];
                switch (random.Below(4)) {
                case 0:
                    trace.push_back(ArenaRelocate(
                        Fn<uint32_t(__cdecl*)(uint32_t)>(original, 0x0048D760)(Crc::GenerateCRCFromString(name))));
                    break;
                case 1:
                    Fn<void(__cdecl*)(const char*)>(original, 0x0048D6C0)(name);
                    break;
                case 2:
                    g_scriptName = name;
                    trace.push_back(Fn<uint32_t(__cdecl*)(void*, void*)>(original, 0x0048D7B0)(
                                        reinterpret_cast<void*>(0x7C000000), nullptr) &
                                    0xFF);
                    break;
                default: {
                    static const char* const digits[] = {"0", "9", "a", "f", "1f", "A"};
                    trace.push_back(static_cast<uint8_t>(
                        Fn<char(__cdecl*)(const char*)>(original, 0x0048D6A0)(digits[random.Below(6)])));
                    break;
                }
                }
                trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            }
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    delete &memory;
    g_relocate = nullptr;
    Check(ok, "font table: looked up, unloaded (and UnloadFont), button glyphs");
}

} // namespace

SELFTEST(Early, TestEngineTextAndFont, RemoveStandIns);
SELFTEST(Early, TestEngineFontTable, RemoveStandIns);
