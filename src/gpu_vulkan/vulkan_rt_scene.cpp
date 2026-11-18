// NOT ORIGINAL - ray tracing's scene on Vulkan: the BLASes, the TLAS per frame, the probe (vulkan_rt_scene.h).
#include "gpu_vulkan/vulkan_rt_scene.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "engine/gfx/light_store.h"
#include "engine/gfx/rt_daylight.h"
#include "engine/gfx/rt_material_table.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/rt_volume_light.h"
#include "gpu_vulkan/glsl_compiler.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_rt_glsl.h"
#include "gpu_vulkan/vulkan_rt_materials.h"
#include "gpu_vulkan/vulkan_rt_effects.h"
#include "gpu_vulkan/vulkan_rt_fog.h"
#include "gpu_vulkan/vulkan_rt_shadows.h"
#include "gpu_vulkan/vulkan_surface_light.h"
#include "gpu_vulkan/vulkan_path_reference.h"
#include "gpu_vulkan/vulkan_cel_draw.h"
#include "gpu_vulkan/vulkan_rt_sky.h"
#include "gpu_vulkan/vulkan_rt_sky_pass.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_frames.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_objects.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

namespace rt = render::rt;

constexpr uint32_t kRefitsBeforeRebuild = 32;                      // a dynamic mesh rebuilt after this many refits
constexpr VkDeviceSize kScratchBatchBytes = 64ULL * 1024 * 1024;  // the scratch a batch of builds shares, at most
constexpr uint32_t kCompactionQueries = 1024;                      // compacted-size queries in flight, at most
constexpr uint32_t kLeastInstances = 64;                           // a frame's TLAS made for at least this many
constexpr uint32_t kCustomIndexMask = 0xFFFFFFU;                   // (24 bits)
constexpr VkDeviceSize kInstanceAlignment = 16;                    // instance data's address
constexpr uint32_t kPositionBytes = 12;                            // three floats
constexpr uint32_t kFloatBytes = 4;
constexpr uint32_t kStripCorners = 2;                              // a strip's triangles: its indices less two
constexpr uint32_t kListCorners = 3;
constexpr double kLogSeconds = 10.0;                               // the scene's line, at most this often
constexpr double kMegabyte = 1024.0 * 1024.0;
constexpr uint64_t kBudgetBytes = 1024ULL * 1024 * 1024;         // logged once when the structures pass it
constexpr uint32_t kProbeGroup = 64;                               // the probe shader's local size
constexpr uint32_t kIndex32Bytes = 4;
constexpr uint32_t kIndex16Bytes = 2;
constexpr uint64_t kNoQuery = ~0ULL;
constexpr float kLeastAlphaRef = (1.0F / 255.0F) + 1.0e-6F; // an alpha test against at most this rejects nothing drawn

// (R5's reflections) The geometry table's GLSL (vulkan_rt_scene.h, scene_geometry_glsl). Every read is a word at a
// 4-byte boundary (a 16-bit index: its word's half); the buffers' starts are aligned at least that much.
constexpr std::string_view kGeometryGlsl = R"(#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require
#extension GL_EXT_scalar_block_layout : require
#ifndef THUG2_GEOMETRY_SET
#define THUG2_GEOMETRY_SET 0
#endif
#ifndef THUG2_GEOMETRY_BINDING
#define THUG2_GEOMETRY_BINDING 7
#endif
struct GeometryRecord {
    uvec2 vertex_address;
    uvec2 index_address;
    uint vertex_offset;
    uint stride;
    uint position_offset;
    uint uv_offset;
    uint colour_offset;
    uint index_offset;
    uint flags;
    uint instance_flags;
    uint normal_offset;
    uvec2 light_address;
    uvec2 object_light_address;
    float light_exposure;
};
layout(set = THUG2_GEOMETRY_SET, binding = THUG2_GEOMETRY_BINDING, scalar) readonly buffer GeometryRecords {
    GeometryRecord geometry_records[];
};
layout(buffer_reference, buffer_reference_align = 4, scalar) readonly buffer GeometryWords { uint words[]; };
const uint GEOMETRY_LIVE = 1u;
const uint GEOMETRY_INDEX32 = 2u;
const uint GEOMETRY_UV = 4u;
const uint GEOMETRY_COLOUR = 8u;
const uint GEOMETRY_NORMAL = 16u;
const uint GEOMETRY_LIGHT = 32u;
const uint GEOMETRY_OBJECT_LIGHT = 64u;
GeometryRecord geometry_record(uint instance)
{
    if (instance >= uint(geometry_records.length())) {
        return GeometryRecord(uvec2(0u), uvec2(0u), 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, uvec2(0u), uvec2(0u), 0.0);
    }
    return geometry_records[instance];
}
uint geometry_word(uvec2 address, uint byte_offset)
{
    return GeometryWords(address).words[byte_offset >> 2];
}
uint geometry_corner(GeometryRecord g, uint primitive, uint k)
{
    uint at = primitive * 3u + k;
    if (g.index_address == uvec2(0u)) {
        return at;
    }
    if ((g.flags & GEOMETRY_INDEX32) != 0u) {
        return geometry_word(g.index_address, g.index_offset + at * 4u);
    }
    uint byte_offset = g.index_offset + at * 2u;
    uint word = geometry_word(g.index_address, byte_offset & ~3u);
    return (byte_offset & 2u) != 0u ? (word >> 16) : (word & 0xFFFFu);
}
float geometry_float(GeometryRecord g, uint vertex, uint offset)
{
    return uintBitsToFloat(geometry_word(g.vertex_address, g.vertex_offset + vertex * g.stride + offset));
}
struct HitVertices {
    vec3 position[3];
    vec2 uv[3];
    vec4 colour[3];
    uint flags;
};
bool hit_vertices(uint instance, uint primitive, out HitVertices v)
{
    GeometryRecord g = geometry_record(instance);
    v.flags = g.flags;
    for (uint k = 0u; k < 3u; ++k) {
        v.position[k] = vec3(0.0);
        v.uv[k] = vec2(0.0);
        v.colour[k] = vec4(1.0);
    }
    if ((g.flags & GEOMETRY_LIVE) == 0u) {
        return false;
    }
    for (uint k = 0u; k < 3u; ++k) {
        uint corner = geometry_corner(g, primitive, k);
        v.position[k] = vec3(geometry_float(g, corner, g.position_offset), geometry_float(g, corner, g.position_offset + 4u),
                             geometry_float(g, corner, g.position_offset + 8u));
        if ((g.flags & GEOMETRY_UV) != 0u) {
            v.uv[k] = vec2(geometry_float(g, corner, g.uv_offset), geometry_float(g, corner, g.uv_offset + 4u));
        }
        if ((g.flags & GEOMETRY_COLOUR) != 0u) {
            uint c = geometry_word(g.vertex_address, g.vertex_offset + corner * g.stride + g.colour_offset);
            v.colour[k] = vec4(float((c >> 16) & 255u), float((c >> 8) & 255u), float(c & 255u), float(c >> 24)) / 255.0;
        }
    }
    return true;
}
vec2 hit_uv(HitVertices v, vec2 barycentrics)
{
    return v.uv[0] * (1.0 - barycentrics.x - barycentrics.y) + v.uv[1] * barycentrics.x + v.uv[2] * barycentrics.y;
}
vec2 hit_uv_set(uint instance, uint primitive, vec2 barycentrics, uint set)
{
    GeometryRecord g = geometry_record(instance);
    if ((g.flags & GEOMETRY_LIVE) == 0u || (g.flags & GEOMETRY_UV) == 0u) {
        return vec2(0.0);
    }
    uint offset = g.uv_offset + (set < ((g.flags >> 8) & 15u) ? set * 8u : 0u);
    float weights[3] = float[3](1.0 - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);
    vec2 uv = vec2(0.0);
    for (uint k = 0u; k < 3u; ++k) {
        uint corner = geometry_corner(g, primitive, k);
        uv += vec2(geometry_float(g, corner, offset), geometry_float(g, corner, offset + 4u)) * weights[k];
    }
    return uv;
}
bool hit_normal(uint instance, uint primitive, vec2 barycentrics, out vec3 normal)
{
    GeometryRecord g = geometry_record(instance);
    normal = vec3(0.0);
    if ((g.flags & GEOMETRY_LIVE) == 0u || (g.flags & GEOMETRY_NORMAL) == 0u) {
        return false;
    }
    float weights[3] = float[3](1.0 - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);
    for (uint k = 0u; k < 3u; ++k) {
        uint corner = geometry_corner(g, primitive, k);
        normal += vec3(geometry_float(g, corner, g.normal_offset), geometry_float(g, corner, g.normal_offset + 4u),
                       geometry_float(g, corner, g.normal_offset + 8u)) * weights[k];
    }
    return true;
}
vec4 hit_colour(HitVertices v, vec2 barycentrics)
{
    return v.colour[0] * (1.0 - barycentrics.x - barycentrics.y) + v.colour[1] * barycentrics.x +
           v.colour[2] * barycentrics.y;
}
// (added by the light energy audit) The relit world's light store (engine/gfx/rt_cel.h: eight words a vertex - round L:
// its light words RGB9E5, physical light in the global unit, x the frame's pre-exposure here - the record's
// light_exposure, one value a frame) blended at a hit - what a hit's surface is (its tint: the albedo, x the texture) and
// how it is lit, in the frame's unit.
struct WorldLight {
    vec3 ambient;   // the sky's and the bounce's light
    float n_dot_l;
    vec3 tint;      // the surface's colour (none of the bake's light in it)
    float seen;     // the sun's visibility the colour was written with
    vec3 lamps;     // the lamps' light on it (tint x L x the lamps on - relit's per-vertex share)
    vec3 sun;       // the direct light's colour and strength
    vec3 sky;       // the sky's part of `ambient`
    float sky_share; // the vertex's K
    vec3 emit;      // (round L) its own emission (a lamp's lit surface: radiance, not x the tint)
};
const WorldLight NO_WORLD_LIGHT = WorldLight(vec3(0.0), 0.0, vec3(0.0), 0.0, vec3(0.0), vec3(0.0), vec3(0.0), 0.0, vec3(0.0));
const float LIGHT_PER_UNORM = 255.0 / 128.0;
bool hit_world_light(uint instance, uint primitive, vec2 barycentrics, out WorldLight w)
{
    GeometryRecord g = geometry_record(instance);
    w = NO_WORLD_LIGHT;
    if ((g.flags & GEOMETRY_LIVE) == 0u || (g.flags & GEOMETRY_LIGHT) == 0u) {
        return false;
    }
    float weights[3] = float[3](1.0 - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);
    for (uint k = 0u; k < 3u; ++k) {
        uint at = geometry_corner(g, primitive, k) * 32u; // (rt_cel.h kCelStreamBytes)
        vec4 flags = unpackUnorm4x8(geometry_word(g.light_address, at + 24u)).zyxw; // (B, G, R, A in memory)
        if (flags.a < 0.75) {
            return false; // (a vertex the game animates: its colour isn't relit's - or a hidden mesh)
        }
        vec4 w1 = unpackUnorm4x8(geometry_word(g.light_address, at + 4u)).zyxw;
        float s = weights[k];
        float e = s * g.light_exposure;
        w.ambient += stored_light(geometry_word(g.light_address, at)) * e;
        w.n_dot_l += flags.r * s;
        w.tint += w1.rgb * (s * LIGHT_PER_UNORM);
        w.seen += w1.a * s;
        w.lamps += stored_light(geometry_word(g.light_address, at + 8u)) * e;
        w.sun += stored_light(geometry_word(g.light_address, at + 12u)) * e;
        w.sky += stored_light(geometry_word(g.light_address, at + 16u)) * e;
        w.sky_share += flags.g * s;
        w.emit += stored_light(geometry_word(g.light_address, at + 28u)) * e;
    }
    return true;
}
vec3 world_light_with(WorldLight w, float seen, vec3 sky)
{
    return w.tint * (max(w.ambient - w.sky, vec3(0.0)) + sky + w.sun * (w.n_dot_l * seen)) + w.lamps + w.emit;
}
// (the objects' shadows) The lights an object is lit by (rt::ObjectLight, three floats a part in its order).
struct ObjectLight {
    vec3 ambient;
    vec3 key;
    vec3 key_direction;
    vec3 fill;
    vec3 fill_direction;
    vec3 local;
    vec3 local_direction;
    bool linear;
};
vec3 object_light_part(uvec2 address, uint part)
{
    uint at = part * 12u;
    return vec3(uintBitsToFloat(geometry_word(address, at)), uintBitsToFloat(geometry_word(address, at + 4u)),
                uintBitsToFloat(geometry_word(address, at + 8u)));
}
bool hit_object_light(uint instance, out ObjectLight l)
{
    GeometryRecord g = geometry_record(instance);
    l = ObjectLight(vec3(0.0), vec3(0.0), vec3(0.0, -1.0, 0.0), vec3(0.0), vec3(0.0, -1.0, 0.0), vec3(0.0),
                    vec3(0.0, -1.0, 0.0), false);
    if ((g.flags & GEOMETRY_LIVE) == 0u || (g.flags & GEOMETRY_OBJECT_LIGHT) == 0u) {
        return false;
    }
    l.ambient = object_light_part(g.object_light_address, 0u);
    l.key = object_light_part(g.object_light_address, 1u);
    l.key_direction = object_light_part(g.object_light_address, 2u);
    l.fill = object_light_part(g.object_light_address, 3u);
    l.fill_direction = object_light_part(g.object_light_address, 4u);
    l.local = object_light_part(g.object_light_address, 5u);
    l.local_direction = object_light_part(g.object_light_address, 6u);
    l.linear = object_light_part(g.object_light_address, 7u).x > 0.5;
    return true;
}
float object_n_dot_l(vec3 normal, vec3 travel)
{
    float length_of = length(travel);
    return length_of > 1e-6 ? max(dot(normal, -travel / length_of), 0.0) : 0.0;
}
vec3 object_light_with(ObjectLight l, vec3 normal, float seen)
{
    return max(l.ambient + l.key * (object_n_dot_l(normal, l.key_direction) * seen) +
               l.fill * object_n_dot_l(normal, l.fill_direction) + l.local * object_n_dot_l(normal, l.local_direction),
               vec3(0.0));
}
)";

// The device's address of a buffer.
VkDeviceAddress address_of(VkDevice device, VkBuffer buffer)
{
    const VkBufferDeviceAddressInfo info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .pNext = nullptr, .buffer = buffer};
    return vkGetBufferDeviceAddress(device, &info);
}

VkDeviceAddress align_up(VkDeviceAddress value, VkDeviceSize alignment)
{
    return alignment <= 1 ? value : (value + alignment - 1) / alignment * alignment;
}

// An acceleration structure and the buffer it is in.
struct Structure {
    VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
    DeviceBuffer memory;
    VkDeviceAddress address = 0; // the structure's (vkGetAccelerationStructureDeviceAddressKHR)
};

// A mesh registered (add_mesh): what it is built from and its BLAS.
struct Mesh {
    rt::Geometry geometry;
    Structure blas;
    DeviceBuffer list_indices; // a strip's (or an unindexed strip's) triangles as a list, 32-bit
    uint32_t triangles = 0;
    uint32_t max_vertex = 0;
    bool refused = false; // not buildable (logged)
    bool built = false;
    bool needs_build = true;
    bool needs_refit = false;
    uint32_t refits = 0; // since the last build
    uint32_t query = 0;  // its compacted-size query (when query_serial != kNoQuery)
    uint64_t query_serial = kNoQuery; // the submission that wrote it
    bool compacted = false;
    bool compaction_wanted = false; // built to be compacted, its size not asked for yet (every query was in use)
    VkDeviceSize built_bytes = 0; // the BLAS's size as built (before compaction)
};

// Something released while the device may still use it: destroyed once `serial` has finished.
struct Retiring {
    VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
    DeviceBuffer memory;
    uint64_t serial = 0;
};

// A frame in flight's TLAS and the instances it is built from.
struct FrameScene {
    Structure tlas;
    uint32_t capacity = 0; // instances it was made for
    DeviceBuffer instances; // processor-written, kInstanceAlignment-aligned at instances_address
    VkDeviceAddress instances_address = 0;
    uint64_t frame = 0;
    uint32_t count = 0;
    std::vector<rt::MeshId> meshes; // per instance (the probe's)
    bool built = false;
    // (R5's reflections) a GeometryRecord per instance (processor-written, made with the instances' buffer) and how
    // many instances have a reflective material
    DeviceBuffer geometry;
    uint32_t reflective = 0;
    // (the objects' shadows) the record's objects' lights (rt::ObjectLight, kObjectLightFloats floats each - at most an
    // instance's worth), read by address from their instances' rows
    DeviceBuffer object_lights;
};

// The probe's compute pass (made on first use).
struct ProbePass {
    bool tried = false;
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
};

// What the probe shader reads and writes (std430).
struct PackedRay {
    std::array<float, 4> origin_t_min{};
    std::array<float, 4> direction_t_max{};
    std::array<uint32_t, 4> cull_mask{}; // (x)
};
struct PackedHit {
    float t = 0.0F;
    uint32_t instance = 0;
    uint32_t custom_index = 0;
    uint32_t primitive = 0;
    std::array<float, 2> barycentrics{};
    uint32_t flags = 0; // 1: hit, 2: front face
    uint32_t pad = 0;
};
constexpr uint32_t kProbeHitFlag = 1;
constexpr uint32_t kProbeFrontFlag = 2;

// (the shader's head: the TLAS, the material table at set 1 and the geometry table at binding 3 - for the cut-outs - go
// before it: probe_source)
constexpr const char* kProbeShader = R"(
layout(local_size_x = 64) in;
struct Ray { vec4 origin_t_min; vec4 direction_t_max; uvec4 cull_mask; }; // (cull_mask.y: 1 - the cut-outs by alpha)
struct Hit { float t; uint instance; uint custom_index; uint primitive; vec2 barycentrics; uint flags; uint pad; };
const uint PROBE_OBJECT_FLAG = 32u; // (rt::kObject)
layout(std430, set = 0, binding = 1) readonly buffer Rays { Ray rays[]; };
layout(std430, set = 0, binding = 2) writeonly buffer Hits { Hit hits[]; };
layout(push_constant) uniform Count { uint count; } given;
void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= given.count) {
        return;
    }
    Ray ray = rays[i];
    bool cutouts = (ray.cull_mask.y & 1u) != 0u;
    rayQueryEXT query;
    rayQueryInitializeEXT(query, u_scene, cutouts ? gl_RayFlagsNoneEXT : gl_RayFlagsOpaqueEXT, ray.cull_mask.x & 0xFFu,
                          ray.origin_t_min.xyz, ray.origin_t_min.w, ray.direction_t_max.xyz, ray.direction_t_max.w);
    while (rayQueryProceedEXT(query)) {
        // (the cut-outs: the one lighting path's test - vulkan_rt_scene.h cutout_rays_glsl -; an object's never)
        if (rayQueryGetIntersectionTypeEXT(query, false) == gl_RayQueryCandidateIntersectionTriangleEXT) {
            uint instance = rayQueryGetIntersectionInstanceIdEXT(query, false);
            if ((geometry_record(instance).instance_flags & PROBE_OBJECT_FLAG) == 0u &&
                cutout_kept(instance, rayQueryGetIntersectionPrimitiveIndexEXT(query, false),
                            rayQueryGetIntersectionInstanceCustomIndexEXT(query, false),
                            rayQueryGetIntersectionBarycentricsEXT(query, false))) {
                rayQueryConfirmIntersectionEXT(query);
            }
        }
    }
    Hit hit;
    hit.t = 0.0;
    hit.instance = 0;
    hit.custom_index = 0;
    hit.primitive = 0;
    hit.barycentrics = vec2(0.0);
    hit.flags = 0;
    hit.pad = 0;
    if (rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionTriangleEXT) {
        hit.t = rayQueryGetIntersectionTEXT(query, true);
        hit.instance = rayQueryGetIntersectionInstanceIdEXT(query, true);
        hit.custom_index = rayQueryGetIntersectionInstanceCustomIndexEXT(query, true);
        hit.primitive = rayQueryGetIntersectionPrimitiveIndexEXT(query, true);
        hit.barycentrics = rayQueryGetIntersectionBarycentricsEXT(query, true);
        hit.flags = 1u | (rayQueryGetIntersectionFrontFaceEXT(query, true) ? 2u : 0u);
    }
    hits[i] = hit;
}
)";

// The scene: one, while the device has ray tracing.
class VulkanRayTracing final : public rt::RayTracing {
public:
    [[nodiscard]] bool available() const override { return true; }
    void add_mesh(rt::MeshId mesh, const rt::Geometry& geometry) override;
    void remove_mesh(rt::MeshId mesh) override;
    void mesh_changed(rt::MeshId mesh) override;
    // (the material table: vulkan_rt_materials.h - any thread)
    void set_material(rt::MaterialId material, const rt::Material& surface) override;
    void remove_material(rt::MaterialId material) override;
    void update_mesh(rt::MeshId mesh, const rt::Geometry& geometry) override;
    void begin_frame(const rt::FrameRecord& record) override;
    void settle(const rt::FrameRecord& record) override;
    void prepare_frame() override;
    rt::ProbeResult probe(const rt::ProbeRequest& ray) override;
    std::string describe_light(const std::array<float, 3>& point) override;
    void probe_many(std::span<const rt::ProbeRequest> rays, std::vector<rt::ProbeResult>* results) override;
    uint64_t probe_later(std::span<const rt::ProbeRequest> rays) override;
    bool probe_ready(uint64_t ticket, std::vector<rt::ProbeResult>* results) override;
    void probe_forget(uint64_t ticket) override;
    void set_light_stream(rt::MeshId mesh, GpuVertexBuffer* stream) override;
};

// (added for the relit mode's moving light) Rays recorded into a frame's work (probe_later), answered once the device
// has done it: their hits' read-back buffer, the submission they went with and the instances' meshes then.
struct LaterProbe {
    uint64_t ticket = 0;
    uint64_t serial = 0;
    DeviceBuffer hits;
    std::size_t count = 0;
    std::vector<rt::MeshId> meshes;
};

struct Scene {
    bool installed = false;
    uint64_t history_cut = 0; // (round S2) the lighting's cut the temporal histories were last started again for
    VkDevice device = nullptr;
    VkDeviceSize scratch_alignment = 1;
    std::string device_name;
    std::unordered_map<rt::MeshId, Mesh> meshes;
    std::unordered_map<rt::MaterialId, float> alpha_refs; // each material's alpha reference (0..1), from set_material
    std::unordered_set<rt::MaterialId> reflective;        // (R5's reflections) the materials with a reflectivity
    std::unordered_map<rt::MeshId, GpuVertexBuffer*> light_streams; // (the light energy audit) set_light_stream's
    std::array<FrameScene, Frames::kSlots> frames{};
    uint32_t current = 0; // the slot of the last TLAS built
    bool any_built = false;
    DeviceBuffer scratch;
    VkDeviceAddress scratch_address = 0; // aligned
    VkDeviceSize scratch_bytes = 0;      // usable from scratch_address
    std::vector<Retiring> retiring;
    VkQueryPool queries = VK_NULL_HANDLE;
    std::vector<uint32_t> free_queries;
    ProbePass probe;
    SceneStats stats;
    double last_log = 0.0;
    bool budget_logged = false;
    bool changed = false; // built or compacted since the scene's last line
    uint32_t compactions_wanted = 0; // meshes with compaction_wanted
    std::vector<LaterProbe> later; // (probe_later's, not answered yet - one at a time: the probe's descriptor set)
    uint64_t next_ticket = 1;
    VulkanRayTracing backend;
};

Scene& scene()
{
    static core::NoDestroy<Scene> made;
    return made.get();
}

double seconds_now()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void barrier(VkCommandBuffer commands, VkPipelineStageFlags from, VkAccessFlags writes, VkPipelineStageFlags to,
             VkAccessFlags reads)
{
    const VkMemoryBarrier memory{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .pNext = nullptr, .srcAccessMask = writes, .dstAccessMask = reads};
    vkCmdPipelineBarrier(commands, from, to, 0, 1, &memory, 0, nullptr, 0, nullptr);
}

constexpr VkPipelineStageFlags kBuildStage = VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
constexpr VkAccessFlags kStructureAccess =
    VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
// What reads a TLAS after it is built (ray queries), and what may write the build's inputs after (the next uploads).
constexpr VkPipelineStageFlags kAfterBuild = kBuildStage | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;

// Something destroyed once the submission being recorded has finished.
void retire(Scene& made, VkAccelerationStructureKHR handle, DeviceBuffer* memory, uint64_t serial)
{
    if (handle == VK_NULL_HANDLE && memory->buffer == VK_NULL_HANDLE) {
        return;
    }
    made.retiring.push_back({.handle = handle, .memory = *memory, .serial = serial});
    *memory = DeviceBuffer{};
}

void retire_structure(Backend& state, Scene& made, Structure* structure)
{
    retire(made, structure->handle, &structure->memory, state.frames.serial());
    *structure = Structure{};
}

void destroy_now(Backend& state, Scene& made, Retiring* gone)
{
    if (gone->handle != VK_NULL_HANDLE) {
        vkDestroyAccelerationStructureKHR(made.device, gone->handle, nullptr);
    }
    state.memory.destroy_buffer(&gone->memory);
}

// The retired things whose submissions have finished, destroyed.
void destroy_finished(Backend& state, Scene& made)
{
    const uint64_t completed = state.frames.completed();
    auto kept = made.retiring.begin();
    for (Retiring& each : made.retiring) {
        if (each.serial <= completed) {
            destroy_now(state, made, &each);
        } else {
            *kept++ = each;
        }
    }
    made.retiring.erase(kept, made.retiring.end());
}

// A structure of `bytes` in a buffer of its own.
bool make_structure(Backend& state, VkAccelerationStructureTypeKHR type, VkDeviceSize bytes, Structure* made)
{
    *made = Structure{};
    if (!state.memory.create_buffer(bytes,
                                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                                        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                    BufferPlace::kDevice, &made->memory)) {
        return false;
    }
    const VkAccelerationStructureCreateInfoKHR info{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
                                                    .pNext = nullptr,
                                                    .createFlags = 0,
                                                    .buffer = made->memory.buffer,
                                                    .offset = 0,
                                                    .size = bytes,
                                                    .type = type,
                                                    .deviceAddress = 0};
    if (vkCreateAccelerationStructureKHR(state.context.device(), &info, nullptr, &made->handle) != VK_SUCCESS) {
        state.memory.destroy_buffer(&made->memory);
        *made = Structure{};
        return false;
    }
    const VkAccelerationStructureDeviceAddressInfoKHR address{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
        .pNext = nullptr,
        .accelerationStructure = made->handle};
    made->address = vkGetAccelerationStructureDeviceAddressKHR(state.context.device(), &address);
    return true;
}

// The scratch buffer at least `bytes` (from its aligned address), made again larger when it isn't.
bool ensure_scratch(Backend& state, Scene& made, VkDeviceSize bytes)
{
    if (made.scratch_bytes >= bytes && made.scratch.buffer != VK_NULL_HANDLE) {
        return true;
    }
    retire(made, VK_NULL_HANDLE, &made.scratch, state.frames.serial());
    const VkDeviceSize wanted = std::max(bytes, made.scratch_bytes * 2);
    if (!state.memory.create_buffer(wanted + made.scratch_alignment,
                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                    BufferPlace::kDevice, &made.scratch)) {
        made.scratch_bytes = 0;
        made.scratch_address = 0;
        return false;
    }
    const VkDeviceAddress start = address_of(made.device, made.scratch.buffer);
    made.scratch_address = align_up(start, made.scratch_alignment);
    made.scratch_bytes = wanted + made.scratch_alignment - (made.scratch_address - start);
    return true;
}

// --- meshes ------------------------------------------------------------------------------------------------------

// The buffer behind a handle, if the Gpu still has it.
Buffer* live_buffer(Backend& state, Buffer* buffer)
{
    return buffer != nullptr && state.buffers.contains(buffer) && !buffer->versions.empty() ? buffer : nullptr;
}

BufferVersion& current_version(Buffer* buffer) { return core::at(buffer->versions, buffer->current); }

// A buffer's bytes as the processor last wrote them: a static one's copy, a dynamic one's current memory.
std::span<const uint8_t> written_bytes(Buffer* buffer)
{
    if (!buffer->dynamic) {
        return buffer->shadow;
    }
    const DeviceBuffer& memory = current_version(buffer).memory;
    if (memory.mapped == nullptr) {
        return {};
    }
    return {static_cast<const uint8_t*>(memory.mapped), static_cast<std::size_t>(memory.bytes)};
}

// `count` indices from index `first` of an index buffer, as 32-bit values; false when they aren't all there.
bool read_indices(Buffer* buffer, uint32_t first, uint32_t count, bool wide, std::vector<uint32_t>* out)
{
    const std::span<const uint8_t> bytes = written_bytes(buffer);
    const std::size_t size = wide ? kIndex32Bytes : kIndex16Bytes;
    const std::size_t from = static_cast<std::size_t>(first) * size;
    if (from + (static_cast<std::size_t>(count) * size) > bytes.size()) {
        return false;
    }
    out->resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        const std::span<const uint8_t> one = bytes.subspan(from + (i * size), size);
        if (wide) {
            uint32_t value = 0;
            std::memcpy(&value, one.data(), sizeof(value));
            core::at(*out, i) = value;
        } else {
            uint16_t value = 0;
            std::memcpy(&value, one.data(), sizeof(value));
            core::at(*out, i) = value;
        }
    }
    return true;
}

// Index `at` of the renderer's copy of a mesh's indices (Geometry::cpu_indices: from index_offset).
uint32_t index_at(const rt::Geometry& geometry, uint32_t at)
{
    const std::size_t size = geometry.indices_32bit ? kIndex32Bytes : kIndex16Bytes;
    const std::span<const uint8_t> bytes(static_cast<const uint8_t*>(geometry.cpu_indices),
                                         static_cast<std::size_t>(geometry.index_count) * size);
    if (geometry.indices_32bit) {
        uint32_t value = 0;
        std::memcpy(&value, bytes.subspan(at * size, size).data(), sizeof(value));
        return value;
    }
    uint16_t value = 0;
    std::memcpy(&value, bytes.subspan(at * size, size).data(), sizeof(value));
    return value;
}

// Why a mesh can't be built ("" when it can), its triangles counted and its largest vertex index found, a strip's
// triangles listed (32-bit, into `list`: strip triangle i is list triangle i, its odd ones turned back round).
std::string plan_mesh(Backend& state, Mesh* mesh, std::vector<uint32_t>* list)
{
    const rt::Geometry& geometry = mesh->geometry;
    const Buffer* const vertices = live_buffer(state, object(geometry.vertices));
    if (vertices == nullptr || vertices->index) {
        return "no vertex buffer";
    }
    if (geometry.vertex_stride % kFloatBytes != 0 || geometry.position_offset % kFloatBytes != 0 ||
        geometry.vertex_offset % kFloatBytes != 0 || geometry.vertex_stride < kPositionBytes) {
        return "positions not 4-byte aligned";
    }
    std::vector<uint32_t> source;
    const bool indexed = geometry.indices != nullptr;
    if (indexed) {
        Buffer* const indices = live_buffer(state, object(geometry.indices));
        if (indices == nullptr || !indices->index) {
            return "no index buffer";
        }
        if (geometry.cpu_indices != nullptr) { // (the renderer's own copy, from index_offset)
            source.resize(geometry.index_count);
            for (uint32_t i = 0; i < geometry.index_count; ++i) {
                core::at(source, i) = index_at(geometry, i);
            }
        } else if (!read_indices(indices, geometry.index_offset, geometry.index_count, geometry.indices_32bit,
                                 &source)) {
            return "indices past the buffer's end";
        }
    }
    const uint32_t corners = indexed ? geometry.index_count : geometry.vertex_count;
    if (geometry.strip) {
        mesh->triangles = corners > kStripCorners ? corners - kStripCorners : 0;
    } else {
        mesh->triangles = corners / kListCorners;
    }
    if (mesh->triangles == 0) {
        return "no triangles";
    }
    uint32_t largest = 0;
    if (indexed) {
        largest = *std::ranges::max_element(source);
    } else {
        largest = geometry.vertex_count - 1;
    }
    mesh->max_vertex = largest;
    const uint64_t end = geometry.vertex_offset + (static_cast<uint64_t>(largest) * geometry.vertex_stride) +
                         geometry.position_offset + kPositionBytes;
    if (end > vertices->bytes) {
        return "vertices past the buffer's end";
    }
    list->clear();
    if (geometry.strip) {
        list->reserve(static_cast<std::size_t>(mesh->triangles) * kListCorners);
        const auto corner = [&](uint32_t at) { return indexed ? core::at(source, at) : at; };
        for (uint32_t i = 0; i < mesh->triangles; ++i) {
            const bool odd = (i & 1U) != 0;
            list->push_back(corner(odd ? i + 1 : i));
            list->push_back(corner(odd ? i : i + 1));
            list->push_back(corner(i + 2));
        }
    }
    return {};
}

// The triangles' description for a build (the buffers' addresses now: a dynamic buffer's current memory).
VkAccelerationStructureGeometryKHR triangles_of(Backend& state, const Mesh& mesh)
{
    const rt::Geometry& geometry = mesh.geometry;
    Buffer* const vertices = object(geometry.vertices);
    BufferVersion& vertex_version = current_version(vertices);
    vertex_version.last_use = state.frames.serial(); // (a later copy into it waits for the build: vulkan_gpu.cpp)
    const VkDevice device = state.context.device();
    const VkDeviceAddress vertex_address =
        address_of(device, vertex_version.memory.buffer) + geometry.vertex_offset + geometry.position_offset;
    VkIndexType index_type = VK_INDEX_TYPE_NONE_KHR;
    VkDeviceAddress index_address = 0;
    if (mesh.list_indices.buffer != VK_NULL_HANDLE) {
        index_type = VK_INDEX_TYPE_UINT32;
        index_address = address_of(device, mesh.list_indices.buffer);
    } else if (geometry.indices != nullptr) {
        Buffer* const indices = object(geometry.indices);
        BufferVersion& index_version = current_version(indices);
        index_version.last_use = state.frames.serial();
        index_type = geometry.indices_32bit ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
        index_address = address_of(device, index_version.memory.buffer) +
                        (static_cast<VkDeviceAddress>(geometry.index_offset) *
                         (geometry.indices_32bit ? kIndex32Bytes : kIndex16Bytes));
    }
    VkAccelerationStructureGeometryKHR made{};
    made.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    made.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    made.flags = 0; // (opaque or not: the instance says - vulkan_rt_scene.h)
    made.geometry.triangles = VkAccelerationStructureGeometryTrianglesDataKHR{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
        .pNext = nullptr,
        .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
        .vertexData = {.deviceAddress = vertex_address},
        .vertexStride = geometry.vertex_stride,
        .maxVertex = mesh.max_vertex,
        .indexType = index_type,
        .indexData = {.deviceAddress = index_address},
        .transformData = {.deviceAddress = 0}};
    return made;
}

VkBuildAccelerationStructureFlagsKHR blas_flags(const Mesh& mesh)
{
    return mesh.geometry.dynamic ? (VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR |
                                    VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR)
                                 : (VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
                                    VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR);
}

void release_query(Scene& made, Mesh* mesh)
{
    if (mesh->query_serial != kNoQuery) {
        made.free_queries.push_back(mesh->query);
        mesh->query_serial = kNoQuery;
    }
}

void forget_mesh(Backend& state, Scene& made, Mesh* mesh)
{
    if (mesh->compaction_wanted) {
        mesh->compaction_wanted = false;
        --made.compactions_wanted;
    }
    release_query(made, mesh);
    retire_structure(state, made, &mesh->blas);
    retire(made, VK_NULL_HANDLE, &mesh->list_indices, state.frames.serial());
}

// One build or update recorded in a batch.
struct BuildJob {
    Mesh* mesh = nullptr;
    VkAccelerationStructureGeometryKHR geometry{};
    VkAccelerationStructureBuildGeometryInfoKHR info{};
    VkAccelerationStructureBuildRangeInfoKHR range{};
    VkDeviceSize scratch = 0;
    bool compact = false;
};

// The jobs recorded in batches whose scratch fits the scratch buffer (each its own aligned part), a barrier between
// batches (the scratch is shared) - after the barrier before the first.
void record_builds(Scene& made, VkCommandBuffer commands, std::vector<BuildJob>& jobs)
{
    std::size_t start = 0;
    while (start < jobs.size()) {
        std::vector<VkAccelerationStructureBuildGeometryInfoKHR> infos;
        std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> ranges;
        VkDeviceSize offset = 0;
        std::size_t end = start;
        while (end < jobs.size()) {
            BuildJob& job = core::at(jobs, end);
            const VkDeviceSize needed = align_up(job.scratch, made.scratch_alignment);
            if (end > start && offset + needed > made.scratch_bytes) {
                break;
            }
            job.info.pGeometries = &job.geometry;
            job.info.scratchData = {.deviceAddress = made.scratch_address + offset};
            infos.push_back(job.info);
            ranges.push_back(&job.range);
            offset += needed;
            ++end;
        }
        if (start > 0) {
            barrier(commands, kBuildStage, VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, kBuildStage,
                    kStructureAccess);
        }
        vkCmdBuildAccelerationStructuresKHR(commands, static_cast<uint32_t>(infos.size()), infos.data(),
                                            ranges.data());
        start = end;
    }
}

// --- the scene's line, the reasons ----------------------------------------------------------------------------------

void log_scene(Scene& made, bool now)
{
    const double time = seconds_now();
    if (!now && time - made.last_log < kLogSeconds) {
        return;
    }
    made.last_log = time;
    const SceneStats& stats = made.stats;
    get_log("vulkan: ray tracing scene: %u meshes (%u built, %u compacted, %u dynamic, %u refused), BLAS %.1f MB "
            "(%.1f MB before compaction), TLAS + instances %.1f MB, scratch %.1f MB; %u instances in the last TLAS; "
            "%llu builds, %llu refits, %llu TLAS builds (asked for: %llu new meshes, %llu new geometry, %llu written; %llu "
            "dynamic ones after their refits)",
            stats.meshes, stats.built, stats.compacted, stats.dynamic, stats.refused,
            static_cast<double>(stats.blas_bytes) / kMegabyte, static_cast<double>(stats.built_bytes) / kMegabyte,
            static_cast<double>(stats.tlas_bytes) / kMegabyte, static_cast<double>(stats.scratch_bytes) / kMegabyte,
            stats.last_instances, static_cast<unsigned long long>(stats.builds),
            static_cast<unsigned long long>(stats.refits), static_cast<unsigned long long>(stats.tlas_builds),
            static_cast<unsigned long long>(stats.asked_new), static_cast<unsigned long long>(stats.asked_geometry),
            static_cast<unsigned long long>(stats.asked_written),
            static_cast<unsigned long long>(stats.rebuilt_after_refits));
}

void count_bytes(Scene& made)
{
    SceneStats& stats = made.stats;
    stats.meshes = static_cast<uint32_t>(made.meshes.size());
    stats.built = 0;
    stats.compacted = 0;
    stats.dynamic = 0;
    stats.refused = 0;
    stats.blas_bytes = 0;
    stats.built_bytes = 0;
    for (const auto& [id, mesh] : made.meshes) {
        stats.built += mesh.built ? 1 : 0;
        stats.compacted += mesh.compacted ? 1 : 0;
        stats.dynamic += mesh.geometry.dynamic ? 1 : 0;
        stats.refused += mesh.refused ? 1 : 0;
        stats.blas_bytes += mesh.blas.memory.bytes;
        stats.built_bytes += mesh.compacted ? mesh.built_bytes : mesh.blas.memory.bytes;
    }
    stats.tlas_bytes = 0;
    for (const FrameScene& frame : made.frames) {
        stats.tlas_bytes += frame.tlas.memory.bytes + frame.instances.bytes;
    }
    stats.scratch_bytes = made.scratch.bytes;
    stats.retired_pending = static_cast<uint32_t>(made.retiring.size());
}

// Why the device has no ray tracing ("" when it has it).
std::string missing_for_ray_tracing(const Backend& state)
{
    if (!state.options.ray_tracing) {
        return "not asked for ([Graphics] ray_tracing=1 turns it on)";
    }
    if (!state.context.draws_supported()) {
        return "the device can't draw (no dynamic rendering or push descriptors)";
    }
    const VkPhysicalDevice physical = state.context.physical_device();
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> offered(count);
    vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, offered.data());
    std::string missing;
    for (const char* name : {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_RAY_QUERY_EXTENSION_NAME,
                             VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME}) {
        const bool has = std::ranges::any_of(offered, [name](const VkExtensionProperties& each) {
            return std::string_view(std::data(each.extensionName)) == name;
        });
        if (!has) {
            missing += missing.empty() ? "" : ", ";
            missing += name;
        }
    }
    if (!missing.empty()) {
        return "the device lacks " + missing;
    }
    return "the device lacks a feature it needs (buffer device addresses, descriptor indexing, scalar layout, "
           "acceleration structures or ray queries)";
}

// --- the probe's pass -----------------------------------------------------------------------------------------------

// The probe's shader: the TLAS (u_scene), the material table (set 1) and the frame's geometry table (binding 3) - the
// one lighting path's cut-out test with them -, then its body.
std::string probe_source()
{
    std::string source = "#version 460\n#extension GL_EXT_ray_query : require\n";
    source += "layout(set = 0, binding = 0) uniform accelerationStructureEXT u_scene;\n";
    source += "#define THUG2_MATERIAL_SET 1\n";
    source += material_glsl();
    source += "#define THUG2_GEOMETRY_SET 0\n#define THUG2_GEOMETRY_BINDING 3\n";
    source += scene_geometry_glsl();
    source += cutout_rays_glsl();
    source += kProbeShader;
    return source;
}

bool make_probe_pass(Scene& made, const Backend& state)
{
    ProbePass& pass = made.probe;
    if (pass.tried) {
        return pass.pipeline != VK_NULL_HANDLE;
    }
    pass.tried = true;
    if (!state.materials.ready()) {
        get_log("vulkan: the ray probe has no material table (its cut-outs' test)");
        return false;
    }
    const CompiledShader compiled = compile_rt_glsl(RtShaderStage::kCompute, probe_source());
    if (!compiled.ok) {
        get_log("vulkan: the ray probe's shader doesn't compile: %s", compiled.log.c_str());
        return false;
    }
    const std::array<VkDescriptorSetLayoutBinding, 4> bindings{{
        {.binding = 0,
         .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
         .pImmutableSamplers = nullptr},
        {.binding = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
         .pImmutableSamplers = nullptr},
        {.binding = 2,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
         .pImmutableSamplers = nullptr},
        {.binding = 3, // (the frame's geometry table: the cut-outs' texture coordinates)
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
         .pImmutableSamplers = nullptr},
    }};
    const VkDescriptorSetLayoutCreateInfo layout{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                 .pNext = nullptr,
                                                 .flags = 0,
                                                 .bindingCount = static_cast<uint32_t>(bindings.size()),
                                                 .pBindings = bindings.data()};
    const VkPushConstantRange push{.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = sizeof(uint32_t)};
    if (vkCreateDescriptorSetLayout(made.device, &layout, nullptr, &pass.layout) != VK_SUCCESS) {
        return false;
    }
    const std::array<VkDescriptorSetLayout, 2> set_layouts{pass.layout, state.materials.layout()};
    const VkPipelineLayoutCreateInfo pipeline_layout{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                     .pNext = nullptr,
                                                     .flags = 0,
                                                     .setLayoutCount = static_cast<uint32_t>(set_layouts.size()),
                                                     .pSetLayouts = set_layouts.data(),
                                                     .pushConstantRangeCount = 1,
                                                     .pPushConstantRanges = &push};
    if (vkCreatePipelineLayout(made.device, &pipeline_layout, nullptr, &pass.pipeline_layout) != VK_SUCCESS) {
        return false;
    }
    const VkShaderModuleCreateInfo module_info{.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                               .pNext = nullptr,
                                               .flags = 0,
                                               .codeSize = compiled.spirv.size() * sizeof(uint32_t),
                                               .pCode = compiled.spirv.data()};
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(made.device, &module_info, nullptr, &module) != VK_SUCCESS) {
        return false;
    }
    const VkComputePipelineCreateInfo info{.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                                           .pNext = nullptr,
                                           .flags = 0,
                                           .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                     .pNext = nullptr,
                                                     .flags = 0,
                                                     .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                                                     .module = module,
                                                     .pName = "main",
                                                     .pSpecializationInfo = nullptr},
                                           .layout = pass.pipeline_layout,
                                           .basePipelineHandle = VK_NULL_HANDLE,
                                           .basePipelineIndex = -1};
    const VkResult result = vkCreateComputePipelines(made.device, VK_NULL_HANDLE, 1, &info, nullptr, &pass.pipeline);
    vkDestroyShaderModule(made.device, module, nullptr);
    if (result != VK_SUCCESS) {
        pass.pipeline = VK_NULL_HANDLE;
        return false;
    }
    const std::array<VkDescriptorPoolSize, 2> sizes{{
        {.type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, .descriptorCount = 1},
        {.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 3},
    }};
    const VkDescriptorPoolCreateInfo pool{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                          .pNext = nullptr,
                                          .flags = 0,
                                          .maxSets = 1,
                                          .poolSizeCount = static_cast<uint32_t>(sizes.size()),
                                          .pPoolSizes = sizes.data()};
    if (vkCreateDescriptorPool(made.device, &pool, nullptr, &pass.pool) != VK_SUCCESS) {
        return false;
    }
    const VkDescriptorSetAllocateInfo allocate{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                               .pNext = nullptr,
                                               .descriptorPool = pass.pool,
                                               .descriptorSetCount = 1,
                                               .pSetLayouts = &pass.layout};
    return vkAllocateDescriptorSets(made.device, &allocate, &pass.set) == VK_SUCCESS;
}

void destroy_probe_pass(Scene& made)
{
    ProbePass& pass = made.probe;
    if (pass.pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(made.device, pass.pool, nullptr);
    }
    if (pass.pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(made.device, pass.pipeline, nullptr);
    }
    if (pass.pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(made.device, pass.pipeline_layout, nullptr);
    }
    if (pass.layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(made.device, pass.layout, nullptr);
    }
    pass = ProbePass{};
}

// --- a frame -------------------------------------------------------------------------------------------------------

// Compactions whose sizes are known: a smaller structure made and put in the mesh's place (its address the one the
// TLAS takes from now), the old one handed back for the copy and then retired.
void collect_compactions(Backend& state, Scene& made, std::vector<std::pair<Mesh*, Structure>>* copies)
{
    const uint64_t completed = state.frames.completed();
    for (auto& [id, mesh] : made.meshes) {
        if (mesh.query_serial == kNoQuery || mesh.query_serial > completed) {
            continue;
        }
        VkDeviceSize compact = 0;
        const VkResult result = vkGetQueryPoolResults(made.device, made.queries, mesh.query, 1, sizeof(compact),
                                                      &compact, sizeof(compact), VK_QUERY_RESULT_64_BIT);
        release_query(made, &mesh);
        if (result != VK_SUCCESS || compact == 0 || compact >= mesh.blas.memory.bytes || !mesh.built) {
            mesh.compacted = result == VK_SUCCESS; // (as small as it gets: left as it is)
            continue;
        }
        Structure smaller;
        if (!make_structure(state, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, compact, &smaller)) {
            continue;
        }
        copies->emplace_back(&mesh, mesh.blas);
        mesh.built_bytes = mesh.blas.memory.bytes;
        mesh.blas = smaller;
        mesh.compacted = true;
    }
}

// The frame's TLAS slot made ready for `count` instances (made again larger when it isn't).
bool ensure_frame_scene(Backend& state, Scene& made, FrameScene* frame, uint32_t count)
{
    if (frame->capacity >= count && frame->tlas.handle != VK_NULL_HANDLE) {
        return true;
    }
    const uint32_t capacity = std::max({count, kLeastInstances, frame->capacity * 2});
    retire_structure(state, made, &frame->tlas);
    retire(made, VK_NULL_HANDLE, &frame->instances, state.frames.serial());
    retire(made, VK_NULL_HANDLE, &frame->geometry, state.frames.serial());
    retire(made, VK_NULL_HANDLE, &frame->object_lights, state.frames.serial());
    frame->capacity = 0;
    const VkDeviceSize instance_bytes = static_cast<VkDeviceSize>(capacity) * sizeof(VkAccelerationStructureInstanceKHR);
    if (!state.memory.create_buffer(instance_bytes + kInstanceAlignment,
                                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                    BufferPlace::kUpload, &frame->instances) ||
        frame->instances.mapped == nullptr) {
        return false;
    }
    // (R5's reflections: the geometry table, a record per instance)
    if (!state.memory.create_buffer(static_cast<VkDeviceSize>(capacity) * sizeof(GeometryRecord),
                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, BufferPlace::kUpload, &frame->geometry) ||
        frame->geometry.mapped == nullptr) {
        return false;
    }
    // (the objects' shadows: the objects' lights, read by address)
    if (!state.memory.create_buffer(static_cast<VkDeviceSize>(capacity) * kObjectLightFloats * sizeof(float),
                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                    BufferPlace::kUpload, &frame->object_lights) ||
        frame->object_lights.mapped == nullptr) {
        return false;
    }
    frame->instances_address = align_up(address_of(made.device, frame->instances.buffer), kInstanceAlignment);
    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances = VkAccelerationStructureGeometryInstancesDataKHR{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
        .pNext = nullptr,
        .arrayOfPointers = VK_FALSE,
        .data = {.deviceAddress = frame->instances_address}};
    VkAccelerationStructureBuildGeometryInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    info.geometryCount = 1;
    info.pGeometries = &geometry;
    VkAccelerationStructureBuildSizesInfoKHR sizes{};
    sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkGetAccelerationStructureBuildSizesKHR(made.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &info,
                                            &capacity, &sizes);
    if (!make_structure(state, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, sizes.accelerationStructureSize,
                        &frame->tlas)) {
        return false;
    }
    frame->capacity = capacity;
    return true;
}

// An instance as the TLAS takes it.
// An instance as the TLAS takes it; `alpha_tested`: its material's alpha test can reject a texel (alpha_tested_for).
VkAccelerationStructureInstanceKHR instance_of(const rt::Instance& given, const Mesh* mesh, bool alpha_tested)
{
    VkAccelerationStructureInstanceKHR made{};
    for (std::size_t row = 0; row < given.object_to_world.size(); ++row) {
        const std::span<float, 4> into(core::at(made.transform.matrix, row));
        std::ranges::copy(core::at(given.object_to_world, row), into.begin());
    }
    // (what R4's shadow rays pick by: vulkan_rt_scene.h)
    uint32_t mask = kMaskWorld;
    // (a transparent instance casts only when it is also alpha tested - render_pc's cut-out blended materials - and its
    // alpha test can reject texels)
    const bool cut_out = (given.flags & rt::kTransparent) != 0 && alpha_tested;
    if (((given.flags & rt::kTransparent) != 0 && !cut_out) || (given.flags & rt::kCastsShadow) == 0) {
        mask = kMaskNoShadow;
    } else if (alpha_tested) {
        mask = kMaskAlphaTested;
    } else if ((given.flags & rt::kObject) != 0) {
        mask = kMaskObjects;
    }
    const bool live = mesh != nullptr && mesh->built;
    made.instanceCustomIndex = given.material & kCustomIndexMask;
    made.mask = live ? mask : 0;
    made.instanceShaderBindingTableRecordOffset = 0;
    VkGeometryInstanceFlagsKHR flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    const bool opaque = (given.flags & rt::kOpaque) != 0 ||
                        ((given.flags & rt::kTransparent) == 0 && (given.flags & rt::kAlphaTested) != 0 && !alpha_tested);
    flags |= opaque ? VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR : VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR;
    made.flags = flags;
    // (0: an inactive instance - a mesh not built, removed or never added - so the instance index stays the record's)
    made.accelerationStructureReference = live ? mesh->blas.address : 0;
    return made;
}

// A strip's triangles as a list in a buffer of the mesh's own (processor-written); false when it can't be made.
bool make_list_indices(Backend& state, Scene& made, Mesh* mesh, const std::vector<uint32_t>& list)
{
    retire(made, VK_NULL_HANDLE, &mesh->list_indices, state.frames.serial());
    const VkDeviceSize bytes = list.size() * sizeof(uint32_t);
    if (!state.memory.create_buffer(bytes,
                                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                    BufferPlace::kUpload, &mesh->list_indices) ||
        mesh->list_indices.mapped == nullptr) {
        return false;
    }
    std::memcpy(mesh->list_indices.mapped, list.data(), bytes);
    state.memory.flush(mesh->list_indices, 0, bytes);
    return true;
}

// What a mesh needs this frame.
enum class MeshWork : uint8_t { kNone, kBuild, kRefit };

MeshWork work_for(const Mesh& mesh, const std::vector<std::pair<Mesh*, Structure>>& copies)
{
    if (mesh.refused || std::ranges::any_of(copies, [&mesh](const auto& copy) { return copy.first == &mesh; })) {
        return MeshWork::kNone; // (a compaction's copy recorded this frame: a rebuild or refit waits for the next)
    }
    if (mesh.needs_build || (mesh.needs_refit && mesh.refits >= kRefitsBeforeRebuild)) {
        return MeshWork::kBuild;
    }
    return mesh.needs_refit ? MeshWork::kRefit : MeshWork::kNone;
}

// Whether a mesh's buffers are still the Gpu's (else it is forgotten and left out, logged).
bool buffers_live(Backend& state, Scene& made, rt::MeshId id, Mesh& mesh)
{
    if (live_buffer(state, object(mesh.geometry.vertices)) != nullptr &&
        (mesh.geometry.indices == nullptr || live_buffer(state, object(mesh.geometry.indices)) != nullptr)) {
        return true;
    }
    get_log("vulkan: ray tracing: mesh %u's buffers were released before it was - left out", id);
    forget_mesh(state, made, &mesh);
    mesh.refused = true;
    mesh.built = false;
    return false;
}

// A mesh's build or refit made ready (its structure made when it needs one); false when there is none to record.
bool plan_job(Backend& state, Scene& made, rt::MeshId id, Mesh& mesh, MeshWork work, BuildJob* job)
{
    if (!buffers_live(state, made, id, mesh)) {
        return false;
    }
    const bool rebuild = work == MeshWork::kBuild;
    if (mesh.needs_build) {
        std::vector<uint32_t> list;
        const std::string problem = plan_mesh(state, &mesh, &list);
        if (!problem.empty()) {
            get_log("vulkan: ray tracing: mesh %u left out: %s", id, problem.c_str());
            mesh.refused = true;
            mesh.needs_build = false;
            return false;
        }
        if (!list.empty() && !make_list_indices(state, made, &mesh, list)) {
            return false;
        }
    }
    job->mesh = &mesh;
    job->geometry = triangles_of(state, mesh);
    job->info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    job->info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    job->info.flags = blas_flags(mesh);
    job->info.mode =
        rebuild ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
    job->info.geometryCount = 1;
    job->info.pGeometries = &job->geometry;
    job->range = {.primitiveCount = mesh.triangles, .primitiveOffset = 0, .firstVertex = 0, .transformOffset = 0};
    VkAccelerationStructureBuildSizesInfoKHR sizes{};
    sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkGetAccelerationStructureBuildSizesKHR(made.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &job->info,
                                            &mesh.triangles, &sizes);
    if (rebuild) {
        release_query(made, &mesh);
        if (mesh.blas.memory.bytes < sizes.accelerationStructureSize || mesh.compacted) {
            retire_structure(state, made, &mesh.blas);
            if (!make_structure(state, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, sizes.accelerationStructureSize,
                                &mesh.blas)) {
                get_log("vulkan: ray tracing: no memory for mesh %u's structure (%llu bytes)", id,
                        static_cast<unsigned long long>(sizes.accelerationStructureSize));
                mesh.built = false;
                return false;
            }
        }
        job->scratch = sizes.buildScratchSize;
        job->compact = !mesh.geometry.dynamic && made.queries != VK_NULL_HANDLE;
        made.stats.rebuilt_after_refits += mesh.needs_build ? 0 : 1; // (a skinned mesh's, its refits used up)
        mesh.refits = 0;
        mesh.compacted = false;
        mesh.built_bytes = mesh.blas.memory.bytes;
        ++made.stats.builds;
    } else {
        job->info.srcAccelerationStructure = mesh.blas.handle;
        job->scratch = sizes.updateScratchSize;
        ++mesh.refits;
        ++made.stats.refits;
    }
    job->info.dstAccelerationStructure = mesh.blas.handle;
    mesh.needs_build = false;
    mesh.needs_refit = false;
    mesh.built = true;
    return true;
}

// Whether an instance's alpha test can reject anything: flagged kAlphaTested, and its material's reference above the
// lowest - the game's materials nearly all test against 1 of 255, which passes every texel drawn, so they are opaque
// casters (a material not given yet: as flagged; material 0, untextured: never).
bool alpha_tested_for(const Scene& made, const rt::Instance& given)
{
    if ((given.flags & rt::kAlphaTested) == 0 || given.material == 0) {
        return false;
    }
    const auto found = made.alpha_refs.find(given.material);
    return found == made.alpha_refs.end() || found->second > kLeastAlphaRef;
}

// (R5's reflections) A device address as the GLSL's uvec2.
std::array<uint32_t, 2> address_words(VkDeviceAddress address)
{
    constexpr uint32_t kWordBits = 32;
    return {static_cast<uint32_t>(address), static_cast<uint32_t>(address >> kWordBits)};
}

// (R5's reflections) An instance's row of the geometry table: its mesh's buffers as they are now (their current
// memory, kept by this frame's submission) - nothing (not live) when the mesh isn't built.
GeometryRecord geometry_record_of(Backend& state, const Scene& made, const rt::Instance& given, const Mesh* mesh)
{
    GeometryRecord row;
    if (mesh == nullptr || !mesh->built) {
        return row;
    }
    const rt::Geometry& geometry = mesh->geometry;
    Buffer* const vertices = live_buffer(state, object(geometry.vertices));
    if (vertices == nullptr) {
        return row;
    }
    const uint64_t serial = state.frames.serial();
    BufferVersion& vertex_version = current_version(vertices);
    vertex_version.last_use = std::max(vertex_version.last_use, serial);
    row.vertex_address = address_words(address_of(made.device, vertex_version.memory.buffer));
    row.vertex_offset = geometry.vertex_offset;
    row.stride = geometry.vertex_stride;
    row.position_offset = geometry.position_offset;
    row.flags = kGeometryLive;
    // (the attributes are read a word at a time: one not on a 4-byte boundary is left out)
    if (geometry.uv_format == rt::kUvFloat2 && geometry.uv_offset % kFloatBytes == 0 &&
        geometry.uv_offset + (2 * kFloatBytes) <= geometry.vertex_stride) {
        row.uv_offset = geometry.uv_offset;
        row.flags |= kGeometryUv;
        uint32_t sets = std::clamp<uint32_t>(geometry.uv_sets, 1, kGeometryUvSetsMask);
        while (sets > 1 && geometry.uv_offset + (sets * 2 * kFloatBytes) > geometry.vertex_stride) {
            --sets; // (only the sets within the vertex)
        }
        row.flags |= sets << kGeometryUvSetsShift;
    }
    if (geometry.colour_format == rt::kColourA8R8G8B8 && geometry.colour_offset % kFloatBytes == 0 &&
        geometry.colour_offset + kFloatBytes <= geometry.vertex_stride) {
        row.colour_offset = geometry.colour_offset;
        row.flags |= kGeometryColour;
    }
    if (geometry.normal_format == rt::kNormalFloat3 && geometry.normal_offset % kFloatBytes == 0 &&
        geometry.normal_offset + (3 * kFloatBytes) <= geometry.vertex_stride) {
        row.normal_offset = geometry.normal_offset;
        row.flags |= kGeometryNormal;
    }
    if (mesh->list_indices.buffer != VK_NULL_HANDLE) {
        row.index_address = address_words(address_of(made.device, mesh->list_indices.buffer));
        row.flags |= kGeometryIndex32;
    } else if (geometry.indices != nullptr) {
        Buffer* const indices = live_buffer(state, object(geometry.indices));
        if (indices == nullptr) {
            return GeometryRecord{};
        }
        BufferVersion& index_version = current_version(indices);
        index_version.last_use = std::max(index_version.last_use, serial);
        row.index_address = address_words(address_of(made.device, index_version.memory.buffer));
        row.index_offset = geometry.index_offset * (geometry.indices_32bit ? kIndex32Bytes : kIndex16Bytes);
        row.flags |= geometry.indices_32bit ? kGeometryIndex32 : 0U;
    }
    row.instance_flags = given.flags;
    // (added by the light energy audit) the relit world's light stream, read by the passes that light a pixel again
    const auto stream = made.light_streams.find(given.mesh);
    if (stream != made.light_streams.end()) {
        Buffer* const light = live_buffer(state, object(stream->second));
        if (light != nullptr) {
            BufferVersion& light_version = current_version(light);
            light_version.last_use = std::max(light_version.last_use, serial);
            row.light_address = address_words(address_of(made.device, light_version.memory.buffer));
            row.flags |= kGeometryLight;
            row.light_exposure = rt::frame_pre_exposure(); // (round L: the store's physical light x it where it is read)
        }
    }
    return row;
}

// The record's instances written for the frame's TLAS (and their meshes kept for the probe), and the geometry table.
void write_instances(Backend& state, Scene& made, FrameScene& frame, const rt::FrameRecord& record)
{
    frame.meshes.clear();
    frame.meshes.reserve(record.instances.size());
    frame.reflective = 0;
    std::vector<VkAccelerationStructureInstanceKHR> instances;
    instances.reserve(record.instances.size());
    std::vector<GeometryRecord> rows;
    rows.reserve(record.instances.size());
    // (the objects' shadows) the objects' lights, each where its instances' rows point
    const std::size_t lights = std::min(record.object_lights.size(), static_cast<std::size_t>(frame.capacity));
    const std::span<float> light_floats(static_cast<float*>(frame.object_lights.mapped), lights * kObjectLightFloats);
    for (std::size_t i = 0; i < lights; ++i) {
        const rt::ObjectLight& light = core::at(record.object_lights, i);
        std::size_t at = i * kObjectLightFloats;
        const std::array<float, 3> flags{light.linear ? 1.0F : 0.0F, 0.0F, 0.0F}; // (round O)
        for (const std::array<float, 3>* part : {&light.ambient, &light.key, &light.key_direction, &light.fill,
                                                &light.fill_direction, &light.local, &light.local_direction, &flags}) {
            for (const float value : *part) {
                core::at(light_floats, at++) = value;
            }
        }
    }
    if (lights > 0) {
        state.memory.flush(frame.object_lights, 0, lights * kObjectLightFloats * sizeof(float));
    }
    const VkDeviceAddress lights_address =
        lights > 0 ? address_of(made.device, frame.object_lights.buffer) : VkDeviceAddress{0};
    for (const rt::Instance& given : record.instances) {
        const auto found = made.meshes.find(given.mesh);
        const Mesh* const mesh = found == made.meshes.end() ? nullptr : &found->second;
        instances.push_back(instance_of(given, mesh, alpha_tested_for(made, given)));
        frame.meshes.push_back(given.mesh);
        rows.push_back(geometry_record_of(state, made, given, mesh));
        if ((rows.back().flags & kGeometryLive) != 0 && given.light < lights) {
            constexpr VkDeviceAddress kLightBytes = kObjectLightFloats * sizeof(float);
            rows.back().object_light_address = address_words(lights_address + (VkDeviceAddress{given.light} * kLightBytes));
            rows.back().flags |= kGeometryObjectLight;
        }
        if ((rows.back().flags & kGeometryLive) != 0 && made.reflective.contains(given.material)) {
            ++frame.reflective;
        }
    }
    if (instances.empty()) {
        return;
    }
    const std::size_t row_bytes = rows.size() * sizeof(GeometryRecord);
    std::memcpy(frame.geometry.mapped, rows.data(), row_bytes);
    state.memory.flush(frame.geometry, 0, row_bytes);
    const std::size_t bytes = instances.size() * sizeof(VkAccelerationStructureInstanceKHR);
    const auto skip = static_cast<std::size_t>(frame.instances_address - address_of(made.device, frame.instances.buffer));
    const std::span<uint8_t> into(static_cast<uint8_t*>(frame.instances.mapped),
                                  static_cast<std::size_t>(frame.instances.bytes));
    std::memcpy(into.subspan(skip, bytes).data(), instances.data(), bytes);
    state.memory.flush(frame.instances, skip, bytes);
}

// The scratch the jobs' batches need: all of theirs up to kScratchBatchBytes, and at least the largest one's.
VkDeviceSize scratch_for(const Scene& made, const std::vector<BuildJob>& jobs)
{
    VkDeviceSize total = 0;
    VkDeviceSize largest = 0;
    for (const BuildJob& job : jobs) {
        const VkDeviceSize needed = align_up(job.scratch, made.scratch_alignment);
        total += needed;
        largest = std::max(largest, needed);
    }
    return std::max(std::min(total, kScratchBatchBytes), largest);
}

// The compactions' copies (old structure into the smaller one in the mesh's place), the old ones retired.
void record_compactions(Backend& state, Scene& made, VkCommandBuffer commands,
                        std::vector<std::pair<Mesh*, Structure>>& copies)
{
    for (auto& [mesh, old] : copies) {
        const VkCopyAccelerationStructureInfoKHR copy{.sType = VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR,
                                                      .pNext = nullptr,
                                                      .src = old.handle,
                                                      .dst = mesh->blas.handle,
                                                      .mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR};
        vkCmdCopyAccelerationStructureKHR(commands, &copy);
        retire_structure(state, made, &old);
    }
}

// The static builds' compacted sizes asked for (after the builds' barrier), each query reset first; read at a later
// frame once this submission has run (collect_compactions).
void ask_compacted_size(Backend& state, Scene& made, VkCommandBuffer commands, Mesh* mesh)
{
    mesh->query = made.free_queries.back();
    made.free_queries.pop_back();
    mesh->query_serial = state.frames.serial();
    vkCmdResetQueryPool(commands, made.queries, mesh->query, 1);
    vkCmdWriteAccelerationStructuresPropertiesKHR(commands, 1, &mesh->blas.handle,
                                                  VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR, made.queries,
                                                  mesh->query);
}

void ask_compacted_sizes(Backend& state, Scene& made, VkCommandBuffer commands, const std::vector<BuildJob>& jobs)
{
    for (const BuildJob& job : jobs) {
        if (!job.compact) {
            continue;
        }
        if (made.free_queries.empty()) {
            if (!job.mesh->compaction_wanted) {
                job.mesh->compaction_wanted = true; // (asked for at a later frame, once queries are free)
                ++made.compactions_wanted;
            }
            continue;
        }
        if (job.mesh->compaction_wanted) {
            job.mesh->compaction_wanted = false;
            --made.compactions_wanted;
        }
        ask_compacted_size(state, made, commands, job.mesh);
    }
    // the ones built at earlier frames while every query was in use (their builds have been waited for: the barrier
    // before the first build of this frame)
    for (auto& [id, mesh] : made.meshes) {
        if (made.compactions_wanted == 0 || made.free_queries.empty()) {
            break;
        }
        if (!mesh.compaction_wanted || mesh.query_serial != kNoQuery) {
            continue;
        }
        mesh.compaction_wanted = false;
        --made.compactions_wanted;
        if (mesh.built && !mesh.compacted && !mesh.geometry.dynamic && !mesh.refused) {
            ask_compacted_size(state, made, commands, &mesh);
        }
    }
}

void begin_scene_frame(Backend& state, Scene& made, const rt::FrameRecord& record)
{
    destroy_finished(state, made);
    std::vector<std::pair<Mesh*, Structure>> copies;
    collect_compactions(state, made, &copies);

    // the builds and refits due
    std::vector<BuildJob> jobs;
    for (auto& [id, mesh] : made.meshes) {
        const MeshWork work = work_for(mesh, copies);
        BuildJob job;
        if (work != MeshWork::kNone && plan_job(state, made, id, mesh, work, &job)) {
            jobs.push_back(job);
        }
    }

    // the instances
    const uint32_t slot = state.frames.slot_index();
    FrameScene& frame = core::at(made.frames, slot);
    const auto count = static_cast<uint32_t>(record.instances.size());
    if (!ensure_frame_scene(state, made, &frame, count)) {
        get_log("vulkan: ray tracing: no memory for a TLAS of %u instances", count);
        frame.built = false;
        made.any_built = false;
        rt_shadows_set_scene_locked(VK_NULL_HANDLE);
        cel_set_scene_locked(VK_NULL_HANDLE); // (added by cel shading: its draws' ray queries)
        rt_effects_set_scene_locked(VK_NULL_HANDLE);
        rt_fog_set_scene_locked(VK_NULL_HANDLE);
        return;
    }
    write_instances(state, made, frame, record);

    VkAccelerationStructureGeometryKHR tlas_geometry{};
    tlas_geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    tlas_geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    tlas_geometry.geometry.instances = VkAccelerationStructureGeometryInstancesDataKHR{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
        .pNext = nullptr,
        .arrayOfPointers = VK_FALSE,
        .data = {.deviceAddress = frame.instances_address}};
    VkAccelerationStructureBuildGeometryInfoKHR tlas_info{};
    tlas_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    tlas_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    tlas_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    tlas_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    tlas_info.geometryCount = 1;
    tlas_info.pGeometries = &tlas_geometry;
    tlas_info.dstAccelerationStructure = frame.tlas.handle;
    VkAccelerationStructureBuildSizesInfoKHR tlas_sizes{};
    tlas_sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkGetAccelerationStructureBuildSizesKHR(made.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &tlas_info,
                                            &count, &tlas_sizes);
    const VkDeviceSize wanted =
        std::max(scratch_for(made, jobs), align_up(tlas_sizes.buildScratchSize, made.scratch_alignment));
    if (!ensure_scratch(state, made, wanted)) {
        get_log("vulkan: ray tracing: no memory for %llu bytes of scratch", static_cast<unsigned long long>(wanted));
        for (const BuildJob& job : jobs) {
            job.mesh->needs_build = true; // (not built after all: tried again next frame)
            job.mesh->built = false;
        }
        frame.built = false;
        made.any_built = false;
        rt_shadows_set_scene_locked(VK_NULL_HANDLE);
        cel_set_scene_locked(VK_NULL_HANDLE); // (added by cel shading: its draws' ray queries)
        rt_effects_set_scene_locked(VK_NULL_HANDLE);
        rt_fog_set_scene_locked(VK_NULL_HANDLE);
        return;
    }

    // recorded in the stream, outside a pass: the inputs' writes (uploads, copies) and the reads of what is rebuilt
    // before; the compactions' copies and the builds; the compacted sizes asked for; the TLAS
    VkCommandBuffer commands = outside_pass(state);
    barrier(commands,
            VK_PIPELINE_STAGE_TRANSFER_BIT | kBuildStage | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, kBuildStage,
            VK_ACCESS_SHADER_READ_BIT | kStructureAccess);
    record_compactions(state, made, commands, copies);
    record_builds(made, commands, jobs);
    barrier(commands, kBuildStage, VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, kBuildStage, kStructureAccess);
    ask_compacted_sizes(state, made, commands, jobs);
    tlas_info.scratchData = {.deviceAddress = made.scratch_address};
    const VkAccelerationStructureBuildRangeInfoKHR tlas_range{
        .primitiveCount = count, .primitiveOffset = 0, .firstVertex = 0, .transformOffset = 0};
    const VkAccelerationStructureBuildRangeInfoKHR* tlas_ranges = &tlas_range;
    vkCmdBuildAccelerationStructuresKHR(commands, 1, &tlas_info, &tlas_ranges);
    barrier(commands, kBuildStage, VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, kAfterBuild,
            VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);
    rt_shadows_set_scene_locked(frame.tlas.handle); // (R4's pass traces this frame's TLAS)
    cel_set_scene_locked(frame.tlas.handle); // (added by cel shading: the draws' ray queries)
    rt_effects_set_scene_locked(frame.tlas.handle); // (R5's ambient occlusion too)
    rt_fog_set_scene_locked(frame.tlas.handle);     // (and the volumetric fog)
    frame.frame = record.frame;
    frame.count = count;
    frame.built = true;
    made.current = slot;
    made.any_built = true;
    ++made.stats.tlas_builds;
    made.stats.last_instances = count;
    count_bytes(made);
    made.changed = made.changed || !jobs.empty() || !copies.empty();
    if (made.changed && seconds_now() - made.last_log >= kLogSeconds) {
        made.changed = false;
        log_scene(made, true);
    }
    if (!made.budget_logged && made.stats.blas_bytes + made.stats.tlas_bytes > kBudgetBytes) {
        made.budget_logged = true;
        get_log("vulkan: ray tracing: the acceleration structures passed %.0f MB",
                static_cast<double>(kBudgetBytes) / kMegabyte);
    }
}

// --- the RayTracing --------------------------------------------------------------------------------------------------

// A mesh registered (an id already registered let go of first) - under the lock.
void register_mesh(Backend& state, Scene& made, rt::MeshId mesh, const rt::Geometry& geometry)
{
    const auto found = made.meshes.find(mesh);
    if (found != made.meshes.end()) {
        forget_mesh(state, made, &found->second);
    }
    Mesh& entry = made.meshes[mesh];
    entry = Mesh{};
    entry.geometry = geometry;
}

void VulkanRayTracing::add_mesh(rt::MeshId mesh, const rt::Geometry& geometry)
{
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    Scene& made = scene();
    if (!made.installed || mesh == 0) {
        return;
    }
    register_mesh(state, made, mesh, geometry);
    ++made.stats.asked_new;
}

// The same triangles from other buffers (a skinned mesh's vertex buffers take turns; buffers made again): a built
// dynamic mesh keeps its BLAS - the next refit reads the new buffers, as mesh_changed says -, anything else is built
// again.
bool same_triangles(const rt::Geometry& before, const rt::Geometry& now)
{
    return before.dynamic && now.dynamic && before.strip == now.strip && before.indices_32bit == now.indices_32bit &&
           before.vertex_count == now.vertex_count && before.index_count == now.index_count &&
           before.vertex_stride == now.vertex_stride && before.position_offset == now.position_offset &&
           before.indices == now.indices && before.index_offset == now.index_offset;
}

void VulkanRayTracing::update_mesh(rt::MeshId mesh, const rt::Geometry& geometry)
{
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    Scene& made = scene();
    if (!made.installed || mesh == 0) {
        return;
    }
    const auto found = made.meshes.find(mesh);
    if (found != made.meshes.end() && found->second.built && !found->second.refused &&
        same_triangles(found->second.geometry, geometry)) {
        // (fixed for the skater's shadow) the same triangles in another buffer - a skinned mesh cycling its vertex
        // buffers, or one moved in an arena - hold a newer pose: refitted, as mesh_changed does. Before, only the
        // buffer was swapped and the structure kept the pose it was built with.
        found->second.geometry = geometry;
        found->second.needs_refit = true;
        return;
    }
    register_mesh(state, made, mesh, geometry);
    ++made.stats.asked_geometry;
}

void VulkanRayTracing::set_material(rt::MaterialId material, const rt::Material& surface)
{
    backend().materials.set(material, surface); // (the table's own lock)
    // (added for the surface reflections: reflective too where the table made it so by its surface map)
    const bool reflective = surface.reflectivity > 0.0F || backend().materials.record(material).reflectivity > 0.0F;
    {
        const std::scoped_lock lock(backend().mutex);
        scene().alpha_refs[material] = surface.alpha_ref;
        if (reflective) {
            scene().reflective.insert(material);
        } else {
            scene().reflective.erase(material);
        }
    }
}

void VulkanRayTracing::remove_material(rt::MaterialId material)
{
    {
        const std::scoped_lock lock(backend().mutex);
        scene().alpha_refs.erase(material);
        scene().reflective.erase(material);
    }
    backend().materials.remove(material);
}

void VulkanRayTracing::remove_mesh(rt::MeshId mesh)
{
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    Scene& made = scene();
    const auto found = made.meshes.find(mesh);
    if (!made.installed || found == made.meshes.end()) {
        return;
    }
    forget_mesh(state, made, &found->second);
    made.meshes.erase(found);
    made.light_streams.erase(mesh);
}

void VulkanRayTracing::set_light_stream(rt::MeshId mesh, GpuVertexBuffer* stream)
{
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    Scene& made = scene();
    if (!made.installed || mesh == 0) {
        return;
    }
    if (stream == nullptr) {
        made.light_streams.erase(mesh);
    } else {
        made.light_streams[mesh] = stream;
    }
}

void VulkanRayTracing::mesh_changed(rt::MeshId mesh)
{
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    Scene& made = scene();
    const auto found = made.meshes.find(mesh);
    if (!made.installed || found == made.meshes.end()) {
        return;
    }
    Mesh& entry = found->second;
    if (entry.geometry.dynamic && entry.built) {
        entry.needs_refit = true;
    } else {
        entry.needs_build = true; // (a static mesh's vertices rewritten: built again)
        entry.refused = false;
        ++made.stats.asked_written;
    }
}

void VulkanRayTracing::begin_frame(const rt::FrameRecord& record)
{
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    Scene& made = scene();
    if (!made.installed || !state.draws_made) {
        return;
    }
    // (round S2) a lighting cut shown (rt_daylight.h DayLight::cut - a time, a moon, a weather set; the world's light
    // made for it): what the temporal effects remember is another lighting's, at another pre-exposure - every history
    // starts again, seeded in this frame, as at a camera cut (vulkan_rt_history.h). The running clock is no cut.
    const uint64_t cut = rt::day_light().cut;
    if (cut != made.history_cut && !rt::cut_settling()) {
        made.history_cut = cut;
        rt_effects_reset_history(state);
        rt_gi_reset_history(state);
        rt_fog_reset_history(state);
        rt_sky_reset_history();
    }
    if (state.materials.ready() && !state.materials.make_visible(state)) {
        get_log("vulkan: ray tracing: the material table's changes couldn't be made visible (out of memory)");
    }
    begin_scene_frame(state, made, record);
}

// (added by the first frame) A new world's builds run and its structures compacted now: the frame's work finished
// (the builds, the compacted sizes asked for), then the frame's scene built again - which collects the sizes, copies
// each BLAS into its smaller structure and builds the TLAS over the new addresses - until no size is waited for (a
// level with more static meshes than kCompactionQueries asks in turns). At most kSettleRounds rounds.
void VulkanRayTracing::settle(const rt::FrameRecord& record)
{
    constexpr int kSettleRounds = 8;
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    Scene& made = scene();
    if (!made.installed || !state.draws_made || !made.any_built) {
        return;
    }
    // (another world: what the temporal effects remember is of the last one - a camera held at the same place would
    // find it again; every history starts again, seeded in this frame - vulkan_rt_history.h)
    rt_effects_reset_history(state);
    rt_gi_reset_history(state);
    rt_fog_reset_history(state);
    const auto waiting = [&made] {
        return made.compactions_wanted > 0 || std::ranges::any_of(made.meshes, [](const auto& entry) {
                   return entry.second.query_serial != kNoQuery;
               });
    };
    for (int round = 0; round < kSettleRounds && waiting(); ++round) {
        if (!finish_work(state)) {
            return;
        }
        begin_scene_frame(state, made, record);
    }
    if (!finish_work(state)) {
        return;
    }
    destroy_finished(state, made);
    count_bytes(made);
    log_scene(made, true);
}

// (added by the first frame) The sky model's table and the sun's light made for the frame's sun before any view is
// drawn (the sun's pass and the fog would make it later in the frame - after the one-bounce light has read the sun's
// colour): only while the sun's effects or the sky model are on.
void VulkanRayTracing::prepare_frame()
{
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    const Scene& made = scene();
    const RtSunSettings& settings = sun_settings_held();
    // (added by the sky) rt_sky: a new level's sky matched before its first view, so what reads the drawn sky's colours
    // (the fog's table, made next) has the new one from the first frame
    if (made.installed && state.draws_made) {
        prepare_rt_sky(state);
    }
    if (!made.installed || !state.draws_made || !(settings.sun || settings.bloom || settings.sky_model)) {
        return;
    }
    std::array<float, 3> toward{};
    if (frame_sun(&toward)) {
        update_sky(state, outside_pass(state), toward);
    }
}

rt::ProbeResult VulkanRayTracing::probe(const rt::ProbeRequest& ray)
{
    std::vector<rt::ProbeResult> results;
    probe_many(std::span<const rt::ProbeRequest>(&ray, 1), &results);
    return results.empty() ? rt::ProbeResult{} : results.front();
}

// (the console's `rt probe`) The clouds' shadow map at the point (read back: the frame's work finished first) - its
// share of the light there (the map's transmittance over the lighting state's mean beam, as the shadows pass reads it,
// cloud_map_kept) - and the light the map was made for.
std::string VulkanRayTracing::describe_light(const std::array<float, 3>& point)
{
    // the light the shadows are cast from, and whether the world's and the objects' casters keep it from the point (a
    // ray each - the shadows pass's, from a little above the point)
    const rt::SceneSun sun = rt::scene_sun();
    const rt::DayLight day = rt::day_light();
    std::string out = text::format("light here: the shadows' light %s toward %.3f %.3f %.3f",
                                   sun.found() ? "found" : "none", static_cast<double>(std::get<0>(sun.toward)),
                                   static_cast<double>(std::get<1>(sun.toward)), static_cast<double>(std::get<2>(sun.toward)));
    if (sun.found()) {
        constexpr float kLift = 2.0F;
        const std::array<float, 3> from{std::get<0>(point), std::get<1>(point) + kLift, std::get<2>(point)};
        const std::array<rt::ProbeRequest, 2> rays{
            rt::ProbeRequest{.origin = from, .direction = sun.toward, .max_distance = 1.0e6F,
                             .cull_mask = kMaskWorld | kMaskAlphaTested, .cutouts = true},
            rt::ProbeRequest{.origin = from, .direction = sun.toward, .max_distance = 1.0e6F, .cull_mask = kMaskObjects}};
        std::vector<rt::ProbeResult> hits;
        probe_many(rays, &hits);
        if (hits.size() == rays.size()) {
            out += text::format(" - the world's casters %s it (%.0f units), the objects' %s it",
                                hits[0].hit ? "block" : "don't block", static_cast<double>(hits[0].distance),
                                hits[1].hit ? "block" : "don't block");
        }
    }
    out += text::format("; the lighting's cloud cover %.2f keeping %.3f of the direct light on average, %.3f of the "
                        "light on a level surface direct",
                        static_cast<double>(day.cloud_cover), static_cast<double>(day.cloud_beam),
                        static_cast<double>(rt::direct_share_now(day, sun.found(), 0.0)));
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    std::vector<float> texels;
    CloudShadowMap made{};
    if (!read_cloud_shadow_map(state, &texels, &made) || texels.empty()) {
        return out + "; no clouds' shadow map";
    }
    const auto side = static_cast<std::size_t>(std::lround(std::sqrt(static_cast<double>(texels.size()))));
    const float rise = std::max(core::at(made.light, 1), static_cast<float>(rt::kCloudMapLeastRise));
    const float up = std::max(core::at(made.place, 1) - std::get<1>(point), 0.0F) / rise;
    const float base_x = std::get<0>(point) + (core::at(made.light, 0) * up);
    const float base_z = std::get<2>(point) + (core::at(made.light, 2) * up);
    const float u = ((base_x - core::at(made.place, 0)) * core::at(made.place, 3)) + 0.5F;
    const float v = ((base_z - core::at(made.place, 2)) * core::at(made.place, 3)) + 0.5F;
    if (!(u >= 0.0F && u < 1.0F && v >= 0.0F && v < 1.0F) || side == 0) {
        return out + text::format("; the point is off the clouds' shadow map (%.2f, %.2f)", static_cast<double>(u),
                                  static_cast<double>(v));
    }
    const auto column = std::min(static_cast<std::size_t>(u * static_cast<float>(side)), side - 1);
    const auto row = std::min(static_cast<std::size_t>(v * static_cast<float>(side)), side - 1);
    const float kept = core::at(texels, (row * side) + column);
    const float share = core::at(made.light, 3);
    out += text::format("; the clouds' map toward %.3f %.3f %.3f keeps %.3f over that mean here (%.3f of the light), its "
                        "shadow kept %.2f",
                        static_cast<double>(core::at(made.light, 0)), static_cast<double>(core::at(made.light, 1)),
                        static_cast<double>(core::at(made.light, 2)), static_cast<double>(kept),
                        static_cast<double>(kept * std::max(day.cloud_beam, 1e-4F)), static_cast<double>(share));
    return out;
}

// The probe's buffers: the rays packed into an upload buffer, the hits' read-back buffer (false: none made).
bool make_probe_buffers(Backend& state, std::span<const TracedRay> rays, DeviceBuffer* ray_buffer, DeviceBuffer* hit_buffer)
{
    const VkDeviceSize ray_bytes = rays.size() * sizeof(PackedRay);
    const VkDeviceSize hit_bytes = rays.size() * sizeof(PackedHit);
    if (!state.memory.create_buffer(ray_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, BufferPlace::kUpload, ray_buffer) ||
        !state.memory.create_buffer(hit_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, BufferPlace::kReadBack,
                                    hit_buffer) ||
        ray_buffer->mapped == nullptr || hit_buffer->mapped == nullptr) {
        state.memory.destroy_buffer(ray_buffer);
        state.memory.destroy_buffer(hit_buffer);
        return false;
    }
    std::vector<PackedRay> packed(rays.size());
    for (std::size_t i = 0; i < rays.size(); ++i) {
        const TracedRay& ray = core::at(rays, i);
        core::at(packed, i) = {.origin_t_min = {std::get<0>(ray.origin), std::get<1>(ray.origin),
                                                std::get<2>(ray.origin), ray.t_min},
                               .direction_t_max = {std::get<0>(ray.direction), std::get<1>(ray.direction),
                                                   std::get<2>(ray.direction), ray.t_max},
                               .cull_mask = {ray.cull_mask, ray.cutouts ? 1U : 0U, 0, 0}};
    }
    std::memcpy(ray_buffer->mapped, packed.data(), ray_bytes);
    state.memory.flush(*ray_buffer, 0, ray_bytes);
    return true;
}

// The probe's descriptor set pointed at `frame`'s TLAS and the buffers - only while no recorded dispatch that reads it
// is still to run (the frame's work finished first, or no probe_later in flight).
void write_probe_set(const Scene& made, const FrameScene& frame, const DeviceBuffer& ray_buffer,
                     const DeviceBuffer& hit_buffer)
{
    const VkDeviceSize ray_bytes = ray_buffer.bytes;
    const VkDeviceSize hit_bytes = hit_buffer.bytes;
    const VkWriteDescriptorSetAccelerationStructureKHR structure{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
        .pNext = nullptr,
        .accelerationStructureCount = 1,
        .pAccelerationStructures = &frame.tlas.handle};
    const VkDescriptorBufferInfo ray_info{.buffer = ray_buffer.buffer, .offset = 0, .range = ray_bytes};
    const VkDescriptorBufferInfo hit_info{.buffer = hit_buffer.buffer, .offset = 0, .range = hit_bytes};
    const VkDescriptorBufferInfo geometry_info{.buffer = frame.geometry.buffer, .offset = 0, .range = VK_WHOLE_SIZE};
    const std::array<VkWriteDescriptorSet, 4> writes{{
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .pNext = &structure,
         .dstSet = made.probe.set,
         .dstBinding = 0,
         .dstArrayElement = 0,
         .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
         .pImageInfo = nullptr,
         .pBufferInfo = nullptr,
         .pTexelBufferView = nullptr},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .pNext = nullptr,
         .dstSet = made.probe.set,
         .dstBinding = 1,
         .dstArrayElement = 0,
         .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .pImageInfo = nullptr,
         .pBufferInfo = &ray_info,
         .pTexelBufferView = nullptr},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .pNext = nullptr,
         .dstSet = made.probe.set,
         .dstBinding = 2,
         .dstArrayElement = 0,
         .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .pImageInfo = nullptr,
         .pBufferInfo = &hit_info,
         .pTexelBufferView = nullptr},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .pNext = nullptr,
         .dstSet = made.probe.set,
         .dstBinding = 3,
         .dstArrayElement = 0,
         .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .pImageInfo = nullptr,
         .pBufferInfo = &geometry_info,
         .pTexelBufferView = nullptr},
    }};
    vkUpdateDescriptorSets(made.device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

// The probe's dispatch of `count` rays recorded (its set written first): after the host's writes and the TLAS's build
// (recorded before it - in the frame's stream, for probe_later), its hits made visible to the host.
void record_probe(Backend& state, const Scene& made, VkCommandBuffer commands, uint32_t count)
{
    const VkDescriptorSet material_set = state.materials.descriptor_set();
    barrier(commands, VK_PIPELINE_STAGE_HOST_BIT | kBuildStage,
            VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, made.probe.pipeline);
    const std::array<VkDescriptorSet, 2> sets{made.probe.set, material_set};
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, made.probe.pipeline_layout, 0,
                            static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);
    vkCmdPushConstants(commands, made.probe.pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(count),
                       &count);
    vkCmdDispatch(commands, (count + kProbeGroup - 1) / kProbeGroup, 1, 1);
    barrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            VK_ACCESS_HOST_READ_BIT);
}

// The hits read back from the probe's buffer.
void read_hits(Backend& state, const DeviceBuffer& hit_buffer, std::size_t count, std::vector<TracedHit>* hits)
{
    vmaInvalidateAllocation(state.memory.allocator(), hit_buffer.allocation, 0, VK_WHOLE_SIZE);
    std::vector<PackedHit> packed(count);
    std::memcpy(packed.data(), hit_buffer.mapped, count * sizeof(PackedHit));
    hits->clear();
    hits->reserve(packed.size());
    for (const PackedHit& each : packed) {
        hits->push_back({.hit = (each.flags & kProbeHitFlag) != 0,
                         .t = each.t,
                         .instance = each.instance,
                         .custom_index = each.custom_index,
                         .primitive = each.primitive,
                         .barycentrics = each.barycentrics,
                         .front_face = (each.flags & kProbeFrontFlag) != 0});
    }
}

// A probe's request as the pass's ray (its direction made a unit one: distances in the request's units).
TracedRay traced_of(const rt::ProbeRequest& ray)
{
    const std::array<float, 3>& way = ray.direction;
    const float length = std::sqrt((std::get<0>(way) * std::get<0>(way)) + (std::get<1>(way) * std::get<1>(way)) +
                                   (std::get<2>(way) * std::get<2>(way)));
    // (a ray of no length: traced to nothing - t_max 0)
    const float scale = length > 0.0F ? 1.0F / length : 0.0F;
    return {.origin = ray.origin,
            .direction = {std::get<0>(way) * scale, std::get<1>(way) * scale,
                          length > 0.0F ? std::get<2>(way) * scale : 1.0F},
            .t_min = 0.0F,
            .t_max = length > 0.0F ? ray.max_distance : 0.0F,
            .cull_mask = ray.cull_mask & rt::kAllMasks,
            .cutouts = ray.cutouts};
}

// A hit as the probe's answer (`meshes`: the traced TLAS's instances' meshes).
rt::ProbeResult result_of(const TracedHit& hit, std::span<const rt::MeshId> meshes)
{
    rt::ProbeResult result;
    result.traced = true;
    result.hit = hit.hit;
    if (hit.hit) {
        result.distance = hit.t;
        result.instance = hit.instance;
        result.material = hit.custom_index;
        result.primitive = hit.primitive;
        if (hit.instance < meshes.size()) {
            result.mesh = core::at(meshes, hit.instance);
        }
    }
    return result;
}

void VulkanRayTracing::probe_many(std::span<const rt::ProbeRequest> rays, std::vector<rt::ProbeResult>* results)
{
    constexpr std::size_t kBatch = std::size_t{1} << 20; // rays a dispatch (its buffers: about 80 MB)
    results->assign(rays.size(), rt::ProbeResult{});
    std::vector<TracedRay> traced;
    std::vector<TracedHit> hits;
    for (std::size_t first = 0; first < rays.size(); first += kBatch) {
        const std::size_t count = std::min(kBatch, rays.size() - first);
        traced.assign(count, TracedRay{});
        for (std::size_t i = 0; i < count; ++i) {
            core::at(traced, i) = traced_of(core::at(rays, first + i));
        }
        if (trace_rays_now(traced, &hits) != TraceResult::kTraced || hits.size() != count) {
            return;
        }
        const std::scoped_lock lock(backend().mutex);
        const FrameScene& frame = core::at(scene().frames, scene().current);
        for (std::size_t i = 0; i < count; ++i) {
            core::at(*results, first + i) = result_of(core::at(hits, i), frame.meshes);
        }
    }
}

// (added for the relit mode's moving light) The rays recorded into the frame's stream after its TLAS's build - no wait:
// answered by probe_ready once the device has done the frame.
uint64_t VulkanRayTracing::probe_later(std::span<const rt::ProbeRequest> rays)
{
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    Scene& made = scene();
    constexpr std::size_t kMostRays = std::size_t{1} << 20; // (probe_many's batch)
    if (!made.installed || !made.any_built || !state.draws_made || rays.empty() || rays.size() > kMostRays ||
        !made.later.empty()) {
        return 0;
    }
    const FrameScene& frame = core::at(made.frames, made.current);
    if (!frame.built || !make_probe_pass(made, state)) {
        return 0;
    }
    std::vector<TracedRay> traced(rays.size());
    for (std::size_t i = 0; i < rays.size(); ++i) {
        core::at(traced, i) = traced_of(core::at(rays, i));
    }
    DeviceBuffer ray_buffer;
    LaterProbe probe{.ticket = made.next_ticket++, .serial = 0, .hits = {}, .count = rays.size(), .meshes = frame.meshes};
    if (!make_probe_buffers(state, traced, &ray_buffer, &probe.hits)) {
        return 0;
    }
    write_probe_set(made, frame, ray_buffer, probe.hits);
    record_probe(state, made, outside_pass(state), static_cast<uint32_t>(rays.size()));
    probe.serial = state.frames.serial();
    state.frames.retire(ray_buffer); // (destroyed once the frame is done)
    const uint64_t ticket = probe.ticket;
    made.later.push_back(std::move(probe));
    return ticket;
}

bool VulkanRayTracing::probe_ready(uint64_t ticket, std::vector<rt::ProbeResult>* results)
{
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    Scene& made = scene();
    results->clear();
    const auto found =
        std::ranges::find_if(made.later, [ticket](const LaterProbe& each) { return each.ticket == ticket; });
    if (found == made.later.end()) {
        return true; // (lost: the scene released)
    }
    if (state.frames.completed() < found->serial) {
        return false;
    }
    std::vector<TracedHit> hits;
    read_hits(state, found->hits, found->count, &hits);
    results->reserve(hits.size());
    for (const TracedHit& hit : hits) {
        results->push_back(result_of(hit, found->meshes));
    }
    state.memory.destroy_buffer(&found->hits);
    made.later.erase(found);
    return true;
}

void VulkanRayTracing::probe_forget(uint64_t ticket)
{
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    Scene& made = scene();
    const auto found =
        std::ranges::find_if(made.later, [ticket](const LaterProbe& each) { return each.ticket == ticket; });
    if (found != made.later.end()) {
        state.frames.retire(found->hits); // (its dispatch may be still to run: destroyed once this frame is done)
        made.later.erase(found);
    }
}

} // namespace

SceneView current_scene(const Backend& state)
{
    (void)state;
    const Scene& made = scene();
    if (!made.installed || !made.any_built) {
        return {};
    }
    const FrameScene& frame = core::at(made.frames, made.current);
    return {.tlas = frame.tlas.handle,
            .address = frame.tlas.address,
            .instances = frame.count,
            .frame = frame.frame,
            .geometry = frame.geometry.buffer,
            .geometry_bytes = std::max<VkDeviceSize>(static_cast<VkDeviceSize>(frame.count) * sizeof(GeometryRecord),
                                                     sizeof(GeometryRecord)),
            .reflective_instances = frame.reflective,
            .instances_address = frame.instances_address};
}

void scene_instance_triangles(const Backend& state, std::vector<uint32_t>* out)
{
    (void)state;
    out->clear();
    const Scene& made = scene();
    if (!made.installed || !made.any_built) {
        return;
    }
    const FrameScene& frame = core::at(made.frames, made.current);
    out->reserve(frame.meshes.size());
    for (const rt::MeshId id : frame.meshes) {
        const auto found = made.meshes.find(id);
        out->push_back(found != made.meshes.end() && found->second.built ? found->second.triangles : 0U);
    }
}

std::string_view scene_geometry_glsl()
{
    // (round L) the light store's decode (engine/gfx/light_store.h) after the extensions and the bindings' defines
    static const std::string made = [] {
        const std::size_t at = kGeometryGlsl.find("struct GeometryRecord {");
        return std::string(kGeometryGlsl.substr(0, at)) + rt::kLightStoreGlsl + std::string(kGeometryGlsl.substr(at));
    }();
    return made;
}

std::string_view cutout_rays_glsl()
{
    static constexpr const char* kCutoutRays = R"(
bool cutout_kept(uint instance, uint primitive, uint id, vec2 barycentrics)
{
    HitVertices v;
    if (!hit_vertices(instance, primitive, v)) {
        return true;
    }
    return material_alpha_passes_at(id, hit_uv(v, barycentrics));
}

bool ray_blocked(uint mask, vec3 origin, vec3 direction, float reach)
{
    if (mask == 0u || reach <= 0.0) {
        return false;
    }
    rayQueryEXT query;
    rayQueryInitializeEXT(query, u_scene, gl_RayFlagsTerminateOnFirstHitEXT, mask, origin, 0.0, direction, reach);
    while (rayQueryProceedEXT(query)) {
        if (rayQueryGetIntersectionTypeEXT(query, false) == gl_RayQueryCandidateIntersectionTriangleEXT &&
            cutout_kept(rayQueryGetIntersectionInstanceIdEXT(query, false),
                        rayQueryGetIntersectionPrimitiveIndexEXT(query, false),
                        rayQueryGetIntersectionInstanceCustomIndexEXT(query, false),
                        rayQueryGetIntersectionBarycentricsEXT(query, false))) {
            rayQueryConfirmIntersectionEXT(query);
        }
    }
    return rayQueryGetIntersectionTypeEXT(query, true) != gl_RayQueryCommittedIntersectionNoneEXT;
}
)";
    return kCutoutRays;
}

std::string_view hit_surface_glsl()
{
    // (path tracing, stage 1 - P1: one hit surface; vulkan_rt_scene.h) The painted surface's values and the metal's
    // least F0 are the material table's (rt_material_table.h), written in as constants.
    static const std::string made = [] {
        std::string text = "const float HIT_SURFACE_F0 = " +
                           std::to_string(std::get<0>(rt::material_f0(rt::MaterialKind::kPainted))) +
                           ";\nconst float HIT_SURFACE_ROUGHNESS = " +
                           std::to_string(rt::material_values(rt::MaterialKind::kPainted).roughness) +
                           ";\nconst float HIT_SURFACE_METAL_F0 = " + std::to_string(kHitMetalF0) + ";\n";
        text += R"(
const uint HIT_RELIT_WORLD = 0u;
const uint HIT_BAKED_WORLD = 1u;
const uint HIT_OBJECT = 2u;
const uint HIT_KIND_DIELECTRIC = 0u;
const uint HIT_KIND_METAL = 1u;
const uint HIT_INSTANCE_OBJECT = 32u; // (rt::kObject)
struct HitSurface {
    vec3 position;
    vec3 normal;
    vec3 shading_normal;
    vec3 base_colour;
    BrdfMaterial material;
    uint kind;
    float alpha;
    bool cut_out;
    vec3 emitted;
    uint origin;
    float area;
    bool hidden;
};
// The light store at the hit, as stored (the global unit - not x the frame's pre-exposure): the tint, the emission,
// whether the emission is a lamp drawn in a texture (word 6's blue), whether the mesh is hidden (word 6's alpha 128).
// False: no store there (none, or an animated vertex).
bool hit_store(uint instance, uint primitive, vec2 barycentrics, out vec3 tint, out vec3 emit, out bool masked,
               out bool hidden)
{
    GeometryRecord g = geometry_record(instance);
    tint = vec3(0.0);
    emit = vec3(0.0);
    masked = false;
    hidden = false;
    if ((g.flags & GEOMETRY_LIVE) == 0u || (g.flags & GEOMETRY_LIGHT) == 0u) {
        return false;
    }
    float weights[3] = float[3](1.0 - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);
    for (uint k = 0u; k < 3u; ++k) {
        uint at = geometry_corner(g, primitive, k) * 32u; // (rt_cel.h kCelStreamBytes)
        vec4 flags = unpackUnorm4x8(geometry_word(g.light_address, at + 24u)).zyxw;
        if (flags.a < 0.25) {
            return false; // (an animated vertex: the game's colour, no store)
        }
        if (flags.a < 0.75) {
            hidden = true;
        }
        masked = masked || flags.b > 0.5;
        tint += unpackUnorm4x8(geometry_word(g.light_address, at + 4u)).zyx * (weights[k] * LIGHT_PER_UNORM);
        emit += stored_light(geometry_word(g.light_address, at + 28u)) * weights[k];
    }
    return true;
}
bool hit_surface(uint instance, uint primitive, uint id, vec2 barycentrics, mat4x3 object_to_world, vec3 direction,
                 out HitSurface s)
{
    s.position = vec3(0.0);
    s.normal = -direction;
    s.shading_normal = -direction;
    s.base_colour = vec3(0.0);
    s.material = brdf_material(vec3(0.0), vec3(HIT_SURFACE_F0), HIT_SURFACE_ROUGHNESS);
    s.kind = HIT_KIND_DIELECTRIC;
    s.alpha = 1.0;
    s.cut_out = false;
    s.emitted = vec3(0.0);
    s.origin = HIT_BAKED_WORLD;
    s.area = 0.0;
    s.hidden = false;
    HitVertices v;
    if (!hit_vertices(instance, primitive, v)) {
        return false;
    }
    vec3 a = object_to_world * vec4(v.position[0], 1.0);
    vec3 b = object_to_world * vec4(v.position[1], 1.0);
    vec3 c = object_to_world * vec4(v.position[2], 1.0);
    float w0 = 1.0 - barycentrics.x - barycentrics.y;
    s.position = a * w0 + b * barycentrics.x + c * barycentrics.y;
    vec3 across = cross(b - a, c - a);
    float twice = length(across);
    s.area = 0.5 * twice;
    vec3 n = twice > 0.0 ? across / twice : -direction;
    if (dot(n, direction) > 0.0) {
        n = -n;
    }
    s.normal = n;
    s.shading_normal = n;
    vec3 vertex_normal;
    if (hit_normal(instance, primitive, barycentrics, vertex_normal)) {
        vec3 smooth_n = transpose(inverse(mat3(object_to_world))) * vertex_normal;
        float smooth_length = length(smooth_n);
        if (smooth_length > 1e-12 && !any(isnan(smooth_n))) {
            smooth_n /= smooth_length;
            s.shading_normal = dot(smooth_n, n) < 0.0 ? -smooth_n : smooth_n;
        }
    }
    Material surface = material(id);
    vec2 uv = hit_uv_set(instance, primitive, barycentrics, surface.uv_set);
    vec4 texel = material_texel_lod(id, uv, 0.0) * surface.colour;
    s.alpha = texel.a;
    s.cut_out = !material_alpha_passes(id, texel.a);
    vec3 linear = linear_from_display(clamp(texel.rgb, 0.0, 1.0));
    bool object = (geometry_record(instance).instance_flags & HIT_INSTANCE_OBJECT) != 0u;
    vec3 tint;
    vec3 emit;
    bool masked;
    bool hidden;
    if (!object && hit_store(instance, primitive, barycentrics, tint, emit, masked, hidden)) {
        s.origin = HIT_RELIT_WORLD;
        s.hidden = hidden;
        s.base_colour = linear * tint;
        float lamp_lit = masked ? smoothstep(0.6, 0.7, max(linear.r, max(linear.g, linear.b))) : 1.0;
        s.emitted = emit * linear * lamp_lit;
    } else {
        s.origin = object ? HIT_OBJECT : HIT_BAKED_WORLD;
        s.base_colour = linear;
    }
    if (!material_has_surface(id) || surface.f0 <= 0.0) {
        s.material = brdf_material(s.base_colour, vec3(HIT_SURFACE_F0), HIT_SURFACE_ROUGHNESS);
        return true;
    }
    float r = clamp(material_surface_lod(id, uv, 0.0).b, 0.0, 1.0);
    r = mix(r, surface.kind_roughness, surface.kind_roughness_weight);
    if (surface.f0 >= HIT_SURFACE_METAL_F0) {
        s.kind = HIT_KIND_METAL;
        s.material = brdf_material(vec3(0.0), s.base_colour, r);
    } else {
        s.material = brdf_material(s.base_colour, vec3(surface.f0), r);
    }
    return true;
}
)";
        return text;
    }();
    return made;
}

VkBufferUsageFlags ray_tracing_buffer_usage(const Backend& state)
{
    return state.context.ray_tracing_enabled()
               ? VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                     VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
               : 0;
}

void create_ray_tracing(Backend& state)
{
    Scene& made = scene();
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(state.context.physical_device(), &properties);
    made.device_name = std::data(properties.deviceName);
    if (!state.context.ray_tracing_enabled() || !state.draws_made) {
        const std::string why = missing_for_ray_tracing(state);
        get_log("vulkan: ray tracing off on %s: %s", made.device_name.c_str(), why.c_str());
        return;
    }
    made.device = state.context.device();
    VkPhysicalDeviceAccelerationStructurePropertiesKHR structures{};
    structures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
    VkPhysicalDeviceProperties2 asked{};
    asked.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    asked.pNext = &structures;
    vkGetPhysicalDeviceProperties2(state.context.physical_device(), &asked);
    made.scratch_alignment = std::max<VkDeviceSize>(structures.minAccelerationStructureScratchOffsetAlignment, 1);
    const VkQueryPoolCreateInfo queries{.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                                        .pNext = nullptr,
                                        .flags = 0,
                                        .queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR,
                                        .queryCount = kCompactionQueries,
                                        .pipelineStatistics = 0};
    if (vkCreateQueryPool(made.device, &queries, nullptr, &made.queries) != VK_SUCCESS) {
        made.queries = VK_NULL_HANDLE; // (built, not compacted)
    }
    made.free_queries.clear();
    if (made.queries != VK_NULL_HANDLE) {
        for (uint32_t i = kCompactionQueries; i > 0; --i) {
            made.free_queries.push_back(i - 1);
        }
    }
    made.installed = true;
    made.last_log = seconds_now();
    install_surface_light(); // (round V: the world lit per surface - vulkan_surface_light.h)
    install_path_reference(); // (path tracing, stage 1: the reference - vulkan_path_reference.h)
    rt::set_ray_tracing(&made.backend);
    get_log("vulkan: ray tracing on (%s): acceleration structures and ray queries; at most %llu instances, scratch "
            "aligned to %u",
            made.device_name.c_str(), static_cast<unsigned long long>(structures.maxInstanceCount),
            structures.minAccelerationStructureScratchOffsetAlignment);
}

void release_ray_tracing(Backend& state)
{
    Scene& made = scene();
    if (!made.installed) {
        return;
    }
    if (rt::ray_tracing() == &made.backend) {
        rt::set_ray_tracing(nullptr);
    }
    uninstall_surface_light(); // (round V)
    release_surface_light(state);
    uninstall_path_reference(); // (path tracing, stage 1)
    release_path_reference(state);
    rt_shadows_set_scene_locked(VK_NULL_HANDLE);
    cel_set_scene_locked(VK_NULL_HANDLE); // (added by cel shading)
    rt_effects_set_scene_locked(VK_NULL_HANDLE);
    rt_fog_set_scene_locked(VK_NULL_HANDLE);
    count_bytes(made);
    log_scene(made, true);
    // (the device is idle: release_device_objects waited for it)
    for (auto& [id, mesh] : made.meshes) {
        forget_mesh(state, made, &mesh);
    }
    made.meshes.clear();
    made.light_streams.clear();
    made.alpha_refs.clear();
    made.reflective.clear();
    for (FrameScene& frame : made.frames) {
        retire_structure(state, made, &frame.tlas);
        retire(made, VK_NULL_HANDLE, &frame.instances, 0);
        retire(made, VK_NULL_HANDLE, &frame.geometry, 0);
        retire(made, VK_NULL_HANDLE, &frame.object_lights, 0);
        frame = FrameScene{};
    }
    retire(made, VK_NULL_HANDLE, &made.scratch, 0);
    for (Retiring& each : made.retiring) {
        destroy_now(state, made, &each);
    }
    made.retiring.clear();
    for (LaterProbe& each : made.later) { // (added for probe_later: the device is idle here)
        state.memory.destroy_buffer(&each.hits);
    }
    made.later.clear();
    destroy_probe_pass(made);
    if (made.queries != VK_NULL_HANDLE) {
        vkDestroyQueryPool(made.device, made.queries, nullptr);
    }
    made.queries = VK_NULL_HANDLE;
    made.free_queries.clear();
    made.scratch_bytes = 0;
    made.scratch_address = 0;
    made.any_built = false;
    made.installed = false;
    made.stats = SceneStats{};
    made.device = nullptr;
}

TraceResult trace_rays_now(std::span<const TracedRay> rays, std::vector<TracedHit>* hits)
{
    hits->clear();
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    Scene& made = scene();
    if (!made.installed) {
        return TraceResult::kNoRayTracing;
    }
    if (!made.any_built) {
        return TraceResult::kNoScene;
    }
    if (rays.empty()) {
        return TraceResult::kTraced;
    }
    if (!make_probe_pass(made, state)) {
        return TraceResult::kFailed;
    }
    // the frame's work (the TLAS's build among it) run first
    if (!finish_work(state)) {
        return TraceResult::kFailed;
    }
    destroy_finished(state, made);
    const FrameScene& frame = core::at(made.frames, made.current);
    DeviceBuffer ray_buffer;
    DeviceBuffer hit_buffer;
    if (!make_probe_buffers(state, rays, &ray_buffer, &hit_buffer)) {
        return TraceResult::kFailed;
    }
    const auto count = static_cast<uint32_t>(rays.size());
    write_probe_set(made, frame, ray_buffer, hit_buffer);
    const bool ran =
        state.memory.submit_now([&](VkCommandBuffer commands) { record_probe(state, made, commands, count); });
    if (ran) {
        read_hits(state, hit_buffer, rays.size(), hits);
    }
    state.memory.destroy_buffer(&ray_buffer);
    state.memory.destroy_buffer(&hit_buffer);
    return ran ? TraceResult::kTraced : TraceResult::kFailed;
}

SceneStats scene_stats()
{
    const std::scoped_lock lock(backend().mutex);
    Scene& made = scene();
    count_bytes(made);
    return made.stats;
}

} // namespace render::vulkan
