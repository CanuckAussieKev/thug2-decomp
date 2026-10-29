#pragma once

#include <cstdint>

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// Fonts loaded by the scripts (LoadFont), by name, and the meta button map the button glyphs go through.
namespace Nx {

// The engine's font (the base of the PC font, CPcFont in pc_font.h: its first 8 bytes; vtable 0x00648A6C - [0] the
// destructor, [1] load (0x0049F4B0: false), [2] - [4] the stubs below, [5] unload (empty), [6] / [7] the height and
// base (0), [8] a string's size (0x0048D090: nothing)).
class CFont {
public:
    // 0x0048D020: construct. Returns this.
    CFont* Construct();
    // 0x0048D030: the destructor's body.
    void DestroyBody();
    // 0x0048D1F0 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    CFont* Destroy(unsigned int flags);
    // 0x0048D040: the platform's unload (virtual [5]), +4 cleared.
    void Unload();
    // (0x0048D060, a string's width and height, is a jump to virtual [8] folded with other classes' slot 8:
    // engine_geom.h's VirtualSlot8Jump.)
    // 0x0048D070 (virtual [2]) / 0x0048D080 (virtual [3] and [4]): "STUB A DUB DUB".
    void PlatSetSpacings(int charSpacing, int spaceSpacing);
    void PlatSetColors(const uint32_t* colors);

    void** m_vtable;
    uint32_t m_unknown4;
};

// A loaded font: its name (the file name the scripts gave) and the font. 16 of them at 0x006FA830.
struct FontSlot {
    char name[24];
    CFont* font;
};
static_assert(sizeof(FontSlot) == 0x1C, "layout must match the original");

constexpr int kFontSlots = 16;

// Load the font `name` (or, if it's loaded, just update it): spacings, colour table (`colors` - 16 RGBA
// entries - or null to keep the font's), and whether it's the button-glyph font. Loading a font also loads
// the meta button map (once).
void LoadFont(const char* name, int charSpacing, int spaceSpacing, uint32_t* colors, bool buttonFont);

// LoadFont <name> [buttons_font] [char_spacing = n] [space_spacing = n] [color_tab = [[r g b a] ...]]
bool __cdecl CFunc_LoadFont(Script::CStruct* params, Script::CScript* script);

// The name of the button-glyph font and of the meta button map script array, as PartyMod's GlyphStyle
// setting picks them (1 PS2, 2 GameCube, otherwise the original Xbox ones).
const char* ButtonFontName();
const char* MetaButtonMapName();

// 0x0048D760: the loaded font named by this checksum, or null.
CFont* __cdecl GetFont(uint32_t name);
// 0x0048D6C0: the font `name` unloaded (0x004B4090) and taken out of the table (its slot's name kept).
void __cdecl UnloadFont(const char* name);
// UnloadFont <name> (0x0048D7B0).
bool __cdecl CFunc_UnloadFont(Script::CStruct* params, Script::CScript* script);
// 0x0048D6A0: the button glyph's character for the meta button a hex digit names (through the meta button map).
char __cdecl ButtonGlyph(const char* digit);

} // namespace Nx
