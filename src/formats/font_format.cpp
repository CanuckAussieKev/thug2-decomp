// NOT ORIGINAL - the bitmap font format's readers: see font_format.h.

#include "engine/core/checked_index.h"
#include "formats/byte_stream.h"
#include "formats/font_format.h"

#include <span>

namespace font_format {

bool read_header(util::ByteReader& reader, Header* out)
{
    reader.u32();
    out->format = reader.u8();
    reader.skip(3);
    out->glyph_count = reader.i32();
    out->height = reader.u32();
    out->base = reader.u32();
    return reader.ok() && out->glyph_count >= 0 && out->glyph_count <= kMaxGlyphs;
}

bool read_glyph_entries(util::ByteReader& reader, int count, std::span<GlyphEntry> out)
{
    if (count < 0 || count > kMaxGlyphs)
        return false;
    for (int i = 0; i < count; ++i) {
        GlyphEntry& entry = core::at(out, i);
        entry.baseline = reader.u16();
        reader.bytes(entry.codes.data(), sizeof(entry.codes));
    }
    return reader.ok();
}

bool read_page(util::ByteReader& reader, Page* out)
{
    reader.u32();
    out->width = reader.u16();
    out->height = reader.u16();
    out->hd_mark = reader.u32();
    out->hd_scale = reader.u32();
    return reader.ok();
}

bool read_glyph_rects(util::ByteReader& reader, int count, std::span<GlyphRect> out)
{
    if (count < 0 || count > kMaxGlyphs)
        return false;
    reader.u32();
    for (int i = 0; i < count; ++i) {
        GlyphRect& rect = core::at(out, i);
        rect.x = reader.u16();
        rect.y = reader.u16();
        rect.w = reader.u16();
        rect.h = reader.u16();
    }
    return reader.ok();
}

} // namespace font_format
