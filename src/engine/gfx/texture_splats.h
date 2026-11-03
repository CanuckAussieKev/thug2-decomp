#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

#include "engine/core/cursor.h"
#include "engine/gfx/engine_primitive.h"
#include "engine/gfx/projected_vertex.h"
#include "engine/maths/vec2.h"

// Texture splats on PC (Nx engine): textures projected onto the world (skid marks, splashes) - per texture a set of
// up to 256 triangles kept in a table (0x007004B0, lists::KeyMap keyed by the set's address), drawn with a
// perspective correction of their texture coordinates, lit, killed by box or all at once - and the geometry helpers
// the projection uses: 2D segment and triangle tests against the screen square, and splitting triangles that are too
// big (a stack of vertices at 0x006FC4A0).
namespace maths {
class BBox;
}

namespace render {

constexpr std::size_t kSplatTriangleCount = 0x100;
constexpr std::size_t kSplatVertexCount = 0x300; // three vertices a triangle

class BitmapTexture;
struct TextureImage;

struct MaterialRef;

struct MeshRef;

// A splat set: arrays of 256 triangles.
struct TextureSplats {
    std::array<uint32_t, kSplatTriangleCount> alive;           // +0x0000: 0 once a triangle is killed
    std::array<int, kSplatTriangleCount> correction_steps_;    // +0x0400 (the depth correction's, see draw_texture_splats)
    std::array<float, kSplatTriangleCount> correction_a_;      // +0x0800
    std::array<float, kSplatTriangleCount> correction_b_;      // +0x0C00
    std::array<std::array<uint32_t, 3>, kSplatTriangleCount> base_colors_; // +0x1000: each triangle's vertex colours before lighting (RGBA)
    int last;                        // +0x1C00: the last vertex / triangle in use (-1: none)
    uint32_t texture;                // +0x1C04: the texture's checksum (set by the engine's splat maker)
    std::array<uint8_t, 4> unknown1c08_;
    MaterialRef* material;            // +0x1C0C
    std::array<std::array<float, 2>, kSplatVertexCount> uvs;              // +0x1C10: each vertex's texture coordinates before the correction
    std::array<std::array<ProjectedVertex, 3>, kSplatTriangleCount> vertices; // +0x3410: drawn as they are

};

// Vertex `index` counted through the triangles (0-0x2FF).
ProjectedVertex& splat_vertex(TextureSplats& splats, int index);

// 0x00490CD0 (thiscall on a set): the first free triangle of the first `count` (alive 0), else the one with the
// most steps past its start (the first on ties).
[[nodiscard]] int oldest_slot(const TextureSplats& splats, uint32_t count);

// 0x004B7F30: triangle `index` of a set collapsed onto its first vertex (killed).
void collapse_splat_triangle(TextureSplats* splats, int index);
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

constexpr std::size_t kSplatParamsTextureOffset = 0x34;
constexpr std::size_t kSplatParamsUpOffset = 0x3C;
constexpr std::size_t kSplatParamsBytes = 0x44;

// What a splat is made from (create_texture_splat's): a ray from `start` to `end` (the texture projected along it,
// `size` wide, a square or the distance from `up` high), how far its faces may turn from it (`coneAngle`), how long
// it lasts (`lifetime` in seconds; below 0: for ever), the texture (+0x10 of `texture`, through one more pointer with
// flag 1 of its +0x08).
struct SplatParams {
    // The fields other code reads and sets.
    std::array<float, 4>& start() { return start_; }
    [[nodiscard]] const std::array<float, 4>& start() const { return start_; }
    std::array<float, 4>& end() { return end_; }
    [[nodiscard]] const std::array<float, 4>& end() const { return end_; }
    [[nodiscard]] float size() const { return size_; }
    void set_size(float value) { size_ = value; }
    [[nodiscard]] float depth() const { return depth_; }
    void set_depth(float value) { depth_ = value; }
    [[nodiscard]] float angle() const { return angle_; }
    void set_angle(float value) { angle_ = value; }
    [[nodiscard]] float cone_angle() const { return cone_angle_; }
    void set_cone_angle(float value) { cone_angle_ = value; }
    [[nodiscard]] float lifetime() const { return lifetime_; }
    void set_lifetime(float value) { lifetime_ = value; }
    [[nodiscard]] BitmapTexture* texture() const { return texture_; }
    void set_texture(BitmapTexture* value) { texture_ = value; }
    void set_unknown38(uint32_t value) { unknown38 = value; }
    [[nodiscard]] const float* up() const { return up_; }
    void set_up(const float* value) { up_ = value; }
    void set_unknown40(uint8_t value) { unknown40 = value; }

private:
    std::array<float, 4> start_{};
    std::array<float, 4> end_{};
    float size_{0.0f};          // +0x20
    float depth_{0.0f};         // +0x24 (each triangle's correction A)
    float angle_{0.0f};         // +0x28: the texture's turn about the ray (0: a random one) when the ray is steep
    float cone_angle_{0.0f};   // +0x2C
    float lifetime_{0.0f};      // +0x30
    BitmapTexture* texture_{nullptr}; // +0x34
    uint32_t unknown38{kBlendName};
    const float* up_{nullptr}; // +0x3C: a point the projection's up comes from, or null
    uint8_t unknown40{0};     // +0x40
    std::array<uint8_t, 3> pad41{};

public:

    // 0x00490E40: construct: the start and end (0, 0, 0, 1), everything else zero but +0x38 (0x54628ED7). Returns
    // this.
    SplatParams();
private:
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(SplatParams, texture_) == kSplatParamsTextureOffset && offsetof(SplatParams, up_) == kSplatParamsUpOffset &&
                          sizeof(SplatParams) == kSplatParamsBytes,
                      "layout must match the original");
    }
};

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
