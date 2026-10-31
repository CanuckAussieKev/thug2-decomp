#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/maths/mat4.h"

// Texture projections on PC (Nx engine: the engine's TextureProjection - constructor 0x0049FE20, destructor body
// 0x004A04A0 - over it; made by the texture projection component, obj/texture_projection_component.h): a texture
// projected through a matrix onto the sectors under it each frame (a skater's shadow), the triangles hit collected
// per texture into batches (up to 10 a frame, 512 triangles each) and drawn after the world.
namespace render {

// A frame's batch for one texture (0x9120 bytes; ten at 0x00705758, how many in use at 0x00705750).
struct ProjectionBatch {
    uint32_t texture;           // +0x00: its key
    uint32_t blend_name_;         // +0x04: the projection's last (a checksum)
    uint32_t triangle_count_;     // +0x08
    uint8_t material[0x110];    // +0x0C (constructor 0x004D1A90)
    void* pixel_shader_;          // +0x11C (material_pixel_shader's)
    uint8_t vertices[0x600][0x18]; // +0x120: position, ARGB colour, u, v

    // 0x004C7610: construct (the material's constructor). Returns this.
    ProjectionBatch* make_new();
    // 0x004C73D0: the destructor (the material's; the ten batches' at exit).
    void teardown();
};
static_assert(offsetof(ProjectionBatch, material) == 0xC && offsetof(ProjectionBatch, pixel_shader_) == 0x11C &&
                  offsetof(ProjectionBatch, vertices) == 0x120 && sizeof(ProjectionBatch) == 0x9120,
              "layout must match the original");

// 0x004C73E0: the frame's batch for a texture: the one made for it already, or the next (its material: one pass of
// the texture, a random checksum - the game's rand() twice -, the blend mode from `blendName`, its pixel shader); a
// message ("hmmm") and null when all ten are taken.
ProjectionBatch* __cdecl get_projection_batch(uint32_t texture, uint32_t blend_name);
// 0x004C7520 (draw_world's): the frame's batches drawn - with linear minification, put back after - and emptied.
void __cdecl draw_projection_batches();

// The PC texture projection (0xC8 bytes, vtable 0x0064A764).
struct PcTextureProjection {
    void** vtable;
    uint8_t engine04[0x14];
    uint8_t* texture;     // +0x18: the texture holder (+0x10 the key, through one more pointer with flag 1 of +0x08)
    uint32_t blend_name_;   // +0x1C
    maths::Mat4 projection; // +0x20: world to the projection's square (-1..1, depth -1 up)
    uint8_t engine60[0xC];
    uint32_t fineness;    // +0x6C: 0 the triangles hit kept whole, else split down to 1 / (16 n) of their size
    uint8_t engine70[0x58];

    // 0x004C7E80: construct. Returns this.
    PcTextureProjection* make_new();
    // 0x004C7EA0 (virtual [0]): destroyed (the engine's destructor body), and freed with flags & 1. Returns this.
    PcTextureProjection* teardown(unsigned int free_flags);
    // 0x004C7620 (virtual [1]): projected onto the sectors (a null-terminated array): their meshes' strip triangles
    // in front of the projection and inside its square get the texture - split finer first with a fineness, the
    // pieces inside kept - into the texture's batch. A full batch ends it there (its buffers left locked, as the
    // original). `unused` isn't.
    void cast_onto(void** sectors, int unused);
};
static_assert(offsetof(PcTextureProjection, texture) == 0x18 && offsetof(PcTextureProjection, projection) == 0x20 &&
                  offsetof(PcTextureProjection, fineness) == 0x6C && sizeof(PcTextureProjection) == 0xC8,
              "layout must match the original");

} // namespace Nx
