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
// visible to them. Released structures are destroyed once the frames that may use them have finished (round GF: retired
// to the frame - vulkan_frames.h -, as a frame's TLAS names its BLASes and its geometry table names buffers by address
// for all of its passes, even after a read-back finished it part-way).
// Materials (set_material / remove_material) go to the material table (Backend::materials, vulkan_rt_materials.h), whose
// changes begin_frame makes visible first. Installed (render::rt::set_ray_tracing) when the device was made with ray
// tracing (Context::ray_tracing_enabled), by create_device_objects; released with the device's objects.
namespace render::vulkan {

struct Backend;

// An instance's mask (VkAccelerationStructureInstanceKHR::mask), one bit from its InstanceFlags - what R4's shadow rays
// pick by (a ray query's cull mask; 0xFF: everything): kMaskWorld (0x01, vulkan_rt_shadows.h) casts a shadow, opaque,
// not an object; kMaskObjects (0x02) the same for kObject (the skater, peds, vehicles); and:
inline constexpr uint32_t kMaskAlphaTested = 0x04; // casts a shadow, kAlphaTested (also a cut-out kTransparent one)
inline constexpr uint32_t kMaskNoShadow = 0x80;    // kTransparent (not cut out), or no kCastsShadow: no solid caster -
                                                   // the seen-through surfaces every light ray takes its share through
                                                   // (round PT-B: ray_light, rt_surface_class.h kSeenThrough)
inline constexpr uint32_t kMaskAddsLight = 0x40;   // (round PT-B) kAddsLight: no surface for a light ray (light passes it
                                                   // whole); only a path's rays and the camera's probes ask for it
// (round PG) rt::kDrawnApart, solid or cut out (by its alpha, as kMaskAlphaTested): the clutter's items, the particles,
// the billboards - drawn by passes of their own after the ray-traced lighting, so in the light rays' and the secondary
// rays' masks (the sun's and the lamps' shadows, the occlusion, the bounce, the reflected rays, the fog's light, the path
// tracer) and never in a camera ray's (their pixels aren't in the depth the passes read). A seen-through one is
// kMaskNoShadow as any other (with the instance flag: seen_through takes it when the ray's mask has kMaskDrawnApart;
// drawn_kept never), an added one kMaskAddsLight.
inline constexpr uint32_t kMaskDrawnApart = 0x08;
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
    // (path tracing, stage 1) the TLAS's instances (VkAccelerationStructureInstanceKHR, 64 bytes each in the frame
    // record's order - its first 48 bytes the object-to-world rows, x' = row . (x, y, z, 1)) as a device address, for a
    // shader that needs an instance's transform without a hit (a light's triangle sampled)
    VkDeviceAddress instances_address = 0;
    // (round NOBIAS3) What a scan of the instances' triangles reads besides the record's instances, as keys over the
    // world's instances (the objects come and go): live_key which are built (and their triangle counts - the scan's places), light_key which of them read the relit world's
    // light store (its stream given - the store's emission is read through it). A list of the emitters made before a
    // mesh was built or its stream given never saw its triangles: New Orleans' list kept 319 of the level's 584 emitting
    // triangles all session (made in the level's first frames, while its streams were still being given).
    uint64_t live_key = 0;
    uint64_t light_key = 0;
};
// (path tracing, stage 1) The current frame's instances' triangle counts, in the frame record's order (0: not built) -
// the primitives a shader may ask hit_vertices for. The caller holds the lock.
void scene_instance_triangles(const Backend& state, std::vector<uint32_t>* out);

// (added by R5's reflections) Where a hit's triangle is, for a shader that shades what a ray hit: per instance of the
// frame (its rayQueryGetIntersectionInstanceIdEXT), the buffers it was built from as device addresses (read by
// GL_EXT_buffer_reference) and the vertex's layout - a strip's corners are the scene's own 32-bit list (triangle i of
// the strip is primitive i, its odd ones turned back round, as the BLAS has them). Written by the processor at
// begin_frame (the buffers' current memory, which the frame's submission then keeps); 68 bytes, scalar layout.
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
    uint32_t normal_offset = 0;   // (added for the skater's smooth shading) three floats (with kGeometryNormal)
    // (added by the light energy audit) the mesh's light stream (rt::RayTracing::set_light_stream: rt_cel.h's words,
    // kCelStreamBytes a vertex from its start - with kGeometryLight)
    std::array<uint32_t, 2> light_address{};
    // (the one lighting path - the objects' shadows) its object's lights (rt::ObjectLight: kObjectLightFloats floats,
    // the frame's own buffer - with kGeometryObjectLight)
    std::array<uint32_t, 2> object_light_address{};
    // (round L - the physical light store) what the light stream's light is multiplied by where it is read: the frame's
    // pre-exposure (rt_daylight.h frame_pre_exposure)
    float light_exposure = 0.0F;
    // (round GF - bounds) what may be read through the addresses: the mesh's triangles (a primitive past them is not
    // followed), the vertices its vertex buffer holds from vertex_offset (a corner past them is not read), and the
    // vertices its light stream holds (with kGeometryLight)
    uint32_t triangles = 0;
    uint32_t vertices = 0;
    uint32_t light_vertices = 0;
};
inline constexpr std::size_t kGeometryRecordBytes = 84;
static_assert(sizeof(GeometryRecord) == kGeometryRecordBytes);
inline constexpr uint32_t kGeometryLive = 1U << 0;    // its mesh is built (else every other field is 0)
inline constexpr uint32_t kGeometryIndex32 = 1U << 1; // 32-bit indices (else 16-bit)
inline constexpr uint32_t kGeometryUv = 1U << 2;
inline constexpr uint32_t kGeometryColour = 1U << 3;
inline constexpr uint32_t kGeometryNormal = 1U << 4; // (added for the skater's smooth shading) the vertex normals
inline constexpr uint32_t kGeometryLight = 1U << 5;  // (added by the light energy audit) the light stream
inline constexpr uint32_t kGeometryObjectLight = 1U << 6; // (the objects' shadows) its object's lights
// (the objects' shadows) An object's lights as the shaders read them: rt::ObjectLight's seven colours / directions,
// three floats each, in its order; then (round O) its `linear` (1 or 0) and two zeros.
inline constexpr std::size_t kObjectLightFloats = 24;
// (added for the reflections' masks) bits 8 - 11: the texture coordinate sets from uv_offset (rt::Geometry::uv_sets,
// those within the stride; at least 1 with kGeometryUv)
inline constexpr uint32_t kGeometryUvSetsShift = 8;
inline constexpr uint32_t kGeometryUvSetsMask = 0xF;

// The GLSL for the table (after `#version 460`; `#define THUG2_GEOMETRY_SET n` / `THUG2_GEOMETRY_BINDING n` first to
// bind it somewhere other than set 0, binding 7). It enables GL_EXT_buffer_reference(_uvec2) and GL_EXT_scalar_block_layout and
// declares:
//   struct GeometryRecord { ... };   GeometryRecord geometry_record(uint instance)  (past the end: not live)
//   bool geometry_triangle(GeometryRecord g, uint primitive, out uvec3 corners) - (round GF) a live record's triangle's
//        corners; false when the primitive is past its triangles or a corner past its vertices (nothing to read there)
//   bool geometry_light_corners(GeometryRecord g, uvec3 corners) - (round GF) every corner within its light stream
//   struct HitVertices { vec3 position[3]; vec2 uv[3]; vec4 colour[3]; uint flags; };
//   bool hit_vertices(uint instance, uint primitive, out HitVertices v)  - false when the instance isn't live; a mesh
//        without texture coordinates / colours gives (0, 0) / (1, 1, 1, 1) - the colour's 128 is 1.0 x 128 / 255
//   vec2 hit_uv(HitVertices v, vec2 barycentrics), vec4 hit_colour(HitVertices v, vec2 barycentrics)
//   vec2 hit_uv_set(uint instance, uint primitive, vec2 barycentrics, uint set) - texture coordinate set `set` at the
//        hit (a material's pass n reads set n); a set the mesh hasn't: set 0 (no coordinates at all: (0, 0))
//   bool hit_normal(uint instance, uint primitive, vec2 barycentrics, out vec3 normal) - (added for the skater's smooth
//        shading) the vertex normals blended at the hit, the object's (not normalised); false when the mesh has none
//   struct WorldLight { vec3 ambient; float n_dot_l; vec3 tint; float seen; vec3 lamps; vec3 sun; vec3 sky;
//        float sky_share; vec3 emit; };
//   bool hit_world_light(uint instance, uint primitive, vec2 barycentrics, out WorldLight w) - (added by the light
//        energy audit) the relit world's light stream blended at the hit, in the frame's unit (round L: the store's
//        physical light x the record's light_exposure); false when the mesh has none or a corner has no stream (an
//        animated vertex, a hidden mesh). `emit` the surface's own emission (radiance x the texel)
//   vec3 world_light_with(WorldLight w, float seen, vec3 sky) - the light a vertex colour of that stream shows with the
//        sun's visibility `seen` and the sky's part `sky`: tint x (bounce + sky + sun x N.L x seen) + lamps + emit
//   struct ObjectLight { vec3 ambient; vec3 key; vec3 key_direction; vec3 fill; vec3 fill_direction; vec3 local;
//        vec3 local_direction; bool linear; } - (round O) `linear`: the light store's linear light in the global unit
//        (render_pc/pc_lights.h OneLightSent), else the game's display-referred lights
//   bool hit_object_light(uint instance, out ObjectLight l) - (the objects' shadows) the lights the instance's object
//        is lit by (rt::ObjectLight); false when the record has none
//   vec3 object_light_with(ObjectLight l, vec3 normal, float seen) - the light a surface of the object facing `normal`
//        (the world's, unit) takes with the sun's visibility `seen` on its key light: ambient + key x N.L x seen +
//        fill x N.L + local x N.L, each N.L toward where its light comes from (Direct3D's lighting; at least 0)
// The positions are the object's (rayQueryGetIntersectionObjectToWorldEXT takes them to the world).
std::string_view scene_geometry_glsl();
// (the one lighting path's rays) Every ray that asks whether light gets through - the sun's shadows, the bounce light's
// rays and its sun rays, the ambient occlusion's, the lamps' shadow rays - takes the cut-out casters (fences, foliage,
// grates, a cannon's spoked wheels: kMaskAlphaTested, their instances not forced opaque) by their alpha, the same test
// the game's draw makes, so light goes through their holes everywhere. After scene_geometry_glsl and material_glsl
// (vulkan_rt_materials.h), with the TLAS bound as `u_scene`:
//   bool ray_blocked(uint mask, vec3 origin, vec3 direction, float reach) - blocked before `reach` by an instance `mask`
//        picks: an opaque one wherever it is hit, an alpha-tested one only where its material's alpha test passes at the
//        hit (a hit whose vertices can't be read counts as solid)
// (path tracing, stage 3) How a ray meets a blended surface, from its material's first pass (never per object):
//   struct HitBlend { uint kind; float coverage; vec3 tint; };
//   HitBlend hit_blend(uint instance, uint primitive, uint id, vec2 barycentrics) - HIT_SOLID (not kTransparent),
//        HIT_COVERAGE (an alpha blend: a surface over `coverage` - the drawn alpha), HIT_GLASS (seen through, glass
//        class: `coverage` painted, the rest a thin pane), HIT_FILTER (a multiply: the light behind x `tint`), HIT_ADDED
//        (add / subtract / brighten: light passes, its emission added)
//   float hit_glass_reflectance(float cos_theta) - a thin pane's Fresnel share (Schlick, the material table's glass F0)
//   vec3 hit_blend_transmittance(HitBlend b, float cos_theta) - the expected light a shadow ray takes through it.
// (round CW) The surface the per-surface lighting's prepass keeps at a pixel (vulkan_surface_light.h):
//   bool drawn_kept(uint instance, uint primitive, uint id, vec2 barycentrics) - an opaque / cut-out one by its alpha
//        test, a blended one (HIT_COVERAGE / HIT_GLASS) at least half opaque, never a filter or an added one
//   bool trace_drawn_layered(... the same, out bool layered, out layer_instance, layer_primitive, layer_id,
//        layer_barycentrics, layer_world) - (round BAKEALB) and the blended world layer lying on the surface it found
//   bool trace_drawn(vec3 origin, vec3 dir, float reach, float expected, out instance, primitive, id, barycentrics, t,
//        to_world) - the nearest such surface along a camera ray, every drawn instance (RAY_DRAWN 0x87) asked; (round
//        DECAL) a blended world one lying on the solid surface behind it (within RAY_LAYER_LIFT - ff_key.h kLayerLift -
//        along its own normal: layer_gap) is that surface's material layer, unless the pixel's depth (`expected`, its
//        distance; negative: unknown) is the blended one's own (a layer that writes depth, kept by the prepass)
//   float layer_gap(uint instance, uint primitive, mat4x3 to_world, vec3 dir, float t, float t_behind) - how far a
//        triangle met at t lies from the point at t_behind along its own normal
// (round PT-B - one light ray: engine/gfx/rt_surface_class.h) Every light ray's visibility:
//   vec3 seen_through(uint mask, vec3 origin, vec3 dir, float reach) - what the seen-through instances (0x80) of the
//        kinds `mask` picks (world 0x01 / 0x04, objects 0x02) let through before `reach`: their hit_blend_transmittance
//        multiplied, whatever their order (the caller has found nothing solid there); (round DECAL) a blended world layer
//        lying on the ray's own surface (within RAY_LAYER_LIFT of the origin along its normal) lets it all through - it
//        is that surface's material, laid in its albedo
//   vec3 ray_light(uint mask, vec3 origin, vec3 direction, float reach) - 0 when ray_blocked, else seen_through
//   float ray_light_share(...) - the same as one share (the mean of the three channels), for a pass that keeps one
std::string_view cutout_rays_glsl();
// (path tracing, stage 1 - P1: one hit surface) What every ray that shades its hit reads there, from one function: after
// material_glsl (vulkan_rt_materials.h), scene_geometry_glsl, rt_display.h's kDisplayTransferGlsl and rt_brdf.h's
// brdf_glsl():
//   struct HitSurface { vec3 position; vec3 normal; vec3 shading_normal; vec3 base_colour; BrdfMaterial material;
//                       uint kind; float alpha; bool cut_out; vec3 emitted; uint origin; float area; bool hidden; };
//   bool hit_surface(uint instance, uint primitive, uint id, vec2 barycentrics, mat4x3 object_to_world, vec3 direction,
//                    out HitSurface s)   - false when the instance isn't live (no vertices to read)
//   bool hit_surface_at(... the same, HitCone cone, out HitSurface s) - (round BAKEALB) a camera ray's: its base colour
//                    at the raster's mips (hit_albedo_at with the cone); hit_surface is it with no_cone()
//   bool camera_surface(vec3 eye, vec3 dir, float reach, float expected, HitCone cone, out uint instance, out uint
//                    primitive, out uint id, out vec2 barycentrics, out float t, out mat4x3 to_world, out HitSurface h) -
//                    (round BAKEALB - one layer set) a camera ray's surface: trace_drawn's (false when none, or not at
//                    `expected` - its depth), its passes at the cone's levels, the blended world layer lying on it
//                    (trace_drawn_layered) laid over its material by the layer's drawn alpha - the raster's DBuffer rule
//   - position: the hit (world); normal: the triangle's, unit, turned toward where the ray came from (-direction);
//     shading_normal: the mesh's vertex normals blended there (same side) where it has them, else `normal`; area: the
//     triangle's (world units^2 - a light sample's pdf);
//   - base_colour: linear - the relit world (HIT_RELIT_WORLD: it has the light store) its texel x its material's colour,
//     decoded, x the store's tint (relit's albedo); an object (HIT_OBJECT) the texel x the material's colour; the
//     world without a light store (HIT_BAKED_WORLD: its colours are its bake's light) the texel x the material's colour
//     alone (no albedo of its own yet - the plan's P10);
//   - material: the light model's (rt_brdf.h) - with a surface map, F0 the material table's for the map's kind
//     (MaterialRecord::f0) and roughness the map's at the hit, the detail kind's where it replaces it; a metal's
//     (HIT_KIND_METAL: F0 at least kHitMetalF0) F0 its base colour, no diffuse; without a map the table's painted
//     surface (rt_material_table.h kPainted);
//   - alpha: the texel's x the material's; cut_out: the material's alpha test rejects it there (the ray goes on);
//   - emitted: the radiance the surface sends itself, in the light store's global unit (NOT pre-exposed): the store's
//     emit word x the texel (the lamp's texture) - on a lamp drawn in a texture only its lamp texels (their brightest
//     channel past rt_lights.h kTexelBright, as the world's draws show it); 0 elsewhere;
//   - hidden: the light store hides the mesh (the bake's shadows drawn as geometry, the original's light cards).
//   bool hit_emission(uint instance, uint primitive, uint id, vec2 barycentrics, out vec3 emitted) - (round PT4b) the
//     same `emitted` alone (no normals, no material - a light sample's own emission, ReSTIR DI's target at every
//     candidate); false where hit_surface would say cut_out or hidden, or the instance isn't live
//   bool hit_emission_of(uint instance, uint primitive, uint id, vec2 barycentrics, HitVertices v, out vec3 emitted) -
//     (round PERF2) the same with the triangle's corners already read (hit_vertices): the texel read once
// How a ray meets a blended surface (hit_blend) is cutout_rays_glsl's: it goes before this.
std::string_view hit_surface_glsl();
// (round BAKEALB - one albedo definition) hit_surface_glsl's first part, alone (the ray probe's: no BRDF): after
// material_glsl, scene_geometry_glsl and kDisplayTransferGlsl -
//   vec4 hit_composed_colour(uint instance, uint primitive, uint id, vec2 barycentrics, vec2 uv, float vertex_alpha) -
//        the material's passes composed at the hit (material_colour, each layer at its uv set)
//   vec3 hit_albedo_at(... the same) - that colour decoded (linear): the material's albedo before the light store's
//        tint - hit_surface's base_colour / tint
//   struct HitCone { vec3 origin; vec3 dx; vec3 dy; float bias; float most_anisotropy; }; HitCone no_cone() - a camera
//        ray's footprint (its neighbouring pixels' rays, the raster's level bias and anisotropy); no_cone: none
//   vec3 hit_albedo_at(... the same, vec3 a, vec3 b, vec3 c, HitCone cone) - with a cone (the triangle's world corners)
//        each pass at its texture's level for the pixel (ray differentials: the raster's mip); without, the most detailed
//   vec3 hit_albedo(uint instance, uint primitive, uint id, vec2 barycentrics) - the same without a cone, its uv and
//        vertex alpha read (0 where the instance isn't live)
std::string_view hit_albedo_glsl();
inline constexpr float kHitMetalF0 = 0.2F; // (a surface map's F0 at least this: a metal - the reflections' HIT_METAL_F0)
// The caller holds the backend's lock.
SceneView current_scene(const Backend& state);

// Made / released with the device's objects (vulkan_gpu.cpp; the caller holds the lock): installs / uninstalls the
// RayTracing and logs whether ray tracing is on, on which device, or why not.
void create_ray_tracing(Backend& state);
void release_ray_tracing(Backend& state);
// (round GF) One of the Gpu's buffers destroyed (vulkan_gpu.cpp destroy_buffer; the caller holds the lock): the meshes
// built from it and the light streams given as it are forgotten now (logged - their owners release them first), so no
// later frame takes another buffer made at the same address for it.
struct Buffer;
void rt_scene_buffer_released_locked(Backend& state, const Buffer* buffer);
// The usage a buffer the Gpu draws from also needs (vulkan_gpu.cpp's make_buffer): device addresses and build input
// when ray tracing is on, else none.
VkBufferUsageFlags ray_tracing_buffer_usage(const Backend& state);

// Rays traced now through the current TLAS (a compute shader with ray queries, each ray's cull mask its own; the
// frame's work finished first, then the dispatch run and waited for): the checks', rtprobe's and the relit level's
// (render::rt::RayTracing::probe_many). Every instance is taken as solid, unless the ray asks for the cut-outs: then the
// one lighting path's test (cutout_rays_glsl's) - an alpha-tested instance hit only where its material's alpha test
// passes at the hit, and an object's not at all. Takes the lock.
inline constexpr uint32_t kMaskEverything = 0xFF; // (a cull mask that sees every instance)
struct TracedRay {
    std::array<float, 3> origin{};
    std::array<float, 3> direction{}; // (any length: distances are in its units, t)
    float t_min = 0.0F;
    float t_max = 0.0F;
    uint32_t cull_mask = kMaskEverything; // the instances it can hit (their masks: above)
    bool cutouts = false;                 // (added by lighting round 4) the cut-outs by their alpha
    bool albedo = false;                  // (round BAKEALB) the hit's albedo wanted (TracedHit::albedo)
};
struct TracedHit {
    bool hit = false;
    float t = 0.0F;
    uint32_t instance = 0;     // its index in the frame record's instances
    uint32_t custom_index = 0; // the instance's material id
    uint32_t primitive = 0;    // the triangle in the mesh
    std::array<float, 2> barycentrics{};
    bool front_face = false;
    float seen_through = 1.0F; // (round PT-B) a ray with the cut-outs: rt::ProbeResult::seen_through
    std::array<float, 3> albedo{1.0F, 1.0F, 1.0F}; // (round BAKEALB) with `albedo` asked: rt::ProbeResult::albedo
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
    // why the builds were asked for, ever: a mesh registered (or registered again), its geometry replaced
    // (update_mesh with other triangles), a static mesh's vertices rewritten (mesh_changed - never a colour-only write)
    uint64_t asked_new = 0;
    uint64_t asked_geometry = 0;
    uint64_t asked_written = 0;
    uint64_t rebuilt_after_refits = 0; // a dynamic mesh built again once kRefitsBeforeRebuild refits had worn it
    uint32_t last_instances = 0;  // in the last TLAS
    // (round GF) the Gpu's buffers destroyed while the scene still named them (their owner released the buffer first):
    // the meshes left out and the light streams taken back then
    uint64_t released_first_meshes = 0;
    uint64_t released_first_streams = 0;
    uint32_t retired_pending = 0; // structures and buffers waiting for their submissions
};
SceneStats scene_stats(); // takes the lock

// (round OM - the checks') A mesh's opacity micromap (vulkan_rt_micromaps.h): its level (4^level micro-triangles a
// triangle), triangles, material and mode (MicromapMode's number), and its states read back - 2 bits a micro-triangle,
// each triangle's from word triangle x 4^level / 16 (the frame's work finished first). False without one, or without
// opacity micromaps on the device. Takes the lock.
struct MicromapView {
    uint32_t level = 0;
    uint32_t triangles = 0;
    uint32_t material = 0;
    uint32_t mode = 0;
    std::vector<uint32_t> words;
};
bool scene_micromap(rt::MeshId mesh, MicromapView* out);
// Whether the device was made with opacity micromaps (and they are on).
bool scene_micromaps_on();

} // namespace render::vulkan
