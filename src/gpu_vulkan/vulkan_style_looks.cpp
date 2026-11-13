// NOT ORIGINAL - the art styles beside the toon filter: comic, sketch, noir, thermal (vulkan_style_looks.h).
#include "gpu_vulkan/vulkan_style_looks.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_formats.h"
#include "gpu_vulkan/vulkan_style.h"
#include "gpu_vulkan/vulkan_style_twin.h"

namespace render::vulkan {

namespace {

using style_twin::clamped_at;
using style_twin::finite3;
using style_twin::from_lab;
using style_twin::kBlueYellow;
using style_twin::kGreenRed;
using style_twin::kLightness;
using style_twin::mix;
using style_twin::mix3;
using style_twin::Pixel;
using style_twin::smoothstep;
using style_twin::to_lab;
using style_twin::upsampled;

constexpr float kBaseHeight = 720.0F; // (the height the looks' sizes are given for)
constexpr float kHalf = 0.5F;

// --- the looks' constants (the GLSL gets the same values: glsl_constants) ---------------------------------------
// comic
constexpr float kComicChroma = 1.2F;     // the fills' chroma x this (over rt_style_saturation)
constexpr float kComicLift = 0.12F;      // the fills' lightness lifted: lift + (1 - lift) L (printed colour is never black)
constexpr float kDotsFrom = 0.62F;       // the lightness below which the halftone dots start
constexpr float kDotsSpan = 0.45F;       // ... and how much darker they are full size
constexpr float kDotSize = 0.62F;        // a full dot's radius (of the spacing)
constexpr float kDotEdge = 0.6F;         // the dots' edge, either side (pixels)
constexpr float kDotLightness = 0.5F;    // the dots' shade: the fill's lightness x this
constexpr float kDotChroma = 1.1F;       // ... its chroma x this
constexpr float kDotInk = 0.8F;          // how much of a dot covers the fill
constexpr float kComicFibreCell = 3.0F;  // the paper fibre's cell (grain cells)
constexpr float kComicFibre = 0.08F;     // the fibre's darkening at paper 1, grain 1
constexpr StyleRgb kComicPaper{0.98F, 0.95F, 0.86F};
constexpr StyleRgb kComicInk{0.05F, 0.045F, 0.06F};
// sketch
constexpr float kToneLow = 0.05F;        // the lightness that is black in the tone
constexpr float kToneScale = 1.5F;       // (the tone: (L - low) x scale)
constexpr int kHatchLayers = 4;
constexpr std::array<float, kHatchLayers> kHatchFrom{0.72F, 0.52F, 0.34F, 0.18F}; // each layer's threshold
constexpr std::array<std::array<float, 2>, kHatchLayers> kHatchWay{{
    {0.57357644F, 0.81915204F},  // 55 degrees
    {-0.81915204F, 0.57357644F}, // 145
    {0.98480775F, 0.17364818F},  // 10
    {-0.17364818F, 0.98480775F}, // 100
}};
constexpr float kHatchBelow = 0.1F;      // a layer full this far below its threshold
constexpr float kHatchAbove = 0.06F;     // ... none this far above
constexpr float kRegion = 96.0F;         // the jitter's regions (grain cells)
constexpr float kJitter = 2.0F;          // the strokes' bend: shifted across by up to half this many spacings
constexpr float kStrokeLength = 6.0F;    // the pressure's and gaps' length (spacings)
constexpr float kStrokeWidth = 0.12F;    // a stroke's half width (of the spacing)
constexpr float kStrokeGrow = 0.1F;      // ... wider by this where its layer is full
constexpr float kStrokeEdge = 0.7F;      // (pixels, either side)
constexpr float kPressureLeast = 0.55F;
constexpr float kGapLow = 0.15F;         // (the gaps: the noise below this; strokes from kGapHigh)
constexpr float kGapHigh = 0.3F;
constexpr float kLayerDark = 0.55F;      // a full stroke's cover
constexpr float kLineGrain = 1.5F;       // the outline's grain cell (grain cells)
constexpr float kLineGrainShare = 0.3F;  // ... and how much it lightens
constexpr float kLineDark = 0.9F;
constexpr float kFineFibre = 2.0F;       // the paper's two fibres (grain cells)
constexpr float kCoarseFibre = 9.0F;
constexpr float kFineShare = 0.6F;
constexpr float kSketchFibre = 0.1F;
constexpr float kWashLight = 0.9F;       // the wash: its lightness toward this
constexpr float kWashLift = 0.55F;       // ... by this much
constexpr float kWashChroma = 0.8F;
constexpr StyleRgb kSketchPaper{0.97F, 0.955F, 0.92F};
constexpr StyleRgb kGraphite{0.17F, 0.17F, 0.2F};
constexpr uint32_t kSeedJitter = 20;     // (the noises' seeds: + the layer)
constexpr uint32_t kSeedPressure = 30;
constexpr uint32_t kSeedGap = 40;
constexpr uint32_t kSeedLine = 7;
constexpr uint32_t kSeedFibre = 11;
constexpr uint32_t kSeedCoarse = 12;
// noir
constexpr float kNoirPivot = 0.5F;       // the tone curve's middle ...
constexpr float kNoirAdapt = 0.6F;       // ... moved this much toward the lightness around the pixel
constexpr float kNoirReach = 40.0F;      // (the ring it is read on: 720-line pixels)
constexpr float kNinth = 1.0F / 9.0F;
constexpr float kNoirGrain = 0.18F;      // the grain's size at rt_style_grain 1
constexpr float kGrainFloor = 0.3F;      // ... in the blacks and whites (x 1 + this in the middle grey)
constexpr float kGrainMiddle = 2.8F;
constexpr float kNoirVignette = 0.8F;
constexpr float kVignetteFrom = 0.4F;    // (of the way to a corner)
// thermal
constexpr int kRingPoints = 8;
constexpr std::array<std::array<int, 2>, kRingPoints> kRing{{{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}}};
constexpr std::size_t kRingPairs = 4;   // (kRing's first four and their opposites)
constexpr int kWideRing = 3;           // the second ring: 3 x the first (a skater's middle is in front at that scale)
constexpr float kStandsLow = 0.05F;
constexpr float kStandsHigh = 0.3F;
constexpr float kSkyHeat = 0.04F;
constexpr float kSkyBright = 0.12F;
constexpr float kHeatFloor = 0.1F;
constexpr float kHeatBright = 0.38F;
constexpr float kHeatNear = 0.15F;
constexpr float kHeatStands = 0.6F;
constexpr float kNoisePixel = 0.05F;
constexpr float kNoiseLine = 0.03F;
constexpr float kNoiseFrame = 0.03F;
constexpr float kThermalVignette = 0.5F;
constexpr uint32_t kLineColumn = 65535;  // (the per-line and per-frame noises' hash columns)
constexpr uint32_t kFrameColumn = 65534;
constexpr std::array<StyleRgb, 7> kHeat{{
    {0.0F, 0.0F, 0.02F}, {0.08F, 0.03F, 0.38F}, {0.45F, 0.05F, 0.6F}, {0.85F, 0.08F, 0.25F},
    {1.0F, 0.45F, 0.02F}, {1.0F, 0.88F, 0.2F}, {1.0F, 1.0F, 0.92F},
}};
constexpr std::array<float, 3> kLuma{0.2126F, 0.7152F, 0.0722F};
// the hash
constexpr uint32_t kHashX = 0x8DA6B343U;
constexpr uint32_t kHashY = 0xD8163841U;
constexpr uint32_t kHashZ = 0x9E3779B9U;
constexpr uint32_t kMixA = 0x7FEB352DU;
constexpr uint32_t kMixB = 0x846CA68BU;
constexpr uint32_t kShiftLong = 16;
constexpr uint32_t kShiftShort = 15;
constexpr uint32_t kUnitShift = 8;
constexpr float kUnitScale = 1.0F / 16777216.0F; // (2^-24)
constexpr int kNoiseOrigin = 32768;              // (the noise's cells counted from here: negative places hash too)
constexpr float kHermiteThree = 3.0F;
constexpr float kHermiteTwo = 2.0F;
constexpr float kRootHalf = 0.70710678F;

// --- the GLSL -----------------------------------------------------------------------------------------------------

struct GlslFloat {
    const char* name;
    float value;
};
struct GlslColour {
    const char* name;
    StyleRgb value;
};

std::string glsl_constants()
{
    const std::array floats{
        GlslFloat{.name = "kComicChroma", .value = kComicChroma}, GlslFloat{.name = "kComicLift", .value = kComicLift},
        GlslFloat{.name = "kDotsFrom", .value = kDotsFrom},
        GlslFloat{.name = "kDotsSpan", .value = kDotsSpan}, GlslFloat{.name = "kDotSize", .value = kDotSize},
        GlslFloat{.name = "kDotEdge", .value = kDotEdge}, GlslFloat{.name = "kDotLightness", .value = kDotLightness},
        GlslFloat{.name = "kDotChroma", .value = kDotChroma}, GlslFloat{.name = "kDotInk", .value = kDotInk},
        GlslFloat{.name = "kComicFibreCell", .value = kComicFibreCell}, GlslFloat{.name = "kComicFibre", .value = kComicFibre},
        GlslFloat{.name = "kToneLow", .value = kToneLow}, GlslFloat{.name = "kToneScale", .value = kToneScale},
        GlslFloat{.name = "kHatchBelow", .value = kHatchBelow}, GlslFloat{.name = "kHatchAbove", .value = kHatchAbove},
        GlslFloat{.name = "kRegion", .value = kRegion}, GlslFloat{.name = "kJitter", .value = kJitter},
        GlslFloat{.name = "kStrokeLength", .value = kStrokeLength}, GlslFloat{.name = "kStrokeWidth", .value = kStrokeWidth},
        GlslFloat{.name = "kStrokeGrow", .value = kStrokeGrow}, GlslFloat{.name = "kStrokeEdge", .value = kStrokeEdge},
        GlslFloat{.name = "kPressureLeast", .value = kPressureLeast}, GlslFloat{.name = "kGapLow", .value = kGapLow},
        GlslFloat{.name = "kGapHigh", .value = kGapHigh}, GlslFloat{.name = "kLayerDark", .value = kLayerDark},
        GlslFloat{.name = "kLineGrain", .value = kLineGrain}, GlslFloat{.name = "kLineGrainShare", .value = kLineGrainShare},
        GlslFloat{.name = "kLineDark", .value = kLineDark}, GlslFloat{.name = "kFineFibre", .value = kFineFibre},
        GlslFloat{.name = "kCoarseFibre", .value = kCoarseFibre}, GlslFloat{.name = "kFineShare", .value = kFineShare},
        GlslFloat{.name = "kSketchFibre", .value = kSketchFibre}, GlslFloat{.name = "kWashLight", .value = kWashLight},
        GlslFloat{.name = "kWashLift", .value = kWashLift}, GlslFloat{.name = "kWashChroma", .value = kWashChroma},
        GlslFloat{.name = "kNoirPivot", .value = kNoirPivot}, GlslFloat{.name = "kNoirAdapt", .value = kNoirAdapt},
        GlslFloat{.name = "kNinth", .value = kNinth}, GlslFloat{.name = "kNoirGrain", .value = kNoirGrain}, GlslFloat{.name = "kGrainFloor", .value = kGrainFloor},
        GlslFloat{.name = "kGrainMiddle", .value = kGrainMiddle}, GlslFloat{.name = "kNoirVignette", .value = kNoirVignette},
        GlslFloat{.name = "kVignetteFrom", .value = kVignetteFrom},
        GlslFloat{.name = "kStandsLow", .value = kStandsLow}, GlslFloat{.name = "kStandsHigh", .value = kStandsHigh},
        GlslFloat{.name = "kSkyHeat", .value = kSkyHeat}, GlslFloat{.name = "kSkyBright", .value = kSkyBright},
        GlslFloat{.name = "kHeatFloor", .value = kHeatFloor}, GlslFloat{.name = "kHeatBright", .value = kHeatBright},
        GlslFloat{.name = "kHeatNear", .value = kHeatNear}, GlslFloat{.name = "kHeatStands", .value = kHeatStands},
        GlslFloat{.name = "kNoisePixel", .value = kNoisePixel}, GlslFloat{.name = "kNoiseLine", .value = kNoiseLine},
        GlslFloat{.name = "kNoiseFrame", .value = kNoiseFrame}, GlslFloat{.name = "kThermalVignette", .value = kThermalVignette},
    };
    const std::array colours{
        GlslColour{.name = "kComicPaper", .value = kComicPaper}, GlslColour{.name = "kComicInk", .value = kComicInk},
        GlslColour{.name = "kSketchPaper", .value = kSketchPaper}, GlslColour{.name = "kGraphite", .value = kGraphite},
    };
    std::string text;
    for (const GlslFloat& constant : floats) {
        text += text::format("const float %s = %.9g;\n", constant.name, static_cast<double>(constant.value));
    }
    for (const GlslColour& constant : colours) {
        text += text::format("const vec3 %s = vec3(%.9g, %.9g, %.9g);\n", constant.name,
                             static_cast<double>(core::at(constant.value, 0)), static_cast<double>(core::at(constant.value, 1)),
                             static_cast<double>(core::at(constant.value, 2)));
    }
    text += "const float kHatchFrom[4] = float[4](";
    for (std::size_t layer = 0; layer < kHatchFrom.size(); ++layer) {
        text += text::format("%s%.9g", layer == 0 ? "" : ", ", static_cast<double>(core::at(kHatchFrom, layer)));
    }
    text += ");\nconst vec2 kHatchWay[4] = vec2[4](";
    for (std::size_t layer = 0; layer < kHatchWay.size(); ++layer) {
        text += text::format("%svec2(%.9g, %.9g)", layer == 0 ? "" : ", ", static_cast<double>(core::at(core::at(kHatchWay, layer), 0)),
                             static_cast<double>(core::at(core::at(kHatchWay, layer), 1)));
    }
    // the ink's taps: every offset within kMostInk + 0.5 pixels, nearest first (x, y, its distance); kInkCount[k] the
    // number within k + 0.5
    struct Tap {
        int x;
        int y;
        float distance;
    };
    std::vector<Tap> taps;
    for (int down = -kMostInk; down <= kMostInk; ++down) {
        for (int across = -kMostInk; across <= kMostInk; ++across) {
            const float distance = std::sqrt(static_cast<float>((across * across) + (down * down)));
            if (distance <= static_cast<float>(kMostInk) + kHalf) {
                taps.push_back({.x = across, .y = down, .distance = distance});
            }
        }
    }
    std::ranges::stable_sort(taps, [](const Tap& a, const Tap& b) { return a.distance < b.distance; });
    text += text::format(");\nconst vec3 kInkTaps[%zu] = vec3[%zu](", taps.size(), taps.size());
    for (std::size_t tap = 0; tap < taps.size(); ++tap) {
        text += text::format("%svec3(%d, %d, %.9g)", tap == 0 ? "" : ", ", core::at(taps, tap).x, core::at(taps, tap).y,
                             static_cast<double>(core::at(taps, tap).distance));
    }
    text += text::format(");\nconst int kInkCount[%d] = int[%d](", kMostInk + 1, kMostInk + 1);
    for (int reach = 0; reach <= kMostInk; ++reach) {
        const auto within = std::ranges::count_if(
            taps, [reach](const Tap& tap) { return tap.distance <= static_cast<float>(reach) + kHalf; });
        text += text::format("%s%d", reach == 0 ? "" : ", ", static_cast<int>(within));
    }
    text += ");\nconst vec3 kHeat[7] = vec3[7](";
    for (std::size_t stop = 0; stop < kHeat.size(); ++stop) {
        const StyleRgb& colour = core::at(kHeat, stop);
        text += text::format("%svec3(%.9g, %.9g, %.9g)", stop == 0 ? "" : ", ", static_cast<double>(core::at(colour, 0)),
                             static_cast<double>(core::at(colour, 1)), static_cast<double>(core::at(colour, 2)));
    }
    text += ");\n";
    return text;
}

// After kCommon, kCompositeShared and kEdgeGlsl (vulkan_style.cpp) and the constants.
constexpr const char* kLookShared = R"(
layout(set = 0, binding = 5) uniform sampler2D u_edges;

const ivec2 kRing[8] = ivec2[8](ivec2(1, 0), ivec2(1, 1), ivec2(0, 1), ivec2(-1, 1), ivec2(-1, 0), ivec2(-1, -1),
                                ivec2(0, -1), ivec2(1, -1));

uint mix_bits(uint h)
{
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h;
}

uint hash3(uint x, uint y, uint z)
{
    return mix_bits((x * 0x8DA6B343u) ^ mix_bits((y * 0xD8163841u) ^ mix_bits(z + 0x9E3779B9u)));
}

float unit(uint h) { return float(h >> 8) * (1.0 / 16777216.0); }

// Smooth noise 0 - 1: a hashed value per whole-numbered place, eased between them.
float value_noise(vec2 x, uint seed)
{
    vec2 i = floor(x);
    vec2 f = x - i;
    vec2 u = f * f * (3.0 - 2.0 * f);
    uvec2 c = uvec2(ivec2(i) + ivec2(32768));
    float a = unit(hash3(c.x, c.y, seed));
    float b = unit(hash3(c.x + 1u, c.y, seed));
    float d = unit(hash3(c.x, c.y + 1u, seed));
    float e = unit(hash3(c.x + 1u, c.y + 1u, seed));
    return mix(mix(a, b, u.x), mix(d, e, u.x), u.y);
}

uvec2 grain_cell(ivec2 local) { return uvec2(local) / uint(pc.look_a.y); }

float vignette_at(ivec2 local, float amount)
{
    vec2 size = vec2(pc.out_rect.zw);
    float aspect = size.x / size.y;
    vec2 at = ((vec2(local) + 0.5) / size - 0.5) * vec2(aspect, 1.0);
    float r = length(at) / length(vec2(0.5 * aspect, 0.5));
    return 1.0 - amount * smoothstep(kVignetteFrom, 1.0, r);
}

// The edge image widened round the pixel (vulkan_style_looks.h "ink"), times rt_style_outlines.
float ink(ivec2 local)
{
    if (pc.depth_map.z < 0.5 || pc.outline.x <= 0.0) {
        return 0.0;
    }
    ivec2 q = depth_pixel(local);
    ivec2 lo = pc.depth_rect.xy;
    ivec2 hi = pc.depth_rect.xy + pc.depth_rect.zw - 1;
    int k = int(pc.look_b.z);
    float grow = float(k) + 1.0;
    float best = 0.0;
    for (int tap = 0; tap < kInkCount[k]; ++tap) {
        vec3 offset = kInkTaps[tap];
        ivec2 s = q + ivec2(offset.xy);
        if (any(lessThan(s, lo)) || any(greaterThan(s, hi))) {
            continue;
        }
        float e = texelFetch(u_edges, s, 0).r;
        best = max(best, e * clamp(e * grow - offset.z, 0.0, 1.0));
    }
    return clamp(best * pc.outline.x, 0.0, 1.0);
}

// The half-resolution colour brought up bilinearly (the thermal look's blur).
vec3 blurred(ivec2 local)
{
    vec2 at = (vec2(local) + 0.5) * 0.5 - 0.5;
    ivec2 corner = ivec2(floor(at));
    vec2 f = at - vec2(corner);
    ivec2 last = pc.sizes.xy - 1;
    vec3 plain = vec3(0.0);
    for (int oy = 0; oy < 2; ++oy) {
        for (int ox = 0; ox < 2; ++ox) {
            vec3 c = texelFetch(u_smooth, clamp(corner + ivec2(ox, oy), ivec2(0), last), 0).rgb;
            float wb = (ox == 1 ? f.x : 1.0 - f.x) * (oy == 1 ? f.y : 1.0 - f.y);
            plain += c * wb;
        }
    }
    return plain;
}
)";

// look_a: x grain, y the grain's cell, z the seed, w vignette. look_b: x the dots' spacing, y paper, z the ink's reach.
constexpr const char* kComicBody = R"(
void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 local = p - pc.out_rect.xy;
    vec3 own = finite3(texelFetch(u_colour, p, 0).rgb);
    vec3 base = pc.sizes.z > 0 ? upsampled(local, own) : own;
    vec3 lab = to_lab(base);
    float light = lab.x;
    float n = pc.tone.x;
    if (n > 0.0) {
        float t = light * n - 0.5;
        float i = floor(t);
        float f = t - i;
        lab.x = clamp((i + 0.5 + smoothstep(0.5 - pc.tone.y, 0.5 + pc.tone.y, f)) / n, 0.0, 1.0);
    }
    lab.x = kComicLift + (1.0 - kComicLift) * lab.x;
    lab.yz *= pc.tone.z * kComicChroma;
    float spacing = pc.look_b.x;
    float darkness = clamp((kDotsFrom - light) / kDotsSpan, 0.0, 1.0);
    if (spacing > 0.0 && darkness > 0.0) {
        vec2 at = (vec2(local) + 0.5) / spacing;
        vec2 turned = vec2(at.x - at.y, at.x + at.y) * 0.70710678;
        vec2 f = fract(turned) - 0.5;
        float gap_px = length(f) * spacing;
        float radius = spacing * kDotSize * sqrt(darkness);
        float cover = 1.0 - smoothstep(radius - kDotEdge, radius + kDotEdge, gap_px);
        lab = mix(lab, vec3(lab.x * kDotLightness, lab.yz * kDotChroma), cover * kDotInk);
    }
    vec3 colour = from_lab(lab);
    float fibre = value_noise((vec2(local) + 0.5) / (pc.look_a.y * kComicFibreCell), 11u);
    colour *= mix(vec3(1.0), kComicPaper, pc.look_b.y) * (1.0 - pc.look_b.y * pc.look_a.x * kComicFibre * fibre);
    colour = mix(colour, kComicInk, ink(local));
    o_colour = vec4(mix(own, colour, pc.tone.w), 1.0);
}
)";

// look_b: x the strokes' spacing, y paper, z the ink's reach, w wash.
constexpr const char* kSketchBody = R"(
float stroke(vec2 at, int k, float weight)
{
    vec2 way = kHatchWay[k];
    float spacing = pc.look_b.x;
    float bend = (value_noise(at / (kRegion * pc.look_a.y), uint(20 + k)) - 0.5) * kJitter;
    float across = dot(at, vec2(-way.y, way.x)) / spacing + bend;
    float along = dot(at, way) / (spacing * kStrokeLength);
    float line = floor(across);
    float off = abs(fract(across) - 0.5) * spacing;
    float half_width = spacing * (kStrokeWidth + kStrokeGrow * weight);
    float body = 1.0 - smoothstep(half_width - kStrokeEdge, half_width + kStrokeEdge, off);
    if (body <= 0.0) {
        return 0.0;
    }
    float pressure = kPressureLeast + (1.0 - kPressureLeast) * value_noise(vec2(along, line), uint(30 + k));
    float gap = smoothstep(kGapLow, kGapHigh, value_noise(vec2(along * 0.5, line + 0.5), uint(40 + k)));
    return body * pressure * gap;
}

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 local = p - pc.out_rect.xy;
    vec3 own = finite3(texelFetch(u_colour, p, 0).rgb);
    vec3 base = pc.sizes.z > 0 ? upsampled(local, own) : own;
    vec3 lab = to_lab(base);
    float tone = clamp((lab.x - kToneLow) * kToneScale, 0.0, 1.0);
    vec2 at = vec2(local) + 0.5;
    float lead = 0.0;
    for (int k = 0; k < 4; ++k) {
        float weight = 1.0 - smoothstep(kHatchFrom[k] - kHatchBelow, kHatchFrom[k] + kHatchAbove, tone);
        if (weight > 0.0) {
            lead = 1.0 - (1.0 - lead) * (1.0 - weight * kLayerDark * stroke(at, k, weight));
        }
    }
    float line = ink(local) * ((1.0 - kLineGrainShare) + kLineGrainShare * value_noise(at / (pc.look_a.y * kLineGrain), 7u));
    lead = 1.0 - (1.0 - lead) * (1.0 - line * kLineDark);
    float fibre = value_noise(at / (pc.look_a.y * kFineFibre), 11u) * kFineShare +
                  value_noise(at / (pc.look_a.y * kCoarseFibre), 12u) * (1.0 - kFineShare);
    vec3 paper = mix(vec3(1.0), kSketchPaper, pc.look_b.y) * (1.0 - pc.look_a.x * kSketchFibre * fibre);
    vec3 pale = from_lab(vec3(mix(lab.x, kWashLight, kWashLift), lab.yz * kWashChroma));
    vec3 under = mix(paper, paper * pale, pc.look_b.w);
    vec3 colour = mix(under, kGraphite, lead);
    o_colour = vec4(mix(own, colour, pc.tone.w), 1.0);
}
)";

// look_b: x contrast, y the surroundings' ring (half-resolution pixels); look_c, look_d: the dark and light colours.
// u_smooth: the half-resolution colour.
constexpr const char* kNoirBody = R"(
// The mean lightness of the half-resolution colour at the pixel and on a ring around it.
float surroundings(ivec2 local)
{
    ivec2 c = local / 2;
    ivec2 last = pc.sizes.xy - 1;
    int r = int(pc.look_b.y);
    float sum = to_lab(texelFetch(u_smooth, clamp(c, ivec2(0), last), 0).rgb).x;
    for (int k = 0; k < 8; ++k) {
        sum += to_lab(texelFetch(u_smooth, clamp(c + kRing[k] * r, ivec2(0), last), 0).rgb).x;
    }
    return sum * kNinth;
}

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 local = p - pc.out_rect.xy;
    vec3 own = finite3(texelFetch(u_colour, p, 0).rgb);
    float light = to_lab(own).x;
    float pivot = mix(kNoirPivot, surroundings(local), kNoirAdapt);
    float t = clamp((light - pivot) * pc.look_b.x + 0.5, 0.0, 1.0);
    t = t * t * (3.0 - 2.0 * t);
    uvec2 g = grain_cell(local);
    float n = unit(hash3(g.x, g.y, uint(pc.look_a.z))) - 0.5;
    t = clamp(t + n * pc.look_a.x * kNoirGrain * (kGrainFloor + kGrainMiddle * t * (1.0 - t)), 0.0, 1.0);
    t *= vignette_at(local, pc.look_a.w * kNoirVignette);
    vec3 colour = mix(pc.look_c.rgb, pc.look_d.rgb, t);
    o_colour = vec4(mix(own, colour, pc.tone.w), 1.0);
}
)";

// look_b: x focus, y the ring (depth pixels), z, w the nearness' range. u_smooth: the half-resolution colour.
constexpr const char* kThermalBody = R"(
// How far in front the pixel stands, from four pairs of opposite points on a ring of radius r: per pair the lesser of
// 1 - z / z_point (both sides must be farther - on a plane one side is nearer; a ledge's edge has the ground on one
// side), the most of the pairs (a point on the sky: 1).
float ring_stands(ivec2 q, float z, int r)
{
    ivec2 lo = pc.depth_rect.xy;
    ivec2 hi = pc.depth_rect.xy + pc.depth_rect.zw - 1;
    float best = 0.0;
    for (int k = 0; k < 4; ++k) {
        float a = depth_at(clamp(q + kRing[k] * r, lo, hi));
        float b = depth_at(clamp(q - kRing[k] * r, lo, hi));
        float behind_a = a >= kSky * 0.5 ? 1.0 : 1.0 - z / a;
        float behind_b = b >= kSky * 0.5 ? 1.0 : 1.0 - z / b;
        best = max(best, min(behind_a, behind_b));
    }
    return best;
}

vec3 heat_colour(float heat)
{
    float x = clamp(heat, 0.0, 1.0) * 6.0;
    int i = min(int(x), 5);
    return mix(kHeat[i], kHeat[i + 1], x - float(i));
}

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 local = p - pc.out_rect.xy;
    vec3 own = finite3(texelFetch(u_colour, p, 0).rgb);
    float bright = dot(blurred(local), vec3(0.2126, 0.7152, 0.0722));
    bool sky = false;
    float near = 0.0;
    float stands = 0.0;
    if (pc.depth_map.z > 0.5) {
        ivec2 q = depth_pixel(local);
        float z = depth_at(q);
        if (z >= kSky * 0.5) {
            sky = true;
        } else {
            near = 1.0 - smoothstep(pc.look_b.z, pc.look_b.w, z);
            int r = int(pc.look_b.y);
            stands = smoothstep(kStandsLow, kStandsHigh, max(ring_stands(q, z, r), ring_stands(q, z, r * 3)));
        }
    }
    float heat = sky ? kSkyHeat + kSkyBright * bright
                     : kHeatFloor + kHeatBright * bright + kHeatNear * near + pc.look_b.x * kHeatStands * stands;
    uint seed = uint(pc.look_a.z);
    uvec2 g = grain_cell(local);
    heat += pc.look_a.x * (kNoisePixel * (unit(hash3(g.x, g.y, seed)) - 0.5) +
                           kNoiseLine * (unit(hash3(65535u, g.y, seed)) - 0.5) +
                           kNoiseFrame * (unit(hash3(65534u, 0u, seed)) - 0.5));
    heat *= vignette_at(local, pc.look_a.w * kThermalVignette);
    o_colour = vec4(mix(own, heat_colour(heat), pc.tone.w), 1.0);
}
)";

// --- the processor's twins ------------------------------------------------------------------------------------------

uint32_t mix_bits(uint32_t bits)
{
    bits ^= bits >> kShiftLong;
    bits *= kMixA;
    bits ^= bits >> kShiftShort;
    bits *= kMixB;
    bits ^= bits >> kShiftLong;
    return bits;
}

uint32_t hash3(uint32_t x, uint32_t y, uint32_t z) { return mix_bits((x * kHashX) ^ mix_bits((y * kHashY) ^ mix_bits(z + kHashZ))); }

float unit(uint32_t bits) { return static_cast<float>(bits >> kUnitShift) * kUnitScale; }

float ease(float part) { return part * part * (kHermiteThree - (kHermiteTwo * part)); }

// A place in a noise's cells (x across, y down).
using NoisePlace = std::array<float, 2>;

float value_noise(const NoisePlace& place, uint32_t seed)
{
    const float x = core::at(place, 0);
    const float y = core::at(place, 1);
    const float whole_x = std::floor(x);
    const float whole_y = std::floor(y);
    const float ease_x = ease(x - whole_x);
    const float ease_y = ease(y - whole_y);
    const auto cell_x = static_cast<uint32_t>(static_cast<int>(whole_x) + kNoiseOrigin);
    const auto cell_y = static_cast<uint32_t>(static_cast<int>(whole_y) + kNoiseOrigin);
    const float top_left = unit(hash3(cell_x, cell_y, seed));
    const float top_right = unit(hash3(cell_x + 1, cell_y, seed));
    const float below_left = unit(hash3(cell_x, cell_y + 1, seed));
    const float below_right = unit(hash3(cell_x + 1, cell_y + 1, seed));
    return mix(mix(top_left, top_right, ease_x), mix(below_left, below_right, ease_x), ease_y);
}

StyleRgb times3(const StyleRgb& colour, const StyleRgb& by)
{
    StyleRgb out{};
    for (std::size_t channel = 0; channel < out.size(); ++channel) {
        core::at(out, channel) = core::at(colour, channel) * core::at(by, channel);
    }
    return out;
}

StyleRgb scaled3(const StyleRgb& colour, float by)
{
    StyleRgb out{};
    for (std::size_t channel = 0; channel < out.size(); ++channel) {
        core::at(out, channel) = core::at(colour, channel) * by;
    }
    return out;
}

constexpr StyleRgb kWhite{1.0F, 1.0F, 1.0F};

// What a twin knows of the viewport and its pixel.
struct Place {
    const StyleParameters* parameters = nullptr;
    Extent size;
    Pixel at;
};

std::array<uint32_t, 2> grain_cell(const Place& place)
{
    const auto cell = static_cast<uint32_t>(place.parameters->cell);
    return {static_cast<uint32_t>(place.at.column) / cell, static_cast<uint32_t>(place.at.row) / cell};
}

float vignette_at(const Place& place, float amount)
{
    const auto width = static_cast<float>(place.size.width);
    const auto height = static_cast<float>(place.size.height);
    const float aspect = width / height;
    const float x = (((static_cast<float>(place.at.column) + kHalf) / width) - kHalf) * aspect;
    const float y = ((static_cast<float>(place.at.row) + kHalf) / height) - kHalf;
    const float corner = std::sqrt((kHalf * aspect * kHalf * aspect) + (kHalf * kHalf));
    const float reach = std::sqrt((x * x) + (y * y)) / corner;
    return 1.0F - (amount * smoothstep(kVignetteFrom, 1.0F, reach));
}

float ink(const Place& place, const std::vector<float>* edges)
{
    if (edges == nullptr || edges->empty() || !(place.parameters->outlines > 0.0F)) {
        return 0.0F;
    }
    const int reach_pixels = place.parameters->ink;
    const float reach = static_cast<float>(reach_pixels) + kHalf;
    float best = 0.0F;
    for (int down = -reach_pixels; down <= reach_pixels; ++down) {
        for (int across = -reach_pixels; across <= reach_pixels; ++across) {
            const Pixel at{.column = place.at.column + across, .row = place.at.row + down};
            const float distance = std::sqrt(static_cast<float>((across * across) + (down * down)));
            if (distance > reach || at.column < 0 || at.row < 0 || std::cmp_greater_equal(at.column, place.size.width) ||
                std::cmp_greater_equal(at.row, place.size.height)) {
                continue;
            }
            const float edge = clamped_at(*edges, place.size, at);
            best = std::max(best, edge * std::clamp((edge * (static_cast<float>(reach_pixels) + 1.0F)) - distance, 0.0F, 1.0F));
        }
    }
    return std::clamp(best * place.parameters->outlines, 0.0F, 1.0F);
}

// A pixel's own colour and what the look starts from (the smoothed colour; thermal: the blurred one).
struct Colours {
    StyleRgb own;
    StyleRgb base;
};

StyleRgb comic_pixel(const Place& place, const Colours& colours, const std::vector<float>* edges)
{
    const StyleParameters& parameters = *place.parameters;
    const StyleRgb& own = colours.own;
    StyleRgb lab = to_lab(colours.base);
    const float light = core::at(lab, kLightness);
    const float bands = parameters.bands;
    if (bands > 0.0F) {
        const float position = (light * bands) - kHalf;
        const float whole = std::floor(position);
        const float part = position - whole;
        core::at(lab, kLightness) =
            std::clamp((whole + kHalf + smoothstep(kHalf - kComicSoftness, kHalf + kComicSoftness, part)) / bands, 0.0F, 1.0F);
    }
    core::at(lab, kLightness) = kComicLift + ((1.0F - kComicLift) * core::at(lab, kLightness));
    const float chroma = parameters.saturation * kComicChroma;
    core::at(lab, kGreenRed) *= chroma;
    core::at(lab, kBlueYellow) *= chroma;
    const float spacing = parameters.halftone;
    const float darkness = std::clamp((kDotsFrom - light) / kDotsSpan, 0.0F, 1.0F);
    if (spacing > 0.0F && darkness > 0.0F) {
        const float at_x = (static_cast<float>(place.at.column) + kHalf) / spacing;
        const float at_y = (static_cast<float>(place.at.row) + kHalf) / spacing;
        const float turned_x = (at_x - at_y) * kRootHalf;
        const float turned_y = (at_x + at_y) * kRootHalf;
        const float off_x = turned_x - std::floor(turned_x) - kHalf;
        const float off_y = turned_y - std::floor(turned_y) - kHalf;
        const float distance = std::sqrt((off_x * off_x) + (off_y * off_y)) * spacing;
        const float radius = spacing * kDotSize * std::sqrt(darkness);
        const float cover = 1.0F - smoothstep(radius - kDotEdge, radius + kDotEdge, distance);
        const StyleRgb dot{core::at(lab, kLightness) * kDotLightness, core::at(lab, kGreenRed) * kDotChroma,
                           core::at(lab, kBlueYellow) * kDotChroma};
        lab = mix3(lab, dot, cover * kDotInk);
    }
    StyleRgb colour = from_lab(lab);
    const float cell = parameters.cell * kComicFibreCell;
    const float fibre = value_noise(
        {(static_cast<float>(place.at.column) + kHalf) / cell, (static_cast<float>(place.at.row) + kHalf) / cell}, kSeedFibre);
    colour = scaled3(times3(colour, mix3(kWhite, kComicPaper, parameters.paper)),
                     1.0F - (parameters.paper * parameters.grain * kComicFibre * fibre));
    colour = mix3(colour, kComicInk, ink(place, edges));
    return mix3(own, colour, parameters.strength);
}

// A hatching layer: which, and how full it is here.
struct Layer {
    std::size_t index = 0;
    float weight = 0.0F;
};

float stroke(const Place& place, const Layer& hatching)
{
    const std::size_t layer = hatching.index;
    const float weight = hatching.weight;
    const StyleParameters& parameters = *place.parameters;
    const float at_x = static_cast<float>(place.at.column) + kHalf;
    const float at_y = static_cast<float>(place.at.row) + kHalf;
    const std::array<float, 2>& way = core::at(kHatchWay, layer);
    const float spacing = parameters.hatch;
    const float region = kRegion * parameters.cell;
    const float bend =
        (value_noise({at_x / region, at_y / region}, kSeedJitter + static_cast<uint32_t>(layer)) - kHalf) * kJitter;
    const float across = (((at_x * -core::at(way, 1)) + (at_y * core::at(way, 0))) / spacing) + bend;
    const float along = ((at_x * core::at(way, 0)) + (at_y * core::at(way, 1))) / (spacing * kStrokeLength);
    const float line = std::floor(across);
    const float off = std::abs(across - line - kHalf) * spacing;
    const float half_width = spacing * (kStrokeWidth + (kStrokeGrow * weight));
    const float body = 1.0F - smoothstep(half_width - kStrokeEdge, half_width + kStrokeEdge, off);
    if (!(body > 0.0F)) {
        return 0.0F;
    }
    const float pressure =
        kPressureLeast + ((1.0F - kPressureLeast) * value_noise({along, line}, kSeedPressure + static_cast<uint32_t>(layer)));
    const float gap =
        smoothstep(kGapLow, kGapHigh, value_noise({along * kHalf, line + kHalf}, kSeedGap + static_cast<uint32_t>(layer)));
    return body * pressure * gap;
}

StyleRgb sketch_pixel(const Place& place, const Colours& colours, const std::vector<float>* edges)
{
    const StyleParameters& parameters = *place.parameters;
    const StyleRgb& own = colours.own;
    const StyleRgb lab = to_lab(colours.base);
    const float tone = std::clamp((core::at(lab, kLightness) - kToneLow) * kToneScale, 0.0F, 1.0F);
    float lead = 0.0F;
    for (std::size_t layer = 0; layer < kHatchFrom.size(); ++layer) {
        const float from = core::at(kHatchFrom, layer);
        const float weight = 1.0F - smoothstep(from - kHatchBelow, from + kHatchAbove, tone);
        if (weight > 0.0F) {
            lead = 1.0F - ((1.0F - lead) * (1.0F - (weight * kLayerDark * stroke(place, {.index = layer, .weight = weight}))));
        }
    }
    const float at_x = static_cast<float>(place.at.column) + kHalf;
    const float at_y = static_cast<float>(place.at.row) + kHalf;
    const float line_cell = parameters.cell * kLineGrain;
    const float line = ink(place, edges) * ((1.0F - kLineGrainShare) +
                                            (kLineGrainShare * value_noise({at_x / line_cell, at_y / line_cell}, kSeedLine)));
    lead = 1.0F - ((1.0F - lead) * (1.0F - (line * kLineDark)));
    const float fine = parameters.cell * kFineFibre;
    const float coarse = parameters.cell * kCoarseFibre;
    const float fibre = (value_noise({at_x / fine, at_y / fine}, kSeedFibre) * kFineShare) +
                        (value_noise({at_x / coarse, at_y / coarse}, kSeedCoarse) * (1.0F - kFineShare));
    const StyleRgb paper =
        scaled3(mix3(kWhite, kSketchPaper, parameters.paper), 1.0F - (parameters.grain * kSketchFibre * fibre));
    const StyleRgb pale = from_lab({mix(core::at(lab, kLightness), kWashLight, kWashLift), core::at(lab, kGreenRed) * kWashChroma,
                                    core::at(lab, kBlueYellow) * kWashChroma});
    const StyleRgb under = mix3(paper, times3(paper, pale), parameters.wash);
    return mix3(own, mix3(under, kGraphite, lead), parameters.strength);
}

float surroundings(const Place& place, const std::vector<StyleRgb>& half)
{
    const Extent half_size{.width = (place.size.width + 1) / 2, .height = (place.size.height + 1) / 2};
    const Pixel centre{.column = place.at.column / 2, .row = place.at.row / 2};
    const int reach = place.parameters->ring;
    const StyleRgb middle = to_lab(clamped_at(half, half_size, centre));
    float sum = core::at(middle, kLightness);
    for (const std::array<int, 2>& way : kRing) {
        const Pixel at{.column = centre.column + (core::at(way, 0) * reach), .row = centre.row + (core::at(way, 1) * reach)};
        const StyleRgb there = to_lab(clamped_at(half, half_size, at));
        sum += core::at(there, kLightness);
    }
    return sum * kNinth;
}

StyleRgb noir_pixel(const Place& place, const StyleRgb& own, const std::vector<StyleRgb>& half)
{
    const StyleParameters& parameters = *place.parameters;
    const StyleRgb lab = to_lab(own);
    const float light = core::at(lab, kLightness);
    const float pivot = mix(kNoirPivot, surroundings(place, half), kNoirAdapt);
    float tone = std::clamp(((light - pivot) * parameters.contrast) + kHalf, 0.0F, 1.0F);
    tone = ease(tone);
    const std::array<uint32_t, 2> cell = grain_cell(place);
    const float noise = unit(hash3(core::at(cell, 0), core::at(cell, 1), parameters.seed)) - kHalf;
    tone = std::clamp(tone + (noise * parameters.grain * kNoirGrain * (kGrainFloor + (kGrainMiddle * tone * (1.0F - tone)))),
                      0.0F, 1.0F);
    tone *= vignette_at(place, parameters.vignette * kNoirVignette);
    return mix3(own, mix3(parameters.dark, parameters.light, tone), parameters.strength);
}

StyleRgb heat_colour(float heat)
{
    constexpr float kStops = 6.0F;
    constexpr int kLastStart = 5;
    const float x = std::clamp(heat, 0.0F, 1.0F) * kStops;
    const int stop = std::min(static_cast<int>(x), kLastStart);
    return mix3(core::at(kHeat, static_cast<std::size_t>(stop)), core::at(kHeat, static_cast<std::size_t>(stop) + 1),
                x - static_cast<float>(stop));
}

// How far the pixel stands in front of its surroundings (0 - 1) and its nearness; false: the sky.
struct Warmth {
    bool sky = false;
    float near = 0.0F;
    float stands = 0.0F;
};

Warmth warmth_at(const Place& place, const std::vector<float>* depth)
{
    Warmth warmth;
    if (depth == nullptr || depth->empty()) {
        return warmth;
    }
    const StyleParameters& parameters = *place.parameters;
    const float z = clamped_at(*depth, place.size, place.at);
    if (z >= kStyleSky * kHalf) {
        warmth.sky = true;
        return warmth;
    }
    warmth.near = 1.0F - smoothstep(kThermalNear, kThermalFar, z);
    const auto behind = [&](int column, int row) {
        const float there = clamped_at(*depth, place.size, {.column = column, .row = row});
        return there >= kStyleSky * kHalf ? 1.0F : 1.0F - (z / there);
    };
    const auto ring_stands = [&](int radius) {
        float best = 0.0F;
        for (std::size_t pair = 0; pair < kRingPairs; ++pair) {
            const std::array<int, 2>& way = core::at(kRing, pair);
            const int across = core::at(way, 0) * radius;
            const int down = core::at(way, 1) * radius;
            best = std::max(best, std::min(behind(place.at.column + across, place.at.row + down),
                                           behind(place.at.column - across, place.at.row - down)));
        }
        return best;
    };
    warmth.stands = smoothstep(kStandsLow, kStandsHigh,
                               std::max(ring_stands(parameters.ring), ring_stands(parameters.ring * kWideRing)));
    return warmth;
}

StyleRgb thermal_pixel(const Place& place, const Colours& colours, const std::vector<float>* depth)
{
    const StyleParameters& parameters = *place.parameters;
    const StyleRgb& own = colours.own;
    const StyleRgb& soft = colours.base;
    const float bright = (core::at(soft, 0) * core::at(kLuma, 0)) + (core::at(soft, 1) * core::at(kLuma, 1)) +
                         (core::at(soft, 2) * core::at(kLuma, 2));
    const Warmth warmth = warmth_at(place, depth);
    float heat = warmth.sky ? kSkyHeat + (kSkyBright * bright)
                            : kHeatFloor + (kHeatBright * bright) + (kHeatNear * warmth.near) +
                                  (parameters.focus * kHeatStands * warmth.stands);
    const std::array<uint32_t, 2> cell = grain_cell(place);
    heat += parameters.grain * ((kNoisePixel * (unit(hash3(core::at(cell, 0), core::at(cell, 1), parameters.seed)) - kHalf)) +
                                (kNoiseLine * (unit(hash3(kLineColumn, core::at(cell, 1), parameters.seed)) - kHalf)) +
                                (kNoiseFrame * (unit(hash3(kFrameColumn, 0, parameters.seed)) - kHalf)));
    heat *= vignette_at(place, parameters.vignette * kThermalVignette);
    return mix3(own, heat_colour(heat), parameters.strength);
}

} // namespace

bool look_reads_depth(ArtStyle look)
{
    return look == ArtStyle::kToon || look == ArtStyle::kComic || look == ArtStyle::kSketch || look == ArtStyle::kThermal;
}

bool look_draws_ink(ArtStyle look) { return look == ArtStyle::kComic || look == ArtStyle::kSketch; }

bool look_draws_lines(ArtStyle look) { return look == ArtStyle::kToon || look_draws_ink(look); }

bool look_reads_half(ArtStyle look) { return look == ArtStyle::kNoir || look == ArtStyle::kThermal; }

void set_look_parameters(const StyleSettings& settings, const StyleHeights& heights, StyleParameters* parameters)
{
    const auto finite_or = [](float value, float fallback, float low, float high) {
        return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
    };
    constexpr float kMostPattern = 16.0F;
    constexpr float kLeastHatch = 2.0F;
    constexpr float kLeastContrast = 0.5F;
    constexpr float kMostContrast = 4.0F;
    constexpr float kMostWidth = 4.0F;
    constexpr float kLeastRing = 2.0F;
    const float output_scale = static_cast<float>(heights.output) / kBaseHeight;
    const float depth_scale = static_cast<float>(heights.depth) / kBaseHeight;
    parameters->style = settings.style;
    parameters->cell = std::max(1.0F, std::round(output_scale));
    parameters->grain = finite_or(settings.grain, kStyleGrain, 0.0F, 1.0F);
    parameters->vignette = finite_or(settings.vignette, kStyleVignette, 0.0F, 1.0F);
    parameters->paper = finite_or(settings.paper, kStylePaper, 0.0F, 1.0F);
    parameters->wash = finite_or(settings.wash, kStyleWash, 0.0F, 1.0F);
    parameters->contrast = finite_or(settings.contrast, kStyleContrast, kLeastContrast, kMostContrast);
    parameters->focus = finite_or(settings.focus, kStyleFocus, 0.0F, 1.0F);
    for (std::size_t channel = 0; channel < parameters->dark.size(); ++channel) {
        core::at(parameters->dark, channel) =
            finite_or(core::at(settings.noir_dark, channel), core::at(kStyleNoirDark, channel), 0.0F, 1.0F);
        core::at(parameters->light, channel) =
            finite_or(core::at(settings.noir_light, channel), core::at(kStyleNoirLight, channel), 0.0F, 1.0F);
    }
    const float width = finite_or(settings.outline_width, 1.0F, 0.0F, kMostWidth);
    const auto reach = [&](float share) {
        return std::clamp(static_cast<int>(std::round(width * depth_scale * share)), 0, kMostInk);
    };
    switch (settings.style) {
    case ArtStyle::kComic: {
        parameters->ink = reach(kComicInkReach);
        const float halftone = finite_or(settings.halftone, kStyleHalftone, 0.0F, kMostPattern);
        parameters->halftone = halftone > 0.0F ? std::max(kLeastPattern, halftone * output_scale) : 0.0F;
        break;
    }
    case ArtStyle::kSketch:
        parameters->ink = reach(kSketchInkReach);
        parameters->hatch = std::max(kLeastPattern, finite_or(settings.hatch, kStyleHatch, kLeastHatch, kMostPattern) * output_scale);
        break;
    case ArtStyle::kNoir:
        parameters->radius = 0;
        parameters->ring = static_cast<int>(std::max(1.0F, std::round(kNoirReach * output_scale * kHalf)));
        break;
    case ArtStyle::kThermal:
        parameters->radius = 0;
        parameters->ring = static_cast<int>(std::max(kLeastRing, std::round(kThermalRing * depth_scale)));
        break;
    case ArtStyle::kOff:
    case ArtStyle::kToon:
    case ArtStyle::kVhs:
    case ArtStyle::kRetro:
    case ArtStyle::kWatercolour:
        break;
    }
}

LookConstants look_constants(const StyleParameters& parameters)
{
    LookConstants look;
    look.softness = parameters.style == ArtStyle::kComic ? kComicSoftness : kStyleBandSoftness * kHalf;
    look.a = {parameters.grain, parameters.cell, static_cast<float>(parameters.seed), parameters.vignette};
    switch (parameters.style) {
    case ArtStyle::kComic:
        look.b = {parameters.halftone, parameters.paper, static_cast<float>(parameters.ink), 0.0F};
        break;
    case ArtStyle::kSketch:
        look.b = {parameters.hatch, parameters.paper, static_cast<float>(parameters.ink), parameters.wash};
        break;
    case ArtStyle::kNoir:
        look.b = {parameters.contrast, static_cast<float>(parameters.ring), 0.0F, 0.0F};
        look.c = {core::at(parameters.dark, 0), core::at(parameters.dark, 1), core::at(parameters.dark, 2), 0.0F};
        look.d = {core::at(parameters.light, 0), core::at(parameters.light, 1), core::at(parameters.light, 2), 0.0F};
        break;
    case ArtStyle::kThermal:
        look.b = {parameters.focus, static_cast<float>(parameters.ring), kThermalNear, kThermalFar};
        break;
    case ArtStyle::kOff:
    case ArtStyle::kToon:
    case ArtStyle::kVhs:
    case ArtStyle::kRetro:
    case ArtStyle::kWatercolour:
        break;
    }
    return look;
}

const char* style_look_shared_glsl()
{
    static const std::string kShared = glsl_constants() + kLookShared;
    return kShared.c_str();
}

const char* style_look_body(ArtStyle look)
{
    switch (look) {
    case ArtStyle::kComic:
        return kComicBody;
    case ArtStyle::kSketch:
        return kSketchBody;
    case ArtStyle::kNoir:
        return kNoirBody;
    case ArtStyle::kThermal:
        return kThermalBody;
    case ArtStyle::kOff:
    case ArtStyle::kToon:
    case ArtStyle::kVhs:
    case ArtStyle::kRetro:
    case ArtStyle::kWatercolour:
        break;
    }
    return "void main() { o_colour = vec4(finite3(texelFetch(u_colour, ivec2(gl_FragCoord.xy), 0).rgb), 1.0); }\n";
}

std::vector<StyleRgb> style_look_reference(const StyleParameters& parameters, const StyleImages& images, Extent size)
{
    const std::vector<StyleRgb>& colour = *images.colour;
    const Extent half_size{.width = (size.width + 1) / 2, .height = (size.height + 1) / 2};
    std::vector<StyleRgb> half;
    if (images.smoothed != nullptr && !images.smoothed->empty()) {
        half = *images.smoothed;
    } else if (parameters.radius > 0) {
        half = style_kuwahara(style_half(colour, size), half_size, parameters.radius);
    } else if (look_reads_half(parameters.style)) {
        half = style_half(colour, size);
    }
    const bool has_depth = images.depth != nullptr && !images.depth->empty();
    std::vector<float> edges;
    if (has_depth && look_draws_ink(parameters.style)) {
        edges = style_edges(parameters, *images.depth, size);
    }
    const std::vector<float>* depth = has_depth ? images.depth : nullptr;
    const std::vector<float>* lines = edges.empty() ? nullptr : &edges;
    std::vector<StyleRgb> out(colour.size());
    for (uint32_t row = 0; row < size.height; ++row) {
        for (uint32_t column = 0; column < size.width; ++column) {
            const std::size_t at = (static_cast<std::size_t>(row) * size.width) + column;
            const Place place{.parameters = &parameters,
                              .size = size,
                              .at = {.column = static_cast<int>(column), .row = static_cast<int>(row)}};
            const StyleRgb own = finite3(core::at(colour, at));
            const StyleRgb base = parameters.radius > 0 ? upsampled(half, half_size, place.at, own) : own;
            StyleRgb result = own;
            switch (parameters.style) {
            case ArtStyle::kComic:
                result = comic_pixel(place, {.own = own, .base = base}, lines);
                break;
            case ArtStyle::kSketch:
                result = sketch_pixel(place, {.own = own, .base = base}, lines);
                break;
            case ArtStyle::kNoir:
                result = noir_pixel(place, own, half);
                break;
            case ArtStyle::kThermal:
                result = thermal_pixel(place, {.own = own, .base = upsampled(half, half_size, place.at, own, false)}, depth);
                break;
            case ArtStyle::kOff:
            case ArtStyle::kToon:
            case ArtStyle::kVhs:
            case ArtStyle::kRetro:
            case ArtStyle::kWatercolour:
                break;
            }
            core::at(out, at) = result;
        }
    }
    return out;
}

} // namespace render::vulkan
