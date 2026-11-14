// NOT ORIGINAL - ray-traced reflections (vulkan_rt_reflections.h; docs/design/vulkan.md, R5's reflections).
#include "gpu_vulkan/vulkan_rt_reflections.h"

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
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/glsl_compiler.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_formats.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_objects.h"
#include "gpu_vulkan/vulkan_rt_glsl.h"
#include "gpu_vulkan/vulkan_rt_materials.h"
#include "gpu_vulkan/vulkan_rt_scene.h"
#include "gpu_vulkan/vulkan_rt_sky_pass.h"
#include "gpu_vulkan/vulkan_ring.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

// The once-only reports' keys (Backend::reported).
constexpr uint64_t kReportNoRayTracing = 0x52360001U;
constexpr uint64_t kReportNoScene = 0x52360002U;
constexpr uint64_t kReportTargets = 0x52360003U;
constexpr uint64_t kReportPipeline = 0x52360004U;
constexpr uint64_t kReportFirstPass = 0x52360005U;
constexpr uint64_t kReportImages = 0x52360006U;
constexpr uint64_t kReportNoMirrors = 0x52360007U;
constexpr uint64_t kReportNoMaterials = 0x52360008U;

constexpr float kMaxStrength = 1.0F;
constexpr float kMaxRoughness = 0.5F; // rt_glass_roughness at most (radians)
constexpr uint32_t kRoughRays = 4;    // the rays a rough (glass) pixel traces
// (added for the surface reflections) the roughest surface-map texel that reflects (perceptual roughness; faded out from
// three quarters of it), a rough surface's cone per GGX alpha (radians), rt_reflection_surfaces at most
constexpr float kSurfaceRoughest = 0.7F;
constexpr float kSurfaceConePerAlpha = 1.0F;
constexpr float kMaxSurfaces = 2.0F;
constexpr float kOffsetScale = 1.0F; // the reflection ray's start pushed off the surface by this much, growing with distance
constexpr uint32_t kGroupSide = 8;   // the trace pass's work groups: 8 x 8 pixels
constexpr uint32_t kSurfaceGroup = 64; // the check's trace: rays per work group
constexpr std::size_t kMatrixSide = 4;
constexpr std::size_t kMatrixFloats = kMatrixSide * kMatrixSide;
constexpr std::size_t kMaxLights = 4;   // directional lights the hits are lit by (objects)
constexpr double kSingular = 1e-300;    // a pivot this small: the matrix can't be inverted
constexpr VkDeviceSize kUniformAlignment = 256; // (at least any device's minUniformBufferOffsetAlignment)
constexpr std::size_t kViewSlots = 4;
constexpr uint32_t kMaterialSet = 1;

// The full-screen triangle (three vertices, no buffers).
constexpr const char* kVertexSource = R"(#version 460
void main()
{
    vec2 at = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(at * 2.0 - 1.0, 0.0, 1.0);
}
)";

// Step 1. The pass's block: to_world (pixel x, pixel y, depth, 1) -> world (the target's pixels); camera: its position
// (w: the depth sample's offset from the pixel's centre, y); params: x the strength, y the rays' length, z the start's
// offset scale, w the depth sample's offset (x); rect: the viewport; extra: x the mask, y the directional lights;
// ambient: the ambient light (w: the angle a pixel spans, radians); then the directional lights (the way they travel).
constexpr const char* kTraceBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) uniform accelerationStructureEXT u_scene;
#ifdef MULTISAMPLED
layout(set = 0, binding = 1) uniform sampler2DMS u_depth;
#else
layout(set = 0, binding = 1) uniform sampler2D u_depth;
#endif
layout(set = 0, binding = 2, rgba16f) uniform writeonly image2D o_out;
layout(set = 0, binding = 8, rgba16f) uniform writeonly image2D o_base;
layout(set = 0, binding = 3, std140) uniform Pass {
    mat4 to_world;
    vec4 camera;
    vec4 params;
    ivec4 rect;
    ivec4 extra;
    vec4 ambient;
    vec4 light_direction[4];
    vec4 light_colour[4];
    vec4 fresnel; // x 1: Fresnel on; y the glass's roughness (radians); z the rays a rough pixel traces; w glass's F0
    // (added by the sky) what a miss sees with rt_sky (vulkan_rt_sky_pass.h rt_sky_for_rays; the SKY variant only)
    mat4 sky_to_pixel;
    vec4 sky_sun;
    vec4 sky_low;
    vec4 sky_rect;
    vec4 sky_tint;
    // (added for the surface reflections) x rt_reflection_surfaces, y the roughest map texel that reflects, z the cone's
    // half-angle per GGX alpha (the roughness squared)
    vec4 surface;
} pass;

const uint INSTANCE_OBJECT = 32u; // (rt::InstanceFlag kObject)
const uint CLASS_WATER = 1u;      // (rt::kReflectWater, kReflectGlass)
const uint CLASS_GLASS = 2u;
const uint CLASS_SURFACE = 3u;    // (rt::kReflectSurface: glossy by its surface map)
const uint MASK_PASS_CLEAR = 1u;
const float MAX_CONE = 0.5;       // (a rough surface's cone at most: rt_glass_roughness's ceiling)  // (a mask pass's kind: rt::kMaskClear; kMaskOpaque 2)


// How much of the reflection the pixel shows: `c` the cosine between the view and the surface's normal.
//  - Fresnel off: reflectivity x strength at every angle (the first version);
//  - water and glass: Schlick's F0 + (1 - F0)(1 - c)^5, x the reflectivity (the game's art: how shiny it made it) x
//    2 strength (at most 1: the default strength, 0.5, is the physical amount);
//  - other shiny surfaces: reflectivity x strength face-on (as before), rising by Schlick's curve toward the
//    reflectivity x 2 strength (at most the reflectivity) at grazing angles.
//  `f0_material`: the record's f0 (water's: rt::kReflectWaterF0); glass takes fresnel.w (rt_glass_reflectance)
float reflection_weight(float reflectivity, uint kind, float f0_material, float c)
{
    float strength = pass.params.x;
    if (pass.fresnel.x < 0.5) {
        return reflectivity * strength;
    }
    float schlick = pow(1.0 - clamp(c, 0.0, 1.0), 5.0);
    float most = reflectivity * min(1.0, 2.0 * strength);
    if (kind == CLASS_WATER || kind == CLASS_GLASS) {
        float f0 = kind == CLASS_WATER ? f0_material : pass.fresnel.w;
        return most * (f0 + (1.0 - f0) * schlick);
    }
    float face_on = reflectivity * strength;
    return face_on + (max(most, face_on) - face_on) * schlick;
}

vec3 world_at(ivec2 p, float d)
{
    vec4 w = pass.to_world * vec4(vec2(p) + vec2(pass.params.w, pass.camera.w), d, 1.0);
    return w.xyz / w.w;
}

// The triangle's corners in the world.
void world_corners(HitVertices v, mat4x3 to_world, out vec3 a, out vec3 b, out vec3 c)
{
    a = to_world * vec4(v.position[0], 1.0);
    b = to_world * vec4(v.position[1], 1.0);
    c = to_world * vec4(v.position[2], 1.0);
}

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

// The share of a material's environment pass its later passes leave (rt::Material::mask_passes, render_pc/rt_record.cpp
// environment_share): for each mask pass k, its texel's alpha a at the hit - the texel's own, or times the vertex alpha
// doubled (at most 1), as the game's pixel shader takes a pass's alpha - as 1 - a (kMaskClear) or a (kMaskOpaque);
// each texture at the level for its own coordinates' footprint (`width`: a pixel's width at the hit).
float environment_mask(uint id, uint instance, uint primitive, HitVertices v, vec2 bary, vec3 a, vec3 b, vec3 c,
                       float width)
{
    Material m = material(id);
    float mask = 1.0;
    float vertex_alpha = (v.flags & GEOMETRY_COLOUR) != 0u ? hit_colour(v, bary).a * 2.0 : 1.0;
    float world_area = length(cross(b - a, c - a));
    for (uint k = 0u; k < 3u; ++k) {
        uint mode = m.mask_mode[k];
        uint kind = mode & 15u;
        if (kind == 0u) {
            continue;
        }
        uint set = (mode >> 4) & 15u;
        vec2 uv = hit_uv_set(instance, primitive, bary, set);
        vec2 uv0 = hit_uv_set(instance, primitive, vec2(0.0), set);
        vec2 du = hit_uv_set(instance, primitive, vec2(1.0, 0.0), set) - uv0;
        vec2 dv = hit_uv_set(instance, primitive, vec2(0.0, 1.0), set) - uv0;
        float uv_area = abs(du.x * dv.y - du.y * dv.x);
        float lod = 0.0;
        if (world_area > 1e-12 && uv_area > 1e-12 && width > 0.0) {
            ivec2 size = textureSize(sampler2D(material_textures[nonuniformEXT(m.mask_texture[k])],
                                               material_samplers[0]), 0);
            lod = max(0.0, log2(max(width * sqrt(uv_area * float(size.x) * float(size.y) / world_area), 1e-6)));
        }
        float alpha = material_mask_alpha(id, k, uv, lod);
        if ((mode & 256u) == 0u) {
            alpha = clamp(alpha * vertex_alpha, 0.0, 1.0);
        }
        mask *= kind == MASK_PASS_CLEAR ? 1.0 - alpha : alpha;
    }
    return mask;
}

vec3 lighting(vec3 n)
{
    vec3 light = pass.ambient.rgb;
    for (int i = 0; i < pass.extra.y && i < 4; ++i) {
        light += pass.light_colour[i].rgb * max(0.0, dot(n, -pass.light_direction[i].xyz));
    }
    return light;
}

// What the reflection ray from `origin` along `direction` sees (a: 1 when it hit something); `travelled`: the path so
// far (the footprint's).
vec4 reflected(vec3 origin, vec3 direction, float travelled)
{
    uint mask = uint(pass.extra.x) & 0xFFu;
    rayQueryEXT query;
    rayQueryInitializeEXT(query, u_scene, gl_RayFlagsNoneEXT, mask, origin, 0.0, direction, pass.params.y);
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
#ifdef SKY
        // (added by the sky) rt_sky: the miss sees the sky drawn - its atmosphere and clouds - as the game's colours
        vec3 sky = sky_for_ray(direction, pass.sky_to_pixel, pass.sky_sun, pass.sky_low, pass.sky_rect, pass.sky_tint);
        return vec4(clamp(pow(max(sky, vec3(0.0)), vec3(1.0 / 2.2)), 0.0, 1.0), 1.0);
#else
        return vec4(0.0);
#endif
    }
    uint id = rayQueryGetIntersectionInstanceCustomIndexEXT(query, true);
    uint instance = rayQueryGetIntersectionInstanceIdEXT(query, true);
    float t = rayQueryGetIntersectionTEXT(query, true);
    vec2 barycentrics = rayQueryGetIntersectionBarycentricsEXT(query, true);
    HitVertices v;
    hit_vertices(instance, rayQueryGetIntersectionPrimitiveIndexEXT(query, true), v);
    vec3 a, b, c;
    world_corners(v, rayQueryGetIntersectionObjectToWorldEXT(query, true), a, b, c);
    vec3 n = cross(b - a, c - a);
    n = length(n) > 0.0 ? normalize(n) : -direction;
    if (dot(n, direction) > 0.0) {
        n = -n;
    }
    float width = pass.ambient.w * (travelled + t);
    vec2 uv = hit_uv(v, barycentrics);
    vec3 colour = (material_texel_lod(id, uv, level_for(id, v, a, b, c, width)) * material(id).colour).rgb;
    if ((geometry_record(instance).instance_flags & INSTANCE_OBJECT) != 0u) {
        colour *= lighting(n);
    } else if ((v.flags & GEOMETRY_COLOUR) != 0u) {
        colour *= hit_colour(v, barycentrics).rgb * 2.0;
    }
    return vec4(clamp(colour, 0.0, 1.0), 1.0);
}

void main()
{
    ivec2 local = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(local, pass.rect.zw))) {
        return;
    }
    ivec2 p = pass.rect.xy + local;
    float d = texelFetch(u_depth, p, 0).r;
    vec4 result = vec4(0.0);
    vec4 base_layer = vec4(0.0); // (the masked glass's own colour x its share, and the share)
    if (d < 1.0) {
        vec3 eye = pass.camera.xyz;
        vec3 here = world_at(p, d);
        float distance = length(here - eye);
        vec3 view = (here - eye) / max(distance, 1e-6);
        uint mask = uint(pass.extra.x) & 0xFFu;
        // the surface the pixel shows
        rayQueryEXT query;
        rayQueryInitializeEXT(query, u_scene, gl_RayFlagsOpaqueEXT, mask, eye, 0.0, view, distance * 1.05 + 1.0);
        while (rayQueryProceedEXT(query)) {
        }
        if (rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionTriangleEXT) {
            float t = rayQueryGetIntersectionTEXT(query, true);
            uint id = rayQueryGetIntersectionInstanceCustomIndexEXT(query, true);
            float reflectivity = material(id).reflectivity;
            HitVertices v;
            if (abs(t - distance) <= 1.0 + 0.02 * distance && reflectivity > 0.0 &&
                hit_vertices(rayQueryGetIntersectionInstanceIdEXT(query, true),
                             rayQueryGetIntersectionPrimitiveIndexEXT(query, true), v)) {
                vec3 a, b, c;
                world_corners(v, rayQueryGetIntersectionObjectToWorldEXT(query, true), a, b, c);
                vec3 n = cross(b - a, c - a);
                if (length(n) > 0.0) {
                    n = normalize(n);
                    if (dot(n, view) > 0.0) {
                        n = -n;
                    }
                    vec3 point = eye + view * t;
                    float offset = pass.params.z * (1.0 + t * 0.0005 + t * t * 6e-8);
                    Material surface = material(id);
                    // (the mask: where on its texture the surface reflects - the share of the environment pass the
                    // game's pixel shader leaves at the pixel's footprint: each mask pass's alpha, 1 - a or a)
                    vec2 bary = rayQueryGetIntersectionBarycentricsEXT(query, true);
                    float mask = environment_mask(id, rayQueryGetIntersectionInstanceIdEXT(query, true),
                                                  rayQueryGetIntersectionPrimitiveIndexEXT(query, true), v, bary,
                                                  a, b, c, pass.ambient.w * t);
                    float weight = reflection_weight(reflectivity, surface.reflection_class, surface.f0, -dot(view, n));
                    if (pass.fresnel.x >= 0.5 && surface.reflection_mask != 0u) {
                        vec4 texel = material_texel_lod(id, hit_uv(v, bary), level_for(id, v, a, b, c, pass.ambient.w * t));
                        // The game drew its environment map there in place of the texture (its pixel shader's lerp by
                        // the alpha): the glass's own colour - the texel x the pass's colour x the vertex colour
                        // doubled (objects: the frame's lights), as that shader would have drawn it - is put back
                        // under the reflection: face-on the pane shows (1 - F) of it, the reflection the rest; the
                        // environment map is kept only for what the rays miss (the base layer, premultiplied).
                        uint instance = rayQueryGetIntersectionInstanceIdEXT(query, true);
                        vec3 light = (geometry_record(instance).instance_flags & INSTANCE_OBJECT) != 0u ? lighting(n)
                                   : ((v.flags & GEOMETRY_COLOUR) != 0u ? hit_colour(v, bary).rgb * 2.0 : vec3(1.0));
                        vec3 own = clamp(texel.rgb * surface.colour.rgb * light, 0.0, 1.0);
                        float share = mask * (1.0 - clamp(weight, 0.0, 1.0));
                        base_layer = vec4(own * share, share);
                    }
                    weight *= mask;
                    vec3 mirror = reflect(view, n);
                    float rough = pass.fresnel.x >= 0.5 && surface.reflection_class == CLASS_GLASS ? pass.fresnel.y : 0.0;
                    if (surface.reflection_class == CLASS_SURFACE) {
                        // (added for the surface reflections) a surface glossy by its map: Schlick's Fresnel at the
                        // map's F0, its grazing rise held to 1 - roughness (a rough surface never turns into a mirror),
                        // x its kind's gloss (the reflectivity) x rt_reflection_surfaces x 2 strength (at most 1); the
                        // map's roughness at the hit fades it out toward the roughest that reflects and spreads its
                        // rays over a cone as wide as its GGX lobe
                        float r = clamp(material_surface_lod(id, hit_uv(v, bary),
                                                             level_for(id, v, a, b, c, pass.ambient.w * t)).b, 0.0, 1.0);
                        r = mix(r, surface.kind_roughness, surface.kind_roughness_weight); // (the kind's, rt_detail)
                        float cosine = clamp(-dot(view, n), 0.0, 1.0);
                        float f0 = surface.f0;
                        float fresnel = f0 + (max(1.0 - r, f0) - f0) * pow(1.0 - cosine, 5.0);
                        weight = mask * reflectivity * pass.surface.x * min(1.0, 2.0 * pass.params.x) * fresnel *
                                 (1.0 - smoothstep(0.75 * pass.surface.y, pass.surface.y, r));
                        rough = min(r * r * pass.surface.z, MAX_CONE);
                    }
                    vec4 seen = vec4(0.0);
                    if (weight <= 0.0) {
                        // (nothing of it shows: no ray)
                    } else if (rough > 0.0) {
                        // a rough surface: rays spread over a cone round the mirror direction (a golden-angle disc of
                        // directions, turned by a quarter of its step in each pixel of a 2 x 2 block - the apply's
                        // 3 x 3 blur then sees four times the directions, with no noise), their hits averaged; the
                        // share that hit something scales the weight (the misses keep the game's environment map)
                        vec3 across = normalize(abs(mirror.y) < 0.99 ? cross(mirror, vec3(0.0, 1.0, 0.0))
                                                                     : cross(mirror, vec3(1.0, 0.0, 0.0)));
                        vec3 up = cross(mirror, across);
                        uint rays = max(uint(pass.fresnel.z), 1u);
                        float turn = 6.2831853 / float(rays) * (float((p.x & 1) + 2 * (p.y & 1)) / 4.0);
                        vec3 sum = vec3(0.0);
                        float hits = 0.0;
                        for (uint i = 0u; i < rays; ++i) {
                            float radius = sqrt((float(i) + 0.5) / float(rays)) * rough;
                            float angle = float(i) * 2.39996323 + turn;
                            vec3 d = normalize(mirror + (across * cos(angle) + up * sin(angle)) * radius);
                            if (dot(d, n) <= 0.0) {
                                d = mirror; // (below the surface: the mirror direction instead)
                            }
                            vec4 one = reflected(point + n * offset, d, t);
                            sum += one.rgb * one.a;
                            hits += one.a;
                        }
                        if (hits > 0.0) {
                            // (negative: a rough pixel - the apply pass blurs it with its rough neighbours)
                            seen = vec4(sum / hits, -(hits / float(rays)));
                        }
                    } else {
                        seen = reflected(point + n * offset, mirror, t);
                    }
                    if (seen.a != 0.0) {
                        result = vec4(seen.rgb, sign(seen.a) * clamp(abs(seen.a) * weight, 0.0, 1.0));
                    }
                }
            }
        }
    }
    imageStore(o_out, local, result);
    imageStore(o_base, local, base_layer);
}
)";

// Step 2: the colour mixed toward the reflection by its weight - premultiplied: (base layer + reflection x weight) +
// colour x (1 - the base's share - the weight), the base layer the masked glass's own colour; the pixels with neither
// discarded.
constexpr const char* kApplyBody = R"(
layout(push_constant) uniform Apply { ivec4 rect; } apply;
layout(set = 0, binding = 4, rgba16f) uniform readonly image2D u_in;
layout(set = 0, binding = 8, rgba16f) uniform readonly image2D u_base;
layout(location = 0) out vec4 o_colour;

void main()
{
    ivec2 local = ivec2(gl_FragCoord.xy) - apply.rect.xy;
    if (any(lessThan(local, ivec2(0))) || any(greaterThanEqual(local, apply.rect.zw))) {
        discard;
    }
    vec4 reflection = imageLoad(u_in, local);
    vec4 base = imageLoad(u_base, local);
    if (!(abs(reflection.a) > 0.0) && !(base.a > 0.0)) {
        discard;
    }
    vec3 colour = reflection.rgb;
    if (reflection.a < 0.0) {
        // a rough pixel (its weight negative): its few rays' noise blurred with its rough neighbours' (a 3 x 3 tent,
        // each by its weight); a mirror pixel is taken as it is
        vec3 sum = vec3(0.0);
        float total = 0.0;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                ivec2 q = clamp(local + ivec2(dx, dy), ivec2(0), apply.rect.zw - ivec2(1));
                vec4 other = imageLoad(u_in, q);
                if (other.a < 0.0) {
                    float k = float((2 - abs(dx)) * (2 - abs(dy))) * -other.a;
                    sum += other.rgb * k;
                    total += k;
                }
            }
        }
        colour = sum / total;
    }
    float weight = clamp(abs(reflection.a), 0.0, 1.0);
    o_colour = vec4(max(base.rgb, vec3(0.0)) + clamp(colour, 0.0, 1.0) * weight, clamp(base.a + weight, 0.0, 1.0));
}
)";

// The check's trace: each ray's hit and what the geometry table gives for it.
constexpr const char* kSurfaceBody = R"(
layout(local_size_x = 64) in;
layout(set = 0, binding = 0) uniform accelerationStructureEXT u_scene;
struct Ray { vec4 origin_t_max; vec4 direction; uvec4 mask; };
struct Hit { vec4 position_t; vec4 colour; vec2 uv; vec2 barycentrics; uint instance; uint primitive; uint material;
             uint flags; };
layout(set = 0, binding = 5, scalar) readonly buffer Rays { Ray rays[]; };
layout(set = 0, binding = 6, scalar) writeonly buffer Hits { Hit hits[]; };
layout(push_constant) uniform Count { uint count; } given;

void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= given.count) {
        return;
    }
    Ray ray = rays[i];
    rayQueryEXT query;
    rayQueryInitializeEXT(query, u_scene, gl_RayFlagsOpaqueEXT, ray.mask.x & 0xFFu, ray.origin_t_max.xyz, 0.0,
                          ray.direction.xyz, ray.origin_t_max.w);
    while (rayQueryProceedEXT(query)) {
    }
    Hit hit;
    hit.position_t = vec4(0.0);
    hit.colour = vec4(0.0);
    hit.uv = vec2(0.0);
    hit.barycentrics = vec2(0.0);
    hit.instance = 0u;
    hit.primitive = 0u;
    hit.material = 0u;
    hit.flags = 0u;
    if (rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionTriangleEXT) {
        hit.instance = rayQueryGetIntersectionInstanceIdEXT(query, true);
        hit.primitive = rayQueryGetIntersectionPrimitiveIndexEXT(query, true);
        hit.material = rayQueryGetIntersectionInstanceCustomIndexEXT(query, true);
        hit.barycentrics = rayQueryGetIntersectionBarycentricsEXT(query, true);
        HitVertices v;
        bool live = hit_vertices(hit.instance, hit.primitive, v);
        hit.flags = 0x80000000u | (live ? 0x40000000u : 0u) | v.flags;
        mat4x3 to_world = rayQueryGetIntersectionObjectToWorldEXT(query, true);
        vec2 b = hit.barycentrics;
        vec3 object = v.position[0] * (1.0 - b.x - b.y) + v.position[1] * b.x + v.position[2] * b.y;
        hit.position_t = vec4(to_world * vec4(object, 1.0), rayQueryGetIntersectionTEXT(query, true));
        hit.uv = hit_uv(v, b);
        hit.colour = hit_colour(v, b);
    }
    hits[i] = hit;
}
)";

constexpr uint32_t kSurfaceHitFlag = 0x80000000U;
constexpr uint32_t kSurfaceLiveFlag = 0x40000000U;
constexpr uint32_t kGeometryFlagMask = 0xFFFFU;

struct PackedSurfaceRay {
    std::array<float, 4> origin_t_max{};
    std::array<float, 4> direction{};
    std::array<uint32_t, 4> mask{};
};
struct PackedSurfaceHit {
    std::array<float, 4> position_t{};
    std::array<float, 4> colour{};
    std::array<float, 2> uv{};
    std::array<float, 2> barycentrics{};
    uint32_t instance = 0;
    uint32_t primitive = 0;
    uint32_t material = 0;
    uint32_t flags = 0;
};
constexpr std::size_t kPackedSurfaceHitBytes = 64;
static_assert(sizeof(PackedSurfaceHit) == kPackedSurfaceHitBytes, "the shader's Hit (scalar layout)");

// The trace pass's block (std140), as the shader lays it out.
struct TraceBlock {
    std::array<float, kMatrixFloats> to_world{}; // column-major (GLSL's mat4)
    std::array<float, 4> camera{};
    std::array<float, 4> params{};
    std::array<int32_t, 4> rect{};
    std::array<int32_t, 4> extra{};
    std::array<float, 4> ambient{};
    std::array<std::array<float, 4>, kMaxLights> light_direction{};
    std::array<std::array<float, 4>, kMaxLights> light_colour{};
    std::array<float, 4> fresnel{};
    // (added by the sky)
    std::array<float, kMatrixFloats> sky_to_pixel{};
    std::array<float, 4> sky_sun{};
    std::array<float, 4> sky_low{};
    std::array<float, 4> sky_rect{};
    std::array<float, 4> sky_tint{};
    std::array<float, 4> surface{}; // (added for the surface reflections)
};
constexpr std::size_t kTraceBlockBytes = 432;
static_assert(sizeof(TraceBlock) == kTraceBlockBytes, "the shader's Pass block (std140)");

// The bindings of set 0 (push descriptors): 0 the scene, 1 the depth, 2 the reflection written, 3 the pass's block,
// 4 the reflection read (the apply), 5 / 6 the check's rays and hits, 7 the geometry table; set 1: the material table.
constexpr uint32_t kBindingScene = 0;
constexpr uint32_t kBindingDepth = 1;
constexpr uint32_t kBindingOut = 2;
constexpr uint32_t kBindingBlock = 3;
constexpr uint32_t kBindingIn = 4;
constexpr uint32_t kBindingRays = 5;
constexpr uint32_t kBindingHits = 6;
constexpr uint32_t kBindingGeometry = 7;
constexpr uint32_t kBindingBase = 8; // (added for the glass) the base layer: written by the trace, read by the apply
// (added by the sky) 9 - 12: what the misses see with rt_sky (vulkan_rt_sky_pass.h rt_sky_rays_glsl): the sky-view table
// and the transmittance (samplers), the clouds' light and info (storage images)
constexpr uint32_t kBindingSky = 9;
constexpr uint32_t kBindingSkyCloudLight = kBindingSky + 2;
constexpr uint32_t kBindingSkyCloudInfo = kBindingSky + 3;
constexpr uint32_t kBindingCount = 13;

constexpr VkShaderStageFlags kStages = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
constexpr uint32_t kPushBytes = 16; // the apply's rect, the check's count

using Matrix = std::array<std::array<double, kMatrixSide>, kMatrixSide>;

// A pipeline for a colour format and sample count (the apply pass).
struct ApplyPipeline {
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkPipeline pipeline = VK_NULL_HANDLE;
};

// A viewport's image.
struct ViewSlot {
    std::array<uint32_t, 4> rect{};
    Extent target;
    Image reflection;
    Image base; // (added for the glass) masked glass's own colour, premultiplied by its share (kBindingBase)
    bool fresh = true; // made, not moved to GENERAL yet
    uint64_t last_use = 0;
};

struct ReflectionState {
    RtReflectionSettings settings;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;     // set 0 + the material table's set 1
    VkPipelineLayout surface_layout = VK_NULL_HANDLE; // set 0 only (the check's trace)
    VkDescriptorSetLayout material_layout = VK_NULL_HANDLE; // the table's when `layout` was made
    VkSampler sampler = VK_NULL_HANDLE;
    VkShaderModule vertex = VK_NULL_HANDLE;
    std::array<VkPipeline, 4> trace{}; // single-sampled depth, multisampled; then the same with the sky (SKY)
    VkPipeline surface = VK_NULL_HANDLE;
    std::vector<ApplyPipeline> applies;
    std::vector<std::unique_ptr<ViewSlot>> slots;
    bool standard_samples = false;
    bool failed = false;
    uint64_t passes = 0;
    uint64_t without_mirrors = 0;
    uint64_t uses = 0;
};

ReflectionState& reflections()
{
    static ReflectionState state;
    return state;
}

bool first_report(Backend& state, uint64_t what) { return state.reported.insert(what).second; }

// The listener rt_scene tells when a view's opaque world is drawn.
class ReflectionListener final : public rt::ViewListener {
public:
    [[nodiscard]] int order() const override { return rt::ViewOrder::kReflections; }
    void opaque_drawn(const rt::ViewDrawn& view) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (state.device_made && state.draws_made) {
            record_rt_reflection_pass(state, view);
        }
    }
};

ReflectionListener& listener()
{
    static ReflectionListener instance;
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

// A compute pass's GLSL: the version, the defines, the ray-query extension, the material table and the geometry table
// where it shades hits, its body.
std::string compute_source(const char* body, bool multisampled, bool materials, bool sky = false)
{
    std::string source = "#version 460\n";
    if (multisampled) {
        source += "#define MULTISAMPLED\n";
    }
    if (sky) {
        source += "#define SKY\n" + rt_sky_rays_glsl(kBindingSky);
    }
    source += "#extension GL_EXT_ray_query : require\n";
    if (materials) {
        source += "#define THUG2_MATERIAL_SET 1\n";
        source += material_glsl();
    }
    source += "#define THUG2_GEOMETRY_SET 0\n#define THUG2_GEOMETRY_BINDING 7\n";
    source += scene_geometry_glsl();
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

// Set 0's layout, the sampler and the check's pipeline layout (once).
bool ensure_layouts(Backend& state, std::string* error)
{
    ReflectionState& own = reflections();
    if (own.set_layout != VK_NULL_HANDLE) {
        return true;
    }
    const VkDevice device = state.context.device();
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(state.context.physical_device(), &properties);
    own.standard_samples = properties.limits.standardSampleLocations == VK_TRUE;
    std::array<VkDescriptorSetLayoutBinding, kBindingCount> bindings{};
    for (uint32_t binding = 0; binding < kBindingCount; ++binding) {
        VkDescriptorType type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        if (binding == kBindingScene) {
            type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        } else if (binding == kBindingDepth || binding == kBindingSky || binding == kBindingSky + 1) {
            type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        } else if (binding == kBindingOut || binding == kBindingIn || binding == kBindingBase ||
                   binding == kBindingSkyCloudLight || binding == kBindingSkyCloudInfo) {
            type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        } else if (binding == kBindingBlock) {
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
                                                   .bindingCount = static_cast<uint32_t>(bindings.size()),
                                                   .pBindings = bindings.data()};
    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_NEAREST;
    sampler_info.minFilter = VK_FILTER_NEAREST;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.maxLod = 0.0F;
    if (vkCreateDescriptorSetLayout(device, &set_info, nullptr, &own.set_layout) != VK_SUCCESS) {
        own.set_layout = VK_NULL_HANDLE;
        *error = "the descriptor layout";
        return false;
    }
    if (vkCreateSampler(device, &sampler_info, nullptr, &own.sampler) != VK_SUCCESS) {
        own.sampler = VK_NULL_HANDLE;
        *error = "the sampler";
        return false;
    }
    const VkPushConstantRange constants{.stageFlags = kStages, .offset = 0, .size = kPushBytes};
    const VkPipelineLayoutCreateInfo layout_info{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                 .pNext = nullptr,
                                                 .flags = 0,
                                                 .setLayoutCount = 1,
                                                 .pSetLayouts = &own.set_layout,
                                                 .pushConstantRangeCount = 1,
                                                 .pPushConstantRanges = &constants};
    if (vkCreatePipelineLayout(device, &layout_info, nullptr, &own.surface_layout) != VK_SUCCESS) {
        own.surface_layout = VK_NULL_HANDLE;
        *error = "the check's pipeline layout";
        return false;
    }
    return true;
}

// The pass's layouts, the vertex shader and the trace pipelines (once; the material table must be ready).
bool ensure_objects(Backend& state, std::string* error)
{
    ReflectionState& own = reflections();
    if (own.trace.back() != VK_NULL_HANDLE) {
        return true;
    }
    if (!ensure_layouts(state, error)) {
        return false;
    }
    const VkDevice device = state.context.device();
    if (own.layout == VK_NULL_HANDLE) {
        own.material_layout = state.materials.layout();
        const std::array<VkDescriptorSetLayout, 2> sets{own.set_layout, own.material_layout};
        const VkPushConstantRange constants{.stageFlags = kStages, .offset = 0, .size = kPushBytes};
        const VkPipelineLayoutCreateInfo layout_info{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                     .pNext = nullptr,
                                                     .flags = 0,
                                                     .setLayoutCount = static_cast<uint32_t>(sets.size()),
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
    for (std::size_t variant = 0; variant < own.trace.size(); ++variant) {
        if (core::at(own.trace, variant) == VK_NULL_HANDLE) {
            core::at(own.trace, variant) = make_compute(
                device, own.layout, compute_source(kTraceBody, (variant & 1U) != 0, true, variant >= 2), error);
            if (core::at(own.trace, variant) == VK_NULL_HANDLE) {
                return false;
            }
        }
    }
    return true;
}

// The apply pass's pipeline for a colour target (made on first use).
VkPipeline apply_pipeline(Backend& state, VkFormat format, VkSampleCountFlagBits samples, std::string* error)
{
    ReflectionState& own = reflections();
    for (const ApplyPipeline& made : own.applies) {
        if (made.format == format && made.samples == samples) {
            return made.pipeline;
        }
    }
    const VkDevice device = state.context.device();
    const VkShaderModule fragment =
        make_module(device, RtShaderStage::kFragment, std::string("#version 460\n") + kApplyBody, error);
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
    // colour toward the reflection by its weight: src (premultiplied) + (dst x (1 - a)); alpha kept
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; // (premultiplied: the shader writes the reflection x its weight)
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
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

// The image a surface draws into (a texture's level 0, a render target's, a depth surface's) - as R5's pass finds it.
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

// The matrix (column vectors) that takes world space to (pixel x, pixel y, depth, 1) in the target's pixels: view x
// projection (their product transposed: Direct3D's row vectors), then clip space to Direct3D 9's pixels (centres at
// whole coordinates, y down) and the viewport's depth range.
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
    Matrix out{};
    for (std::size_t row = 0; row < kMatrixSide; ++row) {
        for (std::size_t column = 0; column < kMatrixSide; ++column) {
            double sum = 0.0;
            for (std::size_t k = 0; k < kMatrixSide; ++k) {
                // (clip_from_world is view_projection transposed)
                sum += core::at(core::at(pixel_from_clip, row), k) * core::at(core::at(view_projection, column), k);
            }
            core::at(core::at(out, row), column) = sum;
        }
    }
    return out;
}

std::array<double, 3> apply(const Matrix& matrix, const std::array<double, 4>& point)
{
    std::array<double, 4> out{};
    for (std::size_t row = 0; row < kMatrixSide; ++row) {
        double sum = 0.0;
        for (std::size_t k = 0; k < kMatrixSide; ++k) {
            sum += core::at(core::at(matrix, row), k) * core::at(point, k);
        }
        core::at(out, row) = sum;
    }
    const double w = std::abs(out.back()) > kSingular ? out.back() : 1.0;
    return {out.at(0) / w, out.at(1) / w, out.at(2) / w};
}

// The angle one pixel spans at the viewport's centre (radians): the footprint's growth along a ray.
float pixel_angle(const Matrix& to_world, const rt::ViewDrawn& view)
{
    const double x = core::at(view.rect, 0) + (core::at(view.rect, 2) / 2.0);
    const double y = core::at(view.rect, 1) + (core::at(view.rect, 3) / 2.0);
    constexpr double kDepth = 0.5;
    const std::array<double, 3> eye{core::at(view.camera.position, 0), core::at(view.camera.position, 1),
                                    core::at(view.camera.position, 2)};
    const std::array<double, 3> one = apply(to_world, {x, y, kDepth, 1.0});
    const std::array<double, 3> next = apply(to_world, {x + 1.0, y, kDepth, 1.0});
    double dot = 0.0;
    double length_one = 0.0;
    double length_next = 0.0;
    for (std::size_t k = 0; k < 3; ++k) {
        const double towards_one = core::at(one, k) - core::at(eye, k);
        const double towards_next = core::at(next, k) - core::at(eye, k);
        dot += towards_one * towards_next;
        length_one += towards_one * towards_one;
        length_next += towards_next * towards_next;
    }
    if (!(length_one > 0.0) || !(length_next > 0.0)) {
        return 0.0F;
    }
    const double cosine = dot / std::sqrt(length_one * length_next);
    return static_cast<float>(std::acos(std::clamp(cosine, -1.0, 1.0)));
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

// Where sample 0 of a multisampled target is, from the pixel's centre (Vulkan's standard sample locations; the centre
// when the device doesn't use them, or for one sample) - as R5's pass reads the depth.
std::array<float, 2> sample_zero_offset(VkSampleCountFlagBits samples, bool standard)
{
    if (!standard) {
        return {0.0F, 0.0F};
    }
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

// IEEE half to float (the read-back of the 16-bit image).
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

// --- the viewports' images ---------------------------------------------------------------------------------------

// The slot for a viewport (made, or made again when the size changed; the least recently used one dropped when all are
// taken); null when its image can't be made.
ViewSlot* slot_for(Backend& state, const std::array<uint32_t, 4>& rect, const Extent& target)
{
    ReflectionState& own = reflections();
    ++own.uses;
    for (const std::unique_ptr<ViewSlot>& slot : own.slots) {
        if (slot->rect == rect && slot->target.width == target.width && slot->target.height == target.height) {
            slot->last_use = own.uses;
            return slot.get();
        }
    }
    if (own.slots.size() >= kViewSlots) {
        const auto oldest = std::ranges::min_element(
            own.slots, {}, [](const std::unique_ptr<ViewSlot>& slot) { return slot->last_use; });
        retire_image(state, &(*oldest)->reflection);
        retire_image(state, &(*oldest)->base);
        own.slots.erase(oldest);
    }
    auto made_slot = std::make_unique<ViewSlot>();
    made_slot->rect = rect;
    made_slot->target = target;
    made_slot->last_use = own.uses;
    const ImageRequest request{.format = VK_FORMAT_R16G16B16A16_SFLOAT,
                               .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                               .extent = {.width = core::at(rect, 2), .height = core::at(rect, 3)},
                               .levels = 1,
                               .samples = VK_SAMPLE_COUNT_1_BIT,
                               // (sampled too: the memory's images get their view for it)
                               .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                               .alpha_one = false};
    if (!state.memory.create_image(request, &made_slot->reflection) ||
        !state.memory.create_image(request, &made_slot->base)) {
        state.memory.destroy_image(&made_slot->reflection);
        state.memory.destroy_image(&made_slot->base);
        if (first_report(state, kReportImages)) {
            get_log("vulkan: rt_reflections: the viewport's image (%ux%u) couldn't be made - the pass skipped there",
                    core::at(rect, 2), core::at(rect, 3));
        }
        return nullptr;
    }
    made_slot->reflection.resting = VK_IMAGE_LAYOUT_GENERAL; // (storage images: written and read in place)
    made_slot->base.resting = VK_IMAGE_LAYOUT_GENERAL;
    own.slots.push_back(std::move(made_slot));
    return own.slots.back().get();
}

// A new image moved to GENERAL (every texel is written by the trace before anything reads it).
void prepare_slot(VkCommandBuffer commands, ViewSlot* slot)
{
    VkImageMemoryBarrier barrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                       .pNext = nullptr,
                                       .srcAccessMask = 0,
                                       .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                       .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                       .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                                       .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                       .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                       .image = slot->reflection.image,
                                       .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                            .baseMipLevel = 0,
                                                            .levelCount = 1,
                                                            .baseArrayLayer = 0,
                                                            .layerCount = 1}};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &barrier);
    barrier.image = slot->base.image;
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &barrier);
    slot->reflection.layout = VK_IMAGE_LAYOUT_GENERAL;
    slot->base.layout = VK_IMAGE_LAYOUT_GENERAL;
    slot->fresh = false;
}

// The depth moved to be sampled by the compute pass (from whatever wrote it) and back to its resting layout after.
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

// The trace's descriptors: the scene, the depth, the image written, the block and the geometry table.
void push_trace_descriptors(VkCommandBuffer commands, VkAccelerationStructureKHR tlas, const Image* depth,
                            const Image* out, const Image* base, const RingSpan& block, const SceneView& scene,
                            const RtSkyForRays* sky)
{
    const ReflectionState& own = reflections();
    const VkWriteDescriptorSetAccelerationStructureKHR structure{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
        .pNext = nullptr,
        .accelerationStructureCount = 1,
        .pAccelerationStructures = &tlas};
    const VkDescriptorImageInfo depth_info{.sampler = own.sampler,
                                           .imageView = depth->view,
                                           .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo out_info{
        .sampler = VK_NULL_HANDLE, .imageView = out->view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo base_info{
        .sampler = VK_NULL_HANDLE, .imageView = base->view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorBufferInfo block_info{.buffer = block.buffer, .offset = block.offset, .range = sizeof(TraceBlock)};
    const VkDescriptorBufferInfo geometry_info{.buffer = scene.geometry, .offset = 0, .range = scene.geometry_bytes};
    const auto write = [](uint32_t binding, VkDescriptorType type) {
        VkWriteDescriptorSet one{};
        one.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        one.dstBinding = binding;
        one.descriptorCount = 1;
        one.descriptorType = type;
        return one;
    };
    std::array writes{
        write(kBindingScene, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR),
        write(kBindingDepth, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
        write(kBindingOut, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        write(kBindingBlock, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER),
        write(kBindingGeometry, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        write(kBindingBase, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
    };
    writes.at(0).pNext = &structure;
    writes.at(1).pImageInfo = &depth_info;
    writes.at(2).pImageInfo = &out_info;
    writes.at(3).pBufferInfo = &block_info;
    writes.at(4).pBufferInfo = &geometry_info;
    writes.back().pImageInfo = &base_info;
    vkCmdPushDescriptorSetKHR(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.layout, 0,
                              static_cast<uint32_t>(writes.size()), writes.data());
    if (sky != nullptr) {
        // (added by the sky) the misses' sky
        std::array sky_writes{
            write(kBindingSky, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
            write(kBindingSky + 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
            write(kBindingSkyCloudLight, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            write(kBindingSkyCloudInfo, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        };
        sky_writes.at(0).pImageInfo = &sky->sky_view;
        sky_writes.at(1).pImageInfo = &sky->transmittance;
        sky_writes.at(2).pImageInfo = &sky->cloud_light;
        sky_writes.at(3).pImageInfo = &sky->cloud_info;
        vkCmdPushDescriptorSetKHR(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.layout, 0,
                                  static_cast<uint32_t>(sky_writes.size()), sky_writes.data());
    }
}

// The colour mixed toward the reflection over the viewport.
void record_apply(Backend& state, VkCommandBuffer commands, VkPipeline pipeline, Image* colour, const Image* reflection,
                  const Image* base, const std::array<int32_t, 4>& rect)
{
    (void)state;
    const ReflectionState& own = reflections();
    Memory::transition(commands, colour, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true);
    VkRenderingAttachmentInfo colour_info{};
    colour_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colour_info.imageView = colour->attachment_view;
    colour_info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colour_info.resolveMode = VK_RESOLVE_MODE_NONE;
    colour_info.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colour_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    const VkRect2D area{.offset = {.x = core::at(rect, 0), .y = core::at(rect, 1)},
                        .extent = {.width = static_cast<uint32_t>(core::at(rect, 2)),
                                   .height = static_cast<uint32_t>(core::at(rect, 3))}};
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
    const VkDescriptorImageInfo in_info{
        .sampler = VK_NULL_HANDLE, .imageView = reflection->view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo base_info{
        .sampler = VK_NULL_HANDLE, .imageView = base->view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
    std::array<VkWriteDescriptorSet, 2> writes{};
    for (VkWriteDescriptorSet& write : writes) {
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    }
    writes.front().dstBinding = kBindingIn;
    writes.front().pImageInfo = &in_info;
    writes.back().dstBinding = kBindingBase;
    writes.back().pImageInfo = &base_info;
    vkCmdPushDescriptorSetKHR(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, own.layout, 0,
                              static_cast<uint32_t>(writes.size()), writes.data());
    vkCmdPushConstants(commands, own.layout, kStages, 0, kPushBytes, rect.data());
    vkCmdDraw(commands, 3, 1, 0, 0);
    vkCmdEndRendering(commands);
}

// The pass's block: the matrices, the settings and the frame record's lights.
TraceBlock trace_block(const Matrix& to_world, const rt::ViewDrawn& view, const std::array<int32_t, 4>& rect,
                       const std::array<float, 2>& sample, float angle)
{
    const ReflectionState& own = reflections();
    TraceBlock block;
    block.to_world = column_major(to_world);
    block.camera = {core::at(view.camera.position, 0), core::at(view.camera.position, 1),
                    core::at(view.camera.position, 2), sample.back()};
    block.params = {own.settings.strength, kReflectionDistance, kOffsetScale, sample.front()};
    block.rect = rect;
    std::size_t lights = 0;
    for (const rt::Light& light : rt::frame_record().lights) {
        if (light.kind == rt::Light::Kind::kAmbient) {
            block.ambient = {core::at(light.colour, 0), core::at(light.colour, 1), core::at(light.colour, 2), 0.0F};
        } else if (light.kind == rt::Light::Kind::kDirectional && lights < kMaxLights) {
            core::at(block.light_direction, lights) = {core::at(light.direction, 0), core::at(light.direction, 1),
                                                       core::at(light.direction, 2), 0.0F};
            core::at(block.light_colour, lights) = {core::at(light.colour, 0), core::at(light.colour, 1),
                                                    core::at(light.colour, 2), 0.0F};
            ++lights;
        }
    }
    block.ambient.back() = angle;
    block.extra = {static_cast<int32_t>(kReflectionMask), static_cast<int32_t>(lights), 0, 0};
    block.fresnel = {own.settings.fresnel ? 1.0F : 0.0F, own.settings.glass_roughness, static_cast<float>(kRoughRays),
                     own.settings.glass_reflectance};
    block.surface = {own.settings.surfaces, kSurfaceRoughest, kSurfaceConePerAlpha, 0.0F};
    return block;
}

} // namespace

void set_rt_reflection_settings(const RtReflectionSettings& settings)
{
    RtReflectionSettings kept = settings;
    kept.strength = std::isfinite(kept.strength) ? std::clamp(kept.strength, 0.0F, kMaxStrength) : 0.0F;
    kept.glass_roughness =
        std::isfinite(kept.glass_roughness) ? std::clamp(kept.glass_roughness, 0.0F, kMaxRoughness) : 0.0F;
    kept.glass_reflectance =
        std::isfinite(kept.glass_reflectance) ? std::clamp(kept.glass_reflectance, 0.0F, 1.0F) : kRtGlassReflectance;
    kept.surfaces = std::isfinite(kept.surfaces) ? std::clamp(kept.surfaces, 0.0F, kMaxSurfaces) : 0.0F;
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        reflections().settings = kept;
    }
    if (kept.on) {
        rt::add_view_listener(&listener());
    } else {
        rt::remove_view_listener(&listener());
    }
}

RtReflectionSettings rt_reflection_settings()
{
    const std::scoped_lock lock(backend().mutex);
    return reflections().settings;
}

VkImageUsageFlags rt_reflections_depth_usage(const Backend& state, VkFormat format)
{
    if (!reflections().settings.on || !state.context.ray_tracing_enabled()) {
        return 0;
    }
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(state.context.physical_device(), format, &properties);
    return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0 ? VK_IMAGE_USAGE_SAMPLED_BIT
                                                                                         : 0;
}

bool record_rt_reflection_pass(Backend& state, const rt::ViewDrawn& view)
{
    ReflectionState& own = reflections();
    if (!own.settings.on || own.failed) {
        return false;
    }
    if (!state.context.ray_tracing_enabled()) {
        if (first_report(state, kReportNoRayTracing)) {
            get_log("vulkan: rt_reflections=1 but ray tracing isn't enabled ([Graphics] ray_tracing=1, and a device "
                    "with VK_KHR_ray_query) - no ray-traced reflections");
        }
        return false;
    }
    const SceneView scene = current_scene(state);
    if (scene.tlas == VK_NULL_HANDLE || scene.geometry == VK_NULL_HANDLE) {
        if (first_report(state, kReportNoScene)) {
            get_log("vulkan: rt_reflections: no acceleration structure for the scene yet - the reflection pass skipped");
        }
        return false;
    }
    if (scene.reflective_instances == 0) {
        ++own.without_mirrors;
        if (first_report(state, kReportNoMirrors)) {
            get_log("vulkan: rt_reflections: nothing reflective in the scene (no environment-mapped material, none glossy by its surface map) - the "
                    "reflection pass skipped while that lasts");
        }
        return false;
    }
    if (!state.materials.ready() || state.materials.descriptor_set() == VK_NULL_HANDLE) {
        if (first_report(state, kReportNoMaterials)) {
            get_log("vulkan: rt_reflections: no material table - the reflection pass skipped");
        }
        return false;
    }
    Image* const colour = image_of_surface(core::at(state.targets, 0));
    Image* const depth = image_of_surface(state.depth);
    if (colour == nullptr || depth == nullptr || colour->attachment_view == VK_NULL_HANDLE ||
        (colour->aspect & VK_IMAGE_ASPECT_COLOR_BIT) == 0 || (depth->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) == 0 ||
        (depth->usage & VK_IMAGE_USAGE_SAMPLED_BIT) == 0 || depth->view == VK_NULL_HANDLE ||
        depth->samples != colour->samples || depth->extent.width < colour->extent.width ||
        depth->extent.height < colour->extent.height) {
        if (first_report(state, kReportTargets)) {
            get_log("vulkan: rt_reflections: the bound targets can't be read (no samplable depth of the colour's size "
                    "and samples) - the reflection pass skipped there");
        }
        return false;
    }
    std::string error;
    const VkPipeline apply =
        ensure_objects(state, &error) ? apply_pipeline(state, colour->format, colour->samples, &error) : VK_NULL_HANDLE;
    if (apply == VK_NULL_HANDLE) {
        own.failed = true;
        if (first_report(state, kReportPipeline)) {
            get_log("vulkan: rt_reflections: the passes couldn't be made (%s) - no ray-traced reflections",
                    error.c_str());
        }
        return false;
    }
    if (own.material_layout != state.materials.layout()) {
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
    Matrix to_world{};
    if (!invert(world_to_pixel(clipped), &to_world)) {
        return false;
    }
    ViewSlot* const slot = slot_for(state, clipped.rect, colour->extent);
    if (slot == nullptr) {
        return false;
    }
    const std::array<int32_t, 4> rect{static_cast<int32_t>(left), static_cast<int32_t>(top),
                                      static_cast<int32_t>(width), static_cast<int32_t>(height)};
    TraceBlock block = trace_block(to_world, view, rect, sample_zero_offset(colour->samples, own.standard_samples),
                                   pixel_angle(to_world, clipped));
    // (added by the sky) rt_sky: the misses see the sky drawn, not the game's environment map
    RtSkyForRays sky;
    const bool sky_seen = rt_sky_for_rays(state, clipped.rect, &sky);
    if (sky_seen) {
        block.sky_to_pixel = sky.to_pixel;
        block.sky_sun = sky.sun;
        block.sky_low = sky.low;
        block.sky_rect = sky.rect;
        block.sky_tint = sky.tint;
    }
    std::array<uint8_t, sizeof(TraceBlock)> block_bytes{};
    std::memcpy(block_bytes.data(), &block, sizeof(block));
    const std::optional<RingSpan> block_span = state.ring.copy(block_bytes, kUniformAlignment);
    if (!block_span) {
        return false;
    }

    VkCommandBuffer commands = outside_pass(state);
    if (slot->fresh) {
        prepare_slot(commands, slot);
    }
    // the scene's build (recorded earlier on this queue), the block's write and the last frame's reads of the image
    const VkMemoryBarrier ready{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                .pNext = nullptr,
                                .srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR |
                                                 VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT |
                                                 VK_ACCESS_SHADER_READ_BIT,
                                .dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR |
                                                 VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
                                                 VK_ACCESS_UNIFORM_READ_BIT};
    vkCmdPipelineBarrier(commands,
                         VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &ready, 0, nullptr, 0, nullptr);
    depth_barrier(commands, depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true);

    // 1. the rays
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE,
                      core::at(own.trace, (colour->samples != VK_SAMPLE_COUNT_1_BIT ? 1U : 0U) + (sky_seen ? 2U : 0U)));
    push_trace_descriptors(commands, scene.tlas, depth, &slot->reflection, &slot->base, *block_span, scene,
                           sky_seen ? &sky : nullptr);
    const VkDescriptorSet material_set = state.materials.descriptor_set();
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.layout, kMaterialSet, 1, &material_set, 0,
                            nullptr);
    vkCmdDispatch(commands, (width + kGroupSide - 1) / kGroupSide, (height + kGroupSide - 1) / kGroupSide, 1);
    const VkMemoryBarrier written{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                  .pNext = nullptr,
                                  .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                  .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1,
                         &written, 0, nullptr, 0, nullptr);
    depth_barrier(commands, depth, depth->resting, false);

    // 2. the colour mixed
    record_apply(state, commands, apply, colour, &slot->reflection, &slot->base, rect);
    if (colour->layout != colour->resting) {
        Memory::transition(commands, colour, colour->resting);
    }
    state.bound = BoundState{};
    const uint64_t serial = state.frames.serial();
    colour->used = serial;
    depth->used = serial;
    slot->reflection.used = serial;
    slot->base.used = serial;
    ++own.passes;
    if (first_report(state, kReportFirstPass)) {
        get_log("vulkan: rt_reflections: the first reflection pass (%ux%u, %u reflective instances of %u, strength "
                "%.2f, Fresnel %s, glass roughness %.3f, surfaces x%.2f)",
                width, height, scene.reflective_instances, scene.instances,
                static_cast<double>(own.settings.strength), own.settings.fresnel ? "on" : "off",
                static_cast<double>(own.settings.glass_roughness), static_cast<double>(own.settings.surfaces));
    }
    return true;
}

uint64_t rt_reflection_passes() { return reflections().passes; }

uint64_t rt_reflection_views_without_mirrors() { return reflections().without_mirrors; }

bool read_rt_reflection_image(Backend& state, const std::array<uint32_t, 4>& rect, RtReflectionTexels* out)
{
    *out = RtReflectionTexels{};
    for (const std::unique_ptr<ViewSlot>& slot : reflections().slots) {
        if (slot->rect != rect || slot->fresh) {
            continue;
        }
        if (!finish_work(state)) {
            return false;
        }
        Image* const image = &slot->reflection;
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

TraceResult trace_surfaces_now(std::span<const SurfaceRay> rays, std::vector<SurfaceHit>* hits)
{
    hits->clear();
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    if (!state.context.ray_tracing_enabled() || rt::ray_tracing() == nullptr) {
        return TraceResult::kNoRayTracing;
    }
    const SceneView scene = current_scene(state);
    if (scene.tlas == VK_NULL_HANDLE || scene.geometry == VK_NULL_HANDLE) {
        return TraceResult::kNoScene;
    }
    if (rays.empty()) {
        return TraceResult::kTraced;
    }
    ReflectionState& own = reflections();
    std::string error;
    if (own.surface == VK_NULL_HANDLE) {
        if (!ensure_layouts(state, &error)) {
            get_log("vulkan: rt_reflections: the check's trace couldn't be made (%s)", error.c_str());
            return TraceResult::kFailed;
        }
        own.surface = make_compute(state.context.device(), own.surface_layout,
                                   compute_source(kSurfaceBody, false, false), &error);
        if (own.surface == VK_NULL_HANDLE) {
            get_log("vulkan: rt_reflections: the check's trace couldn't be made (%s)", error.c_str());
            return TraceResult::kFailed;
        }
    }
    // the frame's work (the TLAS's build among it) run first; the table stays the current frame's
    if (!finish_work(state)) {
        return TraceResult::kFailed;
    }
    DeviceBuffer ray_buffer;
    DeviceBuffer hit_buffer;
    const VkDeviceSize ray_bytes = rays.size() * sizeof(PackedSurfaceRay);
    const VkDeviceSize hit_bytes = rays.size() * sizeof(PackedSurfaceHit);
    if (!state.memory.create_buffer(ray_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, BufferPlace::kUpload, &ray_buffer) ||
        !state.memory.create_buffer(hit_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, BufferPlace::kReadBack,
                                    &hit_buffer) ||
        ray_buffer.mapped == nullptr || hit_buffer.mapped == nullptr) {
        state.memory.destroy_buffer(&ray_buffer);
        state.memory.destroy_buffer(&hit_buffer);
        return TraceResult::kFailed;
    }
    {
        std::vector<PackedSurfaceRay> packed(rays.size());
        for (std::size_t i = 0; i < rays.size(); ++i) {
            const SurfaceRay& ray = core::at(rays, i);
            core::at(packed, i) = {.origin_t_max = {ray.origin.at(0), ray.origin.at(1), ray.origin.at(2), ray.t_max},
                                   .direction = {ray.direction.at(0), ray.direction.at(1), ray.direction.at(2), 0.0F},
                                   .mask = {ray.cull_mask, 0, 0, 0}};
        }
        std::memcpy(ray_buffer.mapped, packed.data(), ray_bytes);
        state.memory.flush(ray_buffer, 0, ray_bytes);
    }
    const SceneView now = current_scene(state);
    const VkWriteDescriptorSetAccelerationStructureKHR structure{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
        .pNext = nullptr,
        .accelerationStructureCount = 1,
        .pAccelerationStructures = &now.tlas};
    const VkDescriptorBufferInfo ray_info{.buffer = ray_buffer.buffer, .offset = 0, .range = ray_bytes};
    const VkDescriptorBufferInfo hit_info{.buffer = hit_buffer.buffer, .offset = 0, .range = hit_bytes};
    const VkDescriptorBufferInfo geometry_info{.buffer = now.geometry, .offset = 0, .range = now.geometry_bytes};
    std::array<VkWriteDescriptorSet, 4> writes{};
    for (VkWriteDescriptorSet& one : writes) {
        one.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        one.descriptorCount = 1;
        one.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }
    writes.at(0).dstBinding = kBindingScene;
    writes.at(0).descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    writes.at(0).pNext = &structure;
    writes.at(1).dstBinding = kBindingRays;
    writes.at(1).pBufferInfo = &ray_info;
    writes.at(2).dstBinding = kBindingHits;
    writes.at(2).pBufferInfo = &hit_info;
    writes.at(3).dstBinding = kBindingGeometry;
    writes.at(3).pBufferInfo = &geometry_info;
    const auto count = static_cast<uint32_t>(rays.size());
    const VkPipelineLayout layout = own.surface_layout;
    const VkPipeline pipeline = own.surface;
    const bool ran = state.memory.submit_now([&](VkCommandBuffer commands) {
        const VkMemoryBarrier before{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                     .pNext = nullptr,
                                     .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
                                     .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &before, 0,
                             nullptr, 0, nullptr);
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdPushDescriptorSetKHR(commands, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0,
                                  static_cast<uint32_t>(writes.size()), writes.data());
        vkCmdPushConstants(commands, layout, kStages, 0, sizeof(count), &count);
        vkCmdDispatch(commands, (count + kSurfaceGroup - 1) / kSurfaceGroup, 1, 1);
        const VkMemoryBarrier after{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                    .pNext = nullptr,
                                    .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                    .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &after, 0,
                             nullptr, 0, nullptr);
    });
    if (ran) {
        vmaInvalidateAllocation(state.memory.allocator(), hit_buffer.allocation, 0, VK_WHOLE_SIZE);
        std::vector<PackedSurfaceHit> packed(rays.size());
        std::memcpy(packed.data(), hit_buffer.mapped, hit_bytes);
        hits->reserve(packed.size());
        for (const PackedSurfaceHit& each : packed) {
            hits->push_back({.hit = (each.flags & kSurfaceHitFlag) != 0,
                             .live = (each.flags & kSurfaceLiveFlag) != 0,
                             .t = each.position_t.at(3),
                             .instance = each.instance,
                             .primitive = each.primitive,
                             .material = each.material,
                             .geometry_flags = each.flags & kGeometryFlagMask,
                             .barycentrics = each.barycentrics,
                             .uv = each.uv,
                             .colour = each.colour,
                             .position = {each.position_t.at(0), each.position_t.at(1), each.position_t.at(2)}});
        }
    }
    state.memory.destroy_buffer(&ray_buffer);
    state.memory.destroy_buffer(&hit_buffer);
    return ran ? TraceResult::kTraced : TraceResult::kFailed;
}

void release_rt_reflections(Backend& state)
{
    ReflectionState& own = reflections();
    const VkDevice device = state.context.device();
    for (const std::unique_ptr<ViewSlot>& slot : own.slots) {
        state.memory.destroy_image(&slot->reflection);
        state.memory.destroy_image(&slot->base);
    }
    own.slots.clear();
    if (device != nullptr) {
        for (const ApplyPipeline& made : own.applies) {
            vkDestroyPipeline(device, made.pipeline, nullptr);
        }
        for (const VkPipeline pipeline : {core::at(own.trace, 0), core::at(own.trace, 1), core::at(own.trace, 2),
                                          core::at(own.trace, 3), own.surface}) {
            if (pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, pipeline, nullptr);
            }
        }
        if (own.vertex != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device, own.vertex, nullptr);
        }
        for (const VkPipelineLayout layout : {own.layout, own.surface_layout}) {
            if (layout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(device, layout, nullptr);
            }
        }
        if (own.set_layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, own.set_layout, nullptr);
        }
        if (own.sampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, own.sampler, nullptr);
        }
    }
    own.applies.clear();
    own.trace = {};
    own.surface = VK_NULL_HANDLE;
    own.vertex = VK_NULL_HANDLE;
    own.layout = VK_NULL_HANDLE;
    own.surface_layout = VK_NULL_HANDLE;
    own.set_layout = VK_NULL_HANDLE;
    own.material_layout = VK_NULL_HANDLE;
    own.sampler = VK_NULL_HANDLE;
    own.failed = false;
}

} // namespace render::vulkan
