// Self-tests for the PartyMod-patched text and Direct3D start-up functions (src/game/gfx/text.cpp, device.cpp):
// the original as PartyMod leaves it (its patch bytes written in for the test) against ours.

#include <windows.h>

#include <d3d9.h>

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "game/gfx/device.h"
#include "game/gfx/text.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

extern "C" uint8_t isUsingKeyboard;
extern "C" uint8_t forceGlyphs;
extern "C" uint32_t dehexifyDigitWrapper(uint8_t* button);
extern "C" uint8_t __cdecl shouldUseGlyph(uint8_t index);
extern "C" uint8_t isWindowed;
extern "C" int resX;
extern "C" int resY;

namespace {

// Game memory (code or data) saved before a test and put back after - also by the cleanup if the test crashes.
struct SavedRange {
    uint32_t address;
    std::vector<uint8_t> bytes;
};
std::vector<SavedRange> g_text_saved;

void Save(uint32_t address, uint32_t size)
{
    auto* p = reinterpret_cast<uint8_t*>(address);
    g_text_saved.push_back({address, std::vector<uint8_t>(p, p + size)});
}

void RestoreTextRanges()
{
    for (size_t i = g_text_saved.size(); i-- > 0;) {
        SavedRange& s = g_text_saved[i];
        DWORD old;
        VirtualProtect(reinterpret_cast<void*>(s.address), s.bytes.size(), PAGE_EXECUTE_READWRITE, &old);
        std::memcpy(reinterpret_cast<void*>(s.address), s.bytes.data(), s.bytes.size());
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(s.address), s.bytes.size());
    }
    g_text_saved.clear();
}

// Bytes written over game code (or the import table) until the cleanup.
void Write(uint32_t address, const std::vector<uint8_t>& bytes)
{
    Save(address, static_cast<uint32_t>(bytes.size()));
    DWORD old;
    VirtualProtect(reinterpret_cast<void*>(address), bytes.size(), PAGE_EXECUTE_READWRITE, &old);
    std::memcpy(reinterpret_cast<void*>(address), bytes.data(), bytes.size());
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), bytes.size());
}
std::vector<uint8_t> Dword(uint32_t value)
{
    return {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value >> 16),
            static_cast<uint8_t>(value >> 24)};
}
// A jump (0xE9) or call (0xE8) at `from` to `to`.
std::vector<uint8_t> Branch(uint8_t opcode, uint32_t from, const void* to)
{
    std::vector<uint8_t> bytes = Dword(reinterpret_cast<uint32_t>(to) - (from + 5));
    bytes.insert(bytes.begin(), opcode);
    return bytes;
}
void JumpTo(uint32_t from, const void* to) { Write(from, Branch(0xE9, from, to)); }

uint32_t Bits(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}

// PartyMod's button glyph patch (glyph.c's patchButtonGlyphs) for one function: the call at `call` goes to
// dehexifyDigitWrapper; the test at `test` becomes PUSH EAX / CALL shouldUseGlyph / CMP AL,1 / POP EAX /
// JZ <glyph> / 3 NOPs.
void WritePartyModGlyphs(uint32_t call, uint32_t test, uint8_t jump)
{
    Write(call, Branch(0xE8, call, reinterpret_cast<void*>(&dehexifyDigitWrapper)));
    std::vector<uint8_t> bytes = {0x67, 0x50};
    std::vector<uint8_t> callGlyph = Branch(0xE8, test + 2, reinterpret_cast<void*>(&shouldUseGlyph));
    bytes.insert(bytes.end(), callGlyph.begin(), callGlyph.end());
    bytes.insert(bytes.end(), {0x3C, 0x01, 0x67, 0x58, 0x74, jump, 0x90, 0x90, 0x90});
    Write(test, bytes);
}

// --- fonts and text ----------------------------------------------------------------------------------------

// Key names for the buttons that aren't glyphs: empty, plain, and one with a character above 0x7F (the
// original indexes the font's map with it as a signed char).
std::vector<uint32_t> g_text_log;
const char* __cdecl StandInKeyName(int button)
{
    g_text_log.insert(g_text_log.end(), {0x4E3Eu, static_cast<uint32_t>(button)});
    switch (button % 4) {
    case 0:
        return "Ctrl";
    case 1:
        return "";
    case 2:
        return "\xFE" "a\xFC Z"; // (map[-2] and map[-4]: bytes of the font's chars pointer)
    default:
        return "Num 5";
    }
}

alignas(16) Nx::SChar g_chars[2][256];
alignas(16) Nx::SFont g_fonts[2]; // the text's font, the button font
Nx::TextVertex g_vertices[0x500];

void __fastcall StandInBeginDraw(Nx::SText*, void*)
{
    g_text_log.push_back(0xBE6u);
    GameVariable<Nx::TextVertex*>(0x00780FE8) = g_vertices;
}
void __fastcall StandInEndDraw(Nx::SText*, void*)
{
    uint32_t count = GameVariable<uint32_t>(0x00780FE4);
    g_text_log.insert(g_text_log.end(), {0xE4Du, count, reinterpret_cast<uint32_t>(GameVariable<void*>(0x00780FE0))});
    const auto* words = reinterpret_cast<const uint32_t*>(g_vertices);
    g_text_log.insert(g_text_log.end(), words, words + count * 7);
    GameVariable<uint32_t>(0x00780FE4) = 0;
}
void* g_text_vtable[4] = {nullptr, reinterpret_cast<void*>(&StandInBeginDraw), nullptr,
                          reinterpret_cast<void*>(&StandInEndDraw)};

void BuildFonts()
{
    for (int f = 0; f < 2; ++f) {
        Nx::SFont& font = g_fonts[f];
        std::memset(&font, 0, sizeof(font));
        font.height = f ? 0x9000001Fu : 23; // (the button font's above 2^31: an unsigned conversion)
        font.base = f ? 3 : 19;
        font.chars = g_chars[f];
        for (int i = 0; i < 256; ++i) {
            Nx::SChar& c = g_chars[f][i];
            c.u0 = 0.001f * static_cast<float>(i) + 0.13f * static_cast<float>(f);
            c.v0 = 0.37f - 0.0007f * static_cast<float>(i);
            c.u1 = c.u0 + 0.0313f;
            c.v1 = c.v0 + 0.0517f + 0.0001f * static_cast<float>(i);
            c.w = static_cast<uint16_t>(3 + (i * 7 + f * 5) % 23);
            c.h = static_cast<uint16_t>(9 + (i * 11) % 17);
            c.baseline = static_cast<uint16_t>((i * 5 + f) % 21); // some above the font's base
            c.pad = 0xEEEE;
            font.map[i] = static_cast<uint8_t>((i * 37 + 11 + f) & 0xFF);
        }
        for (int i = 0; i < 0x20; ++i)
            font.specialMap[i] = static_cast<uint8_t>(200 + i * 3 + f);
        font.charSpacing = static_cast<int16_t>(f ? 2 : -1);
        font.spaceSpacing = static_cast<int16_t>(f ? 9 : 7);
        for (int i = 0; i < 16; ++i)
            font.colors[i] = 0x10203040u * static_cast<uint32_t>(i + 1) ^ 0x8800FF00u;
    }
}

// Texts covering every escape: "\\", colours (0, in range, past the table), specials, buttons plain and through
// the meta map (glyphs and key names, by PartyMod's rules), "\u"/"\l" skips, unknown letters, an escape at the
// end, chains of escapes, and spaces.
const char* const kTexts[] = {
    "Hello world",
    "a\\\\b\\\\",
    "\\c3Red\\c0 plain \\cV far \\cZ \\c1",
    "\\s0\\s5x\\sV",
    "\\b0\\b1\\b2\\b3\\b4\\b5\\b6\\b7\\b8\\b9\\bA\\bB\\bC\\bD\\bE\\bF\\bG\\bH\\bI\\bJ",
    "\\bm\\bn\\bo\\bp\\bq\\br\\bs\\bt\\BM\\BQ\\BT\\bu",
    "\\m0\\m1\\m2\\m3\\m4\\m5\\m6\\m7\\m8\\m9\\mA\\mB\\mC\\mD\\mE\\mF\\M7",
    "pre\\u12mid\\lZ\\U99end\\L",
    "\\x\\y\\Q\\a\\z",
    "\\c2\\l\\u55\\s3tail",
    "\\c4\\b5 \\s2 \\",
    "  \\c5 a  \\b3 b",
    "\\",
    "",
    "\xC4\xFE\x81 high",
    "\\b4\\\\\\s1\\c6\\m2",
    "\\s\\c3x\\b\\u12y\\m\\Lz", // a backslash as the digit: the escape ends there
};

void FillMetaMap()
{
    for (int i = 0; i < 32; ++i)
        GameVariable<char>(0x006FA808 + i) = "4a7dQc9ht0b5kmfrp2s1e8uq6ng3jvlo"[i];
}

// The text's size, for each text x the three PartyMod input states (keyboard, controller, keyboard with
// PreferGlyphs), against the original with PartyMod's patch written in.
void TestQueryString()
{
    Save(0x00780FDC, 4);
    Save(0x006FA808, 32);
    uint8_t savedKeyboard = isUsingKeyboard, savedForce = forceGlyphs;
    JumpTo(0x005BCD50, reinterpret_cast<void*>(&StandInKeyName));
    WritePartyModGlyphs(0x004CED4B, 0x004CED6D, 0x5B);
    BuildFonts();
    FillMetaMap();
    GameVariable<Nx::SFont*>(0x00780FDC) = &g_fonts[1];
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        g_text_log.clear();
        for (int state = 0; state < 3; ++state) {
            isUsingKeyboard = state != 1;
            forceGlyphs = state == 2;
            for (int font = 0; font < 2; ++font) {
                for (size_t t = 0; t < std::size(kTexts); ++t) {
                    const char* text = kTexts[t];
                    g_text_log.push_back(0x7E000000u | static_cast<uint32_t>(state << 16 | font << 8) | static_cast<uint32_t>(t));
                    float width = -1.0f, height = -1.0f;
                    if (run == 0)
                        Original<void(__thiscall*)(Nx::SFont*, const char*, float*, float*)>(0x004CECB0)(
                            &g_fonts[font], text, &width, &height);
                    else
                        g_fonts[font].QueryString(text, &width, &height);
                    g_text_log.insert(g_text_log.end(), {Bits(width), Bits(height)});
                }
            }
        }
        traces[run] = g_text_log;
    }
    isUsingKeyboard = savedKeyboard;
    forceGlyphs = savedForce;
    RestoreTextRanges();
    for (size_t i = 0, shown = 0; i < traces[0].size() && i < traces[1].size() && shown < 6; ++i)
        if (traces[0][i] != traces[1][i]) {
            uint32_t marker = 0;
            for (size_t j = 0; j <= i; ++j)
                if ((traces[0][j] >> 24) == 0x7E)
                    marker = traces[0][j];
            Log("  query string entry %u (state/font/text %06X): original %08X ours %08X", static_cast<unsigned>(i),
                marker & 0xFFFFFF, traces[0][i], traces[1][i]);
            ++shown;
        }
    Check(traces[0] == traces[1] && traces[0].size() > 200, "text: SFont::QueryString (with PartyMod's button glyphs)");
}

// One drawing setup: screen conversion and the text's fields.
struct DrawCase {
    float scaleX, scaleY;
    int offsetX, offsetY;
    float x, y, xscale, yscale, pivotX, pivotY, angle;
    bool effects, rotations, fixedColor;
};

alignas(16) Nx::STextEffect g_effects[600];

void BuildEffects(bool rotations)
{
    for (int i = 0; i < 600; ++i) {
        Nx::STextEffect& e = g_effects[i];
        e.x = static_cast<int16_t>((i * 7) % 13 - 6);
        e.y = static_cast<int16_t>((i * 5) % 11 - 5);
        e.xscale = static_cast<int16_t>(16384 + (i * 1237) % 9000 - 4500);
        e.yscale = static_cast<int16_t>(16384 - (i * 811) % 7000 + 2000);
        e.rotation = rotations && i % 3 ? static_cast<int16_t>((i * 2311) % 40000 - 20000) : 0;
    }
}

// Draw (and DrawKeyChar directly): every text x the PartyMod input states x screen and text setups (scales,
// offsets, a rotated text, a pivot, effects with and without rotations, a fixed colour, a zero scale), the
// vertex buffer filling up (a long text) - the original with PartyMod's patch written in; the vertices recorded
// by a stand-in EndDraw, with the BeginDraw/EndDraw calls, the texture font and the key names asked for.
void TestDrawText()
{
    Save(0x00780FDC, 0x10);
    Save(0x00786D80, 0x10);
    Save(0x006FA808, 32);
    uint8_t savedKeyboard = isUsingKeyboard, savedForce = forceGlyphs;
    JumpTo(0x005BCD50, reinterpret_cast<void*>(&StandInKeyName));
    WritePartyModGlyphs(0x004CFF0C, 0x004CFF36, 0x63);
    BuildFonts();
    FillMetaMap();
    std::string longText;
    for (int i = 0; i < 300; ++i)
        longText += static_cast<char>('!' + (i * 13) % 90);
    longText += "\\b4\\c3\\b0end";
    const DrawCase cases[] = {
        {1.0f, 1.0f, 0, 16, 10.0f, 20.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, false, false, false},
        {1.6f, 1.5f, 0, 24, 37.25f, 101.5f, 0.85f, 1.15f, 0.0f, 0.0f, 0.0f, true, false, false},
        {1.3333334f, 1.25f, 3, 20, 120.7f, 300.3f, 1.2f, 0.9f, 15.5f, -7.25f, 0.4f, false, false, true},
        {3.0f, 2.25f, -5, 36, 600.0f, 470.0f, 0.5f, 0.75f, -40.0f, 12.0f, -2.75f, true, true, false},
        {2.1333334f, 1.5f, 0, 24, 0.0f, 0.0f, 1.7f, 2.3f, 3.0f, 3.0f, 3.14159f, true, true, true},
        // a pivot far away: offsets past 2^24, rounded through floats on one side only
        {1.0f, 1.0f, 0, 16, 37.25f, 11.5f, 1.0f, 1.0f, -1.0e8f, 3.0e7f, 0.0f, false, false, false},
        {1.0f, 1.0f, 0, 16, 5.0f, 5.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, false, false, false}, // zero scale: nothing
        {1.0f, 1.0f, 0, 16, 5.0f, 5.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, false, false, false},
    };
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        g_text_log.clear();
        for (const DrawCase& c : cases) {
            GameVariable<float>(0x00786D80) = c.scaleX;
            GameVariable<float>(0x00786D84) = c.scaleY;
            GameVariable<int>(0x00786D88) = c.offsetX;
            GameVariable<int>(0x00786D8C) = c.offsetY;
            BuildEffects(c.rotations);
            for (int state = 0; state < 3; ++state) {
                isUsingKeyboard = state != 1;
                forceGlyphs = state == 2;
                std::vector<const char*> texts(std::begin(kTexts), std::end(kTexts));
                texts.push_back(longText.c_str());
                for (size_t t = 0; t < texts.size(); ++t) {
                    alignas(16) uint8_t storage[0x44] = {};
                    auto* text = reinterpret_cast<Nx::SText*>(storage);
                    text->vtable = g_text_vtable;
                    text->z = 0.125f + 0.01f * static_cast<float>(t);
                    text->font = &g_fonts[0];
                    text->effects = c.effects ? g_effects : nullptr;
                    text->text = texts[t];
                    text->x = c.x;
                    text->y = c.y;
                    text->xscale = c.xscale;
                    text->yscale = c.yscale;
                    text->pivotX = c.pivotX;
                    text->pivotY = c.pivotY;
                    text->angle = c.angle;
                    text->color = 0xC0806040u + static_cast<uint32_t>(t);
                    text->fixedColor = c.fixedColor;
                    GameVariable<Nx::SFont*>(0x00780FDC) = &g_fonts[1];
                    GameVariable<Nx::SFont*>(0x00780FE0) = nullptr;
                    GameVariable<Nx::TextVertex*>(0x00780FE8) = g_vertices;
                    GameVariable<uint32_t>(0x00780FE4) = static_cast<uint32_t>(t % 3) * 4;
                    std::memset(g_vertices, 0xCD, sizeof(g_vertices));
                    if (run == 0) {
                        OriginalCode keyChar(0x004CFB20);
                        Original<void(__thiscall*)(Nx::SText*)>(0x004CFD00)(text);
                    } else {
                        text->Draw();
                    }
                    StandInEndDraw(text, nullptr); // what's left in the buffer
                    g_text_log.push_back(reinterpret_cast<uint32_t>(GameVariable<void*>(0x00780FE0)));
                    // one key-name character directly, at a fractional x
                    float x = c.x * 1.7f + 0.3f;
                    GameVariable<uint32_t>(0x00780FE4) = t % 2 ? 0x3FC : 0x3F8; // full after this one, or not
                    char ch = texts[t][0] ? texts[t][0] : ' ';
                    if (run == 0)
                        Original<void(__thiscall*)(Nx::SText*, char, float*, float)>(0x004CFB20)(text, ch, &x, c.y + 0.6f);
                    else
                        text->DrawKeyChar(ch, &x, c.y + 0.6f);
                    StandInEndDraw(text, nullptr);
                    g_text_log.push_back(Bits(x));
                }
            }
        }
        traces[run] = g_text_log;
    }
    isUsingKeyboard = savedKeyboard;
    forceGlyphs = savedForce;
    RestoreTextRanges();
    if (traces[0].size() != traces[1].size())
        Log("  draw text trace sizes: original %u ours %u", static_cast<unsigned>(traces[0].size()),
            static_cast<unsigned>(traces[1].size()));
    for (size_t i = 0, shown = 0; i < traces[0].size() && i < traces[1].size() && shown < 8; ++i)
        if (traces[0][i] != traces[1][i]) {
            Log("  draw text entry %u: original %08X ours %08X", static_cast<unsigned>(i), traces[0][i], traces[1][i]);
            ++shown;
        }
    Check(traces[0] == traces[1] && traces[0].size() > 10000, "text: SText::Draw, DrawKeyChar (with PartyMod's button glyphs)");
}

// --- Direct3D start-up ---------------------------------------------------------------------------------------

struct D3DScript {
    bool created;          // Direct3DCreate9 succeeds
    HRESULT displayMode;   // GetAdapterDisplayMode's result
    int failures;          // CreateDevice calls that fail before one succeeds
    HRESULT depthFormat;   // CheckDeviceFormat's result
    int setting;           // the resolution setting (0x007D643C)
    bool multisample;      // 0x007D6434
    bool windowed;         // PartyMod's Windowed
    int width, height;     // PartyMod's window size
    int deviceWidth;       // what the created device's back buffer width becomes (0 = as asked)
};
D3DScript g_d3d;
int g_d3d_failures;
void* g_d3d_vtable[20];
void* g_device_vtable[20];
void* g_d3d_object[2] = {g_d3d_vtable, nullptr};
void* g_device_object[2] = {g_device_vtable, nullptr};

HRESULT __stdcall StandInGetAdapterDisplayMode(void* d3d, UINT adapter, D3DDISPLAYMODE* mode)
{
    g_text_log.insert(g_text_log.end(), {0xD15u, d3d == g_d3d_object, adapter});
    mode->Width = 1;
    return g_d3d.displayMode;
}
HRESULT __stdcall StandInCheckDeviceFormat(void* d3d, UINT adapter, D3DDEVTYPE type, D3DFORMAT format, DWORD usage,
                                          D3DRESOURCETYPE resource, D3DFORMAT check)
{
    g_text_log.insert(g_text_log.end(), {0xCDFu, d3d == g_d3d_object, adapter, static_cast<uint32_t>(type),
                                         static_cast<uint32_t>(format), usage, static_cast<uint32_t>(resource),
                                         static_cast<uint32_t>(check)});
    return g_d3d.depthFormat;
}
HRESULT __stdcall StandInGetCreationParameters(void* device, D3DDEVICE_CREATION_PARAMETERS* p)
{
    g_text_log.insert(g_text_log.end(), {0x6CBu, device == g_device_object});
    p->AdapterOrdinal = 0;
    p->DeviceType = static_cast<D3DDEVTYPE>(2 + g_d3d.setting);
    p->hFocusWindow = nullptr;
    p->BehaviorFlags = 0;
    return D3D_OK;
}
void* __stdcall StandInDirect3DCreate9(UINT version)
{
    g_text_log.insert(g_text_log.end(), {0xC9u, version});
    return g_d3d.created ? g_d3d_object : nullptr;
}
int __stdcall StandInMessageBox(HWND window, const char* text, const char* caption, UINT type)
{
    g_text_log.insert(g_text_log.end(), {0x3B0u, reinterpret_cast<uint32_t>(window), reinterpret_cast<uint32_t>(text),
                                         reinterpret_cast<uint32_t>(caption), type});
    return IDOK;
}
// (the real one also writes the present parameters' size)
HRESULT __cdecl StandInCreateDevice(int width, int height, bool hardware, bool multisample)
{
    g_text_log.insert(g_text_log.end(), {0xCDu, static_cast<uint32_t>(width), static_cast<uint32_t>(height),
                                         static_cast<uint32_t>(hardware), static_cast<uint32_t>(multisample)});
    GameVariable<int>(0x00786A7C) = g_d3d.deviceWidth ? g_d3d.deviceWidth : width;
    GameVariable<int>(0x00786A80) = height;
    GameVariable<void*>(0x007D69E0) = g_device_object;
    return g_d3d_failures-- > 0 ? E_FAIL : D3D_OK;
}
// The game's exit doesn't return (the original doesn't even pop its argument after it): the stand-in throws
// back to the test.
struct GameExit {
    int code;
};
void __cdecl StandInExit(int code)
{
    g_text_log.insert(g_text_log.end(), {0xE817u, static_cast<uint32_t>(code)});
    throw GameExit{code};
}
void __cdecl StandInRenderDefaults() { g_text_log.push_back(0x7D70u); }
void __cdecl StandInTextures() { g_text_log.push_back(0x8420u); }
void __cdecl StandInRegistry() { g_text_log.push_back(0x7B60u); }
void* __cdecl StandInDebugFont() { g_text_log.push_back(0xA1D0u); return reinterpret_cast<void*>(0xDEB0F047); }
void __cdecl StandInLast() { g_text_log.push_back(0xE7C0u); }

// InitDirect3D: Direct3D missing, the display mode failing, every resolution setting (also out of range) x
// multisampling x windowed, devices failing at the first tries / every time (the game's exit), PartyMod's sizes
// (also one past 2^31, converted unsigned), a created device of another width, the depth format check both
// ways - the original with PartyMod's bytes written in, Direct3D, Win32 and the callees stood in.
void TestInitDirect3D()
{
    Save(0x00786A70, 0x330); // present parameters ... screen conversion, 0x00786D90
    Save(0x007D6434, 0x0C);
    Save(0x007D69DC, 0x08);
    Save(0x0067EBCC, 0x08);
    Save(0x007D6A28, 0x04);
    uint8_t savedWindowed = isWindowed;
    int savedX = resX, savedY = resY;
    g_d3d_vtable[8] = reinterpret_cast<void*>(&StandInGetAdapterDisplayMode);
    g_d3d_vtable[10] = reinterpret_cast<void*>(&StandInCheckDeviceFormat);
    g_device_vtable[9] = reinterpret_cast<void*>(&StandInGetCreationParameters);
    JumpTo(0x005BEB14, reinterpret_cast<void*>(&StandInDirect3DCreate9));
    Write(0x00645224, Dword(reinterpret_cast<uint32_t>(&StandInMessageBox)));
    JumpTo(0x004D7C80, reinterpret_cast<void*>(&StandInCreateDevice));
    JumpTo(0x00624AFE, reinterpret_cast<void*>(&StandInExit));
    JumpTo(0x004D7D70, reinterpret_cast<void*>(&StandInRenderDefaults));
    JumpTo(0x004D8420, reinterpret_cast<void*>(&StandInTextures));
    JumpTo(0x004D7B60, reinterpret_cast<void*>(&StandInRegistry));
    JumpTo(0x005BA1D0, reinterpret_cast<void*>(&StandInDebugFont));
    JumpTo(0x005BE7C0, reinterpret_cast<void*>(&StandInLast));
    // PartyMod's bytes that don't depend on the run: no ShowWindow, no SetWindowPos, no ShowCursor loop.
    Write(0x004D869C, {0xEB});
    Write(0x004D8891, std::vector<uint8_t>(14, 0x90));
    Write(0x004D8989, {0xEB});
    GameVariable<uint32_t>(0x007D6A28) = 0x1234ABCD; // the window

    std::vector<D3DScript> scripts = {
        {false, D3D_OK, 0, D3D_OK, 0, false, false, 800, 600, 0},
        {true, E_FAIL, 0, D3D_OK, 0, false, false, 800, 600, 0},
        {true, D3DERR_NOTAVAILABLE, 0, D3D_OK, 2, true, true, 800, 600, 0},
    };
    for (int setting : {-1, 0, 1, 2, 3, 4, 7})
        for (int flags = 0; flags < 4; ++flags)
            scripts.push_back({true, D3D_OK, (setting + 2 * flags) % 5, flags & 1 ? D3D_OK : D3DERR_NOTAVAILABLE,
                               setting, (flags & 1) != 0, (flags & 2) != 0, 1366 + setting, 768 - flags, 0});
    scripts.push_back({true, D3D_OK, 100, D3D_OK, 0, true, false, 1920, 1080, 0}); // nothing works: exit
    scripts.push_back({true, D3D_OK, 100, D3D_OK, 3, false, true, 1024, 576, 0});
    scripts.push_back({true, D3D_OK, 3, D3D_OK, 3, false, false, -1000, 900, 0});  // above 2^31 unsigned
    scripts.push_back({true, D3D_OK, 1, S_FALSE, 1, true, false, 640, 480, 2561}); // another width
    scripts.push_back({true, D3D_OK, 0, D3D_OK, 3, false, false, 1, 1, 0});

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        g_text_log.clear();
        for (const D3DScript& s : scripts) {
            g_d3d = s;
            g_d3d_failures = s.failures;
            for (uint32_t a = 0x00786A70; a < 0x00786DA0; a += 4)
                GameVariable<uint32_t>(a) = 0xA5A5A5A5u ^ a;
            GameVariable<uint32_t>(0x007D69DC) = 0x11111111;
            GameVariable<uint32_t>(0x007D69E0) = 0x22222222;
            GameVariable<uint32_t>(0x0067EBCC) = 0x33333333;
            GameVariable<uint32_t>(0x0067EBD0) = 0x44444444;
            GameVariable<int>(0x007D643C) = s.setting;
            GameVariable<uint8_t>(0x007D6434) = s.multisample;
            GameVariable<uint8_t>(0x00786AB7) = 1; // would maximise the window
            GameVariable<uint32_t>(0x00786A9C) = 0x77; // the Windowed field before it's set
            isWindowed = s.windowed;
            resX = s.width;
            resY = s.height;
            size_t marker = g_text_saved.size();
            if (run == 0) {
                // PartyMod's run-time bytes: its window size, and windowed if set
                Write(0x004D8776, Dword(static_cast<uint32_t>(s.width)));
                Write(0x004D8792, Dword(static_cast<uint32_t>(s.height)));
                if (s.windowed)
                    Write(0x004D8720, {0x05});
                try {
                    Original<void(__cdecl*)()>(0x004D8650)();
                } catch (const GameExit&) {
                    g_text_log.push_back(0xE81Du);
                }
                // put this script's bytes back
                while (g_text_saved.size() > marker) {
                    SavedRange& r = g_text_saved.back();
                    DWORD old;
                    VirtualProtect(reinterpret_cast<void*>(r.address), r.bytes.size(), PAGE_EXECUTE_READWRITE, &old);
                    std::memcpy(reinterpret_cast<void*>(r.address), r.bytes.data(), r.bytes.size());
                    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(r.address), r.bytes.size());
                    g_text_saved.pop_back();
                }
            } else {
                try {
                    Nx::InitDirect3D();
                } catch (const GameExit&) {
                    g_text_log.push_back(0xE81Du);
                }
            }
            for (uint32_t a = 0x00786A70; a < 0x00786DA0; a += 4)
                g_text_log.push_back(GameVariable<uint32_t>(a));
            for (uint32_t a : {0x007D69DCu, 0x007D69E0u, 0x0067EBCCu, 0x0067EBD0u})
                g_text_log.push_back(GameVariable<uint32_t>(a));
        }
        traces[run] = g_text_log;
    }
    isWindowed = savedWindowed;
    resX = savedX;
    resY = savedY;
    RestoreTextRanges();
    if (traces[0].size() != traces[1].size())
        Log("  d3d init trace sizes: original %u ours %u", static_cast<unsigned>(traces[0].size()),
            static_cast<unsigned>(traces[1].size()));
    for (size_t i = 0, shown = 0; i < traces[0].size() && i < traces[1].size() && shown < 8; ++i)
        if (traces[0][i] != traces[1][i]) {
            Log("  d3d init entry %u: original %08X ours %08X", static_cast<unsigned>(i), traces[0][i], traces[1][i]);
            ++shown;
        }
    Check(traces[0] == traces[1] && traces[0].size() > 1000, "gfx: InitDirect3D (with PartyMod's window changes)");
}

} // namespace

SELFTEST(Early, TestQueryString, RestoreTextRanges);
SELFTEST(Early, TestDrawText, RestoreTextRanges);
SELFTEST(Early, TestInitDirect3D, RestoreTextRanges);
