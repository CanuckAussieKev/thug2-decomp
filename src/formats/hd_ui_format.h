#pragma once

#include <cstdint>

// NOT ORIGINAL - the markers of the HD UI's upscaled images and fonts (engine/gfx/hd_ui.h says what they are): an
// upscaled .img.xbx's header +0x14, an upscaled .fnt.xbx's glyph page header +8 / +0xC.
namespace hd_ui_format {

constexpr uint32_t kMarkerScaleMask = 0xFFFFFF00u; // the marker's high three bytes are the image marker; the low one the scale

constexpr uint32_t kImageMarker = 0x48440000; // "HD" in the high half, the scale in the low byte
constexpr uint32_t kFontMarker = 0x49554448;  // "HDUI"
// An upscaled opaque image (the loading screens) may be stored as DXT1 blocks instead of 32-bit texels: header format
// = this ("DXT1"), the file's texels one level of 8 bytes per 4x4 block, rows top-down as d_3d wants them (not swizzled).
constexpr uint32_t kFormatDxt1 = 0x31545844;

// The scale an image header's +0x14 marks (1: not an upscaled image).
inline uint32_t image_scale(uint32_t field)
{
    const uint32_t scale = field & 0xFF;
    return (field & kMarkerScaleMask) == kImageMarker && (scale == 2 || scale == 4) ? scale : 1;
}

// The scale a font's glyph page header marks (1: not an upscaled font).
inline uint32_t font_scale(uint32_t marker, uint32_t scale)
{
    return marker == kFontMarker && (scale == 2 || scale == 4) ? scale : 1;
}

} // namespace hd_ui_format
