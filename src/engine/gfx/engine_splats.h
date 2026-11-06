#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

#include "base/core/checked_index.h"
#include "base/core/layout_32bit.h"
#include "engine/gfx/engine_primitive.h"
#include "engine/gfx/projected_vertex.h"

// Texture splats (Nx engine): textures projected onto the world (skid marks, splashes) - per texture a set of up to 256
// triangles. What the engine keeps and ages (engine_effects.cpp): the sets, what a splat is made from, a triangle
// killed (a plain write into its vertices: called per triangle, so inline here); making, lighting and drawing them is
// the graphics backend's (texture_splats.h).
namespace render {

constexpr std::size_t kSplatTriangleCount = 0x100;
constexpr std::size_t kSplatVertexCount = 0x300; // three vertices a triangle
constexpr int kSplatVertices = 0x300; // the same, as an int count (the loops)

class BitmapTexture;
struct MaterialRef;

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

// 0x004B7F30: triangle `index` of a set collapsed onto its first vertex (killed).
inline void collapse_splat_triangle(TextureSplats* splats, int index)
{
    auto& triangle = core::at(splats->vertices, index);
    core::at(triangle, 1) = core::at(triangle, 0);
    core::at(triangle, 2) = core::at(triangle, 0);
}

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
    float size_{0.0F};                // +0x20
    float depth_{0.0F};               // +0x24 (each triangle's correction A)
    float angle_{0.0F};               // +0x28: the texture's turn about the ray (0: a random one) when the ray is steep
    float cone_angle_{0.0F};          // +0x2C
    float lifetime_{0.0F};            // +0x30
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
        static_assert(core::layout_32bit(offsetof(SplatParams, texture_) == kSplatParamsTextureOffset && offsetof(SplatParams, up_) == kSplatParamsUpOffset &&
                          sizeof(SplatParams) == kSplatParamsBytes),
                      "layout must match the original");
    }
};

} // namespace render
