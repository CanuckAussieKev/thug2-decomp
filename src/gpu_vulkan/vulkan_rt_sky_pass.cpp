// NOT ORIGINAL - a modern sky: the atmosphere and the clouds drawn behind the world (vulkan_rt_sky_pass.h;
// docs/design/vulkan.md "The sky").
#include "gpu_vulkan/vulkan_rt_sky_pass.h"

#include "engine/gfx/wind.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "engine/gfx/rt_clouds.h"
#include "engine/gfx/rt_daylight.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/rt_sky.h"
#include "engine/gfx/rt_volume_light.h"
#include "gpu_vulkan/glsl_compiler.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_cel_draw.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_objects.h"
#include "gpu_vulkan/vulkan_ring.h"
#include "gpu_vulkan/vulkan_rt_clouds.h"
#include "gpu_vulkan/vulkan_rt_glsl.h"
#include "gpu_vulkan/vulkan_rt_shadows.h"
#include "gpu_vulkan/vulkan_rt_sky.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

constexpr uint64_t kReportTargets = 0x534B5001U;
constexpr uint64_t kReportPipeline = 0x534B5002U;
constexpr uint64_t kReportFirst = 0x534B5003U;
constexpr uint64_t kReportImages = 0x534B5004U;
constexpr uint64_t kReportNoRoom = 0x534B5005U;
constexpr uint64_t kReportNoSun = 0x534B5006U;
constexpr uint64_t kReportShadows = 0x534B5008U;
constexpr uint64_t kReportCloudMap = 0x534B5009U;

constexpr uint32_t kGroupSide = 8;
constexpr std::size_t kMatrixSide = 4;
constexpr std::size_t kMatrixFloats = kMatrixSide * kMatrixSide;
constexpr double kSingular = 1e-300;
constexpr double kPi = std::numbers::pi;
constexpr double kDegrees = kPi / 180.0;
constexpr double kGolden = 0.6180339887498949;

// The full-screen triangle at the far plane (depth 1: drawn where the world left the depth's far value).
constexpr const char* kVertexSource = R"(#version 460
void main()
{
    vec2 at = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(at * 2.0 - 1.0, 1.0, 1.0);
}
)";

// What the passes share: the block, the direction through a pixel.
constexpr const char* kCommon = R"(
layout(set = 0, binding = 6) uniform SkyBlock {
    mat4 to_world;        // the target's pixel (x, y, depth, 1) -> world
    mat4 prev_to_pixel;   // a world direction (w 0) -> the last frame's pixel
    vec4 camera;          // xyz the camera (world); w its altitude (metres)
    vec4 sun;             // xyz toward the sun; w the clouds' coverage
    vec4 sun_light;       // rgb the sun's light at the clouds; w their density scale
    vec4 ambient_bottom;  // rgb added to the sky's light on the clouds' base (the moon's glow - rt::CloudLight); w the
                          // clouds' aerial distance (metres)
    vec4 ambient_top;     // rgb added on their top; w the frame's jitter offset
    vec4 offset;          // xy the clouds' offset (metres); z this frame's share in the history; w (round PX) its rescale
    ivec4 rect;           // the viewport in the target's pixels
    ivec4 low;            // xy the clouds' images' size, z pixels per texel, w steps
    ivec4 flags;          // x the depth's samples, y clouds, z a history, w unused (round F)
    vec4 moon;            // (added by the time of day) xyz toward the moon; w its disc (0 - 1)
    vec4 stars;           // xyz toward the celestial pole; w the stars' turn about it (radians)
    vec4 night;           // x the stars (0 - 1)
    vec4 cloud_map;       // (the clouds' shadow map - rt_volume_light.h) x, z its centre (world), y the cloud base's
                          // height (world), w 1 / its width (world units)
    vec4 cloud_light;     // xyz toward the light it is made for (rt::medium_light), w how much of its shadow is kept
    vec4 sun_disc;        // (added by the physical air) rgb the sun's disc's centre radiance (rt_sky.h
                          // sun_disc_radiance), w its angular radius (0: none - rt_sun off)
    vec4 moon_light;      // (round S) rgb the moon's disc's mean radiance (rt_daylight.h DayLight::moon_radiance), w its
                          // angular radius (rt_sky.h kMoonAngularRadius)
} sky;

vec3 direction_of(vec2 p)
{
    vec4 w = sky.to_world * vec4(p, 0.5, 1.0);
    vec3 to = w.xyz / w.w - sky.camera.xyz;
    float l = length(to);
    return l > 1e-12 ? to / l : vec3(0.0, 0.0, -1.0);
}

// the centre of a low-resolution texel, in the target's pixels (centres at whole numbers)
vec2 low_centre(ivec2 q) { return vec2(sky.rect.xy) + vec2(q * sky.low.z) + 0.5 * float(sky.low.z - 1); }

float jitter_at(vec2 p) { return fract(52.9829189 * fract(0.06711056 * p.x + 0.00583715 * p.y)); }
)";

// 2. The clouds' march (low resolution): only texels with sky in their block.
constexpr const char* kMarchBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
#ifdef MULTISAMPLED
layout(set = 0, binding = 0) uniform sampler2DMS u_depth;
#else
layout(set = 0, binding = 0) uniform sampler2D u_depth;
#endif
layout(set = 0, binding = 7, rgba16f) uniform writeonly image2D o_a;
layout(set = 0, binding = 8, rgba16f) uniform writeonly image2D o_b;

void main()
{
    ivec2 q = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(q, sky.low.xy))) {
        return;
    }
    bool open = false;
    for (int dy = 0; dy < sky.low.z && !open; ++dy) {
        for (int dx = 0; dx < sky.low.z && !open; ++dx) {
            ivec2 p = sky.rect.xy + min(q * sky.low.z + ivec2(dx, dy), sky.rect.zw - ivec2(1));
            open = texelFetch(u_depth, p, 0).r >= 1.0;
        }
    }
    if (!open) {
        imageStore(o_a, q, vec4(0.0, 0.0, 0.0, 1.0));
        imageStore(o_b, q, vec4(-1.0, 0.0, 0.0, 0.0));
        return;
    }
    CloudRay ray;
    ray.camera_altitude = sky.camera.w;
    ray.offset = sky.offset.xy;
    ray.direction = direction_of(low_centre(q));
    ray.to_sun = sky.cloud_light.xyz; // (round S: the light that lights them - the sun, or the moon at night)
    ray.coverage = sky.sun.w;
    ray.density_scale = sky.sun_light.w;
    ray.steps = sky.low.w;
    ray.jitter = fract(jitter_at(vec2(q)) + sky.ambient_top.w);
    ray.sun_light = sky.sun_light.rgb;
    vec3 bottom;
    vec3 top;
    cloud_sky_light(bottom, top);
    ray.ambient_bottom = bottom + sky.ambient_bottom.rgb;
    ray.ambient_top = top + sky.ambient_top.rgb;
    CloudResult result = march_clouds(ray);
    imageStore(o_a, q, vec4(result.light, result.transmittance));
    imageStore(o_b, q, vec4(result.distance, 1.0, 0.0, 0.0));
}
)";

// 3. The temporal pass.
constexpr const char* kTemporalBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 7, rgba16f) uniform writeonly image2D o_a;
layout(set = 0, binding = 8, rgba16f) uniform writeonly image2D o_b;
layout(set = 0, binding = 9) uniform sampler2D u_hist_a;
layout(set = 0, binding = 10) uniform sampler2D u_hist_b;
layout(set = 0, binding = 11, rgba16f) uniform readonly image2D i_a;
layout(set = 0, binding = 12, rgba16f) uniform readonly image2D i_b;

void main()
{
    ivec2 q = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(q, sky.low.xy))) {
        return;
    }
    vec4 current = imageLoad(i_a, q);
    vec4 info = imageLoad(i_b, q);
    vec4 result = current;
    if (info.y > 0.5 && sky.flags.z != 0) {
        vec4 prev = sky.prev_to_pixel * vec4(direction_of(low_centre(q)), 0.0);
        if (prev.w > 1e-6) {
            vec2 pixel = prev.xy / prev.w;
            vec2 low = (pixel - vec2(sky.rect.xy) - 0.5 * float(sky.low.z - 1)) / float(sky.low.z);
            vec2 uv = (low + 0.5) / vec2(sky.low.xy);
            if (all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0))) &&
                textureLod(u_hist_b, uv, 0.0).y > 0.99) {
                vec4 history = textureLod(u_hist_a, uv, 0.0) * vec4(vec3(sky.offset.w), 1.0); // (round PX: rescaled)
                vec4 least = current;
                vec4 most = current;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        ivec2 n = clamp(q + ivec2(dx, dy), ivec2(0), sky.low.xy - ivec2(1));
                        if (imageLoad(i_b, n).y > 0.5) {
                            vec4 v = imageLoad(i_a, n);
                            least = min(least, v);
                            most = max(most, v);
                        }
                    }
                }
                result = mix(clamp(history, least, most), current, sky.offset.z);
            }
        }
    }
    imageStore(o_a, q, result);
    imageStore(o_b, q, info);
}
)";

// 4. The composite (every sample of the sky's pixels: the depth test).
constexpr const char* kCompositeBody = R"(
#ifdef CLOUDS
layout(set = 0, binding = 11, rgba16f) uniform readonly image2D i_a;
layout(set = 0, binding = 12, rgba16f) uniform readonly image2D i_b;
#endif
layout(location = 0) out vec4 o_colour;

// (added by the time of day - engine/gfx/rt_daylight.h) the night's sky: the stars - cells on a cube round the celestial
// pole, turned with the hours, one star in about STAR_SHARE of them at a hashed place, each a pixel-wide spot - and the
// moon's disc. (round S: physical) A star's magnitude by the counts' law (N(< m) grows as 10^(0.5 m): m = STAR_FAINTEST
// + 2 log10(u), u its hash's place among the stars - Sirius' -1.4 the brightest of ~94 000), its illuminance
// STAR_ZERO x 10^(-0.4 m) (a magnitude-0 star's, in the unit) spread over its spot's solid angle; the moon its mean
// radiance over its true disc. Both x the frame's pre-exposure (sky.night.y); no glow (the air's forward scattering is
// the sky's own).
const float STAR_CELLS = 512.0;
const float STAR_SHARE = 0.06;
const float STAR_FAINTEST = 8.5;
const float STAR_CELL_ANGLE = 1.5707963 / STAR_CELLS;
const float SUN_LIMB = 0.6; // (added by the physical air: rt_sky.h kSunLimbDarkening)
float star_hash(vec3 p)
{
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.x + p.y) * p.z);
}
vec3 night_light(vec3 dir, float pixel)
{
    vec3 add = vec3(0.0);
    if (sky.night.x > 0.0 && dir.y > 0.0) {
        vec3 p = normalize(sky.stars.xyz);
        float a = -sky.stars.w;
        vec3 d = dir * cos(a) + cross(p, dir) * sin(a) + p * dot(p, dir) * (1.0 - cos(a));
        vec3 m = abs(d);
        vec2 uv;
        float face;
        if (m.x >= m.y && m.x >= m.z) {
            uv = d.yz / m.x;
            face = d.x > 0.0 ? 0.0 : 1.0;
        } else if (m.y >= m.z) {
            uv = d.xz / m.y;
            face = d.y > 0.0 ? 2.0 : 3.0;
        } else {
            uv = d.xy / m.z;
            face = d.z > 0.0 ? 4.0 : 5.0;
        }
        vec2 g = (uv * 0.5 + 0.5) * STAR_CELLS;
        vec2 cell = floor(g);
        float h = star_hash(vec3(cell, face));
        if (h > 1.0 - STAR_SHARE) {
            vec2 centre = cell + 0.25 + 0.5 * vec2(star_hash(vec3(cell, face + 7.0)), star_hash(vec3(cell, face + 13.0)));
            float spread = max(pixel * 0.5 * STAR_CELLS, 0.05);
            float r = length(g - centre) / spread;
            float u = max((h - (1.0 - STAR_SHARE)) / STAR_SHARE, 1e-6);
            float magnitude = STAR_FAINTEST + 2.0 * log(u) / log(10.0);
            float spot = 3.14159265 / 1.5 * (spread * STAR_CELL_ANGLE) * (spread * STAR_CELL_ANGLE);
            float light = STAR_ZERO * pow(10.0, -0.4 * magnitude) / spot * sky.night.y;
            vec3 tint = mix(vec3(0.75, 0.85, 1.0), vec3(1.0, 0.9, 0.75), star_hash(vec3(cell, face + 3.0)));
            add += tint * (light * sky.night.x) * exp(-r * r * 1.5) * smoothstep(0.0, 0.08, dir.y);
        }
    }
    if (sky.moon.w > 0.0) {
        float angle = acos(clamp(dot(dir, sky.moon.xyz), -1.0, 1.0));
        float disc = 1.0 - smoothstep(sky.moon_light.w - pixel, sky.moon_light.w + pixel, angle);
        add += sky.moon.w * disc * sky.moon_light.rgb;
    }
    return add;
}

#ifdef CEL
// (added by cel shading - engine/gfx/rt_cel.h; [Graphics] rt_cel) the atmosphere in flat bands of elevation (6 from the
// horizon to the top, narrower low down: the colour at each band's middle, its azimuth kept) and its brightness in
// half-stop steps (the sun's glow in rings); every edge anti-aliased over about a pixel
float cel_step(float t)
{
    float w = clamp(fwidth(t) * 0.75, 0.001, 0.5);
    return floor(t) + smoothstep(1.0 - w, 1.0, fract(t));
}
vec3 cel_atmosphere(vec3 dir)
{
    float t = sqrt(asin(clamp(dir.y, 0.0, 1.0)) / 1.5707963) * 6.0;
    float q = cel_step(t);
    float e = pow(min(q + 0.5, 6.0) / 6.0, 2.0) * 1.5707963;
    vec2 h = dir.xz;
    float hl = length(h);
    h = hl > 1e-6 ? h / hl : vec2(1.0, 0.0);
    vec3 dq = dir.y < 0.0 ? dir : vec3(h.x * cos(e), sin(e), h.y * cos(e));
    vec3 c = sky_view_linear(dq, sky.sun.xyz);
    float y = dot(c, vec3(0.2126, 0.7152, 0.0722));
    float lt = log2(max(y, 1e-6)) * 2.0;
    float lq = cel_step(lt);
    return y > 1e-6 ? c * (exp2((lq + 0.5) * 0.5) / y) : c;
}
#endif

void main()
{
    vec2 p = floor(gl_FragCoord.xy);
    vec3 dir = direction_of(p);
#ifdef CEL
    vec3 light = cel_atmosphere(dir);
#else
    vec3 light = sky_view_linear(dir, sky.sun.xyz);
#endif
    float pixel = max(length(fwidth(dir)), 1e-5); // (a pixel's angle: outside any branch)
    if (sky.night.x > 0.0 || sky.moon.w > 0.0) {
        light += night_light(dir, pixel);
    }
    // (added by the physical air) the sun's disc - the real sun, limb-darkened, its edge a pixel soft - in the sky,
    // so the clouds cover it as they do the sky behind them
    if (sky.sun_disc.w > 0.0) {
        float theta = atan(length(cross(dir, sky.sun.xyz)), dot(dir, sky.sun.xyz));
        float radius = sky.sun_disc.w;
        float edge = clamp((radius - theta) / pixel + 0.5, 0.0, 1.0);
        float t = min(theta / radius, 1.0);
        float limb = 1.0 - SUN_LIMB * (1.0 - sqrt(max(1.0 - t * t, 0.0)));
        light += sky.sun_disc.rgb * (limb * edge);
    }
#ifdef CLOUDS
    {
        vec2 low = (p - vec2(sky.rect.xy) - 0.5 * float(sky.low.z - 1)) / float(sky.low.z);
        ivec2 base = ivec2(floor(low));
        vec2 f = low - vec2(base);
        vec4 sum = vec4(0.0);
        float distance = 0.0;
        float weights = 0.0;
        for (int i = 0; i < 4; ++i) {
            ivec2 o = ivec2(i & 1, i >> 1);
            ivec2 at = clamp(base + o, ivec2(0), sky.low.xy - ivec2(1));
            vec4 info = imageLoad(i_b, at);
            float w = (o.x == 1 ? f.x : 1.0 - f.x) * (o.y == 1 ? f.y : 1.0 - f.y) * (info.y > 0.5 ? 1.0 : 0.0);
            if (w > 0.0) {
                sum += w * imageLoad(i_a, at);
                distance += w * (info.x < 0.0 ? 0.0 : info.x);
                weights += w;
            }
        }
#ifdef CEL
        // (added by cel shading) the clouds' cover with a crisp edge, their own colour in two-thirds-of-a-stop steps
        // (lit and shaded sides as flat tones) - derivatives taken outside any branch on the pixel's values
        bool any = weights > 1e-4;
        vec4 cloud = any ? sum / weights : vec4(0.0, 0.0, 0.0, 1.0);
        float fade = any ? exp(-(distance / weights) / sky.ambient_bottom.w) : 0.0;
        float cover = 1.0 - clamp(cloud.a, 0.0, 1.0);
        float cw = clamp(fwidth(cover) * 0.75, 0.002, 0.25);
        float shape = smoothstep(0.3 - cw, 0.3 + cw, cover);
        vec3 own = max(cloud.rgb, vec3(0.0)) / max(cover, 0.05);
        float oy = dot(own, vec3(0.2126, 0.7152, 0.0722));
        float ot = log2(max(oy, 1e-6)) * 1.5;
        float oq = cel_step(ot);
        own = oy > 1e-6 ? own * (exp2((oq + 0.5) / 1.5) / oy) : own;
        light = mix(light, own, shape * fade);
#else
        if (weights > 1e-4) {
            vec4 cloud = sum / weights;
            float fade = exp(-(distance / weights) / sky.ambient_bottom.w);
            float transmittance = mix(1.0, clamp(cloud.a, 0.0, 1.0), fade);
            light = light * transmittance + max(cloud.rgb, vec3(0.0)) * fade;
        }
#endif
    }
#endif
    // (round F: the frame is linear light - the sky's radiance written as it is, never clamped; the output encodes)
    o_colour = vec4(min(max(light, vec3(0.0)), vec3(65504.0)), 1.0);
}
)";

// 5. The clouds' shadow map (rt_volume_light.h; at opaque_drawn, before anything reads it): each texel the light's
// transmittance through the layer from where it lies on the cloud base, toward the light (rt::medium_light: the sun, or
// the moon at night), kCloudMapSamples densities (no detail) - what the fog's march and the shadows on the ground read.
constexpr const char* kCloudMapBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 7, rgba16f) uniform writeonly image2D o_a;

void main()
{
    ivec2 q = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(q, ivec2(CLOUD_MAP_SIZE)))) {
        return;
    }
    vec2 uv = (vec2(q) + 0.5) / float(CLOUD_MAP_SIZE);
    vec2 world = sky.cloud_map.xz + (uv - 0.5) / sky.cloud_map.w;
    vec2 across = sky.offset.xy + (world - sky.camera.xz) / GAME_UNITS_PER_METRE;
    vec3 l = sky.cloud_light.xyz;
    float rise = max(l.y, CLOUD_MAP_LEAST_RISE);
    float depth = 0.0;
    for (int k = 0; k < CLOUD_MAP_SAMPLES; ++k) {
        float h = CLOUD_BASE + (float(k) + 0.5) / float(CLOUD_MAP_SAMPLES) * (CLOUD_TOP - CLOUD_BASE);
        float t = (h - CLOUD_BASE) / rise;
        depth += cloud_density(vec3(across.x + l.x * t, h, across.y + l.z * t), sky.sun.w, false);
    }
    depth *= CLOUD_DENSITY * sky.sun_light.w * (CLOUD_TOP - CLOUD_BASE) / float(CLOUD_MAP_SAMPLES) / rise;
    // (round S) over the share the lighting state's light already keeps (its mean over the level: cloud_light.w = 1 /
    // DayLight::cloud_beam) - the light store's direct light is the mean, the map each point's own
    imageStore(o_a, q, vec4(exp(-depth) * sky.cloud_light.w));
}
)";

// (changed by the physical air - one cloud shadow) The clouds' shadows on the world are the sun's visibility in the
// ray-traced shadows' pass alone (vulkan_rt_shadows.h rt_shadows_take_cloud_shadows: the map read where the sun's light
// is lit); the multiply over all of a surface's light this pass drew when that pass didn't run is gone - it dimmed the
// sky's and the lamps' light under a cloud too, a second model of the same shadow.

// 6. What a ray leaving the world sees (rt_sky_for_rays; the reflections' misses): the sky-view table toward it, and the
// clouds' history where its direction falls on the viewport - read as the composite reads it, faded out over the last
// 8% toward the viewport's edges (beyond them: the sky alone); none below the horizon.
constexpr const char* kRaysFunctions = R"(
vec3 sky_for_ray(vec3 dir, mat4 to_pixel, vec4 sun, vec4 low, vec4 rect)
{
    vec3 light = sky_view_linear(dir, sun.xyz);
    if (sun.w < 0.5 || dir.y <= 0.0) {
        return light;
    }
    vec4 at = to_pixel * vec4(dir, 0.0);
    if (at.w <= 1e-6) {
        return light;
    }
    vec2 pixel = at.xy / at.w;
    vec2 inside = (pixel - rect.xy) / rect.zw;
    float keep = smoothstep(0.0, 0.08, min(min(inside.x, 1.0 - inside.x), min(inside.y, 1.0 - inside.y)));
    if (keep <= 0.0) {
        return light;
    }
    vec2 q = (pixel - rect.xy - 0.5 * (low.z - 1.0)) / low.z;
    ivec2 base = ivec2(floor(q));
    vec2 f = q - vec2(base);
    ivec2 most = ivec2(low.xy) - ivec2(1);
    vec4 sum = vec4(0.0);
    float distance = 0.0;
    float weights = 0.0;
    for (int i = 0; i < 4; ++i) {
        ivec2 o = ivec2(i & 1, i >> 1);
        ivec2 t = clamp(base + o, ivec2(0), most);
        vec4 info = imageLoad(u_ray_cloud_info, t);
        float w = (o.x == 1 ? f.x : 1.0 - f.x) * (o.y == 1 ? f.y : 1.0 - f.y) * (info.y > 0.5 ? 1.0 : 0.0);
        if (w > 0.0) {
            sum += w * imageLoad(u_ray_cloud_light, t);
            distance += w * (info.x < 0.0 ? 0.0 : info.x);
            weights += w;
        }
    }
    if (weights > 1e-4) {
        vec4 cloud = sum / weights;
        float fade = exp(-(distance / weights) / low.w) * keep;
        float transmittance = mix(1.0, clamp(cloud.a, 0.0, 1.0), fade);
        light = light * transmittance + max(cloud.rgb, vec3(0.0)) * fade;
    }
    return light;
}
)";

struct SkyBlock {
    std::array<float, kMatrixFloats> to_world{};
    std::array<float, kMatrixFloats> prev_to_pixel{};
    std::array<float, 4> camera{};
    std::array<float, 4> sun{};
    std::array<float, 4> sun_light{};
    std::array<float, 4> ambient_bottom{};
    std::array<float, 4> ambient_top{};
    std::array<float, 4> offset{};
    std::array<int32_t, 4> rect{};
    std::array<int32_t, 4> low{};
    std::array<int32_t, 4> flags{};
    std::array<float, 4> moon{};  // (added by the time of day)
    std::array<float, 4> stars{};
    std::array<float, 4> night{};
    std::array<float, 4> cloud_map{};   // (added by the volumetric light: the clouds' shadow map)
    std::array<float, 4> cloud_light{};
    std::array<float, 4> sun_disc{}; // (added by the physical air)
    std::array<float, 4> moon_light{}; // (round S)
};
constexpr std::size_t kSkyBlockBytes = 384;
static_assert(sizeof(SkyBlock) == kSkyBlockBytes, "the shaders' std140 block");

constexpr VkShaderStageFlags kStages = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

constexpr uint32_t kBindingDepth = 0;
constexpr uint32_t kBindingSkyView = 1;
constexpr uint32_t kBindingTransmittance = 2;
constexpr uint32_t kBindingCloudNoise = 3; // 3, 4, 5
constexpr uint32_t kBindingBlock = 6;
constexpr uint32_t kBindingOutA = 7;
constexpr uint32_t kBindingOutB = 8;
constexpr uint32_t kBindingHistA = 9;
constexpr uint32_t kBindingHistB = 10;
constexpr uint32_t kBindingLoadA = 11;
constexpr uint32_t kBindingLoadB = 12;
constexpr uint32_t kBindingCloudMap = 13; // (the clouds' shadow map, read by the shadows on the ground)
constexpr uint32_t kBindingCount = 14;

using Matrix = std::array<std::array<double, kMatrixSide>, kMatrixSide>;

struct CompositePipeline {
    VkFormat colour = VK_FORMAT_UNDEFINED;
    VkFormat depth = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    bool clouds = false;
    bool cel = false;    // (added by cel shading: the stylised sky, CEL)
    VkPipeline pipeline = VK_NULL_HANDLE;
};

// A viewport's images and its last camera.
struct SkySlot {
    std::array<uint32_t, 4> rect{};
    uint32_t scale = 1;
    Image march;      // rgb light, a transmittance
    Image march_info; // x distance, y 1 where marched
    std::array<Image, 2> history;
    std::array<Image, 2> history_info;
    uint32_t current = 0; // the history written last
    bool fresh = true;
    bool has_history = false;
    float pre_exposure = 1.0F; // (round PX) the frame's pre-exposure its history was written in
    Matrix last_to_pixel{};
    uint64_t drawn = 0; // the frame serial its clouds were last drawn in (the reflections' misses read them that frame)
    uint64_t frames = 0;
    uint64_t last_use = 0;
};

constexpr uint32_t kTimerSets = 8;
constexpr uint32_t kTimerPoints = 4; // before the march, after it, after the temporal pass, after the composite
constexpr uint64_t kTimingLogEvery = 240;

struct SkyPassState {
    RtSkySettings settings;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkSampler nearest = VK_NULL_HANDLE;
    VkSampler linear = VK_NULL_HANDLE;
    VkShaderModule vertex = VK_NULL_HANDLE;
    std::array<VkPipeline, 2> march{}; // single-sampled depth, multisampled
    VkPipeline temporal = VK_NULL_HANDLE;
    std::vector<CompositePipeline> composites;
    std::vector<std::unique_ptr<SkySlot>> slots;
    bool failed = false;
    uint64_t passes = 0;
    uint64_t drawn_serial = 0; // the frame the last pass was drawn in (the reflections' misses: a level with a sky)
    uint64_t uses = 0;
    // the level's sky (its coverage) and the atmosphere's input
    RtSkyLevel level;
    bool level_read = false;
    uint64_t settings_serial = 1; // (a new value each time the settings change: read again)
    uint64_t level_settings = 0;
    RtSkyFrame frame;
    // timing
    VkQueryPool timer = VK_NULL_HANDLE;
    double tick_milliseconds = 0.0;
    uint64_t timed = 0;
    std::array<uint64_t, kTimerSets> timer_serials{};
    RtSkyTiming timing;
    uint64_t window_samples = 0;
    std::array<double, 3> window_milliseconds{};
    // (added by the volumetric light) the clouds' shadow map: made at each viewport's opaque_drawn while the clouds are
    // drawn and the ground's shadows or the fog's march want it
    VkPipeline cloud_map = VK_NULL_HANDLE;
    Image map;
    bool map_fresh = true;      // (not yet moved to GENERAL)
    CloudShadowMap made_map;    // what it holds now
    uint64_t map_frame = 0;     // the frame it was made in (Backend::frame)
    bool map_made = false;      // (made at all)
    uint64_t maps = 0;
};



SkyPassState& pass_state()
{
    static SkyPassState state;
    return state;
}

bool first_report(Backend& state, uint64_t what) { return state.reported.insert(what).second; }

bool record_shadow_pass(Backend& state, const rt::ViewDrawn& view);

class SkyListener final : public rt::ViewListener {
public:
    [[nodiscard]] int order() const override { return rt::ViewOrder::kSky; }
    void opaque_drawn(const rt::ViewDrawn& view) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (state.device_made && state.draws_made) {
            record_shadow_pass(state, view);
        }
    }
    void sky_drawn(const rt::ViewDrawn& view) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (state.device_made && state.draws_made) {
            record_rt_sky_pass(state, view);
        }
    }
};

SkyListener& listener()
{
    static SkyListener instance;
    return instance;
}

// --- the matrices (as the sun's: vulkan_rt_sun.cpp) ------------------------------------------------------------------

bool invert(Matrix matrix, Matrix* inverse)
{
    Matrix result{};
    for (std::size_t i = 0; i < kMatrixSide; ++i) {
        core::at(core::at(result, i), i) = 1.0;
    }
    for (std::size_t column = 0; column < kMatrixSide; ++column) {
        std::size_t pivot = column;
        for (std::size_t row = column + 1; row < kMatrixSide; ++row) {
            if (std::abs(core::at(core::at(matrix, row), column)) >
                std::abs(core::at(core::at(matrix, pivot), column))) {
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

// World -> (pixel x, pixel y, depth, 1) in the target's pixels (centres at whole coordinates, y down).
Matrix world_to_pixel(const rt::ViewDrawn& view)
{
    Matrix clip_from_world{};
    for (std::size_t row = 0; row < kMatrixSide; ++row) {
        for (std::size_t column = 0; column < kMatrixSide; ++column) {
            double sum = 0.0;
            for (std::size_t k = 0; k < kMatrixSide; ++k) {
                sum += static_cast<double>(core::at(core::at(view.camera.view, column), k)) *
                       static_cast<double>(core::at(core::at(view.camera.projection, k), row));
            }
            core::at(core::at(clip_from_world, row), column) = sum;
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

// --- the objects -----------------------------------------------------------------------------------------------------

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

// (added by the low sun's sky) The sky's light on the clouds' bases and tops from the drawn sky's table - rt_clouds.h
// cloud_sky_taps, the texels and weights the processor's cloud_sky_light reads.
std::string cloud_sky_glsl()
{
    std::string bottom;
    std::string top;
    for (const rt::CloudSkyTap& tap : rt::cloud_sky_taps()) {
        (tap.top ? top : bottom) += text::format("    %s += max(texelFetch(u_sky_view, ivec2(%u, %u), 0).rgb, vec3(0.0)) * %.9g;\n",
                                                 tap.top ? "top" : "bottom", tap.column, tap.row, tap.weight);
    }
    return "void cloud_sky_light(out vec3 bottom, out vec3 top)\n{\n    bottom = vec3(0.0);\n    top = vec3(0.0);\n" +
           bottom + top + "}\n";
}

std::string source_of(const char* body, const char* define)
{
    std::string source = "#version 460\n";
    if (define != nullptr) {
        source += "#define ";
        source += define;
        source += "\n";
    }
    source += atmosphere_glsl(kBindingTransmittance, kBindingSkyView);
    source += cloud_glsl(kBindingCloudNoise);
    source += "#define GAME_UNITS_PER_METRE " + std::to_string(rt::kGameUnitsPerMetre) + "\n";
    source += "#define CLOUD_MAP_SIZE " + std::to_string(rt::kCloudMapSize) + "\n";
    source += "#define CLOUD_MAP_SAMPLES " + std::to_string(rt::kCloudMapSamples) + "\n";
    // (round S) a magnitude-0 star's illuminance in the unit (2.08 x 10^-6 lux - Allen's; the unit's lux: rt_sky.h)
    constexpr double kStarZeroLux = 2.08e-6;
    std::array<char, 64> star{};
    std::snprintf(star.data(), star.size(), "#define STAR_ZERO %.9g\n", kStarZeroLux / rt::unit_illuminance());
    source += star.data();
    source += kCommon;
    source += cloud_sky_glsl();
    source += cloud_map_glsl(kBindingCloudMap);
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

VkSampler make_sampler(VkDevice device, VkFilter filter)
{
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = filter;
    info.minFilter = filter;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.maxLod = 0.0F;
    VkSampler sampler = VK_NULL_HANDLE;
    if (vkCreateSampler(device, &info, nullptr, &sampler) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return sampler;
}

bool ensure_objects(Backend& state, std::string* error)
{
    SkyPassState& own = pass_state();
    if (own.temporal != VK_NULL_HANDLE) {
        return true;
    }
    const VkDevice device = state.context.device();
    if (own.layout == VK_NULL_HANDLE) {
        std::array<VkDescriptorSetLayoutBinding, kBindingCount> bindings{};
        for (uint32_t binding = 0; binding < kBindingCount; ++binding) {
            VkDescriptorType type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            if (binding == kBindingBlock) {
                type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            } else if (binding == kBindingOutA || binding == kBindingOutB || binding == kBindingLoadA ||
                       binding == kBindingLoadB) {
                type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
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
        own.nearest = make_sampler(device, VK_FILTER_NEAREST);
        own.linear = make_sampler(device, VK_FILTER_LINEAR);
        if (own.nearest == VK_NULL_HANDLE || own.linear == VK_NULL_HANDLE ||
            vkCreateDescriptorSetLayout(device, &set_info, nullptr, &own.set_layout) != VK_SUCCESS) {
            *error = "the descriptor layout or the samplers";
            return false;
        }
        const VkPipelineLayoutCreateInfo layout_info{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                     .pNext = nullptr,
                                                     .flags = 0,
                                                     .setLayoutCount = 1,
                                                     .pSetLayouts = &own.set_layout,
                                                     .pushConstantRangeCount = 0,
                                                     .pPushConstantRanges = nullptr};
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
    for (std::size_t multisampled = 0; multisampled < 2; ++multisampled) {
        if (core::at(own.march, multisampled) == VK_NULL_HANDLE) {
            core::at(own.march, multisampled) = make_compute(
                device, own.layout, source_of(kMarchBody, multisampled != 0 ? "MULTISAMPLED" : nullptr), error);
        }
        if (core::at(own.march, multisampled) == VK_NULL_HANDLE) {
            return false;
        }
    }
    own.temporal = make_compute(device, own.layout, source_of(kTemporalBody, nullptr), error);
    return own.temporal != VK_NULL_HANDLE;
}

// (added by cel shading) The composite's defines: CLOUDS with the clouds, CEL in the stylised sky.
const char* composite_defines(bool clouds, bool cel)
{
    if (cel) {
        return clouds ? "CLOUDS\n#define CEL" : "CEL";
    }
    return clouds ? "CLOUDS" : nullptr;
}

// The composite's pipeline for a colour target and its depth (made on first use): rgb written, alpha kept, the depth
// tested (at the far plane, less or equal: the sky's samples), not written.
VkPipeline composite_pipeline(Backend& state, const Image& colour, const Image& depth, bool clouds, std::string* error,
                              bool cel = false)
{
    SkyPassState& own = pass_state();
    for (const CompositePipeline& made : own.composites) {
        if (made.colour == colour.format && made.depth == depth.format && made.samples == colour.samples &&
            made.clouds == clouds && made.cel == cel) {
            return made.pipeline;
        }
    }
    const VkDevice device = state.context.device();
    const VkShaderModule fragment =
        make_module(device, RtShaderStage::kFragment, source_of(kCompositeBody, composite_defines(clouds, cel)), error);
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
    const VkPipelineMultisampleStateCreateInfo multisample{.sType =
                                                               VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                                           .pNext = nullptr,
                                                           .flags = 0,
                                                           .rasterizationSamples = colour.samples,
                                                           .sampleShadingEnable = VK_FALSE,
                                                           .minSampleShading = 0.0F,
                                                           .pSampleMask = nullptr,
                                                           .alphaToCoverageEnable = VK_FALSE,
                                                           .alphaToOneEnable = VK_FALSE};
    VkPipelineDepthStencilStateCreateInfo depth_state{};
    depth_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_state.depthTestEnable = VK_TRUE;
    depth_state.depthWriteEnable = VK_FALSE;
    depth_state.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_FALSE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
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
    const VkFormat colour_format = colour.format;
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &colour_format;
    rendering.depthAttachmentFormat = depth.format;
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
    info.pDepthStencilState = &depth_state;
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
    own.composites.push_back({.colour = colour.format,
                              .depth = depth.format,
                              .samples = colour.samples,
                              .clouds = clouds,
                              .cel = cel,
                              .pipeline = pipeline});
    return pipeline;
}

Image* image_of(Surface* surface)
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

bool targets_usable(const Image* colour, const Image* depth)
{
    return colour != nullptr && depth != nullptr && colour->attachment_view != VK_NULL_HANDLE &&
           (colour->aspect & VK_IMAGE_ASPECT_COLOR_BIT) != 0 && (depth->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0 &&
           (depth->usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0 && depth->view != VK_NULL_HANDLE &&
           depth->samples == colour->samples && depth->extent.width >= colour->extent.width &&
           depth->extent.height >= colour->extent.height;
}

// --- the slots -------------------------------------------------------------------------------------------------------

std::vector<Image*> images_of(SkySlot& slot)
{
    return {&slot.march,
            &slot.march_info,
            &core::at(slot.history, 0),
            &core::at(slot.history, 1),
            &core::at(slot.history_info, 0),
            &core::at(slot.history_info, 1)};
}

void release_slot(Backend& state, SkySlot* slot, bool now)
{
    for (Image* image : images_of(*slot)) {
        if (image->image == VK_NULL_HANDLE) {
            continue;
        }
        if (now) {
            state.memory.destroy_image(image);
        } else {
            retire_image(state, image);
        }
    }
}

bool make_storage_image(Backend& state, const Extent& extent, Image* out)
{
    const ImageRequest request{.format = VK_FORMAT_R16G16B16A16_SFLOAT,
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
    out->resting = VK_IMAGE_LAYOUT_GENERAL;
    return true;
}

Extent low_extent(const std::array<uint32_t, 4>& rect, uint32_t scale)
{
    return {.width = std::max(1U, (core::at(rect, 2) + scale - 1) / scale),
            .height = std::max(1U, (core::at(rect, 3) + scale - 1) / scale)};
}

SkySlot* slot_for(Backend& state, const std::array<uint32_t, 4>& rect, uint32_t scale)
{
    SkyPassState& own = pass_state();
    ++own.uses;
    for (const std::unique_ptr<SkySlot>& slot : own.slots) {
        if (slot->rect == rect && slot->scale == scale) {
            slot->last_use = own.uses;
            return slot.get();
        }
    }
    if (own.slots.size() >= kSkyViewSlots) {
        const auto oldest = std::ranges::min_element(
            own.slots, {}, [](const std::unique_ptr<SkySlot>& slot) { return slot->last_use; });
        release_slot(state, oldest->get(), false);
        own.slots.erase(oldest);
    }
    auto made_slot = std::make_unique<SkySlot>();
    SkySlot& slot = *made_slot;
    slot.rect = rect;
    slot.scale = scale;
    slot.last_use = own.uses;
    const Extent low = low_extent(rect, scale);
    bool made = true;
    for (Image* image : images_of(slot)) {
        made = made && make_storage_image(state, low, image);
    }
    if (!made) {
        release_slot(state, &slot, true);
        if (first_report(state, kReportImages)) {
            get_log("vulkan: rt_sky: the clouds' images (%ux%u) couldn't be made - no clouds there", low.width,
                    low.height);
        }
        return nullptr;
    }
    own.slots.push_back(std::move(made_slot));
    return &slot;
}

void clear_slot(VkCommandBuffer commands, SkySlot* slot)
{
    const std::vector<Image*> images = images_of(*slot);
    const VkImageSubresourceRange all{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                      .baseMipLevel = 0,
                                      .levelCount = 1,
                                      .baseArrayLayer = 0,
                                      .layerCount = 1};
    std::vector<VkImageMemoryBarrier> barriers(images.size());
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
                                 .subresourceRange = all};
    }
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, static_cast<uint32_t>(barriers.size()), barriers.data());
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
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &cleared,
                         0, nullptr, 0, nullptr);
    slot->fresh = false;
}

// --- the descriptors and the dispatches --------------------------------------------------------------------------------

struct PassBindings {
    const Image* depth = nullptr;
    bool sky = false;    // the sky-view table and the transmittance
    bool clouds = false; // the noise
    bool cloud_map = false; // (the clouds' shadow map)
    const Image* out_a = nullptr;
    const Image* out_b = nullptr;
    const Image* hist_a = nullptr;
    const Image* hist_b = nullptr;
    const Image* load_a = nullptr;
    const Image* load_b = nullptr;
    VkDescriptorBufferInfo block{};
};

void push_descriptors(VkCommandBuffer commands, VkPipelineBindPoint point, const PassBindings& bound)
{
    const SkyPassState& own = pass_state();
    std::array<VkDescriptorImageInfo, kBindingCount> infos{};
    std::vector<VkWriteDescriptorSet> writes;
    writes.reserve(kBindingCount);
    const auto add = [&](uint32_t binding, VkDescriptorType type, const VkDescriptorImageInfo& info) {
        core::at(infos, binding) = info;
        VkWriteDescriptorSet& write = writes.emplace_back();
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstBinding = binding;
        write.descriptorCount = 1;
        write.descriptorType = type;
        write.pImageInfo = &core::at(infos, binding);
    };
    if (bound.depth != nullptr) {
        add(kBindingDepth, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            {.sampler = own.nearest,
             .imageView = bound.depth->view,
             .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL});
    }
    if (bound.sky) {
        add(kBindingSkyView, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            atmosphere_descriptor(AtmosphereTable::kSkyView));
        add(kBindingTransmittance, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            atmosphere_descriptor(AtmosphereTable::kTransmittance));
    }
    if (bound.clouds) {
        for (uint32_t i = 0; i < 3; ++i) {
            add(kBindingCloudNoise + i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, cloud_noise_descriptor(i));
        }
    }
    if (bound.cloud_map) {
        add(kBindingCloudMap, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, own.made_map.image);
    }
    const std::array<std::pair<uint32_t, const Image*>, 2> sampled{
        {{kBindingHistA, bound.hist_a}, {kBindingHistB, bound.hist_b}}};
    for (const auto& [binding, image] : sampled) {
        if (image != nullptr) {
            add(binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                {.sampler = binding == kBindingHistA ? own.linear : own.nearest,
                 .imageView = image->view,
                 .imageLayout = VK_IMAGE_LAYOUT_GENERAL});
        }
    }
    const std::array<std::pair<uint32_t, const Image*>, 4> storage{{{kBindingOutA, bound.out_a},
                                                                    {kBindingOutB, bound.out_b},
                                                                    {kBindingLoadA, bound.load_a},
                                                                    {kBindingLoadB, bound.load_b}}};
    for (const auto& [binding, image] : storage) {
        if (image != nullptr) {
            add(binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                {.sampler = VK_NULL_HANDLE, .imageView = image->view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL});
        }
    }
    VkWriteDescriptorSet& block = writes.emplace_back();
    block.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    block.dstBinding = kBindingBlock;
    block.descriptorCount = 1;
    block.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    block.pBufferInfo = &bound.block;
    vkCmdPushDescriptorSetKHR(commands, point, own.layout, 0, static_cast<uint32_t>(writes.size()), writes.data());
}

void compute_done(VkCommandBuffer commands)
{
    const VkMemoryBarrier written{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                  .pNext = nullptr,
                                  .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                  .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &written,
                         0, nullptr, 0, nullptr);
}

void dispatch(VkCommandBuffer commands, VkPipeline pipeline, const PassBindings& bound, const Extent& extent)
{
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    push_descriptors(commands, VK_PIPELINE_BIND_POINT_COMPUTE, bound);
    vkCmdDispatch(commands, (extent.width + kGroupSide - 1) / kGroupSide, (extent.height + kGroupSide - 1) / kGroupSide,
                  1);
    compute_done(commands);
}

void depth_barrier(VkCommandBuffer commands, Image* depth, VkImageLayout layout, bool to_passes)
{
    const VkPipelineStageFlags tests =
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    const VkPipelineStageFlags passes = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    const VkImageMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = to_passes ? VkAccessFlags{VK_ACCESS_MEMORY_WRITE_BIT} : VkAccessFlags{0},
        .dstAccessMask = to_passes ? VkAccessFlags{VK_ACCESS_SHADER_READ_BIT}
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
    vkCmdPipelineBarrier(commands, to_passes ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : passes, to_passes ? passes : tests,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    depth->layout = layout;
}

void record_composite(VkCommandBuffer commands, VkPipeline pipeline, Image* colour, Image* depth,
                      const PassBindings& bound, const std::array<uint32_t, 4>& rect)
{
    Memory::transition(commands, colour, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true);
    VkRenderingAttachmentInfo colour_info{};
    colour_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colour_info.imageView = colour->attachment_view;
    colour_info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colour_info.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colour_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingAttachmentInfo depth_info{};
    depth_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    if (depth != nullptr) {
        depth_info.imageView = depth->attachment_view != VK_NULL_HANDLE ? depth->attachment_view : depth->view;
    }
    depth_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth_info.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depth_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    const VkRect2D area{
        .offset = {.x = static_cast<int32_t>(core::at(rect, 0)), .y = static_cast<int32_t>(core::at(rect, 1))},
        .extent = {.width = core::at(rect, 2), .height = core::at(rect, 3)}};
    VkRenderingInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea = area;
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &colour_info;
    info.pDepthAttachment = depth == nullptr ? nullptr : &depth_info;
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
    push_descriptors(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, bound);
    vkCmdDraw(commands, 3, 1, 0, 0);
    vkCmdEndRendering(commands);
}

// --- timing ------------------------------------------------------------------------------------------------------------

void ensure_timer(Backend& state)
{
    SkyPassState& own = pass_state();
    if (own.timer != VK_NULL_HANDLE || own.tick_milliseconds < 0.0) {
        return;
    }
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(state.context.physical_device(), &properties);
    constexpr double kNanosecondsPerMillisecond = 1.0e6;
    const VkQueryPoolCreateInfo info{.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                                     .pNext = nullptr,
                                     .flags = 0,
                                     .queryType = VK_QUERY_TYPE_TIMESTAMP,
                                     .queryCount = kTimerSets * kTimerPoints,
                                     .pipelineStatistics = 0};
    if (!(properties.limits.timestampPeriod > 0.0F) ||
        vkCreateQueryPool(state.context.device(), &info, nullptr, &own.timer) != VK_SUCCESS) {
        own.timer = VK_NULL_HANDLE;
        own.tick_milliseconds = -1.0;
        return;
    }
    own.tick_milliseconds = static_cast<double>(properties.limits.timestampPeriod) / kNanosecondsPerMillisecond;
}

void read_timer(Backend& state, uint32_t set, const std::array<uint32_t, 4>& rect)
{
    SkyPassState& own = pass_state();
    struct Stamp {
        uint64_t value = 0;
        uint64_t available = 0;
    };
    std::array<Stamp, kTimerPoints> stamps{};
    const VkResult got = vkGetQueryPoolResults(state.context.device(), own.timer, set * kTimerPoints, kTimerPoints,
                                               sizeof(stamps), stamps.data(), sizeof(Stamp),
                                               VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if (got != VK_SUCCESS && got != VK_NOT_READY) {
        return;
    }
    for (std::size_t i = 0; i < kTimerPoints; ++i) {
        if (core::at(stamps, i).available == 0 ||
            (i > 0 && core::at(stamps, i).value < core::at(stamps, i - 1).value)) {
            return;
        }
    }
    ++own.timing.samples;
    ++own.window_samples;
    for (std::size_t part = 0; part < own.timing.milliseconds.size(); ++part) {
        const double milliseconds =
            static_cast<double>(core::at(stamps, part + 1).value - core::at(stamps, part).value) *
            own.tick_milliseconds;
        core::at(own.timing.milliseconds, part) += milliseconds;
        core::at(own.window_milliseconds, part) += milliseconds;
    }
    if (own.window_samples >= kTimingLogEvery) {
        const auto count = static_cast<double>(own.window_samples);
        const std::array<double, 3>& w = own.window_milliseconds;
        get_log("vulkan: rt_sky: the passes take %.3f ms on the device - clouds %.3f, temporal %.3f, composite %.3f "
                "(%ux%u; the last %llu passes' average)",
                (core::at(w, 0) + core::at(w, 1) + core::at(w, 2)) / count, core::at(w, 0) / count,
                core::at(w, 1) / count, core::at(w, 2) / count, core::at(rect, 2), core::at(rect, 3),
                static_cast<unsigned long long>(own.window_samples));
        own.window_samples = 0;
        own.window_milliseconds = {};
    }
}

struct Stamps {
    VkCommandBuffer commands = nullptr;
    VkQueryPool pool = VK_NULL_HANDLE;
    uint32_t set = 0;
    bool on = false;
    void stamp(uint32_t point, VkPipelineStageFlagBits stage) const
    {
        if (on) {
            vkCmdWriteTimestamp(commands, stage, pool, (set * kTimerPoints) + point);
        }
    }
};

Stamps begin_timing(Backend& state, VkCommandBuffer commands, const std::array<uint32_t, 4>& rect)
{
    SkyPassState& own = pass_state();
    ensure_timer(state);
    const auto set = static_cast<uint32_t>(own.timed % kTimerSets);
    uint64_t& written = core::at(own.timer_serials, set);
    const bool timing = own.timer != VK_NULL_HANDLE && (written == 0 || state.frames.completed() >= written);
    if (timing) {
        if (written != 0) {
            read_timer(state, set, rect);
        }
        written = state.frames.serial();
        vkCmdResetQueryPool(commands, own.timer, set * kTimerPoints, kTimerPoints);
        ++own.timed;
    }
    return {.commands = commands, .pool = own.timer, .set = set, .on = timing};
}

// --- the level's sky (vulkan_rt_sky_pass.h step 0) ------------------------------------------------------------------------

// The sun the sky is drawn for: the visible one, else a default (logged once).
std::array<float, 3> sky_sun(Backend& state)
{
    std::array<float, 3> toward{};
    const bool have = visible_sun(&toward);
    rt::note_sun_used(rt::SunUser::kSky, have, toward); // (added by the scene's sun)
    if (have) {
        return toward;
    }
    if (first_report(state, kReportNoSun)) {
        get_log("vulkan: rt_sky: no sun shown or baked - the sky drawn with a sun 50 degrees up");
    }
    return kDefaultSkySun;
}

// The level's sky read again when needed (a new capture, the settings changed) and the atmosphere's input for the sun
// drawn - the clock's (rt::DayLight: its sun, the night sky's light) or, without it, the visible one.
bool ensure_level(const std::array<float, 3>& drawn)
{
    SkyPassState& own = pass_state();
    const rt::SkyCapture capture = rt::sky_capture();
    const bool new_capture = capture.serial != own.level.capture_serial;
    if (!own.level_read || new_capture || own.level_settings != own.settings_serial) {
        if (new_capture) {
            own.level.capture_serial = capture.serial;
            // (another level's sky: the clouds' histories start again - seeded in their next pass)
            for (const std::unique_ptr<SkySlot>& slot : own.slots) {
                slot->has_history = false;
            }
        }
        own.level_read = true;
        own.level_settings = own.settings_serial;
    }
    // (round S2) the clouds' cover is the weather's - rt_clouds.h cloud_coverage: rt_cloud_coverage, else the air's
    // typical cover, at least the snow's and the rain's deck; the light, the drawn clouds and their shadows read the same.
    // (The level's painted dome is no longer read for it: the original's sky texture is art, not weather.)
    own.level.coverage = rt::cloud_coverage();
    const rt::DayLight day = rt::day_light();
    own.level.atmosphere.sun = drawn;
    own.level.atmosphere.night = day.on ? day.night_sky : std::array<float, 3>{};
    own.level.atmosphere.aerosol = rt::aerosol(); // (added by the physical air: the weather's)
    own.level.atmosphere.exposure = rt::frame_pre_exposure(); // (round S)
    return true;
}

// --- the pass ------------------------------------------------------------------------------------------------------------

struct Recording {
    Image* colour = nullptr;
    Image* depth = nullptr;
    SkySlot* slot = nullptr;
    VkPipeline composite = VK_NULL_HANDLE;
    VkDescriptorBufferInfo block{};
    std::array<uint32_t, 4> rect{};
    bool clouds = false;
    bool seed_only = false; // (a seeding run: the clouds' march and temporal pass, no composite, not timed)
};

uint32_t steps_for(unsigned quality)
{
    constexpr std::array<uint32_t, 3> kSteps{32, 48, 96};
    return core::at(kSteps, std::min<std::size_t>(quality, kSteps.size() - 1));
}
uint32_t scale_for(unsigned quality)
{
    constexpr std::array<uint32_t, 3> kScales{4, 2, 1};
    return core::at(kScales, std::min<std::size_t>(quality, kScales.size() - 1));
}

// The block for a view, and the frame's parameters (the check's).
SkyBlock block_for(const rt::ViewDrawn& view, const Recording& recording, const Matrix& to_world,
                   const std::array<float, 3>& sun, RtSkyFrame* frame)
{
    const SkyPassState& own = pass_state();
    const RtSkySettings& settings = own.settings;
    const SkySlot* slot = recording.slot;
    SkyBlock block{};
    block.to_world = column_major(to_world);
    if (slot != nullptr && slot->has_history) {
        block.prev_to_pixel = column_major(slot->last_to_pixel);
    }
    // the camera's altitude above the level's ground (the baked sun's estimate, else the camera's own height) + the
    // model's viewer
    const rt::BakedSun& baked = rt::baked_sun();
    const double ground = baked.ground_known ? baked.ground : 0.0;
    const double altitude =
        rt::kViewerAltitude +
        std::max(0.0, (static_cast<double>(core::at(view.camera.position, 1)) - ground) / rt::kGameUnitsPerMetre);
    // (round W) the clouds drift with the one wind aloft (wind.h: the weather's wind at the boundary layer's top, turned
    // by the Ekman spiral)
    const double drift = wind::aloft_speed() * view.game_seconds;
    const std::array<double, 2> aloft = wind::aloft_direction();
    const std::array<double, 2> offset{
        (core::at(view.camera.position, 0) / rt::kGameUnitsPerMetre) + (core::at(aloft, 0) * drift),
        (core::at(view.camera.position, 2) / rt::kGameUnitsPerMetre) + (core::at(aloft, 1) * drift)};
    // the sun's light at the clouds (the atmosphere's transmittance half way up the layer) and the sky's
    const double layer = rt::kGroundRadius + (0.5 * (rt::kCloudBase + rt::kCloudTop));
    const rt::Rgb through = rt::transmittance_to_top(
        {.radius = layer, .cosine = std::clamp(static_cast<double>(core::at(sun, 1)), -1.0, 1.0)}, 64);
    // (the sky's light on them is the drawn sky's, read by the march from its table - rt_clouds.h cloud_sky_light; round
    // S: the light on them is the scene's - the sun, or the moon at night (rt_daylight.h cloud_light: its irradiance at
    // the top, the moon's phase and colour, pre-exposed) - through the air down to the layer, none where the ground is
    // in the way; no glow of its own)
    const rt::CloudLight timed = rt::cloud_light(rt::day_light());
    const std::array<float, 3> to_light = timed.clock ? timed.toward : sun;
    rt::Rgb through_light = through;
    if (timed.clock) {
        const double rise = std::clamp(static_cast<double>(core::at(to_light, 1)), -1.0, 1.0);
        const double grazing = -std::sqrt(1.0 - ((rt::kGroundRadius / layer) * (rt::kGroundRadius / layer)));
        through_light = rise < grazing ? rt::Rgb{} : rt::transmittance_to_top({.radius = layer, .cosine = rise}, 64);
    }
    const double sun_scale = rt::sky_exposure() * kCloudSunScale;
    std::array<float, 3> sun_light{};
    for (std::size_t channel = 0; channel < 3; ++channel) {
        core::at(sun_light, channel) = static_cast<float>(core::at(through_light, channel) * sun_scale *
                                                          static_cast<double>(core::at(timed.light, channel)));
    }
    const std::array<float, 3> glow{};
    const uint64_t frames = slot != nullptr ? slot->frames : 0;
    const auto jitter_offset = static_cast<float>(std::fmod(static_cast<double>(frames) * kGolden, 1.0));
    block.camera = {core::at(view.camera.position, 0), core::at(view.camera.position, 1),
                    core::at(view.camera.position, 2), static_cast<float>(altitude)};
    block.sun = {core::at(sun, 0), core::at(sun, 1), core::at(sun, 2),
                 static_cast<float>(rt::cloud_amount(rt::cloud_coverage()))};
    block.cloud_light = {core::at(to_light, 0), core::at(to_light, 1), core::at(to_light, 2), 1.0F};
    // (added by the physical air) the sun's disc in the sky while rt_sun draws a sun: the real one, its radiance the
    // atmosphere's transmitted sun (rt_sky.h sun_disc_radiance - 0 under the horizon)
    if (sun_settings_held().sun) {
        const rt::Rgb disc =
            rt::sun_disc_radiance(std::asin(std::clamp(static_cast<double>(core::at(sun, 1)), -1.0, 1.0)));
        const auto pre = static_cast<double>(rt::frame_pre_exposure()); // (round S)
        block.sun_disc = {static_cast<float>(core::at(disc, 0) * pre), static_cast<float>(core::at(disc, 1) * pre),
                          static_cast<float>(core::at(disc, 2) * pre), static_cast<float>(rt::kSunAngularRadius)};
    }
    block.sun_light = {core::at(sun_light, 0), core::at(sun_light, 1), core::at(sun_light, 2), 1.0F};
    block.ambient_bottom = {core::at(glow, 0), core::at(glow, 1), core::at(glow, 2),
                            static_cast<float>(kCloudAerialDistance)};
    block.ambient_top = {core::at(glow, 0), core::at(glow, 1), core::at(glow, 2), jitter_offset};
    // (w, round PX: the history's light into this frame's pre-exposure - UE5's PreExposure correction of a history)
    block.offset = {static_cast<float>(core::at(offset, 0)), static_cast<float>(core::at(offset, 1)), kSkyHistoryBlend,
                    recording.slot != nullptr && recording.slot->pre_exposure > 0.0F
                        ? rt::frame_pre_exposure() / recording.slot->pre_exposure
                        : 1.0F};
    block.rect = {static_cast<int32_t>(core::at(recording.rect, 0)), static_cast<int32_t>(core::at(recording.rect, 1)),
                  static_cast<int32_t>(core::at(recording.rect, 2)), static_cast<int32_t>(core::at(recording.rect, 3))};
    const uint32_t scale = scale_for(settings.quality);
    const Extent low = low_extent(recording.rect, scale);
    block.low = {static_cast<int32_t>(low.width), static_cast<int32_t>(low.height), static_cast<int32_t>(scale),
                 static_cast<int32_t>(steps_for(settings.quality))};
    block.flags = {static_cast<int32_t>(recording.colour->samples), recording.clouds ? 1 : 0,
                   slot != nullptr && slot->has_history ? 1 : 0, 0}; // (w unused - round F)
    // (added by the time of day) the moon, the stars (none without the clock)
    const rt::DayLight day = rt::day_light();
    if (day.on) {
        block.moon = {core::at(day.moon, 0), core::at(day.moon, 1), core::at(day.moon, 2), day.moon_disc};
        block.stars = day.stars_pole;
        block.night = {day.stars, day.pre_exposure, 0.0F, 0.0F};
        block.moon_light = {core::at(day.moon_radiance, 0), core::at(day.moon_radiance, 1),
                            core::at(day.moon_radiance, 2), static_cast<float>(rt::kMoonAngularRadius)};
    }
    *frame = RtSkyFrame{.sun = sun,
                        .camera = view.camera.position,
                        .camera_altitude = altitude,
                        .offset = offset,
                        .sun_light = sun_light,
                        .sky_glow = glow,
                        .coverage = static_cast<float>(rt::cloud_amount(rt::cloud_coverage())),
                        .steps = steps_for(settings.quality),
                        .scale = scale,
                        .jitter_offset = jitter_offset,
                        .to_world = block.to_world,
                        .history = slot != nullptr && slot->has_history,
                        .clouds = recording.clouds,
                        .cloud_toward = to_light};
    return block;
}

void record_commands(Backend& state, const Recording& recording, VkCommandBuffer commands)
{
    SkyPassState& own = pass_state();
    SkySlot* const slot = recording.slot;
    Image* const depth = recording.depth;
    const Stamps stamps = recording.seed_only ? Stamps{} : begin_timing(state, commands, recording.rect);
    stamps.stamp(0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    const VkMemoryBarrier ready{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                .pNext = nullptr,
                                .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
                                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &ready, 0, nullptr, 0, nullptr);
    const Image* history_a = nullptr;
    const Image* history_b = nullptr;
    if (recording.clouds) {
        depth_barrier(commands, depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true);
        const Extent low = slot->march.extent;
        const std::size_t multisampled = depth->samples != VK_SAMPLE_COUNT_1_BIT ? 1U : 0U;
        dispatch(commands, core::at(own.march, multisampled),
                 {.depth = depth,
                  .sky = true,
                  .clouds = true,
                  .out_a = &slot->march,
                  .out_b = &slot->march_info,
                  .block = recording.block},
                 low);
        stamps.stamp(1, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        const uint32_t previous = slot->current;
        const uint32_t next = 1U - previous;
        dispatch(commands, own.temporal,
                 {.sky = true,
                  .clouds = true,
                  .out_a = &core::at(slot->history, next),
                  .out_b = &core::at(slot->history_info, next),
                  .hist_a = &core::at(slot->history, previous),
                  .hist_b = &core::at(slot->history_info, previous),
                  .load_a = &slot->march,
                  .load_b = &slot->march_info,
                  .block = recording.block},
                 low);
        slot->current = next;
        history_a = &core::at(slot->history, next);
        history_b = &core::at(slot->history_info, next);
        stamps.stamp(2, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        depth_barrier(commands, depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, false);
    } else {
        stamps.stamp(1, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
        stamps.stamp(2, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
        if (depth->layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
            depth_barrier(commands, depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, false);
        }
    }
    if (!recording.seed_only) {
        record_composite(commands, recording.composite, recording.colour, depth,
                         {.sky = true, .load_a = history_a, .load_b = history_b, .block = recording.block},
                         recording.rect);
    }
    stamps.stamp(3, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    if (depth->layout != depth->resting) {
        depth_barrier(commands, depth, depth->resting, false);
    }
    if (recording.colour->layout != recording.colour->resting) {
        Memory::transition(commands, recording.colour, recording.colour->resting);
    }
    state.bound = BoundState{};
    const uint64_t serial = state.frames.serial();
    recording.colour->used = serial;
    depth->used = serial;
    atmosphere_used(serial);
    if (recording.clouds) {
        cloud_noise_used(serial);
        for (Image* image : images_of(*slot)) {
            image->used = serial;
        }
    }
}

// The clouds' shadow map's pipeline and image (once). False: they can't be made (logged once).
bool ensure_cloud_map(Backend& state, std::string* error)
{
    SkyPassState& own = pass_state();
    if (own.cloud_map == VK_NULL_HANDLE) {
        own.cloud_map = make_compute(state.context.device(), own.layout, source_of(kCloudMapBody, nullptr), error);
        if (own.cloud_map == VK_NULL_HANDLE) {
            return false;
        }
    }
    if (own.map.image == VK_NULL_HANDLE) {
        if (!make_storage_image(state, {.width = rt::kCloudMapSize, .height = rt::kCloudMapSize}, &own.map)) {
            *error = "the clouds' shadow map's image";
            return false;
        }
        own.map_fresh = true;
    }
    return true;
}

// (round S) the least mean share the map is divided by (an overcast's beam: the map's own then is ~0 as well)
constexpr float kLeastCloudBeam = 1e-4F;

// Where the map lies for a view (its block's fields - rt_volume_light.h): centred where the camera's way toward the
// light meets the cloud base, snapped to whole texels.
void place_cloud_map(const rt::MediumLight& light, SkyBlock* block)
{
    const double units = rt::kGameUnitsPerMetre;
    const std::array<double, 3> camera{core::at(block->camera, 0), core::at(block->camera, 1),
                                       core::at(block->camera, 2)};
    const std::array<double, 3> toward{core::at(light.toward, 0), core::at(light.toward, 1),
                                       core::at(light.toward, 2)};
    // (the camera's altitude in the clouds' metres is block->camera.w: the base is that far below / above it)
    const double base_y = core::at(camera, 1) + ((rt::kCloudBase - core::at(block->camera, 3)) * units);
    const std::array<double, 2> centre = rt::cloud_map_place(camera, toward, base_y);
    const double width = 2.0 * rt::kCloudMapHalf * units;
    const double texel = width / static_cast<double>(rt::kCloudMapSize);
    block->cloud_map = {static_cast<float>(std::round(core::at(centre, 0) / texel) * texel),
                        static_cast<float>(base_y),
                        static_cast<float>(std::round(core::at(centre, 1) / texel) * texel),
                        static_cast<float>(1.0 / width)};
    // (round S) w: 1 / the share the lighting state's light keeps (rt_daylight.h DayLight::cloud_beam) - the map is each
    // point's over it
    const rt::DayLight day = rt::day_light();
    const float mean = day.on ? std::max(day.cloud_beam, kLeastCloudBeam) : 1.0F;
    block->cloud_light = {core::at(light.toward, 0), core::at(light.toward, 1), core::at(light.toward, 2), 1.0F / mean};
}

// The clouds' shadow map for a viewport (at its opaque_drawn; rt_cloud_shadows): the light's transmittance through the
// cloud layer, read as the sun's visibility by the ray-traced shadows' pass and the fog's march. False: skipped.
bool record_shadow_pass(Backend& state, const rt::ViewDrawn& view)
{
    SkyPassState& own = pass_state();
    const RtSkySettings& settings = own.settings;
    if (own.failed || !settings.on || !settings.clouds || !own.level_read || !settings.cloud_shadows) {
        return false;
    }
    const rt::MediumLight light = rt::medium_light();
    if (!light.have) {
        return false;
    }
    Recording recording;
    recording.colour = image_of(core::at(state.targets, 0));
    recording.depth = image_of(state.depth);
    if (!targets_usable(recording.colour, recording.depth)) {
        return false;
    }
    const Extent target = recording.colour->extent;
    const uint32_t left = std::min(core::at(view.rect, 0), target.width);
    const uint32_t top = std::min(core::at(view.rect, 1), target.height);
    recording.rect = {left, top, std::min(core::at(view.rect, 2), target.width - left),
                      std::min(core::at(view.rect, 3), target.height - top)};
    if (core::at(recording.rect, 2) == 0 || core::at(recording.rect, 3) == 0) {
        return false;
    }
    rt::ViewDrawn clipped = view;
    clipped.rect = recording.rect;
    Matrix to_world{};
    if (!invert(world_to_pixel(clipped), &to_world)) {
        return false;
    }
    std::string error;
    VkCommandBuffer commands = outside_pass(state);
    if (!ensure_objects(state, &error) || !update_cloud_noise(state, commands)) {
        return false;
    }
    if (!ensure_cloud_map(state, &error)) {
        if (first_report(state, kReportCloudMap)) {
            get_log("vulkan: rt_sky: the clouds' shadow map couldn't be made (%s) - no cloud shadows", error.c_str());
        }
        own.failed = true;
        return false;
    }
    recording.clouds = true;
    RtSkyFrame unused;
    SkyBlock block = block_for(clipped, recording, to_world, own.frame.sun, &unused);
    place_cloud_map(light, &block);
    const auto block_in_ring = [&state](const SkyBlock& written) -> std::optional<VkDescriptorBufferInfo> {
        const void* const block_bytes = &written;
        const std::optional<RingSpan> span = state.ring.copy(
            std::span(static_cast<const uint8_t*>(block_bytes), sizeof(written)), state.uniform_alignment);
        if (!span) {
            return std::nullopt;
        }
        return VkDescriptorBufferInfo{.buffer = span->buffer, .offset = span->offset, .range = sizeof(written)};
    };
    const std::optional<VkDescriptorBufferInfo> map_block = block_in_ring(block);
    if (!map_block) {
        return false;
    }
    // 1. the map (after every earlier read of it - the viewport before's fog and shadows)
    if (own.map_fresh) {
        const VkImageMemoryBarrier to_general{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                              .pNext = nullptr,
                                              .srcAccessMask = 0,
                                              .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                              .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                              .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                                              .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                              .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                              .image = own.map.image,
                                              .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                                   .baseMipLevel = 0,
                                                                   .levelCount = 1,
                                                                   .baseArrayLayer = 0,
                                                                   .layerCount = 1}};
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &to_general);
        own.map.layout = VK_IMAGE_LAYOUT_GENERAL;
        own.map_fresh = false;
    } else {
        const VkMemoryBarrier read_before{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                          .pNext = nullptr,
                                          .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
                                          .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT};
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &read_before, 0, nullptr, 0, nullptr);
    }
    dispatch(commands, own.cloud_map, {.clouds = true, .out_a = &own.map, .block = *map_block},
             {.width = rt::kCloudMapSize, .height = rt::kCloudMapSize});
    const float fade = static_cast<float>(rt::cloud_map_fade(core::at(light.toward, 1)));
    own.made_map = CloudShadowMap{
        .image = {.sampler = own.linear, .imageView = own.map.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
        .place = block.cloud_map,
        .light = {core::at(light.toward, 0), core::at(light.toward, 1), core::at(light.toward, 2), fade}};
    own.map_frame = state.frame;
    own.map_made = true;
    ++own.maps;
    const uint64_t serial = state.frames.serial();
    own.map.used = serial;
    cloud_noise_used(serial);
    if (first_report(state, kReportCloudMap)) {
        get_log("vulkan: rt_sky: the clouds' shadow map made (%ux%u over %.0f m, toward the %s, %.1f degrees up)",
                rt::kCloudMapSize, rt::kCloudMapSize, 2.0 * rt::kCloudMapHalf, light.moonlit ? "moon" : "sun",
                light.sun_elevation);
    }
    // (the clouds' shadow is the sun's visibility in the ray-traced shadows' pass alone - one model; without that pass
    // the map lights only the air - the fog's march)
    if (settings.cloud_shadows && !rt_shadows_take_cloud_shadows(state) && first_report(state, kReportShadows)) {
        get_log("vulkan: rt_sky: the clouds' shadows fall on the level through the ray-traced shadows (rt_shadows) - "
                "without them only the air is shaded by the clouds");
    }
    return true;
}

const char* clouds_text(bool clouds, unsigned quality)
{
    if (!clouds) {
        return "";
    }
    if (quality == 0) {
        return " and clouds at a quarter resolution";
    }
    return quality == 1 ? " and clouds at half resolution" : " and clouds at full resolution";
}

bool record_pass(Backend& state, const rt::ViewDrawn& view)
{
    SkyPassState& own = pass_state();
    const RtSkySettings& settings = own.settings;
    if (own.failed || !settings.on) {
        return false;
    }
    Recording recording;
    recording.colour = image_of(core::at(state.targets, 0));
    recording.depth = image_of(state.depth);
    if (!targets_usable(recording.colour, recording.depth)) {
        if (first_report(state, kReportTargets)) {
            const Image* colour = recording.colour;
            const Image* depth = recording.depth;
            get_log(
                "vulkan: rt_sky: the bound targets can't be drawn over (no samplable depth of the colour's size and "
                "samples: colour %s, depth %s%s) - the original sky kept there",
                colour == nullptr ? "none"
                                  : text::format("%ux%u x%u", colour->extent.width, colour->extent.height,
                                                 static_cast<unsigned>(colour->samples))
                                        .c_str(),
                depth == nullptr ? "none"
                                 : text::format("%ux%u x%u", depth->extent.width, depth->extent.height,
                                                static_cast<unsigned>(depth->samples))
                                       .c_str(),
                depth != nullptr && (depth->usage & VK_IMAGE_USAGE_SAMPLED_BIT) == 0 ? ", not samplable" : "");
        }
        return false;
    }
    const Extent target = recording.colour->extent;
    const uint32_t left = std::min(core::at(view.rect, 0), target.width);
    const uint32_t top = std::min(core::at(view.rect, 1), target.height);
    recording.rect = {left, top, std::min(core::at(view.rect, 2), target.width - left),
                      std::min(core::at(view.rect, 3), target.height - top)};
    if (core::at(recording.rect, 2) == 0 || core::at(recording.rect, 3) == 0) {
        return false;
    }
    rt::ViewDrawn clipped = view;
    clipped.rect = recording.rect;
    const Matrix to_pixel = world_to_pixel(clipped);
    Matrix to_world{};
    if (!invert(to_pixel, &to_world)) {
        return false;
    }
    std::string error;
    if (!ensure_objects(state, &error)) {
        own.failed = true;
        if (first_report(state, kReportPipeline)) {
            get_log("vulkan: rt_sky: the passes couldn't be made (%s) - the original sky kept", error.c_str());
        }
        return false;
    }
    // 0 - 1: the level's sky read, the atmosphere's tables
    const std::array<float, 3> sun = sky_sun(state);
    if (!ensure_level(sun)) {
        return false;
    }
    VkCommandBuffer commands = outside_pass(state);
    if (!update_atmosphere(state, commands, own.level.atmosphere)) {
        return false;
    }
    recording.clouds = settings.clouds && update_cloud_noise(state, commands);
    if (recording.clouds) {
        recording.slot = slot_for(state, recording.rect, scale_for(settings.quality));
        recording.clouds = recording.slot != nullptr;
        if (recording.slot != nullptr && recording.slot->fresh) {
            clear_slot(commands, recording.slot);
        }
    }
    recording.composite = composite_pipeline(state, *recording.colour, *recording.depth, recording.clouds, &error,
                                             cel_on()); // (added by cel shading: the stylised sky)
    if (recording.composite == VK_NULL_HANDLE) {
        own.failed = true;
        if (first_report(state, kReportPipeline)) {
            get_log("vulkan: rt_sky: the passes couldn't be made (%s) - the original sky kept", error.c_str());
        }
        return false;
    }
    // (a history that starts again - a new viewport, a new level - seeded: kSkySeedFrames runs of the clouds' passes,
    // each with the next frame's jitter and this camera, the composite after the last)
    const uint32_t runs = recording.clouds && !recording.slot->has_history ? kSkySeedFrames : 1U;
    for (uint32_t run = 0; run < runs; ++run) {
        recording.seed_only = run + 1 < runs;
        const SkyBlock block = block_for(clipped, recording, to_world, sun, &own.frame);
        const void* const block_bytes = &block;
        const std::optional<RingSpan> span = state.ring.copy(
            std::span(static_cast<const uint8_t*>(block_bytes), sizeof(block)), state.uniform_alignment);
        if (!span) {
            if (first_report(state, kReportNoRoom)) {
                get_log("vulkan: rt_sky: no room in the ring for the pass's block - the pass skipped");
            }
            return false;
        }
        recording.block = {.buffer = span->buffer, .offset = span->offset, .range = sizeof(block)};
        record_commands(state, recording, commands);
        if (recording.slot != nullptr) {
            recording.slot->last_to_pixel = to_pixel;
            recording.slot->has_history = true;
            recording.slot->pre_exposure = rt::frame_pre_exposure(); // (round PX)
            recording.slot->drawn = state.frames.serial();
            ++recording.slot->frames;
        }
    }
    ++own.passes;
    own.drawn_serial = state.frames.serial();
    if (first_report(state, kReportFirst)) {
        get_log("vulkan: rt_sky: the first pass (%ux%u, %u sample%s; the atmosphere%s; sun %.1f degrees up)",
                core::at(recording.rect, 2), core::at(recording.rect, 3),
                static_cast<unsigned>(recording.colour->samples), recording.colour->samples == 1 ? "" : "s",
                clouds_text(recording.clouds, settings.quality),
                std::asin(std::clamp(static_cast<double>(core::at(sun, 1)), -1.0, 1.0)) / kDegrees);
    }
    return true;
}

} // namespace

void set_rt_sky_settings(const RtSkySettings& settings)
{
    RtSkySettings kept = settings;
    kept.coverage = std::isfinite(kept.coverage) ? std::min(kept.coverage, 1.0F) : kRtCloudCoverageAuto;
    kept.quality = std::min(kept.quality, 2U);
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        SkyPassState& own = pass_state();
        own.settings = kept;
        // (a split comparison's swap between its sides keeps each side's clouds' history and the level's sky read -
        // rt_scene.h settings_swap)
        if (!rt::settings_swap()) {
            ++own.settings_serial;
            for (const std::unique_ptr<SkySlot>& slot : own.slots) {
                slot->has_history = false;
            }
            invalidate_sky();
        }
        set_table_follows_sky_view(kept.on);
    }
    // (round S2) the settings' cover is the weather's first source (rt_clouds.h cloud_coverage - the app sets the same
    // from the ini: renderer_choice.cpp)
    rt::set_cloud_cover(rt::CloudCoverSource::kSettings, kept.coverage >= 0.0F ? kept.coverage : -1.0F);
    rt::set_sky_replacement({.on = kept.on});
    rt::set_sky_capture_wanted(kept.on || water_settings().on); // (NOT ORIGINAL - rt_water reflects the captured sky too)
    if (kept.on) {
        rt::add_view_listener(&listener());
    } else {
        rt::remove_view_listener(&listener());
    }
}

RtSkySettings rt_sky_settings()
{
    const std::scoped_lock lock(backend().mutex);
    return pass_state().settings;
}

VkImageUsageFlags rt_sky_depth_usage(const Backend& state, VkFormat format)
{
    if (!pass_state().settings.on) {
        return 0;
    }
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(state.context.physical_device(), format, &properties);
    return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0 ? VK_IMAGE_USAGE_SAMPLED_BIT
                                                                                         : 0;
}

bool record_rt_sky_pass(Backend& state, const rt::ViewDrawn& view) { return record_pass(state, view); }

void prepare_rt_sky(Backend& state)
{
    const SkyPassState& own = pass_state();
    if (own.failed || !own.settings.on) {
        return;
    }
    const std::array<float, 3> sun = sky_sun(state);
    if (ensure_level(sun)) {
        update_atmosphere(state, outside_pass(state), own.level.atmosphere);
    }
}

uint64_t rt_sky_passes() { return pass_state().passes; }

void rt_sky_reset_history()
{
    for (const std::unique_ptr<SkySlot>& slot : pass_state().slots) {
        slot->has_history = false;
    }
}

RtSkyLevel rt_sky_level() { return pass_state().level; }

RtSkyFrame rt_sky_frame() { return pass_state().frame; }

std::string rt_sky_cloud_light_glsl() { return cloud_sky_glsl(); }

RtSkyTiming rt_sky_timing() { return pass_state().timing; }

bool read_rt_sky_image(Backend& state, const std::array<uint32_t, 4>& rect, RtSkyImage which, RtSkyTexels* out)
{
    *out = RtSkyTexels{};
    for (const std::unique_ptr<SkySlot>& slot : pass_state().slots) {
        if (slot->rect != rect) {
            continue;
        }
        Image* image = &slot->march;
        switch (which) {
        case RtSkyImage::kMarch:
            break;
        case RtSkyImage::kMarchInfo:
            image = &slot->march_info;
            break;
        case RtSkyImage::kHistory:
            image = &core::at(slot->history, slot->current);
            break;
        case RtSkyImage::kHistoryInfo:
            image = &core::at(slot->history_info, slot->current);
            break;
        }
        if (!finish_work(state)) {
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
                core::at(core::at(out->texels, i), channel) = half_to_float(half);
            }
        }
        return true;
    }
    return false;
}

bool rt_sky_for_rays(Backend& state, const std::array<uint32_t, 4>& rect, RtSkyForRays* out)
{
    *out = RtSkyForRays{};
    const SkyPassState& own = pass_state();
    // (a sky drawn this frame or the one before: a level with no sky scene - indoors - keeps the environment map)
    const bool drawn = own.drawn_serial != 0 && own.drawn_serial + 1 >= state.frames.serial();
    if (own.failed || !own.settings.on || !own.level_read || !drawn) {
        return false;
    }
    out->sky_view = atmosphere_descriptor(AtmosphereTable::kSkyView);
    out->transmittance = atmosphere_descriptor(AtmosphereTable::kTransmittance);
    if (out->sky_view.imageView == VK_NULL_HANDLE || out->transmittance.imageView == VK_NULL_HANDLE) {
        return false;
    }
    const std::array<float, 3>& sun = own.level.atmosphere.sun;
    out->sun = {core::at(sun, 0), core::at(sun, 1), core::at(sun, 2), 0.0F};
    out->cloud_light = {
        .sampler = VK_NULL_HANDLE, .imageView = out->sky_view.imageView, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
    out->cloud_info = out->cloud_light;
    const uint64_t serial = state.frames.serial();
    for (const std::unique_ptr<SkySlot>& slot : own.slots) {
        if (slot->rect != rect || slot->drawn != serial || !own.settings.clouds) {
            continue;
        }
        const Image& light = core::at(slot->history, slot->current);
        const Image& info = core::at(slot->history_info, slot->current);
        out->cloud_light = {.sampler = VK_NULL_HANDLE, .imageView = light.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
        out->cloud_info = {.sampler = VK_NULL_HANDLE, .imageView = info.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
        out->to_pixel = column_major(slot->last_to_pixel);
        out->sun.back() = 1.0F;
        out->low = {static_cast<float>(light.extent.width), static_cast<float>(light.extent.height),
                    static_cast<float>(slot->scale), static_cast<float>(kCloudAerialDistance)};
        out->rect = {static_cast<float>(core::at(rect, 0)), static_cast<float>(core::at(rect, 1)),
                     static_cast<float>(core::at(rect, 2)), static_cast<float>(core::at(rect, 3))};
        break;
    }
    atmosphere_used(serial);
    return true;
}

std::string rt_sky_rays_glsl(uint32_t first_binding)
{
    std::string out = atmosphere_glsl(first_binding + 1, first_binding);
    out += "layout(set = 0, binding = " + std::to_string(first_binding + 2) +
           ", rgba16f) uniform readonly image2D u_ray_cloud_light;\n";
    out += "layout(set = 0, binding = " + std::to_string(first_binding + 3) +
           ", rgba16f) uniform readonly image2D u_ray_cloud_info;\n";
    out += kRaysFunctions;
    return out;
}

bool cloud_shadow_map(const Backend& state, CloudShadowMap* out)
{
    const SkyPassState& own = pass_state();
    if (!own.map_made || own.map_frame != state.frame || own.made_map.image.imageView == VK_NULL_HANDLE) {
        return false;
    }
    *out = own.made_map;
    return true;
}

std::string cloud_map_glsl(uint32_t binding)
{
    std::string glsl = "layout(set = 0, binding = " + std::to_string(binding) + ") uniform sampler2D u_cloud_map;\n";
    glsl += "const float CLOUD_MAP_LEAST_RISE = " + std::to_string(rt::kCloudMapLeastRise) + ";\n";
    glsl += R"(
// The share of the light that gets through the clouds to `at` (the clouds' shadow map - vulkan_rt_sky_pass.h) over the
// mean share the lighting state's light already carries (round S: rt_daylight.h DayLight::cloud_beam): read where at's
// way toward the light meets the cloud base; light.w of its shadow kept.
float cloud_map_kept(vec3 at, vec4 place, vec4 light)
{
    if (!(light.w > 0.0)) {
        return 1.0;
    }
    float rise = max(light.y, CLOUD_MAP_LEAST_RISE);
    vec2 base = at.xz + light.xz * (max(place.y - at.y, 0.0) / rise);
    vec2 uv = (base - place.xz) * place.w + 0.5;
    float through = textureLod(u_cloud_map, uv, 0.0).r;
    return mix(1.0, max(through, 0.0), light.w); // (round S: over the mean the light state keeps - above 1 in a gap)
}
)";
    return glsl;
}


uint64_t cloud_maps_made() { return pass_state().maps; }

bool read_cloud_shadow_map(Backend& state, std::vector<float>* texels, CloudShadowMap* made)
{
    SkyPassState& own = pass_state();
    texels->clear();
    if (!own.map_made || own.map.image == VK_NULL_HANDLE || !finish_work(state)) {
        return false;
    }
    constexpr std::size_t kChannels = 4;
    const std::size_t count = static_cast<std::size_t>(own.map.extent.width) * own.map.extent.height;
    std::vector<uint8_t> bytes(count * kChannels * sizeof(uint16_t));
    if (!state.memory.read_image_level(&own.map, 0, &bytes)) {
        return false;
    }
    texels->resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        uint16_t half = 0;
        std::memcpy(&half, &core::at(bytes, i * kChannels * sizeof(uint16_t)), sizeof(half));
        core::at(*texels, i) = half_to_float(half);
    }
    *made = own.made_map;
    return true;
}

void release_rt_sky(Backend& state)
{
    SkyPassState& own = pass_state();
    const VkDevice device = state.context.device();
    for (const std::unique_ptr<SkySlot>& slot : own.slots) {
        release_slot(state, slot.get(), true);
    }
    own.slots.clear();
    if (device != nullptr) {
        for (const CompositePipeline& made : own.composites) {
            vkDestroyPipeline(device, made.pipeline, nullptr);
        }
        for (const VkPipeline pipeline : {own.march.front(), own.march.back(), own.temporal, own.cloud_map}) {
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
        for (const VkSampler sampler : {own.nearest, own.linear}) {
            if (sampler != VK_NULL_HANDLE) {
                vkDestroySampler(device, sampler, nullptr);
            }
        }
        if (own.timer != VK_NULL_HANDLE) {
            vkDestroyQueryPool(device, own.timer, nullptr);
        }
    }
    if (own.map.image != VK_NULL_HANDLE) {
        state.memory.destroy_image(&own.map);
    }
    const RtSkySettings settings = own.settings;
    const uint64_t serial = own.settings_serial;
    own = SkyPassState{};
    own.settings = settings;
    own.settings_serial = serial + 1;
    release_cloud_noise(state);
}

} // namespace render::vulkan
