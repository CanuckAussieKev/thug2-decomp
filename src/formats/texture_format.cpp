#include "formats/texture_format.h"
#include "formats/byte_stream.h"

#include <cstdint>

namespace texture_format {

constexpr int kCompressionDxt5 = 5;
constexpr int kDxt5BlockBytes = 16;
constexpr int kDxt1BlockBytes = 8;
constexpr int kBitsPerByte = 8;

bool read_dictionary_header(util::ByteReader& reader, DictionaryHeader* out)
{
    out->version = reader.u32();
    out->count = reader.i32();
    return reader.ok() && out->count >= 0;
}

bool read_texture_header(util::ByteReader& reader, TextureHeader* out)
{
    out->checksum = reader.u32();
    out->width = reader.u32();
    out->height = reader.u32();
    out->levels = reader.u32();
    out->depth = reader.u32();
    out->palette_depth = reader.u32();
    out->compression = reader.u32();
    out->palette_size = reader.u32();
    return reader.ok();
}

bool read_level(util::ByteReader& reader, const uint8_t** data, uint32_t* size)
{
    *size = reader.u32();
    *data = reader.cursor();
    return reader.skip(*size);
}

uint32_t level_size(const TextureHeader& header, uint32_t level)
{
    uint32_t width = header.width >> level;
    uint32_t height = header.height >> level;
    if (width == 0)
        width = 1;
    if (height == 0)
        height = 1;
    if (header.compression == 1 || header.compression == 2 || header.compression == kCompressionDxt5) {
        const uint32_t blocks = ((width + 3) / 4) * ((height + 3) / 4);
        return blocks * (header.compression == kCompressionDxt5 ? kDxt5BlockBytes : kDxt1BlockBytes);
    }
    return width * height * (header.depth / kBitsPerByte);
}

} // namespace texture_format
