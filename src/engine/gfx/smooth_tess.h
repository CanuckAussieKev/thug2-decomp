#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// NOT ORIGINAL - smoothed curved surfaces (docs/design/vulkan.md "Smoothed curves"): a mesh's faceted curves - quarter
// pipes, rails, pipes, car bodies, round props - tessellated once at load into more triangles that follow the curve, so
// every renderer draws them (and ray tracing's acceleration structures hold them) without a tessellation stage.
//
// How: Phong tessellation (each new point is the flat triangle's point pulled toward the tangent planes of its
// corners; shape factor 0.5, raised per edge so that an edge's middle lies on the circular arc its two normals describe)
// on the triangles of a strip, with the corners' normals made from the geometry itself - the faces round a corner averaged over the edges
// whose faces meet at under the crease angle (smoothing groups), the file's normals being lighting, not shape.
//
// Which triangles: a triangle is split when one of its edges is curved - shared by exactly two triangles of the mesh
// that meet at under the crease angle (and wind the same way), whose curve leaves the straight edge by more than a
// small distance. Flat walls (no curve), hard edges (creases), boundaries and open edges stay straight; triangles with
// no curved edge are kept as they are.
//
// Crack-free: every split triangle of a mesh is split the same number of times (one level a mesh); a curved edge's
// points are made from its two ends' positions and normals only, in one order (the lower welded position first), so
// the two triangles on it - with the same vertex indices or not (texture seams) - get the same points to the bit; a
// straight edge of a split triangle is not split: its points are moved onto its two ends (degenerate slivers, a fan),
// so the meshes and triangles beside it meet it at its original two vertices (no T-junctions).
//
// Bounded: the new points' distance from the original flat triangles (what collision is made of: the game's collision
// is its own files and unchanged) is measured; past `max_deviation` the shape factor is scaled down until the largest
// is exactly that. Displacement along the smoothed normal from a height source is added inside that bound, only on
// new points shared by the vertex indices of both sides (or interior), never on thin meshes.
//
// The original vertices stay first and unchanged (other levels of detail, the editor's moves, the colour writes refer
// to them); the new ones follow; the output is a strip again (16-bit indices).
namespace render::tess {

// A vertex's layout: three floats of position at 0; the rest optional (offset -1: none).
struct VertexLayout {
    uint32_t stride = 0;
    int normal_offset = -1; // three floats, renormalised when blended
    int colour_offset = -1; // four bytes, each blended
    int uv_offset = -1;     // `uv_sets` pairs of floats, each blended
    uint32_t uv_sets = 0;
};

// A height at texture coordinates (0 - 1, 0.5 the surface itself; negative: no height there), from whatever holds the
// material's height map.
using HeightLookup = float (*)(const void* context, float u, float v);
struct HeightSource {
    HeightLookup lookup = nullptr;
    const void* context = nullptr;
    float scale = 0.0F; // units the full height range moves a point (0: no displacement)
};

constexpr int kDefaultLevel = 3;
constexpr float kDefaultShape = 0.5F;
constexpr float kDefaultCreaseDegrees = 50.0F;
constexpr float kDefaultMaxDeviation = 2.0F;
constexpr float kDefaultCurvedEpsilon = 0.02F;
constexpr float kDefaultTargetError = 0.05F;
constexpr float kDefaultThinSize = 24.0F;

struct Options {
    int max_level = kDefaultLevel;                    // the most pieces an edge is split into (2 - 8; under 2: none)
    float shape = kDefaultShape;                      // Phong tessellation's shape factor before the cap
    float crease_degrees = kDefaultCreaseDegrees;     // faces meeting at more than this are a hard edge
    float max_deviation = kDefaultMaxDeviation;       // the most a point may leave the original surface (units)
    float curved_epsilon = kDefaultCurvedEpsilon;     // an edge whose curve leaves it by less is straight (units)
    float target_error = kDefaultTargetError;         // a mesh's level: its curves followed to about this (units)
    float thin_size = kDefaultThinSize;               // thinner meshes (their bounds' smallest side: rails) aren't displaced
    HeightSource height;                              // displacement (scale 0: none)
};

struct Result {
    bool changed = false;            // false: the mesh is left as it was (nothing curved, too big, unreadable)
    std::vector<uint8_t> vertices;   // the original vertices, then the new ones
    uint32_t vertex_count = 0;
    std::vector<uint16_t> strip;     // the new first level of detail's strip
    uint32_t triangles = 0;          // the original's (not degenerate)
    uint32_t smoothed = 0;           // of them, split
    uint32_t curved_edges = 0;
    int level = 0;
    float shape = 0.0F;              // the shape factor used (under Options::shape when capped)
    float deviation = 0.0F;          // the largest distance of a point from the original surface, as made
    float uncapped = 0.0F;           // ... before the cap (with Options::shape)
    float displaced = 0.0F;          // the largest displacement added
    bool capped = false;
};

// A strip's triangles in drawing order and winding (odd ones turned round, as Direct3D draws them), the degenerate
// ones left out.
std::vector<std::array<uint16_t, 3>> strip_triangles(std::span<const uint16_t> strip);

// The mesh smoothed (Result::changed false: keep it as it is).
Result smooth(std::span<const uint8_t> vertices, uint32_t vertex_count, const VertexLayout& layout,
              std::span<const uint16_t> strip, const Options& options);

} // namespace render::tess
