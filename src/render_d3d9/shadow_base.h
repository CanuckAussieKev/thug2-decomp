#pragma once

#include <cstdint>
#include <array>
#include <span>
#include "base/maths/mat4.h"

// Shadows (Nx engine, PC): a shadow caster renders its model into a texture from a light's point of view (in
// the table at 0x007CA970, found by its target); the meshes around it are drawn again with that texture projected on them.
// Also the stencil shadow volumes of a scene's meshes.
namespace render {


struct TextureRef;
struct MeshRef;

// A shadow caster (the table's values).
struct ShadowCaster {
    TextureRef* target; // the render target (its texture and surface)
    void* model;     // what casts the shadow
    uint32_t unknown8;
    std::array<float, maths::kMatrixFloats> view;             // from the light
    std::array<float, maths::kMatrixFloats> projection;       // the shadow texture's (near 5)
    std::array<float, maths::kMatrixFloats> near_projection_; // the receivers' (near 80)
    std::array<float, maths::kMatrixFloats> texture_matrix_;  // world -> shadow texture coordinates
};
constexpr std::size_t kShadowCasterBytes = 0x10C;

static_assert(sizeof(ShadowCaster) == kShadowCasterBytes, "layout must match the original");

// 0x004DA0F0: transform `count` positions by `matrix` into the projection buffer (0x0079B640: each the position,
// the transformed one and its clip flags).
void project_shadow_vertices(std::span<const float> positions, const void* matrix);
// 0x004DA190: of `count` triangles (indices into the buffer), the ones not wholly outside one side and facing
// the texture, into the triangle buffer (0x00796640: position, texture u, v per vertex). Returns their number.
int clip_shadow_triangles(std::span<const uint16_t> indices);
// 0x004DB880: the texture matrix: (`first` x) `a` x `b` x the shadow viewport's scale and offset, into `out`
// (the view's inverse is worked out and not used).
void shadow_texture_matrix(const float* a, const float* b, float* out, const float* first);
// 0x004DB9D0: the stencil shadow volumes of the scene's meshes in view (both faces in one pass with two-sided
// stencil 0x007CA97C, else front and back faces apart).
void draw_shadow_volumes(const void* scene, int viewport); // (the viewport isn't read)
// 0x004DBBD0: each caster's model into its shadow texture.
void render_shadow_textures();
// 0x004DBF50: each caster's shadow onto the meshes in view (`meshes`, `count`).
void draw_shadows(const void* unused, std::span<MeshRef* const> meshes);
// 0x004DDC90: a new caster drawing `model` into `target` (its key), with identity view and projection.
void add_shadow_caster(TextureRef* target, void* model, uint32_t unknown);
// 0x004DB0A0: remove the caster with this key (freed).
void remove_shadow_caster(const TextureRef* key);
// 0x004DB100: aim the caster with this key from `position` at `target`.
void aim_shadow_caster(const TextureRef* key, std::span<const float, 3> position, std::span<const float, 3> target);

// NOT ORIGINAL (ours, for THAW's BMX - game/rider/thaw_bmx.h): asked for each caster's model as render_shadow_textures
// draws it; a model returned is drawn into the same shadow texture after it (the rider's shadow with his bike in it).
// Null: nothing more. The function must answer from live state - it's asked every frame.
using ShadowCompanionFn = void* (*)(void* caster_model);
void set_shadow_companion(ShadowCompanionFn fn);

} // namespace render
