#pragma once

#include <cstddef>
#include <cstdint>

#include "formats/byte_stream.h"

// NOT ORIGINAL - the engine's image format (.img, PC: "x.img.xbx"; also the face texture's image in memory and on
// the wire), read through util::ByteReader.
//
// An image (little-endian): a 32-byte header
//   u32 id (0x9B: never swizzled - the memory loader only), u32 (not read), u32 width, u32 height (the texture's),
//   u32 format (0: 32-bit, 2: 16-bit, 0x13: 8-bit with a palette; other values below 0x10: their bits with a
//   palette), u32 (0; hd_ui's marker and scale in an upscaled image), u16 image width, u16 image height, u32 palette
//   size (the file loader's: the memory loader always takes 0x400 bytes)
// then the palette (palette formats only) and the texels: bits / 8 per texel, padded to 4 bytes - swizzled unless
// the texture's sizes aren't both powers of two (then the image's sizes, as they are).
//
// Read by render::read_texture_file / load_texture_memory (engine/gfx/texture_file.cpp).
namespace image_format {

constexpr size_t kHeaderSize = 0x20;
constexpr uint32_t kPaletteCapacity = 0x400; // the loaders' palette buffer (256 colours)
constexpr uint32_t kUnswizzledId = 0x9B;

struct ImageHeader {
    uint32_t id = 0;
    uint32_t unknown4 = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t format = 0;
    uint32_t unknown14 = 0;
    uint16_t image_width = 0;
    uint16_t image_height = 0;
    uint32_t palette_size = 0;
};

bool read_image_header(util::ByteReader& reader, ImageHeader* out);

// Bits per texel and whether a palette follows the header.
void decode_format(uint32_t format, uint32_t* bits, bool* palette);

// Whether the texels are stored as they are (the texture's sizes not both powers of two; `memory`: the memory
// loader's id 0x9B too).
bool stored_plain(const ImageHeader& header, bool memory);

// The texels' bytes as the loaders read them: bits / 8 per texel of the stored size (the image's when plain, the
// texture's otherwise), padded to 4.
uint32_t texel_size(const ImageHeader& header, bool plain);

} // namespace image_format
