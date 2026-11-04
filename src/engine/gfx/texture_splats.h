#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

#include "base/core/cursor.h"
#include "engine/gfx/engine_splats.h"
#include "base/maths/vec2.h"

// Texture splats on PC (Nx engine): textures projected onto the world (skid marks, splashes) - per texture a set of
// up to 256 triangles kept in a table (0x007004B0, lists::KeyMap keyed by the set's address), drawn with a
// perspective correction of their texture coordinates, lit, killed by box or all at once - and the geometry helpers
// the projection uses: 2D segment and triangle tests against the screen square, and splitting triangles that are too
// big (a stack of vertices at 0x006FC4A0).
namespace maths {
class BBox;
}

namespace render {

class BitmapTexture;
struct TextureImage;

struct MaterialRef;

struct MeshRef;

// Vertex `index` counted through the triangles (0-0x2FF).
ProjectedVertex& splat_vertex(TextureSplats& splats, int index);

// 0x00490CD0 (thiscall on a set): the first free triangle of the first `count` (alive 0), else the one with the
// most steps past its start (the first on ties).
[[nodiscard]] int oldest_slot(const TextureSplats& splats, uint32_t count);

// 0x004B87B0: each triangle's three vertex colours lit (0x0048F620: from its base colours, at its first vertex's
// position for all three; RGBA to ARGB).
void light_texture_splats(TextureSplats* splats);
// 0x004B9200: every set freed (its material too) and taken out of the table.
void kill_all_texture_splats();
// 0x004B92A0: the triangles whose first vertex is in the box (min, then max at +0x10) killed.
void kill_texture_splats_in_box(const maths::BBox* box);
// 0x004B9380: every set drawn - each vertex's texture coordinates corrected first about 0.5 by 1 / (r + t (1 - r)),
// r = A / B and t = (steps - alive) / steps (1 when A - B is 0) - with linear
// minification (put back after).
void draw_texture_splats();

// 0x004B7FA0: whether segments a and b meet (the parameters along each within 0-1 of the denominator; NaN
// passes, as the original's flag tests).
// A segment in the plane, from one end to the other.
struct Segment2 {
    maths::Vec2 from;
    maths::Vec2 to;
};
bool segments_intersect(const Segment2& a, const Segment2& b);
// 0x004B88D0 (and its jump 0x004B8E30): whether a 2D triangle overlaps the square -1..1 - not wholly to one side,
// an edge crossing the square's, a vertex inside it, or a corner inside the triangle.
bool triangle_overlaps_screen(float x0, float y0, float x1, float y1, float x2, float y2);

// 0x004B8220 (with its body Ghidra split off, 0x004B8232): the next triangle off the vertex stack - split into four
// (pushed back) when the cross product of its edges, squared, is above the limit (0x0067DDF0: 82944 as loaded; the
// splat maker sets it for each triangle), else copied to *out (moved past it). False when the stack is empty.
// `format`: the mesh whose vertex format the stack holds (its colour's and texture coordinates' offsets; 0 none).
bool split_next_triangle(core::Cursor<uint8_t>& out, const MeshRef* format);

// 0x004B9A60: the splat set for a texture: the table's, or a new one (its material: one pass of the texture, a random
// checksum - the game's rand() twice -, blend mode 5) put in it.
TextureSplats* get_texture_splats(TextureImage* texture);
// 0x004B9C30: a splat made on the sectors (a null-terminated array) along the ray: their meshes' strip triangles
// that face it (within the cone) and fall inside the projection get the texture - split finer where they're big
// (the pieces inside kept) - into the texture's set's oldest triangles (0x00490CD0). The set, or null if no
// triangle was hit. `objects` (the collision objects found with the sectors) isn't used.
class HitMesh;
TextureSplats* create_texture_splat(void** sectors, HitMesh* const* objects, SplatParams* params);

} // namespace render
