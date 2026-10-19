#pragma once

#include <cstdint>

// Shadows (Nx engine, PC): a shadow caster renders its model into a texture from a light's point of view (in
// the table at 0x007CA970, keyed by its target's address); the meshes around it are drawn again with that texture projected on them.
// Also the stencil shadow volumes of a scene's meshes.
namespace Nx {

// A shadow caster (the table's values).
struct ShadowCaster {
    uint8_t* target;         // the render target: +0x10 its texture, +0x18 its surface
    void* model;             // what casts the shadow
    uint32_t unknown8;
    float view[16];          // from the light
    float projection[16];    // the shadow texture's (near 5)
    float nearProjection[16]; // the receivers' (near 80)
    float textureMatrix[16]; // world -> shadow texture coordinates
};
static_assert(sizeof(ShadowCaster) == 0x10C, "layout must match the original");

// 0x004DA0F0: transform `count` positions by `matrix` into the projection buffer (0x0079B640: each the position,
// the transformed one and its clip flags).
void __cdecl ProjectShadowVertices(const float* positions, uint32_t count, const void* matrix);
// 0x004DA190: of `count` triangles (indices into the buffer), the ones not wholly outside one side and facing
// the texture, into the triangle buffer (0x00796640: position, texture u, v per vertex). Returns their number.
int __cdecl ClipShadowTriangles(const uint16_t* indices, uint32_t count);
// 0x004DB880: the texture matrix: (`first` x) `a` x `b` x the shadow viewport's scale and offset, into `out`
// (the view's inverse is worked out and not used).
void __cdecl ShadowTextureMatrix(const float* a, const float* b, float* out, const float* first);
// 0x004DB9D0: the stencil shadow volumes of the scene's meshes in view (both faces in one pass with two-sided
// stencil 0x007CA97C, else front and back faces apart).
void __cdecl DrawShadowVolumes(const void* scene, int viewport); // (the viewport isn't read)
// 0x004DBBD0: each caster's model into its shadow texture.
void __cdecl RenderShadowTextures();
// 0x004DBF50: each caster's shadow onto the meshes in view (`meshes`, `count`).
void __cdecl DrawShadows(const void* unused, void* const* meshes, int count);
// 0x004DDC90: a new caster drawing `model` into `target` (its key), with identity view and projection.
void __cdecl AddShadowCaster(uint8_t* target, void* model, uint32_t unknown);
// 0x004DB0A0: remove the caster with this key (freed).
void __cdecl RemoveShadowCaster(uint32_t key);
// 0x004DB100: aim the caster with this key from `position` at `target`.
void __cdecl AimShadowCaster(uint32_t key, const float* position, const float* target);

} // namespace Nx
