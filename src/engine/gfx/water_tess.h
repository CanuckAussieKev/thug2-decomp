#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "engine/gfx/smooth_tess.h"

// NOT ORIGINAL - the water's moving surface (docs/design/vulkan.md "Water", "Moving water"; [Graphics] rt_water_geometry):
// a water mesh - big flat sheets, two triangles a quad - split at load into a fine grid, so that a backend that moves
// its vertices by the swell (gpu_vulkan/vulkan_water.h) has points to move. API-neutral: positions and the vertex's
// other parts (tess::VertexLayout) only; render_pc/water_geometry.h decides which meshes.
//
// Which meshes: every triangle near horizontal (its normal within about 20 degrees of up or down: the sea, a canal - not
// a waterfall, whose vertices would be lifted sideways) and some edge longer than the grid's spacing.
//
// How: each edge is split into pieces of at most the spacing (its length / spacing, rounded up, capped), its points made
// from its two ends' positions in one order (the lower position first, by their bits) - so two triangles on it, of this
// mesh or of the mesh beside it, get the same points to the bit. A triangle is split into n x n smaller ones, n its
// longest edge's pieces; on an edge with fewer pieces its border points are moved onto the edge's own points (the
// nearest - degenerate slivers, no T-junctions), its inner points are the corners' blend. Everything else in a vertex (the
// normal renormalised, the colour, the texture coordinates) is blended from the corners as the rasteriser would.
//
// The original vertices stay first and unchanged (other levels of detail refer to them); the new ones follow; the output
// is a strip (16-bit indices). Too many vertices or indices: the spacing is widened until they fit.
namespace render::water_tess {

inline constexpr float kDefaultSpacing = 40.0F;  // the grid's cell (units: about a metre)
inline constexpr int kDefaultMostPieces = 64;    // the most pieces an edge is split into
inline constexpr float kDefaultLeastUp = 0.94F;  // a triangle's |normal.y| at least this (about 20 degrees)

struct Options {
    float spacing = kDefaultSpacing;
    int most_pieces = kDefaultMostPieces;
    float least_up = kDefaultLeastUp;
};

struct Result {
    bool changed = false;          // false: the mesh is left as it was (`why`)
    const char* why = "";
    std::vector<uint8_t> vertices; // the original vertices, then the new ones
    uint32_t vertex_count = 0;
    std::vector<uint16_t> strip;   // the new first level of detail's strip
    uint32_t triangles = 0;        // the original's (not degenerate)
    uint32_t made = 0;             // the triangles drawn now (not degenerate)
    float spacing = 0.0F;          // the spacing used (wider than asked when it didn't fit)
};

// The mesh split into a grid (Result::changed false: keep it as it is).
[[nodiscard]] Result subdivide(std::span<const uint8_t> vertices, uint32_t vertex_count, const tess::VertexLayout& layout,
                               std::span<const uint16_t> strip, const Options& options);

// The pieces an edge of this length is split into (1: not split) - the same for every mesh that shares the edge.
[[nodiscard]] int pieces_of(float length, const Options& options, float spacing);

} // namespace render::water_tess
