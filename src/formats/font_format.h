#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <array>

#include "formats/byte_stream.h"
#include "engine/core/checked_index.h"

// NOT ORIGINAL - the bitmap font format (PC: "fonts/x.fnt.xbx"), read through util::ByteReader.
//
// A font (little-endian): a 0x14-byte header {u32 (not read), u8 character map format (1 or 2) + 3 bytes, u32 glyph
// count, u32 height, u32 base}; each glyph's character map entry (6 bytes: u16 baseline, then format 1 an i16
// character - or, negative, -1 - special glyph index - format 2 up to four character bytes, 0xFF ending them); the
// page's header (0x10 bytes: +4 u16 width, +6 u16 height; +8 / +0xC the HD menu UI's mark and scale); its texels
// (one palette index each, the count rounded up to 4 - an HD page: 4 bytes each, no palette); the palette (256 RGBA,
// PS2 alpha); 4 bytes, then each glyph's rectangle in the page (u16 x, y, width, height).
//
// Read by render::TypefaceData::read_in (engine/gfx/text.cpp).
namespace font_format {

constexpr int kBitsPerByte = 8;

constexpr size_t kHeaderSize = 0x14;
constexpr size_t kPageHeaderSize = 0x10;
constexpr size_t kPaletteSize = 0x400;
constexpr int kMaxGlyphs = 256;      // the glyph indexes are bytes (and the original's buffer held 256 rectangles)
constexpr int kSpecialGlyphs = 0x20; // the special map's entries

// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct Header {
    uint8_t format = 0;
    int glyph_count = 0;
    uint32_t height = 0;
    uint32_t base = 0;
};
// NOLINTEND(readability-identifier-naming)

struct GlyphEntry {
    uint16_t baseline = 0;
    std::array<uint8_t, 4> codes = {}; // format 1: an i16 character / special glyph (codes 0-1); format 2: up to four characters
};

[[nodiscard]] inline int16_t glyph_code(const GlyphEntry& entry)
{
    return static_cast<int16_t>(core::at(entry.codes, 0) | core::at(entry.codes, 1) << kBitsPerByte);
}

// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct Page {
    uint32_t width = 0, height = 0;
    uint32_t hd_mark = 0, hd_scale = 0;
};
// NOLINTEND(readability-identifier-naming)

struct GlyphRect {
    uint16_t x = 0, y = 0, w = 0, h = 0;
};

// The header. False if cut short or the glyph count is negative or more than kMaxGlyphs.
bool read_header(util::ByteReader& reader, Header* out);
// `count` character map entries into `out` (kMaxGlyphs of room). False if cut short.
bool read_glyph_entries(util::ByteReader& reader, int count, std::span<GlyphEntry> out);
// The page's header. False if cut short.
bool read_page(util::ByteReader& reader, Page* out);
// The page's texel count (width x height rounded up to 4).
inline uint32_t texel_count(const Page& page) { return (page.height * page.width + 3) & ~3u; }
// `count` glyph rectangles (after their 4 bytes) into `out`. False if cut short.
bool read_glyph_rects(util::ByteReader& reader, int count, std::span<GlyphRect> out);

} // namespace font_format
