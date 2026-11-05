#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace render {

// A vertex of a texture projected onto the world (texture splats, texture projections) as the card draws it
// (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1: the GPU's vertex layout, handed to DrawPrimitiveUP as it is).
struct ProjectedVertex {
    std::array<float, 3> position;
    uint32_t argb;            // +0x0C
    std::array<float, 2> uv;  // +0x10
};
constexpr std::size_t kProjectedVertexBytes = 0x18;
constexpr std::size_t kProjectedVertexArgbOffset = 0x0C;
constexpr std::size_t kProjectedVertexUvOffset = 0x10;

static_assert(sizeof(ProjectedVertex) == kProjectedVertexBytes && offsetof(ProjectedVertex, argb) == kProjectedVertexArgbOffset &&
                  offsetof(ProjectedVertex, uv) == kProjectedVertexUvOffset,
              "the vertex format drawn");

} // namespace render
