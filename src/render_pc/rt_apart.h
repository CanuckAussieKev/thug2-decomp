#pragma once

// NOT ORIGINAL - (round PG) what the game draws on the processor each frame for the passes apart - the particles (the
// particle systems' and the flexible particles' quads, ribbons and stars) and the level's billboards (cards turned to the
// camera) - kept as it is drawn, for the rays: rt_record makes the next frame's per-frame meshes of it
// (engine/gfx/rt_apart.h - RTX Remix's way: the game's own draws captured, their triangles as the GPU took them, their
// material the draw's). Nothing is drawn differently; with no RayTracing installed (or [debug] rt_particles=0) every
// call here is a test of one flag.
//
// Only the first viewport's draws are kept (split screen draws the same particles again, turned to its own camera), and
// only what the game draws: a particle system the view culls draws nothing, so it has no triangles that frame. The
// triangles are the draw's - turned toward the camera that drew them - a frame before the rays that meet them (the
// record is made before the frame's draws, as the objects' lights are).
//
// A particle draw: its material handed in (MaterialRef::hand_in / submit_states - material_used), its vertices written
// to the renderer's dynamic buffer (unlock_dynamic_vertices - vertices_unlocked: the run kept), then drawn from there as
// quads (the quad index buffer: 0 1 2, 2 3 0 a quad - quads_drawn) or a list (list_drawn). The glows drawn from memory
// (PcGlowParticles: added light - no surface for a ray) are not kept.
// A billboard: its turned vertices written into its own buffer and drawn with its own indices (billboard_drawn).
#include <cstdint>
#include <span>
#include <vector>

namespace render {

struct MeshRef;
struct MaterialRef;

namespace rt_apart {

// draw_frame: the viewport about to be drawn (the first's draws are kept), and the particle passes begun / ended.
void view_begins(int viewport);
void set_particle_passes(bool inside);
// A particle draw is kept now (the first viewport's particle passes, the rays want them).
[[nodiscard]] bool capturing();
// Whether the rays want them at all: a RayTracing installed and [debug] rt_particles (default 1).
[[nodiscard]] bool wanted();

void material_used(const MaterialRef* material);
void vertices_unlocked(std::span<const uint8_t> bytes, uint32_t first_vertex, uint32_t stride);
// The run from `first_vertex` drawn: `triangles` of quads (two a quad) / `vertices` of a list.
void quads_drawn(uint32_t first_vertex, uint32_t triangles);
void list_drawn(uint32_t first_vertex, uint32_t vertices);
// A billboard drawn: its vertices as written for this camera (the mesh's stride and format), its indices its own.
void billboard_drawn(const MeshRef* mesh, std::span<const uint8_t> vertices);

// A draw kept: its material, vertex format and stride, and its triangles' vertices (three a triangle).
struct Draw {
    const MaterialRef* material = nullptr;
    uint32_t fvf = 0;
    uint32_t stride = 0;
    std::vector<uint8_t> vertices;
};
// The draws kept since the last take (rt_record's record_frame: the frame before's).
[[nodiscard]] std::vector<Draw> take();
// A material destroyed: draws kept with it forgotten (its pointer may be another's next).
void material_released(const MaterialRef* material);

} // namespace rt_apart
} // namespace render
