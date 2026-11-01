#pragma once

#include <cstdint>

#include "formats/hd_ui_format.h"

// NOT ORIGINAL - HD menu UI (an experiment at the user's request, 2026-10-24; not the game's and not PartyMod's): the
// menus' images and fonts upscaled 2x / 4x by tools/pipeline/hd_textures/ui.ts into the data overlay
// (tools/pipeline/menu_level/make_menu_level.ts --hd-ui [2|4]). Always built; switched by thug2-decomp.ini
// [features] hd_ui=1 (default 0). The upscaled files are in the overlay's hd_ui subfolder, which the data overlay
// consults (first) only while it is on - so with 0 nothing is read from there and the game is exactly as before.
//
// What keeps the on-screen size: a sprite's size comes from its texture's image size (the platform texture's
// width / height read the TextureRef's image size - sprite elements take it as their size), a glyph's from the
// font's rectangles. So:
//   - an upscaled .img.xbx has hd_ui::kImageMarker | scale in its header's +0x14 (0 in every game file); after the
//     loader (render::read_texture_file / load_texture_memory) has made the full-size Direct3D texture, the TextureRef's sizes
//     are divided by the scale - texture coordinates are ratios of them, so the whole HD texture is still drawn;
//   - an upscaled .fnt.xbx has hd_ui::kFontMarker at its glyph page's header +8 and the scale at +0xC, and the page as
//     32-bit BGRA texels (no palette); its glyph rectangles stay in the original's pixels, so TypefaceData::ReadIn makes the
//     texture coordinates from the page's size divided by the scale.
// Textures something edits texel by texel after loading (Create-A-Graphic, faces) aren't upscaled by the tool: their
// code still takes the TextureRef's sizes as the Direct3D texture's.
namespace render::hd_ui {

// The upscaled files' markers and scales (formats/hd_ui_format.h).
using hd_ui_format::kImageMarker;
using hd_ui_format::kFontMarker;
using hd_ui_format::kFormatDxt1;
using hd_ui_format::image_scale;
using hd_ui_format::font_scale;

// [features] hd_ui (read once).
bool is_switched_on();

// Counts the upscaled images / fonts loaded and logs the first of each (hd_ui.cpp).
void note_image(uint32_t scale, uint32_t width, uint32_t height);
void note_font(const char* name, uint32_t scale, uint32_t width, uint32_t height);

} // namespace render::hd_ui
