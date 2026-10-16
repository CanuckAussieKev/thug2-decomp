#pragma once

// Contains MIT-licensed code (c) 2024 PARTYMANX - see THIRD_PARTY_NOTICES.md

#include <cstdint>

// Button glyphs in text on PC. The PC release changed most button tags from meta buttons to explicit buttons
// and spells out the key's name for most of them; with a controller in use (or [Miscellaneous] PreferGlyphs=1)
// the tags are turned back into the controller's buttons (Xbox layout) and drawn as glyphs of the button font.
// [Miscellaneous] GlyphStyle picks that font and its meta button map: 0 Xbox, 1 PS2 (the default), 2 GameCube.
namespace Nx {

// Read the settings (at start-up).
void LoadGlyphSettings();
// The PreferGlyphs and GlyphStyle settings.
bool& PreferGlyphs();
uint8_t& GlyphStyle();

// Which button a "\b<digit>" tag stands for (`digit` points at the digit): the digit's value with the keyboard in
// use (Str::DehexifyDigit), else the explicit buttons M-T / m-t mapped to the controller's (3, 2, 1, 0, 14, 15,
// 16, 17).
uint32_t __cdecl ButtonForTag(uint8_t* digit);
// Whether button `index` is drawn as a glyph (else the key's name is spelled out): 4-13 with the keyboard in use,
// 0-17 otherwise.
uint8_t __cdecl ShouldUseGlyph(uint8_t index);

} // namespace Nx
