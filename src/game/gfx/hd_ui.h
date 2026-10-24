#pragma once

#include <cstdint>

// NOT ORIGINAL - HD menu UI (an experiment at the user's request, 2026-10-24; not the game's and not PartyMod's): the
// menus' images and fonts upscaled 2x / 4x by tools/hd_textures/ui.ts into the data overlay
// (tools/menu_level/make_menu_level.ts --hd-ui [2|4]). Built only with the CMake option THUG2_HD_UI (default OFF):
// without it this is compiled out and the game is exactly as before (and upscaled files would show larger).
//
// What keeps the on-screen size: a sprite's size comes from its texture's image size (the platform texture's
// width / height read the sTexture's image size - sprite elements take it as their size), a glyph's from the
// font's rectangles. So:
//   - an upscaled .img.xbx has HdUi::kImageMarker | scale in its header's +0x14 (0 in every game file); after the
//     loader (Nx::LoadTextureFile / LoadTextureMemory) has made the full-size Direct3D texture, the sTexture's sizes
//     are divided by the scale - texture coordinates are ratios of them, so the whole HD texture is still drawn;
//   - an upscaled .fnt.xbx has HdUi::kFontMarker at its glyph page's header +8 and the scale at +0xC, and the page as
//     32-bit BGRA texels (no palette); its glyph rectangles stay in the original's pixels, so SFont::Load makes the
//     texture coordinates from the page's size divided by the scale.
// Textures something edits texel by texel after loading (Create-A-Graphic, faces) aren't upscaled by the tool: their
// code still takes the sTexture's sizes as the Direct3D texture's.
namespace Nx::HdUi {

constexpr uint32_t kImageMarker = 0x48440000; // "HD" in the high half, the scale in the low byte
constexpr uint32_t kFontMarker = 0x49554448;  // "HDUI"

// The scale an image header's +0x14 marks (1: not an upscaled image).
inline uint32_t ImageScale(uint32_t field)
{
    uint32_t scale = field & 0xFF;
    return (field & 0xFFFFFF00) == kImageMarker && (scale == 2 || scale == 4) ? scale : 1;
}

// The scale a font's glyph page header marks (1: not an upscaled font).
inline uint32_t FontScale(uint32_t marker, uint32_t scale)
{
    return marker == kFontMarker && (scale == 2 || scale == 4) ? scale : 1;
}

// Counts the upscaled images / fonts loaded and logs the first of each (hd_ui.cpp).
void NoteImage(uint32_t scale, uint32_t width, uint32_t height);
void NoteFont(const char* name, uint32_t scale, uint32_t width, uint32_t height);

} // namespace Nx::HdUi
