// Self-tests for the PC fonts and texts (src/game/gfx/pc_font.cpp, RFX-3): the original and ours with stand-ins
// for the bitmap fonts' load / unload / query, the screen texts and their 2D drawing calls, the engine's text
// destructor and font unload, and allocations from an arena; traces and the objects compared.

#include <windows.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/gfx/draw2d.h"
#include "game/gfx/pc_font.h"
#include "game/gfx/text.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;

alignas(16) Nx::SFont g_fonts[2];
uint32_t g_loadResult; // what the stand-in SFont::Load gives: 0 (null) or 1 / 2 (g_fonts[0] / [1])
void** g_textVtable;   // the stand-in screen texts' (slot 0 records their destruction)

void StandInFontCalls(StandIns& s)
{
    StandInMemory(s);
    s.At({0x004CF0F0, AddressOf(&Nx::SFont::Load)}, 0, [](Regs& r) {
        Record({0xF0F0, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1] & 0xFF});
        return g_loadResult ? reinterpret_cast<uint32_t>(&g_fonts[g_loadResult - 1]) : 0u;
    });
    s.At({0x004CFA80, AddressOf(&Nx::SFont::Unload)}, 0, [](Regs& r) {
        Record({0xFA80, Relocate(r.args[0])});
        return 0u;
    });
    s.At({0x004CECB0, AddressOf(&Nx::SFont::QueryString)}, 12, [](Regs& r) {
        Record({0xECB0, Relocate(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0])), Relocate(r.args[1]),
                Relocate(r.args[2])});
        return 0u;
    });
    s.At({0x004CEED0, AddressOf(&Nx::SText::Construct)}, 4, [](Regs& r) {
        Record({0xEED0, Relocate(r.ecx), r.args[0]});
        std::memset(reinterpret_cast<void*>(r.ecx), 0x5A, sizeof(Nx::SText));
        *reinterpret_cast<void***>(r.ecx) = g_textVtable;
        return r.ecx;
    });
    s.At({0x004DFB70, AddressOf(&Nx::SDraw2D::SetHidden)}, 4, [](Regs& r) {
        Record({0xFB70, Relocate(r.ecx), r.args[0] & 0xFF});
        return 0u;
    });
    s.At({0x004DFAE0, AddressOf(&Nx::SDraw2D::SetPriority)}, 4, [](Regs& r) {
        Record({0xFAE0, Relocate(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x004DFB20, AddressOf(&Nx::SDraw2D::SetZ)}, 4, [](Regs& r) {
        Record({0xFB20, Relocate(r.ecx), r.args[0]});
        return 0u;
    });
    StandInThis(s, {0x0048D620}, 0); // the engine text's destructor body (frees its string and effects)
    StandInThis(s, {0x0048D040}, 0); // the engine font's Unload
    static void* vtable[1];
    vtable[0] = s.Thunk(4, [](Regs& r) {
        Record({0xDE57, Relocate(r.ecx), r.args[0]});
        return r.ecx;
    });
    g_textVtable = vtable;
}

uint32_t FontRelocate(uint32_t word)
{
    for (int i = 0; i < 2; ++i) {
        auto base = reinterpret_cast<uint32_t>(&g_fonts[i]);
        if (word >= base && word < base + sizeof(Nx::SFont))
            return 0xF0000000u + 0x1000u * i + (word - base);
    }
    return ArenaRelocate(word);
}

// Fonts: made, loaded (and not), spacings (space width 0 / negative / positive), colours, the button font flag,
// unloaded, the getters and the query, destroyed (freed or not); a font made and loaded; the engine unload.
void TestPcFonts()
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}, {0x00780FDC, 4}});
    StandIns& s = *new StandIns();
    StandInFontCalls(s);
    g_relocate = FontRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 6; ++round) {
        ok &= CompareTwoRuns("pc font: fonts", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0xF0E7 + round);
            GameVariable<uint8_t>(0x007CC979) = 1;
            Lcg random(0xF0 + round);
            for (Nx::SFont& f : g_fonts) {
                auto* bytes = reinterpret_cast<uint8_t*>(&f);
                for (size_t i = 0; i < sizeof(f); ++i)
                    bytes[i] = static_cast<uint8_t>(random.Next());
            }
            std::vector<uint32_t> trace;
            auto* font = reinterpret_cast<Nx::CPcFont*>(Bump(sizeof(Nx::CPcFont)));
            trace.push_back(FontRelocate(Fn<uint32_t(__thiscall*)(void*)>(original, 0x004B3BB0)(font)));
            trace.push_back(FontRelocate(reinterpret_cast<uint32_t>(font->font)));
            g_loadResult = round % 3;
            trace.push_back(Fn<uint32_t(__thiscall*)(void*, const char*)>(original, 0x004B3BD0)(font, "small") & 0xFF);
            font->font = &g_fonts[round & 1];
            int spaces[3] = {0, -3, 7 + round};
            Fn<void(__thiscall*)(void*, int, int)>(original, 0x004B3C00)(font, 0x12345 + round, spaces[round % 3]);
            uint32_t colors[16];
            for (uint32_t& c : colors)
                c = random.Next();
            Fn<void(__thiscall*)(void*, const uint32_t*)>(original, 0x004B3C30)(font, colors);
            Fn<void(__thiscall*)(void*, int)>(original, 0x004B3CB0)(font, round & 2 ? 0x100 : 0x101);
            trace.push_back(FontRelocate(GameVariable<uint32_t>(0x00780FDC)));
            trace.push_back(Fn<uint32_t(__thiscall*)(void*)>(original, 0x004B3CF0)(font));
            trace.push_back(Fn<uint32_t(__thiscall*)(void*)>(original, 0x004B3D00)(font));
            float w, h;
            Fn<void(__thiscall*)(void*, const char*, float*, float*)>(original, 0x004B3D10)(font, "text", &w, &h);
            if (round == 4)
                Fn<void(__thiscall*)(void*)>(original, 0x004B3CD0)(font);
            if (round == 5)
                font->font = nullptr;
            trace.push_back(FontRelocate(
                Fn<uint32_t(__thiscall*)(void*, unsigned int)>(original, 0x004B4060)(font, round == 1 ? 2 : round & 1)));
            Fn<void(__cdecl*)(void*)>(original, 0x004B4090)(font);
            g_loadResult = 1;
            trace.push_back(FontRelocate(Fn<uint32_t(__cdecl*)(const char*)>(original, 0x004B40A0)("dialog")));
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            for (Nx::SFont& f : g_fonts)
                trace.push_back(HashBytes(reinterpret_cast<const uint8_t*>(&f), sizeof(f)));
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "pc font: the PC font's members, made and loaded, the engine's unload");
}

// Texts: made one by one and as a pool, every property sent (with and without a font,
// z on and off, hidden or not), updated, destroyed with and without a screen text.
void TestPcTexts()
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}, {0x006FA804, 4}});
    StandIns& s = *new StandIns();
    StandInFontCalls(s);
    g_relocate = FontRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 8; ++round) {
        ok &= CompareTwoRuns("pc font: texts", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0x7E47 + round);
            GameVariable<uint8_t>(0x007CC979) = 1;
            Lcg random(0x7E + round);
            std::vector<uint32_t> trace;
            auto* text = reinterpret_cast<Nx::CPcText*>(Bump(sizeof(Nx::CPcText)));
            trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*)>(original, 0x004B3E70)(text)));
            // every field of the text set, then sent
            auto* words = reinterpret_cast<uint32_t*>(text);
            for (int i = 1; i < 0x12; ++i)
                words[i] = random.Next();
            alignas(16) static Nx::CPcFont fontObject;
            fontObject.font = &g_fonts[round & 1];
            text->font = round & 1 ? &fontObject : nullptr;
            text->z = round == 3 ? 0xFFFFFFFFu : random.Next();
            text->zEnabled = (round & 2) != 0;
            text->hidden = (round & 4) != 0;
            alignas(16) static uint8_t screenText[0x44];
            auto* screen = reinterpret_cast<Nx::SText*>(screenText);
            std::memset(screen, 0x33, 0x44);
            screen->vtable = g_textVtable;
            text->text = round == 7 ? nullptr : screen;
            if (round != 7) {
                Fn<void(__thiscall*)(void*)>(original, 0x004B3F50)(text);
                Fn<void(__thiscall*)(void*)>(original, 0x004B3DB0)(text);
                Fn<void(__thiscall*)(void*)>(original, 0x004B3DA0)(text);
                trace.push_back(HashBytes(screenText + 4, 0x40));
                // (Update's virtual calls reach the game's code: only its order is ours)
                Fn<void(__thiscall*)(void*)>(original, 0x004B3D80)(text);
            }
            trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, unsigned int)>(original, 0x004B3F20)(
                text, round == 5 ? 2 : round & 1)));
            // the pool (a failed allocation would be written through, in both)
            Fn<void(__cdecl*)(uint32_t)>(original, 0x004B3FD0)(round == 0 ? 0 : round);
            trace.push_back(ArenaRelocate(GameVariable<uint32_t>(0x006FA804)));
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "pc font: the PC text's members and the texts' pool");
}

} // namespace

SELFTEST(Early, TestPcFonts, RemoveStandIns);
SELFTEST(Early, TestPcTexts, RemoveStandIns);
