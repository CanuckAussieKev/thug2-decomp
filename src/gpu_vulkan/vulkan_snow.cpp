// NOT ORIGINAL - snow: the cover on the world's draws, the trails' layer, the spray and the falling snow
// (vulkan_snow.h; docs/design/vulkan.md "Snow").
#include "gpu_vulkan/vulkan_snow.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "engine/gfx/rt_daylight.h"
#include "engine/gfx/rt_light_field.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/snow.h"
#include "engine/gfx/wind.h"
#include "gpu_vulkan/ff_uniforms.h"
#include "gpu_vulkan/glsl_compiler.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_cel_draw.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_motion.h"
#include "gpu_vulkan/vulkan_objects.h"
#include "gpu_vulkan/vulkan_pipelines.h"
#include "gpu_vulkan/vulkan_ring.h"
#include "gpu_vulkan/vulkan_rt_glsl.h"
#include "gpu_vulkan/vulkan_rt_scene.h"
#include "gpu_vulkan/vulkan_surface_trail.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

// The once-only reports' keys (Backend::reported).
constexpr uint64_t kReportTarget = 0x534E5701U;
constexpr uint64_t kReportPipeline = 0x534E5702U;
constexpr uint64_t kReportFirstLayer = 0x534E5703U;
constexpr uint64_t kReportUpload = 0x534E5704U;
constexpr uint64_t kReportNoRoom = 0x534E5705U;
constexpr uint64_t kReportFirstAir = 0x534E5706U;
constexpr uint64_t kReportFirstCover = 0x534E5707U;
constexpr uint64_t kReportHaze = 0x534E5708U;
constexpr uint64_t kReportUncovered = 0x534E570900000000ULL;

constexpr VkShaderStageFlags kStages =
    VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
constexpr uint32_t kBindingBlock = 0;   // the pass's block (uniform, in the ring)
constexpr uint32_t kBindingSky = 1;     // the sky map (nearest, clamped)
constexpr uint32_t kBindingTrails = 2;  // the trails' window (linear, repeating: the window is toroidal)
constexpr uint32_t kBindingColours = 3; // the ground's colours seen (storage: the gather writes them)
constexpr uint32_t kBindingSource = 4;  // the viewport's colour copied out
constexpr uint32_t kBindingDepth = 5;   // the depth (the gather's)
constexpr uint32_t kBindingSeen = 6;    // the ground's colours seen (sampled: the layer reads them)
constexpr uint32_t kBindingScene = 7;   // the frame's scene (the flakes' rays; only on a device with ray tracing)
constexpr uint32_t kBindingLamps = 8;   // the lamps' grid round the camera (uniform: three vec4 a cell)
constexpr uint32_t kBindingActive = 9;  // the trails' activity's window (linear, repeating: toroidal like the trails')
constexpr uint32_t kBindingCount = 10;
constexpr std::size_t kLampVectors = 3; // (a cell's vec4s)
constexpr std::size_t kLampGridVectors =
    kLampVectors * static_cast<std::size_t>(core::at(kSnowLampCells, 0) * core::at(kSnowLampCells, 1) * core::at(kSnowLampCells, 2));
static_assert(kLampGridVectors * sizeof(std::array<float, 4>) <= 16384, "the lamps' block within every device's uniform range");
constexpr uint32_t kGatherGroup = 8;
constexpr VkFormat kSkyFormat = VK_FORMAT_R16G16_UNORM;
constexpr VkFormat kTrailsFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr VkFormat kColoursFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr std::size_t kColourTexelBytes = 8;
constexpr std::size_t kTrailTexelBytes = 4;
constexpr VkDeviceSize kStagingAlignment = 16;
constexpr float kHalf = 0.5F;
constexpr double kClockWraps = 3600.0; // (seconds: the falling snow's clock kept small)
constexpr uint32_t kQuadVertices = 6;
constexpr uint32_t kStripVertices = 4;
constexpr float kFarStep = 2.0F;
constexpr float kAmbientShare = 1.0F - rt::kDirectShare;

// --- the shaders ---------------------------------------------------------------------------------------------------

std::string glsl_float(float value)
{
    std::string written = text::format("%.9g", static_cast<double>(value));
    if (written.find_first_of(".eEn") == std::string::npos) {
        written += ".0";
    }
    return written;
}

std::string constants_source()
{
    std::string out;
    const auto define = [&out](const char* name, float value) {
        out += "#define ";
        out += name;
        out += ' ';
        out += glsl_float(value);
        out += '\n';
    };
    define("STEP_HEIGHT", snow::kStepHeight);
    define("BERM_HEIGHT", snow::kBermHeight);
    define("TILE_TEXELS", static_cast<float>(snow::kTileTexels));
    define("GRAVITY", snow::kSprayGravity);
    define("PACKED_SHADE", kSnowPackedShade);
    define("LIFT", kSnowLift);
    define("FLAKE_BOX", kSnowFlakeBox);
    define("FLAKE_HEIGHT", kSnowFlakeHeight);
    define("NEAR_FLAKES", static_cast<float>(kSnowNearFlakes));
    define("NEAR_BOX", kSnowNearBox);
    define("NEAR_HEIGHT", kSnowNearHeight);
    define("FLAKE_LEAST", kSnowFlakeLeast);
    define("FLAKE_SPREAD", kSnowFlakeSpread);
    define("FLAKE_ALPHA", kSnowFlakeAlpha);
    define("FLAKE_SKY_REACH", kSnowSkyReach);
    define("FLAKE_SUN_REACH", kSnowSunReach);
    define("ALBEDO", kSnowAlbedo);
    out += "#define LAMP_VECTORS " + std::to_string(kLampGridVectors) + "\n";
    return out;
}

// What every stage shares: the block, the sky map's ground (snow::ground_at's twin), the trails' height
// (snow::layer_height's twin).
constexpr const char* kCommon = R"(
layout(std140, set = 0, binding = 0) uniform Block {
    mat4 vp;               // D3D's row-vector matrix as stored: vp * v is its v x M
    mat4 vp_before;        // the frame before's, unjittered (the motion)
    vec4 camera;           // xyz; w: the snow's clock (seconds, wrapped)
    vec4 trails;           // x: a texel (units); y: the window's texels a side; z, w: the window's first texel (x, z)
    vec4 snow;             // x: the depth; y: the near distance (units: every texel a vertex nearer); z: the activity's
                           // texel (units); w: its window's texels a side
    vec4 sky_map;          // x, y: the first cell's corner (x, z); z: a cell's size; w: unused
    vec4 sky_range;        // x: the heights' low; y: their span; z, w: the map's columns and rows
    vec4 light;            // xyz: toward the light; w: its direct share
    vec4 light_colour;     // rgb: the direct light's colour (x the share); w: the sky's share (the ambient)
    vec4 rect;             // the viewport: x, y, width, height (pixels)
    vec4 offsets;          // x, y: Direct3D 9's half pixel (1 / the size); z, w: the jitter (clip space)
    vec4 depth_projection; // P22, P32, P23, P33
    vec4 depth_map;        // x: the depth range's minimum; y: 1 / its span; z: 1 with a depth; w: unused
    vec4 right;            // xyz: the camera's right (world)
    vec4 up;               // xyz: the camera's up (world)
    vec4 flake;            // rgb: snow's colour in the light now; w: the flakes drawn
    vec4 wind;             // x, z: downwind; y: the near flakes' drift (units, wrapped); w: the far flakes'
    vec4 sky_light;        // rgb: the sky's light on the snow (x its share); w: unused
    vec4 lens;             // x: the focal length (pixels); y: the aperture (units); z: the focus (units); w: the least
                           // radius drawn (pixels)
    vec4 haze;             // rgb: the falling snow's haze's light; w: its extinction a unit (0: none)
    vec4 haze_range;       // x: where the haze starts (units); y: the sky's distance (units); z, w: unused
    vec4 lamps_origin;     // xyz: the lamps' grid's first cell's middle; w: a cell's size (0: no grid)
    vec4 lamps_size;       // xyz: the grid's cells (x, y, z); w: unused
} p;
layout(set = 0, binding = 1) uniform sampler2D u_sky;
layout(set = 0, binding = 2) uniform sampler2D u_trails;
layout(set = 0, binding = 9) uniform sampler2D u_active;
layout(push_constant) uniform Tile { ivec4 tile; } pc; // x, y: the tile's first texel (x, z); z: the vertices' step

vec3 sky_cell(ivec2 c)
{
    if (c.x < 0 || c.y < 0 || c.x >= int(p.sky_range.z) || c.y >= int(p.sky_range.w)) {
        return vec3(0.0);
    }
    vec2 v = texelFetch(u_sky, c, 0).rg;
    float t = floor(v.r * 65535.0 + 0.5);
    if (t < 0.5) {
        return vec3(0.0);
    }
    return vec3(p.sky_range.x + (t - 1.0) / 65534.0 * p.sky_range.y, v.g, 1.0);
}

// x: the ground's height; y: its cover; z: 1 a continuous surface, 2 a step (the nearest cell's), 0 none
vec3 ground_at(vec2 xz)
{
    vec2 f = (xz - p.sky_map.xy) / p.sky_map.z - 0.5;
    ivec2 c = ivec2(floor(f));
    vec2 t = f - vec2(c);
    vec3 a = sky_cell(c);
    vec3 b = sky_cell(c + ivec2(1, 0));
    vec3 d = sky_cell(c + ivec2(0, 1));
    vec3 e = sky_cell(c + ivec2(1, 1));
    bool all_found = a.z > 0.5 && b.z > 0.5 && d.z > 0.5 && e.z > 0.5;
    float least = min(min(a.x, b.x), min(d.x, e.x));
    float most = max(max(a.x, b.x), max(d.x, e.x));
    if (all_found && most - least <= STEP_HEIGHT) {
        vec2 top = mix(a.xy, b.xy, t.x);
        vec2 bottom = mix(d.xy, e.xy, t.x);
        return vec3(mix(top, bottom, t.y), 1.0);
    }
    vec3 n = t.y >= 0.5 ? (t.x >= 0.5 ? e : d) : (t.x >= 0.5 ? b : a);
    return vec3(n.xy, n.z > 0.5 ? 2.0 : 0.0);
}

vec4 trail_at(vec2 xz) { return textureLod(u_trails, xz / (p.trails.x * p.trails.y), 0.0); }
// the activity (its own coarse field: snow::TrailField::active_at's twin)
float active_at(vec2 xz) { return textureLod(u_active, xz / (p.snow.z * p.snow.w), 0.0).r; }

// the trails' window faded at its edges (the slots there wrap to the other side)
float window_fade(vec2 xz)
{
    vec2 f = (xz / p.trails.x - p.trails.zw) / p.trails.y;
    vec2 in_window = smoothstep(vec2(0.0), vec2(0.04), f) * (vec2(1.0) - smoothstep(vec2(0.96), vec2(1.0), f));
    return in_window.x * in_window.y;
}

float layer_height(vec4 trail, float raised, float cover)
{
    return p.snow.x * cover * (raised * (1.0 - trail.r) + BERM_HEIGHT * trail.g * (1.0 - trail.r));
}

vec4 to_target(vec4 clip)
{
    return vec4(clip.x + clip.w * p.offsets.x, -clip.y + clip.w * p.offsets.y, clip.z, clip.w);
}

vec3 finite_colour(vec3 c)
{
    return vec3(c.r >= 0.0 ? min(c.r, 64.0) : 0.0, c.g >= 0.0 ? min(c.g, 64.0) : 0.0, c.b >= 0.0 ? min(c.b, 64.0) : 0.0);
}
)";

// The gather: each texel of a tile's ground point on the screen; where the depth there is the ground's, its colour kept.
constexpr const char* kGatherBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 3, rgba16f) uniform image2D u_colours;
layout(set = 0, binding = 4) uniform sampler2D u_source;
#ifdef MULTISAMPLED
layout(set = 0, binding = 5) uniform sampler2DMS u_depth;
#else
layout(set = 0, binding = 5) uniform sampler2D u_depth;
#endif

float linear_depth(float d)
{
    float ndc = (d - p.depth_map.x) * p.depth_map.y;
    float z = (p.depth_projection.y - ndc * p.depth_projection.w) / (ndc * p.depth_projection.z - p.depth_projection.x);
    float w = z * p.depth_projection.z + p.depth_projection.w;
    return abs(p.depth_projection.z) > 0.5 ? w : abs(z);
}

void main()
{
    ivec2 texel = pc.tile.xy + ivec2(gl_GlobalInvocationID.xy);
    vec2 xz = (vec2(texel) + 0.5) * p.trails.x;
    vec3 g = ground_at(xz);
    if (g.z < 0.5) {
        return;
    }
    vec4 clip = p.vp * vec4(xz.x, g.x, xz.y, 1.0);
    if (!(clip.w > 1.0e-3)) {
        return;
    }
    vec2 ndc = clip.xy / clip.w;
    vec2 at = vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5) * p.rect.zw;
    if (any(lessThan(at, vec2(0.0))) || any(greaterThanEqual(at, p.rect.zw))) {
        return;
    }
    ivec2 pixel = ivec2(floor(at)) + ivec2(p.rect.xy);
    if (p.depth_map.z > 0.5) {
        float z = linear_depth(texelFetch(u_depth, pixel, 0).r);
        if (!(abs(z - clip.w) <= clip.w * 0.03 + 6.0)) {
            return;
        }
    }
    int n = int(p.trails.y);
    ivec2 slot = ivec2(((texel.x % n) + n) % n, ((texel.y % n) + n) % n);
    imageStore(u_colours, slot, vec4(finite_colour(texelFetch(u_source, pixel, 0).rgb), 1.0));
}
)";

// The layer: a tile's grid, each vertex a trail texel's centre lifted over the ground.
constexpr const char* kLayerVertexBody = R"(
layout(location = 0) out vec3 v_world;
layout(location = 1) out float v_alive;
layout(location = 2) out float v_cover;
layout(location = 3) out float v_step;
#ifdef MOTION
layout(location = 4) out vec4 v_now;
layout(location = 5) out vec4 v_before;
#endif
out gl_PerVertex { vec4 gl_Position; };

const ivec2 CORNERS[6] = ivec2[6](ivec2(0, 0), ivec2(1, 0), ivec2(0, 1), ivec2(1, 0), ivec2(1, 1), ivec2(0, 1));

void main()
{
    int step = max(pc.tile.z, 1);
    int across = int(TILE_TEXELS) / step;
    int quad = gl_VertexIndex / 6;
    ivec2 q = ivec2(quad % across, quad / across);
    ivec2 texel = pc.tile.xy + (q + CORNERS[gl_VertexIndex % 6]) * step;
    vec2 xz = (vec2(texel) + 0.5) * p.trails.x;
    vec3 g = ground_at(xz);
    vec4 trail = trail_at(xz);
    float raised = active_at(xz) * window_fade(xz);
    float h = layer_height(trail, raised, g.y);
    vec3 world = vec3(xz.x, g.x + h + LIFT * min(raised * 4.0, 1.0), xz.y); // (lifted off the ground drawn under it)
    v_world = world;
    v_alive = g.z > 0.5 ? raised * g.y : 0.0;
    v_cover = g.y;
    v_step = g.z > 1.5 || g.z < 0.5 ? 1.0 : 0.0;
    vec4 clip = p.vp * vec4(world, 1.0);
    gl_Position = to_target(clip);
#ifdef MOTION
    v_now = vec4(clip.xy - p.offsets.zw * clip.w, 0.0, clip.w);
    v_before = p.vp_before * vec4(world, 1.0);
#endif
}
)";

constexpr const char* kLayerFragmentBody = R"(
layout(location = 0) in vec3 v_world;
layout(location = 1) in float v_alive;
layout(location = 2) in float v_cover;
layout(location = 3) in float v_step;
layout(location = 0) out vec4 o_colour;
#ifdef MOTION
layout(location = 4) in vec4 v_now;
layout(location = 5) in vec4 v_before;
layout(location = 1) out vec4 o_motion;
#endif
layout(set = 0, binding = 4) uniform sampler2D u_source;
layout(set = 0, binding = 6) uniform sampler2D u_seen;

// The trails' relief: the layer's height as if it were raised everywhere (the activity's fall at the raised area's edge
// left out - it is the layer meeting the ground's own snow, not a shape of the snow: its slope never shades)
float height_at(vec2 xz)
{
    vec4 trail = trail_at(xz);
    return p.snow.x * v_cover * ((1.0 - trail.r) + BERM_HEIGHT * trail.g * (1.0 - trail.r));
}

void main()
{
    if (v_step > 0.001 || v_alive < 0.004) {
        discard;
    }
    float texel = p.trails.x;
    vec2 xz = v_world.xz;
    float hc = height_at(xz);
    float hx = height_at(xz + vec2(texel, 0.0)) - height_at(xz - vec2(texel, 0.0));
    float hz = height_at(xz + vec2(0.0, texel)) - height_at(xz - vec2(0.0, texel));
    vec3 n = normalize(vec3(-hx / (2.0 * texel), 1.0, -hz / (2.0 * texel)));
    // the occlusion: lower than round it
    float around = 0.25 * (height_at(xz + vec2(2.0 * texel, 0.0)) + height_at(xz - vec2(2.0 * texel, 0.0)) +
                           height_at(xz + vec2(0.0, 2.0 * texel)) + height_at(xz - vec2(0.0, 2.0 * texel)));
    float ao = clamp(1.0 - max(around - hc, 0.0) / max(p.snow.x, 0.5) * 0.45, 0.6, 1.0);
    // the shadow of the trail's walls: a short march toward the light over the heights
    vec3 l = p.light.xyz;
    float lit = 1.0;
    if (l.y > 0.02) {
        vec2 along = normalize(l.xz + vec2(1.0e-5)) * texel;
        float rise = l.y / max(length(l.xz), 1.0e-3) * texel;
        for (int i = 1; i <= 6; ++i) {
            float above = height_at(xz + along * float(i)) - (hc + rise * float(i));
            lit = min(lit, 1.0 - smoothstep(0.0, 0.6, above));
        }
    }
    float direct = p.light.w;
    float flat_light = (1.0 - direct) + direct * max(l.y, 0.0);
    float own_light = (1.0 - direct) * ao * (0.6 + 0.4 * n.y) + direct * max(dot(n, l), 0.0) * mix(0.35, 1.0, lit);
    float turn = mix(1.0, own_light / max(flat_light, 0.05), 0.8);
    // the ground's colour under it (seen by the gather; never seen: what is behind this pixel)
    vec4 seen = texture(u_seen, xz / (p.trails.x * p.trails.y));
    vec3 behind = finite_colour(texelFetch(u_source, ivec2(gl_FragCoord.xy), 0).rgb);
    vec3 base = seen.a > 0.05 ? seen.rgb / seen.a : behind;
    float packed = trail_at(xz).r;
    vec3 colour = base * clamp(turn, 0.4, 1.5) * mix(1.0, PACKED_SHADE, packed);
    vec3 v = normalize(p.camera.xyz - v_world);
    vec3 h = normalize(l + v);
    colour += p.light_colour.rgb * (0.3 * packed * lit * pow(max(dot(n, h), 0.0), 40.0));
    o_colour = vec4(finite_colour(colour), 1.0);
#ifdef MOTION
    vec2 now = v_now.xy / v_now.w;
    vec2 before = v_before.w > 0.0 ? v_before.xy / v_before.w : now;
    o_motion = vec4((before - now) * vec2(0.5, -0.5), 0.0, 1.0);
#endif
}
)";

// The spray: a particle a quad (an instance), thrown from its start.
constexpr const char* kSprayVertexBody = R"(
layout(location = 0) in vec4 a_start;    // xyz where; w: its age (seconds)
layout(location = 1) in vec4 a_velocity; // xyz; w: its life
layout(location = 2) in vec4 a_look;     // x: its size; y: its shade
layout(location = 0) out vec2 v_corner;
layout(location = 1) out vec4 v_colour;
out gl_PerVertex { vec4 gl_Position; };

void main()
{
    float age = a_start.w;
    float life = max(a_velocity.w, 1.0e-3);
    float share = clamp(age / life, 0.0, 1.0);
    vec3 at = a_start.xyz + a_velocity.xyz * age - vec3(0.0, 0.5 * GRAVITY * age * age, 0.0);
    vec2 corner = vec2((gl_VertexIndex & 1) == 0 ? -1.0 : 1.0, (gl_VertexIndex & 2) == 0 ? -1.0 : 1.0);
    float size = a_look.x * (1.0 + 1.5 * share);
    vec3 world = at + (p.right.xyz * corner.x + p.up.xyz * corner.y) * size;
    gl_Position = age < life ? to_target(p.vp * vec4(world, 1.0)) : vec4(0.0);
    v_corner = corner;
    v_colour = vec4(p.flake.rgb * (0.92 + 0.16 * a_look.y), 0.75 * pow(1.0 - share, 1.5));
}
)";

// The falling snow: flakes in the world. A flake a quad (an instance), its place a hash of its number falling through a
// box round the camera - the first NEAR_FLAKES in a small box (the dense snow close by), the rest in the big one -,
// carried downwind by the level's wind (engine/gfx/wind.h: the drift its gusts add up to, on the snow's clock) and
// fluttering. Drawn as a disc of its true size on the screen, widened by the lens's defocus near the camera and to at
// least lens.w pixels far off, its alpha the share of the disc the flake covers (big soft near flakes, tiny faint far
// ones). Lit by the light now (the lighting state's: the sky's light, the direct light where it reaches the flake -
// TRACED: a ray toward it through the frame's scene - scattered forward, so flakes glint looking toward the sun or the
// moon, each flake's own twinkle) and the level's lamps round it (the light field's local light, a grid round the
// camera). Not drawn under a roof (TRACED: a ray straight up; else the sky map's top over it).
constexpr const char* kFlakeVertexBody = R"(
layout(location = 0) out vec2 v_corner;
layout(location = 1) out vec4 v_colour;
layout(location = 2) out float v_soft;
out gl_PerVertex { vec4 gl_Position; };
layout(std140, set = 0, binding = 8) uniform Lamps { vec4 lamp[LAMP_VECTORS]; } lamps;
#ifdef TRACED
layout(set = 0, binding = 7) uniform accelerationStructureEXT u_scene;
bool blocked(vec3 origin, vec3 direction, float reach, uint mask)
{
    rayQueryEXT q;
    rayQueryInitializeEXT(q, u_scene, gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT, mask, origin, 0.0,
                          direction, reach);
    while (rayQueryProceedEXT(q)) {
    }
    return rayQueryGetIntersectionTypeEXT(q, true) != gl_RayQueryCommittedIntersectionNoneEXT;
}
#endif

uint hash(uint x)
{
    x ^= x >> 16u;
    x *= 0x7FEB352Du;
    x ^= x >> 15u;
    x *= 0x846CA68Bu;
    x ^= x >> 16u;
    return x;
}
float unit(uint x) { return float(x >> 8u) / 16777216.0; }

// The lamps' light at a point (the grid's cells blended, trilinear): the ambient part, and the directional part as it
// reaches the eye through a flake (scattered forward toward where the light goes).
vec3 lamp_cell(ivec3 c, vec3 view)
{
    int index = (c.z * int(p.lamps_size.y) + c.y) * int(p.lamps_size.x) + c.x;
    vec4 a = lamps.lamp[index * 3];
    vec4 b = lamps.lamp[index * 3 + 1];
    vec4 d = lamps.lamp[index * 3 + 2];
    vec3 toward = vec3(a.w, b.w, d.w);
    float cosine = dot(view, toward); // (looking toward the lamps through the flake: their light scattered forward)
    return a.rgb + b.rgb * (0.35 + 1.8 * pow(max(cosine, 0.0), 6.0));
}
vec3 lamp_light(vec3 at, vec3 view)
{
    if (p.lamps_origin.w <= 0.0) {
        return vec3(0.0);
    }
    vec3 most = p.lamps_size.xyz - 1.0;
    vec3 f = clamp((at - p.lamps_origin.xyz) / p.lamps_origin.w, vec3(0.0), most);
    ivec3 c = ivec3(min(floor(f), max(most - 1.0, vec3(0.0))));
    vec3 t = f - vec3(c);
    ivec3 e = min(c + 1, ivec3(most));
    vec3 x00 = mix(lamp_cell(ivec3(c.x, c.y, c.z), view), lamp_cell(ivec3(e.x, c.y, c.z), view), t.x);
    vec3 x10 = mix(lamp_cell(ivec3(c.x, e.y, c.z), view), lamp_cell(ivec3(e.x, e.y, c.z), view), t.x);
    vec3 x01 = mix(lamp_cell(ivec3(c.x, c.y, e.z), view), lamp_cell(ivec3(e.x, c.y, e.z), view), t.x);
    vec3 x11 = mix(lamp_cell(ivec3(c.x, e.y, e.z), view), lamp_cell(ivec3(e.x, e.y, e.z), view), t.x);
    return mix(mix(x00, x10, t.y), mix(x01, x11, t.y), t.z);
}

void main()
{
    uint i = uint(gl_InstanceIndex);
    bool near = i < uint(NEAR_FLAKES);
    float box = near ? NEAR_BOX : FLAKE_BOX;
    float height = near ? NEAR_HEIGHT : FLAKE_HEIGHT;
    float drift = near ? p.wind.y : p.wind.w;
    float hx = unit(hash(i * 5u + 1u));
    float hy = unit(hash(i * 5u + 2u));
    float hz = unit(hash(i * 5u + 3u));
    float hs = unit(hash(i * 5u + 4u));
    float hp = unit(hash(i * 5u + 5u));
    float t = p.camera.w;
    float fall = 38.0 + 30.0 * hs;
    vec2 downwind = vec2(p.wind.x, p.wind.z);
    vec2 flutter = vec2(sin(t * (0.9 + hp) + hx * 40.0), cos(t * (0.7 + hp) + hz * 40.0)) * (2.0 + 4.0 * hp);
    vec2 shift = downwind * drift + flutter;
    vec3 world;
    float rise = mod(hy * height - fall * t, height);
    world.y = p.camera.y - 0.4 * height + rise;
    world.x = p.camera.x + (fract((hx * box + shift.x - p.camera.x) / box) - 0.5) * box;
    world.z = p.camera.z + (fract((hz * box + shift.y - p.camera.z) / box) - 0.5) * box;
    vec3 to_flake = world - p.camera.xyz;
    float flat_distance = length(to_flake.xz);
    float distance = length(to_flake);
    vec3 view = to_flake / max(distance, 1.0e-3);
    // faded at its box's sides, top and bottom (the box wraps there) and far off
    float edge = rise / height;
    float fade = (1.0 - smoothstep(0.36 * box, 0.5 * box, flat_distance)) * smoothstep(0.0, 0.08, edge) *
                 (1.0 - smoothstep(0.9, 1.0, edge));
#ifdef TRACED
    bool sheltered = blocked(world, vec3(0.0, 1.0, 0.0), FLAKE_SKY_REACH, 0x05u);
    float lit = p.light.y > 0.0 && !blocked(world + p.light.xyz * 0.5, p.light.xyz, FLAKE_SUN_REACH, 0x07u) ? 1.0 : 0.0;
#else
    vec2 f = (world.xz - p.sky_map.xy) / p.sky_map.z;
    vec3 top = sky_cell(ivec2(floor(f)));
    bool sheltered = top.z > 0.5 && top.x > world.y + 4.0;
    float lit = 1.0;
#endif
    // its light: the sky's, the direct light scattered forward (a glint looking toward it) with its twinkle, the lamps'
    float forward = pow(max(dot(view, p.light.xyz), 0.0), 10.0);
    float phase = 0.6 + 2.4 * forward;
    float twinkle = 1.0 + 1.2 * pow(0.5 + 0.5 * sin(t * (6.0 + 9.0 * hp) + hs * 60.0), 24.0);
    vec3 light = p.sky_light.rgb + p.light_colour.rgb * (lit * phase * twinkle) + lamp_light(world, view);
    // its size on the screen: the true disc, widened by the defocus, at least lens.w pixels
    vec4 clip = p.vp * vec4(world, 1.0);
    float w = max(clip.w, 1.0e-3);
    float radius = (FLAKE_LEAST + FLAKE_SPREAD * hs) * p.lens.x / w;
    float blur = p.lens.y * p.lens.x * abs(1.0 / w - 1.0 / p.lens.z);
    float drawn = max(sqrt(radius * radius + blur * blur), p.lens.w);
    float coverage = clamp((radius * radius) / (drawn * drawn), 0.0, 1.0);
    vec2 corner = vec2((gl_VertexIndex & 1) == 0 ? -1.0 : 1.0, (gl_VertexIndex & 2) == 0 ? -1.0 : 1.0);
    clip.xy += corner * drawn * 2.0 / p.rect.zw * clip.w;
    bool hidden = sheltered || fade <= 0.0 || clip.w < 2.0 || coverage * fade < 0.002;
    gl_Position = hidden ? vec4(0.0) : to_target(clip);
    v_corner = corner;
    v_soft = clamp(blur / drawn, 0.0, 1.0);
    v_colour = vec4(ALBEDO * light * (0.92 + 0.16 * hs), FLAKE_ALPHA * coverage * fade);
}
)";

// The air's discs: a crisp flake's edge, a defocused one's soft falloff. With the motion attachment, the share of the
// pixel the disc took (z, the largest: the upscaler's reactive mask) - its motion left as the scene's behind it.
constexpr const char* kAirFragmentBody = R"(
layout(location = 0) in vec2 v_corner;
layout(location = 1) in vec4 v_colour;
#ifdef FLAKES
layout(location = 2) in float v_soft;
#endif
layout(location = 0) out vec4 o_colour;
#ifdef MOTION
layout(location = 1) out vec4 o_motion;
#endif

void main()
{
    float r = length(v_corner);
#ifdef FLAKES
    float crisp = 1.0 - smoothstep(0.55, 1.0, r);
    float soft = max(1.0 - r * r, 0.0);
    float alpha = v_colour.a * mix(crisp, soft * soft * 1.6, v_soft);
#else
    float alpha = v_colour.a * (1.0 - smoothstep(0.45, 1.0, r));
#endif
    if (alpha <= 0.002) {
        discard;
    }
    alpha = min(alpha, 1.0);
    o_colour = vec4(finite_colour(v_colour.rgb), alpha);
#ifdef MOTION
    o_motion = vec4(0.0, 0.0, alpha, 0.0);
#endif
}
)";

// The falling snow's haze: what the snow too far off to draw as flakes does to the picture - a medium of the snow's
// light, its extinction p.haze.w a unit from p.haze_range.x out (the sky: as far as p.haze_range.y), blended over the
// world (colour x T + the haze's light x (1 - T)). A full-screen triangle; the depth read, not attached.
constexpr const char* kHazeVertexBody = R"(
out gl_PerVertex { vec4 gl_Position; };
void main()
{
    vec2 corner = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
)";
constexpr const char* kHazeFragmentBody = R"(
layout(location = 0) out vec4 o_colour;
#ifdef MULTISAMPLED
layout(set = 0, binding = 5) uniform sampler2DMS u_depth;
#else
layout(set = 0, binding = 5) uniform sampler2D u_depth;
#endif

float linear_depth(float d)
{
    float ndc = (d - p.depth_map.x) * p.depth_map.y;
    float z = (p.depth_projection.y - ndc * p.depth_projection.w) / (ndc * p.depth_projection.z - p.depth_projection.x);
    float w = z * p.depth_projection.z + p.depth_projection.w;
    return abs(p.depth_projection.z) > 0.5 ? w : abs(z);
}

void main()
{
    float d = texelFetch(u_depth, ivec2(gl_FragCoord.xy), 0).r;
    float distance = d >= 1.0 ? p.haze_range.y : min(linear_depth(d), p.haze_range.y);
    float tau = p.haze.w * max(distance - p.haze_range.x, 0.0);
    float through = exp(-min(tau, 40.0));
    o_colour = vec4(finite_colour(p.haze.rgb) * (1.0 - through), through);
}
)";

// The block, as the shaders' std140 block lays it out.
struct Block {
    Float4x4 vp{};
    Float4x4 vp_before{};
    std::array<float, 4> camera{};
    std::array<float, 4> trails{};
    std::array<float, 4> snow{};
    std::array<float, 4> sky_map{};
    std::array<float, 4> sky_range{};
    std::array<float, 4> light{};
    std::array<float, 4> light_colour{};
    std::array<float, 4> rect{};
    std::array<float, 4> offsets{};
    std::array<float, 4> depth_projection{};
    std::array<float, 4> depth_map{};
    std::array<float, 4> right{};
    std::array<float, 4> up{};
    std::array<float, 4> flake{};
    std::array<float, 4> wind{};
    std::array<float, 4> sky_light{};
    std::array<float, 4> lens{};
    std::array<float, 4> haze{};
    std::array<float, 4> haze_range{};
    std::array<float, 4> lamps_origin{};
    std::array<float, 4> lamps_size{};
};
constexpr std::size_t kBlockBytes = 464;
static_assert(sizeof(Block) == kBlockBytes, "the shaders' Block");

struct TileConstants {
    std::array<int32_t, 4> tile{};
};

enum class DrawKind : uint8_t { kLayer, kSpray, kFlakes, kHaze };

// The passes' timestamps (a set a frame, used in turn): the layer's start and end, the air's start, after its flakes and
// spray, after the haze.
constexpr uint32_t kTimerSets = 8;
constexpr uint32_t kTimerPoints = 5;
constexpr uint32_t kLayerStart = 0;
constexpr uint32_t kLayerEnd = 1;
constexpr uint32_t kAirStart = 2;
constexpr uint32_t kAirFlakes = 3;
constexpr uint32_t kAirHaze = 4;
constexpr uint64_t kTimingLogFrames = 600;

struct DrawPipeline {
    DrawKind kind = DrawKind::kLayer;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkFormat depth = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    bool motion = false;
    bool traced = false;       // (the flakes': their rays through the frame's scene)
    bool multisampled = false; // (the haze's: the depth it reads)
    VkPipeline pipeline = VK_NULL_HANDLE;
};

// The light the snow is turned to (the cover's own light is the world's: this is only the layer's and the air's).
struct SnowLight {
    std::array<float, 3> toward{0.0F, 1.0F, 0.0F};
    float direct = rt::kDirectShare;
    std::array<float, 3> colour{1.0F, 1.0F, 1.0F}; // the direct light (x the share)
    std::array<float, 3> ambient{kAmbientShare, kAmbientShare, kAmbientShare};
};

struct SnowState {
    SnowSettings settings;
    // the sky map on the device
    std::shared_ptr<const snow::SkyMap> sky_uploaded;
    Image sky;
    // the trails' window and the ground's colours seen
    Image trails;
    Image colours;
    TrailWindow window; // (the surface trails' window: vulkan_surface_trail.h)
    Image active;
    TrailWindow active_window;             // (the activity's: as many of its tiles as cover the trails' window and more)
    std::vector<std::array<int, 2>> drawn; // the trails' tiles in the window the activity raised (the layer's)
    uint64_t window_frame = 0;   // the frame whose first view placed it
    // the view
    rt::ViewDrawn view;
    bool view_known = false;
    uint64_t view_serial = 0;
    uint64_t block_serial = 0;
    uint64_t block_frame = 0;
    VkDescriptorBufferInfo cover_block{};
    // the passes
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkSampler nearest = VK_NULL_HANDLE;
    VkSampler repeat = VK_NULL_HANDLE;
    std::array<VkPipeline, 2> gather{}; // single-sampled depth, multisampled
    std::array<VkShaderModule, 2> layer_vertex{};
    std::array<VkShaderModule, 2> layer_fragment{};
    VkShaderModule spray_vertex = VK_NULL_HANDLE;
    std::array<VkShaderModule, 2> flake_vertex{};   // plain, traced
    std::array<VkShaderModule, 4> air_fragment{};   // [motion x 2 + flakes]
    VkShaderModule haze_vertex = VK_NULL_HANDLE;
    std::array<VkShaderModule, 2> haze_fragment{};  // single-sampled depth, multisampled
    bool scene_binding = false;                     // the set layout has kBindingScene
    std::vector<DrawPipeline> draws;
    Image source;
    bool failed = false;
    SnowStats stats;
    // the passes' time on the device: the layer's (gather + layer) and the air's (flakes + spray, haze)
    VkQueryPool timer = VK_NULL_HANDLE;
    double tick_milliseconds = 0.0;  // (< 0: the device can't time)
    std::array<uint64_t, kTimerSets> timer_frames{}; // the frame each set was written in (0: never)
    std::array<uint8_t, kTimerSets> timer_written{}; // its points written (bits)
    uint64_t timed = 0;
    std::array<double, 3> window_milliseconds{}; // layer, flakes and spray, haze
    std::array<uint64_t, 3> window_samples{};
    uint64_t window_frames = 0;
};

SnowState& snow_state()
{
    static SnowState state;
    return state;
}

bool first_report(Backend& state, uint64_t what) { return state.reported.insert(what).second; }

class SnowListener final : public rt::ViewListener {
public:
    [[nodiscard]] int order() const override { return rt::ViewOrder::kSnow; }
    void view_begins(const rt::ViewDrawn& view) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (state.device_made && state.draws_made) {
            begin_snow_view(state, view);
        }
    }
    void opaque_drawn(const rt::ViewDrawn& view) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (state.device_made && state.draws_made) {
            record_snow_layer(state, view);
        }
    }
    void world_drawn(const rt::ViewDrawn& view) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (state.device_made && state.draws_made) {
            record_snow_air(state, view);
        }
        snow_state().view_known = false;
    }
};

SnowListener& listener()
{
    static SnowListener instance;
    return instance;
}

using Matrix = std::array<std::array<float, 4>, 4>;

Float4x4 flat(const Matrix& matrix)
{
    Float4x4 out{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            core::at(out, (row * 4) + column) = core::at(core::at(matrix, row), column);
        }
    }
    return out;
}

Matrix multiply(const Matrix& a, const Matrix& b)
{
    Matrix out{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            float sum = 0.0F;
            for (std::size_t k = 0; k < 4; ++k) {
                sum += core::at(core::at(a, row), k) * core::at(core::at(b, k), column);
            }
            core::at(core::at(out, row), column) = sum;
        }
    }
    return out;
}

// The light the layer and the air are turned to: the time of day's when its clock lights the level, else the level's
// own sun (the console's, the bake's, the game's) at the level's own light.
SnowLight snow_light()
{
    SnowLight light;
    const rt::DayLight day = rt::day_light();
    std::array<float, 3> toward{};
    bool found = false;
    if (day.on) {
        toward = day.light;
        found = true;
        for (std::size_t c = 0; c < 3; ++c) {
            core::at(light.colour, c) = rt::kDirectShare * core::at(day.light_colour, c);
            core::at(light.ambient, c) = kAmbientShare * core::at(day.sky, c);
        }
    } else if (rt::sun_override(&toward)) {
        found = true;
    } else if (rt::baked_sun().found) {
        toward = rt::baked_sun().toward;
        found = true;
    } else {
        found = rt::game_sun(&toward);
    }
    const float length = std::sqrt((core::at(toward, 0) * core::at(toward, 0)) + (core::at(toward, 1) * core::at(toward, 1)) +
                                   (core::at(toward, 2) * core::at(toward, 2)));
    if (found && length > 0.0F) {
        light.toward = {core::at(toward, 0) / length, core::at(toward, 1) / length, core::at(toward, 2) / length};
    } else {
        constexpr float kOverheadDirect = 0.4F; // (no sun known: a soft light from above)
        light.toward = {0.0F, 1.0F, 0.0F};
        light.direct = kOverheadDirect;
        light.colour = {kOverheadDirect, kOverheadDirect, kOverheadDirect};
        light.ambient = {1.0F - kOverheadDirect, 1.0F - kOverheadDirect, 1.0F - kOverheadDirect};
    }
    return light;
}

// The lamps' grid round the camera: where its first cell's middle is (the camera's cell in the middle of it).
std::array<float, 3> lamp_origin(const std::array<float, 3>& camera)
{
    std::array<float, 3> origin{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const float cells = static_cast<float>(core::at(kSnowLampCells, axis) - 1) * kHalf;
        core::at(origin, axis) = (std::round(core::at(camera, axis) / kSnowLampCell) - cells) * kSnowLampCell;
    }
    return origin;
}

// The level's own lights (the light field's local light: rt_light_field.h, the one lighting path's) at each cell of the
// grid - three vec4 a cell: the ambient part and toward.x, the directional part and toward.y, toward.z. None without a
// field (a level not relit: the flakes take the sky's and the direct light alone).
std::vector<std::array<float, 4>> lamp_cells(const std::array<float, 3>& origin, bool* any)
{
    const std::shared_ptr<const rt::LightField> field = rt::light_field();
    const auto count = static_cast<std::size_t>(core::at(kSnowLampCells, 0) * core::at(kSnowLampCells, 1) * core::at(kSnowLampCells, 2));
    std::vector<std::array<float, 4>> cells(count * kLampVectors, std::array<float, 4>{});
    *any = false;
    if (field == nullptr) {
        return cells;
    }
    std::size_t index = 0;
    for (int z = 0; z < core::at(kSnowLampCells, 2); ++z) {
        for (int y = 0; y < core::at(kSnowLampCells, 1); ++y) {
            for (int x = 0; x < core::at(kSnowLampCells, 0); ++x) {
                const rt::FieldPoint at{core::at(origin, 0) + (static_cast<float>(x) * kSnowLampCell),
                                        core::at(origin, 1) + (static_cast<float>(y) * kSnowLampCell),
                                        core::at(origin, 2) + (static_cast<float>(z) * kSnowLampCell)};
                const rt::LocalLight local = field->local_at(at);
                *any = *any || local.any();
                core::at(cells, index++) = {core::at(local.ambient, 0), core::at(local.ambient, 1), core::at(local.ambient, 2),
                                            core::at(local.toward, 0)};
                core::at(cells, index++) = {core::at(local.directional, 0), core::at(local.directional, 1),
                                            core::at(local.directional, 2), core::at(local.toward, 1)};
                core::at(cells, index++) = {0.0F, 0.0F, 0.0F, core::at(local.toward, 2)};
            }
        }
    }
    return cells;
}

// --- the objects ----------------------------------------------------------------------------------------------------

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

std::string source_of(const char* body, const char* define)
{
    std::string source = "#version 460\n";
    if (define != nullptr) {
        source += "#define ";
        source += define;
        source += '\n';
    }
    source += constants_source();
    source += kCommon;
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

VkSampler make_sampler(VkDevice device, VkFilter filter, VkSamplerAddressMode address)
{
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = filter;
    info.minFilter = filter;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = address;
    info.addressModeV = address;
    info.addressModeW = address;
    info.maxLod = 0.0F;
    VkSampler sampler = VK_NULL_HANDLE;
    if (vkCreateSampler(device, &info, nullptr, &sampler) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return sampler;
}

bool make_layouts(Backend& state, std::string* error)
{
    SnowState& own = snow_state();
    const VkDevice device = state.context.device();
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    own.scene_binding = state.context.ray_tracing_enabled();
    for (uint32_t binding = 0; binding < kBindingCount; ++binding) {
        VkDescriptorType type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        if (binding == kBindingBlock) {
            type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        } else if (binding == kBindingColours) {
            type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        } else if (binding == kBindingLamps) {
            type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        } else if (binding == kBindingScene) {
            if (!own.scene_binding) {
                continue;
            }
            type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        }
        bindings.push_back({.binding = binding, .descriptorType = type, .descriptorCount = 1, .stageFlags = kStages, .pImmutableSamplers = nullptr});
    }
    const VkDescriptorSetLayoutCreateInfo set_info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                   .pNext = nullptr,
                                                   .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
                                                   .bindingCount = static_cast<uint32_t>(bindings.size()),
                                                   .pBindings = bindings.data()};
    own.nearest = make_sampler(device, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    own.repeat = make_sampler(device, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    if (own.nearest == VK_NULL_HANDLE || own.repeat == VK_NULL_HANDLE ||
        vkCreateDescriptorSetLayout(device, &set_info, nullptr, &own.set_layout) != VK_SUCCESS) {
        *error = "the descriptor layout or the samplers";
        return false;
    }
    const VkPushConstantRange constants{.stageFlags = kStages, .offset = 0, .size = sizeof(TileConstants)};
    const VkPipelineLayoutCreateInfo layout_info{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                 .pNext = nullptr,
                                                 .flags = 0,
                                                 .setLayoutCount = 1,
                                                 .pSetLayouts = &own.set_layout,
                                                 .pushConstantRangeCount = 1,
                                                 .pPushConstantRanges = &constants};
    if (vkCreatePipelineLayout(device, &layout_info, nullptr, &own.layout) != VK_SUCCESS) {
        own.layout = VK_NULL_HANDLE;
        *error = "the pipeline layout";
        return false;
    }
    return true;
}

bool ensure_objects(Backend& state, std::string* error)
{
    SnowState& own = snow_state();
    if (own.haze_vertex != VK_NULL_HANDLE) {
        return true;
    }
    const VkDevice device = state.context.device();
    if (own.layout == VK_NULL_HANDLE && !make_layouts(state, error)) {
        return false;
    }
    for (std::size_t variant = 0; variant < 2; ++variant) {
        if (core::at(own.gather, variant) == VK_NULL_HANDLE) {
            core::at(own.gather, variant) =
                make_compute(device, own.layout, source_of(kGatherBody, variant == 1 ? "MULTISAMPLED" : nullptr), error);
            if (core::at(own.gather, variant) == VK_NULL_HANDLE) {
                return false;
            }
        }
        const char* motion = variant == 1 ? "MOTION" : nullptr;
        if (core::at(own.layer_vertex, variant) == VK_NULL_HANDLE) {
            core::at(own.layer_vertex, variant) =
                make_module(device, RtShaderStage::kVertex, source_of(kLayerVertexBody, motion), error);
        }
        if (core::at(own.layer_fragment, variant) == VK_NULL_HANDLE) {
            core::at(own.layer_fragment, variant) =
                make_module(device, RtShaderStage::kFragment, source_of(kLayerFragmentBody, motion), error);
        }
        for (std::size_t flakes = 0; flakes < 2; ++flakes) {
            VkShaderModule& module = core::at(own.air_fragment, (variant * 2) + flakes);
            if (module == VK_NULL_HANDLE) {
                std::string defines = variant == 1 ? "MOTION\n#define " : "";
                defines += flakes == 1 ? "FLAKES" : "SPRAY";
                module = make_module(device, RtShaderStage::kFragment, source_of(kAirFragmentBody, defines.c_str()), error);
            }
            if (module == VK_NULL_HANDLE) {
                return false;
            }
        }
        if (core::at(own.haze_fragment, variant) == VK_NULL_HANDLE) {
            core::at(own.haze_fragment, variant) = make_module(device, RtShaderStage::kFragment,
                                                               source_of(kHazeFragmentBody, variant == 1 ? "MULTISAMPLED" : nullptr), error);
        }
        if (core::at(own.layer_vertex, variant) == VK_NULL_HANDLE ||
            core::at(own.layer_fragment, variant) == VK_NULL_HANDLE || core::at(own.haze_fragment, variant) == VK_NULL_HANDLE) {
            return false;
        }
    }
    if (own.spray_vertex == VK_NULL_HANDLE) {
        own.spray_vertex = make_module(device, RtShaderStage::kVertex, source_of(kSprayVertexBody, nullptr), error);
    }
    if (core::at(own.flake_vertex, 0) == VK_NULL_HANDLE) {
        core::at(own.flake_vertex, 0) = make_module(device, RtShaderStage::kVertex, source_of(kFlakeVertexBody, nullptr), error);
    }
    if (own.scene_binding && core::at(own.flake_vertex, 1) == VK_NULL_HANDLE) {
        core::at(own.flake_vertex, 1) = make_module(device, RtShaderStage::kVertex,
                                                    source_of(kFlakeVertexBody, "TRACED\n#extension GL_EXT_ray_query : require"), error);
    }
    if (own.spray_vertex == VK_NULL_HANDLE || core::at(own.flake_vertex, 0) == VK_NULL_HANDLE ||
        (own.scene_binding && core::at(own.flake_vertex, 1) == VK_NULL_HANDLE)) {
        return false;
    }
    own.haze_vertex = make_module(device, RtShaderStage::kVertex, source_of(kHazeVertexBody, nullptr), error);
    return own.haze_vertex != VK_NULL_HANDLE;
}

// A draw's pipeline for the targets' formats and samples (made on first use). The layer: triangles, no culling, the
// depth tested and written, the colour's rgb written. The air: strips blended by their alpha over the colour, the depth
// tested, not written; the motion (when bound) left as it is.
VkPipeline draw_pipeline(Backend& state, const DrawPipeline& wanted, std::string* error)
{
    SnowState& own = snow_state();
    for (const DrawPipeline& made : own.draws) {
        if (made.kind == wanted.kind && made.format == wanted.format && made.depth == wanted.depth &&
            made.samples == wanted.samples && made.motion == wanted.motion && made.traced == wanted.traced &&
            made.multisampled == wanted.multisampled) {
            return made.pipeline;
        }
    }
    const bool layer = wanted.kind == DrawKind::kLayer;
    const bool haze = wanted.kind == DrawKind::kHaze;
    const std::size_t variant = wanted.motion ? 1 : 0;
    VkShaderModule vertex_module = core::at(own.layer_vertex, variant);
    VkShaderModule fragment_module = core::at(own.layer_fragment, variant);
    if (wanted.kind == DrawKind::kSpray) {
        vertex_module = own.spray_vertex;
        fragment_module = core::at(own.air_fragment, variant * 2);
    } else if (wanted.kind == DrawKind::kFlakes) {
        vertex_module = core::at(own.flake_vertex, wanted.traced ? 1 : 0);
        fragment_module = core::at(own.air_fragment, (variant * 2) + 1);
    } else if (haze) {
        vertex_module = own.haze_vertex;
        fragment_module = core::at(own.haze_fragment, wanted.multisampled ? 1 : 0);
    }
    const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .pNext = nullptr,
         .flags = 0,
         .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = vertex_module,
         .pName = "main",
         .pSpecializationInfo = nullptr},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .pNext = nullptr,
         .flags = 0,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = fragment_module,
         .pName = "main",
         .pSpecializationInfo = nullptr},
    }};
    // the spray's particles: an instance each (where, its age; its velocity, its life; its size and shade)
    const VkVertexInputBindingDescription spray_binding{
        .binding = 0, .stride = sizeof(snow::SprayParticle), .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE};
    constexpr uint32_t kSprayAttributes = 3;
    std::array<VkVertexInputAttributeDescription, kSprayAttributes> spray_attributes{};
    for (uint32_t index = 0; index < kSprayAttributes; ++index) {
        core::at(spray_attributes, index) = {.location = index,
                                             .binding = 0,
                                             .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                                             .offset = index * static_cast<uint32_t>(sizeof(std::array<float, 4>))};
    }
    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    if (wanted.kind == DrawKind::kSpray) {
        vertex_input.vertexBindingDescriptionCount = 1;
        vertex_input.pVertexBindingDescriptions = &spray_binding;
        vertex_input.vertexAttributeDescriptionCount = kSprayAttributes;
        vertex_input.pVertexAttributeDescriptions = spray_attributes.data();
    }
    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = layer || haze ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1.0F;
    const VkPipelineMultisampleStateCreateInfo multisample{.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                                           .pNext = nullptr,
                                                           .flags = 0,
                                                           .rasterizationSamples = wanted.samples,
                                                           .sampleShadingEnable = VK_FALSE,
                                                           .minSampleShading = 0.0F,
                                                           .pSampleMask = nullptr,
                                                           .alphaToCoverageEnable = VK_FALSE,
                                                           .alphaToOneEnable = VK_FALSE};
    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = haze ? VK_FALSE : VK_TRUE;
    depth.depthWriteEnable = layer ? VK_TRUE : VK_FALSE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    std::array<VkPipelineColorBlendAttachmentState, 2> blend{};
    VkPipelineColorBlendAttachmentState& colour_blend = core::at(blend, 0);
    colour_blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
    if (!layer) {
        // the air: over the colour by its alpha; the haze: colour x its transmittance (alpha) + its light
        colour_blend.blendEnable = VK_TRUE;
        colour_blend.srcColorBlendFactor = haze ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_SRC_ALPHA;
        colour_blend.dstColorBlendFactor = haze ? VK_BLEND_FACTOR_SRC_ALPHA : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colour_blend.colorBlendOp = VK_BLEND_OP_ADD;
        colour_blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colour_blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colour_blend.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    // the motion: the layer's own (x, y); the air's share of the pixel (z, the largest - the upscaler's reactive mask)
    VkPipelineColorBlendAttachmentState& motion_blend = core::at(blend, 1);
    if (layer) {
        motion_blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    } else if (!haze) {
        motion_blend.blendEnable = VK_TRUE;
        motion_blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        motion_blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        motion_blend.colorBlendOp = VK_BLEND_OP_MAX;
        motion_blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        motion_blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        motion_blend.alphaBlendOp = VK_BLEND_OP_MAX;
        motion_blend.colorWriteMask = VK_COLOR_COMPONENT_B_BIT;
    }
    VkPipelineColorBlendStateCreateInfo blending{};
    blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blending.attachmentCount = wanted.motion ? 2U : 1U;
    blending.pAttachments = blend.data();
    const std::array<VkDynamicState, 2> dynamic_states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
    dynamic.pDynamicStates = dynamic_states.data();
    const std::array<VkFormat, 2> formats{wanted.format, kMotionFormat};
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = wanted.motion ? 2U : 1U;
    rendering.pColorAttachmentFormats = formats.data();
    rendering.depthAttachmentFormat = haze ? VK_FORMAT_UNDEFINED : wanted.depth;
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
    if (vkCreateGraphicsPipelines(state.context.device(), VK_NULL_HANDLE, 1, &info, nullptr, &pipeline) != VK_SUCCESS) {
        *error = "vkCreateGraphicsPipelines failed";
        return VK_NULL_HANDLE;
    }
    DrawPipeline made = wanted;
    made.pipeline = pipeline;
    own.draws.push_back(made);
    return pipeline;
}

// The level's sky map onto the device (once a map).
bool upload_sky(Backend& state)
{
    SnowState& own = snow_state();
    const std::shared_ptr<const snow::SkyMap> map = snow::sky_map();
    if (map == own.sky_uploaded) {
        return own.sky.image != VK_NULL_HANDLE && map != nullptr;
    }
    own.sky_uploaded = map;
    if (map == nullptr || map->cells.empty()) {
        retire_image(state, &own.sky);
        return false;
    }
    constexpr VkImageUsageFlags kUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    own.sky.resting = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if (!ensure_trail_image(state, &own.sky, kSkyFormat, {.width = map->columns, .height = map->rows}, kUsage)) {
        own.sky_uploaded.reset();
        return false;
    }
    own.sky.resting = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    std::vector<uint8_t> bytes(map->cells.size() * sizeof(uint32_t));
    std::memcpy(bytes.data(), map->cells.data(), bytes.size());
    std::array<VkBufferImageCopy, 1> region{image_region(0, 0, 0, map->columns, map->rows)};
    if (!copy_into_image(state, &own.sky, bytes, region)) {
        if (first_report(state, kReportUpload)) {
            get_log("vulkan: snow: the sky map (%ux%u) couldn't be put on the device - no snow", map->columns, map->rows);
        }
        retire_image(state, &own.sky);
        return false;
    }
    get_log("vulkan: snow: the sky map on the device (%ux%u cells of %.1f units)", map->columns, map->rows,
            static_cast<double>(map->cell));
    return true;
}

// The trails' window placed round the camera (the surface trails' window: only the tiles that came in or changed copied),
// the colours seen of a slot given another tile cleared.
bool update_window(Backend& state, const std::array<float, 3>& camera)
{
    SnowState& own = snow_state();
    const snow::Settings settings = snow::settings();
    const int tiles = snow::window_tiles(settings.quality);
    const float texel = snow::trail_texel(settings.quality);
    const auto side = static_cast<uint32_t>(tiles * snow::kTileTexels);
    if (own.window.tiles != tiles || own.window.texel != texel || own.trails.image == VK_NULL_HANDLE) {
        own.trails.resting = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        if (!ensure_trail_image(state, &own.trails, kTrailsFormat, {.width = side, .height = side},
                                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT) ||
            !ensure_trail_image(state, &own.colours, kColoursFormat, {.width = side, .height = side},
                                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
            return false;
        }
        own.trails.resting = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        const TrailWindow before = own.window;
        own.window = TrailWindow{};
        own.window.tiles_copied = before.tiles_copied;
        own.window.slots_cleared = before.slots_cleared;
    }
    // the activity's window: its tiles cover kActiveScale of the trails' a side - as many as cover the trails' window
    // wherever the two are placed, and one more
    const int active_tiles = (tiles / snow::kActiveScale) + 2;
    const auto active_side = static_cast<uint32_t>(active_tiles * snow::kTileTexels);
    if (own.active_window.tiles != active_tiles || own.active.image == VK_NULL_HANDLE ||
        own.active_window.texel != texel * static_cast<float>(snow::kActiveScale)) {
        own.active.resting = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        if (!ensure_trail_image(state, &own.active, kTrailsFormat, {.width = active_side, .height = active_side},
                                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
            return false;
        }
        own.active.resting = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        own.active_window = TrailWindow{};
    }
    std::vector<std::array<int, 2>> moved;
    bool done = false;
    snow::read_trails([&](const snow::TrailField& field) {
        const surface_trail::Point centre{core::at(camera, 0), core::at(camera, 2)};
        const std::array<Image*, 1> images{&own.trails};
        const std::array<Image*, 1> active_images{&own.active};
        done = update_trail_window(state, &own.window, field, centre, tiles, images, &moved) &&
               update_trail_window(state, &own.active_window, field.activity(), centre, active_tiles, active_images, nullptr);
        // the layer's tiles: those of the trails' window the activity raised (pressed or not - the fall round a trail
        // reaches past its own tiles)
        own.drawn.clear();
        if (done) {
            for (int z = 0; z < tiles; ++z) {
                for (int x = 0; x < tiles; ++x) {
                    const int tx = core::at(own.window.first, 0) + x;
                    const int tz = core::at(own.window.first, 1) + z;
                    if (field.tile_active(tx, tz)) {
                        own.drawn.push_back({tx, tz});
                    }
                }
            }
        }
    });
    own.stats.tiles_copied = own.window.tiles_copied;
    own.stats.slots_cleared = own.window.slots_cleared;
    own.stats.window = own.window.first;
    own.stats.window_size = tiles;
    if (!done) {
        return false;
    }
    if (!moved.empty()) { // (another tile's colours seen: forgotten)
        std::vector<VkBufferImageCopy> colour_regions;
        for (const std::array<int, 2>& slot : moved) {
            colour_regions.push_back(image_region(0, core::at(slot, 0) * snow::kTileTexels, core::at(slot, 1) * snow::kTileTexels,
                                                  snow::kTileTexels, snow::kTileTexels));
        }
        const std::vector<uint8_t> zeros(snow::kTileTexelCount * kColourTexelBytes, 0);
        return copy_into_image(state, &own.colours, zeros, colour_regions);
    }
    return true;
}

// The cover's block for the view (once a view and frame).
std::optional<VkDescriptorBufferInfo> cover_block(Backend& state)
{
    SnowState& own = snow_state();
    const uint64_t frame = state.frames.serial();
    if (own.block_serial == own.view_serial && own.block_frame == frame && own.cover_block.buffer != VK_NULL_HANDLE) {
        return own.cover_block;
    }
    const std::shared_ptr<const snow::SkyMap>& map = own.sky_uploaded;
    if (map == nullptr) {
        return std::nullopt;
    }
    SnowUniforms uniforms{};
    uniforms.eye_to_world = trail_eye_to_world(own.view.camera.view);
    uniforms.map = {map->x0, map->z0, 1.0F / map->cell, map->cell};
    uniforms.range = {map->low, map->span, snow::kOpenSlack + (snow::kOpenCellShare * map->cell), 1.0F};
    uniforms.size = {static_cast<float>(map->columns), static_cast<float>(map->rows), snow::kLeastUp, snow::kCoatUp};
    uniforms.albedo = {kSnowAlbedo, kSnowAlbedo, kSnowAlbedo * 1.02F, kSnowEdgeNoise};
    uniforms.slope = {snow::kThinUp, snow::kFullUp, snow::kCoatShare, kSnowSkyReach};
    const void* const data = &uniforms;
    const std::optional<RingSpan> span =
        state.ring.copy(std::span(static_cast<const uint8_t*>(data), sizeof(uniforms)), state.uniform_alignment);
    if (!span) {
        return std::nullopt;
    }
    own.cover_block = {.buffer = span->buffer, .offset = span->offset, .range = sizeof(uniforms)};
    own.block_serial = own.view_serial;
    own.block_frame = frame;
    return own.cover_block;
}

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

std::array<uint32_t, 4> clip_rect(const std::array<uint32_t, 4>& rect, const Extent& extent)
{
    const uint32_t left = std::min(core::at(rect, 0), extent.width);
    const uint32_t top = std::min(core::at(rect, 1), extent.height);
    return {left, top, std::min(core::at(rect, 2), extent.width - left), std::min(core::at(rect, 3), extent.height - top)};
}

// What a view's passes work on.
struct ViewPlan {
    Image* colour = nullptr;
    Image* depth = nullptr;
    bool sampled_depth = false;
    bool motion = false;
    std::array<uint32_t, 4> rect{};
};

bool plan_view(Backend& state, const rt::ViewDrawn& view, ViewPlan* plan)
{
    Surface* const colour_surface = core::at(state.targets, 0);
    Image* const colour = image_of_surface(colour_surface);
    Image* const depth = image_of_surface(state.depth);
    if (colour == nullptr || colour->attachment_view == VK_NULL_HANDLE || (colour->aspect & VK_IMAGE_ASPECT_COLOR_BIT) == 0 ||
        (colour->usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0 || depth == nullptr ||
        (depth->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) == 0 || depth->attachment_view == VK_NULL_HANDLE ||
        depth->samples != colour->samples) {
        if (first_report(state, kReportTarget)) {
            get_log("vulkan: snow: a view drawn into targets that can't take it (no depth, or not readable) - none there");
        }
        return false;
    }
    plan->colour = colour;
    plan->depth = depth;
    plan->sampled_depth = (depth->usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0 && depth->view != VK_NULL_HANDLE;
    const Extent area{.width = std::min(colour->extent.width, depth->extent.width),
                      .height = std::min(colour->extent.height, depth->extent.height)};
    plan->rect = clip_rect(view.rect, area);
    plan->motion = motion_wanted(state, colour_surface, colour) && state.motion.image.image != VK_NULL_HANDLE;
    return core::at(plan->rect, 2) != 0 && core::at(plan->rect, 3) != 0;
}

Block block_for(const Backend& state, const ViewPlan& plan, const rt::ViewDrawn& view, bool lamps = false)
{
    const SnowState& own = snow_state();
    const snow::Settings settings = snow::settings();
    Block block{};
    block.vp = flat(multiply(view.camera.view, view.camera.projection));
    block.vp_before = block.vp;
    const MotionVectors& motion = state.motion;
    if (plan.motion && motion.before.known) {
        block.vp_before = motion_multiply(motion.before.view, motion.before.projection);
    }
    block.camera = {core::at(view.camera.position, 0), core::at(view.camera.position, 1), core::at(view.camera.position, 2),
                    static_cast<float>(std::fmod(snow::clock(), kClockWraps))};
    block.trails = {own.window.texel, static_cast<float>(own.window.tiles * snow::kTileTexels),
                    static_cast<float>(core::at(own.window.first, 0) * snow::kTileTexels),
                    static_cast<float>(core::at(own.window.first, 1) * snow::kTileTexels)};
    block.snow = {settings.depth, kSnowNearTiles * own.window.texel * static_cast<float>(snow::kTileTexels), own.active_window.texel,
                  static_cast<float>(own.active_window.tiles * snow::kTileTexels)};
    if (const std::shared_ptr<const snow::SkyMap>& map = own.sky_uploaded; map != nullptr) {
        block.sky_map = {map->x0, map->z0, map->cell, 0.0F};
        block.sky_range = {map->low, map->span, static_cast<float>(map->columns), static_cast<float>(map->rows)};
    }
    const SnowLight light = snow_light();
    block.light = {core::at(light.toward, 0), core::at(light.toward, 1), core::at(light.toward, 2), light.direct};
    const float ambient = (core::at(light.ambient, 0) + core::at(light.ambient, 1) + core::at(light.ambient, 2)) / 3.0F;
    block.light_colour = {core::at(light.colour, 0), core::at(light.colour, 1), core::at(light.colour, 2), ambient};
    block.rect = {static_cast<float>(core::at(plan.rect, 0)), static_cast<float>(core::at(plan.rect, 1)),
                  static_cast<float>(core::at(plan.rect, 2)), static_cast<float>(core::at(plan.rect, 3))};
    block.offsets = {core::at(plan.rect, 2) != 0 ? 1.0F / static_cast<float>(core::at(plan.rect, 2)) : 0.0F,
                     core::at(plan.rect, 3) != 0 ? 1.0F / static_cast<float>(core::at(plan.rect, 3)) : 0.0F,
                     plan.motion ? core::at(motion.jitter, 0) : 0.0F, plan.motion ? core::at(motion.jitter, 1) : 0.0F};
    constexpr std::size_t kRowZ = 2;
    constexpr std::size_t kRowW = 3;
    const auto& projection = view.camera.projection;
    block.depth_projection = {core::at(core::at(projection, kRowZ), kRowZ), core::at(core::at(projection, kRowW), kRowZ),
                              core::at(core::at(projection, kRowZ), kRowW), core::at(core::at(projection, kRowW), kRowW)};
    const float span = core::at(view.depth_range, 1) - core::at(view.depth_range, 0);
    block.depth_map = {core::at(view.depth_range, 0), span > 0.0F ? 1.0F / span : 1.0F, plan.sampled_depth ? 1.0F : 0.0F, 0.0F};
    // the camera's right and up in the world: the view matrix's columns (row vectors: its 3 x 3 part's transpose)
    const auto& v = view.camera.view;
    block.right = {core::at(core::at(v, 0), 0), core::at(core::at(v, 1), 0), core::at(core::at(v, 2), 0), 0.0F};
    block.up = {core::at(core::at(v, 0), 1), core::at(core::at(v, 1), 1), core::at(core::at(v, 2), 1), 0.0F};
    // snow's colour in the light now: the level's own light 1 (the time of day's relative to it)
    std::array<float, 3> flake{};
    for (std::size_t c = 0; c < 3; ++c) {
        core::at(flake, c) = kSnowAlbedo * (core::at(light.ambient, c) + core::at(light.colour, c));
    }
    const float flakes = settings.falling ? static_cast<float>(kSnowMostFlakes) : 0.0F;
    block.flake = {core::at(flake, 0), core::at(flake, 1), core::at(flake, 2), flakes};
    // the wind's drift (wrapped by each box: the flakes' places are modulo their box)
    const double drift = snow::wind_drift();
    block.wind = {core::at(wind::kDirection, 0), static_cast<float>(std::fmod(drift, static_cast<double>(kSnowNearBox))),
                  core::at(wind::kDirection, 1), static_cast<float>(std::fmod(drift, static_cast<double>(kSnowFlakeBox)))};
    block.sky_light = {core::at(light.ambient, 0), core::at(light.ambient, 1), core::at(light.ambient, 2), 0.0F};
    // the lens: the focal length in the view's pixels, its defocus focused on what the camera follows
    const float focal = core::at(plan.rect, 3) != 0 ? static_cast<float>(core::at(plan.rect, 3)) * kHalf *
                                                          std::abs(core::at(core::at(projection, 1), 1))
                                                    : 1.0F;
    const float focus = view.focus_known && view.focus_distance > 1.0F ? view.focus_distance : kSnowFocus;
    block.lens = {focal, kSnowAperture, focus, kSnowLeastPixels};
    // the haze while snow falls: the snow's light, half its direct light (the falling snow shades itself)
    if (settings.falling) {
        constexpr float kHazeDirect = 0.5F;
        block.haze = {kSnowAlbedo * (core::at(light.ambient, 0) + (kHazeDirect * core::at(light.colour, 0))),
                      kSnowAlbedo * (core::at(light.ambient, 1) + (kHazeDirect * core::at(light.colour, 1))),
                      kSnowAlbedo * (core::at(light.ambient, 2) + (kHazeDirect * core::at(light.colour, 2))), kSnowHazeExtinction};
    }
    block.haze_range = {kSnowHazeStart, kSnowHazeSky, 0.0F, 0.0F};
    const std::array<float, 3> origin = lamp_origin(view.camera.position);
    block.lamps_origin = {core::at(origin, 0), core::at(origin, 1), core::at(origin, 2), lamps ? kSnowLampCell : 0.0F};
    block.lamps_size = {static_cast<float>(core::at(kSnowLampCells, 0)), static_cast<float>(core::at(kSnowLampCells, 1)),
                        static_cast<float>(core::at(kSnowLampCells, 2)), 0.0F};
    return block;
}

std::optional<VkDescriptorBufferInfo> block_in_ring(Backend& state, const Block& block)
{
    const void* const bytes = &block;
    const std::optional<RingSpan> span =
        state.ring.copy(std::span(static_cast<const uint8_t*>(bytes), sizeof(block)), state.uniform_alignment);
    if (!span) {
        return std::nullopt;
    }
    return VkDescriptorBufferInfo{.buffer = span->buffer, .offset = span->offset, .range = sizeof(block)};
}

struct Bound {
    VkDescriptorBufferInfo block{};
    bool gather = false;           // the colours as storage, the source and the depth
    const Image* depth = nullptr;  // (the gather's: null - the source in its place; the haze's)
    bool layer = false;            // the source and the colours seen
    VkDescriptorBufferInfo lamps{}; // (the flakes': the lamps' grid)
    VkAccelerationStructureKHR scene = VK_NULL_HANDLE; // (the flakes' traced rays)
    bool haze = false;             // the depth alone
};

void push_descriptors(VkCommandBuffer commands, VkPipelineBindPoint point, const Bound& bound)
{
    const SnowState& own = snow_state();
    std::array<VkDescriptorImageInfo, kBindingCount> images{};
    std::array<VkWriteDescriptorSet, kBindingCount> writes{};
    uint32_t count = 0;
    const auto add = [&](uint32_t binding, VkDescriptorType type, const VkDescriptorImageInfo* image,
                         const VkDescriptorBufferInfo* buffer) {
        VkWriteDescriptorSet& write = core::at(writes, count++);
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstBinding = binding;
        write.descriptorCount = 1;
        write.descriptorType = type;
        write.pImageInfo = image;
        write.pBufferInfo = buffer;
    };
    add(kBindingBlock, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &bound.block);
    core::at(images, kBindingSky) = {.sampler = own.nearest, .imageView = own.sky.view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    add(kBindingSky, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &core::at(images, kBindingSky), nullptr);
    core::at(images, kBindingTrails) = {.sampler = own.repeat, .imageView = own.trails.view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    add(kBindingTrails, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &core::at(images, kBindingTrails), nullptr);
    core::at(images, kBindingActive) = {.sampler = own.repeat, .imageView = own.active.view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    add(kBindingActive, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &core::at(images, kBindingActive), nullptr);
    if (bound.gather || bound.layer) {
        core::at(images, kBindingSource) = {.sampler = own.nearest, .imageView = own.source.view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        add(kBindingSource, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &core::at(images, kBindingSource), nullptr);
    }
    if (bound.gather) {
        core::at(images, kBindingColours) = {.sampler = VK_NULL_HANDLE, .imageView = own.colours.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
        add(kBindingColours, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &core::at(images, kBindingColours), nullptr);
        core::at(images, kBindingDepth) =
            bound.depth != nullptr
                ? VkDescriptorImageInfo{.sampler = own.nearest, .imageView = bound.depth->view, .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL}
                : core::at(images, kBindingSource);
        add(kBindingDepth, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &core::at(images, kBindingDepth), nullptr);
    }
    if (bound.layer) {
        core::at(images, kBindingSeen) = {.sampler = own.repeat, .imageView = own.colours.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
        add(kBindingSeen, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &core::at(images, kBindingSeen), nullptr);
    }
    if (bound.haze && bound.depth != nullptr) {
        core::at(images, kBindingDepth) = {.sampler = own.nearest, .imageView = bound.depth->view,
                                           .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        add(kBindingDepth, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &core::at(images, kBindingDepth), nullptr);
    }
    if (bound.lamps.buffer != VK_NULL_HANDLE) {
        add(kBindingLamps, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &bound.lamps);
    }
    const VkWriteDescriptorSetAccelerationStructureKHR scene{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
                                                             .pNext = nullptr,
                                                             .accelerationStructureCount = 1,
                                                             .pAccelerationStructures = &bound.scene};
    if (bound.scene != VK_NULL_HANDLE && own.scene_binding) {
        add(kBindingScene, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, nullptr, nullptr);
        core::at(writes, count - 1).pNext = &scene;
    }
    vkCmdPushDescriptorSetKHR(commands, point, own.layout, 0, count, writes.data());
}

void push_tile(VkCommandBuffer commands, int x, int z, int step)
{
    const TileConstants constants{.tile = {x, z, step, 0}};
    vkCmdPushConstants(commands, snow_state().layout, kStages, 0, sizeof(constants), &constants);
}

void memory_barrier(VkCommandBuffer commands, VkPipelineStageFlags from, VkAccessFlags written, VkPipelineStageFlags to,
                    VkAccessFlags read)
{
    const VkMemoryBarrier barrier{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .pNext = nullptr, .srcAccessMask = written, .dstAccessMask = read};
    vkCmdPipelineBarrier(commands, from, to, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void depth_barrier(VkCommandBuffer commands, Image* depth, bool to_gather)
{
    const VkPipelineStageFlags tests = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    const VkImageLayout layout =
        to_gather ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    const VkImageMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = to_gather ? VkAccessFlags{VK_ACCESS_MEMORY_WRITE_BIT} : VkAccessFlags{0},
        .dstAccessMask = to_gather ? VkAccessFlags{VK_ACCESS_SHADER_READ_BIT}
                                   : VkAccessFlags{VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT},
        .oldLayout = depth->layout,
        .newLayout = layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = depth->image,
        .subresourceRange = {.aspectMask = depth->aspect, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1},
    };
    vkCmdPipelineBarrier(commands, to_gather ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         to_gather ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : tests, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    depth->layout = layout;
}

bool ensure_source(Backend& state, const Image& colour)
{
    SnowState& own = snow_state();
    Image& source = own.source;
    if (source.image != VK_NULL_HANDLE && source.format == colour.format && source.extent.width == colour.extent.width &&
        source.extent.height == colour.extent.height) {
        return true;
    }
    retire_image(state, &source);
    const ImageRequest request{.format = colour.format,
                               .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                               .extent = colour.extent,
                               .levels = 1,
                               .samples = VK_SAMPLE_COUNT_1_BIT,
                               .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                               .alpha_one = false};
    if (!state.memory.create_image(request, &source)) {
        return false;
    }
    prepare_image(state, &source);
    return true;
}

// The viewport's colour copied out (resolved when multisampled) into the source image.
void copy_colour(VkCommandBuffer commands, Image* colour, const std::array<uint32_t, 4>& rect)
{
    SnowState& own = snow_state();
    memory_barrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                   VK_PIPELINE_STAGE_TRANSFER_BIT, 0);
    Memory::transition(commands, colour, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    Memory::transition(commands, &own.source, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, true);
    const VkImageSubresourceLayers layers{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1};
    const VkOffset3D corner{.x = static_cast<int32_t>(core::at(rect, 0)), .y = static_cast<int32_t>(core::at(rect, 1)), .z = 0};
    const VkExtent3D size{.width = core::at(rect, 2), .height = core::at(rect, 3), .depth = 1};
    if (colour->samples == VK_SAMPLE_COUNT_1_BIT) {
        const VkImageCopy region{.srcSubresource = layers, .srcOffset = corner, .dstSubresource = layers, .dstOffset = corner, .extent = size};
        vkCmdCopyImage(commands, colour->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, own.source.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    } else {
        const VkImageResolve region{.srcSubresource = layers, .srcOffset = corner, .dstSubresource = layers, .dstOffset = corner, .extent = size};
        vkCmdResolveImage(commands, colour->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, own.source.image,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    }
    Memory::transition(commands, &own.source, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

// The rendering begun over the view's colour and depth (and the motion).
void begin_rendering(Backend& state, VkCommandBuffer commands, const ViewPlan& plan, const rt::ViewDrawn& view)
{
    Image* const colour = plan.colour;
    Image* const depth = plan.depth;
    Memory::transition(commands, colour, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    if (depth->layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        Memory::transition(commands, depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    }
    std::array<VkRenderingAttachmentInfo, 2> colours{};
    VkRenderingAttachmentInfo& colour_info = core::at(colours, 0);
    colour_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colour_info.imageView = colour->attachment_view;
    colour_info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colour_info.resolveMode = VK_RESOLVE_MODE_NONE;
    colour_info.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colour_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    if (plan.motion) {
        Image& image = state.motion.image;
        Memory::transition(commands, &image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        VkRenderingAttachmentInfo& motion_info = core::at(colours, 1);
        motion_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        motion_info.imageView = image.attachment_view;
        motion_info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        motion_info.resolveMode = VK_RESOLVE_MODE_NONE;
        motion_info.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        motion_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        image.used = state.frames.serial();
    }
    VkRenderingAttachmentInfo depth_info{};
    depth_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth_info.imageView = depth->attachment_view;
    depth_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth_info.resolveMode = VK_RESOLVE_MODE_NONE;
    depth_info.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depth_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    const VkRect2D area{.offset = {.x = static_cast<int32_t>(core::at(plan.rect, 0)), .y = static_cast<int32_t>(core::at(plan.rect, 1))},
                        .extent = {.width = core::at(plan.rect, 2), .height = core::at(plan.rect, 3)}};
    VkRenderingInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea = area;
    info.layerCount = 1;
    info.colorAttachmentCount = plan.motion ? 2U : 1U;
    info.pColorAttachments = colours.data();
    info.pDepthAttachment = &depth_info;
    vkCmdBeginRendering(commands, &info);
    const VkViewport viewport{.x = static_cast<float>(area.offset.x),
                              .y = static_cast<float>(area.offset.y),
                              .width = static_cast<float>(area.extent.width),
                              .height = static_cast<float>(area.extent.height),
                              .minDepth = std::clamp(core::at(view.depth_range, 0), 0.0F, 1.0F),
                              .maxDepth = std::clamp(core::at(view.depth_range, 1), 0.0F, 1.0F)};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &area);
    const uint64_t serial = state.frames.serial();
    colour->used = serial;
    depth->used = serial;
}

// The colour alone (the haze: the depth is read).
void begin_colour_rendering(Backend& state, VkCommandBuffer commands, const ViewPlan& plan, const rt::ViewDrawn& view)
{
    VkRenderingAttachmentInfo colour_info{};
    colour_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colour_info.imageView = plan.colour->attachment_view;
    colour_info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colour_info.resolveMode = VK_RESOLVE_MODE_NONE;
    colour_info.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colour_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    const VkRect2D area{.offset = {.x = static_cast<int32_t>(core::at(plan.rect, 0)), .y = static_cast<int32_t>(core::at(plan.rect, 1))},
                        .extent = {.width = core::at(plan.rect, 2), .height = core::at(plan.rect, 3)}};
    VkRenderingInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea = area;
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &colour_info;
    vkCmdBeginRendering(commands, &info);
    const VkViewport viewport{.x = static_cast<float>(area.offset.x),
                              .y = static_cast<float>(area.offset.y),
                              .width = static_cast<float>(area.extent.width),
                              .height = static_cast<float>(area.extent.height),
                              .minDepth = std::clamp(core::at(view.depth_range, 0), 0.0F, 1.0F),
                              .maxDepth = std::clamp(core::at(view.depth_range, 1), 0.0F, 1.0F)};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &area);
    plan.colour->used = state.frames.serial();
}

// The depth made readable by the haze's fragments (`to_read`), then an attachment again.
void haze_barrier(VkCommandBuffer commands, Image* depth, bool to_read)
{
    const VkPipelineStageFlags tests = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    const VkImageLayout layout = to_read ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    const VkImageMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = to_read ? VkAccessFlags{VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT} : VkAccessFlags{VK_ACCESS_SHADER_READ_BIT},
        .dstAccessMask = to_read ? VkAccessFlags{VK_ACCESS_SHADER_READ_BIT}
                                 : VkAccessFlags{VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT},
        .oldLayout = depth->layout,
        .newLayout = layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = depth->image,
        .subresourceRange = {.aspectMask = depth->aspect, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1},
    };
    vkCmdPipelineBarrier(commands, to_read ? tests : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         to_read ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : tests, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    depth->layout = layout;
}

// The tiles of the window that hold trails, nearest first, with their vertices' step.
struct TileDraw {
    int x = 0; // the tile's first texel
    int z = 0;
    int step = 1;
};

std::vector<TileDraw> tiles_to_draw(const std::array<float, 3>& camera)
{
    const SnowState& own = snow_state();
    std::vector<TileDraw> found;
    const float tile_units = own.window.texel * static_cast<float>(snow::kTileTexels);
    for (const std::array<int, 2>& tile : own.drawn) {
        const float middle_x = (static_cast<float>(core::at(tile, 0)) + kHalf) * tile_units;
        const float middle_z = (static_cast<float>(core::at(tile, 1)) + kHalf) * tile_units;
        const float dx = middle_x - core::at(camera, 0);
        const float dz = middle_z - core::at(camera, 2);
        const bool close = std::sqrt((dx * dx) + (dz * dz)) < kSnowNearTiles * tile_units;
        found.push_back({.x = core::at(tile, 0) * snow::kTileTexels, .z = core::at(tile, 1) * snow::kTileTexels,
                         .step = close ? 1 : static_cast<int>(kFarStep)});
    }
    return found;
}

// --- the passes' time on the device (the log's "snow: the passes take") ----------------------------------------------

void read_timer_set(Backend& state, uint32_t set)
{
    SnowState& own = snow_state();
    struct Stamp {
        uint64_t value = 0;
        uint64_t available = 0;
    };
    std::array<Stamp, kTimerPoints> stamps{};
    const VkResult got = vkGetQueryPoolResults(state.context.device(), own.timer, set * kTimerPoints, kTimerPoints, sizeof(stamps),
                                               stamps.data(), sizeof(Stamp), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if (got != VK_SUCCESS && got != VK_NOT_READY) {
        return;
    }
    const uint8_t written = core::at(own.timer_written, set);
    const auto span = [&](uint32_t from, uint32_t to, std::size_t part) {
        const Stamp& a = core::at(stamps, from);
        const Stamp& b = core::at(stamps, to);
        if ((written & (1U << from)) == 0U || (written & (1U << to)) == 0U || a.available == 0 || b.available == 0 || b.value < a.value) {
            return;
        }
        core::at(own.window_milliseconds, part) += static_cast<double>(b.value - a.value) * own.tick_milliseconds;
        ++core::at(own.window_samples, part);
    };
    span(kLayerStart, kLayerEnd, 0);
    span(kAirStart, kAirFlakes, 1);
    span(kAirFlakes, kAirHaze, 2);
    if (++own.window_frames >= kTimingLogFrames) {
        const auto average = [&own](std::size_t part) {
            const uint64_t samples = core::at(own.window_samples, part);
            return samples != 0 ? core::at(own.window_milliseconds, part) / static_cast<double>(samples) : 0.0;
        };
        get_log("vulkan: snow: the passes take %.3f ms on the device - the trails' layer %.3f, the flakes and spray %.3f, the "
                "haze %.3f (the last %llu frames' average)",
                average(0) + average(1) + average(2), average(0), average(1), average(2),
                static_cast<unsigned long long>(own.window_frames));
        own.window_frames = 0;
        own.window_milliseconds = {};
        own.window_samples = {};
    }
}

// The frame's set (its earlier results read first, reset once a frame); false when the device can't time or the set's
// last results aren't in yet.
bool timer_set(Backend& state, VkCommandBuffer commands, uint32_t* set)
{
    SnowState& own = snow_state();
    if (own.tick_milliseconds < 0.0) {
        return false;
    }
    if (own.timer == VK_NULL_HANDLE) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(state.context.physical_device(), &properties);
        constexpr double kNanosecondsPerMillisecond = 1.0e6;
        const VkQueryPoolCreateInfo info{.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                                         .pNext = nullptr,
                                         .flags = 0,
                                         .queryType = VK_QUERY_TYPE_TIMESTAMP,
                                         .queryCount = kTimerSets * kTimerPoints,
                                         .pipelineStatistics = 0};
        if (!(properties.limits.timestampPeriod > 0.0F) || vkCreateQueryPool(state.context.device(), &info, nullptr, &own.timer) != VK_SUCCESS) {
            own.timer = VK_NULL_HANDLE;
            own.tick_milliseconds = -1.0;
            return false;
        }
        own.tick_milliseconds = static_cast<double>(properties.limits.timestampPeriod) / kNanosecondsPerMillisecond;
    }
    const uint64_t frame = state.frames.serial();
    const auto current = static_cast<uint32_t>(own.timed % kTimerSets);
    if (core::at(own.timer_frames, current) == frame) {
        *set = current;
        return true;
    }
    // (a new frame: the next set, once its last results are in)
    const auto next = static_cast<uint32_t>((own.timed + 1) % kTimerSets);
    uint64_t& written = core::at(own.timer_frames, next);
    if (written != 0 && state.frames.completed() < written) {
        return false;
    }
    if (written != 0) {
        read_timer_set(state, next);
    }
    ++own.timed;
    written = frame;
    core::at(own.timer_written, next) = 0;
    vkCmdResetQueryPool(commands, own.timer, next * kTimerPoints, kTimerPoints);
    *set = next;
    return true;
}

void stamp_time(Backend& state, VkCommandBuffer commands, uint32_t point, VkPipelineStageFlagBits stage)
{
    SnowState& own = snow_state();
    uint32_t set = 0;
    if (!timer_set(state, commands, &set) || (core::at(own.timer_written, set) & (1U << point)) != 0U) {
        return; // (a second view this frame: the first's times kept)
    }
    vkCmdWriteTimestamp(commands, stage, own.timer, (set * kTimerPoints) + point);
    core::at(own.timer_written, set) = static_cast<uint8_t>(core::at(own.timer_written, set) | (1U << point));
}

bool ready_for(Backend& state, const ViewPlan& plan, DrawKind kind, VkPipeline* pipeline, bool traced = false)
{
    SnowState& own = snow_state();
    std::string error;
    *pipeline = ensure_objects(state, &error)
                    ? draw_pipeline(state, {.kind = kind, .format = plan.colour->format, .depth = plan.depth->format,
                                            .samples = plan.colour->samples, .motion = plan.motion, .traced = traced},
                                    &error)
                    : VK_NULL_HANDLE;
    const bool made = *pipeline != VK_NULL_HANDLE && (kind != DrawKind::kLayer || ensure_source(state, *plan.colour));
    if (!made) {
        own.failed = true;
        if (first_report(state, kReportPipeline)) {
            get_log("vulkan: snow: the passes couldn't be made (%s) - no trails or falling snow",
                    error.empty() ? "its images" : error.c_str());
        }
    }
    return made;
}

} // namespace

// --- settings ----------------------------------------------------------------------------------------------------

void set_snow_settings(const SnowSettings& settings)
{
    const snow::Settings engine{.on = settings.on, .depth = settings.depth, .quality = settings.quality, .falling = settings.falling};
    snow::set_settings(engine); // (cleaned there)
    const snow::Settings clean = snow::settings();
    {
        const std::scoped_lock lock(backend().mutex);
        snow_state().settings = {.on = clean.on, .depth = clean.depth, .quality = clean.quality, .falling = clean.falling};
    }
    if (clean.on) {
        rt::add_view_listener(&listener());
    } else {
        rt::remove_view_listener(&listener());
    }
}

SnowSettings snow_settings()
{
    const std::scoped_lock lock(backend().mutex);
    return snow_state().settings;
}

SnowStats snow_stats() { return snow_state().stats; }

void begin_snow_view(Backend& state, const rt::ViewDrawn& view)
{
    SnowState& own = snow_state();
    own.view = view;
    own.view_known = true;
    ++own.view_serial;
    if (!own.settings.on) {
        return;
    }
    if (!upload_sky(state)) {
        return;
    }
    const uint64_t frame = state.frames.serial();
    if (own.window_frame != frame) { // (the frame's first view places the window)
        own.window_frame = frame;
        if (!update_window(state, view.camera.position) && first_report(state, kReportUpload)) {
            get_log("vulkan: snow: the trails couldn't be put on the device - none drawn");
        }
    }
}

SnowGround snow_ground(Backend& state)
{
    SnowState& own = snow_state();
    const std::shared_ptr<const snow::SkyMap>& map = own.sky_uploaded;
    if (!own.settings.on || own.sky.image == VK_NULL_HANDLE || map == nullptr || own.nearest == VK_NULL_HANDLE) {
        return {};
    }
    own.sky.used = state.frames.serial();
    return {.on = true,
            .sky = {.sampler = own.nearest, .imageView = own.sky.view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            .map = {map->x0, map->z0, map->cell, snow::kOpenSlack + (snow::kOpenCellShare * map->cell)},
            .range = {map->low, map->span, static_cast<float>(map->columns), static_cast<float>(map->rows)}};
}

void snow_key(Backend& state, const GpuStateBlock& block, const DrawCall& call, ShaderKey* shaders, SnowDescriptors* out)
{
    *out = {};
    SnowState& own = snow_state();
    if (!own.settings.on || !own.view_known || own.sky.image == VK_NULL_HANDLE || own.sky_uploaded == nullptr ||
        !surface_trail::current_ground() || fvf_pre_transformed(block.vertex_format) || shaders->fragment.water != 0U) {
        return;
    }
    // traced: the frame's scene has something to trace (a device with ray tracing - its pipelines take ray queries)
    const SceneView scene = state.context.ray_tracing_enabled() ? current_scene(state) : SceneView{};
    const bool traced = scene.tlas != VK_NULL_HANDLE && scene.instances > 0;
    // the tint: the relit world's stream for this draw (its vertex colour the tint x the light)
    Buffer* const stream = relit_stream_of(state, call);
    ShaderKey with_snow = *shaders;
    with_snow.fragment.trails = static_cast<uint8_t>(with_snow.fragment.trails | kTrailSnow | (traced ? kTrailSnowTraced : 0U) |
                                                     (stream != nullptr ? kTrailSnowTint : 0U));
    with_snow = normalised(with_snow);
    if ((with_snow.fragment.trails & kTrailSnow) == 0U) {
        if (first_report(state, kReportUncovered ^ hash_of(shaders->fragment))) { // (each kind of ground draw left once)
            get_log("vulkan: snow: a ground draw its key can't cover (%s)", describe(shaders->fragment).c_str());
        }
        return;
    }
    out->scene = (with_snow.fragment.trails & kTrailSnowTraced) != 0U ? scene.tlas : VK_NULL_HANDLE;
    out->stream = (with_snow.fragment.trails & kTrailSnowTint) != 0U ? stream : nullptr;
    own.stats.traced_draws += out->scene != VK_NULL_HANDLE ? 1U : 0U;
    const std::optional<VkDescriptorBufferInfo> uniforms = cover_block(state);
    if (!uniforms) {
        return;
    }
    if (own.nearest == VK_NULL_HANDLE) {
        std::string error;
        if (!ensure_objects(state, &error)) {
            return;
        }
    }
    *shaders = with_snow;
    out->on = true;
    out->block = *uniforms;
    out->sky = {.sampler = own.nearest, .imageView = own.sky.view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    own.sky.used = state.frames.serial();
    ++own.stats.covered_draws;
    if (first_report(state, kReportFirstCover)) {
        get_log("vulkan: snow: the first draw covered (%s)", describe(with_snow.fragment).c_str());
    }
}

bool record_snow_layer(Backend& state, const rt::ViewDrawn& view)
{
    SnowState& own = snow_state();
    if (!own.settings.on || own.failed || own.sky.image == VK_NULL_HANDLE || own.trails.image == VK_NULL_HANDLE ||
        own.active.image == VK_NULL_HANDLE) {
        return false;
    }
    const std::vector<TileDraw> tiles = tiles_to_draw(view.camera.position);
    if (tiles.empty()) {
        own.stats.last_tiles = 0;
        return false;
    }
    ViewPlan plan;
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (!plan_view(state, view, &plan) || !ready_for(state, plan, DrawKind::kLayer, &pipeline)) {
        return false;
    }
    const std::optional<VkDescriptorBufferInfo> block = block_in_ring(state, block_for(state, plan, view));
    if (!block) {
        if (first_report(state, kReportNoRoom)) {
            get_log("vulkan: snow: no room in the ring for the pass's block - skipped");
        }
        return false;
    }
    VkCommandBuffer commands = outside_pass(state);
    stamp_time(state, commands, kLayerStart, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    copy_colour(commands, plan.colour, plan.rect);
    // the gather: the ground's colours seen this frame (the last layer read them)
    memory_barrier(commands, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                   VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    Memory::transition(commands, &own.colours, VK_IMAGE_LAYOUT_GENERAL);
    if (plan.sampled_depth) {
        depth_barrier(commands, plan.depth, true);
    }
    const bool multisampled_depth = plan.sampled_depth && plan.depth->samples != VK_SAMPLE_COUNT_1_BIT;
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, core::at(own.gather, multisampled_depth ? 1 : 0));
    push_descriptors(commands, VK_PIPELINE_BIND_POINT_COMPUTE,
                     {.block = *block, .gather = true, .depth = plan.sampled_depth ? plan.depth : nullptr});
    constexpr uint32_t kGroups = static_cast<uint32_t>(snow::kTileTexels) / kGatherGroup;
    for (const TileDraw& tile : tiles) {
        push_tile(commands, tile.x, tile.z, 1);
        vkCmdDispatch(commands, kGroups, kGroups, 1);
    }
    memory_barrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    if (plan.sampled_depth) {
        depth_barrier(commands, plan.depth, false);
    }
    begin_rendering(state, commands, plan, view);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    push_descriptors(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, {.block = *block, .layer = true});
    for (const TileDraw& tile : tiles) {
        push_tile(commands, tile.x, tile.z, tile.step);
        const auto across = static_cast<uint32_t>(snow::kTileTexels / tile.step);
        vkCmdDraw(commands, across * across * kQuadVertices, 1, 0, 0);
    }
    vkCmdEndRendering(commands);
    stamp_time(state, commands, kLayerEnd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    state.bound = BoundState{};
    const uint64_t serial = state.frames.serial();
    own.source.used = serial;
    own.colours.used = serial;
    own.trails.used = serial;
    own.active.used = serial;
    own.sky.used = serial;
    own.stats.last_tiles = static_cast<uint32_t>(tiles.size());
    ++own.stats.layer_passes;
    if (first_report(state, kReportFirstLayer)) {
        get_log("vulkan: snow: the first trails drawn (%ux%u, %u sample%s%s; %zu tiles, a texel %.1f units)", core::at(plan.rect, 2),
                core::at(plan.rect, 3), static_cast<unsigned>(plan.colour->samples),
                plan.colour->samples == VK_SAMPLE_COUNT_1_BIT ? "" : "s", plan.motion ? ", motion" : "", tiles.size(),
                static_cast<double>(own.window.texel));
    }
    return true;
}

bool record_snow_air(Backend& state, const rt::ViewDrawn& view)
{
    SnowState& own = snow_state();
    if (!own.settings.on || own.failed || own.sky.image == VK_NULL_HANDLE || own.trails.image == VK_NULL_HANDLE ||
        own.active.image == VK_NULL_HANDLE) {
        return false;
    }
    std::vector<snow::SprayParticle> spray = snow::live_spray();
    const auto now = static_cast<float>(snow::clock());
    for (snow::SprayParticle& particle : spray) {
        particle.born = now - particle.born; // (handed on as its age)
    }
    const uint32_t flakes = own.settings.falling ? kSnowMostFlakes : 0U;
    if (spray.empty() && flakes == 0) {
        return false;
    }
    ViewPlan plan;
    if (!plan_view(state, view, &plan)) {
        return false;
    }
    // the flakes traced: a device with ray tracing and a scene with something in it
    const SceneView scene = own.scene_binding ? current_scene(state) : SceneView{};
    const bool traced = scene.tlas != VK_NULL_HANDLE && scene.instances > 0;
    const bool haze = flakes > 0 && plan.sampled_depth;
    VkPipeline spray_pipeline = VK_NULL_HANDLE;
    VkPipeline flake_pipeline = VK_NULL_HANDLE;
    VkPipeline haze_pipeline = VK_NULL_HANDLE;
    std::string error;
    if (!ready_for(state, plan, DrawKind::kSpray, &spray_pipeline) ||
        (flakes > 0 && !ready_for(state, plan, DrawKind::kFlakes, &flake_pipeline, traced))) {
        return false;
    }
    if (haze) {
        haze_pipeline = draw_pipeline(state, {.kind = DrawKind::kHaze, .format = plan.colour->format, .depth = plan.depth->format,
                                              .samples = plan.colour->samples, .motion = false,
                                              .multisampled = plan.depth->samples != VK_SAMPLE_COUNT_1_BIT},
                                      &error);
        if (haze_pipeline == VK_NULL_HANDLE && first_report(state, kReportHaze)) {
            get_log("vulkan: snow: the haze's pass couldn't be made (%s) - none drawn", error.c_str());
        }
    }
    bool lamps_on = false;
    const std::vector<std::array<float, 4>> lamps = flakes > 0 ? lamp_cells(lamp_origin(view.camera.position), &lamps_on)
                                                               : std::vector<std::array<float, 4>>(kLampVectors);
    const std::optional<VkDescriptorBufferInfo> block = block_in_ring(state, block_for(state, plan, view, lamps_on));
    const void* const lamp_bytes = lamps.data();
    const std::optional<RingSpan> lamp_span = state.ring.copy(
        std::span(static_cast<const uint8_t*>(lamp_bytes), lamps.size() * sizeof(std::array<float, 4>)), state.uniform_alignment);
    std::optional<RingSpan> instances;
    if (!spray.empty()) {
        const void* const bytes = spray.data();
        instances = state.ring.copy(std::span(static_cast<const uint8_t*>(bytes), spray.size() * sizeof(snow::SprayParticle)),
                                    kStagingAlignment);
    }
    if (!block || !lamp_span || (!spray.empty() && !instances)) {
        if (first_report(state, kReportNoRoom)) {
            get_log("vulkan: snow: no room in the ring for the spray - skipped");
        }
        return false;
    }
    const VkDescriptorBufferInfo lamp_info{.buffer = lamp_span->buffer, .offset = lamp_span->offset,
                                           .range = lamps.size() * sizeof(std::array<float, 4>)};
    VkCommandBuffer commands = outside_pass(state);
    stamp_time(state, commands, kAirStart, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    begin_rendering(state, commands, plan, view);
    if (flakes > 0) {
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, flake_pipeline);
        push_descriptors(commands, VK_PIPELINE_BIND_POINT_GRAPHICS,
                         {.block = *block, .lamps = lamp_info, .scene = traced ? scene.tlas : VK_NULL_HANDLE});
        vkCmdDraw(commands, kStripVertices, flakes, 0, 0);
    }
    if (instances) {
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, spray_pipeline);
        push_descriptors(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, {.block = *block});
        const VkDeviceSize offset = instances->offset;
        vkCmdBindVertexBuffers(commands, 0, 1, &instances->buffer, &offset);
        vkCmdDraw(commands, kStripVertices, static_cast<uint32_t>(spray.size()), 0, 0);
    }
    vkCmdEndRendering(commands);
    stamp_time(state, commands, kAirFlakes, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    // the haze: the snow too far off to draw, over the world by its depth (read, not attached)
    if (haze && haze_pipeline != VK_NULL_HANDLE) {
        haze_barrier(commands, plan.depth, true);
        Memory::transition(commands, plan.colour, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        begin_colour_rendering(state, commands, plan, view);
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, haze_pipeline);
        push_descriptors(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, {.block = *block, .depth = plan.depth, .haze = true});
        vkCmdDraw(commands, 3, 1, 0, 0);
        vkCmdEndRendering(commands);
        haze_barrier(commands, plan.depth, false);
        ++own.stats.haze_passes;
        stamp_time(state, commands, kAirHaze, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    }
    state.bound = BoundState{};
    own.sky.used = state.frames.serial();
    own.stats.last_spray = static_cast<uint32_t>(spray.size());
    own.stats.last_flakes = flakes;
    own.stats.last_traced_air = traced && flakes > 0;
    own.stats.last_lamp_cells = lamps_on ? static_cast<uint32_t>(lamps.size() / kLampVectors) : 0U;
    ++own.stats.air_passes;
    if (first_report(state, kReportFirstAir)) {
        get_log("vulkan: snow: the first snow in the air (%u flakes%s%s, %zu spray particles%s)", flakes, traced ? ", traced" : "",
                lamps_on ? ", lamps" : "", spray.size(), haze ? ", the haze" : "");
    }
    return true;
}

bool read_snow_window(Backend& state, std::vector<uint32_t>* texels, uint32_t* side)
{
    SnowState& own = snow_state();
    texels->clear();
    *side = 0;
    if (own.trails.image == VK_NULL_HANDLE) {
        return false;
    }
    std::vector<uint8_t> raw(static_cast<std::size_t>(own.trails.extent.width) * own.trails.extent.height * kTrailTexelBytes);
    if (!finish_work(state) || !state.memory.read_image_level(&own.trails, 0, &raw)) {
        return false;
    }
    texels->resize(raw.size() / kTrailTexelBytes);
    std::memcpy(texels->data(), raw.data(), raw.size());
    *side = own.trails.extent.width;
    return true;
}

void release_snow(Backend& state)
{
    SnowState& own = snow_state();
    const VkDevice device = state.context.device();
    if (device != nullptr) {
        for (const DrawPipeline& made : own.draws) {
            vkDestroyPipeline(device, made.pipeline, nullptr);
        }
        for (VkPipeline pipeline : own.gather) {
            if (pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, pipeline, nullptr);
            }
        }
        for (VkShaderModule module :
             {core::at(own.layer_vertex, 0), core::at(own.layer_vertex, 1), core::at(own.layer_fragment, 0), core::at(own.layer_fragment, 1),
              core::at(own.air_fragment, 0), core::at(own.air_fragment, 1), core::at(own.air_fragment, 2), core::at(own.air_fragment, 3),
              own.spray_vertex, core::at(own.flake_vertex, 0), core::at(own.flake_vertex, 1), own.haze_vertex,
              core::at(own.haze_fragment, 0), core::at(own.haze_fragment, 1)}) {
            if (module != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, module, nullptr);
            }
        }
        if (own.layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, own.layout, nullptr);
        }
        if (own.set_layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, own.set_layout, nullptr);
        }
        if (own.timer != VK_NULL_HANDLE) {
            vkDestroyQueryPool(device, own.timer, nullptr);
        }
        for (VkSampler sampler : {own.nearest, own.repeat}) {
            if (sampler != VK_NULL_HANDLE) {
                vkDestroySampler(device, sampler, nullptr);
            }
        }
    }
    if (state.memory.ready()) {
        for (Image* image : {&own.source, &own.sky, &own.trails, &own.colours, &own.active}) {
            state.memory.destroy_image(image);
        }
    }
    const SnowSettings settings = own.settings;
    own = SnowState{};
    own.settings = settings;
}

} // namespace render::vulkan
