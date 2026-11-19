#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
#include <volk.h>

#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_rt_scene.h"

// NOT ORIGINAL - (round OM) opacity micromaps (VK_EXT_opacity_micromap; vulkan_rt_micromaps.cpp, docs/design/vulkan.md
// "Opacity micromaps"): NVIDIA's standard for alpha-tested geometry (foliage, fences) in ray tracing. A ray's candidate
// hit on a cut-out or seen-through instance runs a test in the shader (vulkan_rt_scene.h cutout_kept / hit_blend: the
// triangle's UVs and vertex alphas, a texel, the alpha test) - the largest single cost of the light rays (round PF). An
// opacity micromap splits each triangle of a mesh into 4^level micro-triangles, each with one of four states the
// traversal reads before any candidate is made:
//   - transparent: no hit at all (the shader's test would reject every point of it, in every ray that can meet it);
//   - opaque: a hit committed at once, as a forced-opaque instance's (the test would keep every point);
//   - unknown: a candidate, the shader's test as before.
// So the states never change what a ray finds: they say beforehand what the test would answer, and leave the rest to it.
//
// What a state may say depends on how the instances that use the micromap are traced (MicromapMode, decided per instance
// from its flags and mask - never per object):
//   kCutout     - a cut-out (instance mask 0x04, not blended): every ray's test is the alpha test (cutout_kept; the bounce
//                 and reflection rays' material_alpha_passes_at, with the vertex alpha the game's vertices usually hold).
//                 Opaque where the drawn alpha passes everywhere in the micro-triangle, transparent where it fails
//                 everywhere.
//   kSeenSolid  - not blended, but a seen-through instance (mask 0x80: casts no shadow): its candidates count as light
//                 through it where the test rejects (vulkan_rt_scene.h seen_through), so an opaque state - a commit -
//                 would drop its share: transparent only.
//   kSeenBlended- a blended instance drawn over by an alpha (hit_blend's HIT_COVERAGE: blend 5, 6, 0, 11 - 13, not glass,
//                 its UVs set 0): transparent where the alpha test rejects everywhere (coverage 0: no surface, no share,
//                 the path tracer's coin never lands); filters, glass and added surfaces never (a path counts them).
// "Everywhere" is conservative: the micro-triangle's texels with a texel's margin around them (the filter's footprint,
// the address mode - wrap, mirror, clamp, border), the vertex alpha's range over its corners with 128's (0.5) added, and
// a margin of 1/1024 on the alpha; a footprint past 32 x 32 texels is unknown. Objects (moving, skinned) and dynamic
// meshes have none.
//
// The bake runs on the device (a compute pass reading the same texture level, record and vertex buffer the rays read),
// then the micromaps are built and the mesh's BLAS built with its micromap attached (one per mesh: the material and mode
// of the first instance that wants one; an instance of the mesh with another material or mode is traced as before -
// VK_GEOMETRY_INSTANCE_DISABLE_OPACITY_MICROMAPS_EXT). Baked again, its BLAS built again, when what it was baked from
// changes: the material's record (texture slot, sampler, alpha test, blend, UV set, class), its texture's level 0
// (texture sets, a texture uploaded again), the mesh's vertex alphas (the game's colour writes) or the mesh itself (a
// rebuild). UV animation (the game's UV wibble) is a texture transform the rays never apply - the rays read the mesh's
// UVs as stored, and so does the bake.
//
// The shaders that traverse a scene with micromaps say so (vulkan_rt_glsl.h set_micromaps_declared). Without the
// extension, or with [debug] rt_opacity_micromaps=0, nothing here is made and every instance is traced as before.
// Every ray traced with gl_RayFlagsOpaqueEXT (the passes' "everything solid" camera and occlusion rays) meets the
// transparent micro-triangles as no hit: they pass where the game draws nothing, as the alpha-tested rays always did.
namespace render::vulkan {

struct Backend;

enum class MicromapMode : uint8_t { kNone, kCutout, kSeenSolid, kSeenBlended };

// What a micromap was baked from - another key: baked again.
struct MicromapKey {
    rt::MaterialId material = 0;
    MicromapMode mode = MicromapMode::kNone;
    // the material's record as the device has it (vulkan_rt_materials.h MaterialRecord)
    uint32_t texture_slot = 0;
    uint32_t sampler = 0;
    uint32_t flags = 0;
    uint32_t blend = 0;
    uint32_t uv_set = 0;
    uint32_t reflection_class = 0;
    float alpha_ref = 0.0F;
    uint64_t texture_uploads = 0; // its texture's level 0 uploads (Texture::level0_uploads)
    uint64_t alpha_hash = 0;      // the mesh's vertex alphas
    bool operator==(const MicromapKey&) const = default;
};

// A mesh's micromap and what its BLAS's triangles name it by.
struct MeshMicromap {
    MicromapKey key;           // what it was baked from (mode kNone: none)
    VkMicromapEXT handle = VK_NULL_HANDLE;
    DeviceBuffer storage;      // the micromap
    DeviceBuffer data;         // the states: 2 bits a micro-triangle, a triangle's from data_address + i x its bytes
    VkDeviceAddress data_address = 0;
    DeviceBuffer triangles;    // a VkMicromapTriangleEXT a triangle
    VkDeviceAddress triangles_address = 0;
    DeviceBuffer indices;      // the BLAS's micromap index a triangle (its own: 0, 1, 2...)
    VkDeviceAddress indices_address = 0;
    uint32_t level = 0;        // 4^level micro-triangles a triangle (at least 2: a word)
    uint32_t count = 0;        // triangles
    VkMicromapUsageEXT usage{};
    VkDeviceSize build_scratch = 0;
    uint64_t vertex_writes = 0; // the vertex buffer's writes (Buffer::writes) when alpha_hash was taken
    bool counted = false;       // in micromap_stats
    uint32_t verdict_slot = 0;  // its bake's decided / unknown counts (micromap_verdict)
    uint64_t verdict_serial = 0; // ... written in this submission (0: read, or not baked)
};

// The micromap a BLAS build attaches (copied into its geometry's pNext when recorded).
struct MicromapAttach {
    VkAccelerationStructureTrianglesOpacityMicromapEXT triangles{};
    VkMicromapUsageEXT usage{};
};

// A bake recorded this frame: the mesh's micromap (made by micromap_prepare) and the row its shader reads the mesh by.
struct MicromapBake {
    MeshMicromap* micromap = nullptr;
    GeometryRecord geometry;
};

// Made / released with ray tracing (vulkan_rt_scene.cpp create_ray_tracing / release_ray_tracing; the lock held): on when
// the device was made with the extension. Logs which.
void micromaps_create(Backend& state);
void micromaps_release(Backend& state);
[[nodiscard]] bool micromaps_on();

// How an instance traced with `mask` (its instance mask) and `forced_opaque` (VK_GEOMETRY_INSTANCE_FORCE_OPAQUE) may use a
// micromap; kNone: it can't.
[[nodiscard]] MicromapMode micromap_mode(const rt::Instance& instance, uint32_t mask, bool forced_opaque);

// The key a mesh's micromap for `material` / `mode` has now, but for its vertex alphas (the material table's record and
// texture) - and the hash of the vertex alphas of `vertex_count` vertices (the vertex buffer's copy) that goes in it.
[[nodiscard]] MicromapKey micromap_key(Backend& state, rt::MaterialId material, MicromapMode mode);
[[nodiscard]] uint64_t micromap_alpha_hash(std::span<const uint8_t> vertices, const rt::Geometry& geometry,
                                           uint32_t vertex_count);

// The level for a mesh: its triangles' median area in texels (set 0's UVs, the texture's size), one micro-triangle a
// texel or so, at most `most` and at most kMostMicromapBytes for the mesh.
[[nodiscard]] uint32_t micromap_level(std::span<const uint8_t> vertices, const rt::Geometry& geometry,
                                      std::span<const uint32_t> corners, uint32_t width, uint32_t height);

// A micromap made for `triangles` triangles at `level` (its buffers; the old one retired first): false when the memory
// ran out (none kept).
bool micromap_prepare(Backend& state, MeshMicromap* micromap, const MicromapKey& key, uint32_t triangles,
                      uint32_t level);
// The old one's objects retired with the frame (vulkan_frames.h).
void micromap_retire(Backend& state, MeshMicromap* micromap);
// The attach for a BLAS build of the mesh (the micromap ready or being built this frame).
MicromapAttach micromap_attach(const MeshMicromap& micromap);

// The bakes recorded into `commands` (outside a pass): their states zeroed, the bake pass, the micromaps built (their
// scratch from `scratch`, kScratchAlignment apart, at most `scratch_bytes`), then a barrier - every BLAS build after it
// may take them. False when the pass can't be made (the bakes' micromaps are then dropped by the caller).
bool record_micromap_bakes(Backend& state, VkCommandBuffer commands, std::span<MicromapBake> bakes,
                           VkDeviceAddress scratch, VkDeviceSize scratch_bytes, VkDeviceSize scratch_alignment);
// Whether a baked micromap is worth its traversal (NVIDIA's OMM guidance: one whose micro-triangles are nearly all unknown -
// a soft alpha everywhere - only adds the micromap's lookups to every hit): kWaiting until its bake's submission has
// run, then kDropped (once) when it decided under a quarter of its micro-triangles - the caller builds the mesh again
// without it -, else kKept.
enum class MicromapVerdict : uint8_t { kWaiting, kKept, kDropped };
MicromapVerdict micromap_verdict(Backend& state, MeshMicromap* micromap);

// The scratch the bakes' micromap builds take together.
[[nodiscard]] VkDeviceSize micromap_scratch(std::span<const MicromapBake> bakes, VkDeviceSize scratch_alignment);

// Counts for the log and the checks.
inline constexpr std::size_t kMicromapTally = 16; // (4 modes x 4 states)
struct MicromapStats {
    uint32_t meshes = 0;      // with a micromap
    uint64_t bytes = 0;       // their data and storage
    uint64_t bakes = 0;       // ever
    uint64_t micro_triangles = 0; // in the live micromaps
    uint64_t dropped = 0;         // dropped by their verdict (micromap_verdict)
    // the micro-triangles baked so far by mode (MicromapMode's number) x state (0 transparent, 1 opaque, 2 / 3 unknown)
    std::array<uint64_t, kMicromapTally> states{};
};
[[nodiscard]] MicromapStats micromap_stats();

// The checks' look at a mesh's states (after the frame that baked it has run): 2 bits a micro-triangle, the triangles'
// words in order. False without a micromap.
bool read_micromap_states(Backend& state, const MeshMicromap& micromap, std::vector<uint32_t>* words);

// The micro-triangle index a hit's barycentrics fall in at `level` - the specification's space-filling curve
// (VK_EXT_opacity_micromap's reference BarycentricsToSpaceFillingCurveIndex), for the checks.
[[nodiscard]] uint32_t micromap_index(float u, float v, uint32_t level);

} // namespace render::vulkan
