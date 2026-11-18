// NOT ORIGINAL - HDR rendering, tone mapping and colour grading (vulkan_hdr.h; docs/design/vulkan.md "HDR").
#include "gpu_vulkan/vulkan_hdr.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "engine/file/disk.h"
#include "engine/gfx/rt_daylight.h"
#include "engine/gfx/rt_display.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/rt_sky.h"
#include "gpu_vulkan/glsl_compiler.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_objects.h"
#include "gpu_vulkan/vulkan_upscale.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

// The once-only reports' keys (Backend::reported).
constexpr uint64_t kReportNotFloat = 0x48445201U;
constexpr uint64_t kReportPipeline = 0x48445202U;
constexpr uint64_t kReportFirstPass = 0x48445203U;
constexpr uint64_t kReportCopy = 0x48445204U;
constexpr uint64_t kReportMeter = 0x48445205U;
constexpr uint64_t kReportLocal = 0x48445206U;

constexpr float kLargest = 65504.0F;         // the largest 16-bit float
constexpr float kBeyondWhite = 2.0F * kLargest; // (an infinity read back)
constexpr float kWhiteMargin = 0.5F / 255.0F; // the white point shown within half a step of white
constexpr float kLeastWhite = 1.0F;
constexpr float kMostWhite = 8.0F;
constexpr float kMostStops = 16.0F;
constexpr float kMostSaturation = 4.0F;
constexpr float kLeastContrast = 0.1F;
constexpr float kMostContrast = 4.0F;
constexpr float kMidGrey = 0.5F;
constexpr int kBisections = 60;
constexpr uint32_t kLeastLut = 2;
constexpr uint32_t kMostLut = 128;
constexpr std::array<float, 3> kLuma{0.2126F, 0.7152F, 0.0722F}; // Rec. 709
// Narkowicz's ACES fit: x (a x + b) / (x (c x + d) + e)
constexpr float kAcesA = 2.51F;
constexpr float kAcesB = 0.03F;
constexpr float kAcesC = 2.43F;
constexpr float kAcesD = 0.59F;
constexpr float kAcesE = 0.14F;
// (round EX) ACES's reference rendering and sRGB output transforms in Stephen Hill's fit (vulkan_hdr.h, rt_tonemap=2):
// Rec. 709 -> AP1 with the RRT's saturation, the fitted curve, AP1 -> Rec. 709 (rows)
using Matrix3 = std::array<std::array<float, 3>, 3>;
constexpr Matrix3 kAcesInput{{{0.59719F, 0.35458F, 0.04823F}, {0.07600F, 0.90834F, 0.01566F}, {0.02840F, 0.13383F, 0.83777F}}};
constexpr Matrix3 kAcesOutput{
    {{1.60475F, -0.53108F, -0.07367F}, {-0.10208F, 1.10813F, -0.00605F}, {-0.00327F, -0.07276F, 1.07602F}}};
constexpr float kHillA = 0.0245786F;
constexpr float kHillB = 0.000090537F;
constexpr float kHillC = 0.983729F;
constexpr float kHillD = 0.4329510F;
constexpr float kHillE = 0.238081F;
constexpr uint32_t kCurveShoulder = 0;
constexpr uint32_t kCurveAcesNarkowicz = 1;
constexpr uint32_t kCurveAces = 2;
// (round EX) the local exposure's passes: the grid's group (16 x 16 invocations over a tile), the blur's (64)
constexpr uint32_t kLocalGroupSide = 16;
constexpr uint32_t kLocalBlurGroup = 64;
constexpr float kLocalLeastWeight = 1e-4F; // (a grid read with less weight than this: the blurred luminance alone)
constexpr float kLocalFloor = 1e-30F;
constexpr std::size_t kHdrPixelBytes = 8;
constexpr std::size_t kBgraPixelBytes = 4;
constexpr float kByteMax = 255.0F;

// The full-screen triangle (three vertices, no buffers).
constexpr const char* kVertexSource = R"(#version 450
void main()
{
    vec2 at = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(at * 2.0 - 1.0, 0.0, 1.0);
}
)";

// What the fragment shader and the meter (both read the viewport's copy) share: the bindings, the push constants.
// MULTISAMPLED is defined before it for a multisampled target, METER_WRITES for the meter.
constexpr const char* kPassCommon = R"(
#ifdef MULTISAMPLED
layout(set = 0, binding = 0) uniform sampler2DMS u_colour;
#else
layout(set = 0, binding = 0) uniform sampler2D u_colour;
#endif
layout(std430, set = 0, binding = 1) readonly buffer Lut { vec4 entries[]; } u_lut;
// (added by the low sun's glare) the meter's state per viewport: x the exposure's factor now (linear light), y its
// stops now, z the view's metered light, w the stops it is heading for
#ifdef METER_WRITES
layout(std430, set = 0, binding = 2) buffer Meter { vec4 slots[]; } u_meter;
#else
layout(std430, set = 0, binding = 2) readonly buffer Meter { vec4 slots[]; } u_meter;
#endif
layout(push_constant) uniform Pass {
    vec4 grade;      // x: the exposure's factor; y: saturation; z: contrast; w: the curve (0, 1)
    vec4 shape;      // x: the knee; y: 1 - the knee; z: ACES's scale; w: samples
    vec4 lut;        // x: the table's size (0: none)
    vec4 lut_min;    // the domain's low corner
    vec4 lut_scale;  // 1 / (the domain's high corner - low corner)
    vec4 meter;      // x: the viewport's slot; y: 1 metered; z: the seconds since its last pass (0: start there); w:
                     // the most stops down (rt_exposure_adapt)
    vec4 meter_rect; // the viewport in the copy's pixels: x, y, width, height
    vec4 night;      // (round S) x: the frame's pre-exposure (rt_daylight.h frame_pre_exposure); (path tracing) y: the
                     // manual exposure's factor on the physical light (0: none); (round EX) z: local exposure - 0 off,
                     // 1 about the meter's key, 2 about the fixed key (no meter); w: the grid's tiles across
} pc;
// (round EX) the local exposure's grid (vulkan_hdr.h "Local exposure"), vec2s: the cells (log2 sum, count) - tile by
// tile, LOCAL_SLICES each -, the blurred cells, each tile's whole (log2 sum, count), each tile's blurred luminance
// (weighted log2 sum, weight)
#ifdef LOCAL_WRITES
layout(std430, set = 0, binding = 3) buffer Local { vec2 cells[]; } u_local;
#else
layout(std430, set = 0, binding = 3) readonly buffer Local { vec2 cells[]; } u_local;
#endif

const float kLargest = 65504.0;
const vec3 kLuma = vec3(0.2126, 0.7152, 0.0722);

float finite(float c)
{
    return c >= 0.0 ? min(c, kLargest) : 0.0; // (not a number fails the comparison: 0)
}

vec3 finite3(vec3 c)
{
    return vec3(finite(c.r), finite(c.g), finite(c.b));
}

// The exposure's factor on the frame's light: rt_exposure's x the camera's - the manual exposure's (path tracing,
// stage 1), the meter's slot (round S: the physical light's exposure over the frame's pre-exposure), or without either
// the pre-exposure divided out.
float exposure_factor()
{
    return pc.grade.x * (pc.night.y > 0.0 ? pc.night.y / pc.night.x
                                          : (pc.meter.y > 0.5 ? u_meter.slots[int(pc.meter.x)].x : 1.0 / pc.night.x));
}

int local_tiles_x()
{
    return int(pc.night.w);
}

int local_tiles_y()
{
    return (int(pc.meter_rect.w) + LOCAL_TILE - 1) / LOCAL_TILE;
}

// An exposed luminance's place in the grid's slices (LOCAL_FIXED a slice)
uint local_place(float y)
{
    float z = clamp(log2(max(y, LOCAL_FLOOR)) - LOCAL_LOWEST, 0.0, float(LOCAL_SLICES) - 1.0 / LOCAL_FIXED);
    return uint(z * LOCAL_FIXED);
}
)";

// The fragment shader (vulkan_hdr.h's curve; tone_map_reference is its twin): reads the viewport's copy, writes the
// colour target's rgb.
constexpr const char* kFragmentBody = R"(
layout(location = 0) out vec4 o_colour;

float shoulder(float c)
{
    if (c <= pc.shape.x) {
        return c;
    }
    if (pc.shape.y <= 0.0) {
        return 1.0;
    }
    return 1.0 - pc.shape.y * exp(-(c - pc.shape.x) / pc.shape.y);
}

vec3 aces(vec3 x)
{
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

// (round EX) ACES's reference rendering and sRGB output transforms, Hill's fit (rt_tonemap=2): linear Rec. 709 in and out
vec3 aces_fitted(vec3 x)
{
    vec3 v = vec3(dot(vec3(0.59719, 0.35458, 0.04823), x), dot(vec3(0.07600, 0.90834, 0.01566), x),
                  dot(vec3(0.02840, 0.13383, 0.83777), x));
    v = (v * (v + 0.0245786) - 0.000090537) / (v * (0.983729 * v + 0.4329510) + 0.238081);
    vec3 y = vec3(dot(vec3(1.60475, -0.53108, -0.07367), v), dot(vec3(-0.10208, 1.10813, -0.00605), v),
                  dot(vec3(-0.00327, -0.07276, 1.07602), v));
    return clamp(y, 0.0, 1.0);
}

// (round EX) The local exposure's factor (vulkan_hdr.h "Local exposure"; local_exposure_reference is its twin) for an
// exposed luminance `y` at `p` (the pixel's centre in the viewport).
float local_factor(vec2 p, float y)
{
    int nx = local_tiles_x();
    int ny = local_tiles_y();
    int tiles = nx * ny;
    int cells = tiles * LOCAL_SLICES;
    vec2 t = clamp(p / float(LOCAL_TILE) - 0.5, vec2(0.0), vec2(float(nx - 1), float(ny - 1)));
    ivec2 t0 = ivec2(t);
    ivec2 t1 = min(t0 + 1, ivec2(nx - 1, ny - 1));
    vec2 f = t - vec2(t0);
    float z = float(local_place(y)) / LOCAL_FIXED - 0.5;
    z = clamp(z, 0.0, float(LOCAL_SLICES - 1));
    int z0 = int(z);
    int z1 = min(z0 + 1, LOCAL_SLICES - 1);
    float fz = z - float(z0);
    vec2 grid = vec2(0.0);
    vec2 blur = vec2(0.0);
    for (int k = 0; k < 4; ++k) {
        int tx = (k & 1) != 0 ? t1.x : t0.x;
        int ty = (k & 2) != 0 ? t1.y : t0.y;
        float w = ((k & 1) != 0 ? f.x : 1.0 - f.x) * ((k & 2) != 0 ? f.y : 1.0 - f.y);
        int tile = ty * nx + tx;
        grid += w * mix(u_local.cells[cells + tile * LOCAL_SLICES + z0], u_local.cells[cells + tile * LOCAL_SLICES + z1], fz);
        blur += w * u_local.cells[2 * cells + tiles + tile];
    }
    float own = LOCAL_LOWEST + float(local_place(y)) / LOCAL_FIXED;
    float blurred = blur.y > 0.0 ? blur.x / blur.y : own;
    float bilateral = grid.y > LOCAL_LEAST_WEIGHT ? grid.x / grid.y : blurred;
    float base = mix(bilateral, blurred, LOCAL_BLEND);
    float key = pc.night.z > 1.5 ? METER_KEY : u_meter.slots[3 * METER_SLOTS + int(pc.meter.x)].x;
    float pivot = log2(key * pc.grade.x);
    float contrast = base > pivot ? LOCAL_HIGHLIGHT : LOCAL_SHADOW;
    return exp2((base - pivot) * (contrast - 1.0));
}

vec3 lut_entry(ivec3 i)
{
    int n = int(pc.lut.x);
    return u_lut.entries[i.x + n * (i.y + n * i.z)].rgb;
}

vec3 graded(vec3 c)
{
    int n = int(pc.lut.x);
    vec3 u = clamp((c - pc.lut_min.xyz) * pc.lut_scale.xyz, 0.0, 1.0) * float(n - 1);
    ivec3 i = min(ivec3(u), ivec3(n - 2));
    vec3 f = u - vec3(i);
    vec3 c00 = mix(lut_entry(i), lut_entry(i + ivec3(1, 0, 0)), f.x);
    vec3 c10 = mix(lut_entry(i + ivec3(0, 1, 0)), lut_entry(i + ivec3(1, 1, 0)), f.x);
    vec3 c01 = mix(lut_entry(i + ivec3(0, 0, 1)), lut_entry(i + ivec3(1, 0, 1)), f.x);
    vec3 c11 = mix(lut_entry(i + ivec3(0, 1, 1)), lut_entry(i + ivec3(1, 1, 1)), f.x);
    return mix(mix(c00, c10, f.y), mix(c01, c11, f.y), f.z);
}

// (round F: the frame is linear light) exposed in linear light, then encoded for the display once - the display's
// preferences (saturation, contrast, the curve, the grade) act on the encoded colour
vec3 tone(vec3 c, vec2 p)
{
    c = finite3(c) * exposure_factor();
    // (round EX) local exposure, on the exposed light
    if (pc.night.z > 0.5) {
        c *= local_factor(p, dot(c, kLuma));
    }
    c = display_from_linear(c);
    if (pc.grade.y != 1.0) {
        float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
        c = max(vec3(luma) + (c - vec3(luma)) * pc.grade.y, vec3(0.0));
    }
    if (pc.grade.z != 1.0) {
        c = 0.5 * pow(c / 0.5, vec3(pc.grade.z));
    }
    if (pc.grade.w < 0.5) {
        c = vec3(shoulder(c.r), shoulder(c.g), shoulder(c.b));
    } else if (pc.grade.w < 1.5) {
        c = display_from_linear(min(aces(linear_from_display(c)) * pc.shape.z, vec3(1.0)));
    } else {
        c = display_from_linear(aces_fitted(linear_from_display(c)));
    }
    if (pc.lut.x >= 2.0) {
        c = graded(c);
    }
    return clamp(c, 0.0, 1.0);
}

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec2 inside = gl_FragCoord.xy - pc.meter_rect.xy; // (the pixel's centre in the viewport)
#ifdef MULTISAMPLED
    int samples = int(pc.shape.w);
    vec3 sum = vec3(0.0);
    for (int i = 0; i < samples; ++i) {
        sum += tone(texelFetch(u_colour, p, i).rgb, inside);
    }
    o_colour = vec4(sum / float(samples), 1.0);
#else
    o_colour = vec4(tone(texelFetch(u_colour, p, 0).rgb, inside), 1.0);
#endif
}
)";

// (added by the low sun's glare; changed by the physical air; round EX: the histogram) The meter (vulkan_hdr.h "The
// metering"; meter_count / meter_window_mean / meter_key / meter_target_stops / meter_adapt are its twins): one group of
// METER_SIDE x METER_SIDE invocations, each METER_EACH x METER_EACH of the grid's samples (sample 0 of a multisampled
// copy), counted centre-weighted into the histogram of their physical log2 luminance; the first invocation averages
// the window between the percentiles and moves the viewport's slot toward its target.
constexpr const char* kMeterBody = R"(
layout(local_size_x = METER_SIDE, local_size_y = METER_SIDE) in;
shared float s_over[METER_SIDE * METER_SIDE];
shared float s_brightest[METER_SIDE * METER_SIDE];
shared float s_negative[METER_SIDE * METER_SIDE];
// the histogram: each bin's weight and its samples' weighted places inside it (fixed point)
shared uint s_weight[METER_BINS];
shared uint s_offset[METER_BINS];

// The luminance (cd/m^2) a share `wanted` of the histogram's weight is under (its bin's middle).
float percentile(float wanted)
{
    float counted = 0.0;
    for (int b = 0; b < METER_BINS; ++b) {
        counted += float(s_weight[b]);
        if (counted >= wanted && s_weight[b] > 0u) {
            return exp2(float(METER_LOWEST_STOP) + (float(b) + 0.5) / float(METER_BINS_PER_STOP));
        }
    }
    return 0.0;
}

void main()
{
    uint id = gl_LocalInvocationIndex;
    for (uint b = id; b < uint(METER_BINS); b += uint(METER_SIDE * METER_SIDE)) {
        s_weight[b] = 0u;
        s_offset[b] = 0u;
    }
    barrier();
    float grid = float(METER_SIDE * METER_EACH);
    float over = 0.0;
    float negative = 0.0;
    float brightest = 0.0;
    for (int j = 0; j < METER_EACH; ++j) {
        for (int i = 0; i < METER_EACH; ++i) {
            vec2 uv = (vec2(gl_LocalInvocationID.xy * uint(METER_EACH) + uvec2(i, j)) + 0.5) / grid;
            ivec2 p = ivec2(pc.meter_rect.xy) + ivec2(uv * pc.meter_rect.zw);
            vec3 c = texelFetch(u_colour, p, 0).rgb;
            // (round S2 - the readout) a sample at the frame format's largest or beyond (an overflow to infinity) or not
            // a number, and one with a negative channel: counted; the brightest finite one kept
            float most = max(c.r, max(c.g, c.b));
            over += most < kLargest ? 0.0 : 1.0;
            negative += min(c.r, min(c.g, c.b)) < 0.0 ? 1.0 : 0.0;
            c = finite3(c);
            float luma = dot(c, kLuma);
            brightest = max(brightest, luma);
            // the sample's physical luminance (cd/m^2) counted in its bin with its centre weight
            float physical = luma / pc.night.x * METER_WHITE;
            float stop = physical > 0.0 ? log2(physical) : float(METER_LOWEST_STOP);
            float place = max((stop - float(METER_LOWEST_STOP)) * float(METER_BINS_PER_STOP), 0.0);
            int bin = min(int(place), METER_BINS - 1);
            float within = clamp(place - float(bin), 0.0, 1.0 - 1.0 / METER_OFFSET_SCALE);
            vec2 d = uv - 0.5;
            float w = exp(-dot(d, d) / (2.0 * METER_CENTRE * METER_CENTRE));
            uint weight = uint(w * METER_WEIGHT_SCALE + 0.5);
            atomicAdd(s_weight[bin], weight);
            atomicAdd(s_offset[bin], uint(within * METER_OFFSET_SCALE) * weight);
        }
    }
    s_over[id] = over;
    s_brightest[id] = brightest;
    s_negative[id] = negative;
    barrier();
    for (uint half_count = uint(METER_SIDE * METER_SIDE) / 2u; half_count > 0u; half_count >>= 1u) {
        if (id < half_count) {
            s_over[id] += s_over[id + half_count];
            s_brightest[id] = max(s_brightest[id], s_brightest[id + half_count]);
            s_negative[id] += s_negative[id + half_count];
        }
        barrier();
    }
    if (id == 0u) {
        // the mean log2 luminance between the percentiles (Unreal's histogram average: each bin's weight inside the
        // window, at its samples' mean place)
        float total = 0.0;
        for (int b = 0; b < METER_BINS; ++b) {
            total += float(s_weight[b]);
        }
        float low = total * METER_LOW_PERCENT;
        float high = total * METER_HIGH_PERCENT;
        float sum = 0.0;
        float summed = 0.0;
        for (int b = 0; b < METER_BINS; ++b) {
            if (s_weight[b] == 0u) {
                continue;
            }
            float v = float(s_weight[b]);
            float below = min(v, low);
            v -= below;
            low -= below;
            high -= below;
            v = min(v, high);
            high -= v;
            float at = float(METER_LOWEST_STOP) +
                       (float(b) + float(s_offset[b]) / (float(s_weight[b]) * METER_OFFSET_SCALE)) / float(METER_BINS_PER_STOP);
            sum += v * at;
            summed += v;
        }
        float metered = summed > 0.0 ? exp2(sum / summed) : exp2(float(METER_LOWEST_STOP));
        // (round S) the metered light in the unit (the physical light); (round L) a camera's exposure for it, within the
        // range its settings reach (CAMERA_LEAST_STOPS - CAMERA_MOST_STOPS)
        float mean = metered / METER_WHITE;
        float seen = max(mean, 1e-30);
        // the key: middle grey x the compensation curve (Krawczyk's scene key over daylight's: the absolute luminance)
        float adapted = 1.03 - 2.0 / (2.0 + log(seen * METER_WHITE + 1.0) / log(10.0));
        float key = METER_KEY * adapted / METER_DAY_ADAPTED;
        float target = clamp(clamp(log2(key / seen), CAMERA_LEAST_STOPS, CAMERA_MOST_STOPS), -pc.meter.w, pc.meter.w);
        int slot = int(pc.meter.x);
        float now = target;
        if (pc.meter.z > 0.0) {
            float was = u_meter.slots[slot].y;
            float seconds = target < was ? METER_DARKEN : METER_BRIGHTEN;
            now = was + (target - was) * (1.0 - exp(-pc.meter.z / seconds));
        }
        u_meter.slots[slot] = vec4(exp2(now) / pc.night.x, now, mean, target);
        // (round S2 - the readout, after the slots) the samples over the format's range or not a number, the brightest
        // (pre-exposed), the pre-exposure metered with, the samples with a negative channel
        u_meter.slots[METER_SLOTS + slot] = vec4(s_over[0], s_brightest[0], pc.night.x, s_negative[0]);
        // (the readout's luminance) the weighted median, 95th and 99.5th percentiles (cd/m^2: each bin's middle) and the
        // EV100 the camera stands at (round L)
        u_meter.slots[2 * METER_SLOTS + slot] =
            vec4(percentile(0.5 * total), percentile(0.95 * total), percentile(0.995 * total), CAMERA_EV_AT_ZERO - now);
        // (round EX) the key (the local exposure's pivot) and the window's ends
        u_meter.slots[3 * METER_SLOTS + slot] =
            vec4(key, percentile(METER_LOW_PERCENT * total), percentile(METER_HIGH_PERCENT * total), metered);
    }
}
)";

// (round EX) The local exposure's grid (vulkan_hdr.h "Local exposure"; local_exposure_reference is its twin): a group
// per tile, LOCAL_GROUP x LOCAL_GROUP invocations each a square of its pixels (sample 0 of a multisampled copy), their
// exposed log2 luminance counted into the tile's slices (fixed point); the cells and the tile's whole written.
constexpr const char* kLocalGridBody = R"(
layout(local_size_x = LOCAL_GROUP, local_size_y = LOCAL_GROUP) in;
shared uint s_count[LOCAL_SLICES];
shared uint s_sum[LOCAL_SLICES];

void main()
{
    uint id = gl_LocalInvocationIndex;
    if (id < uint(LOCAL_SLICES)) {
        s_count[id] = 0u;
        s_sum[id] = 0u;
    }
    barrier();
    float factor = exposure_factor();
    const int each = LOCAL_TILE / LOCAL_GROUP;
    ivec2 origin = ivec2(gl_WorkGroupID.xy) * LOCAL_TILE + ivec2(gl_LocalInvocationID.xy) * each;
    for (int j = 0; j < each; ++j) {
        for (int i = 0; i < each; ++i) {
            ivec2 q = origin + ivec2(i, j);
            if (q.x >= int(pc.meter_rect.z) || q.y >= int(pc.meter_rect.w)) {
                continue;
            }
            vec3 c = finite3(texelFetch(u_colour, ivec2(pc.meter_rect.xy) + q, 0).rgb);
            uint place = local_place(dot(c, kLuma) * factor);
            uint slice = place / uint(LOCAL_FIXED);
            atomicAdd(s_count[slice], 1u);
            atomicAdd(s_sum[slice], place);
        }
    }
    barrier();
    int nx = local_tiles_x();
    int cells = nx * local_tiles_y() * LOCAL_SLICES;
    int tile = int(gl_WorkGroupID.y) * nx + int(gl_WorkGroupID.x);
    if (id < uint(LOCAL_SLICES)) {
        u_local.cells[tile * LOCAL_SLICES + int(id)] =
            vec2(float(s_sum[id]) / LOCAL_FIXED + LOCAL_LOWEST * float(s_count[id]), float(s_count[id]));
    }
    if (id == 0u) {
        uint count = 0u;
        uint sum = 0u;
        for (int k = 0; k < LOCAL_SLICES; ++k) {
            count += s_count[k];
            sum += s_sum[k];
        }
        u_local.cells[2 * cells + tile] = vec2(float(sum) / LOCAL_FIXED + LOCAL_LOWEST * float(count), float(count));
    }
}
)";

// (round EX) The local exposure's blur: an invocation per cell (the 3 x 3 x 3 binomial over its neighbours, unnormalised:
// the read divides) and per tile (the blurred luminance: the tiles' wholes under a Gaussian of the viewport's width x
// LOCAL_KERNEL / 4 sigma).
constexpr const char* kLocalBlurBody = R"(
layout(local_size_x = LOCAL_BLUR_GROUP) in;

void main()
{
    int nx = local_tiles_x();
    int ny = local_tiles_y();
    int tiles = nx * ny;
    int cells = tiles * LOCAL_SLICES;
    int g = int(gl_GlobalInvocationID.x);
    if (g < cells) {
        int z = g % LOCAL_SLICES;
        int tile = g / LOCAL_SLICES;
        int tx = tile % nx;
        int ty = tile / nx;
        vec2 sum = vec2(0.0);
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    int x = tx + dx;
                    int y = ty + dy;
                    int s = z + dz;
                    if (x < 0 || x >= nx || y < 0 || y >= ny || s < 0 || s >= LOCAL_SLICES) {
                        continue;
                    }
                    float w = float((2 - abs(dx)) * (2 - abs(dy)) * (2 - abs(dz)));
                    sum += w * u_local.cells[(y * nx + x) * LOCAL_SLICES + s];
                }
            }
        }
        u_local.cells[cells + g] = sum;
    } else if (g < cells + tiles) {
        int tile = g - cells;
        int tx = tile % nx;
        int ty = tile / nx;
        float sigma = LOCAL_KERNEL * 0.25 * pc.meter_rect.z / float(LOCAL_TILE);
        vec2 sum = vec2(0.0);
        for (int s = 0; s < tiles; ++s) {
            vec2 d = vec2(float(s % nx - tx), float(s / nx - ty));
            sum += exp(-dot(d, d) / (2.0 * sigma * sigma)) * u_local.cells[2 * cells + s];
        }
        u_local.cells[2 * cells + tiles + tile] = sum;
    }
}
)";

// The push constants, as the shaders' block lays them out.
struct PassConstants {
    std::array<float, 4> grade{};
    std::array<float, 4> shape{};
    std::array<float, 4> lut{};
    std::array<float, 4> lut_min{};
    std::array<float, 4> lut_scale{};
    std::array<float, 4> meter{};      // (added by the low sun's glare)
    std::array<float, 4> meter_rect{};
    std::array<float, 4> night{}; // (round S) the pre-exposure, the unit's luminance (rod vision)
};
constexpr std::size_t kPassConstantBytes = 128;
static_assert(sizeof(PassConstants) == kPassConstantBytes, "the shader's push-constant block");
constexpr VkShaderStageFlags kPassStages = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

struct PassPipeline {
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkPipeline pipeline = VK_NULL_HANDLE;
};

// (added by the low sun's glare) A viewport's meter: its slot in the meter's buffer, kept by its rectangle and its
// target's size (split screen's views each their own), the least recently used given up past kMeterSlots.
struct MeterSlot {
    std::array<uint32_t, 4> rect{};
    Extent target{};
    uint64_t last_use = 0;
    bool started = false;
    std::chrono::steady_clock::time_point last_pass;
    uint64_t cut = 0; // (round S2) the lighting's cut it adapted in (rt_daylight.h DayLight::cut)
};

struct HdrState {
    HdrSettings settings;
    ToneParameters parameters;
    GradeLut lut;
    bool lut_changed = false; // (the table's buffer made again at the next pass)
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkShaderModule vertex = VK_NULL_HANDLE;
    std::vector<PassPipeline> pipelines;
    Image copy;     // the viewport's colour copied out for the pass to read
    Image resolved; // resolve_for_copy's
    DeviceBuffer lut_buffer;
    // (added by the low sun's glare) the meter: its pipelines (single-sampled, multisampled), its buffer (kMeterSlots
    // vec4s), the viewports' slots
    std::array<VkPipeline, 2> meter_pipelines{};
    DeviceBuffer meter_buffer;
    std::array<MeterSlot, kMeterSlots> meter_slots{};
    uint64_t meter_uses = 0;
    // (round EX) the local exposure: its pipelines (the grid, the blur; single-sampled, multisampled) and its buffer
    // (the grid's cells, made larger when a viewport needs more)
    std::array<VkPipeline, 4> local_pipelines{};
    DeviceBuffer local_buffer;
    // (round S2) the meter's readout: its buffer copied each pass into memory the processor reads (a frame or two
    // behind), logged while the pre-exposure changes and for kReadoutSeconds after (log_readout)
    DeviceBuffer meter_read;
    float readout_pre = 0.0F;
    uint64_t readout_cut = 0; // (the lighting's cut the readout window last opened for - a tod / moon / weather set)
    double readout_last = -1.0;
    std::chrono::steady_clock::time_point readout_until;
    std::chrono::steady_clock::time_point readout_logged;
    bool failed = false;
    uint64_t passes = 0;
    uint64_t upscales_seen = 0; // (round F) the upscaler's passes counted at the last pass
    // (path tracing, stage 1) the last metered slot (the readout's: last_metered_ev100), -1 none
    int last_slot = -1;
};

// (path tracing, stage 1 - P5) The manual exposure (set_manual_ev100): any thread.
struct ManualState {
    std::mutex mutex;
    ManualExposure exposure;
};
ManualState& manual_state()
{
    static ManualState made;
    return made;
}

HdrState& hdr()
{
    static HdrState state;
    return state;
}

bool first_report(Backend& state, uint64_t what) { return state.reported.insert(what).second; }

// The listener rt_scene tells when a view's world is drawn (kToneMapping: after every light, the upscaler, the camera
// and the lens - before the art styles and the grade, which take the display's colours). (Round F) It also opens the
// view's linear phase: from view_begins to its pass the Gpu's draws into the float target write linear light
// (Backend::linear_phase, ff_shaders' decode).
class ToneListener final : public rt::ViewListener {
public:
    [[nodiscard]] int order() const override { return rt::ViewOrder::kToneMapping; }
    void opaque_drawn(const rt::ViewDrawn& /*view*/) override {}
    void view_begins(const rt::ViewDrawn& /*view*/) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        state.linear_phase = state.device_made && rt::hdr_colour();
    }
    void world_drawn(const rt::ViewDrawn& view) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (state.device_made && state.draws_made) {
            record_tone_map_pass(state, view);
        }
        state.linear_phase = false; // (the 2D after it: the display's colours)
    }
};

ToneListener& listener()
{
    static ToneListener instance;
    return instance;
}

float aces_curve(float x) { return std::clamp((x * ((kAcesA * x) + kAcesB)) / ((x * ((kAcesC * x) + kAcesD)) + kAcesE), 0.0F, 1.0F); }

// What is left below white at the white point for a shoulder with room `room` (1 - the knee).
double left_at_white(double room, double white)
{
    return room * std::exp(-1.0 - ((white - 1.0) / room));
}

float finite(float value)
{
    return value >= 0.0F ? std::min(value, kLargest) : 0.0F; // (not a number fails the comparison: 0)
}

float shoulder(const ToneParameters& parameters, float value)
{
    if (value <= parameters.knee) {
        return value;
    }
    if (parameters.room <= 0.0F) {
        return 1.0F;
    }
    return 1.0F - (parameters.room * std::exp(-(value - parameters.knee) / parameters.room));
}

using Rgb = std::array<float, 3>;
using Index3 = std::array<uint32_t, 3>;

// A matrix's rows on a colour.
Rgb times(const Matrix3& matrix, const Rgb& colour)
{
    Rgb out{};
    for (std::size_t row = 0; row < out.size(); ++row) {
        const std::array<float, 3>& m = core::at(matrix, row);
        core::at(out, row) = (core::at(m, 0) * core::at(colour, 0)) + (core::at(m, 1) * core::at(colour, 1)) +
                             (core::at(m, 2) * core::at(colour, 2));
    }
    return out;
}

// (round EX) ACES's reference rendering and sRGB output transforms, Hill's fit (the shader's aces_fitted): linear
// Rec. 709 in and out, clamped to 0 - 1.
Rgb aces_fitted(const Rgb& colour)
{
    Rgb v = times(kAcesInput, colour);
    for (float& x : v) {
        x = ((x * (x + kHillA)) - kHillB) / ((x * ((kHillC * x) + kHillD)) + kHillE);
    }
    Rgb out = times(kAcesOutput, v);
    for (float& x : out) {
        x = std::clamp(x, 0.0F, 1.0F);
    }
    return out;
}

// The table's entry at (red, green, blue).
Rgb lut_entry(const GradeLut& lut, const Index3& at)
{
    const std::size_t side = lut.size;
    return core::at(lut.entries, core::at(at, 0) + (side * (core::at(at, 1) + (side * core::at(at, 2)))));
}

// From one colour toward another by `part`.
Rgb mix(const Rgb& from, const Rgb& to, float part)
{
    Rgb mixed{};
    for (std::size_t channel = 0; channel < mixed.size(); ++channel) {
        core::at(mixed, channel) =
            core::at(from, channel) + ((core::at(to, channel) - core::at(from, channel)) * part);
    }
    return mixed;
}

Rgb graded(const GradeLut& lut, const Rgb& colour)
{
    Index3 low{};
    Rgb fraction{};
    for (std::size_t channel = 0; channel < colour.size(); ++channel) {
        const float span = core::at(lut.domain_max, channel) - core::at(lut.domain_min, channel);
        const float scale = span != 0.0F ? 1.0F / span : 0.0F;
        const float place =
            std::clamp((core::at(colour, channel) - core::at(lut.domain_min, channel)) * scale, 0.0F, 1.0F) *
            static_cast<float>(lut.size - 1);
        const uint32_t below = std::min(static_cast<uint32_t>(place), lut.size - 2);
        core::at(low, channel) = below;
        core::at(fraction, channel) = place - static_cast<float>(below);
    }
    const auto entry = [&](const Index3& step) {
        return lut_entry(lut, {core::at(low, 0) + core::at(step, 0), core::at(low, 1) + core::at(step, 1),
                               core::at(low, 2) + core::at(step, 2)});
    };
    const float along_red = core::at(fraction, 0);
    const Rgb near_near = mix(entry({0, 0, 0}), entry({1, 0, 0}), along_red);
    const Rgb far_near = mix(entry({0, 1, 0}), entry({1, 1, 0}), along_red);
    const Rgb near_far = mix(entry({0, 0, 1}), entry({1, 0, 1}), along_red);
    const Rgb far_far = mix(entry({0, 1, 1}), entry({1, 1, 1}), along_red);
    return mix(mix(near_near, far_near, core::at(fraction, 1)), mix(near_far, far_far, core::at(fraction, 1)),
               core::at(fraction, 2));
}

// A whole file's text (empty when it can't be read).
std::string read_text_file(const std::string& path)
{
    std::string text;
    fs::DiskStream* const in = fs::disk().open_stream(path.c_str(), "rb", fs::StreamSharing::kAll);
    if (in == nullptr) {
        return text;
    }
    if (fs::disk().stream_seek(in, 0, SEEK_END) == 0) {
        const long size = fs::disk().stream_tell(in);
        if (size > 0 && fs::disk().stream_seek(in, 0, SEEK_SET) == 0) {
            text.resize(static_cast<std::size_t>(size));
            text.resize(fs::disk().stream_read(in, text.data(), 1, text.size()));
        }
    }
    fs::disk().stream_close(in);
    return text;
}

// --- the pass's objects -----------------------------------------------------------------------------------------

VkShaderModule make_module(VkDevice device, ShaderStage stage, const std::string& source, std::string* error)
{
    const CompiledShader compiled = compile_glsl(stage, source);
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

// The layouts, the sampler and the vertex shader (once).
bool ensure_layouts(Backend& state, std::string* error)
{
    HdrState& own = hdr();
    if (own.layout != VK_NULL_HANDLE) {
        return true;
    }
    const VkDevice device = state.context.device();
    const std::array<VkDescriptorSetLayoutBinding, 4> bindings{{
        {.binding = 0,
         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         .descriptorCount = 1,
         .stageFlags = kPassStages,
         .pImmutableSamplers = nullptr},
        {.binding = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = 1,
         .stageFlags = kPassStages,
         .pImmutableSamplers = nullptr},
        {.binding = 2,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = 1,
         .stageFlags = kPassStages,
         .pImmutableSamplers = nullptr},
        {.binding = 3, // (round EX) the local exposure's grid
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = 1,
         .stageFlags = kPassStages,
         .pImmutableSamplers = nullptr},
    }};
    const VkDescriptorSetLayoutCreateInfo set_info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                   .pNext = nullptr,
                                                   .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
                                                   .bindingCount = static_cast<uint32_t>(bindings.size()),
                                                   .pBindings = bindings.data()};
    const VkPushConstantRange constants{.stageFlags = kPassStages, .offset = 0, .size = sizeof(PassConstants)};
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
    own.vertex = make_module(device, ShaderStage::kVertex, kVertexSource, error);
    return own.vertex != VK_NULL_HANDLE;
}

// The constants every shader of the pass is made with (the meter's, the local exposure's - defined with the meter).
std::string pass_defines();

// The pipeline for a colour target's format and samples (made on first use): no blending, colour written, alpha kept.

VkPipeline pass_pipeline(Backend& state, VkFormat format, VkSampleCountFlagBits samples, std::string* error)
{
    HdrState& own = hdr();
    for (const PassPipeline& made : own.pipelines) {
        if (made.format == format && made.samples == samples) {
            return made.pipeline;
        }
    }
    if (!ensure_layouts(state, error)) {
        return VK_NULL_HANDLE;
    }
    const VkDevice device = state.context.device();
    std::string source = "#version 450\n";
    if (samples != VK_SAMPLE_COUNT_1_BIT) {
        source += "#define MULTISAMPLED\n";
    }
    source += pass_defines();
    source += kPassCommon;
    source += rt::kDisplayTransferGlsl;
    source += kFragmentBody;
    const VkShaderModule fragment = make_module(device, ShaderStage::kFragment, source, error);
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
    const VkResult made = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
    vkDestroyShaderModule(device, fragment, nullptr);
    if (made != VK_SUCCESS) {
        *error = "vkCreateGraphicsPipelines failed";
        return VK_NULL_HANDLE;
    }
    own.pipelines.push_back({.format = format, .samples = samples, .pipeline = pipeline});
    return pipeline;
}

// (added by the low sun's glare) The viewport's copy moved to `layout`, with a barrier from what it was used for - the
// copy's write, or both shaders' reads (the curve's fragment shader, the meter's compute shader) - to what it will be.
void copy_barrier(VkCommandBuffer commands, Image* copy, VkImageLayout layout)
{
    constexpr VkPipelineStageFlags kReaders = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    VkPipelineStageFlags source_stages = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkAccessFlags source_access = VK_ACCESS_MEMORY_WRITE_BIT;
    if (copy->layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        source_stages = VK_PIPELINE_STAGE_TRANSFER_BIT;
        source_access = VK_ACCESS_TRANSFER_WRITE_BIT;
    } else if (copy->layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        source_stages = kReaders;
        source_access = 0;
    }
    const bool to_read = layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    const VkImageMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = source_access,
        .dstAccessMask = to_read ? VkAccessFlags{VK_ACCESS_SHADER_READ_BIT} : VkAccessFlags{VK_ACCESS_TRANSFER_WRITE_BIT},
        .oldLayout = copy->layout,
        .newLayout = layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = copy->image,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .baseMipLevel = 0,
                             .levelCount = copy->levels,
                             .baseArrayLayer = 0,
                             .layerCount = 1}};
    vkCmdPipelineBarrier(commands, source_stages, to_read ? kReaders : VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);
    copy->layout = layout;
}

// (added by the physical air) the eye's adaptation (Krawczyk, Myszkowski and Seidel 2005: the key 1.03 - 2 / (2 +
// log10(L + 1)), L the absolute luminance - linear 1 the white diffuser under the reference day: the sun's illuminance
// at the top over the unit's factor - rt_sky.h sky_exposure) and daylight's average (an 18% world under that day)
constexpr double kDaylightAverage = 0.18;
double meter_white() { return kSolarIlluminance / rt::sky_exposure(); }
// (round L) the camera's EV100 at 0 stops: the luminance (cd/m^2) the factor 1 puts at kMeterKey, x 100 / K (12.5)
double camera_ev_at_zero() { return std::log2(static_cast<double>(kMeterKey) * meter_white() * (100.0 / 12.5)); }
double adaptation_at(double mean)
{
    return 1.03 - (2.0 / (2.0 + std::log10((std::max(mean, 0.0) * meter_white()) + 1.0)));
}

// A number as GLSL reads it (a float literal).
std::string glsl_real(float value)
{
    std::array<char, 32> text{};
    std::snprintf(text.data(), text.size(), "%.9g", static_cast<double>(value));
    std::string out(text.data());
    if (out.find_first_of(".e") == std::string::npos) {
        out += ".0";
    }
    return out;
}

std::string pass_defines()
{
    std::string source;
    const auto define = [&source](const char* name, const std::string& value) {
        source += std::string("#define ") + name + " " + value + "\n";
    };
    define("METER_SIDE", std::to_string(kMeterSide));
    define("METER_EACH", std::to_string(kMeterEach));
    define("METER_KEY", glsl_real(kMeterKey));
    define("METER_WHITE", glsl_real(static_cast<float>(meter_white())));
    define("METER_DAY_ADAPTED", glsl_real(static_cast<float>(adaptation_at(kDaylightAverage))));
    const std::array<float, 2> camera = camera_stops_range();
    define("CAMERA_LEAST_STOPS", glsl_real(std::get<0>(camera)));
    define("CAMERA_MOST_STOPS", glsl_real(std::get<1>(camera)));
    define("CAMERA_EV_AT_ZERO", glsl_real(static_cast<float>(camera_ev_at_zero())));
    define("METER_SLOTS", std::to_string(kMeterSlots));
    define("METER_BINS", std::to_string(kMeterBins));
    define("METER_BINS_PER_STOP", std::to_string(kMeterBinsPerStop));
    define("METER_LOWEST_STOP", std::to_string(kMeterLowestStop));
    define("METER_LOW_PERCENT", glsl_real(kMeterLowPercent));
    define("METER_HIGH_PERCENT", glsl_real(kMeterHighPercent));
    define("METER_WEIGHT_SCALE", glsl_real(kMeterWeightScale));
    define("METER_OFFSET_SCALE", glsl_real(kMeterOffsetScale));
    define("METER_CENTRE", glsl_real(kMeterCentre));
    define("METER_DARKEN", glsl_real(kMeterDarkenSeconds));
    define("METER_BRIGHTEN", glsl_real(kMeterBrightenSeconds));
    define("LOCAL_TILE", std::to_string(kLocalTile));
    define("LOCAL_SLICES", std::to_string(kLocalSlices));
    define("LOCAL_GROUP", std::to_string(kLocalGroupSide));
    define("LOCAL_BLUR_GROUP", std::to_string(kLocalBlurGroup));
    define("LOCAL_LOWEST", glsl_real(kLocalLowestStop));
    define("LOCAL_FIXED", glsl_real(kLocalFixed));
    define("LOCAL_FLOOR", glsl_real(kLocalFloor));
    define("LOCAL_LEAST_WEIGHT", glsl_real(kLocalLeastWeight));
    define("LOCAL_BLEND", glsl_real(kLocalBlurredBlend));
    define("LOCAL_KERNEL", glsl_real(kLocalBlurredKernel));
    define("LOCAL_HIGHLIGHT", glsl_real(kLocalHighlightContrast));
    define("LOCAL_SHADOW", glsl_real(kLocalShadowContrast));
    return source;
}

// The pass's compute shaders (round EX: the meter, the local exposure's grid and blur).
enum class ComputeKind : uint8_t { kMeter, kLocalGrid, kLocalBlur };

// A compute pipeline of the pass for a single-sampled or multisampled copy (made on first use; the layouts are the
// curve's).
VkPipeline compute_pipeline(Backend& state, ComputeKind kind, bool multisampled, std::string* error)
{
    HdrState& own = hdr();
    const std::size_t variant = multisampled ? 1 : 0;
    VkPipeline& made = kind == ComputeKind::kMeter ? core::at(own.meter_pipelines, variant)
                                                   : core::at(own.local_pipelines,
                                                              (kind == ComputeKind::kLocalBlur ? 2 : 0) + variant);
    if (made != VK_NULL_HANDLE) {
        return made;
    }
    if (!ensure_layouts(state, error)) {
        return VK_NULL_HANDLE;
    }
    const VkDevice device = state.context.device();
    std::string source = "#version 450\n";
    source += kind == ComputeKind::kMeter ? "#define METER_WRITES\n" : "#define LOCAL_WRITES\n";
    if (multisampled) {
        source += "#define MULTISAMPLED\n";
    }
    source += pass_defines();
    source += kPassCommon;
    switch (kind) {
    case ComputeKind::kMeter:
        source += kMeterBody;
        break;
    case ComputeKind::kLocalGrid:
        source += kLocalGridBody;
        break;
    case ComputeKind::kLocalBlur:
        source += kLocalBlurBody;
        break;
    }
    const VkShaderModule module = make_module(device, ShaderStage::kCompute, source, error);
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
                                           .layout = own.layout,
                                           .basePipelineHandle = VK_NULL_HANDLE,
                                           .basePipelineIndex = -1};
    const VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &made);
    vkDestroyShaderModule(device, module, nullptr);
    if (result != VK_SUCCESS) {
        made = VK_NULL_HANDLE;
        *error = "vkCreateComputePipelines failed";
    }
    return made;
}

// (round EX) The local exposure's grid for a viewport `width` x `height`: its tiles across and down.
std::array<uint32_t, 2> local_tiles(uint32_t width, uint32_t height)
{
    return {(width + kLocalTile - 1) / kLocalTile, (height + kLocalTile - 1) / kLocalTile};
}

// (round EX) The local exposure's buffer, at least `vec2s` of them (one when off: the descriptor needs a buffer), made
// again larger when a viewport needs more.
bool ensure_local_buffer(Backend& state, std::size_t vec2s)
{
    HdrState& own = hdr();
    constexpr std::size_t kVec2Bytes = 2 * sizeof(float);
    const auto bytes = static_cast<VkDeviceSize>(std::max<std::size_t>(vec2s, 1) * kVec2Bytes);
    if (own.local_buffer.buffer != VK_NULL_HANDLE && own.local_buffer.bytes >= bytes) {
        return true;
    }
    if (own.local_buffer.buffer != VK_NULL_HANDLE) {
        retire_buffer(state, &own.local_buffer);
    }
    return state.memory.create_buffer(bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, BufferPlace::kDevice,
                                      &own.local_buffer);
}

// The meter's buffer (kMeterSlots vec4s; device memory - written and read only by the passes, read back by the check).
bool ensure_meter_buffer(Backend& state)
{
    HdrState& own = hdr();
    if (own.meter_buffer.buffer != VK_NULL_HANDLE) {
        return true;
    }
    constexpr std::size_t kSlotBytes = 4 * sizeof(float);
    own.meter_slots = {};
    // (the slots, then each slot's readout - round S2 -, then each slot's luminance percentiles, then - round EX - each
    // slot's key and window)
    constexpr auto kBytes = static_cast<VkDeviceSize>(4 * kMeterSlots * kSlotBytes);
    return state.memory.create_buffer(kBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                      BufferPlace::kDevice, &own.meter_buffer) &&
           state.memory.create_buffer(kBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, BufferPlace::kReadBack,
                                      &own.meter_read);
}

// The slot of the viewport `rect` of a target `target` large (the least recently used one given up for a new one).
std::size_t meter_slot_of(const std::array<uint32_t, 4>& rect, const Extent& target)
{
    HdrState& own = hdr();
    ++own.meter_uses;
    std::size_t oldest = 0;
    for (std::size_t i = 0; i < own.meter_slots.size(); ++i) {
        MeterSlot& slot = core::at(own.meter_slots, i);
        if (slot.started && slot.rect == rect && slot.target.width == target.width &&
            slot.target.height == target.height) {
            slot.last_use = own.meter_uses;
            return i;
        }
        if (slot.last_use < core::at(own.meter_slots, oldest).last_use) {
            oldest = i;
        }
    }
    MeterSlot& fresh = core::at(own.meter_slots, oldest);
    fresh = MeterSlot{};
    fresh.rect = rect;
    fresh.target = target;
    fresh.last_use = own.meter_uses;
    return oldest;
}

// The meter recorded over the viewport's copy (in SHADER_READ_ONLY): its slot moved toward the view's target, ready for
// the curve's pass (a barrier from its write to the fragment shader's read).
void record_meter(VkCommandBuffer commands, VkPipeline pipeline, std::size_t slot_index, PassConstants* constants)
{
    HdrState& own = hdr();
    MeterSlot& slot = core::at(own.meter_slots, slot_index);
    const auto now = std::chrono::steady_clock::now();
    // (a slot not drawn for a while - a menu over another view, a level loading - starts again at its target)
    constexpr double kLongestGap = 0.5;
    double seconds = 0.0;
    if (slot.started) {
        seconds = std::chrono::duration<double>(now - slot.last_pass).count();
        if (!(seconds > 0.0) || seconds > kLongestGap) {
            seconds = 0.0;
        }
    }
    // (round S2) a lighting cut (a time, a moon, a weather set: rt_daylight.h DayLight::cut) - the eye's state is from
    // another lighting, not a moment before this one: it starts again at its target, and follows the picture without
    // adapting while the world's light is still being made for the cut (cut_settling) - as the passes' histories start
    // again at a camera cut (vulkan_rt_history.h). The clock running is no cut: the eye adapts to it as it goes.
    const uint64_t cut = rt::day_light().cut;
    if (cut != slot.cut || rt::cut_settling()) {
        seconds = 0.0;
        slot.cut = cut;
    }
    slot.started = true;
    slot.last_pass = now;
    constants->meter = {static_cast<float>(slot_index), 1.0F, static_cast<float>(seconds),
                        own.settings.exposure_adapt > 0.0F ? kMeterMostStops : 0.0F};
    const VkBufferMemoryBarrier before{.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                                       .pNext = nullptr,
                                       .srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                                       .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                                       .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                       .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                       .buffer = own.meter_buffer.buffer,
                                       .offset = 0,
                                       .size = VK_WHOLE_SIZE};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &before, 0, nullptr);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdPushConstants(commands, own.layout, kPassStages, 0, sizeof(PassConstants), constants);
    vkCmdDispatch(commands, 1, 1, 1);
    VkBufferMemoryBarrier after = before;
    after.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    after.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                         nullptr, 1, &after, 0, nullptr);
    // (round S2) the readout's copy for the processor
    if (own.meter_read.buffer != VK_NULL_HANDLE) {
        VkBufferMemoryBarrier to_copy = before;
        to_copy.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        to_copy.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                             nullptr, 1, &to_copy, 0, nullptr);
        const VkBufferCopy region{.srcOffset = 0, .dstOffset = 0, .size = own.meter_read.bytes};
        vkCmdCopyBuffer(commands, own.meter_buffer.buffer, own.meter_read.buffer, 1, &region);
        VkBufferMemoryBarrier to_host = before;
        to_host.buffer = own.meter_read.buffer;
        to_host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        to_host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1,
                             &to_host, 0, nullptr);
    }
}

// (round EX) The local exposure's passes over the viewport's copy - the grid (a group per tile), then its blur and the
// blurred luminance - after the meter (they read its factor and key), ready for the curve's pass. The compute
// descriptors are pushed.
void record_local(VkCommandBuffer commands, const std::array<VkPipeline, 2>& pipelines, const PassConstants& constants,
                  const std::array<uint32_t, 2>& tiles)
{
    const HdrState& own = hdr();
    VkMemoryBarrier barrier{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                            .pNext = nullptr,
                            .srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    // (the meter's slot written; the grid of a viewport before read by its curve's pass)
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, std::get<0>(pipelines));
    vkCmdPushConstants(commands, own.layout, kPassStages, 0, sizeof(PassConstants), &constants);
    vkCmdDispatch(commands, std::get<0>(tiles), std::get<1>(tiles), 1);
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                         &barrier, 0, nullptr, 0, nullptr);
    const uint32_t count = std::get<0>(tiles) * std::get<1>(tiles);
    const uint32_t invocations = (count * kLocalSlices) + count;
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, std::get<1>(pipelines));
    vkCmdPushConstants(commands, own.layout, kPassStages, 0, sizeof(PassConstants), &constants);
    vkCmdDispatch(commands, (invocations + kLocalBlurGroup - 1) / kLocalBlurGroup, 1, 1);
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1,
                         &barrier, 0, nullptr, 0, nullptr);
}

// (round S2) The meter's readout in the log: the frame's pre-exposure, the eye's exposure (stops, and the factor on the
// pre-exposed light), the metered physical light and its target, the brightest sample and the samples over the
// format's range - from the last pass's copy the processor can read (a frame or two behind: the readout of a pass
// finished) - and the luminance it measures (the samples' median, 95th and 99.5th percentiles and brightest in cd/m^2,
// and the camera's EV100 - round L). Logged when the pre-exposure or the lighting's cut changes and every
// kReadoutEvery for kReadoutSeconds after, and whenever a sample is over the range (at most every kOverEvery).
// (round PX) The camera's exposure from the meter's readout (its slot's stops: the factor on the physical light), told to
// the frame's pre-exposure (rt_daylight.h set_camera_exposure - UE5's PreExposure: the frame's light written about
// where it is shown). A slot not metered yet (its factor 0) tells nothing.
void follow_camera(Backend& state, std::size_t slot_index)
{
    HdrState& own = hdr();
    if (own.meter_read.buffer == VK_NULL_HANDLE || own.meter_read.mapped == nullptr) {
        return;
    }
    std::array<float, 4> values{};
    vmaInvalidateAllocation(state.memory.allocator(), own.meter_read.allocation, 0, VK_WHOLE_SIZE);
    const std::span<const uint8_t> mapped(static_cast<const uint8_t*>(own.meter_read.mapped),
                                          static_cast<std::size_t>(own.meter_read.bytes));
    const std::size_t slot_bytes = values.size() * sizeof(float);
    if ((slot_index + 1) * slot_bytes > mapped.size()) {
        return;
    }
    std::memcpy(values.data(), mapped.subspan(slot_index * slot_bytes, slot_bytes).data(), slot_bytes);
    const float factor = core::at(values, 0);
    const float stops = core::at(values, 1);
    if (factor > 0.0F && std::isfinite(stops)) {
        rt::set_camera_exposure(std::exp2(stops));
    }
}

void log_readout(Backend& state, std::size_t slot_index, float pre)
{
    HdrState& own = hdr();
    constexpr double kReadoutSeconds = 8.0;
    constexpr double kReadoutEvery = 0.5;
    if (own.meter_read.buffer == VK_NULL_HANDLE || own.meter_read.mapped == nullptr) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    const uint64_t cut = rt::day_light().cut;
    // (round PX: the pre-exposure follows the camera - a window opens when it has moved a stop since the last one)
    const bool moved = !(own.readout_pre > 0.0F) || std::abs(std::log2(pre / own.readout_pre)) >= 1.0F;
    if (moved || cut != own.readout_cut) {
        own.readout_cut = cut;
        if (own.readout_pre > 0.0F && moved) {
            get_log("vulkan: rt_hdr: the pre-exposure %.6g -> %.6g", static_cast<double>(own.readout_pre),
                    static_cast<double>(pre));
        }
        own.readout_pre = pre;
        own.readout_until = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                      std::chrono::duration<double>(kReadoutSeconds));
    }
    if (std::chrono::duration<double>(now - own.readout_logged).count() < kReadoutEvery) {
        return;
    }
    constexpr std::size_t kVec4 = 4;
    std::array<float, 4 * kVec4> values{};
    vmaInvalidateAllocation(state.memory.allocator(), own.meter_read.allocation, 0, VK_WHOLE_SIZE);
    const std::span<const uint8_t> mapped(static_cast<const uint8_t*>(own.meter_read.mapped),
                                          static_cast<std::size_t>(own.meter_read.bytes));
    const std::size_t slot_bytes = kVec4 * sizeof(float);
    std::memcpy(values.data(), mapped.subspan(slot_index * slot_bytes, slot_bytes).data(), slot_bytes);
    std::memcpy(&core::at(values, kVec4), mapped.subspan((kMeterSlots + slot_index) * slot_bytes, slot_bytes).data(),
                slot_bytes);
    std::memcpy(&core::at(values, 2 * kVec4),
                mapped.subspan(((2 * kMeterSlots) + slot_index) * slot_bytes, slot_bytes).data(), slot_bytes);
    std::memcpy(&core::at(values, 3 * kVec4),
                mapped.subspan(((3 * kMeterSlots) + slot_index) * slot_bytes, slot_bytes).data(), slot_bytes);
    const float over = core::at(values, 4);
    constexpr double kOverEvery = 5.0; // (outside the window: a sample over the range logged at most this often)
    if (now >= own.readout_until &&
        (!(over > 0.0F) || std::chrono::duration<double>(now - own.readout_logged).count() < kOverEvery)) {
        return;
    }
    own.readout_logged = now;
    get_log("vulkan: rt_hdr: meter: pre-exposure %.6g (metered with %.6g), camera %+.2f stops heading for %+.2f (x %.4g on "
            "the frame), metered %.4g cd/m^2, the brightest sample %.5g; of %u samples %.0f over the frame's range or not "
            "a number, %.0f negative",
            static_cast<double>(pre), static_cast<double>(core::at(values, 6)), static_cast<double>(core::at(values, 1)),
            static_cast<double>(core::at(values, 3)), static_cast<double>(core::at(values, 0)),
            static_cast<double>(core::at(values, 2)) * meter_white(), static_cast<double>(core::at(values, 5)),
            kMeterGrid * kMeterGrid, static_cast<double>(over), static_cast<double>(core::at(values, 7)));
    // (the luminance: what the scene's parts measure; round L: the camera's exposure value)
    const double brightest = static_cast<double>(core::at(values, 5)) / std::max(static_cast<double>(pre), 1e-30) *
                             meter_white();
    get_log("vulkan: rt_hdr: luminance: median %.3g, 95th %.3g, 99.5th %.3g, brightest %.3g cd/m^2; the camera at EV100 "
            "%.2f (its range %.1f - %.1f)",
            static_cast<double>(core::at(values, 8)), static_cast<double>(core::at(values, 9)),
            static_cast<double>(core::at(values, 10)), brightest, static_cast<double>(core::at(values, 11)),
            static_cast<double>(kCameraLeastEv100), static_cast<double>(kCameraMostEv100));
    // (round EX) the histogram's window and the key it was put at; the local exposure
    get_log("vulkan: rt_hdr: histogram: %.0f%% - %.0f%% window %.3g - %.3g cd/m^2, its mean %.3g cd/m^2 at the key %.4g "
            "(middle grey %.2f x the compensation curve's %.3f); local exposure %s",
            static_cast<double>(kMeterLowPercent) * 100.0, static_cast<double>(kMeterHighPercent) * 100.0,
            static_cast<double>(core::at(values, 13)), static_cast<double>(core::at(values, 14)),
            static_cast<double>(core::at(values, 15)), static_cast<double>(core::at(values, 12)),
            static_cast<double>(kMeterKey), static_cast<double>(core::at(values, 12) / kMeterKey),
            own.settings.local_exposure ? "on" : "off");
}

// An image like `like` (format, size, samples) for `usage`, made again when any of them changed.
bool ensure_image_like(Backend& state, Image* image, const Image& like, VkImageUsageFlags usage)
{
    if (image->image != VK_NULL_HANDLE && image->format == like.format && image->samples == like.samples &&
        image->extent.width == like.extent.width && image->extent.height == like.extent.height) {
        return true;
    }
    retire_image(state, image);
    const ImageRequest request{.format = like.format,
                               .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                               .extent = like.extent,
                               .levels = 1,
                               .samples = like.samples,
                               .usage = usage,
                               .alpha_one = false};
    if (!state.memory.create_image(request, image)) {
        return false;
    }
    prepare_image(state, image);
    return true;
}

// The grade table's buffer (one entry when there is none: the descriptor needs a buffer), made again when it changed.
bool ensure_lut_buffer(Backend& state)
{
    HdrState& own = hdr();
    if (own.lut_buffer.buffer != VK_NULL_HANDLE && !own.lut_changed) {
        return true;
    }
    retire_buffer(state, &own.lut_buffer);
    own.lut_changed = false;
    const std::size_t entries = std::max<std::size_t>(own.lut.entries.size(), 1);
    constexpr std::size_t kEntryBytes = 4 * sizeof(float); // (std430 vec4)
    if (!state.memory.create_buffer(entries * kEntryBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, BufferPlace::kUpload,
                                    &own.lut_buffer)) {
        return false;
    }
    std::vector<float> values(entries * 4, 0.0F);
    for (std::size_t entry = 0; entry < own.lut.entries.size(); ++entry) {
        const Rgb& colour = core::at(own.lut.entries, entry);
        for (std::size_t channel = 0; channel < colour.size(); ++channel) {
            core::at(values, (entry * 4) + channel) = core::at(colour, channel);
        }
    }
    const std::span<uint8_t> into(static_cast<uint8_t*>(own.lut_buffer.mapped), entries * kEntryBytes);
    const auto from = std::as_bytes(std::span(values));
    std::ranges::transform(from, into.begin(), [](std::byte b) { return static_cast<uint8_t>(b); });
    state.memory.flush(own.lut_buffer, 0, entries * kEntryBytes);
    return true;
}

// The image a surface draws into (a texture's level 0, a render target's).
Image* colour_image_of(Surface* surface)
{
    if (surface == nullptr) {
        return nullptr;
    }
    switch (surface->kind) {
    case SurfaceKind::kTextureLevel:
        return surface->level == 0 ? &surface->texture->image : nullptr;
    case SurfaceKind::kRenderTarget:
        return &surface->image;
    case SurfaceKind::kDepth:
    case SurfaceKind::kSystemMemory:
        break;
    }
    return nullptr;
}

// One 16-bit float's value.
float half_value(uint16_t bits)
{
    constexpr uint32_t kSignShift = 15;
    constexpr uint32_t kExponentShift = 10;
    constexpr uint32_t kExponentMask = 0x1F;
    constexpr uint32_t kMantissaMask = 0x3FF;
    constexpr uint32_t kMantissaUnit = 0x400;
    constexpr int kBias = 15;
    const uint32_t exponent = (bits >> kExponentShift) & kExponentMask;
    const uint32_t mantissa = bits & kMantissaMask;
    float value = 0.0F;
    if (exponent == 0) {
        value = std::ldexp(static_cast<float>(mantissa), 1 - kBias - static_cast<int>(kExponentShift));
    } else if (exponent == kExponentMask) {
        value = mantissa == 0 ? kBeyondWhite : 0.0F; // (infinity read as beyond white; not a number as 0)
    } else {
        value = std::ldexp(static_cast<float>(mantissa + kMantissaUnit),
                           static_cast<int>(exponent) - kBias - static_cast<int>(kExponentShift));
    }
    return (bits >> kSignShift) != 0 ? -value : value;
}

uint8_t to_byte(float value)
{
    return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0F, 1.0F) * kByteMax));
}

// Every 16-bit float's 8-bit value (to_byte of half_value), made on first use: the screenshot's read-back converts two
// million pixels on the drawing thread, and a table look-up is a fifth of the arithmetic.
const std::vector<uint8_t>& half_bytes()
{
    static core::NoDestroy<std::vector<uint8_t>> kTable([] {
        constexpr std::size_t kHalves = 0x10000;
        std::vector<uint8_t> table(kHalves);
        for (std::size_t bits = 0; bits < kHalves; ++bits) {
            core::at(table, bits) = to_byte(half_value(static_cast<uint16_t>(bits)));
        }
        return table;
    }());
    return kTable.get();
}

// A .cube line's words (split at spaces and tabs).
std::vector<std::string_view> cube_words(std::string_view line)
{
    std::vector<std::string_view> found;
    std::size_t at = 0;
    const auto blank = [](char letter) { return letter == ' ' || letter == '\t'; };
    while (at < line.size()) {
        while (at < line.size() && blank(line.at(at))) {
            ++at;
        }
        const std::size_t start = at;
        while (at < line.size() && !blank(line.at(at))) {
            ++at;
        }
        if (at > start) {
            found.push_back(line.substr(start, at - start));
        }
    }
    return found;
}

// A number word (all of it a number, finite).
bool cube_number(std::string_view word, float* value)
{
    const std::string copy(word);
    char* end = nullptr;
    const double parsed = std::strtod(copy.c_str(), &end);
    if (end == copy.c_str() || *end != '\0' || !std::isfinite(parsed)) {
        return false;
    }
    *value = static_cast<float>(parsed);
    return true;
}

// Three number words from `first` into `rgb`.
bool cube_triple(const std::vector<std::string_view>& parts, std::size_t first, Rgb* rgb)
{
    if (parts.size() != first + rgb->size()) {
        return false;
    }
    for (std::size_t channel = 0; channel < rgb->size(); ++channel) {
        if (!cube_number(core::at(parts, first + channel), &core::at(*rgb, channel))) {
            return false;
        }
    }
    return true;
}

// One line of a .cube file (not empty) into the table; false (and why, unless it isn't three numbers) when it's wrong.
bool read_cube_line(const std::vector<std::string_view>& parts, GradeLut* made, std::string* error)
{
    const std::string_view key = parts.front();
    if (key.starts_with('#') || key == "TITLE") {
        return true;
    }
    if (key == "LUT_1D_SIZE") {
        *error = "a 1D table (only LUT_3D_SIZE is read)";
        return false;
    }
    if (key == "LUT_3D_SIZE") {
        float size = 0.0F;
        if (parts.size() != 2 || !cube_number(parts.back(), &size) || size < static_cast<float>(kLeastLut) ||
            size > static_cast<float>(kMostLut) || size != std::floor(size)) {
            *error = "LUT_3D_SIZE isn't 2 - 128";
            return false;
        }
        made->size = static_cast<uint32_t>(size);
        return true;
    }
    if (key == "DOMAIN_MIN" || key == "DOMAIN_MAX") {
        if (!cube_triple(parts, 1, key == "DOMAIN_MIN" ? &made->domain_min : &made->domain_max)) {
            *error = "a DOMAIN line without three numbers";
            return false;
        }
        return true;
    }
    Rgb entry{};
    if (!cube_triple(parts, 0, &entry)) {
        return false;
    }
    made->entries.push_back(entry);
    return true;
}

} // namespace

// --- settings ----------------------------------------------------------------------------------------------------

ToneParameters tone_parameters(const HdrSettings& settings)
{
    ToneParameters parameters;
    const float stops = std::isfinite(settings.exposure) ? std::clamp(settings.exposure, -kMostStops, kMostStops) : 0.0F;
    parameters.scale = static_cast<float>(std::exp2(static_cast<double>(stops))); // (linear light: round F)
    parameters.saturation =
        std::isfinite(settings.saturation) ? std::clamp(settings.saturation, 0.0F, kMostSaturation) : 1.0F;
    parameters.contrast =
        std::isfinite(settings.contrast) ? std::clamp(settings.contrast, kLeastContrast, kMostContrast) : 1.0F;
    parameters.curve = settings.tonemap <= kCurveAces ? settings.tonemap : kCurveShoulder;
    const double white = std::isfinite(settings.white_point)
                             ? std::clamp(static_cast<double>(settings.white_point), 1.0, static_cast<double>(kMostWhite))
                             : static_cast<double>(kHdrWhitePoint);
    // the shoulder's room: the largest that still shows the white point within the margin (what is left below white
    // there grows with the room), found by halving; 0 at a white point of 1
    double room = 0.0;
    if (white > static_cast<double>(kLeastWhite)) {
        double low = 0.0;
        double high = 1.0;
        if (left_at_white(high, white) <= static_cast<double>(kWhiteMargin)) {
            low = high;
        } else {
            for (int i = 0; i < kBisections; ++i) {
                const double middle = (low + high) * 0.5;
                (left_at_white(middle, white) <= static_cast<double>(kWhiteMargin) ? low : high) = middle;
            }
        }
        room = low;
    }
    parameters.room = static_cast<float>(room);
    parameters.knee = static_cast<float>(1.0 - room);
    parameters.aces_scale = 1.0F / aces_curve(static_cast<float>(rt::linear_from_display(white)));
    return parameters;
}

void set_hdr_settings(const HdrSettings& settings)
{
    GradeLut lut;
    // (a split comparison's swap between its sides with the same grading table: the table kept, not read again -
    // rt_scene.h settings_swap)
    if (rt::settings_swap()) {
        const std::scoped_lock lock(backend().mutex);
        HdrState& own = hdr();
        if (own.settings.grade_lut == settings.grade_lut) {
            own.settings = settings;
            own.parameters = tone_parameters(settings);
            if (settings.on) {
                rt::add_view_listener(&listener());
            } else {
                rt::remove_view_listener(&listener());
            }
            return;
        }
    }
    if (!settings.grade_lut.empty()) {
        const std::string text = read_text_file(settings.grade_lut);
        std::string error;
        if (text.empty()) {
            get_log("vulkan: rt_grade_lut: %s can't be read - no grading table", settings.grade_lut.c_str());
        } else if (!parse_cube(text, &lut, &error)) {
            get_log("vulkan: rt_grade_lut: %s: %s - no grading table", settings.grade_lut.c_str(), error.c_str());
            lut = GradeLut{};
        } else {
            get_log("vulkan: rt_grade_lut: %s (%u x %u x %u)", settings.grade_lut.c_str(), lut.size, lut.size,
                    lut.size);
        }
    }
    {
        const std::scoped_lock lock(backend().mutex);
        HdrState& own = hdr();
        own.settings = settings;
        own.parameters = tone_parameters(settings);
        own.lut = std::move(lut);
        own.lut_changed = true;
    }
    if (settings.on) {
        rt::add_view_listener(&listener());
    } else {
        rt::remove_view_listener(&listener());
    }
}

void set_hdr_lut(GradeLut lut)
{
    const std::scoped_lock lock(backend().mutex);
    HdrState& own = hdr();
    own.lut = std::move(lut);
    own.lut_changed = true;
}

HdrSettings hdr_settings()
{
    const std::scoped_lock lock(backend().mutex);
    return hdr().settings;
}

bool parse_cube(std::string_view text, GradeLut* lut, std::string* error)
{
    GradeLut made;
    std::size_t at = 0;
    int line_number = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view line = text.substr(at, end - at);
        at = end + 1;
        ++line_number;
        if (line.ends_with('\r')) {
            line.remove_suffix(1);
        }
        const std::vector<std::string_view> parts = cube_words(line);
        if (!parts.empty() && !read_cube_line(parts, &made, error)) {
            if (error->empty()) {
                *error = "line " + std::to_string(line_number) + " isn't three numbers";
            }
            return false;
        }
    }
    if (made.size == 0) {
        *error = "no LUT_3D_SIZE";
        return false;
    }
    const std::size_t wanted = static_cast<std::size_t>(made.size) * made.size * made.size;
    if (made.entries.size() != wanted) {
        *error = std::to_string(made.entries.size()) + " entries, not " + std::to_string(wanted);
        return false;
    }
    for (std::size_t channel = 0; channel < made.domain_min.size(); ++channel) {
        if (!(core::at(made.domain_max, channel) > core::at(made.domain_min, channel))) {
            *error = "DOMAIN_MAX not above DOMAIN_MIN";
            return false;
        }
    }
    *lut = std::move(made);
    return true;
}

std::array<float, 3> tone_map_reference(const ToneParameters& parameters, const GradeLut* lut,
                                        std::array<float, 3> colour)
{
    for (float& value : colour) {
        value = finite(value);
    }
    for (float& value : colour) {
        value = rt::display_from_linear(value * parameters.scale); // (round F: linear in, encoded once)
    }
    if (parameters.saturation != 1.0F) {
        float luma = 0.0F;
        for (std::size_t channel = 0; channel < colour.size(); ++channel) {
            luma += core::at(colour, channel) * core::at(kLuma, channel);
        }
        for (float& value : colour) {
            value = std::max(luma + ((value - luma) * parameters.saturation), 0.0F);
        }
    }
    if (parameters.contrast != 1.0F) {
        for (float& value : colour) {
            value = kMidGrey * std::pow(value / kMidGrey, parameters.contrast);
        }
    }
    if (parameters.curve == kCurveAces) {
        Rgb linear{};
        for (std::size_t channel = 0; channel < colour.size(); ++channel) {
            core::at(linear, channel) = rt::linear_from_display(core::at(colour, channel));
        }
        const Rgb mapped = aces_fitted(linear);
        for (std::size_t channel = 0; channel < colour.size(); ++channel) {
            core::at(colour, channel) = rt::display_from_linear(core::at(mapped, channel));
        }
    } else {
        for (float& value : colour) {
            if (parameters.curve == kCurveShoulder) {
                value = shoulder(parameters, value);
            } else {
                const float mapped = std::min(aces_curve(rt::linear_from_display(value)) * parameters.aces_scale, 1.0F);
                value = rt::display_from_linear(mapped);
            }
        }
    }
    if (lut != nullptr && lut->size >= kLeastLut) {
        colour = graded(*lut, colour);
    }
    for (float& value : colour) {
        value = std::clamp(value, 0.0F, 1.0F);
    }
    return colour;
}

// --- the device ----------------------------------------------------------------------------------------------------

// --- (added by the low sun's glare) the metering: the meter's twins -------------------------------------------------


std::array<float, 2> meter_place(uint32_t i, uint32_t j)
{
    const auto grid = static_cast<float>(kMeterGrid);
    return {(static_cast<float>(i) + 0.5F) / grid, (static_cast<float>(j) + 0.5F) / grid};
}

float meter_weight(const std::array<float, 2>& place)
{
    const float u = core::at(place, 0) - 0.5F;
    const float v = core::at(place, 1) - 0.5F;
    return std::exp(-((u * u) + (v * v)) / (2.0F * kMeterCentre * kMeterCentre));
}

float meter_light(const std::array<float, 3>& colour)
{
    float light = 0.0F;
    for (std::size_t channel = 0; channel < colour.size(); ++channel) {
        light += core::at(kLuma, channel) * finite(core::at(colour, channel)); // (the frame is linear: round F)
    }
    return light;
}

void meter_count(MeterHistogram* histogram, float luminance, float weight)
{
    const auto lowest = static_cast<float>(kMeterLowestStop);
    const float stop = luminance > 0.0F ? std::log2(luminance) : lowest;
    const float place = std::max((stop - lowest) * static_cast<float>(kMeterBinsPerStop), 0.0F);
    const int bin = std::min(static_cast<int>(place), kMeterBins - 1);
    const float within = std::clamp(place - static_cast<float>(bin), 0.0F, 1.0F - (1.0F / kMeterOffsetScale));
    const auto counted = static_cast<uint32_t>((weight * kMeterWeightScale) + 0.5F);
    const auto at = static_cast<std::size_t>(bin);
    core::at(histogram->weight, at) += counted;
    core::at(histogram->offset, at) += static_cast<uint32_t>(within * kMeterOffsetScale) * counted;
}

float meter_window_mean(const MeterHistogram& histogram)
{
    float total = 0.0F;
    for (const uint32_t weight : histogram.weight) {
        total += static_cast<float>(weight);
    }
    float low = total * kMeterLowPercent;
    float high = total * kMeterHighPercent;
    float sum = 0.0F;
    float summed = 0.0F;
    for (std::size_t b = 0; b < histogram.weight.size(); ++b) {
        const uint32_t weight = core::at(histogram.weight, b);
        if (weight == 0) {
            continue;
        }
        float v = static_cast<float>(weight);
        const float below = std::min(v, low);
        v -= below;
        low -= below;
        high -= below;
        v = std::min(v, high);
        high -= v;
        const float at = static_cast<float>(kMeterLowestStop) +
                         ((static_cast<float>(b) + (static_cast<float>(core::at(histogram.offset, b)) /
                                                    (static_cast<float>(weight) * kMeterOffsetScale))) /
                          static_cast<float>(kMeterBinsPerStop));
        sum += v * at;
        summed += v;
    }
    return summed > 0.0F ? std::exp2(sum / summed) : std::exp2(static_cast<float>(kMeterLowestStop));
}

// (round EX) the local exposure's twin
namespace {

// An exposed luminance's place in the grid's slices (the shader's local_place).
uint32_t local_place(float luminance)
{
    const float z = std::clamp(std::log2(std::max(luminance, kLocalFloor)) - kLocalLowestStop, 0.0F,
                               static_cast<float>(kLocalSlices) - (1.0F / kLocalFixed));
    return static_cast<uint32_t>(z * kLocalFixed);
}

using Cell = std::array<float, 2>;

void add_cell(Cell* into, float weight, const Cell& cell)
{
    core::at(*into, 0) += weight * core::at(cell, 0);
    core::at(*into, 1) += weight * core::at(cell, 1);
}

} // namespace

std::vector<float> local_exposure_reference(uint32_t width, uint32_t height, const std::vector<float>& exposed,
                                            float pivot)
{
    const std::array<uint32_t, 2> counts = local_tiles(width, height);
    const auto nx = static_cast<int>(std::get<0>(counts));
    const auto ny = static_cast<int>(std::get<1>(counts));
    const auto slices = static_cast<int>(kLocalSlices);
    const int tiles = nx * ny;
    const int cells = tiles * slices;
    // the grid: each cell's count and fixed-point sum, then as the shader writes them
    std::vector<uint32_t> count(static_cast<std::size_t>(cells), 0);
    std::vector<uint32_t> sum(static_cast<std::size_t>(cells), 0);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t place = local_place(core::at(exposed, (static_cast<std::size_t>(y) * width) + x));
            const auto tile = static_cast<int>(((y / kLocalTile) * std::get<0>(counts)) + (x / kLocalTile));
            const auto at = static_cast<std::size_t>((tile * slices) + static_cast<int>(place / static_cast<uint32_t>(kLocalFixed)));
            ++core::at(count, at);
            core::at(sum, at) += place;
        }
    }
    std::vector<Cell> raw(static_cast<std::size_t>(cells));
    std::vector<Cell> whole(static_cast<std::size_t>(tiles));
    for (int tile = 0; tile < tiles; ++tile) {
        uint32_t tile_count = 0;
        uint32_t tile_sum = 0;
        for (int s = 0; s < slices; ++s) {
            const auto at = static_cast<std::size_t>((tile * slices) + s);
            core::at(raw, at) = {(static_cast<float>(core::at(sum, at)) / kLocalFixed) +
                                     (kLocalLowestStop * static_cast<float>(core::at(count, at))),
                                 static_cast<float>(core::at(count, at))};
            tile_count += core::at(count, at);
            tile_sum += core::at(sum, at);
        }
        core::at(whole, static_cast<std::size_t>(tile)) = {
            (static_cast<float>(tile_sum) / kLocalFixed) + (kLocalLowestStop * static_cast<float>(tile_count)),
            static_cast<float>(tile_count)};
    }
    // the blur: 3 x 3 x 3 binomial over the cells; the tiles' Gaussian
    std::vector<Cell> blurred(static_cast<std::size_t>(cells), Cell{});
    for (int g = 0; g < cells; ++g) {
        const int z = g % slices;
        const int tile = g / slices;
        const int tx = tile % nx;
        const int ty = tile / nx;
        Cell& out = core::at(blurred, static_cast<std::size_t>(g));
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int x = tx + dx;
                    const int y = ty + dy;
                    const int s = z + dz;
                    if (x < 0 || x >= nx || y < 0 || y >= ny || s < 0 || s >= slices) {
                        continue;
                    }
                    const auto w = static_cast<float>((2 - std::abs(dx)) * (2 - std::abs(dy)) * (2 - std::abs(dz)));
                    add_cell(&out, w, core::at(raw, static_cast<std::size_t>((((y * nx) + x) * slices) + s)));
                }
            }
        }
    }
    std::vector<Cell> wide(static_cast<std::size_t>(tiles), Cell{});
    const float sigma = kLocalBlurredKernel * 0.25F * static_cast<float>(width) / static_cast<float>(kLocalTile);
    for (int tile = 0; tile < tiles; ++tile) {
        const int tx = tile % nx;
        const int ty = tile / nx;
        for (int s = 0; s < tiles; ++s) {
            const auto dx = static_cast<float>((s % nx) - tx);
            const auto dy = static_cast<float>((s / nx) - ty);
            add_cell(&core::at(wide, static_cast<std::size_t>(tile)),
                     std::exp(-((dx * dx) + (dy * dy)) / (2.0F * sigma * sigma)), core::at(whole, static_cast<std::size_t>(s)));
        }
    }
    // each pixel: the grid read trilinearly at its own level, the blurred luminance bilinearly
    std::vector<float> factors(static_cast<std::size_t>(width) * height, 1.0F);
    const float log_pivot = std::log2(pivot);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const std::size_t pixel = (static_cast<std::size_t>(y) * width) + x;
            const float tx = std::clamp(((static_cast<float>(x) + 0.5F) / static_cast<float>(kLocalTile)) - 0.5F, 0.0F,
                                        static_cast<float>(nx - 1));
            const float ty = std::clamp(((static_cast<float>(y) + 0.5F) / static_cast<float>(kLocalTile)) - 0.5F, 0.0F,
                                        static_cast<float>(ny - 1));
            const int x0 = static_cast<int>(tx);
            const int y0 = static_cast<int>(ty);
            const int x1 = std::min(x0 + 1, nx - 1);
            const int y1 = std::min(y0 + 1, ny - 1);
            const float fx = tx - static_cast<float>(x0);
            const float fy = ty - static_cast<float>(y0);
            const uint32_t place = local_place(core::at(exposed, pixel));
            const float z = std::clamp((static_cast<float>(place) / kLocalFixed) - 0.5F, 0.0F, static_cast<float>(slices - 1));
            const int z0 = static_cast<int>(z);
            const int z1 = std::min(z0 + 1, slices - 1);
            const float fz = z - static_cast<float>(z0);
            Cell grid{};
            Cell blur{};
            for (int k = 0; k < 4; ++k) {
                const int cx = (k & 1) != 0 ? x1 : x0;
                const int cy = (k & 2) != 0 ? y1 : y0;
                const float w = ((k & 1) != 0 ? fx : 1.0F - fx) * ((k & 2) != 0 ? fy : 1.0F - fy);
                const int tile = (cy * nx) + cx;
                const Cell& low = core::at(blurred, static_cast<std::size_t>((tile * slices) + z0));
                const Cell& high = core::at(blurred, static_cast<std::size_t>((tile * slices) + z1));
                add_cell(&grid, w,
                         {core::at(low, 0) + ((core::at(high, 0) - core::at(low, 0)) * fz),
                          core::at(low, 1) + ((core::at(high, 1) - core::at(low, 1)) * fz)});
                add_cell(&blur, w, core::at(wide, static_cast<std::size_t>(tile)));
            }
            const float own_level = kLocalLowestStop + (static_cast<float>(place) / kLocalFixed);
            const float blurred_level = core::at(blur, 1) > 0.0F ? core::at(blur, 0) / core::at(blur, 1) : own_level;
            const float bilateral =
                core::at(grid, 1) > kLocalLeastWeight ? core::at(grid, 0) / core::at(grid, 1) : blurred_level;
            const float base = bilateral + ((blurred_level - bilateral) * kLocalBlurredBlend);
            const float contrast = base > log_pivot ? kLocalHighlightContrast : kLocalShadowContrast;
            core::at(factors, pixel) = std::exp2((base - log_pivot) * (contrast - 1.0F));
        }
    }
    return factors;
}

float meter_key(float mean)
{
    return kMeterKey * static_cast<float>(adaptation_at(static_cast<double>(mean)) / adaptation_at(kDaylightAverage));
}

float meter_target_stops(float mean, float most_stops)
{
    const float most = std::max(most_stops, 0.0F);
    const float seen = std::max(mean, 1e-30F);
    const std::array<float, 2> range = camera_stops_range(); // (round L: the camera's settings)
    const float stops = std::clamp(std::log2(meter_key(seen) / seen), core::at(range, 0), core::at(range, 1));
    return std::clamp(stops, -most, most);
}

float camera_ev100(float stops) { return static_cast<float>(camera_ev_at_zero()) - stops; }

std::array<float, 2> camera_stops_range()
{
    const auto zero = static_cast<float>(camera_ev_at_zero());
    return {zero - kCameraMostEv100, zero - kCameraLeastEv100};
}

float meter_adapt(float now_stops, float target_stops, float seconds)
{
    if (!(seconds > 0.0F)) {
        return target_stops;
    }
    const float over = target_stops < now_stops ? kMeterDarkenSeconds : kMeterBrightenSeconds;
    return now_stops + ((target_stops - now_stops) * (1.0F - std::exp(-seconds / over)));
}

void set_manual_ev100(bool on, float ev100)
{
    ManualState& own = manual_state();
    const std::scoped_lock lock(own.mutex);
    const ManualExposure wanted{.on = on, .ev100 = on ? ev100 : 0.0F};
    if (wanted.on != own.exposure.on || wanted.ev100 != own.exposure.ev100) {
        if (on) {
            get_log("vulkan: rt_hdr: the camera's exposure set by hand - EV100 %.2f (x %.6g on the physical light; the "
                    "meter off)",
                    static_cast<double>(ev100), static_cast<double>(exposure_at_ev100(ev100)));
        } else {
            get_log("vulkan: rt_hdr: the camera's exposure metered again");
        }
    }
    own.exposure = wanted;
}

ManualExposure manual_exposure()
{
    ManualState& own = manual_state();
    const std::scoped_lock lock(own.mutex);
    return own.exposure;
}

float exposure_at_ev100(float ev100) { return std::exp2(camera_ev100(0.0F) - ev100); }

ToneParameters tone_parameters_locked() { return hdr().parameters; }

const GradeLut* grade_lut_locked()
{
    const GradeLut& lut = hdr().lut;
    return lut.size >= kLeastLut ? &lut : nullptr;
}

bool last_metered_ev100(Backend& state, float* ev100)
{
    HdrState& own = hdr();
    if (own.last_slot < 0 || own.meter_read.buffer == VK_NULL_HANDLE || own.meter_read.mapped == nullptr) {
        return false;
    }
    constexpr std::size_t kSlotBytes = 4 * sizeof(float);
    std::array<float, 4> values{};
    vmaInvalidateAllocation(state.memory.allocator(), own.meter_read.allocation, 0, VK_WHOLE_SIZE);
    const std::span<const uint8_t> mapped(static_cast<const uint8_t*>(own.meter_read.mapped),
                                          static_cast<std::size_t>(own.meter_read.bytes));
    std::memcpy(values.data(), mapped.subspan(static_cast<std::size_t>(own.last_slot) * kSlotBytes, kSlotBytes).data(),
                kSlotBytes);
    const float stops = core::at(values, 1);
    if (!std::isfinite(stops) || !(core::at(values, 0) > 0.0F)) {
        return false;
    }
    *ev100 = camera_ev100(stops);
    return true;
}

bool read_meter(Backend& state, const std::array<uint32_t, 4>& rect, MeterReading* reading)
{
    const HdrState& own = hdr();
    if (own.meter_buffer.buffer == VK_NULL_HANDLE) {
        return false;
    }
    for (std::size_t i = 0; i < own.meter_slots.size(); ++i) {
        const MeterSlot& slot = core::at(own.meter_slots, i);
        if (!slot.started || slot.rect != rect) {
            continue;
        }
        constexpr std::size_t kSlotBytes = 4 * sizeof(float);
        std::vector<uint8_t> bytes;
        if (!state.memory.read_buffer(own.meter_buffer, i * kSlotBytes, kSlotBytes, &bytes) || bytes.size() < kSlotBytes) {
            return false;
        }
        std::array<float, 4> values{};
        std::memcpy(values.data(), bytes.data(), kSlotBytes);
        std::array<float, 4> luminance{};
        std::vector<uint8_t> more;
        if (state.memory.read_buffer(own.meter_buffer, ((2 * kMeterSlots) + i) * kSlotBytes, kSlotBytes, &more) &&
            more.size() >= kSlotBytes) {
            std::memcpy(luminance.data(), more.data(), kSlotBytes);
        }
        std::array<float, 4> keyed{};
        std::vector<uint8_t> key_bytes;
        if (state.memory.read_buffer(own.meter_buffer, ((3 * kMeterSlots) + i) * kSlotBytes, kSlotBytes, &key_bytes) &&
            key_bytes.size() >= kSlotBytes) {
            std::memcpy(keyed.data(), key_bytes.data(), kSlotBytes);
        }
        *reading = {.factor = core::at(values, 0),
                    .stops = core::at(values, 1),
                    .mean = core::at(values, 2),
                    .target = core::at(values, 3),
                    .luminance = luminance,
                    .key = core::at(keyed, 0)};
        return true;
    }
    return false;
}

bool hdr_wanted(const Backend& state)
{
    if (!hdr().settings.on) { // (the game's always on - round F's linear frame; a check may ask for the 8-bit frame)
        return false;
    }
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(state.context.physical_device(), kHdrFormat, &properties);
    constexpr VkFormatFeatureFlags kNeeded =
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
        VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
        VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if ((properties.optimalTilingFeatures & kNeeded) != kNeeded) {
        get_log("vulkan: this device can't draw into 16-bit float targets - the 8-bit display-referred frame kept");
        return false;
    }
    return true;
}

bool record_tone_map_pass(Backend& state, const rt::ViewDrawn& view)
{
    HdrState& own = hdr();
    if (!own.settings.on || own.failed) {
        return false;
    }
    Image* const colour = colour_image_of(core::at(state.targets, 0));
    if (colour == nullptr || colour->format != kHdrFormat || colour->attachment_view == VK_NULL_HANDLE) {
        if (first_report(state, kReportNotFloat)) {
            get_log("vulkan: rt_hdr: a view drawn into an 8-bit target - not tone mapped there (it is clipped as the "
                    "original's)");
        }
        return false;
    }
    std::string error;
    const VkPipeline pipeline = pass_pipeline(state, colour->format, colour->samples, &error);
    const bool copy_made = pipeline != VK_NULL_HANDLE &&
                           ensure_image_like(state, &own.copy, *colour,
                                             VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT) &&
                           ensure_lut_buffer(state) && ensure_meter_buffer(state);
    if (!copy_made) {
        own.failed = true;
        if (first_report(state, kReportPipeline)) {
            get_log("vulkan: rt_hdr: the tone-mapping pass couldn't be made (%s) - the frame shown clipped",
                    error.empty() ? "its images or buffer" : error.c_str());
        }
        return false;
    }
    // the viewport, inside the target - (round F: the pass runs after the upscaler) where it is shown once scaled up
    const bool upscaled = upscale_passes() != own.upscales_seen && view.output_rect != view.rect &&
                          core::at(view.output_rect, 2) != 0 && core::at(view.output_rect, 3) != 0;
    own.upscales_seen = upscale_passes();
    const std::array<uint32_t, 4>& shown = upscaled ? view.output_rect : view.rect;
    const uint32_t left = std::min(core::at(shown, 0), colour->extent.width);
    const uint32_t top = std::min(core::at(shown, 1), colour->extent.height);
    const uint32_t width = std::min(core::at(shown, 2), colour->extent.width - left);
    const uint32_t height = std::min(core::at(shown, 3), colour->extent.height - top);
    if (width == 0 || height == 0) {
        return false;
    }
    const ToneParameters& tone = own.parameters;
    PassConstants constants{};
    // (the user's exposure; the camera's own is the meter's - changed by the physical air: one controller)
    constants.grade = {tone.scale, tone.saturation, tone.contrast, static_cast<float>(tone.curve)};
    constants.shape = {tone.knee, tone.room, tone.aces_scale, static_cast<float>(colour->samples)};
    const GradeLut& lut = own.lut;
    if (lut.size >= kLeastLut) {
        constants.lut = {static_cast<float>(lut.size), 0.0F, 0.0F, 0.0F};
        for (std::size_t channel = 0; channel < lut.domain_min.size(); ++channel) {
            const float low = core::at(lut.domain_min, channel);
            core::at(constants.lut_min, channel) = low;
            core::at(constants.lut_scale, channel) = 1.0F / (core::at(lut.domain_max, channel) - low);
        }
    }
    // (added by the low sun's glare) the meter, when rt_exposure_adapt lets a bright view bring the exposure down
    VkPipeline meter = VK_NULL_HANDLE;
    const ManualExposure manual = manual_exposure(); // (path tracing, stage 1: the camera set by hand - no meter)
    const bool multisampled = colour->samples != VK_SAMPLE_COUNT_1_BIT;
    if (own.settings.exposure_adapt > 0.0F && !manual.on) {
        std::string meter_error;
        meter = compute_pipeline(state, ComputeKind::kMeter, multisampled, &meter_error);
        if (meter == VK_NULL_HANDLE && first_report(state, kReportMeter)) {
            get_log("vulkan: rt_hdr: the meter couldn't be made (%s) - the exposure isn't adapted to bright views",
                    meter_error.c_str());
        }
    }
    // (round EX) the local exposure (not while the camera is set by hand), its grid sized for the viewport
    const std::array<uint32_t, 2> tiles = local_tiles(width, height);
    const std::size_t tile_count = static_cast<std::size_t>(std::get<0>(tiles)) * std::get<1>(tiles);
    std::array<VkPipeline, 2> local{};
    if (own.settings.local_exposure && !manual.on) {
        std::string local_error;
        std::get<0>(local) = compute_pipeline(state, ComputeKind::kLocalGrid, multisampled, &local_error);
        std::get<1>(local) = compute_pipeline(state, ComputeKind::kLocalBlur, multisampled, &local_error);
        if ((std::get<0>(local) == VK_NULL_HANDLE || std::get<1>(local) == VK_NULL_HANDLE) &&
            first_report(state, kReportLocal)) {
            get_log("vulkan: rt_hdr: the local exposure couldn't be made (%s) - off", local_error.c_str());
        }
    }
    const bool local_on = std::get<0>(local) != VK_NULL_HANDLE && std::get<1>(local) != VK_NULL_HANDLE &&
                          ensure_local_buffer(state, (2 * tile_count * kLocalSlices) + (2 * tile_count));
    if (!local_on && !ensure_local_buffer(state, 1)) {
        return false;
    }
    constants.meter_rect = {static_cast<float>(left), static_cast<float>(top), static_cast<float>(width),
                            static_cast<float>(height)};
    // (round S) the frame's pre-exposure divided out
    // (path tracing, stage 1) y: the manual exposure's factor on the physical light (0: none)
    // (round EX) z: the local exposure - about the meter's key (1) or, unmetered, the fixed key (2); w: its tiles across
    const float local_mode = !local_on ? 0.0F : (meter != VK_NULL_HANDLE ? 1.0F : 2.0F);
    constants.night = {rt::frame_pre_exposure(), manual.on ? exposure_at_ev100(manual.ev100) : 0.0F, local_mode,
                       static_cast<float>(std::get<0>(tiles))};

    VkCommandBuffer commands = outside_pass(state);
    // the viewport's colour copied out (the copy's barriers by hand: the meter's compute shader reads it too)
    Memory::transition(commands, colour, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    copy_barrier(commands, &own.copy, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    const VkImageSubresourceLayers layers{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1};
    const VkOffset3D corner{.x = static_cast<int32_t>(left), .y = static_cast<int32_t>(top), .z = 0};
    const VkImageCopy region{.srcSubresource = layers,
                             .srcOffset = corner,
                             .dstSubresource = layers,
                             .dstOffset = corner,
                             .extent = {.width = width, .height = height, .depth = 1}};
    vkCmdCopyImage(commands, colour->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, own.copy.image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    copy_barrier(commands, &own.copy, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Memory::transition(commands, colour, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    const VkDescriptorImageInfo source{.sampler = own.sampler,
                                       .imageView = own.copy.view,
                                       .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorBufferInfo table{.buffer = own.lut_buffer.buffer, .offset = 0, .range = VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo metered{.buffer = own.meter_buffer.buffer, .offset = 0, .range = VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo grid{.buffer = own.local_buffer.buffer, .offset = 0, .range = VK_WHOLE_SIZE};
    std::array<VkWriteDescriptorSet, 4> writes{};
    for (VkWriteDescriptorSet& write : writes) {
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.descriptorCount = 1;
    }
    core::at(writes, 0).dstBinding = 0;
    core::at(writes, 0).descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    core::at(writes, 0).pImageInfo = &source;
    core::at(writes, 1).dstBinding = 1;
    core::at(writes, 1).descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    core::at(writes, 1).pBufferInfo = &table;
    core::at(writes, 2).dstBinding = 2;
    core::at(writes, 2).descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    core::at(writes, 2).pBufferInfo = &metered;
    core::at(writes, 3).dstBinding = 3;
    core::at(writes, 3).descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    core::at(writes, 3).pBufferInfo = &grid;
    if (meter != VK_NULL_HANDLE || local_on) {
        vkCmdPushDescriptorSetKHR(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.layout, 0,
                                  static_cast<uint32_t>(writes.size()), writes.data());
    }
    // (the meter first: the viewport's slot moved toward its target)
    if (meter != VK_NULL_HANDLE) {
        const std::size_t slot = meter_slot_of(shown, colour->extent);
        own.last_slot = static_cast<int>(slot);
        follow_camera(state, slot);
        log_readout(state, slot, core::at(constants.night, 0));
        record_meter(commands, meter, slot, &constants);
    }
    // (round EX) then the local exposure's grid, with the meter's factor and key
    if (local_on) {
        record_local(commands, local, constants, tiles);
    }
    // the pass: one triangle over the viewport
    VkRenderingAttachmentInfo colour_info{};
    colour_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colour_info.imageView = colour->attachment_view;
    colour_info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colour_info.resolveMode = VK_RESOLVE_MODE_NONE;
    colour_info.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colour_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    const VkRect2D area{.offset = {.x = static_cast<int32_t>(left), .y = static_cast<int32_t>(top)},
                        .extent = {.width = width, .height = height}};
    VkRenderingInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea = area;
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &colour_info;
    vkCmdBeginRendering(commands, &info);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    const VkViewport viewport{.x = static_cast<float>(left),
                              .y = static_cast<float>(top),
                              .width = static_cast<float>(width),
                              .height = static_cast<float>(height),
                              .minDepth = 0.0F,
                              .maxDepth = 1.0F};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &area);
    vkCmdPushDescriptorSetKHR(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, own.layout, 0,
                              static_cast<uint32_t>(writes.size()), writes.data());
    vkCmdPushConstants(commands, own.layout, kPassStages, 0, sizeof(PassConstants), &constants);
    vkCmdDraw(commands, 3, 1, 0, 0);
    vkCmdEndRendering(commands);
    // the target back where the Gpu's passes expect it; its bound state forgotten (pipeline, descriptors, dynamics)
    if (colour->layout != colour->resting) {
        Memory::transition(commands, colour, colour->resting);
    }
    state.bound = BoundState{};
    const uint64_t serial = state.frames.serial();
    colour->used = serial;
    own.copy.used = serial;
    ++own.passes;
    if (first_report(state, kReportFirstPass)) {
        get_log("vulkan: rt_hdr: the first tone-mapping pass (%ux%u, %u sample%s; exposure %+.2f, curve %u, white "
                "point %.2f - knee %.3f, saturation %.2f, contrast %.2f%s; %s; local exposure %s)",
                width, height, static_cast<unsigned>(colour->samples), colour->samples == 1 ? "" : "s",
                static_cast<double>(own.settings.exposure), tone.curve, static_cast<double>(own.settings.white_point),
                static_cast<double>(tone.knee), static_cast<double>(tone.saturation),
                static_cast<double>(tone.contrast), lut.size >= kLeastLut ? ", a grading table" : "",
                meter != VK_NULL_HANDLE ? "metered: a histogram auto exposure" : "not metered",
                local_on ? "on" : "off");
    }
    return true;
}

uint64_t tone_map_passes() { return hdr().passes; }

bool read_hdr_as_bgra8(Backend& state, Image* image, Extent extent, std::vector<uint8_t>* bgra)
{
    const std::size_t pixels = static_cast<std::size_t>(extent.width) * extent.height;
    std::vector<uint8_t> raw(pixels * kHdrPixelBytes);
    if (image->format != kHdrFormat || image->samples != VK_SAMPLE_COUNT_1_BIT ||
        !state.memory.read_image_level(image, 0, &raw)) {
        return false;
    }
    bgra->resize(pixels * kBgraPixelBytes);
    const std::span<const uint8_t> from(raw);
    const std::span<uint8_t> into(*bgra);
    constexpr uint32_t kHighByte = 8;
    const std::vector<uint8_t>& bytes = half_bytes();
    for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
        const std::span<const uint8_t> texel = from.subspan(pixel * kHdrPixelBytes, kHdrPixelBytes);
        std::array<uint8_t, 4> rgba{};
        for (std::size_t channel = 0; channel < rgba.size(); ++channel) {
            const auto bits = static_cast<uint16_t>(core::at(texel, channel * 2) |
                                                    (core::at(texel, (channel * 2) + 1) << kHighByte));
            core::at(rgba, channel) = core::at(bytes, bits);
        }
        // (B, G, R, A)
        const std::span<uint8_t> out = into.subspan(pixel * kBgraPixelBytes, kBgraPixelBytes);
        core::at(out, 0) = core::at(rgba, 2);
        core::at(out, 1) = core::at(rgba, 1);
        core::at(out, 2) = core::at(rgba, 0);
        core::at(out, 3) = core::at(rgba, 3);
    }
    return true;
}

Image* resolve_for_copy(Backend& state, VkCommandBuffer commands, Image* multisampled)
{
    HdrState& own = hdr();
    Image single = *multisampled;
    single.samples = VK_SAMPLE_COUNT_1_BIT;
    if (!ensure_image_like(state, &own.resolved, single,
                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
        if (first_report(state, kReportCopy)) {
            get_log("vulkan: rt_hdr: no image to resolve a multisampled float target into - its copy refused");
        }
        return nullptr;
    }
    Memory::record_resolve(commands, multisampled, &own.resolved);
    own.resolved.used = state.frames.serial();
    return &own.resolved;
}

void release_hdr(Backend& state)
{
    HdrState& own = hdr();
    const VkDevice device = state.context.device();
    if (device != nullptr) {
        for (const PassPipeline& made : own.pipelines) {
            vkDestroyPipeline(device, made.pipeline, nullptr);
        }
        for (VkPipeline& meter : own.meter_pipelines) {
            if (meter != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, meter, nullptr);
            }
            meter = VK_NULL_HANDLE;
        }
        for (VkPipeline& local : own.local_pipelines) {
            if (local != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, local, nullptr);
            }
            local = VK_NULL_HANDLE;
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
    }
    if (state.memory.ready()) {
        state.memory.destroy_image(&own.copy);
        state.memory.destroy_image(&own.resolved);
        state.memory.destroy_buffer(&own.lut_buffer);
        state.memory.destroy_buffer(&own.meter_buffer);
        state.memory.destroy_buffer(&own.meter_read);
        state.memory.destroy_buffer(&own.local_buffer);
    }
    own.meter_slots = {};
    own.pipelines.clear();
    own.vertex = VK_NULL_HANDLE;
    own.layout = VK_NULL_HANDLE;
    own.set_layout = VK_NULL_HANDLE;
    own.sampler = VK_NULL_HANDLE;
    own.lut_changed = true;
    own.failed = false;
}

} // namespace render::vulkan
