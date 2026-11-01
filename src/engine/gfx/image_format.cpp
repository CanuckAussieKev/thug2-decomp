#include "engine/gfx/image_format.h"

namespace image_format {

bool read_image_header(util::ByteReader& reader, ImageHeader* out)
{
    out->id = reader.u32();
    out->unknown4 = reader.u32();
    out->width = reader.u32();
    out->height = reader.u32();
    out->format = reader.u32();
    out->unknown14 = reader.u32();
    out->image_width = reader.u16();
    out->image_height = reader.u16();
    out->palette_size = reader.u32();
    return reader.ok();
}

void decode_format(uint32_t format, uint32_t* bits, bool* palette)
{
    switch (format) {
    case 0:
        *bits = 32;
        *palette = false;
        break;
    case 2:
        *bits = 16;
        *palette = false;
        break;
    case 0x13:
        *bits = 8;
        *palette = true;
        break;
    default:
        *bits = format;
        *palette = format < 0x10;
        break;
    }
}

namespace {
bool power_of_two(uint32_t n) { return (n & (n - 1)) == 0; }
} // namespace

bool stored_plain(const ImageHeader& header, bool memory)
{
    bool plain = !(header.width && power_of_two(header.width) && header.height && power_of_two(header.height));
    return plain || (memory && header.id == kUnswizzledId);
}

uint32_t texel_size(const ImageHeader& header, bool plain)
{
    uint32_t bits;
    bool palette;
    decode_format(header.format, &bits, &palette);
    // (the sizes as the loaders keep them: 16 bits each)
    uint32_t width = plain ? header.image_width : static_cast<uint16_t>(header.width);
    uint32_t height = plain ? header.image_height : static_cast<uint16_t>(header.height);
    return ((bits >> 3) * height * width + 3) & ~3u;
}

} // namespace image_format
