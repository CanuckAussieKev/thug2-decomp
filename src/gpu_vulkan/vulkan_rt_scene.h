#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>
#include <volk.h>

#include "engine/gfx/rt_scene.h"

// NOT ORIGINAL - ray tracing's scene on Vulkan (vulkan_rt_scene.cpp; docs/design/vulkan.md, R2): the acceleration
// structures render::rt::RayTracing (engine/gfx/rt_scene.h) is given meshes and frames for.
//   - A bottom-level structure (BLAS) per mesh, built from the buffers the Gpu draws from (their addresses: with ray
//     tracing on, every vertex and index buffer is made usable as a build input - vulkan_gpu.cpp). Strips become
//     lists in an index buffer of the scene's own (triangle i of the strip is primitive i, degenerate joins kept, so
//     a primitive id counts the mesh's triangles as drawn). Static meshes are built once, then compacted when the
//     build's submission has run; dynamic ones (Geometry::dynamic) are built to be updated: mesh_changed() refits
//     them at the next begin_frame (rebuilt every so many refits, as refits wear the tree).
//   - The top-level structure (TLAS) of each frame, built in begin_frame from the frame record's instances: one per
//     frame in flight (a frame's is left alone until that frame has run), rebuilt each frame - an empty scene is a
//     TLAS of no instances, so a valid handle exists from the first begin_frame on.
//     Each built TLAS is handed to the shadow pass (rt_shadows_set_scene_locked, vulkan_rt_shadows.h); none when the
//     build fails or the scene is released.
// Builds are recorded into the frame's stream (vulkan_frames.h) outside a rendering pass, in batches sharing one
// scratch buffer per frame in flight, after a barrier that makes the buffers' uploads and the processor's writes
// visible to them. Released structures are destroyed once the submissions that may use them have finished.
// Materials (set_material / remove_material) go to the material table (Backend::materials, vulkan_rt_materials.h), whose
// changes begin_frame makes visible first. Installed (render::rt::set_ray_tracing) when the device was made with ray
// tracing (Context::ray_tracing_enabled), by create_device_objects; released with the device's objects.
namespace render::vulkan {

struct Backend;

// An instance's mask (VkAccelerationStructureInstanceKHR::mask), one bit from its InstanceFlags - what R4's shadow rays
// pick by (a ray query's cull mask; 0xFF: everything): kMaskWorld (0x01, vulkan_rt_shadows.h) casts a shadow, opaque,
// not an object; kMaskObjects (0x02) the same for kObject (the skater, peds, vehicles); and:
inline constexpr uint32_t kMaskAlphaTested = 0x04; // casts a shadow, kAlphaTested
inline constexpr uint32_t kMaskNoShadow = 0x80;    // kTransparent, or no kCastsShadow: never casts
// An instance's custom index is its material id (rayQueryGetIntersectionInstanceCustomIndexEXT), 24 bits; its
// instance index (rayQueryGetIntersectionInstanceIdEXT) its place in the frame record's instances. Instances flagged
// kOpaque are forced opaque, the others forced non-opaque (a query sees them as candidates: alpha, R3); back faces are
// never culled.

// The scene the effects trace against (R4, R5): the current frame's TLAS.
//   tlas / address: valid from the first begin_frame until the backend is released; null / 0 before. Built in the
//   stream at begin_frame (outside a pass) and followed by a barrier from the build
//   (VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR) to the
//   fragment, compute and later build stages' VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR: a ray query recorded into
//   the stream after begin_frame needs nothing more. A handle stays usable for the frame it was handed out in (the
//   next begin_frame may return another - each frame in flight has its own; grown TLASes are retired, not destroyed at
//   once). Ask again each frame after begin_frame.
struct SceneView {
    VkAccelerationStructureKHR tlas = VK_NULL_HANDLE;
    VkDeviceAddress address = 0;
    uint32_t instances = 0; // in it (0: an empty scene - every ray misses)
    uint64_t frame = 0;     // the frame record's
    // (added by R5's reflections) the frame's geometry table (below: a GeometryRecord per instance, in the order of
    // the frame record's instances - bind it as a storage buffer beside the TLAS, for the frame it was handed out in)
    // and how many of its instances have a reflective material (rt::Material::reflectivity above 0)
    VkBuffer geometry = VK_NULL_HANDLE;
    VkDeviceSize geometry_bytes = 0;
    uint32_t reflective_instances = 0;
};

// (added by R5's reflections) Where a hit's triangle is, for a shader that shades what a ray hit: per instance of the
// frame (its rayQueryGetIntersectionInstanceIdEXT), the buffers it was built from as device addresses (read by
// GL_EXT_buffer_reference) and the vertex's layout - a strip's corners are the scene's own 32-bit list (triangle i of
// the strip is primitive i, its odd ones turned back round, as the BLAS has them). Written by the processor at
// begin_frame (the buffers' current memory, which the frame's submission then keeps); 48 bytes, scalar layout.
struct GeometryRecord {
    std::array<uint32_t, 2> vertex_address{}; // the vertex buffer's start (low, high word)
    std::array<uint32_t, 2> index_address{};  // the index buffer's start (0: not indexed - corner = primitive x 3 + k)
    uint32_t vertex_offset = 0;               // bytes from vertex_address to the first vertex
    uint32_t stride = 0;
    uint32_t position_offset = 0; // bytes into a vertex: three floats
    uint32_t uv_offset = 0;       // two floats (with kGeometryUv)
    uint32_t colour_offset = 0;   // A8R8G8B8 (with kGeometryColour)
    uint32_t index_offset = 0;    // bytes from index_address to the first index
    uint32_t flags = 0;           // kGeometry*
    uint32_t instance_flags = 0;  // the instance's rt::InstanceFlag
};
inline constexpr std::size_t kGeometryRecordBytes = 48;
static_assert(sizeof(GeometryRecord) == kGeometryRecordBytes);
inline constexpr uint32_t kGeometryLive = 1U << 0;    // its mesh is built (else every other field is 0)
inline constexpr uint32_t kGeometryIndex32 = 1U << 1; // 32-bit indices (else 16-bit)
inline constexpr uint32_t kGeometryUv = 1U << 2;
inline constexpr uint32_t kGeometryColour = 1U << 3;
// (added for the reflections' masks) bits 8 - 11: the texture coordinate sets from uv_offset (rt::Geometry::uv_sets,
// those within the stride; at least 1 with kGeometryUv)
inline constexpr uint32_t kGeometryUvSetsShift = 8;
inline constexpr uint32_t kGeometryUvSetsMask = 0xF;

// The GLSL for the table (after `#version 460`; `#define THUG2_GEOMETRY_SET n` / `THUG2_GEOMETRY_BINDING n` first to
// bind it somewhere other than set 0, binding 7). It enables GL_EXT_buffer_reference(_uvec2) and GL_EXT_scalar_block_layout and
// declares:
//   struct GeometryRecord { ... };   GeometryRecord geometry_record(uint instance)  (past the end: not live)
//   struct HitVertices { vec3 position[3]; vec2 uv[3]; vec4 colour[3]; uint flags; };
//   bool hit_vertices(uint instance, uint primitive, out HitVertices v)  - false when the instance isn't live; a mesh
//        without texture coordinates / colours gives (0, 0) / (1, 1, 1, 1) - the colour's 128 is 1.0 x 128 / 255
//   vec2 hit_uv(HitVertices v, vec2 barycentrics), vec4 hit_colour(HitVertices v, vec2 barycentrics)
//   vec2 hit_uv_set(uint instance, uint primitive, vec2 barycentrics, uint set) - texture coordinate set `set` at the
//        hit (a material's pass n reads set n); a set the mesh hasn't: set 0 (no coordinates at all: (0, 0))
// The positions are the object's (rayQueryGetIntersectionObjectToWorldEXT takes them to the world).
std::string_view scene_geometry_glsl();
// The caller holds the backend's lock.
SceneView current_scene(const Backend& state);

// Made / released with the device's objects (vulkan_gpu.cpp; the caller holds the lock): installs / uninstalls the
// RayTracing and logs whether ray tracing is on, on which device, or why not.
void create_ray_tracing(Backend& state);
void release_ray_tracing(Backend& state);
// The usage a buffer the Gpu draws from also needs (vulkan_gpu.cpp's make_buffer): device addresses and build input
// when ray tracing is on, else none.
VkBufferUsageFlags ray_tracing_buffer_usage(const Backend& state);

// Rays traced now through the current TLAS (a compute shader with ray queries, every instance taken as opaque, each
// ray's cull mask its own; the
// frame's work finished first, then the dispatch run and waited for): the checks' and rtprobe's. Takes the lock.
inline constexpr uint32_t kMaskEverything = 0xFF; // (a cull mask that sees every instance)
struct TracedRay {
    std::array<float, 3> origin{};
    std::array<float, 3> direction{}; // (any length: distances are in its units, t)
    float t_min = 0.0F;
    float t_max = 0.0F;
    uint32_t cull_mask = kMaskEverything; // the instances it can hit (their masks: above)
};
struct TracedHit {
    bool hit = false;
    float t = 0.0F;
    uint32_t instance = 0;     // its index in the frame record's instances
    uint32_t custom_index = 0; // the instance's material id
    uint32_t primitive = 0;    // the triangle in the mesh
    std::array<float, 2> barycentrics{};
    bool front_face = false;
};
enum class TraceResult : uint8_t { kTraced, kNoRayTracing, kNoScene, kFailed };
TraceResult trace_rays_now(std::span<const TracedRay> rays, std::vector<TracedHit>* hits);

// Counts for the log and the checks.
struct SceneStats {
    uint32_t meshes = 0;          // registered
    uint32_t built = 0;           // with a BLAS
    uint32_t compacted = 0;       // of them compacted
    uint32_t dynamic = 0;
    uint32_t refused = 0;         // registered but not buildable (logged once each)
    uint64_t blas_bytes = 0;      // the BLASes' buffers
    uint64_t built_bytes = 0;     // what the compacted ones took before
    uint64_t tlas_bytes = 0;      // every frame's TLAS buffers and instance buffers
    uint64_t scratch_bytes = 0;   // the scratch buffers
    uint64_t builds = 0;          // BLAS builds recorded, ever
    uint64_t refits = 0;          // updates recorded, ever
    uint64_t tlas_builds = 0;
    uint32_t last_instances = 0;  // in the last TLAS
    uint32_t retired_pending = 0; // structures and buffers waiting for their submissions
};
SceneStats scene_stats(); // takes the lock

} // namespace render::vulkan
