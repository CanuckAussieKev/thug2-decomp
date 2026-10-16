// Self-tests for the PC-port-patched font and loading bar functions (src/game/gfx): the original as the PC port
// leaves it (its patch bytes written in for the test) against ours.

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <utility>
#include <vector>

#include <windows.h>

#include "game/core/crc.h"
#include "game/gfx/font.h"
#include "game/gfx/glyphs.h"
#include "game/gfx/loading_bar.h"
#include "game/lst/hashtable.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"


namespace {

// Bytes written over game code (or data) for the length of a test, put back when it goes.
struct TestPatch {
    uint8_t* at;
    std::vector<uint8_t> saved;
    TestPatch(uint32_t address, const void* bytes, size_t length)
        : at(reinterpret_cast<uint8_t*>(address)), saved(at, at + length)
    {
        DWORD old;
        VirtualProtect(at, length, PAGE_EXECUTE_READWRITE, &old);
        std::memcpy(at, bytes, length);
        FlushInstructionCache(GetCurrentProcess(), at, length);
    }
    ~TestPatch()
    {
        std::memcpy(at, saved.data(), saved.size());
        FlushInstructionCache(GetCurrentProcess(), at, saved.size());
    }
};

// A jump written over a game function's start for the length of a test (copied from selftest.cpp).
struct TestDetour {
    uint8_t* at;
    uint8_t saved[5];
    TestDetour(uint32_t address, const void* to) : at(reinterpret_cast<uint8_t*>(address))
    {
        DWORD old;
        VirtualProtect(at, 5, PAGE_EXECUTE_READWRITE, &old);
        std::memcpy(saved, at, 5);
        at[0] = 0xE9;
        int32_t relative = static_cast<int32_t>(reinterpret_cast<uintptr_t>(to) - (address + 5));
        std::memcpy(at + 1, &relative, 4);
        FlushInstructionCache(GetCurrentProcess(), at, 5);
    }
    ~TestDetour()
    {
        std::memcpy(at, saved, 5);
        FlushInstructionCache(GetCurrentProcess(), at, 5);
    }
};

// Game memory saved before a test and put back after (also by the cleanup if the test crashes).
struct SavedRange {
    uint32_t address;
    uint32_t size;
    std::vector<uint8_t> bytes;
};
std::vector<SavedRange> g_glyph_saved;

void SaveRanges(std::initializer_list<std::pair<uint32_t, uint32_t>> ranges)
{
    g_glyph_saved.clear();
    for (auto [address, size] : ranges) {
        auto* p = reinterpret_cast<uint8_t*>(address);
        g_glyph_saved.push_back({address, size, std::vector<uint8_t>(p, p + size)});
    }
}

void RestoreGlyphRanges()
{
    for (SavedRange& s : g_glyph_saved) {
        DWORD old;
        VirtualProtect(reinterpret_cast<void*>(s.address), s.size, PAGE_EXECUTE_READWRITE, &old);
        std::memcpy(reinterpret_cast<void*>(s.address), s.bytes.data(), s.size);
    }
    g_glyph_saved.clear();
}

uint32_t Bits(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}

// --- the loading bar ---------------------------------------------------------------------------------------

void TestLoadingBar()
{
    uint8_t* gradient = Nx::LoadingBarGradientBuffer();
    auto buffer = reinterpret_cast<uint32_t>(gradient);
    SaveRanges({{0x0067DDBC, 4}, {0x0067DDCC, 12}, {0x00786D6C, 4}, {0x00786D80, 4}, {0x00701680, 8},
                {0x00705288, 24}, {buffer - 4, 4 + 0x9000}, {0x004B6C64, 4}});
    // The PC port's DWord patch (the table's base -> the bigger buffer).
    uint32_t base = buffer;
    TestPatch patch(0x004B6C64, &base, 4);

    struct Case {
        int screenWidth, barWidth;
        float scale;
        uint32_t start, end, border;
    };
    const Case cases[] = {
        {640, 400, 1.0f, 0x00102030, 0x00FFF0E0, 0x11223344},   // rising
        {1920, 333, 3.0f, 0x80FF7F01, 0x00020304, 0xFFFFFFFF},  // falling, high resolution (~3000 entries)
        {800, 7, 1.25f, 0x00000000, 0x00FF00FF, 0x00000080},     // short
        {640, 0, 1.0f, 0x00123456, 0x00654321, 0x00ABCDEF},      // no steps
        {640, -50, 1.0f, 0x00123456, 0x00654321, 0x00ABCDEF},    // negative width
        {1024, 255, 1.6f, 0x00010203, 0x00010203, 0x00F0F0F0},   // flat
        {1280, 101, 1.999f, 0x00FE01FD, 0x0001FE02, 0x00010101}, // rounding of both conversions
    };
    std::vector<uint32_t> traces[2];
    for (int original = 1; original >= 0; --original) {
        std::vector<uint32_t>& trace = traces[original];
        for (const Case& c : cases) {
            std::memset(reinterpret_cast<void*>(buffer - 4), 0xCD, 4 + 0x9000);
            std::memset(reinterpret_cast<void*>(0x00705288), 0xEE, 24);
            GameVariable<int>(0x00786D6C) = c.screenWidth;
            GameVariable<int>(0x0067DDBC) = c.barWidth;
            GameVariable<float>(0x00786D80) = c.scale;
            GameVariable<uint32_t>(0x0067DDCC) = c.start;
            GameVariable<uint32_t>(0x0067DDD0) = c.end;
            GameVariable<uint32_t>(0x0067DDD4) = c.border;
            if (original)
                Original<void(__cdecl*)()>(0x004B6B80)();
            else
                Nx::UpdateLoadingBarColors();
            for (uint32_t a = 0x00705288; a < 0x007052A0; a += 4)
                trace.push_back(GameVariable<uint32_t>(a));
            trace.push_back(GameVariable<uint32_t>(0x00701680));
            trace.push_back(GameVariable<uint32_t>(0x00701684));
            // The table: the original (as the PC port patches it) puts it 4 bytes before the buffer, ours at its
            // start; the word after it must be untouched.
            const uint8_t* table = original ? gradient - 4 : gradient;
            for (uint32_t i = 0; i < 0x9000 / 4; ++i)
                trace.push_back(reinterpret_cast<const uint32_t*>(table)[i]);
            if (!original)
                trace.push_back(*reinterpret_cast<uint32_t*>(buffer - 4)); // ours leaves this alone
            else
                trace.push_back(0xCDCDCDCD);
        }
    }
    RestoreGlyphRanges();
    Check(traces[0] == traces[1] && traces[0].size() > 1000,
          "loading bar: gradient, width, border colour and table (the PC port's buffer)");
    for (size_t i = 0, shown = 0; i < traces[0].size() && i < traces[1].size() && shown < 8; ++i) {
        if (traces[0][i] != traces[1][i]) {
            Log("  loading bar entry %u: original %08X ours %08X", static_cast<unsigned>(i), traces[1][i],
                traces[0][i]);
            ++shown;
        }
    }
}

// --- fonts ---------------------------------------------------------------------------------------------------

// Stand-in fonts: the setters' virtuals record their calls.
struct StandInFont {
    void** vtable;
    uint32_t padding[3];
};
StandInFont g_fonts[24];
int g_fonts_made;
std::vector<uint32_t>* g_font_trace;
int g_colors_to_record; // how many colour entries the script gave (the rest are garbage in the original)

uint32_t FontIndex(const void* font)
{
    auto* f = static_cast<const StandInFont*>(font);
    return f >= g_fonts && f < g_fonts + 24 ? 0xF0000000u + static_cast<uint32_t>(f - g_fonts)
                                            : reinterpret_cast<uint32_t>(font);
}

void __fastcall StandInSetSpacings(StandInFont* self, void*, int charSpacing, int spaceSpacing)
{
    g_font_trace->insert(g_font_trace->end(), {0x5E7u, FontIndex(self), static_cast<uint32_t>(charSpacing),
                                               static_cast<uint32_t>(spaceSpacing)});
}
void __fastcall StandInSetColorTable(StandInFont* self, void*, const uint32_t* colors)
{
    g_font_trace->insert(g_font_trace->end(), {0xC01u, FontIndex(self)});
    for (int i = 0; i < g_colors_to_record; ++i)
        g_font_trace->push_back(colors[i]);
}
void __fastcall StandInMarkAsButtonFont(StandInFont* self, void*, uint32_t buttonFont)
{
    g_font_trace->insert(g_font_trace->end(), {0xB07u, FontIndex(self), buttonFont & 0xFF});
}
void* g_font_vtable[5] = {nullptr, nullptr, reinterpret_cast<void*>(&StandInSetSpacings),
                          reinterpret_cast<void*>(&StandInSetColorTable),
                          reinterpret_cast<void*>(&StandInMarkAsButtonFont)};

void* __cdecl StandInLoadFontFile(const char* name)
{
    g_font_trace->push_back(0x10ADu);
    g_font_trace->push_back(Crc::GenerateCRCFromString(name));
    StandInFont* font = &g_fonts[g_fonts_made++];
    font->vtable = g_font_vtable;
    return font;
}

Lst::HashTable::Item g_font_buckets[1 << 12];

void TestLoadFont()
{
    using namespace Script;
    SaveRanges({{0x006FA808, 0x21}, {0x006FA830, 0x1C * 16}, {0x006FA9F0, sizeof(Lst::HashTable)},
                {0x006F3624, 4}, {0x0048D8F8, 4}, {0x0048D983, 4}, {0x004B40A0, 5}});
    uint8_t savedStyle = Nx::GlyphStyle();
    TestDetour loadFile(0x004B40A0, reinterpret_cast<const void*>(&StandInLoadFontFile));

    // A stand-in symbol table holding the three meta button maps (different digits each, past 9 and fewer
    // than 32 so the rest default to 0), and one case with none.
    static CSymbolTableEntry* table[0x1000];
    static CSymbolTableEntry symbols[3];
    static CArray maps[3];
    static uint32_t mapDigits[3][24];
    const char* mapNames[3] = {"meta_button_map_xbox", "meta_button_map_ps2", "meta_button_map_gamecube"};
    GameVariable<CSymbolTableEntry**>(0x006F3624) = table;

    // The parameters: built by hand (no allocation).
    static CComponent components[8];
    static CArray colorTab, colors[4];
    static uint32_t colorElements[4][5] = {{1, 2, 3, 4, 0}, {0x105, 6, 7, 0, 0}, {0, 0, 0, 0, 0}, {8, 9, 10, 11, 12}};
    static uint32_t colorPointers[4];
    static CStruct params;
    auto component = [](int i, ESymbolType type, uint32_t name, uint32_t raw, CComponent* next) {
        components[i] = {};
        components[i].typeAndFlag = static_cast<uint8_t>(type << 1);
        components[i].name = name;
        components[i].raw = raw;
        components[i].next = next;
    };
    auto array = [](CArray& a, ESymbolType type, uint32_t size, uint32_t* elements) {
        a = {};
        a.m_type = type;
        a.m_size = size;
        if (size == 1)
            a.m_single = elements[0];
        else
            a.m_elements = elements;
    };

    std::vector<uint32_t> traces[2];
    for (int style = 0; style < 3; ++style) {
        Nx::GlyphStyle() = static_cast<uint8_t>(style);
        // The PC port's DWord patches (patchButtonGlyphs) for this style: the names ours uses.
        uint32_t fontName = style == 0 ? 0x00648AFC : reinterpret_cast<uint32_t>(Nx::ButtonFontName());
        uint32_t mapName = style == 0 ? 0x00648AB8 : reinterpret_cast<uint32_t>(Nx::MetaButtonMapName());
        TestPatch fontPatch(0x0048D983, &fontName, 4);
        TestPatch mapPatch(0x0048D8F8, &mapName, 4);

        for (int original = 1; original >= 0; --original) {
            std::vector<uint32_t>& trace = traces[original];
            g_font_trace = &trace;
            g_fonts_made = 0;
            std::memset(g_fonts, 0, sizeof(g_fonts));
            std::memset(table, 0, sizeof(table));
            for (int m = 0; m < 3; ++m) {
                for (int i = 0; i < 24; ++i)
                    mapDigits[m][i] = static_cast<uint32_t>((i * 7 + m * 5) % 17);
                array(maps[m], ESYMBOLTYPE_INTEGER, 20 + m, mapDigits[m]);
                symbols[m] = {};
                symbols[m].unknown0 = 1;
                symbols[m].type = ESYMBOLTYPE_ARRAY;
                symbols[m].name = Crc::GenerateCRCFromString(mapNames[m]);
                symbols[m].raw = reinterpret_cast<uint32_t>(&maps[m]);
                table[symbols[m].name & 0xFFF] = &symbols[m];
            }
            auto reset = [&]() {
                std::memset(reinterpret_cast<void*>(0x006FA808), 0x77, 32);
                GameVariable<bool>(0x006FA828) = false;
                std::memset(reinterpret_cast<void*>(0x006FA830), 0, 0x1C * 16);
                std::memset(g_font_buckets, 0, sizeof(g_font_buckets));
                auto& hash = GameVariable<Lst::HashTable>(0x006FA9F0);
                hash.m_bits = 12;
                hash.m_buckets = g_font_buckets;
                hash.m_size = 0;
            };
            auto record = [&]() {
                for (uint32_t a = 0x006FA808; a < 0x006FA830; a += 4)
                    trace.push_back(GameVariable<uint32_t>(a) & (a == 0x006FA828 ? 0xFFu : 0xFFFFFFFFu));
                for (int i = 0; i < 16; ++i) {
                    auto& slot = (&GameVariable<Nx::FontSlot>(0x006FA830))[i];
                    for (int w = 0; w < 6; ++w)
                        trace.push_back(reinterpret_cast<uint32_t*>(slot.name)[w]);
                    trace.push_back(FontIndex(slot.font));
                }
                auto& hash = GameVariable<Lst::HashTable>(0x006FA9F0);
                trace.push_back(static_cast<uint32_t>(hash.m_size));
                for (auto& item : g_font_buckets) {
                    if (item.value || item.next)
                        trace.insert(trace.end(), {item.key, reinterpret_cast<uint32_t>(item.value) - 0x006FA830,
                                                   reinterpret_cast<uint32_t>(item.next)});
                }
            };
            auto loadFont = [&](CStruct* p, int colorCount) {
                g_colors_to_record = colorCount;
                bool result = original ? Original<bool(__cdecl*)(CStruct*, CScript*)>(0x0048D950)(p, nullptr)
                                       : Nx::CFunc_LoadFont(p, nullptr);
                trace.push_back(0xF0F0F000u | (result ? 1u : 0u));
                record();
            };
            auto loadDirect = [&](const char* name, int charSpacing, int spaceSpacing, uint32_t* c, bool button) {
                g_colors_to_record = c ? 16 : 0;
                if (original)
                    Original<void(__cdecl*)(const char*, int, int, uint32_t*, bool)>(0x0048D7E0)(
                        name, charSpacing, spaceSpacing, c, button);
                else
                    Nx::LoadFont(name, charSpacing, spaceSpacing, c, button);
                record();
            };

            // 1. A new font: name, both spacings (truncated, one negative), a colour table of 4 entries (3, 1
            //    and 5 bytes - the 5th wraps onto the 1st - and an empty one); loads the meta map.
            reset();
            for (int i = 0; i < 4; ++i)
                colorPointers[i] = reinterpret_cast<uint32_t>(&colors[i]);
            array(colors[0], ESYMBOLTYPE_INTEGER, 3, colorElements[0]);
            array(colors[1], ESYMBOLTYPE_INTEGER, 1, colorElements[1]);
            array(colors[2], ESYMBOLTYPE_INTEGER, 0, colorElements[2]);
            array(colors[3], ESYMBOLTYPE_INTEGER, 5, colorElements[3]);
            array(colorTab, ESYMBOLTYPE_ARRAY, 4, colorPointers);
            component(4, ESYMBOLTYPE_ARRAY, Crc::GenerateCRCFromString("color_tab"),
                      reinterpret_cast<uint32_t>(&colorTab), nullptr);
            component(3, ESYMBOLTYPE_FLOAT, Crc::GenerateCRCFromString("space_spacing"), Bits(-3.75f), &components[4]);
            component(2, ESYMBOLTYPE_FLOAT, Crc::GenerateCRCFromString("char_spacing"), Bits(2.9f), &components[3]);
            component(1, ESYMBOLTYPE_STRING, 0, reinterpret_cast<uint32_t>("fontA"), &components[2]);
            params.Init();
            params.m_head = &components[1];
            loadFont(&params, 4);
            // 2. The same font again: updated, not reloaded (only char spacing; no colours given).
            component(1, ESYMBOLTYPE_STRING, 0, reinterpret_cast<uint32_t>("fontA"), &components[2]);
            component(2, ESYMBOLTYPE_FLOAT, Crc::GenerateCRCFromString("char_spacing"), Bits(7.5f), nullptr);
            loadFont(&params, 0);
            // 3. The button font: the name is replaced (the PC port's style), flag after the name.
            component(1, ESYMBOLTYPE_STRING, 0, reinterpret_cast<uint32_t>("ignored"), &components[0]);
            component(0, ESYMBOLTYPE_NAME, 0, Crc::GenerateCRCFromString("buttons_font"), nullptr);
            loadFont(&params, 0);
            // 4. More than 16 colours: only 16 taken.
            static uint32_t manyPointers[20];
            for (int i = 0; i < 20; ++i)
                manyPointers[i] = reinterpret_cast<uint32_t>(&colors[i % 2 ? 3 : 0]);
            array(colorTab, ESYMBOLTYPE_ARRAY, 20, manyPointers);
            component(1, ESYMBOLTYPE_STRING, 0, reinterpret_cast<uint32_t>("fontMany"), &components[4]);
            loadFont(&params, 16);
            // 5. Directly: a font with no colour table, button-font flag set, then the same font updated.
            static uint32_t direct[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
            mapDigits[style][0] += 3; // (the map is loaded: a new font mustn't load it again)
            loadDirect("fontC", 3, -4, nullptr, true);
            loadDirect("fontC", -1, 9, direct, false);
            loadDirect("fontC", 0, 0, nullptr, true);
            // 6. Fill every slot, then one more: nothing loaded, the map still loads (here: not yet loaded,
            //    and the style's map missing from the table, so it stays unloaded), then with the map back.
            static char names[20][8];
            for (int i = 0; i < 20; ++i) {
                names[i][0] = 'f';
                names[i][1] = static_cast<char>('a' + i);
                names[i][2] = static_cast<char>('A' + (i * 3) % 26);
                names[i][3] = '\0';
            }
            for (int i = 0; i < 13; ++i)
                loadDirect(names[i], i, -i, nullptr, (i & 1) != 0);
            GameVariable<bool>(0x006FA828) = false;
            CSymbolTableEntry* styleMap = table[symbols[style].name & 0xFFF];
            table[symbols[style].name & 0xFFF] = nullptr;
            loadDirect(names[13], 1, 1, nullptr, false);
            table[symbols[style].name & 0xFFF] = styleMap;
            loadDirect(names[14], 1, 1, nullptr, false);
            // 7. A fresh start with the map array shorter than 32 and the slots already full.
            reset();
            for (int i = 0; i < 16; ++i)
                (&GameVariable<Nx::FontSlot>(0x006FA830))[i].font = reinterpret_cast<Nx::CFont*>(&g_fonts[23]);
            loadDirect("fontZ", 2, 2, direct, true);
            trace.push_back(static_cast<uint32_t>(g_fonts_made));
        }
    }
    Nx::GlyphStyle() = savedStyle;
    RestoreGlyphRanges();
    Check(traces[0] == traces[1] && traces[0].size() > 3000,
          "LoadFont / CFunc_LoadFont: new and loaded fonts, setters, colour tables, slots, meta button map, "
          "the PC port's glyph styles");
    if (traces[0].size() != traces[1].size())
        Log("  font trace sizes: original %u ours %u", static_cast<unsigned>(traces[1].size()),
            static_cast<unsigned>(traces[0].size()));
    for (size_t i = 0, shown = 0; i < traces[0].size() && i < traces[1].size() && shown < 8; ++i) {
        if (traces[0][i] != traces[1][i]) {
            Log("  font entry %u: original %08X ours %08X", static_cast<unsigned>(i), traces[1][i], traces[0][i]);
            ++shown;
        }
    }
}

} // namespace

SELFTEST(Early, TestLoadingBar, RestoreGlyphRanges);
SELFTEST(Early, TestLoadFont, RestoreGlyphRanges);
