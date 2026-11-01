#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/util/byte_stream.h"

// NOT ORIGINAL - the texture dictionary format (.tex, PC: "x.tex.xbx"), read through util::ByteReader.
//
// A dictionary (little-endian): u32 version, i32 texture count, then each texture:
//   u32 name checksum, u32 width, u32 height, u32 levels, u32 depth (bits: 8 / 16 / 32), u32 palette depth,
//   u32 compression (0: none, 1 / 2: DXT1, 5: DXT5), u32 palette size; the palette (palette size bytes);
//   then each level {u32 size, `size` bytes of texels - the Direct3D surface's, rows of 4x4 blocks if compressed}.
//
// Read by render::load_textures_from_file / _memory (engine/gfx/pc_texture.cpp).
namespace texture_format {

constexpr size_t kDictionaryHeaderSize = 8;
constexpr size_t kTextureHeaderSize = 32;

struct DictionaryHeader {
    uint32_t version = 0;
    int32_t count = 0;
};

struct TextureHeader {
    uint32_t checksum = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t levels = 0;
    uint32_t depth = 0;
    uint32_t palette_depth = 0;
    uint32_t compression = 0;
    uint32_t palette_size = 0;
};

// False if cut short or the count is negative.
bool read_dictionary_header(util::ByteReader& reader, DictionaryHeader* out);
// The texture's fixed header (not its palette or levels).
bool read_texture_header(util::ByteReader& reader, TextureHeader* out);
// One level: `*data` / `*size` in place (the reader past it); false if cut short.
bool read_level(util::ByteReader& reader, const uint8_t** data, uint32_t* size);

// The bytes of a level as stored (tightly packed rows): DXT1 8 / DXT5 16 bytes per 4x4 block, else depth / 8 per
// texel. Level `level` is the texture halved `level` times (at least 1 each way).
uint32_t level_size(const TextureHeader& header, uint32_t level);

} // namespace texture_format
