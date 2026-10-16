#pragma once

#include <cstdint>

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// Fonts loaded by the scripts (LoadFont), by name, and the meta button map the button glyphs go through.
namespace Nx {

class CFont;

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

// The name of the button-glyph font and of the meta button map script array, as the PC port's GlyphStyle
// setting picks them (1 PS2, 2 GameCube, otherwise the original Xbox ones).
const char* ButtonFontName();
const char* MetaButtonMapName();

} // namespace Nx
