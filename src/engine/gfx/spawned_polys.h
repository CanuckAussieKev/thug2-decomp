#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

#include "base/core/layout_32bit.h"
#include "base/maths/mat4.h"
#include "base/maths/vec4.h"
#include "engine/memory/slab_bank.h"
#include "engine/gfx/engine_primitive.h"
#include <gsl/pointers>

// The engine's spawned polygons (the SpawnPoly script command): textured or plain quads at a place, turned to face a
// direction, their size and corner colours moved from a start to an end over their lifetime, each drawn by a PC
// primitive of its own; kept in the table 0x007004AC (keyed by address).
namespace render {

class BitmapTexture;

class VertexRun;

constexpr std::size_t kSpawnedPolyDimsOffset = 0x50;
constexpr std::size_t kSpawnedPolyColoursOffset = 0x88;
constexpr std::size_t kSpawnedPolyPrimitiveOffset = 0xA8;
constexpr std::size_t kSpawnedPolyBytes = 0xB8;

// A spawned polygon (0xB8 bytes; the script command's SpawnedPolyDetails, from the pool pair 0x00700500).
struct SpawnedPoly {
    // The fields other code reads and sets.
    maths::Vec4& pos() { return pos_; }
    [[nodiscard]] const maths::Vec4& pos() const { return pos_; }
    maths::Mat4& matrix() { return matrix_; }
    [[nodiscard]] const maths::Mat4& matrix() const { return matrix_; }
    std::array<float, 2>& dims() { return dims_; }
    [[nodiscard]] const std::array<float, 2>& dims() const { return dims_; }
    std::array<float, 2>& start_dims() { return start_dims_; }
    [[nodiscard]] const std::array<float, 2>& start_dims() const { return start_dims_; }
    std::array<float, 2>& end_dims() { return end_dims_; }
    [[nodiscard]] const std::array<float, 2>& end_dims() const { return end_dims_; }
    std::array<std::array<uint8_t, 4>, 4>& start_colours() { return start_colours_; }
    [[nodiscard]] const std::array<std::array<uint8_t, 4>, 4>& start_colours() const { return start_colours_; }
    std::array<std::array<uint8_t, 4>, 4>& end_colours() { return end_colours_; }
    [[nodiscard]] const std::array<std::array<uint8_t, 4>, 4>& end_colours() const { return end_colours_; }
    std::array<std::array<uint8_t, 4>, 4>& colours() { return colours_; }
    [[nodiscard]] const std::array<std::array<uint8_t, 4>, 4>& colours() const { return colours_; }
    uint32_t& flags() { return flags_; }
    [[nodiscard]] uint32_t flags() const { return flags_; }
    [[nodiscard]] float age() const { return age_; }
    void set_age(float value) { age_ = value; }
    float& lifetime() { return lifetime_; }
    [[nodiscard]] float lifetime() const { return lifetime_; }
    [[nodiscard]] BitmapTexture* texture() const { return texture_; }
    void set_texture(BitmapTexture* value) { texture_ = value; }
    [[nodiscard]] VertexRun* primitive() const { return primitive_; }
    void set_primitive(VertexRun* value) { primitive_ = value; }
    uint32_t& blend_mode() { return blend_mode_; }
    [[nodiscard]] uint32_t blend_mode() const { return blend_mode_; }
    [[nodiscard]] float align_h() const { return align_h_; }
    void set_align_h(float value) { align_h_ = value; }
    [[nodiscard]] float align_v() const { return align_v_; }
    void set_align_v(float value) { align_v_ = value; }

private:
    maths::Vec4 pos_{};              // +0x00
    maths::Mat4 matrix_{};           // +0x10: its turn (row 0 the direction, rows 1 / 2 across; row 3 (0, 0, 0, 1))
    std::array<float, 2> dims_{};                // +0x50: the size now
    std::array<float, 2> start_dims_{};         // +0x58
    std::array<float, 2> end_dims_{};           // +0x60
    std::array<std::array<uint8_t, 4>, 4> start_colours_{}; // +0x68: per corner (RGBA)
    std::array<std::array<uint8_t, 4>, 4> end_colours_{};   // +0x78
    std::array<std::array<uint8_t, 4>, 4> colours_{};        // +0x88: now
    uint32_t flags_{};               // +0x98: 1 done; 2 the primitive's +0x14
    float age_{};                    // +0x9C: seconds
    float lifetime_{};               // +0xA0
    BitmapTexture* texture_{};       // +0xA4 (null: untextured)
    VertexRun* primitive_{};       // +0xA8 (the polygon's own: deleted with it)
    uint32_t blend_mode_{kBlendName}; // +0xAC
    float align_h_{0.0F};             // +0xB0 (-1 left, 0 centre, 1 right)
    float align_v_{0.0F};             // +0xB4 (-1 top, 0 centre, 1 bottom)

public:

    // 0x00491990: construct: zeroed, blend mode 0x54628ED7.
    SpawnedPoly();
private:
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(core::layout_32bit(offsetof(SpawnedPoly, dims_) == kSpawnedPolyDimsOffset && offsetof(SpawnedPoly, colours_) == kSpawnedPolyColoursOffset &&
                          offsetof(SpawnedPoly, primitive_) == kSpawnedPolyPrimitiveOffset && sizeof(SpawnedPoly) == kSpawnedPolyBytes),
                      "layout must match the original");
    }
};

namespace engine {

// 0x00492450: a polygon spawned: its primitive made (0x158 bytes; none when out of memory), its turn from
// `direction` (row 0) and `up` (both normalised first; rows 2 and 1 their cross products), its texture, the start
// colours and size, its four corners about its centre (the turn applied - each corner's w as the stack left it in the
// original, 0 here: row 3 is (0, 0, 0, 1)) - with texture coordinates when textured - then readied and put in the
// table.
void spawn_poly(SpawnedPoly* poly, maths::Vec4* direction, maths::Vec4* up);
// 0x004919C0: each polygon not done aged: past its lifetime it's done (its primitive destroyed, taken out of the table
// and freed - 0x004472D0); else its colours and size moved on and its corners set again, placed by its
// justification.
void update_spawned_polys();
// 0x00491E10: every polygon's primitive destroyed, the polygon taken out of the table and freed (the pool pair's).
void kill_all_spawned_polys();

} // namespace engine

memory::PoolPair& spawned_poly_pools();
} // namespace render
