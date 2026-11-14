// NOT ORIGINAL - the camera effects: motion blur, depth of field and the colour grade (vulkan_camera_fx.h; docs/design/
// vulkan.md "Camera effects"): the settings, the listeners, the passes' objects and their recording.
#include "gpu_vulkan/vulkan_camera_fx.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "engine/gfx/rt_scene.h"
#include "engine/sys/frame_blend.h"
#include "gpu_vulkan/glsl_compiler.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_fisheye.h"
#include "gpu_vulkan/vulkan_formats.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_motion.h"
#include "gpu_vulkan/vulkan_objects.h"
#include "gpu_vulkan/vulkan_rt_glsl.h"
#include "gpu_vulkan/vulkan_style.h"
#include "gpu_vulkan/vulkan_upscale.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

constexpr uint32_t kGroupSide = 8;
constexpr VkFormat kWorkFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkShaderStageFlags kStages = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
constexpr float kHalf = 0.5F;
constexpr uint64_t kSeedMask = 0xFFFFU;
constexpr uint32_t kFlagMotionDrawn = 1U; // the view's motion attachment was written (its clear at least)
constexpr uint32_t kFlagBlur = 2U;        // the preparation works out the motion
constexpr uint64_t kReportFailed = 0xCA3E'0001ULL;
constexpr uint64_t kReportMultisampled = 0xCA3E'0002ULL;
constexpr uint64_t kReportTarget = 0xCA3E'0003ULL;
constexpr uint64_t kReportFirstBlur = 0xCA3E'0004ULL;
constexpr uint64_t kReportFirstFocus = 0xCA3E'0005ULL;
constexpr uint64_t kReportFirstGrade = 0xCA3E'0006ULL;
constexpr uint64_t kReportNoDepth = 0xCA3E'0007ULL;
constexpr uint64_t kReportConstants = 0xCA3E'0008ULL;
constexpr uint64_t kTimingLogEvery = 600;

// The bindings (one layout for every pass; each shader declares the ones it reads).
constexpr uint32_t kBindingSource = 0; // the viewport's colour copied out (linear sampler, SHADER_READ_ONLY_OPTIMAL)
constexpr uint32_t kBindingDepth = 1;  // the depth surface (nearest, DEPTH_STENCIL_READ_ONLY_OPTIMAL)
constexpr uint32_t kBindingMotion = 2; // the motion attachment (nearest, GENERAL)
constexpr uint32_t kBindingA = 3;      // work images (linear, GENERAL)
constexpr uint32_t kBindingB = 4;
constexpr uint32_t kBindingC = 5;
constexpr uint32_t kBindingOut = 6;    // a compute pass's outputs (storage, GENERAL)
constexpr uint32_t kBindingOut2 = 7;
constexpr uint32_t kBindingTable = 8;  // the grade's table (a storage buffer of vec4)
constexpr uint32_t kBindingCount = 9;

constexpr const char* kVertexSource = R"(#version 460
void main()
{
    vec2 at = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(at * 2.0 - 1.0, 0.0, 1.0);
}
)";

// What every pass shares (CameraConstants below), and the helpers - the hashes are vulkan_style_kit's (the grade's twin
// uses film_kit's).
constexpr const char* kCommon = R"(#version 460
layout(push_constant) uniform Camera {
    ivec4 rect;    // the output viewport in the target: x, y, width, height
    ivec4 render;  // the drawn rectangle in the depth and motion images
    ivec4 info;    // x: K, the blur's tile side; y: flags (1 the motion attachment written, 2 the blur); z: the frame's
                   // seed; w: the grade table's size
    ivec4 sizes;   // x, y: the half-resolution size; z, w: the blur's tiles across, down
    mat4 to_before; // this frame's clip space (jittered) -> the frame before's (Direct3D's row vectors)
    vec4 lens;     // P22, P32, P23, P33
    vec4 view;     // x: the depth range's minimum; y: 1 / its span; z, w: the jitter (pixels of the drawn rectangle)
    vec4 blur;     // x: the shutter / 2; y: K; z: the soft depth's share; w: unused
    vec4 dof;      // x: the focus distance; y: A (output pixels); z, w: unused
    vec4 grade;    // x: strength; y: vignette; z: grain; w: the grain's cell (pixels)
} pc;

const float kFarthest = KFARTHEST;

float finite(float c) { return c >= 0.0 ? min(c, 1.0) : 0.0; } // (not a number fails the comparison: 0)
vec3 finite3(vec3 c) { return vec3(finite(c.r), finite(c.g), finite(c.b)); }

uint hash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
float hash01(uint x) { return float(hash(x) >> 8) * (1.0 / 16777216.0); }
uint hash2(int column, int row, uint seed) { return hash(uint(column) * 0x8da6b343u ^ hash(uint(row) * 0xd8163841u + seed)); }

// A depth value's distance along the view (the far plane's ceiling where nothing was drawn).
float linear_depth(float d)
{
    if (!(d < 1.0)) {
        return kFarthest;
    }
    float ndc = (d - pc.view.x) * pc.view.y;
    float z = (pc.lens.y - ndc * pc.lens.w) / (ndc * pc.lens.z - pc.lens.x);
    float w = z * pc.lens.z + pc.lens.w;
    float along = abs(pc.lens.z) > 0.5 ? w : abs(z);
    return along > 0.0 ? min(along, kFarthest) : kFarthest;
}

// The circle of confusion of a distance, in output pixels (behind the focus positive, before it negative).
float circle_of(float z)
{
    return clamp(pc.dof.y * (1.0 - pc.dof.x / max(z, 1e-3)), -pc.dof.y, pc.dof.y);
}
)";

// 1. the preparation: v (the half streak, output pixels) and z per output pixel
constexpr const char* kPrepareBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 1) uniform sampler2D u_depth;
#ifdef WITH_MOTION
layout(set = 0, binding = 2) uniform sampler2D u_motion;
#endif
layout(set = 0, binding = 6, rgba16f) uniform writeonly image2D o_out;
const float kNoMotionHalf = KNO_MOTION_HALF;

void main()
{
    ivec2 local = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(local, pc.rect.zw))) {
        return;
    }
    vec2 scale = vec2(pc.render.zw) / vec2(pc.rect.zw);
    ivec2 drawn = min(ivec2((vec2(local) + 0.5) * scale), pc.render.zw - 1);
    float d = texelFetch(u_depth, pc.render.xy + drawn, 0).r;
    float z = linear_depth(d);
    vec2 v = vec2(0.0);
#ifdef WITH_MOTION
    {
        vec2 m = vec2(kNoMotionHalf * 2.0);
        if ((pc.info.y & 1) != 0) {
            m = texelFetch(u_motion, pc.render.xy + drawn, 0).xy;
        }
        if (!(m.x < kNoMotionHalf)) {
            // no draw wrote it: the camera's motion of the point at its depth
            vec2 size = vec2(pc.render.zw);
            vec2 centre = vec2(drawn); // (Direct3D 9's pixel centres: the draws are moved half a pixel to them)
            vec2 ndc = vec2(centre.x / size.x * 2.0 - 1.0, 1.0 - centre.y / size.y * 2.0);
            vec4 before = pc.to_before * vec4(ndc, d, 1.0);
            vec2 now_uv = (centre - pc.view.zw) / size;
            vec2 before_uv = before.w > 1e-6 ? vec2(before.x / before.w * 0.5 + 0.5, 0.5 - before.y / before.w * 0.5) : now_uv;
            m = before_uv - now_uv;
        }
        v = m * vec2(pc.rect.zw) * pc.blur.x;
        float len = length(v);
        if (!(len < 1.0e6)) {
            v = vec2(0.0);
        } else if (len > pc.blur.y) {
            v *= pc.blur.y / len;
        }
    }
#endif
    imageStore(o_out, local, vec4(v, z, 0.0));
}
)";

// 2a. the tiles: the longest v of each K x K tile
constexpr const char* kTilesBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 3) uniform sampler2D u_a;
layout(set = 0, binding = 6, rgba16f) uniform writeonly image2D o_out;

void main()
{
    ivec2 tile = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(tile, pc.sizes.zw))) {
        return;
    }
    ivec2 first = tile * pc.info.x;
    ivec2 last = min(first + pc.info.x, pc.rect.zw);
    vec2 longest = vec2(0.0);
    float most = 0.0;
    for (int y = first.y; y < last.y; ++y) {
        for (int x = first.x; x < last.x; ++x) {
            vec2 v = texelFetch(u_a, ivec2(x, y), 0).xy;
            float l = dot(v, v);
            if (l > most) {
                most = l;
                longest = v;
            }
        }
    }
    imageStore(o_out, tile, vec4(longest, 0.0, 0.0));
}
)";

// 2b. the neighbourhood: the longest of the 3 x 3 tiles round each
constexpr const char* kNeighboursBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 3) uniform sampler2D u_a;
layout(set = 0, binding = 6, rgba16f) uniform writeonly image2D o_out;

void main()
{
    ivec2 tile = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(tile, pc.sizes.zw))) {
        return;
    }
    vec2 longest = vec2(0.0);
    float most = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 v = texelFetch(u_a, clamp(tile + ivec2(x, y), ivec2(0), pc.sizes.zw - 1), 0).xy;
            float l = dot(v, v);
            if (l > most) {
                most = l;
                longest = v;
            }
        }
    }
    imageStore(o_out, tile, vec4(longest, 0.0, 0.0));
}
)";

// 2c. the reconstruction (full screen)
constexpr const char* kBlurBody = R"(
layout(set = 0, binding = 0) uniform sampler2D u_source;
layout(set = 0, binding = 3) uniform sampler2D u_a;
layout(set = 0, binding = 4) uniform sampler2D u_b;
layout(location = 0) out vec4 o_colour;
const int kTaps = KTAPS;

vec3 colour_at(ivec2 local) { return finite3(texelFetch(u_source, pc.rect.xy + local, 0).rgb); }
float cone(float d, float l) { return clamp(1.0 - d / l, 0.0, 1.0); }
float cylinder(float d, float l) { return 1.0 - smoothstep(0.95 * l, 1.05 * l, d); }
// How much a point at zb stands in front of one at za (1 in front or level, 0 a soft depth's share behind).
float in_front(float za, float zb) { return clamp(1.0 + (za - zb) / (pc.blur.z * min(za, zb)), 0.0, 1.0); }

void main()
{
    ivec2 local = ivec2(gl_FragCoord.xy) - pc.rect.xy;
    vec3 here = colour_at(local);
    vec2 n = texelFetch(u_b, min(local / pc.info.x, pc.sizes.zw - 1), 0).xy;
    if (!(length(n) >= 0.5)) {
        o_colour = vec4(here, 1.0);
        return;
    }
    vec4 px = texelFetch(u_a, local, 0);
    float lx = max(length(px.xy), 0.5);
    float total = 1.0 / lx;
    vec3 sum = here * total;
    float j = hash01(hash2(local.x, local.y, uint(pc.info.z))) - 0.5;
    for (int i = 0; i < kTaps; ++i) {
        float t = mix(-1.0, 1.0, (float(i) + 0.5 + j) / float(kTaps));
        vec2 offset = n * t;
        ivec2 y = clamp(ivec2(floor(vec2(local) + 0.5 + offset)), ivec2(0), pc.rect.zw - 1);
        vec4 py = texelFetch(u_a, y, 0);
        float ly = max(length(py.xy), 0.5);
        float d = length(offset);
        float w = in_front(px.z, py.z) * cone(d, ly) + in_front(py.z, px.z) * cone(d, lx) +
                  2.0 * cylinder(d, ly) * cylinder(d, lx);
        total += w;
        sum += w * colour_at(y);
    }
    o_colour = vec4(finite3(sum / total), 1.0);
}
)";

// 3a. the depth of field's half resolution: each 2 x 2 block's colour and circle (half-resolution pixels)
constexpr const char* kFocusDownBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) uniform sampler2D u_source;
layout(set = 0, binding = 3) uniform sampler2D u_a;
layout(set = 0, binding = 6, rgba16f) uniform writeonly image2D o_out;

void main()
{
    ivec2 h = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(h, pc.sizes.xy))) {
        return;
    }
    vec3 sum = vec3(0.0);
    float far_sum = 0.0;
    float nearest = 0.0;
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            ivec2 q = min(h * 2 + ivec2(x, y), pc.rect.zw - 1);
            sum += finite3(texelFetch(u_source, pc.rect.xy + q, 0).rgb);
            float c = circle_of(texelFetch(u_a, q, 0).z);
            far_sum += max(c, 0.0);
            nearest = min(nearest, c);
        }
    }
    float circle = nearest < -0.5 ? nearest : far_sum * 0.25;
    imageStore(o_out, h, vec4(sum * 0.25, circle * 0.5));
}
)";

// 3b. the near field's reach: per tile, the largest near circle of its 3 x 3 tiles
constexpr const char* kFocusReachBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 3) uniform sampler2D u_a;
layout(set = 0, binding = 6, rgba16f) uniform writeonly image2D o_out;
const int kTile = KDOF_TILE;

void main()
{
    ivec2 tile = ivec2(gl_GlobalInvocationID.xy);
    ivec2 tiles = (pc.sizes.xy + kTile - 1) / kTile;
    if (any(greaterThanEqual(tile, tiles))) {
        return;
    }
    ivec2 first = max((tile - 1) * kTile, ivec2(0));
    ivec2 last = min((tile + 2) * kTile, pc.sizes.xy);
    float reach = 0.0;
    for (int y = first.y; y < last.y; ++y) {
        for (int x = first.x; x < last.x; ++x) {
            reach = max(reach, -texelFetch(u_a, ivec2(x, y), 0).a);
        }
    }
    imageStore(o_out, tile, vec4(reach, 0.0, 0.0, 0.0));
}
)";

// 3c. the gather: the far field (out) and the near field with its coverage (out2), half resolution
constexpr const char* kFocusGatherBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 3) uniform sampler2D u_a;
layout(set = 0, binding = 4) uniform sampler2D u_b;
layout(set = 0, binding = 6, rgba16f) uniform writeonly image2D o_far;
layout(set = 0, binding = 7, rgba16f) uniform writeonly image2D o_near;
const int kTaps = KTAPS;
const int kTile = KDOF_TILE;
const float kGoldenAngle = 2.39996323;

void main()
{
    ivec2 h = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(h, pc.sizes.xy))) {
        return;
    }
    vec4 centre = texelFetch(u_a, h, 0);
    float reach = texelFetch(u_b, h / kTile, 0).r;
    float own_far = max(centre.a, 0.0);
    float radius = max(abs(centre.a), reach);
    if (!(radius >= 0.5)) {
        imageStore(o_far, h, vec4(centre.rgb, 1.0));
        imageStore(o_near, h, vec4(0.0));
        return;
    }
    vec3 far_sum = vec3(0.0);
    float far_weight = 0.0;
    vec3 near_sum = vec3(0.0);
    float near_weight = 0.0;
    if (centre.a >= 0.0) {
        far_sum = centre.rgb;
        far_weight = 1.0;
    } else {
        near_sum = centre.rgb;
        near_weight = 1.0;
    }
    for (int i = 0; i < kTaps; ++i) {
        float r = sqrt((float(i) + 0.5) / float(kTaps)) * radius;
        float a = float(i) * kGoldenAngle;
        vec2 o = r * vec2(cos(a), sin(a));
        ivec2 q = clamp(ivec2(floor(vec2(h) + 0.5 + o)), ivec2(0), pc.sizes.xy - 1);
        vec4 s = texelFetch(u_a, q, 0);
        if (s.a >= 0.0) {
            // (a far sample reaches the pixel only as far as the pixel's own circle: a sharper thing in front of a
            // softer background keeps its edge)
            float w = clamp(min(s.a, own_far) - r + 1.0, 0.0, 1.0);
            far_sum += s.rgb * w;
            far_weight += w;
        } else {
            float w = clamp(-s.a - r + 1.0, 0.0, 1.0);
            near_sum += s.rgb * w;
            near_weight += w;
        }
    }
    imageStore(o_far, h, vec4(far_weight > 0.0 ? far_sum / far_weight : centre.rgb, 1.0));
    float coverage = clamp(near_weight / float(kTaps + 1), 0.0, 1.0);
    imageStore(o_near, h, vec4(near_weight > 0.0 ? near_sum / near_weight : centre.rgb, coverage));
}
)";

// 3d. the composite (full screen)
constexpr const char* kFocusCompositeBody = R"(
layout(set = 0, binding = 0) uniform sampler2D u_source;
layout(set = 0, binding = 3) uniform sampler2D u_a;
layout(set = 0, binding = 4) uniform sampler2D u_b;
layout(set = 0, binding = 5) uniform sampler2D u_c;
layout(location = 0) out vec4 o_colour;

void main()
{
    ivec2 local = ivec2(gl_FragCoord.xy) - pc.rect.xy;
    vec3 here = finite3(texelFetch(u_source, pc.rect.xy + local, 0).rgb);
    float c = circle_of(texelFetch(u_a, local, 0).z);
    // the half-resolution fields at this pixel (texel centres at + 0.5; kept inside the viewport's half)
    vec2 at = clamp((vec2(local) + 0.5) * 0.5, vec2(0.5), vec2(pc.sizes.xy) - 0.5);
    vec4 far_field = textureLod(u_b, at / vec2(textureSize(u_b, 0)), 0.0);
    vec4 near_field = textureLod(u_c, at / vec2(textureSize(u_c, 0)), 0.0);
    vec3 o = here;
    if (c > 0.0) {
        o = mix(here, far_field.rgb, smoothstep(0.5, 2.0, c));
    }
    o = mix(o, near_field.rgb, clamp(near_field.a, 0.0, 1.0));
    o_colour = vec4(finite3(o), 1.0);
}
)";

// 4. the grade (full screen; grade_reference is its twin)
constexpr const char* kGradeBody = R"(
layout(set = 0, binding = 0) uniform sampler2D u_source;
layout(std430, set = 0, binding = 8) readonly buffer Table { vec4 entries[]; } u_table;
layout(location = 0) out vec4 o_colour;
const float kVignetteDepth = KVIGNETTE_DEPTH;
const float kGrainAmount = KGRAIN_AMOUNT;

vec3 entry(ivec3 i)
{
    int n = pc.info.w;
    return u_table.entries[i.x + n * (i.y + n * i.z)].rgb;
}
vec3 table_colour(vec3 c)
{
    int n = pc.info.w;
    vec3 u = clamp(c, 0.0, 1.0) * float(n - 1);
    ivec3 i = min(ivec3(u), ivec3(n - 2));
    vec3 f = u - vec3(i);
    vec3 c00 = mix(entry(i), entry(i + ivec3(1, 0, 0)), f.x);
    vec3 c10 = mix(entry(i + ivec3(0, 1, 0)), entry(i + ivec3(1, 1, 0)), f.x);
    vec3 c01 = mix(entry(i + ivec3(0, 0, 1)), entry(i + ivec3(1, 0, 1)), f.x);
    vec3 c11 = mix(entry(i + ivec3(0, 1, 1)), entry(i + ivec3(1, 1, 1)), f.x);
    return mix(mix(c00, c10, f.y), mix(c01, c11, f.y), f.z);
}

void main()
{
    ivec2 local = ivec2(gl_FragCoord.xy) - pc.rect.xy;
    vec3 c = finite3(texelFetch(u_source, pc.rect.xy + local, 0).rgb);
    vec3 g = table_colour(c);
    vec3 o = c + (g - c) * pc.grade.x;
    vec2 p = vec2(local) + 0.5;
    vec2 size = vec2(pc.rect.zw);
    if (pc.grade.y > 0.0) {
        float aspect = size.x / size.y;
        float corner = sqrt(aspect * aspect + 1.0) * 0.5;
        vec2 q = vec2((p.x / size.x - 0.5) * aspect, p.y / size.y - 0.5);
        float v = smoothstep(0.3, 1.0, length(q) / corner);
        o *= 1.0 - pc.grade.y * kVignetteDepth * v;
    }
    if (pc.grade.z > 0.0) {
        ivec2 cell = ivec2(floor(p / pc.grade.w));
        float n = hash01(hash2(cell.x, cell.y, uint(pc.info.z))) - 0.5;
        float l = clamp(dot(o, vec3(0.2126, 0.7152, 0.0722)), 0.0, 1.0);
        o += n * (pc.grade.z * kGrainAmount * (0.35 + 2.6 * l * (1.0 - l)));
    }
    o_colour = vec4(finite3(o), 1.0);
}
)";

struct CameraConstants {
    std::array<int32_t, 4> rect{};
    std::array<int32_t, 4> render{};
    std::array<int32_t, 4> info{};
    std::array<int32_t, 4> sizes{};
    Float4x4 to_before{};
    std::array<float, 4> lens{};
    std::array<float, 4> view{};
    std::array<float, 4> blur{};
    std::array<float, 4> dof{};
    std::array<float, 4> grade{};
};
constexpr std::size_t kConstantBytes = 208;
static_assert(sizeof(CameraConstants) == kConstantBytes, "the shaders' push-constant block");

struct FragmentPipeline {
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkPipeline pipeline = VK_NULL_HANDLE;
};

// A kind of pass timed (the blur and the depth of field together; the grade).
struct Timer {
    static constexpr uint32_t kPairs = 4;
    std::array<uint64_t, kPairs> serials{};
    uint64_t timed = 0;
    uint32_t pair = 0;
    bool timing = false;
    double last = -1.0;
    double window = 0.0;
    uint64_t samples = 0;
};

struct CameraState {
    CameraFxSettings settings;
    std::vector<GradeLevel> levels;
    // the objects
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkSampler linear = VK_NULL_HANDLE;
    VkSampler nearest = VK_NULL_HANDLE;
    VkShaderModule vertex = VK_NULL_HANDLE;
    VkPipeline prepare = VK_NULL_HANDLE;        // the depth alone (the depth of field)
    VkPipeline prepare_motion = VK_NULL_HANDLE; // and the motion (the blur)
    VkPipeline tiles = VK_NULL_HANDLE;
    VkPipeline neighbours = VK_NULL_HANDLE;
    VkPipeline focus_down = VK_NULL_HANDLE;
    VkPipeline focus_reach = VK_NULL_HANDLE;
    VkPipeline focus_gather = VK_NULL_HANDLE;
    std::vector<FragmentPipeline> blur;
    std::vector<FragmentPipeline> focus_composite;
    std::vector<FragmentPipeline> grade;
    bool failed = false;
    // the images (the colour target's size, or a part of it)
    Image source;
    Image prepared;
    Image tile_max;
    Image neighbour_max;
    Image half;
    Image reach;
    Image far_field;
    Image near_field;
    DeviceBuffer table;
    GradeLook table_look = GradeLook::kOff; // the look the table holds
    // the view being drawn
    bool began_motion = false;
    uint64_t camera_upscales_seen = 0;
    uint64_t grade_upscales_seen = 0;
    uint64_t lens_upscales_seen = 0;
    uint64_t cuts_seen = 0;
    // timing
    VkQueryPool timer_pool = VK_NULL_HANDLE;
    double tick_milliseconds = 0.0; // (negative: no timestamps here)
    Timer camera_timer;
    Timer grade_timer;
    // what ran
    CameraPassCounts counts;
    GradeParameters last_grade;
    FilmConstants last_lens;
};

CameraState& camera()
{
    static CameraState state;
    return state;
}

bool first_report(Backend& state, uint64_t what) { return state.reported.insert(what).second; }

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

std::array<uint32_t, 4> clip_rect(const std::array<uint32_t, 4>& rect, const Extent& extent)
{
    const uint32_t left = std::min(core::at(rect, 0), extent.width);
    const uint32_t top = std::min(core::at(rect, 1), extent.height);
    return {left, top, std::min(core::at(rect, 2), extent.width - left), std::min(core::at(rect, 3), extent.height - top)};
}

std::array<int32_t, 4> signed_rect(const std::array<uint32_t, 4>& rect)
{
    return {static_cast<int32_t>(core::at(rect, 0)), static_cast<int32_t>(core::at(rect, 1)),
            static_cast<int32_t>(core::at(rect, 2)), static_cast<int32_t>(core::at(rect, 3))};
}

float finite_or(float value, float fallback, float low, float high)
{
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}

// --- the objects ---------------------------------------------------------------------------------------------------

std::string with_numbers(std::string source)
{
    const auto put = [&source](const std::string& marker, const std::string& value) {
        for (std::size_t at = source.find(marker); at != std::string::npos; at = source.find(marker)) {
            source.replace(at, marker.size(), value);
        }
    };
    const auto number = [](float value) {
        std::string text = std::to_string(value);
        return text.find('.') == std::string::npos ? text + ".0" : text;
    };
    put("KNO_MOTION_HALF", number(kNoMotion * kHalf));
    put("KFARTHEST", number(kCameraFarthest));
    put("KVIGNETTE_DEPTH", number(kGradeVignetteDepth));
    put("KGRAIN_AMOUNT", number(kGradeGrainAmount));
    put("KDOF_TILE", std::to_string(kDofTile));
    return source;
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

std::string source_of(const char* body, int taps = 0, const char* defines = "")
{
    std::string source = with_numbers(std::string(kCommon) + body);
    source.insert(source.find('\n') + 1, defines); // (after #version)
    const std::string marker = "KTAPS";
    if (const std::size_t at = source.find(marker); at != std::string::npos) {
        source.replace(at, marker.size(), std::to_string(taps));
    }
    return source;
}

VkPipeline make_compute(Backend& state, const std::string& source, std::string* error)
{
    const VkDevice device = state.context.device();
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
                                           .layout = camera().layout,
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

// A full-screen pass's pipeline for a colour target (colour written, alpha kept), kept per format and samples.
VkPipeline fragment_pipeline(Backend& state, std::vector<FragmentPipeline>* made, const std::string& source,
                             VkFormat format, VkSampleCountFlagBits samples, std::string* error)
{
    for (const FragmentPipeline& pipeline : *made) {
        if (pipeline.format == format && pipeline.samples == samples) {
            return pipeline.pipeline;
        }
    }
    const CameraState& own = camera();
    const VkDevice device = state.context.device();
    const VkShaderModule fragment = make_module(device, RtShaderStage::kFragment, source, error);
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
    const VkPipelineMultisampleStateCreateInfo multisample{.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
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
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_FALSE;
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
    const VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
    vkDestroyShaderModule(device, fragment, nullptr);
    if (result != VK_SUCCESS) {
        *error = "vkCreateGraphicsPipelines failed";
        return VK_NULL_HANDLE;
    }
    made->push_back({.format = format, .samples = samples, .pipeline = pipeline});
    return pipeline;
}

// The shared objects (once): the layouts, the samplers, the vertex shader, the compute pipelines.
bool ensure_objects(Backend& state, std::string* error)
{
    CameraState& own = camera();
    if (own.focus_gather != VK_NULL_HANDLE) {
        return true;
    }
    const VkDevice device = state.context.device();
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(state.context.physical_device(), &properties);
    if (properties.limits.maxPushConstantsSize < sizeof(CameraConstants)) {
        *error = "the device's push constants are too small";
        return false;
    }
    if (own.layout == VK_NULL_HANDLE) {
        std::array<VkDescriptorSetLayoutBinding, kBindingCount> bindings{};
        for (uint32_t binding = 0; binding < kBindingCount; ++binding) {
            VkDescriptorType type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            if (binding == kBindingOut || binding == kBindingOut2) {
                type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            } else if (binding == kBindingTable) {
                type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
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
        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.maxLod = 0.0F;
        VkSamplerCreateInfo nearest_info = sampler_info;
        nearest_info.magFilter = VK_FILTER_NEAREST;
        nearest_info.minFilter = VK_FILTER_NEAREST;
        if (vkCreateDescriptorSetLayout(device, &set_info, nullptr, &own.set_layout) != VK_SUCCESS ||
            vkCreateSampler(device, &sampler_info, nullptr, &own.linear) != VK_SUCCESS ||
            vkCreateSampler(device, &nearest_info, nullptr, &own.nearest) != VK_SUCCESS) {
            *error = "the descriptor layout or the samplers";
            return false;
        }
        const VkPushConstantRange constants{.stageFlags = kStages, .offset = 0, .size = sizeof(CameraConstants)};
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
    }
    if (own.vertex == VK_NULL_HANDLE) {
        own.vertex = make_module(device, RtShaderStage::kVertex, kVertexSource, error);
        if (own.vertex == VK_NULL_HANDLE) {
            return false;
        }
    }
    const std::array<std::pair<VkPipeline*, std::string>, 7> computes{{
        {&own.prepare, source_of(kPrepareBody)},
        {&own.prepare_motion, source_of(kPrepareBody, 0, "#define WITH_MOTION\n")},
        {&own.tiles, source_of(kTilesBody)},
        {&own.neighbours, source_of(kNeighboursBody)},
        {&own.focus_down, source_of(kFocusDownBody)},
        {&own.focus_reach, source_of(kFocusReachBody)},
        {&own.focus_gather, source_of(kFocusGatherBody, kDofTaps)},
    }};
    for (const auto& [pipeline, source] : computes) {
        if (*pipeline == VK_NULL_HANDLE) {
            *pipeline = make_compute(state, source, error);
            if (*pipeline == VK_NULL_HANDLE) {
                return false;
            }
        }
    }
    return true;
}

// A work image (storage, sampled, readable back), made again when its size changed; it rests in GENERAL.
bool ensure_work_image(Backend& state, Image* image, Extent extent)
{
    extent.width = std::max(extent.width, 1U);
    extent.height = std::max(extent.height, 1U);
    if (image->image != VK_NULL_HANDLE && image->extent.width == extent.width && image->extent.height == extent.height) {
        return true;
    }
    retire_image(state, image);
    const ImageRequest request{.format = kWorkFormat,
                               .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                               .extent = extent,
                               .levels = 1,
                               .samples = VK_SAMPLE_COUNT_1_BIT,
                               .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                               .alpha_one = false};
    if (!state.memory.create_image(request, image)) {
        return false;
    }
    image->resting = VK_IMAGE_LAYOUT_GENERAL;
    prepare_image(state, image);
    return true;
}

// The colour copied out: the target's format and size (sampled).
bool ensure_source(Backend& state, const Image& colour)
{
    Image& source = camera().source;
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

// The grade's table for `look` (made again when the look changed).
bool ensure_table(Backend& state, GradeLook look)
{
    CameraState& own = camera();
    if (own.table.buffer != VK_NULL_HANDLE && own.table_look == look) {
        return true;
    }
    retire_buffer(state, &own.table);
    const std::vector<CameraRgb> entries = grade_table(look);
    constexpr std::size_t kEntryBytes = 4 * sizeof(float); // (std430 vec4)
    if (!state.memory.create_buffer(entries.size() * kEntryBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, BufferPlace::kUpload,
                                    &own.table)) {
        return false;
    }
    std::vector<float> values(entries.size() * 4, 0.0F);
    for (std::size_t entry = 0; entry < entries.size(); ++entry) {
        for (std::size_t channel = 0; channel < 3; ++channel) {
            core::at(values, (entry * 4) + channel) = core::at(core::at(entries, entry), channel);
        }
    }
    std::memcpy(own.table.mapped, values.data(), values.size() * sizeof(float));
    state.memory.flush(own.table, 0, values.size() * sizeof(float));
    own.table_look = look;
    return true;
}

// --- recording -----------------------------------------------------------------------------------------------------

// One pass's images and buffer (null: not bound).
struct Bound {
    const Image* source = nullptr;
    const Image* depth = nullptr;
    const Image* motion = nullptr;
    const Image* a = nullptr;
    const Image* b = nullptr;
    const Image* c = nullptr;
    const Image* out = nullptr;
    const Image* out2 = nullptr;
    const DeviceBuffer* table = nullptr;
};

void push_descriptors(VkCommandBuffer commands, VkPipelineBindPoint point, const Bound& bound)
{
    const CameraState& own = camera();
    std::array<VkDescriptorImageInfo, kBindingCount> infos{};
    std::array<VkWriteDescriptorSet, kBindingCount> writes{};
    VkDescriptorBufferInfo table{};
    uint32_t count = 0;
    const auto add = [&](uint32_t binding, const Image* image, VkSampler sampler, VkImageLayout layout) {
        if (image == nullptr) {
            return;
        }
        const bool storage = binding == kBindingOut || binding == kBindingOut2;
        VkDescriptorImageInfo& info = core::at(infos, binding);
        info = {.sampler = storage ? VK_NULL_HANDLE : sampler, .imageView = image->view, .imageLayout = layout};
        VkWriteDescriptorSet& write = core::at(writes, count++);
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstBinding = binding;
        write.descriptorCount = 1;
        write.descriptorType = storage ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &info;
    };
    add(kBindingSource, bound.source, own.linear, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    add(kBindingDepth, bound.depth, own.nearest, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    add(kBindingMotion, bound.motion, own.nearest, VK_IMAGE_LAYOUT_GENERAL);
    add(kBindingA, bound.a, own.linear, VK_IMAGE_LAYOUT_GENERAL);
    add(kBindingB, bound.b, own.linear, VK_IMAGE_LAYOUT_GENERAL);
    add(kBindingC, bound.c, own.linear, VK_IMAGE_LAYOUT_GENERAL);
    add(kBindingOut, bound.out, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL);
    add(kBindingOut2, bound.out2, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL);
    if (bound.table != nullptr) {
        table = {.buffer = bound.table->buffer, .offset = 0, .range = VK_WHOLE_SIZE};
        VkWriteDescriptorSet& write = core::at(writes, count++);
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstBinding = kBindingTable;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &table;
    }
    vkCmdPushDescriptorSetKHR(commands, point, own.layout, 0, count, writes.data());
}

void compute_done(VkCommandBuffer commands)
{
    const VkMemoryBarrier written{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                  .pNext = nullptr,
                                  .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                  .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &written, 0,
                         nullptr, 0, nullptr);
}

// The work images: the last pass's reads of them done before they are written again.
void reads_done(VkCommandBuffer commands)
{
    const VkMemoryBarrier barrier{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                  .pNext = nullptr,
                                  .srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                                  .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void dispatch(VkCommandBuffer commands, VkPipeline pipeline, const Bound& bound, uint32_t width, uint32_t height)
{
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    push_descriptors(commands, VK_PIPELINE_BIND_POINT_COMPUTE, bound);
    vkCmdDispatch(commands, (width + kGroupSide - 1) / kGroupSide, (height + kGroupSide - 1) / kGroupSide, 1);
    compute_done(commands);
}

// A full-screen pass over the viewport `shown` of `colour`.
void draw(VkCommandBuffer commands, VkPipeline pipeline, const Bound& bound, Image* colour, const std::array<uint32_t, 4>& shown)
{
    Memory::transition(commands, colour, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo colour_info{};
    colour_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colour_info.imageView = colour->attachment_view;
    colour_info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colour_info.resolveMode = VK_RESOLVE_MODE_NONE;
    colour_info.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colour_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    const VkRect2D area{.offset = {.x = static_cast<int32_t>(core::at(shown, 0)), .y = static_cast<int32_t>(core::at(shown, 1))},
                        .extent = {.width = core::at(shown, 2), .height = core::at(shown, 3)}};
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
    push_descriptors(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, bound);
    vkCmdDraw(commands, 3, 1, 0, 0);
    vkCmdEndRendering(commands);
}

// The viewport's colour copied into the source image (resolved when multisampled).
void copy_out(VkCommandBuffer commands, Image* colour, const std::array<uint32_t, 4>& shown)
{
    Image& source = camera().source;
    Memory::transition(commands, colour, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    Memory::transition(commands, &source, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, true);
    const VkImageSubresourceLayers layers{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1};
    const VkOffset3D corner{.x = static_cast<int32_t>(core::at(shown, 0)), .y = static_cast<int32_t>(core::at(shown, 1)), .z = 0};
    const VkExtent3D size{.width = core::at(shown, 2), .height = core::at(shown, 3), .depth = 1};
    if (colour->samples == VK_SAMPLE_COUNT_1_BIT) {
        const VkImageCopy region{.srcSubresource = layers, .srcOffset = corner, .dstSubresource = layers, .dstOffset = corner, .extent = size};
        vkCmdCopyImage(commands, colour->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, source.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    } else {
        const VkImageResolve region{.srcSubresource = layers, .srcOffset = corner, .dstSubresource = layers, .dstOffset = corner, .extent = size};
        vkCmdResolveImage(commands, colour->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, source.image,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    }
    Memory::transition(commands, &source, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

// The depth moved to be sampled by the passes, and back to its resting layout after them.
void depth_barrier(VkCommandBuffer commands, Image* depth, VkImageLayout layout, bool to_passes)
{
    const VkPipelineStageFlags tests = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
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
        .subresourceRange = {.aspectMask = depth->aspect, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1},
    };
    vkCmdPipelineBarrier(commands, to_passes ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : passes, to_passes ? passes : tests, 0, 0,
                         nullptr, 0, nullptr, 1, &barrier);
    depth->layout = layout;
}

// --- timing ----------------------------------------------------------------------------------------------------------

void ensure_timer(Backend& state)
{
    CameraState& own = camera();
    if (own.timer_pool != VK_NULL_HANDLE || own.tick_milliseconds < 0.0) {
        return;
    }
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(state.context.physical_device(), &properties);
    constexpr double kNanosecondsPerMillisecond = 1.0e6;
    const VkQueryPoolCreateInfo info{.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                                     .pNext = nullptr,
                                     .flags = 0,
                                     .queryType = VK_QUERY_TYPE_TIMESTAMP,
                                     .queryCount = Timer::kPairs * 2 * 2,
                                     .pipelineStatistics = 0};
    if (!(properties.limits.timestampPeriod > 0.0F) ||
        vkCreateQueryPool(state.context.device(), &info, nullptr, &own.timer_pool) != VK_SUCCESS) {
        own.timer_pool = VK_NULL_HANDLE;
        own.tick_milliseconds = -1.0;
        return;
    }
    own.tick_milliseconds = static_cast<double>(properties.limits.timestampPeriod) / kNanosecondsPerMillisecond;
}

// The timing's first stamp (when a pair is free: its last result read first). `base`: the timer's first query.
void begin_timing(Backend& state, VkCommandBuffer commands, Timer* timer, uint32_t base, const char* name, Extent size)
{
    CameraState& own = camera();
    ensure_timer(state);
    timer->timing = false;
    if (own.timer_pool == VK_NULL_HANDLE) {
        return;
    }
    timer->pair = static_cast<uint32_t>(timer->timed % Timer::kPairs);
    uint64_t& written = core::at(timer->serials, timer->pair);
    if (written != 0 && state.frames.completed() < written) {
        return;
    }
    const uint32_t first = base + (timer->pair * 2);
    if (written != 0) {
        struct Stamp {
            uint64_t value = 0;
            uint64_t available = 0;
        };
        std::array<Stamp, 2> stamps{};
        const VkResult got = vkGetQueryPoolResults(state.context.device(), own.timer_pool, first, 2, sizeof(stamps),
                                                   stamps.data(), sizeof(Stamp),
                                                   VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        if ((got == VK_SUCCESS || got == VK_NOT_READY) && stamps.front().available != 0 && stamps.back().available != 0 &&
            stamps.back().value >= stamps.front().value) {
            timer->last = static_cast<double>(stamps.back().value - stamps.front().value) * own.tick_milliseconds;
            timer->window += timer->last;
            if (++timer->samples >= kTimingLogEvery) {
                get_log("vulkan: %s: %.3f ms on the device (%ux%u; the last %llu passes' average)", name,
                        timer->window / static_cast<double>(timer->samples), size.width, size.height,
                        static_cast<unsigned long long>(timer->samples));
                timer->samples = 0;
                timer->window = 0.0;
            }
        }
    }
    written = state.frames.serial();
    vkCmdResetQueryPool(commands, own.timer_pool, first, 2);
    vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, own.timer_pool, first);
    timer->timing = true;
}

void end_timing(VkCommandBuffer commands, Timer* timer, uint32_t base)
{
    if (!timer->timing) {
        return;
    }
    vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, camera().timer_pool, base + (timer->pair * 2) + 1);
    ++timer->timed;
    timer->timing = false;
}

constexpr uint32_t kCameraTimerBase = 0;
constexpr uint32_t kGradeTimerBase = Timer::kPairs * 2;

// --- the view ------------------------------------------------------------------------------------------------------

// Where a view's colour is (the upscaler's output when it upscaled the view), and its drawn rectangle; false when the
// target can't be read.
struct ViewPlan {
    Image* colour = nullptr;
    std::array<uint32_t, 4> shown{};
    std::array<uint32_t, 4> drawn{};
};

bool plan_view(Backend& state, const rt::ViewDrawn& view, uint64_t* upscales_seen, ViewPlan* plan)
{
    Image* const colour = image_of(core::at(state.targets, 0));
    const bool upscaled = upscale_passes() != *upscales_seen && view.output_rect != view.rect &&
                          core::at(view.output_rect, 2) != 0 && core::at(view.output_rect, 3) != 0;
    *upscales_seen = upscale_passes();
    if (colour == nullptr || colour->attachment_view == VK_NULL_HANDLE || (colour->aspect & VK_IMAGE_ASPECT_COLOR_BIT) == 0 ||
        (colour->usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0) {
        if (first_report(state, kReportTarget)) {
            get_log("vulkan: camera effects: a view drawn into a target that can't be read - none there");
        }
        return false;
    }
    plan->colour = colour;
    plan->shown = clip_rect(upscaled ? view.output_rect : view.rect, colour->extent);
    plan->drawn = view.rect;
    return core::at(plan->shown, 2) != 0 && core::at(plan->shown, 3) != 0;
}

// Whether the camera jumped between the frame before and this one (moved or turned past a cut's limits).
bool camera_jumped(const MotionVectors& motion)
{
    Float4x4 now_inverse{};
    Float4x4 before_inverse{};
    if (!motion_inverse(motion.now.view, &now_inverse) || !motion_inverse(motion.before.view, &before_inverse)) {
        return true;
    }
    constexpr std::size_t kSide = 4;
    const auto at = [](const Float4x4& matrix, std::size_t row, std::size_t column) {
        return core::at(matrix, (row * kSide) + column);
    };
    float moved = 0.0F;
    float facing = 0.0F;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const float step = at(now_inverse, 3, axis) - at(before_inverse, 3, axis);
        moved += step * step;
        facing += at(motion.now.view, axis, 2) * at(motion.before.view, axis, 2);
    }
    return std::sqrt(moved) > kCutDistance || facing < kCutTurnCosine;
}

int blur_tile(uint32_t height)
{
    return std::clamp(static_cast<int>(std::lround(static_cast<float>(height) * kBlurTileShare)), kBlurLeastTile, kBlurMostTile);
}

// The listener of the blur and the depth of field.
class CameraListener final : public rt::ViewListener {
public:
    [[nodiscard]] int order() const override { return rt::ViewOrder::kCameraEffects; }
    void view_begins(const rt::ViewDrawn& view) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (state.device_made && state.draws_made) {
            begin_camera_view(state, view);
        }
    }
    void opaque_drawn(const rt::ViewDrawn& /*view*/) override { camera().camera_upscales_seen = upscale_passes(); }
    void world_drawn(const rt::ViewDrawn& view) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (state.device_made && state.draws_made) {
            record_camera_pass(state, view);
        }
    }
};

class GradeListener final : public rt::ViewListener {
public:
    [[nodiscard]] int order() const override { return rt::ViewOrder::kGrade; }
    void opaque_drawn(const rt::ViewDrawn& /*view*/) override { camera().grade_upscales_seen = upscale_passes(); }
    void world_drawn(const rt::ViewDrawn& view) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (state.device_made && state.draws_made) {
            record_grade_pass(state, view);
        }
    }
};

// The fisheye lens' listener (vulkan_fisheye.h).
class LensListener final : public rt::ViewListener {
public:
    [[nodiscard]] int order() const override { return rt::ViewOrder::kLens; }
    void opaque_drawn(const rt::ViewDrawn& /*view*/) override { camera().lens_upscales_seen = upscale_passes(); }
    void world_drawn(const rt::ViewDrawn& view) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (state.device_made && state.draws_made) {
            record_lens_pass(state, view);
        }
    }
};

LensListener& lens_listener()
{
    static LensListener instance;
    return instance;
}

CameraListener& camera_listener()
{
    static CameraListener instance;
    return instance;
}

GradeListener& grade_listener()
{
    static GradeListener instance;
    return instance;
}

void log_failure(Backend& state, const std::string& error)
{
    camera().failed = true;
    if (first_report(state, kReportFailed)) {
        get_log("vulkan: camera effects: the passes couldn't be made (%s) - none drawn", error.empty() ? "their images" : error.c_str());
    }
}

// The look the grade draws for a view (rt_grade=level resolved).
GradeLook look_for(const CameraState& own, const rt::ViewDrawn& view)
{
    if (own.settings.grade != GradeLook::kLevel) {
        return own.settings.grade;
    }
    GradeLook look = GradeLook::kNeutral;
    for (const GradeLevel& level : own.levels) {
        if (level.scene == view.level && view.level != 0) {
            look = level.look;
        }
    }
    return look;
}

} // namespace

// --- the settings ----------------------------------------------------------------------------------------------------

void set_camera_fx_settings(const CameraFxSettings& settings)
{
    CameraFxSettings kept = settings;
    kept.motion_blur_strength = finite_or(settings.motion_blur_strength, kMotionBlurShutter, 0.0F, 1.0F);
    kept.dof_strength = finite_or(settings.dof_strength, kDofStrength, 0.0F, 1.0F);
    kept.grade_strength = finite_or(settings.grade_strength, 1.0F, 0.0F, 1.0F);
    kept.grade_vignette = finite_or(settings.grade_vignette, 0.0F, 0.0F, 1.0F);
    kept.grade_grain = finite_or(settings.grade_grain, 0.0F, 0.0F, 1.0F);
    kept.fisheye_strength = finite_or(settings.fisheye_strength, kFisheyeStrength, 0.0F, 1.0F);
    kept.fisheye_vignette = finite_or(settings.fisheye_vignette, kFisheyeVignette, 0.0F, 1.0F);
    kept.fisheye_circle = finite_or(settings.fisheye_circle, 0.0F, 0.0F, 1.0F);
    std::vector<GradeLevel> levels;
    if (!parse_grade_levels(kept.grade_levels, &levels)) {
        get_log("[Graphics] rt_grade_levels=%s: a pair isn't <scene>:<look> - left out", kept.grade_levels.c_str());
    }
    {
        const std::scoped_lock lock(backend().mutex);
        CameraState& own = camera();
        own.settings = kept;
        own.levels = std::move(levels);
        own.failed = false;
    }
    const bool camera_on = (kept.motion_blur && kept.motion_blur_strength > 0.0F) || (kept.dof && kept.dof_strength > 0.0F);
    if (camera_on) {
        rt::add_view_listener(&camera_listener());
    } else {
        rt::remove_view_listener(&camera_listener());
    }
    if (kept.grade != GradeLook::kOff) {
        rt::add_view_listener(&grade_listener());
    } else {
        rt::remove_view_listener(&grade_listener());
    }
    // the lens: the game camera widened (render_pc, rt_scene.h ViewLens) only while it is drawn - with nothing to do
    // (no distortion, vignette or frame) neither
    const bool lens_on = kept.fisheye && (kept.fisheye_strength > 0.0F || kept.fisheye_vignette > 0.0F || kept.fisheye_circle > 0.0F);
    rt::set_view_lens({.on = lens_on, .strength = kept.fisheye_strength, .circle = kept.fisheye_circle});
    if (lens_on) {
        rt::add_view_listener(&lens_listener());
        get_log("vulkan: camera effects: fisheye lens on (strength %.2f, vignette %.2f, circle %.2f)",
                static_cast<double>(kept.fisheye_strength), static_cast<double>(kept.fisheye_vignette),
                static_cast<double>(kept.fisheye_circle));
    } else {
        rt::remove_view_listener(&lens_listener());
    }
    if (camera_on || kept.grade != GradeLook::kOff) {
        get_log("vulkan: camera effects: motion blur %s (shutter %.2f), depth of field %s (%.2f), grade %s (%.2f, vignette "
                "%.2f, grain %.2f)",
                kept.motion_blur ? "on" : "off", static_cast<double>(kept.motion_blur_strength), kept.dof ? "on" : "off",
                static_cast<double>(kept.dof_strength), grade_look_name(kept.grade), static_cast<double>(kept.grade_strength),
                static_cast<double>(kept.grade_vignette), static_cast<double>(kept.grade_grain));
    }
}

CameraFxSettings camera_fx_settings()
{
    const std::scoped_lock lock(backend().mutex);
    return camera().settings;
}

// --- the passes ------------------------------------------------------------------------------------------------------

void begin_camera_view(Backend& state, const rt::ViewDrawn& view)
{
    CameraState& own = camera();
    own.began_motion = false;
    if (!own.settings.motion_blur || own.failed || state.motion.active) {
        return; // (off, or the upscaler began the view's motion)
    }
    const Image* const colour = image_of(core::at(state.targets, 0));
    if (colour == nullptr || colour->attachment_view == VK_NULL_HANDLE || colour->samples != VK_SAMPLE_COUNT_1_BIT) {
        return;
    }
    const Extent output{.width = std::max(core::at(view.output_rect, 2), 1U), .height = std::max(core::at(view.output_rect, 3), 1U)};
    own.began_motion = begin_motion(state, view, *colour, output);
}

bool record_camera_pass(Backend& state, const rt::ViewDrawn& view)
{
    CameraState& own = camera();
    MotionVectors& motion = state.motion;
    const bool motion_ours = own.began_motion;
    own.began_motion = false;
    // (the motion's view ends here when the blur began it; the upscaler ended its own)
    const bool motion_live = motion_ours ? motion.active : motion.now.known && motion.frame == state.frame;
    const bool motion_fresh = motion.fresh;
    if (motion_ours) {
        motion.active = false;
    }
    const CameraFxSettings& settings = own.settings;
    ViewPlan plan;
    if (own.failed || !plan_view(state, view, &own.camera_upscales_seen, &plan)) {
        return false;
    }
    const uint64_t cuts = frame_blend::cuts();
    const bool cut = cuts != own.cuts_seen;
    own.cuts_seen = cuts;
    bool blur = settings.motion_blur && settings.motion_blur_strength > 0.0F && !view.menu && !view.game_blur && motion_live &&
                motion.image.image != VK_NULL_HANDLE;
    if (blur && (cut || camera_jumped(motion))) {
        blur = false;
        ++own.counts.cut_skips;
    }
    const bool focus = settings.dof && settings.dof_strength > 0.0F && !view.menu && view.focus_known;
    if (!blur && !focus) {
        return false;
    }
    Image* const colour = plan.colour;
    if (colour->samples != VK_SAMPLE_COUNT_1_BIT) {
        if (first_report(state, kReportMultisampled)) {
            get_log("vulkan: camera effects: the world's target is multisampled ([Graphics] msaa / AntiAliasing) - no motion "
                    "blur or depth of field while it is (set msaa=0)");
        }
        return false;
    }
    Image* const depth = image_of(state.depth);
    const std::array<uint32_t, 4> drawn = depth != nullptr ? clip_rect(plan.drawn, depth->extent) : std::array<uint32_t, 4>{};
    if (depth == nullptr || (depth->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) == 0 || (depth->usage & VK_IMAGE_USAGE_SAMPLED_BIT) == 0 ||
        depth->view == VK_NULL_HANDLE || depth->samples != VK_SAMPLE_COUNT_1_BIT || core::at(drawn, 2) == 0 ||
        core::at(drawn, 3) == 0) {
        if (first_report(state, kReportNoDepth)) {
            get_log("vulkan: camera effects: no samplable depth with the view - no motion blur or depth of field there");
        }
        return false;
    }
    const Extent whole = colour->extent;
    const Extent half{.width = (whole.width + 1) / 2, .height = (whole.height + 1) / 2};
    const Extent tiles{.width = (whole.width + kBlurLeastTile - 1) / kBlurLeastTile,
                       .height = (whole.height + kBlurLeastTile - 1) / kBlurLeastTile};
    const Extent reach_tiles{.width = (half.width + kDofTile - 1) / kDofTile, .height = (half.height + kDofTile - 1) / kDofTile};
    std::string error;
    const VkPipeline blur_pipeline =
        ensure_objects(state, &error) && blur
            ? fragment_pipeline(state, &own.blur, source_of(kBlurBody, kBlurTaps), colour->format, colour->samples, &error)
            : VK_NULL_HANDLE;
    const VkPipeline focus_pipeline =
        own.focus_gather != VK_NULL_HANDLE && focus
            ? fragment_pipeline(state, &own.focus_composite, source_of(kFocusCompositeBody), colour->format, colour->samples, &error)
            : VK_NULL_HANDLE;
    const bool made = own.focus_gather != VK_NULL_HANDLE && (!blur || blur_pipeline != VK_NULL_HANDLE) &&
                      (!focus || focus_pipeline != VK_NULL_HANDLE) && ensure_source(state, *colour) &&
                      ensure_work_image(state, &own.prepared, whole) &&
                      (!blur || (ensure_work_image(state, &own.tile_max, tiles) && ensure_work_image(state, &own.neighbour_max, tiles))) &&
                      (!focus || (ensure_work_image(state, &own.half, half) && ensure_work_image(state, &own.reach, reach_tiles) &&
                                  ensure_work_image(state, &own.far_field, half) && ensure_work_image(state, &own.near_field, half)));
    if (!made) {
        log_failure(state, error);
        return false;
    }
    // the constants
    const std::array<uint32_t, 4>& shown = plan.shown;
    const uint32_t width = core::at(shown, 2);
    const uint32_t height = core::at(shown, 3);
    const int tile = blur_tile(height);
    CameraConstants constants{};
    constants.rect = signed_rect(shown);
    constants.render = signed_rect(drawn);
    const uint32_t flags = (blur ? kFlagBlur : 0U) | (blur && !motion_fresh ? kFlagMotionDrawn : 0U);
    constants.info = {tile, static_cast<int32_t>(flags), static_cast<int32_t>(state.frames.serial() & kSeedMask), 0};
    constants.sizes = {static_cast<int32_t>((width + 1) / 2), static_cast<int32_t>((height + 1) / 2),
                       static_cast<int32_t>((width + static_cast<uint32_t>(tile) - 1) / static_cast<uint32_t>(tile)),
                       static_cast<int32_t>((height + static_cast<uint32_t>(tile) - 1) / static_cast<uint32_t>(tile))};
    {
        constexpr std::size_t kRowZ = 2;
        constexpr std::size_t kRowW = 3;
        const auto& projection = view.camera.projection;
        constants.lens = {core::at(core::at(projection, kRowZ), kRowZ), core::at(core::at(projection, kRowW), kRowZ),
                          core::at(core::at(projection, kRowZ), kRowW), core::at(core::at(projection, kRowW), kRowW)};
        const float span = core::at(view.depth_range, 1) - core::at(view.depth_range, 0);
        constants.view = {core::at(view.depth_range, 0), span > 0.0F ? 1.0F / span : 1.0F, core::at(view.jitter, 0),
                          core::at(view.jitter, 1)};
        if (blur) {
            // this frame's clip space (jittered) -> the world -> the frame before's (unjittered)
            Float4x4 now_view{};
            Float4x4 now_projection{};
            for (std::size_t row = 0; row < 4; ++row) {
                for (std::size_t column = 0; column < 4; ++column) {
                    core::at(now_view, (row * 4) + column) = core::at(core::at(view.camera.view, row), column);
                    core::at(now_projection, (row * 4) + column) = core::at(core::at(projection, row), column);
                }
            }
            const Float4x4 now = motion_multiply(now_view, now_projection);
            Float4x4 to_world{};
            if (!motion_inverse(now, &to_world)) {
                to_world = now;
            }
            constants.to_before = motion_multiply(to_world, motion_multiply(motion.before.view, motion.before.projection));
        }
    }
    constants.blur = {settings.motion_blur_strength * kHalf, static_cast<float>(tile), kBlurSoftDepth, 0.0F};
    constants.dof = {std::max(view.focus_distance, 1.0F), settings.dof_strength * kDofAperture * static_cast<float>(height), 0.0F,
                     0.0F};
    // the passes
    VkCommandBuffer commands = outside_pass(state);
    begin_timing(state, commands, &own.camera_timer, kCameraTimerBase, "camera effects (motion blur, depth of field)",
                 {.width = width, .height = height});
    copy_out(commands, colour, shown);
    for (Image* image : {&own.prepared, &own.tile_max, &own.neighbour_max, &own.half, &own.reach, &own.far_field, &own.near_field}) {
        if (image->image != VK_NULL_HANDLE) {
            Memory::transition(commands, image, VK_IMAGE_LAYOUT_GENERAL);
        }
    }
    if (blur) {
        Memory::transition(commands, &motion.image, VK_IMAGE_LAYOUT_GENERAL, true);
    }
    reads_done(commands);
    const VkImageLayout depth_resting = depth->layout;
    depth_barrier(commands, depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true);
    vkCmdPushConstants(commands, own.layout, kStages, 0, sizeof(constants), &constants);
    // 1. the preparation (with the blur: the motion too)
    dispatch(commands, blur ? own.prepare_motion : own.prepare,
             {.depth = depth, .motion = blur ? &motion.image : nullptr, .out = &own.prepared}, width, height);
    const Extent focus_half{.width = (width + 1) / 2, .height = (height + 1) / 2};
    if (focus) {
        // 3. the depth of field
        dispatch(commands, own.focus_down, {.source = &own.source, .a = &own.prepared, .out = &own.half}, focus_half.width,
                 focus_half.height);
        dispatch(commands, own.focus_reach, {.a = &own.half, .out = &own.reach},
                 (focus_half.width + kDofTile - 1) / kDofTile, (focus_half.height + kDofTile - 1) / kDofTile);
        dispatch(commands, own.focus_gather, {.a = &own.half, .b = &own.reach, .out = &own.far_field, .out2 = &own.near_field},
                 focus_half.width, focus_half.height);
        draw(commands, focus_pipeline,
             {.source = &own.source, .a = &own.prepared, .b = &own.far_field, .c = &own.near_field}, colour, shown);
        ++own.counts.focuses;
        if (first_report(state, kReportFirstFocus)) {
            get_log("vulkan: camera effects: the first depth of field (%ux%u, focus %.0f, circle up to %.1f pixels)", width,
                    height, static_cast<double>(core::at(constants.dof, 0)), static_cast<double>(core::at(constants.dof, 1)));
        }
        if (blur) {
            copy_out(commands, colour, shown); // (the blur reads the focused picture)
        }
    }
    if (blur) {
        // 2. the motion blur
        const auto tiles_across = static_cast<uint32_t>(core::at(constants.sizes, 2));
        const auto tiles_down = static_cast<uint32_t>(core::at(constants.sizes, 3));
        dispatch(commands, own.tiles, {.a = &own.prepared, .out = &own.tile_max}, tiles_across, tiles_down);
        dispatch(commands, own.neighbours, {.a = &own.tile_max, .out = &own.neighbour_max}, tiles_across, tiles_down);
        draw(commands, blur_pipeline, {.source = &own.source, .a = &own.prepared, .b = &own.neighbour_max}, colour, shown);
        ++own.counts.blurs;
        if (first_report(state, kReportFirstBlur)) {
            get_log("vulkan: camera effects: the first motion blur (%ux%u, tiles of %d pixels, %s motion vectors)", width,
                    height, tile, motion_ours ? "its own" : "the upscaler's");
        }
    }
    depth_barrier(commands, depth,
                  depth_resting == VK_IMAGE_LAYOUT_UNDEFINED ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : depth_resting,
                  false);
    end_timing(commands, &own.camera_timer, kCameraTimerBase);
    if (colour->layout != colour->resting) {
        Memory::transition(commands, colour, colour->resting);
    }
    state.bound = BoundState{};
    const uint64_t serial = state.frames.serial();
    for (Image* image : {colour, depth, &own.source, &own.prepared, &own.tile_max, &own.neighbour_max, &own.half, &own.reach,
                         &own.far_field, &own.near_field, &motion.image}) {
        if (image->image != VK_NULL_HANDLE) {
            image->used = serial;
        }
    }
    return true;
}

bool record_grade_pass(Backend& state, const rt::ViewDrawn& view)
{
    CameraState& own = camera();
    const CameraFxSettings& settings = own.settings;
    ViewPlan plan;
    if (settings.grade == GradeLook::kOff || own.failed || !plan_view(state, view, &own.grade_upscales_seen, &plan)) {
        return false;
    }
    GradeParameters parameters;
    parameters.look = look_for(own, view);
    parameters.strength = parameters.look == GradeLook::kNeutral ? 0.0F : settings.grade_strength;
    // (an art style with its own vignette or grain keeps it)
    const ArtStyle style = style_settings_locked().style;
    const bool style_vignette = style == ArtStyle::kNoir || style == ArtStyle::kThermal;
    const bool style_grain = style_vignette || style == ArtStyle::kComic || style == ArtStyle::kSketch;
    parameters.vignette = style_vignette ? 0.0F : settings.grade_vignette;
    parameters.grain = style_grain ? 0.0F : settings.grade_grain;
    if (!(parameters.strength > 0.0F) && !(parameters.vignette > 0.0F) && !(parameters.grain > 0.0F)) {
        return false; // (nothing would change)
    }
    const std::array<uint32_t, 4>& shown = plan.shown;
    const uint32_t width = core::at(shown, 2);
    const uint32_t height = core::at(shown, 3);
    constexpr float kBaseHeight = 720.0F;
    parameters.cell = std::max(1.0F, std::round(static_cast<float>(height) / kBaseHeight));
    parameters.seed = static_cast<uint32_t>(state.frames.serial() & kSeedMask);
    Image* const colour = plan.colour;
    std::string error;
    const VkPipeline pipeline =
        ensure_objects(state, &error)
            ? fragment_pipeline(state, &own.grade, source_of(kGradeBody), colour->format, colour->samples, &error)
            : VK_NULL_HANDLE;
    if (pipeline == VK_NULL_HANDLE || !ensure_source(state, *colour) || !ensure_table(state, parameters.look)) {
        log_failure(state, error);
        return false;
    }
    CameraConstants constants{};
    constants.rect = signed_rect(shown);
    constants.info = {0, 0, static_cast<int32_t>(parameters.seed), static_cast<int32_t>(kGradeTableSize)};
    constants.grade = {parameters.strength, parameters.vignette, parameters.grain, parameters.cell};
    VkCommandBuffer commands = outside_pass(state);
    begin_timing(state, commands, &own.grade_timer, kGradeTimerBase, "camera effects (grade)", {.width = width, .height = height});
    copy_out(commands, colour, shown);
    vkCmdPushConstants(commands, own.layout, kStages, 0, sizeof(constants), &constants);
    draw(commands, pipeline, {.source = &own.source, .table = &own.table}, colour, shown);
    end_timing(commands, &own.grade_timer, kGradeTimerBase);
    if (colour->layout != colour->resting) {
        Memory::transition(commands, colour, colour->resting);
    }
    state.bound = BoundState{};
    const uint64_t serial = state.frames.serial();
    colour->used = serial;
    own.source.used = serial;
    ++own.counts.grades;
    own.last_grade = parameters;
    if (first_report(state, kReportFirstGrade)) {
        get_log("vulkan: camera effects: the first grade (%s, %ux%u)", grade_look_name(parameters.look), width, height);
    }
    return true;
}

CameraPassCounts camera_pass_counts()
{
    const std::scoped_lock lock(backend().mutex);
    return camera().counts;
}

bool record_lens_pass(Backend& state, const rt::ViewDrawn& view)
{
    CameraState& own = camera();
    const CameraFxSettings& settings = own.settings;
    ViewPlan plan;
    // (a view the game camera wasn't widened for - a menu's front end, the editor, split screen - left as it is)
    if (!settings.fisheye || !view.lens.on || !plan_view(state, view, &own.lens_upscales_seen, &plan)) {
        return false;
    }
    const FilmConstants constants = fisheye_constants(view.lens, settings.fisheye_vignette, plan.shown);
    if (!record_fisheye(state, constants, {.colour = plan.colour, .shown = plan.shown})) {
        return false;
    }
    ++own.counts.lenses;
    own.last_lens = constants;
    return true;
}

FilmConstants lens_last_constants()
{
    const std::scoped_lock lock(backend().mutex);
    return camera().last_lens;
}

GradeParameters grade_last_parameters()
{
    const std::scoped_lock lock(backend().mutex);
    return camera().last_grade;
}

CameraTimings camera_timings()
{
    const std::scoped_lock lock(backend().mutex);
    return {.camera = camera().camera_timer.last, .grade = camera().grade_timer.last};
}

void release_camera_fx(Backend& state)
{
    CameraState& own = camera();
    release_fisheye(state);
    const VkDevice device = state.context.device();
    if (device != nullptr) {
        for (std::vector<FragmentPipeline>* made : {&own.blur, &own.focus_composite, &own.grade}) {
            for (const FragmentPipeline& pipeline : *made) {
                vkDestroyPipeline(device, pipeline.pipeline, nullptr);
            }
            made->clear();
        }
        for (VkPipeline* pipeline : {&own.prepare, &own.prepare_motion, &own.tiles, &own.neighbours, &own.focus_down, &own.focus_reach, &own.focus_gather}) {
            if (*pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, *pipeline, nullptr);
            }
            *pipeline = VK_NULL_HANDLE;
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
        for (VkSampler sampler : {own.linear, own.nearest}) {
            if (sampler != VK_NULL_HANDLE) {
                vkDestroySampler(device, sampler, nullptr);
            }
        }
        if (own.timer_pool != VK_NULL_HANDLE) {
            vkDestroyQueryPool(device, own.timer_pool, nullptr);
        }
    }
    if (state.memory.ready()) {
        for (Image* image : {&own.source, &own.prepared, &own.tile_max, &own.neighbour_max, &own.half, &own.reach,
                             &own.far_field, &own.near_field, &state.motion.image}) {
            state.memory.destroy_image(image);
        }
        state.memory.destroy_buffer(&own.table);
    }
    own.vertex = VK_NULL_HANDLE;
    own.layout = VK_NULL_HANDLE;
    own.set_layout = VK_NULL_HANDLE;
    own.linear = VK_NULL_HANDLE;
    own.nearest = VK_NULL_HANDLE;
    own.timer_pool = VK_NULL_HANDLE;
    own.tick_milliseconds = 0.0;
    own.camera_timer = {};
    own.grade_timer = {};
    own.table_look = GradeLook::kOff;
    own.began_motion = false;
    own.failed = false;
}

} // namespace render::vulkan
