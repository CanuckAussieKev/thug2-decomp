// NOT ORIGINAL - ray-traced ambient occlusion and its denoiser (vulkan_rt_effects.h; docs/design/vulkan.md, R5).
#include "gpu_vulkan/vulkan_rt_effects.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "engine/gfx/rt_daylight.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/glsl_compiler.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_cel_draw.h"
#include "gpu_vulkan/vulkan_formats.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_objects.h"
#include "gpu_vulkan/vulkan_rt_history.h"
#include "gpu_vulkan/vulkan_rt_glsl.h"
#include "gpu_vulkan/vulkan_upscale.h"
#include "gpu_vulkan/vulkan_rt_materials.h"
#include "gpu_vulkan/vulkan_rt_scene.h"
#include "gpu_vulkan/vulkan_ring.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

// The once-only reports' keys (Backend::reported).
constexpr uint64_t kReportNoRayTracing = 0x52350001U;
constexpr uint64_t kReportNoScene = 0x52350002U;
constexpr uint64_t kReportTargets = 0x52350003U;
constexpr uint64_t kReportPipeline = 0x52350004U;
constexpr uint64_t kReportFirstPass = 0x52350005U;
constexpr uint64_t kReportImages = 0x52350006U;
constexpr uint64_t kReportNoMaterials = 0x52350007U;
constexpr uint64_t kReportSeeded = 0x52350008U; // (added by the first frame: a history seeded - vulkan_rt_history.h)
constexpr uint64_t kReportIndirect = 0x00000100U; // (added to a key: the indirect light's report)

constexpr float kMaxStrength = 1.0F;
constexpr float kMinRadius = 1.0F;
constexpr float kMaxRadius = 1000.0F;
constexpr float kPerMille = 1000.0F; // (added for the characters: rt_ao_characters in the trace's integer constants)
// The indirect light's.
constexpr float kMaxGiStrength = 4.0F;
constexpr float kMaxGiDistance = 5000.0F;
constexpr uint32_t kGiMask = 0x01 | 0x02 | 0x04; // the bounce rays (and the surface's own): world, objects, alpha-tested
constexpr uint32_t kGiSunMask = 0x01 | 0x02;      // the rays toward the sun: the opaque world and objects
constexpr float kSunDistance = 20000.0F;         // how far a ray toward the sun looks (world units)
constexpr float kBounceSpread = 0.05F; // a bounce ray's footprint grows this much per unit (it stands for a wide cone)
constexpr float kSmallestUnit = 1e-3F; // (the rays' noise unit, however dark the level's lights)
constexpr float kGiSunLight = 1.0F;          // the sun's light on a surface facing it (each channel)
constexpr unsigned kMaxRays = 16;
constexpr float kOffsetScale = 1.0F; // the rays' start pushed off the surface by this much, growing with distance
constexpr uint32_t kGroupSide = 8;   // the compute passes' work groups: 8 x 8 pixels
constexpr std::size_t kMatrixSide = 4;
constexpr std::size_t kMatrixFloats = kMatrixSide * kMatrixSide;
constexpr std::size_t kPushConstantBytes = 128; // (the space every Vulkan device has)
constexpr double kSingular = 1e-300;            // a pivot this small: the matrix can't be inverted
constexpr uint32_t kSequenceWrap = 4096;        // (the directions' sequence index wraps: float precision)

// The full-screen triangle (three vertices, no buffers).
constexpr const char* kVertexSource = R"(#version 460
void main()
{
    vec2 at = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(at * 2.0 - 1.0, 0.0, 1.0);
}
)";

// What every pass shares: the push constants (their meaning per pass in PassConstants below) and the guide's packing -
// the world position's three floats and the normal (octahedral, two 16-bit numbers from 1: 0 means no surface).
constexpr const char* kCommon = R"(
layout(push_constant) uniform Pass {
    mat4 matrix;
    ivec4 rect;
    vec4 camera;
    vec4 params;
    ivec4 extra;
} pc;

vec2 sign_of(vec2 v) { return vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0); }

uint pack_normal(vec3 n)
{
    vec2 o = n.xy / (abs(n.x) + abs(n.y) + abs(n.z));
    if (n.z < 0.0) {
        o = (1.0 - abs(o.yx)) * sign_of(o);
    }
    uvec2 q = uvec2(round(clamp(o * 0.5 + 0.5, 0.0, 1.0) * 65534.0)) + uvec2(1u);
    return q.x | (q.y << 16);
}

vec3 unpack_normal(uint v)
{
    vec2 o = (vec2(float(v & 0xFFFFu), float(v >> 16)) - 1.0) / 65534.0 * 2.0 - 1.0;
    vec3 n = vec3(o, 1.0 - abs(o.x) - abs(o.y));
    if (n.z < 0.0) {
        n.xy = (1.0 - abs(n.yx)) * sign_of(n.xy);
    }
    return normalize(n);
}

bool inside(ivec2 local) { return all(greaterThanEqual(local, ivec2(0))) && all(lessThan(local, pc.rect.zw)); }

// How far apart two points on one surface may be from its plane (world units) at `distance` from the camera: the
// depth's precision falls with distance.
float plane_tolerance(float distance) { return 0.5 + 0.005 * distance + 1.2e-7 * distance * distance; }

// What the denoiser filters (VALUE_RGB: the indirect light's colour, its history's length in a; else the occlusion in r,
// the length in g), how far apart two values are, and the range a value is kept in.
#ifdef VALUE_RGB
#define Value vec3
Value value_of(vec4 t) { return t.rgb; }
float length_of(vec4 t) { return t.a; }
vec4 packed(Value v, float history_length) { return vec4(v, history_length); }
float difference(Value a, Value b) { vec3 d = abs(a - b); return max(d.x, max(d.y, d.z)); }
Value kept_in_range(Value v) { return max(v, vec3(0.0)); }
#else
#define Value float
Value value_of(vec4 t) { return t.r; }
float length_of(vec4 t) { return t.g; }
vec4 packed(Value v, float history_length) { return vec4(v, history_length, 0.0, 0.0); }
float difference(Value a, Value b) { return abs(a - b); }
Value kept_in_range(Value v) { return clamp(v, 0.0, 1.0); }
#endif
)";

// Step 1: the rays. matrix: (pixel x, pixel y, depth, 1) -> world (the target's pixels); rect: the viewport; camera: the
// camera's position (w: the depth sample's offset from the pixel's centre, y); params: x the rays' length, y rays, z the
// offset's scale, w the depth sample's offset (x); extra: x the frame's index, y the mask. What both effects' rays share:
constexpr const char* kTraceShared = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) uniform accelerationStructureEXT u_scene;
#ifdef MULTISAMPLED
layout(set = 0, binding = 1) uniform sampler2DMS u_depth;
#else
layout(set = 0, binding = 1) uniform sampler2D u_depth;
#endif
layout(set = 0, binding = 2, rgba16f) uniform writeonly image2D o_out;
layout(set = 0, binding = 3, rgba32ui) uniform writeonly uimage2D o_guide;

float depth_at(ivec2 p) { return texelFetch(u_depth, p, 0).r; }

// (a multisampled depth is read at sample 0, which isn't at the pixel's centre: the point is where the sample is)
vec3 world_at(ivec2 p, float d)
{
    vec4 w = pc.matrix * vec4(vec2(p) + vec2(pc.params.w, pc.camera.w), d, 1.0);
    return w.xyz / w.w;
}

// The surface's step to the next pixel along `step`: toward the neighbour whose depth is nearer this pixel's (so a
// silhouette's other side isn't taken), zero when neither neighbour is inside the viewport.
vec3 surface_step(ivec2 p, float d, vec3 here, ivec2 step)
{
    ivec2 low = pc.rect.xy;
    ivec2 high = pc.rect.xy + pc.rect.zw - ivec2(1);
    ivec2 ahead = p + step;
    ivec2 behind = p - step;
    bool ahead_in = all(greaterThanEqual(ahead, low)) && all(lessThanEqual(ahead, high));
    bool behind_in = all(greaterThanEqual(behind, low)) && all(lessThanEqual(behind, high));
    float d_ahead = ahead_in ? depth_at(ahead) : 2.0;
    float d_behind = behind_in ? depth_at(behind) : 2.0;
    if (!ahead_in && !behind_in) {
        return vec3(0.0);
    }
    if (ahead_in && (!behind_in || abs(d_ahead - d) <= abs(d_behind - d))) {
        return world_at(ahead, d_ahead) - here;
    }
    return here - world_at(behind, d_behind);
}

uint hash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

// The pixel's surface normal from the nearer neighbours' depths, facing the camera (toward it where there is none).
vec3 surface_normal(ivec2 p, float d, vec3 here, vec3 to_camera, float distance)
{
    vec3 normal = cross(surface_step(p, d, here, ivec2(1, 0)), surface_step(p, d, here, ivec2(0, 1)));
    float normal_length = length(normal);
    normal = normal_length > 1e-20 ? normal / normal_length : to_camera / max(distance, 1e-6);
    if (dot(normal, to_camera) < 0.0) {
        normal = -normal;
    }
    return normal;
}

// How far a ray's start is pushed off a surface `distance` from the camera.
float start_offset(float distance) { return pc.params.z * (1.0 + distance * 0.0005 + distance * distance * 6e-8); }

// (added for the skater's look) The mask the pixel's rays trace with: the pass's, less the objects (0x02: the skater,
// peds, vehicles) where the pixel shows one - a ray from the camera through it against the objects alone ends where
// the depth does. An object's low-poly body traced against itself from the depth's per-triangle normals drew dark
// facets over the skater (its shirt, its arms); objects take the world's occlusion and bounce only, as they take the
// world's shadows only (vulkan_rt_shadows.cpp).
// (added for the skater's smooth shading) And on an object whose mesh has vertex normals, `normal` becomes the one the
// game lights it with - its vertices' blended at the hit, in the world - in place of the depth's: the depth's normal
// is its triangle's, flat, and the rays' hemisphere tilted with each triangle, so the ground and a wall beside him
// took a different share of each (his shirt, face and arms in hard light and dark facets in the shade). Turned to
// the side the depth's faces (a mesh whose normals point inward).
// (added for the characters - rt_ao_characters) on_character: the object the pixel shows is skinned (the skater, a
// ped - rt_scene.h kSkinned): the occlusion's own strength for them (pc.extra.z, per mille).
const uint OBJECTS_MASK = 0x02u;
const uint SKINNED_FLAG = 0x08u;
bool on_character = false;
uint rays_mask(vec3 here, vec3 to_camera, float distance, inout vec3 normal)
{
    uint mask = uint(pc.extra.y) & 0xFFu;
    if ((mask & OBJECTS_MASK) == 0u) {
        return mask;
    }
    rayQueryEXT query;
    rayQueryInitializeEXT(query, u_scene, gl_RayFlagsOpaqueEXT, OBJECTS_MASK, pc.camera.xyz, 0.0,
                          -to_camera / max(distance, 1e-6), distance * 1.02 + 2.0);
    while (rayQueryProceedEXT(query)) {
    }
    bool on_object = rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionTriangleEXT &&
                     rayQueryGetIntersectionTEXT(query, true) >= distance - (2.0 + 0.02 * distance);
    if (!on_object) {
        return mask;
    }
    on_character = (geometry_record(rayQueryGetIntersectionInstanceIdEXT(query, true)).instance_flags & SKINNED_FLAG) != 0u;
    vec3 vertex_normal;
    if (hit_normal(rayQueryGetIntersectionInstanceIdEXT(query, true),
                   rayQueryGetIntersectionPrimitiveIndexEXT(query, true),
                   rayQueryGetIntersectionBarycentricsEXT(query, true), vertex_normal)) {
        // (to the world by the inverse transpose: a scaled object's normals stay at right angles to its surface)
        mat3 to_world = mat3(rayQueryGetIntersectionObjectToWorldEXT(query, true));
        vec3 n = transpose(inverse(to_world)) * vertex_normal;
        float n_length = length(n);
        if (n_length > 1e-12 && !any(isnan(n))) {
            n /= n_length;
            normal = dot(n, normal) < 0.0 ? -n : n;
        }
    }
    return mask & ~OBJECTS_MASK;
}

// The rays' directions: the hemisphere around `normal` (an orthonormal basis without a branch on the normal's
// direction), cosine-weighted, ray i of `rays` this frame - a 2D R2 sequence advanced by the frame's index, turned per
// pixel by a hash of its place in the viewport.
struct Hemisphere {
    vec3 normal;
    vec3 across;
    vec3 up;
    vec2 turn;
};
Hemisphere hemisphere_of(vec3 normal, ivec2 local)
{
    Hemisphere h;
    float s = normal.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (s + normal.z);
    float b = normal.x * normal.y * a;
    h.normal = normal;
    h.across = vec3(1.0 + s * normal.x * normal.x * a, s * b, -s * normal.x);
    h.up = vec3(b, s + normal.y * normal.y * a, -normal.y);
    uint seed = hash(uint(local.x) + hash(uint(local.y)));
    h.turn = vec2(float(hash(seed) >> 8u), float(hash(seed + 1u) >> 8u)) * (1.0 / 16777216.0);
    return h;
}
vec3 hemisphere_ray(Hemisphere h, uint i, uint rays)
{
    uint n = (uint(pc.extra.x) * rays + i) & 4095u;
    vec2 u = fract(h.turn + float(n) * vec2(0.7548776662, 0.5698402910));
    float r = sqrt(u.x);
    float phi = 6.2831853 * u.y;
    return h.across * (r * cos(phi)) + h.up * (r * sin(phi)) + h.normal * sqrt(max(0.0, 1.0 - u.x));
}
)";

// The ambient occlusion's rays.
constexpr const char* kAoTraceBody = R"(
float occluded(vec3 origin, vec3 direction, uint mask)
{
    rayQueryEXT query;
    rayQueryInitializeEXT(query, u_scene, gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT, mask, origin, 0.0,
                          direction, pc.params.x);
    while (rayQueryProceedEXT(query)) {
    }
    return rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionNoneEXT ? 0.0 : 1.0;
}

void main()
{
    ivec2 local = ivec2(gl_GlobalInvocationID.xy);
    if (!inside(local)) {
        return;
    }
    ivec2 p = pc.rect.xy + local;
    float d = depth_at(p);
    if (d >= 1.0) {
        imageStore(o_out, local, vec4(0.0));
        imageStore(o_guide, local, uvec4(0u));
        return;
    }
    vec3 here = world_at(p, d);
    vec3 to_camera = pc.camera.xyz - here;
    float distance = length(to_camera);
    vec3 normal = surface_normal(p, d, here, to_camera, distance);
    uint mask = rays_mask(here, to_camera, distance, normal);
    vec3 origin = here + normal * start_offset(distance);
    Hemisphere h = hemisphere_of(normal, local);
    uint rays = uint(pc.params.y);
    float hits = 0.0;
    for (uint i = 0u; i < rays; ++i) {
        hits += occluded(origin, hemisphere_ray(h, i, rays), mask);
    }
    if (on_character && pc.extra.z != 1000) {
        hits *= float(pc.extra.z) * 0.001; // (added for the characters: rt_ao_characters)
    }
    imageStore(o_out, local, vec4(hits / float(rays), 0.0, 0.0, 0.0));
    imageStore(o_guide, local, uvec4(floatBitsToUint(here), pack_normal(normal)));
}
)";

// The indirect light's rays (rt_gi). The same frame and directions as the occlusion's; set 1 is the material table (R3),
// binding 7 the frame's geometry table (R2), 8 the surface's own colour written, 9 the lights. params: x the rays' length.
// Per pixel: the surface's albedo (a ray from the camera finds its triangle - kept only where it ends at the depth's
// point - and its material is read at the hit's texture coordinates, at the level the pixel's footprint asks for: a 0
// when it can't be found), and the light arriving over the hemisphere (rgb: the mean of the rays' radiance). A ray's
// radiance: what it hits, its albedo x (the ambient light + the sun x N.L where a ray toward the sun is clear), or the
// sky's light when it hits nothing. Alpha-tested surfaces are tested along the bounce rays; the sun's rays see the
// opaque world and objects only.
constexpr const char* kGiTraceBody = R"(
layout(set = 0, binding = 8, rgba16f) uniform writeonly image2D o_albedo;
layout(set = 0, binding = 9, std140) uniform Lights {
    vec4 to_sun;  // xyz toward the sun; w 1 when there is one
    vec4 sun;     // its colour
    vec4 sky;     // what a ray that hits nothing brings
    vec4 ambient; // the light every hit surface gets
    vec4 shape;   // x how wide a bounce ray's footprint grows per unit (radians), y the sun rays' length, z their mask
} lights;

// The texture's level for a footprint `width` world units across on the triangle (its texels per world unit from its
// texture coordinates' area against its own).
float level_for(uint id, HitVertices v, vec3 a, vec3 b, vec3 c, float width)
{
    float world_area = length(cross(b - a, c - a));
    vec2 du = v.uv[1] - v.uv[0];
    vec2 dv = v.uv[2] - v.uv[0];
    float uv_area = abs(du.x * dv.y - du.y * dv.x);
    if (world_area <= 1e-12 || uv_area <= 1e-12 || width <= 0.0) {
        return 0.0;
    }
    ivec2 size = textureSize(sampler2D(material_textures[nonuniformEXT(material(id).texture_slot)], material_samplers[0]), 0);
    float texels = sqrt(uv_area * float(size.x) * float(size.y) / world_area);
    return max(0.0, log2(max(width * texels, 1e-6)));
}

// A hit's albedo: its material's texture at the hit's texture coordinates (the level for `width`) x the material's
// colour, at most 1 (a material's colour is doubled: 2 is an overbright pass, not a surface giving back more light than
// it gets) - the vertex colours (the level's baked light) left out; its triangle's normal facing `facing`.
vec3 hit_albedo(uint id, uint instance, uint primitive, vec2 barycentrics, mat4x3 to_world, float width, vec3 facing,
                out vec3 n)
{
    HitVertices v;
    n = facing;
    if (!hit_vertices(instance, primitive, v)) {
        return min(material(id).colour.rgb, vec3(1.0));
    }
    vec3 a = to_world * vec4(v.position[0], 1.0);
    vec3 b = to_world * vec4(v.position[1], 1.0);
    vec3 c = to_world * vec4(v.position[2], 1.0);
    vec3 cn = cross(b - a, c - a);
    if (length(cn) > 0.0) {
        n = normalize(cn);
        if (dot(n, facing) < 0.0) {
            n = -n;
        }
    }
    vec2 uv = hit_uv(v, barycentrics);
    return min((material_texel_lod(id, uv, level_for(id, v, a, b, c, width)) * material(id).colour).rgb, vec3(1.0));
}

bool sun_blocked(vec3 origin)
{
    rayQueryEXT query;
    rayQueryInitializeEXT(query, u_scene, gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT,
                          uint(lights.shape.z) & 0xFFu, origin, 0.0, lights.to_sun.xyz, lights.shape.y);
    while (rayQueryProceedEXT(query)) {
    }
    return rayQueryGetIntersectionTypeEXT(query, true) != gl_RayQueryCommittedIntersectionNoneEXT;
}

// The light a bounce ray brings back to `origin` (travelled: the camera's distance to it, for the start offsets).
vec3 incoming(vec3 origin, vec3 direction, float travelled, uint mask)
{
    rayQueryEXT query;
    rayQueryInitializeEXT(query, u_scene, gl_RayFlagsNoneEXT, mask, origin, 0.0, direction, pc.params.x);
    while (rayQueryProceedEXT(query)) {
        if (rayQueryGetIntersectionTypeEXT(query, false) == gl_RayQueryCandidateIntersectionTriangleEXT) {
            uint id = rayQueryGetIntersectionInstanceCustomIndexEXT(query, false);
            HitVertices candidate;
            bool keep = true;
            if (hit_vertices(rayQueryGetIntersectionInstanceIdEXT(query, false),
                             rayQueryGetIntersectionPrimitiveIndexEXT(query, false), candidate)) {
                keep = material_alpha_passes_at(id, hit_uv(candidate, rayQueryGetIntersectionBarycentricsEXT(query, false)));
            }
            if (keep) {
                rayQueryConfirmIntersectionEXT(query);
            }
        }
    }
    if (rayQueryGetIntersectionTypeEXT(query, true) != gl_RayQueryCommittedIntersectionTriangleEXT) {
        return lights.sky.rgb;
    }
    float t = rayQueryGetIntersectionTEXT(query, true);
    vec3 n;
    vec3 albedo = hit_albedo(rayQueryGetIntersectionInstanceCustomIndexEXT(query, true),
                             rayQueryGetIntersectionInstanceIdEXT(query, true),
                             rayQueryGetIntersectionPrimitiveIndexEXT(query, true),
                             rayQueryGetIntersectionBarycentricsEXT(query, true),
                             rayQueryGetIntersectionObjectToWorldEXT(query, true), lights.shape.x * t, -direction, n);
    vec3 light = lights.ambient.rgb;
    float facing = dot(n, lights.to_sun.xyz);
    if (lights.to_sun.w > 0.0 && facing > 0.0) {
        vec3 point = origin + direction * t;
        if (!sun_blocked(point + n * start_offset(travelled + t))) {
            light += lights.sun.rgb * facing;
        }
    }
    return albedo * light;
}

// The pixel's own albedo (a 1 when found, else 0).
vec4 surface_albedo(vec3 here, float distance, float footprint)
{
    vec3 eye = pc.camera.xyz;
    vec3 view = (here - eye) / max(distance, 1e-6);
    rayQueryEXT query;
    rayQueryInitializeEXT(query, u_scene, gl_RayFlagsOpaqueEXT, uint(pc.extra.y) & 0xFFu, eye, 0.0, view,
                          distance * 1.05 + 1.0);
    while (rayQueryProceedEXT(query)) {
    }
    if (rayQueryGetIntersectionTypeEXT(query, true) != gl_RayQueryCommittedIntersectionTriangleEXT) {
        return vec4(0.0);
    }
    float t = rayQueryGetIntersectionTEXT(query, true);
    if (abs(t - distance) > 1.0 + 0.02 * distance) {
        return vec4(0.0);
    }
    vec3 n;
    vec3 albedo = hit_albedo(rayQueryGetIntersectionInstanceCustomIndexEXT(query, true),
                             rayQueryGetIntersectionInstanceIdEXT(query, true),
                             rayQueryGetIntersectionPrimitiveIndexEXT(query, true),
                             rayQueryGetIntersectionBarycentricsEXT(query, true),
                             rayQueryGetIntersectionObjectToWorldEXT(query, true), footprint, -view, n);
    return vec4(albedo, 1.0);
}

void main()
{
    ivec2 local = ivec2(gl_GlobalInvocationID.xy);
    if (!inside(local)) {
        return;
    }
    ivec2 p = pc.rect.xy + local;
    float d = depth_at(p);
    if (d >= 1.0) {
        imageStore(o_out, local, vec4(0.0));
        imageStore(o_guide, local, uvec4(0u));
        imageStore(o_albedo, local, vec4(0.0));
        return;
    }
    vec3 here = world_at(p, d);
    vec3 to_camera = pc.camera.xyz - here;
    float distance = length(to_camera);
    vec3 normal = surface_normal(p, d, here, to_camera, distance);
    uint mask = rays_mask(here, to_camera, distance, normal);
    vec3 origin = here + normal * start_offset(distance);
    float footprint = length(world_at(p + ivec2(1, 0), d) - here);
    vec4 albedo = surface_albedo(here, distance, footprint);
    Hemisphere h = hemisphere_of(normal, local);
    uint rays = uint(pc.params.y);
    vec3 sum = vec3(0.0);
    for (uint i = 0u; i < rays; ++i) {
        sum += incoming(origin, hemisphere_ray(h, i, rays), distance, mask);
    }
    vec3 light = sum / float(rays);
    if (any(isnan(light)) || any(isinf(light))) {
        light = vec3(0.0);
    }
    imageStore(o_out, local, vec4(light, 0.0));
    imageStore(o_guide, local, uvec4(floatBitsToUint(here), pack_normal(normal)));
    imageStore(o_albedo, local, albedo);
}
)";

// Step 2: the history. matrix: world -> the previous frame's pixel (the target's, homogeneous); camera: the camera's
// position; params: x the longest history, y whether there is one (1), z rays, w the most one ray can bring (the
// occlusion's 1; the indirect light's brightest).
constexpr const char* kTemporalBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 2, rgba16f) uniform writeonly image2D o_out;
layout(set = 0, binding = 3, rgba32ui) uniform readonly uimage2D u_guide;
layout(set = 0, binding = 4, rgba16f) uniform readonly image2D u_in;
layout(set = 0, binding = 5, rgba32ui) uniform readonly uimage2D u_guide_before;
layout(set = 0, binding = 6, rgba16f) uniform readonly image2D u_history;

void main()
{
    ivec2 local = ivec2(gl_GlobalInvocationID.xy);
    if (!inside(local)) {
        return;
    }
    uvec4 guide = imageLoad(u_guide, local);
    Value now = value_of(imageLoad(u_in, local));
    if (guide.w == 0u) {
        imageStore(o_out, local, vec4(0.0));
        return;
    }
    vec3 here = uintBitsToFloat(guide.xyz);
    vec3 normal = unpack_normal(guide.w);
    float tolerance = plane_tolerance(length(pc.camera.xyz - here));
    // this frame's spread around the pixel (its surface's neighbours): what the history may be
    Value sum = Value(0.0);
    Value squares = Value(0.0);
    float count = 0.0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            ivec2 q = local + ivec2(dx, dy);
            if (!inside(q) || imageLoad(u_guide, q).w == 0u) {
                continue;
            }
            Value v = value_of(imageLoad(u_in, q));
            sum += v;
            squares += v * v;
            count += 1.0;
        }
    }
    Value mean = sum / count;
    Value spread = sqrt(max(Value(0.0), squares / count - mean * mean));
    float noise = pc.params.w * 0.5 / sqrt(max(1.0, pc.params.z * count));
    Value low = mean - 1.5 * spread - noise;
    Value high = mean + 1.5 * spread + noise;
    Value history = Value(0.0);
    float history_length = 0.0;
    if (pc.params.y > 0.5) {
        vec4 before = pc.matrix * vec4(here, 1.0);
        if (before.w > 0.0) {
            vec2 at = before.xy / before.w - vec2(pc.rect.xy);
            ivec2 base = ivec2(floor(at));
            vec2 f = at - vec2(base);
            float weights = 0.0;
            Value values = Value(0.0);
            float lengths = 0.0;
            for (int corner = 0; corner < 4; ++corner) {
                ivec2 q = base + ivec2(corner & 1, corner >> 1);
                float w = ((corner & 1) != 0 ? f.x : 1.0 - f.x) * ((corner >> 1) != 0 ? f.y : 1.0 - f.y);
                if (w <= 0.0 || !inside(q)) {
                    continue;
                }
                uvec4 was = imageLoad(u_guide_before, q);
                if (was.w == 0u) {
                    continue;
                }
                vec3 there = uintBitsToFloat(was.xyz);
                if (abs(dot(normal, there - here)) > tolerance || dot(normal, unpack_normal(was.w)) < 0.9) {
                    continue;
                }
                vec4 kept = imageLoad(u_history, q);
                weights += w;
                values += w * value_of(kept);
                lengths += w * length_of(kept);
            }
            if (weights > 1e-3) {
                history = values / weights;
                history_length = lengths / weights;
            }
        }
    }
    history = clamp(history, low, high);
    history_length = min(floor(history_length + 0.5) + 1.0, pc.params.x);
    Value result = kept_in_range(mix(history, now, 1.0 / history_length));
    imageStore(o_out, local, packed(result, history_length));
}
)";

// Step 3: one a-trous pass. camera: the camera's position; params: z rays, w the most one ray can bring; extra: x the
// step (pixels).
constexpr const char* kFilterBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 2, rgba16f) uniform writeonly image2D o_out;
layout(set = 0, binding = 3, rgba32ui) uniform readonly uimage2D u_guide;
layout(set = 0, binding = 4, rgba16f) uniform readonly image2D u_in;

const float kKernel[3] = float[3](0.375, 0.25, 0.0625);

void main()
{
    ivec2 local = ivec2(gl_GlobalInvocationID.xy);
    if (!inside(local)) {
        return;
    }
    uvec4 guide = imageLoad(u_guide, local);
    vec4 centre = imageLoad(u_in, local);
    if (guide.w == 0u) {
        imageStore(o_out, local, centre);
        return;
    }
    vec3 here = uintBitsToFloat(guide.xyz);
    vec3 normal = unpack_normal(guide.w);
    float tolerance = plane_tolerance(length(pc.camera.xyz - here));
    // the noise a history this long leaves (a share of rays: at most 0.5 / sqrt(rays) a frame), with a margin
    float noise = pc.params.w * (2.0 * 0.5 / sqrt(max(1.0, max(length_of(centre), 1.0) * pc.params.z)) + 1e-3);
    float weights = 0.0;
    Value sum = Value(0.0);
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            ivec2 q = local + ivec2(dx, dy) * pc.extra.x;
            if (!inside(q)) {
                continue;
            }
            uvec4 there_guide = imageLoad(u_guide, q);
            if (there_guide.w == 0u) {
                continue;
            }
            Value value = value_of(imageLoad(u_in, q));
            vec3 there = uintBitsToFloat(there_guide.xyz);
            float w = kKernel[abs(dx)] * kKernel[abs(dy)];
            w *= pow(max(0.0, dot(normal, unpack_normal(there_guide.w))), 32.0);
            w *= exp(-abs(dot(normal, there - here)) / tolerance);
            w *= exp(-difference(value, value_of(centre)) / noise);
            weights += w;
            sum += w * value;
        }
    }
    imageStore(o_out, local, packed(weights > 0.0 ? sum / weights : value_of(centre), length_of(centre)));
}
)";

// Step 4: the colour multiplied (a multiplying blend). rect: the viewport; params: x the strength.
constexpr const char* kApplyBody = R"(
layout(set = 0, binding = 4, rgba16f) uniform readonly image2D u_in;
layout(location = 0) out vec4 o_factor;

void main()
{
    ivec2 local = ivec2(gl_FragCoord.xy) - pc.rect.xy;
    float ao = inside(local) ? clamp(imageLoad(u_in, local).r, 0.0, 1.0) : 0.0;
    o_factor = vec4(vec3(1.0 - pc.params.x * ao), 1.0);
}
)";

// The indirect light's step 4: what is added to the colour - the strength x the surface's albedo x the light arriving
// beyond the sky's (what an open sky would give, which the level's baked lighting already has); the blend then scales
// it by what the colour leaves below white (1 - colour; a float target: added as it is). rect: the viewport; params: x
// the strength, yzw the sky's light. Pixels whose albedo wasn't found are left alone.
constexpr const char* kGiApplyBody = R"(
layout(set = 0, binding = 4, rgba16f) uniform readonly image2D u_in;
layout(set = 0, binding = 8, rgba16f) uniform readonly image2D u_albedo;
layout(location = 0) out vec4 o_added;

void main()
{
    ivec2 local = ivec2(gl_FragCoord.xy) - pc.rect.xy;
    if (!inside(local)) {
        discard;
    }
    vec4 albedo = imageLoad(u_albedo, local);
    if (!(albedo.a > 0.0)) {
        discard;
    }
    vec3 beyond = max(imageLoad(u_in, local).rgb - pc.params.yzw, vec3(0.0));
    vec3 added = pc.params.x * clamp(albedo.rgb, 0.0, 1.0) * beyond;
    if (any(isnan(added)) || any(isinf(added))) {
        discard;
    }
    o_added = vec4(added, 0.0);
}
)";

// The push constants, as the shaders' block lays them out.
struct PassConstants {
    std::array<float, kMatrixFloats> matrix{}; // column-major (GLSL's mat4)
    std::array<int32_t, 4> rect{};
    std::array<float, 4> camera{};
    std::array<float, 4> params{};
    std::array<int32_t, 4> extra{};
};
static_assert(sizeof(PassConstants) == kPushConstantBytes, "Vulkan's guaranteed push-constant space");

constexpr VkShaderStageFlags kStages = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

// The bindings: 0 the scene, 1 the depth, 2 - 6 storage images (2 written; 3 the guide - written by the rays, read
// after; 4 the input; 5 the guide before; 6 the history before).
constexpr uint32_t kBindingScene = 0;
constexpr uint32_t kBindingDepth = 1;
constexpr uint32_t kBindingOut = 2;
constexpr uint32_t kBindingGuide = 3;
constexpr uint32_t kBindingIn = 4;
constexpr uint32_t kBindingGuideBefore = 5;
constexpr uint32_t kBindingHistory = 6;
// (the indirect light's own: 7 the frame's geometry table, 8 the surfaces' albedo, 9 the lights; set 1 the materials)
constexpr uint32_t kBindingGeometry = 7;
constexpr uint32_t kBindingAlbedo = 8;
constexpr uint32_t kBindingLights = 9;
constexpr uint32_t kGiBindingCount = 10;
// (the occlusion's: 0 - 6, and 7 the geometry table - the objects' vertex normals, added for the skater's smooth
// shading)
constexpr uint32_t kAoBindingCount = kBindingGeometry + 1;
constexpr uint32_t kMaterialSet = 1;
constexpr VkDeviceSize kUniformAlignment = 256; // (at least any device's minUniformBufferOffsetAlignment)

// The indirect light's lights block (std140), as the shader lays it out.
struct LightsBlock {
    std::array<float, 4> to_sun{};
    std::array<float, 4> sun{};
    std::array<float, 4> sky{};
    std::array<float, 4> ambient{};
    std::array<float, 4> shape{};
};
constexpr std::size_t kLightsBlockBytes = 80;
static_assert(sizeof(LightsBlock) == kLightsBlockBytes, "the shader's Lights block (std140)");

// Which effect a state is.
enum class Effect : uint8_t { kOcclusion, kIndirect };

// A pipeline for a colour format and sample count (the apply pass).
struct ApplyPipeline {
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkPipeline pipeline = VK_NULL_HANDLE;
};

using Matrix = std::array<std::array<double, kMatrixSide>, kMatrixSide>;

constexpr std::size_t kSlotImages = 8; // (a viewport's: two guides, the rays', two histories, two filtered, the albedo)

// A viewport's images and history.
struct ViewSlot {
    std::array<uint32_t, 4> rect{};
    Extent target;
    std::array<Image, 2> guides;    // this frame's and the one before (by `current`)
    Image raw;                      // step 1
    std::array<Image, 2> histories; // step 2's, this frame's and the one before
    std::array<Image, 2> filtered;  // step 3's ping-pong
    Image albedo;                   // the indirect light's step 1: the surfaces' own colour (else not made)
    uint32_t current = 0;           // which of guides / histories this frame writes
    bool fresh = true;              // made, not cleared yet
    bool have_before = false;       // a frame before whose history and camera can be used
    uint64_t frame_drawn = 0;       // Backend::frame when last drawn
    uint64_t passes = 0;            // the frame index of the directions
    Matrix world_to_pixel{};        // the camera of the frame before
    HistoryCamera camera;           // (added by the first frame) ... as a cut is told by (vulkan_rt_history.h)
    Image* applied = nullptr;       // what the last apply read
    Image* history_written = nullptr;
    uint64_t last_use = 0;
};

// How long an effect's passes take on the device (timestamps before the barrier and after the apply, read back a few
// passes later without waiting; logged every kTimingReport passes measured).
inline constexpr uint32_t kTimingSlots = 16;
inline constexpr uint32_t kTimingReport = 600;
struct PassTiming {
    VkQueryPool pool = VK_NULL_HANDLE;
    std::array<uint64_t, kTimingSlots> pending{}; // the submission that wrote a slot's pair (0: none)
    double period_ns = 0.0; // a tick
    double sum_ms = 0.0;
    uint32_t measured = 0;
    bool unavailable = false; // no timestamps on this device (not tried again)
};

// An effect's passes and histories: the ambient occlusion's (effects()) and the indirect light's (indirect()) - the
// same denoiser, over one value or a colour.
struct EffectsState {
    explicit EffectsState(Effect which) : effect(which) {}
    Effect effect;
    RtAoSettings settings;   // (the occlusion's)
    RtGiSettings gi;         // (the indirect light's)
    VkAccelerationStructureKHR scene = VK_NULL_HANDLE; // (the occlusion's: the indirect light reads current_scene)
    VkBuffer geometry = VK_NULL_HANDLE;                // (the frame's geometry table: the objects' vertex normals, the
                                                       // indirect light's hits)
    VkDeviceSize geometry_bytes = 0;
    // (the occlusion's, added for the skater's smooth shading) A table of one record, nothing live, for a TLAS handed
    // over without one (rt_effects_set_scene by a caller other than the scene's builder): every pixel keeps the depth's
    // normal
    DeviceBuffer no_geometry;
    PassTiming timing;
    uint64_t sun_logged = 0; // (the indirect light's: the baked sun's fit last logged)
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE; // (the indirect light's: + the material table's set 1)
    VkDescriptorSetLayout material_layout = VK_NULL_HANDLE; // the table's when `layout` was made
    VkSampler sampler = VK_NULL_HANDLE;
    VkShaderModule vertex = VK_NULL_HANDLE;
    std::array<VkPipeline, 2> trace{}; // single-sampled depth, multisampled
    VkPipeline temporal = VK_NULL_HANDLE;
    VkPipeline filter = VK_NULL_HANDLE;
    std::vector<ApplyPipeline> applies;
    std::vector<std::unique_ptr<ViewSlot>> slots;
    bool standard_samples = false; // the device puts multisampled targets' samples where Vulkan's standard says // (kept where they are: the slots' images are pointed at)
    bool failed = false; // the shaders or layouts couldn't be made (not tried again)
    uint64_t passes = 0;
    uint64_t uses = 0;
};

EffectsState& effects()
{
    static EffectsState state(Effect::kOcclusion);
    return state;
}

EffectsState& indirect()
{
    static EffectsState state(Effect::kIndirect);
    return state;
}

bool is_indirect(const EffectsState& own) { return own.effect == Effect::kIndirect; }
bool effect_on(const EffectsState& own) { return is_indirect(own) ? own.gi.on : own.settings.on; }
// (NOT ORIGINAL either: with DLSS Ray Reconstruction denoising the frame - vulkan_upscale.h - each frame's rays go
// through as they are)
bool effect_denoised(const EffectsState& own)
{
    return (is_indirect(own) ? own.gi.denoise : own.settings.denoise) && !ray_reconstruction_active();
}
unsigned effect_rays(const EffectsState& own) { return is_indirect(own) ? own.gi.rays : own.settings.rays; }
const char* effect_name(const EffectsState& own) { return is_indirect(own) ? "rt_gi" : "rt_ao"; }

bool first_report(Backend& state, uint64_t what) { return state.reported.insert(what).second; }

// The listener rt_scene tells when a view's opaque world is drawn.
class EffectsListener final : public rt::ViewListener {
public:
    [[nodiscard]] int order() const override { return rt::ViewOrder::kOcclusion; }
    void opaque_drawn(const rt::ViewDrawn& view) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (state.device_made && state.draws_made) {
            record_rt_ao_pass(state, view);
        }
    }
};

EffectsListener& listener()
{
    static EffectsListener instance;
    return instance;
}

// The indirect light's listener: after the shadows and the occlusion (they darken what the bounce then brightens).
class IndirectListener final : public rt::ViewListener {
public:
    [[nodiscard]] int order() const override { return rt::ViewOrder::kIndirect; }
    void opaque_drawn(const rt::ViewDrawn& view) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (state.device_made && state.draws_made) {
            record_rt_gi_pass(state, view);
        }
    }
};

IndirectListener& indirect_listener()
{
    static IndirectListener instance;
    return instance;
}

VkShaderModule make_module(VkDevice device, RtShaderStage stage, const std::string& source, std::string* error)
{
    const CompiledShader compiled = compile_rt_glsl(stage, source);
    if (!compiled.ok) {
        *error = compiled.log;
        return VK_NULL_HANDLE;
    }
    const VkShaderModuleCreateInfo info{.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                        .pNext = nullptr,
                                        .flags = 0,
                                        .codeSize = compiled.spirv.size() * sizeof(uint32_t),
                                        .pCode = compiled.spirv.data()};
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) {
        *error = "vkCreateShaderModule failed";
        return VK_NULL_HANDLE;
    }
    return module;
}

// A pass's GLSL: the version, the defines, the ray-query extension where it traces (the indirect light's: the material
// and geometry tables too), what all share, what the rays share, its body. `rgb`: the indirect light's (VALUE_RGB).
std::string source_of(const char* body, bool traces, bool multisampled, bool rgb = false)
{
    std::string source = "#version 460\n";
    if (multisampled) {
        source += "#define MULTISAMPLED\n";
    }
    if (rgb) {
        source += "#define VALUE_RGB\n";
    }
    if (traces) {
        source += "#extension GL_EXT_ray_query : require\n";
        if (rgb) {
            source += "#define THUG2_MATERIAL_SET 1\n";
            source += material_glsl();
        }
        // (both effects' rays: the occlusion's read the objects' vertex normals too)
        source += "#define THUG2_GEOMETRY_SET 0\n#define THUG2_GEOMETRY_BINDING 7\n";
        source += scene_geometry_glsl();
    }
    source += kCommon;
    if (traces) {
        source += kTraceShared;
    }
    source += body;
    return source;
}

VkPipeline make_compute(VkDevice device, VkPipelineLayout layout, const std::string& source, std::string* error)
{
    const VkShaderModule module = make_module(device, RtShaderStage::kCompute, source, error);
    if (module == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
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
                                           .layout = layout,
                                           .basePipelineHandle = VK_NULL_HANDLE,
                                           .basePipelineIndex = -1};
    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult made = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
    vkDestroyShaderModule(device, module, nullptr);
    if (made != VK_SUCCESS) {
        *error = "vkCreateComputePipelines failed";
        return VK_NULL_HANDLE;
    }
    return pipeline;
}

// The layouts, the sampler, the vertex shader and the compute pipelines (once).
bool ensure_objects(Backend& state, EffectsState& own, std::string* error)
{
    if (own.filter != VK_NULL_HANDLE) {
        return true;
    }
    const VkDevice device = state.context.device();
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(state.context.physical_device(), &properties);
    own.standard_samples = properties.limits.standardSampleLocations == VK_TRUE;
    const bool rgb = is_indirect(own);
    if (own.layout == VK_NULL_HANDLE) {
        const uint32_t count = rgb ? kGiBindingCount : kAoBindingCount;
        std::array<VkDescriptorSetLayoutBinding, kGiBindingCount> bindings{};
        for (uint32_t binding = 0; binding < count; ++binding) {
            VkDescriptorType type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            if (binding == kBindingScene) {
                type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            } else if (binding == kBindingDepth) {
                type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            } else if (binding == kBindingGeometry) {
                type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            } else if (binding == kBindingLights) {
                type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            }
            core::at(bindings, binding) = {.binding = binding,
                                           .descriptorType = type,
                                           .descriptorCount = 1,
                                           .stageFlags = kStages,
                                           .pImmutableSamplers = nullptr};
        }
        const VkDescriptorSetLayoutCreateInfo set_info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                       .pNext = nullptr,
                                                       .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
                                                       .bindingCount = count,
                                                       .pBindings = bindings.data()};
        const VkPushConstantRange constants{.stageFlags = kStages, .offset = 0, .size = sizeof(PassConstants)};
        VkSamplerCreateInfo sampler_info{};
        sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_info.magFilter = VK_FILTER_NEAREST;
        sampler_info.minFilter = VK_FILTER_NEAREST;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.maxLod = 0.0F;
        if (vkCreateDescriptorSetLayout(device, &set_info, nullptr, &own.set_layout) != VK_SUCCESS ||
            vkCreateSampler(device, &sampler_info, nullptr, &own.sampler) != VK_SUCCESS) {
            *error = "the descriptor layout or the sampler";
            return false;
        }
        own.material_layout = rgb ? state.materials.layout() : VK_NULL_HANDLE;
        const std::array<VkDescriptorSetLayout, 2> sets{own.set_layout, own.material_layout};
        const VkPipelineLayoutCreateInfo layout_info{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                     .pNext = nullptr,
                                                     .flags = 0,
                                                     .setLayoutCount = rgb ? 2U : 1U,
                                                     .pSetLayouts = sets.data(),
                                                     .pushConstantRangeCount = 1,
                                                     .pPushConstantRanges = &constants};
        if (vkCreatePipelineLayout(device, &layout_info, nullptr, &own.layout) != VK_SUCCESS) {
            own.layout = VK_NULL_HANDLE;
            *error = "the pipeline layout";
            return false;
        }
    }
    if (own.vertex == VK_NULL_HANDLE) {
        own.vertex = make_module(device, RtShaderStage::kVertex, kVertexSource, error);
        if (own.vertex == VK_NULL_HANDLE) {
            return false;
        }
    }
    for (std::size_t multisampled = 0; multisampled < own.trace.size(); ++multisampled) {
        if (core::at(own.trace, multisampled) == VK_NULL_HANDLE) {
            core::at(own.trace, multisampled) = make_compute(
                device, own.layout, source_of(rgb ? kGiTraceBody : kAoTraceBody, true, multisampled != 0, rgb), error);
            if (core::at(own.trace, multisampled) == VK_NULL_HANDLE) {
                return false;
            }
        }
    }
    if (own.temporal == VK_NULL_HANDLE) {
        own.temporal = make_compute(device, own.layout, source_of(kTemporalBody, false, false, rgb), error);
        if (own.temporal == VK_NULL_HANDLE) {
            return false;
        }
    }
    own.filter = make_compute(device, own.layout, source_of(kFilterBody, false, false, rgb), error);
    return own.filter != VK_NULL_HANDLE;
}

// Whether a colour format holds values above 1 (a float target: the indirect light is added, not screened).
bool float_format(VkFormat format)
{
    switch (format) {
    case VK_FORMAT_R16G16B16A16_SFLOAT:
    case VK_FORMAT_R32G32B32A32_SFLOAT:
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
    case VK_FORMAT_R16G16B16_SFLOAT:
    case VK_FORMAT_R32G32B32_SFLOAT:
        return true;
    default:
        return false;
    }
}

// The apply pass's pipeline for a colour target (made on first use).
VkPipeline apply_pipeline(Backend& state, EffectsState& own, VkFormat format, VkSampleCountFlagBits samples,
                          std::string* error)
{
    for (const ApplyPipeline& made : own.applies) {
        if (made.format == format && made.samples == samples) {
            return made.pipeline;
        }
    }
    const bool rgb = is_indirect(own);
    const VkDevice device = state.context.device();
    const VkShaderModule fragment =
        make_module(device, RtShaderStage::kFragment, source_of(rgb ? kGiApplyBody : kApplyBody, false, false, rgb),
                    error);
    if (fragment == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }
    const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .pNext = nullptr,
         .flags = 0,
         .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = own.vertex,
         .pName = "main",
         .pSpecializationInfo = nullptr},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .pNext = nullptr,
         .flags = 0,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = fragment,
         .pName = "main",
         .pSpecializationInfo = nullptr},
    }};
    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0F;
    const VkPipelineMultisampleStateCreateInfo multisample{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .rasterizationSamples = samples,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 0.0F,
        .pSampleMask = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE};
    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    // the occlusion: colour x the factor - (dst x src) + (dst x 0); the indirect light: colour + what is added x (1 -
    // colour) - a screen, which never takes the colour past white - or, on a float target, + what is added; alpha kept
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    if (rgb) {
        blend.srcColorBlendFactor = float_format(format) ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    }
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
    VkPipelineColorBlendStateCreateInfo blending{};
    blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blending.attachmentCount = 1;
    blending.pAttachments = &blend;
    const std::array<VkDynamicState, 2> dynamic_states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
    dynamic.pDynamicStates = dynamic_states.data();
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &format;
    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pNext = &rendering;
    info.stageCount = static_cast<uint32_t>(stages.size());
    info.pStages = stages.data();
    info.pVertexInputState = &vertex_input;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blending;
    info.pDynamicState = &dynamic;
    info.layout = own.layout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult made = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
    vkDestroyShaderModule(device, fragment, nullptr);
    if (made != VK_SUCCESS) {
        *error = "vkCreateGraphicsPipelines failed";
        return VK_NULL_HANDLE;
    }
    own.applies.push_back({.format = format, .samples = samples, .pipeline = pipeline});
    return pipeline;
}

// The image a surface draws into (a texture's level 0, a render target's, a depth surface's).
Image* image_of_surface(Surface* surface)
{
    if (surface == nullptr) {
        return nullptr;
    }
    switch (surface->kind) {
    case SurfaceKind::kTextureLevel:
        return surface->level == 0 ? &surface->texture->image : nullptr;
    case SurfaceKind::kRenderTarget:
    case SurfaceKind::kDepth:
        return &surface->image;
    case SurfaceKind::kSystemMemory:
        break;
    }
    return nullptr;
}

// The inverse of a 4x4 matrix (Gauss-Jordan with partial pivoting); false when it is singular.
bool invert(Matrix matrix, Matrix* inverse)
{
    Matrix result{};
    for (std::size_t i = 0; i < kMatrixSide; ++i) {
        core::at(core::at(result, i), i) = 1.0;
    }
    for (std::size_t column = 0; column < kMatrixSide; ++column) {
        std::size_t pivot = column;
        for (std::size_t row = column + 1; row < kMatrixSide; ++row) {
            if (std::abs(core::at(core::at(matrix, row), column)) > std::abs(core::at(core::at(matrix, pivot), column))) {
                pivot = row;
            }
        }
        const double top = core::at(core::at(matrix, pivot), column);
        if (std::abs(top) < kSingular) {
            return false;
        }
        std::swap(core::at(matrix, pivot), core::at(matrix, column));
        std::swap(core::at(result, pivot), core::at(result, column));
        for (std::size_t k = 0; k < kMatrixSide; ++k) {
            core::at(core::at(matrix, column), k) /= top;
            core::at(core::at(result, column), k) /= top;
        }
        for (std::size_t row = 0; row < kMatrixSide; ++row) {
            if (row == column) {
                continue;
            }
            const double factor = core::at(core::at(matrix, row), column);
            for (std::size_t k = 0; k < kMatrixSide; ++k) {
                core::at(core::at(matrix, row), k) -= factor * core::at(core::at(matrix, column), k);
                core::at(core::at(result, row), k) -= factor * core::at(core::at(result, column), k);
            }
        }
    }
    *inverse = result;
    return true;
}

Matrix multiply(const Matrix& a, const Matrix& b)
{
    Matrix product{};
    for (std::size_t row = 0; row < kMatrixSide; ++row) {
        for (std::size_t column = 0; column < kMatrixSide; ++column) {
            double sum = 0.0;
            for (std::size_t k = 0; k < kMatrixSide; ++k) {
                sum += core::at(core::at(a, row), k) * core::at(core::at(b, k), column);
            }
            core::at(core::at(product, row), column) = sum;
        }
    }
    return product;
}

// The matrix (column vectors) that takes world space to (pixel x, pixel y, depth, 1) in the target's pixels: view x
// projection (their product transposed: Direct3D's row vectors), then clip space to Direct3D 9's pixels (centres at
// whole coordinates, y down) and the viewport's depth range. pixel_to_world is its inverse.
Matrix world_to_pixel(const rt::ViewDrawn& view)
{
    Matrix view_projection{}; // row vectors: clip = world x view x projection
    for (std::size_t row = 0; row < kMatrixSide; ++row) {
        for (std::size_t column = 0; column < kMatrixSide; ++column) {
            double sum = 0.0;
            for (std::size_t k = 0; k < kMatrixSide; ++k) {
                sum += static_cast<double>(core::at(core::at(view.camera.view, row), k)) *
                       static_cast<double>(core::at(core::at(view.camera.projection, k), column));
            }
            core::at(core::at(view_projection, row), column) = sum;
        }
    }
    Matrix clip_from_world{}; // column vectors
    for (std::size_t row = 0; row < kMatrixSide; ++row) {
        for (std::size_t column = 0; column < kMatrixSide; ++column) {
            core::at(core::at(clip_from_world, row), column) = core::at(core::at(view_projection, column), row);
        }
    }
    const double x = core::at(view.rect, 0);
    const double y = core::at(view.rect, 1);
    const double width = std::max(1U, core::at(view.rect, 2));
    const double height = std::max(1U, core::at(view.rect, 3));
    const double near_z = core::at(view.depth_range, 0);
    const double depth_span = std::max(1e-6, static_cast<double>(core::at(view.depth_range, 1)) - near_z);
    const Matrix pixel_from_clip{{{width / 2.0, 0.0, 0.0, x + (width / 2.0)},
                                  {0.0, -height / 2.0, 0.0, y + (height / 2.0)},
                                  {0.0, 0.0, depth_span, near_z},
                                  {0.0, 0.0, 0.0, 1.0}}};
    return multiply(pixel_from_clip, clip_from_world);
}

std::array<float, kMatrixFloats> column_major(const Matrix& matrix)
{
    std::array<float, kMatrixFloats> out{};
    for (std::size_t column = 0; column < kMatrixSide; ++column) {
        for (std::size_t row = 0; row < kMatrixSide; ++row) {
            core::at(out, (column * kMatrixSide) + row) = static_cast<float>(core::at(core::at(matrix, row), column));
        }
    }
    return out;
}

// --- the viewports' images ---------------------------------------------------------------------------------------

// The slot's images (the albedo where it was made).
std::vector<Image*> images_of(ViewSlot& slot)
{
    std::vector<Image*> images{&slot.guides.front(),    &slot.guides.back(),     &slot.raw,
                               &slot.histories.front(), &slot.histories.back(), &slot.filtered.front(),
                               &slot.filtered.back()};
    if (slot.albedo.image != VK_NULL_HANDLE) {
        images.push_back(&slot.albedo);
    }
    return images;
}

void release_slot(Backend& state, ViewSlot* slot, bool now)
{
    for (Image* image : images_of(*slot)) {
        if (now) {
            state.memory.destroy_image(image);
        } else {
            retire_image(state, image);
        }
    }
}

bool make_storage_image(Backend& state, VkFormat format, const Extent& extent, Image* out)
{
    const ImageRequest request{.format = format,
                               .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                               .extent = extent,
                               .levels = 1,
                               .samples = VK_SAMPLE_COUNT_1_BIT,
                               .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                               .alpha_one = false};
    if (!state.memory.create_image(request, out)) {
        return false;
    }
    out->resting = VK_IMAGE_LAYOUT_GENERAL; // (storage images: read and written in place)
    return true;
}

// The slot for a viewport (made, or made again when the size changed; the least recently used one dropped when all are
// taken); null when its images can't be made.
ViewSlot* slot_for(Backend& state, EffectsState& own, const std::array<uint32_t, 4>& rect, const Extent& target)
{
    ++own.uses;
    for (const std::unique_ptr<ViewSlot>& slot : own.slots) {
        if (slot->rect == rect && slot->target.width == target.width && slot->target.height == target.height) {
            slot->last_use = own.uses;
            return slot.get();
        }
    }
    if (own.slots.size() >= kAoViewSlots) {
        const auto oldest = std::ranges::min_element(
            own.slots, {}, [](const std::unique_ptr<ViewSlot>& slot) { return slot->last_use; });
        release_slot(state, oldest->get(), false);
        own.slots.erase(oldest);
    }
    auto made_slot = std::make_unique<ViewSlot>();
    ViewSlot& slot = *made_slot;
    slot.rect = rect;
    slot.target = target;
    slot.last_use = own.uses;
    const Extent extent{.width = core::at(rect, 2), .height = core::at(rect, 3)};
    bool made = true;
    for (Image& guide : slot.guides) {
        made = made && make_storage_image(state, VK_FORMAT_R32G32B32A32_UINT, extent, &guide);
    }
    for (Image* image : {&slot.raw, &slot.histories.front(), &slot.histories.back(), &slot.filtered.front(),
                         &slot.filtered.back()}) {
        made = made && make_storage_image(state, VK_FORMAT_R16G16B16A16_SFLOAT, extent, image);
    }
    if (is_indirect(own)) {
        made = made && make_storage_image(state, VK_FORMAT_R16G16B16A16_SFLOAT, extent, &slot.albedo);
    }
    if (!made) {
        release_slot(state, &slot, true);
        if (first_report(state, kReportImages + (is_indirect(own) ? kReportIndirect : 0U))) {
            get_log("vulkan: %s: the viewport's images (%ux%u) couldn't be made - the pass skipped there",
                    effect_name(own), extent.width, extent.height);
        }
        return nullptr;
    }
    own.slots.push_back(std::move(made_slot));
    return &slot;
}

// The new images moved to GENERAL and cleared (no NaN read from an unwritten history), before the passes.
void clear_slot(VkCommandBuffer commands, ViewSlot* slot)
{
    const std::vector<Image*> images = images_of(*slot);
    std::array<VkImageMemoryBarrier, kSlotImages> barriers{};
    for (std::size_t i = 0; i < images.size(); ++i) {
        core::at(barriers, i) = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                 .pNext = nullptr,
                                 .srcAccessMask = 0,
                                 .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                 .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                 .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                                 .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                 .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                 .image = core::at(images, i)->image,
                                 .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                      .baseMipLevel = 0,
                                                      .levelCount = 1,
                                                      .baseArrayLayer = 0,
                                                      .layerCount = 1}};
    }
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, static_cast<uint32_t>(images.size()), barriers.data());
    const VkImageSubresourceRange all{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                      .baseMipLevel = 0,
                                      .levelCount = 1,
                                      .baseArrayLayer = 0,
                                      .layerCount = 1};
    const VkClearColorValue zero{};
    for (Image* image : images) {
        vkCmdClearColorImage(commands, image->image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &all);
        image->layout = VK_IMAGE_LAYOUT_GENERAL;
    }
    const VkMemoryBarrier cleared{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                  .pNext = nullptr,
                                  .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                  .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &cleared, 0,
                         nullptr, 0, nullptr);
    slot->fresh = false;
}

// One pass's descriptors: the scene, the depth and the storage images it uses (null: not written).
struct PassImages {
    bool scene = false;
    const Image* depth = nullptr;
    const Image* out = nullptr;
    const Image* guide = nullptr;
    const Image* in = nullptr;
    const Image* guide_before = nullptr;
    const Image* history = nullptr;
    // (the indirect light's)
    const Image* albedo = nullptr;
    VkBuffer geometry = VK_NULL_HANDLE;
    VkDeviceSize geometry_bytes = 0;
    VkBuffer lights = VK_NULL_HANDLE;
    VkDeviceSize lights_offset = 0;
};

void push_descriptors(VkCommandBuffer commands, const EffectsState& own, VkPipelineBindPoint point,
                      const PassImages& images)
{
    const VkWriteDescriptorSetAccelerationStructureKHR scene{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
        .pNext = nullptr,
        .accelerationStructureCount = 1,
        .pAccelerationStructures = &own.scene};
    std::array<VkDescriptorImageInfo, kGiBindingCount> infos{};
    std::array<VkWriteDescriptorSet, kGiBindingCount> writes{};
    uint32_t count = 0;
    const auto add = [&](uint32_t binding, VkDescriptorType type) -> VkWriteDescriptorSet& {
        VkWriteDescriptorSet& write = core::at(writes, count++);
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstBinding = binding;
        write.descriptorCount = 1;
        write.descriptorType = type;
        return write;
    };
    if (images.scene) {
        add(kBindingScene, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR).pNext = &scene;
    }
    if (images.depth != nullptr) {
        VkDescriptorImageInfo& info = core::at(infos, kBindingDepth);
        info = {.sampler = own.sampler,
                .imageView = images.depth->view,
                .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        add(kBindingDepth, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER).pImageInfo = &info;
    }
    const std::array<std::pair<uint32_t, const Image*>, 6> storage{{{kBindingOut, images.out},
                                                                    {kBindingGuide, images.guide},
                                                                    {kBindingIn, images.in},
                                                                    {kBindingGuideBefore, images.guide_before},
                                                                    {kBindingHistory, images.history},
                                                                    {kBindingAlbedo, images.albedo}}};
    for (const auto& [binding, image] : storage) {
        if (image == nullptr) {
            continue;
        }
        VkDescriptorImageInfo& info = core::at(infos, binding);
        info = {.sampler = VK_NULL_HANDLE, .imageView = image->view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
        add(binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE).pImageInfo = &info;
    }
    const VkDescriptorBufferInfo geometry{.buffer = images.geometry, .offset = 0, .range = images.geometry_bytes};
    if (images.geometry != VK_NULL_HANDLE) {
        add(kBindingGeometry, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER).pBufferInfo = &geometry;
    }
    const VkDescriptorBufferInfo lights{
        .buffer = images.lights, .offset = images.lights_offset, .range = sizeof(LightsBlock)};
    if (images.lights != VK_NULL_HANDLE) {
        add(kBindingLights, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER).pBufferInfo = &lights;
    }
    vkCmdPushDescriptorSetKHR(commands, point, own.layout, 0, count, writes.data());
}

// A compute pass's writes before the next pass reads or writes them (and the apply's fragment shader reads them).
void compute_done(VkCommandBuffer commands)
{
    const VkMemoryBarrier written{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                  .pNext = nullptr,
                                  .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                  .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &written, 0,
                         nullptr, 0, nullptr);
}

void dispatch(VkCommandBuffer commands, const EffectsState& own, VkPipeline pipeline, const PassImages& images,
              const PassConstants& constants)
{
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    push_descriptors(commands, own, VK_PIPELINE_BIND_POINT_COMPUTE, images);
    vkCmdPushConstants(commands, own.layout, kStages, 0, sizeof(PassConstants), &constants);
    const auto groups = [](int32_t pixels) {
        return (static_cast<uint32_t>(pixels) + kGroupSide - 1) / kGroupSide;
    };
    vkCmdDispatch(commands, groups(core::at(constants.rect, 2)), groups(core::at(constants.rect, 3)), 1);
    compute_done(commands);
}

// The depth moved to be sampled by the compute passes (from whatever wrote it) and back to its resting layout after.
void depth_barrier(VkCommandBuffer commands, Image* depth, VkImageLayout layout, bool to_compute)
{
    const VkPipelineStageFlags tests =
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    const VkImageMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = to_compute ? VkAccessFlags{VK_ACCESS_MEMORY_WRITE_BIT} : VkAccessFlags{0},
        .dstAccessMask = to_compute ? VkAccessFlags{VK_ACCESS_SHADER_READ_BIT}
                                    : VkAccessFlags{VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT},
        .oldLayout = depth->layout,
        .newLayout = layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = depth->image,
        .subresourceRange =
            {.aspectMask = depth->aspect, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1},
    };
    vkCmdPipelineBarrier(commands, to_compute ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         to_compute ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : tests, 0, 0, nullptr, 0, nullptr, 1,
                         &barrier);
    depth->layout = layout;
}

// The colour multiplied by the occlusion in `ao` over the viewport (the indirect light: `ao` the light, `albedo` the
// surfaces' colour, added).
void record_apply(VkCommandBuffer commands, const EffectsState& own, VkPipeline pipeline, Image* colour,
                  const Image* ao, const Image* albedo, const PassConstants& constants)
{
    Memory::transition(commands, colour, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true);
    VkRenderingAttachmentInfo colour_info{};
    colour_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colour_info.imageView = colour->attachment_view;
    colour_info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colour_info.resolveMode = VK_RESOLVE_MODE_NONE;
    colour_info.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colour_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    const VkRect2D area{
        .offset = {.x = core::at(constants.rect, 0), .y = core::at(constants.rect, 1)},
        .extent = {.width = static_cast<uint32_t>(core::at(constants.rect, 2)),
                   .height = static_cast<uint32_t>(core::at(constants.rect, 3))}};
    VkRenderingInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea = area;
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &colour_info;
    vkCmdBeginRendering(commands, &info);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    const VkViewport viewport{.x = static_cast<float>(area.offset.x),
                              .y = static_cast<float>(area.offset.y),
                              .width = static_cast<float>(area.extent.width),
                              .height = static_cast<float>(area.extent.height),
                              .minDepth = 0.0F,
                              .maxDepth = 1.0F};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &area);
    push_descriptors(commands, own, VK_PIPELINE_BIND_POINT_GRAPHICS, {.in = ao, .albedo = albedo});
    vkCmdPushConstants(commands, own.layout, kStages, 0, sizeof(PassConstants), &constants);
    vkCmdDraw(commands, 3, 1, 0, 0);
    vkCmdEndRendering(commands);
}

// Where sample 0 of a multisampled target is, from the pixel's centre (Vulkan's standard sample locations; the centre
// when the device doesn't use them, or for one sample).
std::array<float, 2> sample_zero_offset(VkSampleCountFlagBits samples, bool standard)
{
    if (!standard) {
        return {0.0F, 0.0F};
    }
    // sample 0 in the pixel (0 - 1) as the standard places it for 2, 4, 8 and 16 samples
    constexpr std::array<float, 2> kTwo{0.75F, 0.75F};
    constexpr std::array<float, 2> kFour{0.375F, 0.125F};
    constexpr std::array<float, 2> kEight{0.5625F, 0.3125F};
    constexpr std::array<float, 2> kSixteen{0.5625F, 0.5625F};
    constexpr float kCentre = 0.5F;
    const auto from_centre = [](const std::array<float, 2>& at) {
        return std::array<float, 2>{at.front() - kCentre, at.back() - kCentre};
    };
    switch (samples) {
    case VK_SAMPLE_COUNT_2_BIT:
        return from_centre(kTwo);
    case VK_SAMPLE_COUNT_4_BIT:
        return from_centre(kFour);
    case VK_SAMPLE_COUNT_8_BIT:
        return from_centre(kEight);
    case VK_SAMPLE_COUNT_16_BIT:
        return from_centre(kSixteen);
    default:
        return {0.0F, 0.0F};
    }
}

// IEEE half to float (the read-back of the 16-bit images).
float from_half(uint16_t half)
{
    constexpr uint32_t kExponentShift = 10;
    constexpr uint32_t kExponentMask = 0x1F;
    constexpr uint32_t kMantissaMask = 0x3FF;
    constexpr int kBias = 15;
    const uint32_t sign = (static_cast<uint32_t>(half) >> 15U) & 1U;
    const uint32_t exponent = (static_cast<uint32_t>(half) >> kExponentShift) & kExponentMask;
    const uint32_t mantissa = static_cast<uint32_t>(half) & kMantissaMask;
    float value = 0.0F;
    if (exponent == 0) {
        value = std::ldexp(static_cast<float>(mantissa), 1 - kBias - static_cast<int>(kExponentShift));
    } else if (exponent == kExponentMask) {
        value = mantissa == 0 ? std::numeric_limits<float>::infinity() : std::numeric_limits<float>::quiet_NaN();
    } else {
        value = std::ldexp(static_cast<float>(mantissa | (1U << kExponentShift)),
                           static_cast<int>(exponent) - kBias - static_cast<int>(kExponentShift));
    }
    return sign != 0 ? -value : value;
}

// The sun the bounce is lit by (toward it, normalised): the scene's (rt_scene.h scene_sun - changed by the scene's sun:
// the console's `rt sun`, else rt_sun_source's, every effect's; the frame record's directional lights are the
// skater's, the same in every level); none while the level's fit isn't made or when its bake shows no sun (indoors):
// then the hits get the ambient light only.
bool bounce_sun(std::array<float, 3>* toward)
{
    const rt::SceneSun sun = rt::scene_sun();
    rt::note_sun_used(rt::SunUser::kBounce, sun.found(), sun.toward);
    if (!sun.found()) {
        return false;
    }
    *toward = sun.toward;
    return true;
}

// The indirect light's lights: the sun (bounce_sun; white, kGiSunLight - the bake has no sun colour). What THUG2's
// baked vertex colours lack is the sun's light bounced off what is around a surface - its sky and ambient light they
// already hold (and the frame record's ambient light is the skater's, the same everywhere) - so the hits get no ambient
// light and a ray that hits nothing brings none: E is the sun's light bounced once. (The block keeps both, and the
// apply's sky, for a level that one day gives a sky colour.) The rays' noise unit: the most one ray can bring.
LightsBlock lights_block(EffectsState& own, float* unit)
{
    LightsBlock block;
    std::array<float, 3> toward{};
    if (bounce_sun(&toward)) {
        block.to_sun = {toward.front(), core::at(toward, 1), toward.back(), 1.0F};
        block.sun = {kGiSunLight, kGiSunLight, kGiSunLight, 0.0F};
        // (added by the sun's glare) a sky model's sun colour (rt_sky_model / rt_sun_colour: rt::sun_light), its
        // brightness kept - Rec. 709 luma 1 x kGiSunLight; white without one
        const rt::SunLight light = rt::sun_light();
        const float luma = (0.2126F * core::at(light.colour, 0)) + (0.7152F * core::at(light.colour, 1)) +
                           (0.0722F * core::at(light.colour, 2));
        if (light.on && luma > 0.0F) {
            block.sun = {kGiSunLight * core::at(light.colour, 0) / luma, kGiSunLight * core::at(light.colour, 1) / luma,
                         kGiSunLight * core::at(light.colour, 2) / luma, 0.0F};
        }
        // (added by the time of day - engine/gfx/rt_daylight.h) the clock's light: its strength and colour over the
        // level's own (the moon's at night)
        const rt::DayLight day = rt::day_light();
        if (day.on) {
            for (std::size_t i = 0; i < 3; ++i) {
                core::at(block.sun, i) = kGiSunLight * day.direct * core::at(day.direct_tint, i);
            }
        }
    }
    for (std::size_t i = 0; i < 3; ++i) {
        core::at(block.ambient, i) = std::max(0.0F, core::at(block.ambient, i));
    }
    block.sky = block.ambient;
    block.shape = {kBounceSpread, kSunDistance, static_cast<float>(kGiSunMask), 0.0F};
    float most = kSmallestUnit;
    for (std::size_t i = 0; i < 3; ++i) {
        most = std::max(most, core::at(block.sun, i) + core::at(block.ambient, i));
    }
    *unit = most;
    // (logged when the level's fit is new: a level's sun, or none)
    const uint64_t fit = rt::baked_sun().scene;
    if (fit != own.sun_logged) {
        own.sun_logged = fit;
        if (block.to_sun.back() > 0.0F) {
            get_log("vulkan: rt_gi: the bounce's sun toward (%.3f, %.3f, %.3f)", static_cast<double>(block.to_sun.front()),
                    static_cast<double>(core::at(block.to_sun, 1)), static_cast<double>(core::at(block.to_sun, 2)));
        } else if (fit != 0) {
            get_log("vulkan: rt_gi: the level's bake shows no sun - no bounce here");
        }
    }
    return block;
}

// The timestamps' pool (once; none on a device without timestamps on its graphics queue).
bool ensure_timing(Backend& state, EffectsState& own)
{
    PassTiming& timing = own.timing;
    if (timing.pool != VK_NULL_HANDLE || timing.unavailable) {
        return timing.pool != VK_NULL_HANDLE;
    }
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(state.context.physical_device(), &properties);
    const VkQueryPoolCreateInfo info{.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                                     .pNext = nullptr,
                                     .flags = 0,
                                     .queryType = VK_QUERY_TYPE_TIMESTAMP,
                                     .queryCount = kTimingSlots * 2,
                                     .pipelineStatistics = 0};
    if (properties.limits.timestampComputeAndGraphics != VK_TRUE ||
        vkCreateQueryPool(state.context.device(), &info, nullptr, &timing.pool) != VK_SUCCESS) {
        timing.pool = VK_NULL_HANDLE;
        timing.unavailable = true;
        return false;
    }
    timing.period_ns = static_cast<double>(properties.limits.timestampPeriod);
    timing.pending = {};
    return true;
}

// A slot's last pair read back if it is ready (else lost), and every kTimingReport the mean logged.
void collect_timing(Backend& state, EffectsState& own, uint32_t slot, const std::array<uint32_t, 4>& rect)
{
    PassTiming& timing = own.timing;
    const uint64_t written = core::at(timing.pending, slot);
    core::at(timing.pending, slot) = 0;
    if (written == 0 || state.frames.completed() < written) {
        return; // (none, or its submission not known to have finished: lost)
    }
    std::array<uint64_t, 2> ticks{};
    if (vkGetQueryPoolResults(state.context.device(), timing.pool, slot * 2, 2, sizeof(ticks), ticks.data(),
                              sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) != VK_SUCCESS ||
        ticks.back() < ticks.front()) {
        return;
    }
    constexpr double kNsPerMs = 1e6;
    timing.sum_ms += static_cast<double>(ticks.back() - ticks.front()) * timing.period_ns / kNsPerMs;
    if (++timing.measured >= kTimingReport) {
        get_log("vulkan: %s: %.3f ms a pass on the device over the last %u (%ux%u, %u rays, %s)", effect_name(own),
                timing.sum_ms / timing.measured, timing.measured, core::at(rect, 2), core::at(rect, 3), effect_rays(own),
                effect_denoised(own) ? "denoised" : "not denoised");
        timing.sum_ms = 0.0;
        timing.measured = 0;
    }
}

// What the effect needs before it can trace (logged once when it can't): ray tracing, the scene - and for the indirect
// light the frame's geometry table and the material table.
bool effect_ready(Backend& state, EffectsState& own)
{
    const bool rgb = is_indirect(own);
    const uint64_t key = rgb ? kReportIndirect : 0U;
    if (!state.context.ray_tracing_enabled()) {
        if (first_report(state, kReportNoRayTracing + key)) {
            get_log("vulkan: %s=1 but ray tracing isn't enabled ([Graphics] ray_tracing=1, and a device with "
                    "VK_KHR_ray_query) - no ray-traced %s",
                    effect_name(own), rgb ? "indirect light" : "ambient occlusion");
        }
        return false;
    }
    const SceneView scene = current_scene(state);
    if (rgb) {
        own.scene = scene.tlas;
        own.geometry = scene.geometry;
        own.geometry_bytes = scene.geometry_bytes;
    } else if (own.scene != VK_NULL_HANDLE && own.scene == scene.tlas && scene.geometry != VK_NULL_HANDLE) {
        // (added for the skater's smooth shading) the occlusion reads the objects' vertex normals from the geometry
        // table of the TLAS it traces - else from a table with nothing live (the depth's normals everywhere)
        own.geometry = scene.geometry;
        own.geometry_bytes = scene.geometry_bytes;
    } else if (own.scene != VK_NULL_HANDLE) {
        if (own.no_geometry.buffer == VK_NULL_HANDLE &&
            state.memory.create_buffer(sizeof(GeometryRecord), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                       BufferPlace::kUpload, &own.no_geometry)) {
            if (own.no_geometry.mapped != nullptr) {
                std::memset(own.no_geometry.mapped, 0, sizeof(GeometryRecord));
                state.memory.flush(own.no_geometry, 0, sizeof(GeometryRecord));
            } else {
                state.memory.destroy_buffer(&own.no_geometry);
                own.no_geometry = DeviceBuffer{};
            }
        }
        own.geometry = own.no_geometry.buffer;
        own.geometry_bytes = sizeof(GeometryRecord);
    }
    if (own.scene == VK_NULL_HANDLE || own.geometry == VK_NULL_HANDLE) {
        if (first_report(state, kReportNoScene + key)) {
            get_log("vulkan: %s: no acceleration structure for the scene yet - the pass skipped", effect_name(own));
        }
        return false;
    }
    if (rgb && (!state.materials.ready() || state.materials.descriptor_set() == VK_NULL_HANDLE)) {
        if (first_report(state, kReportNoMaterials + key)) {
            get_log("vulkan: rt_gi: no material table - the indirect light pass skipped");
        }
        return false;
    }
    return true;
}

// The scene's build (recorded earlier on this queue), the lights' write (the indirect light's) and the last frame's
// uses of the images before this one's.
void ready_barrier(VkCommandBuffer commands, bool rgb)
{
    const VkMemoryBarrier ready{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                .pNext = nullptr,
                                .srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR |
                                                 VK_ACCESS_SHADER_WRITE_BIT |
                                                 (rgb ? VkAccessFlags{VK_ACCESS_HOST_WRITE_BIT} : VkAccessFlags{0}),
                                .dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR |
                                                 VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
                                                 (rgb ? VkAccessFlags{VK_ACCESS_UNIFORM_READ_BIT} : VkAccessFlags{0})};
    vkCmdPipelineBarrier(commands,
                         VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                             (rgb ? VkPipelineStageFlags{VK_PIPELINE_STAGE_HOST_BIT} : VkPipelineStageFlags{0}),
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &ready, 0, nullptr, 0, nullptr);
}

// The first pass's line in the log.
void log_first_pass(const EffectsState& own, const std::array<uint32_t, 4>& rect, bool added, bool sun)
{
    const char* const filtered = effect_denoised(own) ? "denoised" : "not denoised";
    if (is_indirect(own)) {
        get_log("vulkan: rt_gi: the first indirect light pass (%ux%u, %u rays, distance %.1f, strength %.2f, %s, %s; "
                "sun %s)",
                core::at(rect, 2), core::at(rect, 3), own.gi.rays, static_cast<double>(own.gi.distance),
                static_cast<double>(own.gi.strength), filtered, added ? "added" : "screened", sun ? "yes" : "none");
        return;
    }
    get_log("vulkan: rt_ao: the first ambient occlusion pass (%ux%u, %u rays, radius %.1f, strength %.2f, %s)",
            core::at(rect, 2), core::at(rect, 3), own.settings.rays, static_cast<double>(own.settings.radius),
            static_cast<double>(own.settings.strength), filtered);
}

// Step 2, the history, after the rays of the slot's current frame: written into the slot's current history.
struct DenoiseInputs {
    bool have_before = false;
    float rays = 1.0F;
    float unit = 1.0F; // the most one ray can bring
};
Image* record_temporal(VkCommandBuffer commands, const EffectsState& own, ViewSlot* slot, const PassConstants& constants,
                       const DenoiseInputs& inputs)
{
    const uint32_t now = slot->current;
    const uint32_t before = now ^ 1U;
    PassConstants temporal = constants;
    temporal.matrix = column_major(slot->world_to_pixel);
    temporal.params = {static_cast<float>(kAoMaxHistory), inputs.have_before ? 1.0F : 0.0F, inputs.rays, inputs.unit};
    Image* const history = &core::at(slot->histories, now);
    dispatch(commands, own, own.temporal,
             {.out = history,
              .guide = &core::at(slot->guides, now),
              .in = &slot->raw,
              .guide_before = &core::at(slot->guides, before),
              .history = &core::at(slot->histories, before)},
             temporal);
    slot->history_written = history;
    return history;
}

// Step 3, the spatial filter over the history just written; returns the image the apply reads.
Image* record_filter(VkCommandBuffer commands, const EffectsState& own, ViewSlot* slot, const PassConstants& constants,
                     const DenoiseInputs& inputs, Image* history)
{
    const Image* const guide = &core::at(slot->guides, slot->current);
    Image* result = history;
    PassConstants filter = constants;
    filter.params = {0.0F, 0.0F, inputs.rays, inputs.unit};
    for (uint32_t pass = 0; pass < kAoFilterPasses; ++pass) {
        Image* const target = &core::at(slot->filtered, pass % 2);
        filter.extra = {static_cast<int32_t>(1U << pass), 0, 0, 0};
        dispatch(commands, own, own.filter, {.out = target, .guide = guide, .in = result}, filter);
        result = target;
    }
    return result;
}

// The passes of either effect over the bound targets for a viewport (record_rt_ao_pass, record_rt_gi_pass).
bool record_effect(Backend& state, EffectsState& own, const rt::ViewDrawn& view)
{
    if (!effect_on(own) || own.failed || !effect_ready(state, own)) {
        return false;
    }
    const bool rgb = is_indirect(own);
    const uint64_t key = rgb ? kReportIndirect : 0U;
    Image* const colour = image_of_surface(core::at(state.targets, 0));
    Image* const depth = image_of_surface(state.depth);
    if (colour == nullptr || depth == nullptr || colour->attachment_view == VK_NULL_HANDLE ||
        (colour->aspect & VK_IMAGE_ASPECT_COLOR_BIT) == 0 || (depth->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) == 0 ||
        (depth->usage & VK_IMAGE_USAGE_SAMPLED_BIT) == 0 || depth->view == VK_NULL_HANDLE ||
        depth->samples != colour->samples || depth->extent.width < colour->extent.width ||
        depth->extent.height < colour->extent.height) {
        if (first_report(state, kReportTargets + key)) {
            get_log("vulkan: %s: the bound targets can't be read (no samplable depth of the colour's size and "
                    "samples) - the pass skipped there",
                    effect_name(own));
        }
        return false;
    }
    std::string error;
    const VkPipeline apply = ensure_objects(state, own, &error)
                                 ? apply_pipeline(state, own, colour->format, colour->samples, &error)
                                 : VK_NULL_HANDLE;
    if (apply == VK_NULL_HANDLE) {
        own.failed = true;
        if (first_report(state, kReportPipeline + key)) {
            get_log("vulkan: %s: the passes couldn't be made (%s) - the effect is off", effect_name(own),
                    error.c_str());
        }
        return false;
    }
    if (rgb && own.material_layout != state.materials.layout()) {
        return false; // (the table was made again with another layout: not while the device lives)
    }
    // the viewport, inside the target
    const uint32_t left = std::min(core::at(view.rect, 0), colour->extent.width);
    const uint32_t top = std::min(core::at(view.rect, 1), colour->extent.height);
    const uint32_t width = std::min(core::at(view.rect, 2), colour->extent.width - left);
    const uint32_t height = std::min(core::at(view.rect, 3), colour->extent.height - top);
    if (width == 0 || height == 0) {
        return false;
    }
    rt::ViewDrawn clipped = view;
    clipped.rect = {left, top, width, height};
    const Matrix to_pixel = world_to_pixel(clipped);
    Matrix to_world{};
    if (!invert(to_pixel, &to_world)) {
        return false;
    }
    float unit = 1.0F; // (the occlusion: one ray brings 0 or 1)
    LightsBlock lights;
    std::optional<RingSpan> lights_span;
    if (rgb) {
        lights = lights_block(own, &unit);
        std::array<uint8_t, sizeof(LightsBlock)> bytes{};
        std::memcpy(bytes.data(), &lights, sizeof(lights));
        lights_span = state.ring.copy(bytes, kUniformAlignment);
        if (!lights_span) {
            return false;
        }
    }
    ViewSlot* const slot = slot_for(state, own, clipped.rect, colour->extent);
    if (slot == nullptr) {
        return false;
    }
    const bool denoise = effect_denoised(own);
    bool have_before = denoise && slot->have_before && slot->frame_drawn + 1 >= state.frame;
    const bool cut = have_before && camera_cut(slot->camera, view.camera);
    have_before = have_before && !cut;
    // (added by the first frame - vulkan_rt_history.h: a history that starts again is seeded with a full history's
    // frames of rays, so nothing fades in)
    const uint32_t seeds = seed_frames(denoise, !have_before, kAoMaxHistory);

    VkCommandBuffer commands = outside_pass(state);
    const bool timed = ensure_timing(state, own);
    const auto timing_slot = static_cast<uint32_t>(own.passes % kTimingSlots);
    if (timed) {
        collect_timing(state, own, timing_slot, clipped.rect);
        vkCmdResetQueryPool(commands, own.timing.pool, timing_slot * 2, 2);
        vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, own.timing.pool, timing_slot * 2);
    }
    if (slot->fresh) {
        clear_slot(commands, slot);
    }
    ready_barrier(commands, rgb);
    depth_barrier(commands, depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true);

    PassConstants constants{};
    constants.rect = {static_cast<int32_t>(left), static_cast<int32_t>(top), static_cast<int32_t>(width),
                      static_cast<int32_t>(height)};
    constants.camera = {core::at(view.camera.position, 0), core::at(view.camera.position, 1),
                        core::at(view.camera.position, 2), 0.0F};
    const auto rays = static_cast<float>(effect_rays(own));
    const DenoiseInputs inputs{.have_before = have_before, .rays = rays, .unit = unit};

    PassConstants trace = constants;
    trace.matrix = column_major(to_world);
    const std::array<float, 2> sample = sample_zero_offset(colour->samples, own.standard_samples);
    trace.params = {rgb ? own.gi.distance : own.settings.radius, rays, kOffsetScale, sample.front()};
    core::at(trace.camera, 3) = sample.back();
    Image* result = &slot->raw;
    slot->history_written = nullptr;
    for (uint32_t seed = 0; seed < seeds; ++seed) {
        if (seed > 0) {
            // (the next frame's rays from the same camera: the history just written is the one before)
            slot->current ^= 1U;
            ++slot->passes;
            slot->world_to_pixel = to_pixel;
        }
        // 1. the rays
        trace.extra = {static_cast<int32_t>(slot->passes % kSequenceWrap),
                       static_cast<int32_t>(rgb ? kGiMask : kAoMask),
                       // (added for the characters) the occlusion on the skinned characters x this, per mille
                       rgb ? 0 : static_cast<int32_t>(std::lround(own.settings.characters * kPerMille)), 0};
        PassImages trace_images{
            .scene = true, .depth = depth, .out = &slot->raw, .guide = &core::at(slot->guides, slot->current)};
        trace_images.geometry = own.geometry;
        trace_images.geometry_bytes = own.geometry_bytes;
        if (rgb) {
            trace_images.albedo = &slot->albedo;
            trace_images.lights = lights_span->buffer;
            trace_images.lights_offset = lights_span->offset;
            const VkDescriptorSet material_set = state.materials.descriptor_set();
            vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.layout, kMaterialSet, 1,
                                    &material_set, 0, nullptr);
        }
        dispatch(commands, own, core::at(own.trace, colour->samples != VK_SAMPLE_COUNT_1_BIT ? 1U : 0U), trace_images,
                 trace);
        // 2. the history
        if (denoise) {
            DenoiseInputs step = inputs;
            step.have_before = have_before || seed > 0;
            result = record_temporal(commands, own, slot, constants, step);
        }
    }
    // 3. the spatial filter
    if (denoise) {
        result = record_filter(commands, own, slot, constants, inputs, result);
    }
    depth_barrier(commands, depth, depth->resting, false);

    // 4. the colour multiplied (the occlusion) or added to (the indirect light)
    PassConstants apply_constants = constants;
    if (rgb) {
        apply_constants.params = {own.gi.strength, core::at(lights.sky, 0), core::at(lights.sky, 1),
                                  core::at(lights.sky, 2)};
    } else {
        apply_constants.params = {own.settings.strength, 0.0F, 0.0F, 0.0F};
    }
    record_apply(commands, own, apply, colour, result, rgb ? &slot->albedo : nullptr, apply_constants);
    if (colour->layout != colour->resting) {
        Memory::transition(commands, colour, colour->resting);
    }
    if (timed) {
        vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, own.timing.pool, (timing_slot * 2) + 1);
        core::at(own.timing.pending, timing_slot) = state.frames.serial();
    }
    state.bound = BoundState{};
    const uint64_t serial = state.frames.serial();
    colour->used = serial;
    depth->used = serial;
    for (Image* image : images_of(*slot)) {
        image->used = serial;
    }
    slot->applied = result;
    slot->world_to_pixel = to_pixel;
    slot->camera = history_camera(view.camera);
    slot->have_before = denoise;
    slot->frame_drawn = state.frame;
    slot->current ^= 1U;
    ++slot->passes;
    ++own.passes;
    if (seeds > 1 && first_report(state, kReportSeeded + key)) {
        get_log("vulkan: %s: a history started again (%s) - seeded with %u frames' rays in one (nothing fades in; "
                "logged once)",
                effect_name(own), cut ? "a camera cut" : "a new view, a load or the settings", seeds);
    }
    if (first_report(state, kReportFirstPass + key)) {
        log_first_pass(own, clipped.rect, float_format(colour->format), lights.to_sun.back() > 0.0F);
    }
    return true;
}

// One of an effect's images of the viewport last drawn with `rect` read back.
bool read_image(Backend& state, EffectsState& own, const std::array<uint32_t, 4>& rect, Image* (*pick)(ViewSlot&),
                RtAoTexels* out)
{
    *out = RtAoTexels{};
    for (const std::unique_ptr<ViewSlot>& slot : own.slots) {
        if (slot->rect != rect) {
            continue;
        }
        Image* const image = pick(*slot);
        if (image == nullptr || image->image == VK_NULL_HANDLE || !finish_work(state)) {
            return false;
        }
        constexpr std::size_t kChannels = 4;
        const std::size_t texels = static_cast<std::size_t>(image->extent.width) * image->extent.height;
        std::vector<uint8_t> bytes(texels * kChannels * sizeof(uint16_t));
        if (!state.memory.read_image_level(image, 0, &bytes)) {
            return false;
        }
        out->width = image->extent.width;
        out->height = image->extent.height;
        out->texels.resize(texels);
        for (std::size_t i = 0; i < texels; ++i) {
            for (std::size_t channel = 0; channel < kChannels; ++channel) {
                uint16_t half = 0;
                std::memcpy(&half, &core::at(bytes, ((i * kChannels) + channel) * sizeof(uint16_t)), sizeof(half));
                core::at(core::at(out->texels, i), channel) = from_half(half);
            }
        }
        return true;
    }
    return false;
}

Image* pick_raw(ViewSlot& slot) { return &slot.raw; }
Image* pick_history(ViewSlot& slot) { return slot.history_written; }
Image* pick_final(ViewSlot& slot) { return slot.applied; }
Image* pick_albedo(ViewSlot& slot) { return &slot.albedo; }

// An effect's passes' objects and histories released.
void release_effect(Backend& state, EffectsState& own)
{
    const VkDevice device = state.context.device();
    for (const std::unique_ptr<ViewSlot>& slot : own.slots) {
        release_slot(state, slot.get(), true);
    }
    own.slots.clear();
    if (device != nullptr) {
        for (const ApplyPipeline& made : own.applies) {
            vkDestroyPipeline(device, made.pipeline, nullptr);
        }
        for (const VkPipeline pipeline : {own.trace.front(), own.trace.back(), own.temporal, own.filter}) {
            if (pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, pipeline, nullptr);
            }
        }
        if (own.vertex != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device, own.vertex, nullptr);
        }
        if (own.layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, own.layout, nullptr);
        }
        if (own.set_layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, own.set_layout, nullptr);
        }
        if (own.sampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, own.sampler, nullptr);
        }
        if (own.timing.pool != VK_NULL_HANDLE) {
            vkDestroyQueryPool(device, own.timing.pool, nullptr);
        }
    }
    own.timing = PassTiming{};
    own.applies.clear();
    own.trace = {};
    own.temporal = VK_NULL_HANDLE;
    own.filter = VK_NULL_HANDLE;
    own.vertex = VK_NULL_HANDLE;
    own.layout = VK_NULL_HANDLE;
    own.set_layout = VK_NULL_HANDLE;
    own.material_layout = VK_NULL_HANDLE;
    own.sampler = VK_NULL_HANDLE;
    own.scene = VK_NULL_HANDLE;
    own.geometry = VK_NULL_HANDLE;
    own.geometry_bytes = 0;
    if (own.no_geometry.buffer != VK_NULL_HANDLE) {
        state.memory.destroy_buffer(&own.no_geometry);
    }
    own.no_geometry = DeviceBuffer{};
    own.failed = false;
}

void reset_effect_history(EffectsState& own)
{
    for (const std::unique_ptr<ViewSlot>& slot : own.slots) {
        slot->have_before = false;
        slot->passes = 0;
    }
}

} // namespace

void set_rt_ao_settings(const RtAoSettings& settings)
{
    RtAoSettings kept = settings;
    kept.strength = std::isfinite(kept.strength) ? std::clamp(kept.strength, 0.0F, kMaxStrength) : 0.0F;
    kept.radius = std::isfinite(kept.radius) ? std::clamp(kept.radius, kMinRadius, kMaxRadius) : kRtAoRadius;
    kept.rays = std::clamp(kept.rays, 1U, kMaxRays);
    kept.characters = std::isfinite(kept.characters) ? std::clamp(kept.characters, 0.0F, 1.0F) : 1.0F; // (added for the characters)
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        effects().settings = kept;
        // (a split comparison's swap between its sides keeps each side's history - rt_scene.h settings_swap)
        if (!rt::settings_swap()) {
            rt_effects_reset_history(state);
        }
    }
    if (kept.on) {
        rt::add_view_listener(&listener());
    } else {
        rt::remove_view_listener(&listener());
    }
}

RtAoSettings rt_ao_settings()
{
    const std::scoped_lock lock(backend().mutex);
    return effects().settings;
}

void set_rt_gi_settings(const RtGiSettings& settings)
{
    RtGiSettings kept = settings;
    kept.strength = std::isfinite(kept.strength) ? std::clamp(kept.strength, 0.0F, kMaxGiStrength) : 0.0F;
    kept.distance =
        std::isfinite(kept.distance) ? std::clamp(kept.distance, kMinRadius, kMaxGiDistance) : kRtGiDistance;
    kept.rays = std::clamp(kept.rays, 1U, kMaxRays);
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        indirect().gi = kept;
        if (!rt::settings_swap()) {
            reset_effect_history(indirect());
        }
    }
    // (the bounce's sun is the level's baked one: render_pc fits it in a new world's first frame. Set after the shadows'
    // settings - renderer_choice's order -, whose own `off` would clear it)
    if (kept.on) {
        rt::set_baked_sun_wanted(true);
    }
    if (kept.on) {
        rt::add_view_listener(&indirect_listener());
    } else {
        rt::remove_view_listener(&indirect_listener());
    }
}

RtGiSettings rt_gi_settings()
{
    const std::scoped_lock lock(backend().mutex);
    return indirect().gi;
}

void rt_effects_set_scene(VkAccelerationStructureKHR tlas)
{
    const std::scoped_lock lock(backend().mutex);
    rt_effects_set_scene_locked(tlas);
}

void rt_effects_set_scene_locked(VkAccelerationStructureKHR tlas) { effects().scene = tlas; }

VkImageUsageFlags rt_effects_depth_usage(const Backend& state, VkFormat format)
{
    if (!(effects().settings.on || indirect().gi.on) || !state.context.ray_tracing_enabled()) {
        return 0;
    }
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(state.context.physical_device(), format, &properties);
    return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0 ? VK_IMAGE_USAGE_SAMPLED_BIT
                                                                                         : 0;
}

void rt_effects_reset_history(Backend& state)
{
    (void)state;
    reset_effect_history(effects());
}

void rt_gi_reset_history(Backend& state)
{
    (void)state;
    reset_effect_history(indirect());
}

// (added by cel shading, vulkan_cel_draw.h: its bands take the soft gradients away - no occlusion, no bounce while on)
bool record_rt_ao_pass(Backend& state, const rt::ViewDrawn& view) { return !cel_on() && record_effect(state, effects(), view); }

bool record_rt_gi_pass(Backend& state, const rt::ViewDrawn& view) { return !cel_on() && record_effect(state, indirect(), view); }

uint64_t rt_ao_passes() { return effects().passes; }

uint64_t rt_gi_passes() { return indirect().passes; }

bool read_rt_ao_image(Backend& state, const std::array<uint32_t, 4>& rect, RtAoImage which, RtAoTexels* out)
{
    Image* (*pick)(ViewSlot&) = pick_raw;
    if (which == RtAoImage::kHistory) {
        pick = pick_history;
    } else if (which == RtAoImage::kFinal) {
        pick = pick_final;
    }
    return read_image(state, effects(), rect, pick, out);
}

bool read_rt_gi_image(Backend& state, const std::array<uint32_t, 4>& rect, RtGiImage which, RtAoTexels* out)
{
    Image* (*pick)(ViewSlot&) = pick_raw;
    if (which == RtGiImage::kHistory) {
        pick = pick_history;
    } else if (which == RtGiImage::kFinal) {
        pick = pick_final;
    } else if (which == RtGiImage::kAlbedo) {
        pick = pick_albedo;
    }
    return read_image(state, indirect(), rect, pick, out);
}

void release_rt_effects(Backend& state)
{
    release_effect(state, effects());
    release_effect(state, indirect());
}

} // namespace render::vulkan
