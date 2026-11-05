#include "formats/image_format.h"
#include "formats/byte_stream.h"

#include <cstdint>

namespace image_format {

constexpr int kTrueColourBits = 32;
constexpr int kHighColourBits = 16;
constexpr int kPalettedBits = 8;
constexpr int kPalettedFormat = 0x13;     // the 8-bit paletted format
constexpr int kPaletteFormatLimit = 0x10; // other formats below this have a palette, the rest are colour bit counts

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
        *bits = kTrueColourBits;
        *palette = false;
        break;
    case 2:
        *bits = kHighColourBits;
        *palette = false;
        break;
    case kPalettedFormat:
        *bits = kPalettedBits;
        *palette = true;
        break;
    default:
        *bits = format;
        *palette = format < kPaletteFormatLimit;
        break;
    }
}

namespace {
bool power_of_two(uint32_t value) { return (value & (value - 1)) == 0; }
} // namespace

bool stored_plain(const ImageHeader& header, bool memory)
{
    const bool plain =
        !((header.width != 0U) && power_of_two(header.width) && (header.height != 0U) && power_of_two(header.height));
    return plain || (memory && header.id == kUnswizzledId);
}

uint32_t texel_size(const ImageHeader& header, bool plain)
{
    uint32_t bits = 0;
    bool palette = false;
    decode_format(header.format, &bits, &palette);
    // (the sizes as the loaders keep them: 16 bits each)
    const uint32_t width = plain ? header.image_width : static_cast<uint16_t>(header.width);
    const uint32_t height = plain ? header.image_height : static_cast<uint16_t>(header.height);
    return (((bits >> 3) * height * width) + 3) & ~3U;
}

} // namespace image_format
