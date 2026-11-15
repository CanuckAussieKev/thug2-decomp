#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "engine/gfx/smooth_tess.h"

// NOT ORIGINAL - building relief (docs/design/vulkan.md "Building relief"; [Graphics] building_relief): a building
// front's windows, frames, sills and floor bands - painted on a flat wall - built as real geometry once, at load, on the
// processor, from the facade structure made offline for its texture (formats/relief_format.h: rectangles of the
// texture's cell at heights off the wall). Every renderer draws it and ray tracing's acceleration structures hold it
// (shadows from sills and ledges, occlusion in the window recesses); the collision is the game's own and unchanged.
//
// How: a mesh's walls - triangles within 15 degrees of upright, with texture coordinates - are joined into planes
// (triangles sharing an edge, wound alike, facing the same way, their texture coordinates the same along it); each
// wall triangle is cut along the structure's rectangle edges (lines of constant u and v, in the texture's cells) into
// pieces, each lying wholly inside or outside every rectangle; a piece moves along its plane's normal by its height
// (the last rectangle holding it: texture widths x the units the plane draws a texture width across x the strength,
// capped in units). Between pieces of different heights a wall is built from the higher piece's edge down to the lower
// one's; along a plane's outline (an open edge, a crease - the building's corner, the roof line) a piece off the
// surface is closed by a wall back to the original edge. So the surface stays closed: no cracks, the outline where
// the original was.
//
// Crack-free by construction: every point is made from what both sides of an edge share - a corner as it is; a cut
// line's point on a triangle's edge from that edge's two ends in one order (the lower welded position first) and the
// line; a point inside a triangle from the two lines crossing there - so a wall and the pieces either side, and two
// triangles of a plane, meet at the same points to the bit.
//
// The original vertices stay first and unchanged (other levels of detail, the editor's moves and the colour writes
// refer to them); the new ones follow, the output a strip again (16-bit indices). Triangles that aren't walls, or whose
// cuts would be too many, are kept as they are; a mesh whose result would not fit 16 bits is tried with the windows
// alone, and otherwise left as it was.
namespace render::relief {

// A rectangle of the structure's cell (fractions of the cell: u0 < u1, v0 < v1, at most a cell wide - u1 / v1 may pass
// 1, wrapping into the next) at a height off the wall in texture widths (+ out of it).
struct Rect {
    float u0 = 0.0F;
    float v0 = 0.0F;
    float u1 = 0.0F;
    float v1 = 0.0F;
    float height = 0.0F;
    bool window = false; // a window's (kept when only the windows fit)
};

// A texture's facade: its cell repeated across and down the texture, the cell's rectangles (later ones over earlier).
struct Structure {
    uint16_t repeat_u = 1;
    uint16_t repeat_v = 1;
    std::vector<Rect> rects;
};

constexpr float kDefaultUpright = 0.26F;    // |normal.y| under this: a wall (within 15 degrees of upright)
constexpr float kDefaultMostRecess = 12.0F; // units a piece may go into the wall
constexpr float kDefaultMostRaise = 4.0F;   // ... and stand out of it
constexpr float kDefaultLeast = 0.25F;      // units: a smaller height is left flat
constexpr uint32_t kMostCuts = 96;          // lines one triangle may be cut by along u or v (more: kept flat)

struct Options {
    float strength = 1.0F; // the heights' scale (0: nothing moves)
    float upright = kDefaultUpright;
    float most_recess = kDefaultMostRecess;
    float most_raise = kDefaultMostRaise;
    float least = kDefaultLeast;
    bool windows_only = false; // (the second try of a mesh too big with everything)
};

// A new vertex's sources: the original vertices (the wall triangle's corners) it is blended from, and their weights -
// where it lies on the original surface (the ray-traced relit light reads the flat wall's light there: rt_relit.h
// RelitMesh::sources).
struct VertexSource {
    std::array<uint16_t, 3> from{};
    std::array<float, 3> weight{};
    uint8_t count = 0;
};

struct Result {
    bool changed = false;          // false: the mesh is left as it was
    std::vector<uint8_t> vertices; // the original vertices, then the new ones
    uint32_t vertex_count = 0;
    std::vector<uint16_t> strip;   // the new first level of detail's strip
    std::vector<VertexSource> sources; // each new vertex's (vertex_count - the original count of them)
    uint32_t triangles = 0;        // the original's (not degenerate)
    uint32_t walls = 0;            // of them, wall triangles of the facade (with texture coordinates, upright)
    uint32_t relieved = 0;         // ... cut into pieces with some off the surface
    uint32_t pieces = 0;           // the pieces made
    uint32_t sides = 0;            // the walls built between pieces and along the outlines (quads)
    uint32_t planes = 0;           // the planes the walls were joined into
    float deepest = 0.0F;          // units: the most a piece went in
    float highest = 0.0F;          // ... stood out
    bool windows_only = false;     // only the windows fit
    bool too_big = false;          // nothing fit 16 bits: left as it was
};

// The height at texture coordinates (u, v) - any repeat - in texture widths: the last rectangle of the cell holding it,
// else 0 (the wall).
[[nodiscard]] float height_at(const Structure& structure, double u, double v, bool windows_only = false);

// The mesh with its walls relieved (Result::changed false: keep it as it is). `layout` must have texture coordinates;
// the first set is the structure's.
[[nodiscard]] Result build(std::span<const uint8_t> vertices, uint32_t vertex_count, const tess::VertexLayout& layout,
                           std::span<const uint16_t> strip, const Structure& structure, const Options& options);

} // namespace render::relief
