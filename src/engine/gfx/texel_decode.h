#pragma once

// NOT ORIGINAL - (lighting round A3) a texture level's texels decoded on the processor, API-neutral: the relit bake reads
// what a material's texture shows (render_pc/rt_record.cpp: the ceiling lights drawn as bright textures - rt_lights.h
// textured candidates). A small shared reader: anything else that needs a texture's colours on the processor (the
// material table's texture statistics) can use it too.
//
// The formats the game's textures come in (Direct3D 9's numbers, engine/gfx/gpu.h kFormat...): A8R8G8B8, X8R8G8B8,
// R5G6B5, X1R5G5B5, A1R5G5B5, A4R4G4B4 and the block-compressed DXT1, DXT3, DXT5 (BC1 - BC3).

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace render::texel {

// A decoded level: width x height texels, each R, G, B, A bytes, rows top first (as stored).
struct Image {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<std::array<uint8_t, 4>> texels;
    // The texel at (u, v) - texture coordinates, wrapped (repeat) - nearest.
    [[nodiscard]] std::array<uint8_t, 4> at(float u, float v) const;
    [[nodiscard]] bool empty() const { return texels.empty(); }
};

// Whether `format` (a Direct3D 9 format number) can be decoded.
[[nodiscard]] bool decodable(uint32_t format);
// The level's bytes as locked (`pitch` bytes a row - of 4x4 blocks for DXT), width x height texels, decoded; false (and
// `out` empty) for a format it can't read or too few bytes.
bool decode(uint32_t format, uint32_t width, uint32_t height, std::span<const uint8_t> bytes, uint32_t pitch, Image* out);

} // namespace render::texel
