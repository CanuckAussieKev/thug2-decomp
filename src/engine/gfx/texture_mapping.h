#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "engine/gfx/smooth_tess.h"

// NOT ORIGINAL - (round TRAIN) surfaces whose texture coordinates don't lay their texture on them (docs/design/vulkan.md
// "Texture maps that place nothing"). Some of a level's surfaces were mapped along one direction only: every corner of
// a triangle has the same v (or the coordinates change along one line of the texture), so one row of texels is drawn
// across the whole triangle - a fan of stripes. The art never showed them: the bake left them black (Training's ceiling,
// baked 000000 at every corner). Lit by their lamps they are seen, and the stripes are the mapping's, not the surface's
// (the path-traced reference shows them too: it reads the same coordinates).
//
// The rule, from the data alone: a triangle of an opaque base pass whose map from the triangle's plane to its texture
// has rank one (its least stretch under kRankOneShare of its most) and whose narrow side spans at least kVisibleTexels of
// the texture along its mapped direction - the stripes would show (a trim's thin strip mapped lengthwise doesn't) - and
// that the original never showed: every corner's colour black (a rank-one map the art lit is the artists' own - a
// gradient, a fade strip, a sky: Boston's 12 700 such triangles, kept). Such a triangle gets world-aligned coordinates - the planar projection on the axis plane it faces most (UE's world-aligned
// textures; the standard mapping of a surface without its own UVs), at the mesh's own density (its such triangles'
// median texture length per world unit along the mapped direction), so the texture keeps its size and every renderer
// (the raster, the rays, the path tracer, the bake) reads the same coordinates. A vertex a triangle with a sound map
// shares keeps its coordinates (that triangle drew it right): the other gets a copy of it.
namespace render::texture_mapping {

inline constexpr float kRankOneShare = 1.0F / 64.0F;
inline constexpr float kVisibleTexels = 4.0F;

// How a triangle's texture is laid on it: the texture length (coordinate units) a world unit covers along the
// triangle's most and least stretched directions (the singular values of the map from its plane to its coordinates).
struct Stretch {
    float most = 0.0F;
    float least = 0.0F;
};
[[nodiscard]] Stretch stretch_of(const std::array<std::array<float, 3>, 3>& at,
                                 const std::array<std::array<float, 2>, 3>& uv);

// The triangle's narrow size: twice its area over its longest edge (its height off that edge).
[[nodiscard]] float narrow_size(const std::array<std::array<float, 3>, 3>& at);

// Whether a triangle's map places nothing along one of its directions where it shows (the rule above), its texture
// `texels` across (its larger side).
[[nodiscard]] bool places_nothing(const std::array<std::array<float, 3>, 3>& at,
                                  const std::array<std::array<float, 2>, 3>& uv, uint32_t texels);

struct Result {
    bool changed = false;              // false: nothing placed anew (the mesh left as it was)
    std::vector<uint8_t> vertex_bytes; // (changed) the vertices: the original ones (their first coordinates
                                       // world-aligned where the rule says), then the copies made
    uint32_t vertex_count = 0;
    std::vector<uint16_t> strip;   // (changed, copies made) the first level of detail's new strip; empty: as it was
    std::vector<uint16_t> sources; // each copy's original vertex
    uint32_t triangles = 0;        // the strip's triangles (index-degenerate ones left out)
    uint32_t placing_nothing = 0;  // of them, those the rule found
    uint32_t shown = 0;            // ... and of those, the ones the art lit (a corner's colour not black): kept
    uint32_t vertices = 0;         // the vertices given world-aligned coordinates (copies included)
    float density = 0.0F;          // the coordinate units a world unit took (the median `most`)
    float largest = 0.0F;          // the largest coordinate written (its size)
};

// The vertices (`count` of `layout.stride` bytes, the position three floats at 0, the first coordinate pair at
// layout.uv_offset) of a strip, the triangles the rule finds given world-aligned first coordinates (the other sets as
// they were). A triangle is placed whole: a corner a sound triangle also draws - or another plane's triangle already
// took - is copied for it, and the strip made again over the copies. `texels`: the base pass's texture's larger side.
Result world_aligned(std::span<const uint8_t> vertices, uint32_t count, const tess::VertexLayout& layout,
                     std::span<const uint16_t> strip, uint32_t texels);

} // namespace render::texture_mapping
