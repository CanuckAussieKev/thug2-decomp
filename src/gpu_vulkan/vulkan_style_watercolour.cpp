// NOT ORIGINAL - the watercolour art style: a wash painting on paper (vulkan_style_film.h; docs/design/vulkan.md "Art
// style"). Two compute passes at half resolution (the colour, the paint) and a full-screen composite;
// watercolour_paint / watercolour_reference are their twins on the processor.
//
// FilmConstants:
//   info: x, y the half-resolution size (the viewport's, rounded up), z the paint's tap spacing (half-resolution texels;
//         0: the half image as it is);
//   a: x s (the viewport's height / 720), y the flow's reach (pixels), z the flow's cell (pixels), w the granulation;
//   b: x the wet edges, y the paper (the highlights' wash), z the graphite line, w the strength;
//   c: x the paint's colour weight (exp(-k |difference|^2)).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_formats.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_style_kit.h"
#include "gpu_vulkan/vulkan_style_film.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

constexpr uint64_t kReportPipeline = 0x53545931U;
constexpr uint64_t kReportFirstPass = 0x53545932U;

constexpr float kBaseHeight = 720.0F;
constexpr float kSpacing = 2.0F;       // the paint's tap spacing at rt_style_watercolour_bleed 1 (half texels at 720 lines)
constexpr int32_t kMostSpacing = 8;
constexpr float kFlowReach = 3.0F;     // pixels at 720 lines, flow 1
constexpr float kFlowCell = 48.0F;     // the flow's noise cell (pixels at 720 lines)
constexpr float kColourWeight = 30.0F; // the bilateral's
constexpr int kTaps = 2;               // the paint's taps each way (5 x 5)
constexpr float kTapFalloff = 0.3F;    // exp(-k (dx^2 + dy^2))
constexpr float kEdgeStep = 2.0F;      // the wet edge's gradient step (pixels at 720 lines... times s)
constexpr float kEdgeLow = 0.03F;
constexpr float kEdgeHigh = 0.3F;
constexpr float kEdgeDensity = 0.9F;
constexpr float kGrainDensity = 0.8F;
constexpr float kFineCell = 2.5F;      // the paper's grain (pixels at 720 lines)
constexpr float kCoarseCell = 14.0F;
constexpr float kFineShare = 0.55F;
constexpr float kWashLow = 0.6F;
constexpr float kWashHigh = 0.92F;
constexpr std::array<float, 3> kPaper{0.97F, 0.95F, 0.9F};
constexpr float kPaperShade = 0.05F;
constexpr float kLineBack = 0.7F;      // the line read from the place moved this far the other way
constexpr float kLineLow = 0.12F;
constexpr float kLineHigh = 0.35F;
constexpr std::array<float, 3> kGraphite{0.3F, 0.28F, 0.27F};
constexpr uint32_t kFlowSeedX = 1U;
constexpr uint32_t kFlowSeedY = 2U;
constexpr uint32_t kFineSeed = 11U;
constexpr uint32_t kCoarseSeed = 23U;

constexpr const char* kDownBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 3, rgba16f) uniform writeonly image2D o_out;

void main()
{
    ivec2 q = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(q, pc.info.xy))) {
        return;
    }
    ivec2 last = pc.rect.xy + pc.rect.zw - 1;
    ivec2 base = pc.rect.xy + q * 2;
    vec3 sum = finite3(texelFetch(u_source, min(base, last), 0).rgb);
    sum += finite3(texelFetch(u_source, min(base + ivec2(1, 0), last), 0).rgb);
    sum += finite3(texelFetch(u_source, min(base + ivec2(0, 1), last), 0).rgb);
    sum += finite3(texelFetch(u_source, min(base + ivec2(1, 1), last), 0).rgb);
    imageStore(o_out, q, vec4(sum * 0.25, 1.0));
}
)";

constexpr const char* kPaintBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 3, rgba16f) uniform writeonly image2D o_out;

void main()
{
    ivec2 q = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(q, pc.info.xy))) {
        return;
    }
    ivec2 last = pc.info.xy - 1;
    int spacing = pc.info.z;
    vec3 centre = texelFetch(u_a, q, 0).rgb;
    vec3 total = vec3(0.0);
    float weights = 0.0;
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            vec3 t = texelFetch(u_a, clamp(q + ivec2(dx, dy) * spacing, ivec2(0), last), 0).rgb;
            vec3 d = t - centre;
            float w = exp(-0.3 * float(dx * dx + dy * dy) - pc.c.x * dot(d, d));
            total += t * w;
            weights += w;
        }
    }
    imageStore(o_out, q, vec4(total / weights, 1.0));
}
)";

constexpr const char* kCompositeBody = R"(
layout(location = 0) out vec4 o_colour;

vec3 paint_at(vec2 at)
{
    vec2 half_at = clamp(at * 0.5, vec2(0.5), vec2(pc.info.xy) - 0.5);
    return textureLod(u_b, half_at / vec2(textureSize(u_b, 0)), 0.0).rgb;
}
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
float gradient(vec2 at, float step)
{
    float gx = luma(paint_at(at + vec2(step, 0.0))) - luma(paint_at(at - vec2(step, 0.0)));
    float gy = luma(paint_at(at + vec2(0.0, step))) - luma(paint_at(at - vec2(0.0, step)));
    return sqrt(gx * gx + gy * gy);
}

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec2 local = vec2(p - pc.rect.xy) + 0.5;
    float s = pc.a.x;
    // the flow: the paint read off its place (the paper's noise: fixed to the screen)
    vec2 cell = local / pc.a.z;
    vec2 flow = vec2(value_noise(cell, 1u), value_noise(cell, 2u)) * 2.0 - 1.0;
    vec2 at = local + flow * pc.a.y;
    vec3 paint = paint_at(at);
    // the wet edge and the paper's grain: the pigment's density
    float edge = gradient(at, 2.0 * s);
    float paper = value_noise(local / (2.5 * s), 11u) * 0.55 + value_noise(local / (14.0 * s), 23u) * 0.45;
    float density = 1.0 + pc.b.x * smoothstep(0.03, 0.3, edge) * 0.9 + pc.a.w * (paper - 0.5) * 0.8;
    vec3 c = paint - (paint - paint * paint) * (density - 1.0);
    // the highlights washed out: the paper shows
    vec3 paper_colour = vec3(0.97, 0.95, 0.9) * (1.0 - 0.05 + 0.05 * paper);
    c = mix(c, paper_colour, pc.b.y * smoothstep(0.6, 0.92, luma(paint)));
    // a light graphite line along the paint's edges, read from a place moved the other way
    float line = smoothstep(0.12, 0.35, gradient(local - flow * pc.a.y * 0.7, s)) * pc.b.z;
    c = mix(c, c * vec3(0.3, 0.28, 0.27), line);
    c = clamp(c, 0.0, 1.0);
    vec3 own = finite3(texelFetch(u_source, p, 0).rgb);
    o_colour = vec4(mix(own, c, pc.b.w), 1.0);
}
)";

struct WatercolourState {
    VkPipeline down = VK_NULL_HANDLE;
    VkPipeline paint = VK_NULL_HANDLE;
    std::vector<film_kit::FragmentPipeline> composites;
    Image half;
    Image paint_image;
    bool failed = false;
};

WatercolourState& watercolour()
{
    static WatercolourState state;
    return state;
}

float knob(float value, float fallback, float most) { return std::isfinite(value) ? std::clamp(value, 0.0F, most) : fallback; }

Extent half_of(const FilmConstants& constants)
{
    return {.width = static_cast<uint32_t>(std::max(0, core::at(constants.info, 0))),
            .height = static_cast<uint32_t>(std::max(0, core::at(constants.info, 1)))};
}

float luma(const FilmRgb& colour)
{
    constexpr std::array<float, 3> kToY{0.299F, 0.587F, 0.114F};
    return (core::at(colour, 0) * core::at(kToY, 0)) + (core::at(colour, 1) * core::at(kToY, 1)) +
           (core::at(colour, 2) * core::at(kToY, 2));
}

} // namespace

FilmConstants watercolour_constants(const WatercolourSettings& settings, float strength, const std::array<uint32_t, 4>& shown)
{
    constexpr float kMostTwo = 2.0F;
    FilmConstants constants;
    constants.rect = {static_cast<int32_t>(core::at(shown, 0)), static_cast<int32_t>(core::at(shown, 1)),
                      static_cast<int32_t>(core::at(shown, 2)), static_cast<int32_t>(core::at(shown, 3))};
    const float scale = static_cast<float>(std::max(1U, core::at(shown, 3))) / kBaseHeight;
    const float bleed = knob(settings.bleed, 1.0F, kMostTwo);
    const auto spacing = std::clamp(static_cast<int32_t>(std::lround(bleed * kSpacing * scale)), bleed > 0.0F ? 1 : 0, kMostSpacing);
    constants.info = {static_cast<int32_t>((core::at(shown, 2) + 1) / 2), static_cast<int32_t>((core::at(shown, 3) + 1) / 2),
                      spacing, 0};
    auto& values = constants.values;
    core::at(values, 0) = {scale, knob(settings.flow, 1.0F, kMostTwo) * kFlowReach * scale, kFlowCell * scale,
                           knob(settings.granulation, 1.0F, kMostTwo)};
    core::at(values, 1) = {knob(settings.edges, 1.0F, kMostTwo), knob(settings.paper, kWatercolourPaper, 1.0F),
                           knob(settings.outlines, kWatercolourOutlines, 1.0F), std::clamp(strength, 0.0F, 1.0F)};
    core::at(values, 2) = {kColourWeight, 0.0F, 0.0F, 0.0F};
    return constants;
}

std::vector<FilmRgb> watercolour_paint(const FilmConstants& constants, const std::vector<FilmRgb>& colour)
{
    constexpr float kQuarter = 0.25F;
    const Extent size = film_kit::size_of(constants);
    const Extent half_size = half_of(constants);
    std::vector<FilmRgb> half(static_cast<std::size_t>(half_size.width) * half_size.height);
    for (uint32_t row = 0; row < half_size.height; ++row) {
        for (uint32_t column = 0; column < half_size.width; ++column) {
            FilmRgb sum{};
            for (uint32_t down = 0; down < 2; ++down) {
                for (uint32_t across = 0; across < 2; ++across) {
                    const FilmRgb texel = film_kit::finite3(film_kit::texel(colour, size, static_cast<int32_t>((column * 2) + across),
                                                                            static_cast<int32_t>((row * 2) + down)));
                    for (std::size_t channel = 0; channel < sum.size(); ++channel) {
                        core::at(sum, channel) += core::at(texel, channel);
                    }
                }
            }
            for (float& value : sum) {
                value *= kQuarter;
            }
            core::at(half, (static_cast<std::size_t>(row) * half_size.width) + column) = film_kit::as_half3(sum);
        }
    }
    const int32_t spacing = core::at(constants.info, 2);
    const float sharpness = core::at(core::at(constants.values, 2), 0);
    std::vector<FilmRgb> paint(half.size());
    for (uint32_t row = 0; row < half_size.height; ++row) {
        for (uint32_t column = 0; column < half_size.width; ++column) {
            const std::size_t at = (static_cast<std::size_t>(row) * half_size.width) + column;
            const FilmRgb& centre = core::at(half, at);
            FilmRgb total{};
            float weights = 0.0F;
            for (int dy = -kTaps; dy <= kTaps; ++dy) {
                for (int dx = -kTaps; dx <= kTaps; ++dx) {
                    const FilmRgb& tap = film_kit::texel(half, half_size, static_cast<int32_t>(column) + (dx * spacing),
                                                         static_cast<int32_t>(row) + (dy * spacing));
                    float distance = 0.0F;
                    for (std::size_t channel = 0; channel < tap.size(); ++channel) {
                        const float difference = core::at(tap, channel) - core::at(centre, channel);
                        distance += difference * difference;
                    }
                    const float weight =
                        std::exp((-kTapFalloff * static_cast<float>((dx * dx) + (dy * dy))) - (sharpness * distance));
                    for (std::size_t channel = 0; channel < tap.size(); ++channel) {
                        core::at(total, channel) += core::at(tap, channel) * weight;
                    }
                    weights += weight;
                }
            }
            for (float& value : total) {
                value /= weights;
            }
            core::at(paint, at) = film_kit::as_half3(total);
        }
    }
    return paint;
}

std::vector<FilmRgb> watercolour_reference(const FilmConstants& constants, const std::vector<FilmRgb>& colour,
                                           const std::vector<FilmRgb>* paint)
{
    constexpr float kHalf = 0.5F;
    constexpr float kTwo = 2.0F;
    const Extent size = film_kit::size_of(constants);
    const Extent half_size = half_of(constants);
    const bool given = paint != nullptr && paint->size() == static_cast<std::size_t>(half_size.width) * half_size.height;
    const std::vector<FilmRgb> made = given ? std::vector<FilmRgb>{} : watercolour_paint(constants, colour);
    const std::vector<FilmRgb>& pigment = given ? *paint : made;
    const Extent pigment_size = half_size;
    const auto& flow_values = core::at(constants.values, 0);
    const auto& paint_values = core::at(constants.values, 1);
    const float scale = core::at(flow_values, 0);
    const auto paint_at = [&](float x, float y) {
        const float half_x = std::clamp(x * kHalf, kHalf, static_cast<float>(half_size.width) - kHalf);
        const float half_y = std::clamp(y * kHalf, kHalf, static_cast<float>(half_size.height) - kHalf);
        return film_kit::bilinear(pigment, pigment_size, {.x = half_x, .y = half_y});
    };
    const auto gradient = [&](float x, float y, float step) {
        const float gx = luma(paint_at(x + step, y)) - luma(paint_at(x - step, y));
        const float gy = luma(paint_at(x, y + step)) - luma(paint_at(x, y - step));
        return std::sqrt((gx * gx) + (gy * gy));
    };
    std::vector<FilmRgb> out(colour.size());
    for (uint32_t row = 0; row < size.height; ++row) {
        for (uint32_t column = 0; column < size.width; ++column) {
            const float local_x = static_cast<float>(column) + kHalf;
            const float local_y = static_cast<float>(row) + kHalf;
            const float cell_x = local_x / core::at(flow_values, 2);
            const float cell_y = local_y / core::at(flow_values, 2);
            const float flow_x = (film_kit::value_noise({.x = cell_x, .y = cell_y}, kFlowSeedX) * kTwo) - 1.0F;
            const float flow_y = (film_kit::value_noise({.x = cell_x, .y = cell_y}, kFlowSeedY) * kTwo) - 1.0F;
            const float at_x = local_x + (flow_x * core::at(flow_values, 1));
            const float at_y = local_y + (flow_y * core::at(flow_values, 1));
            const FilmRgb paint_here = paint_at(at_x, at_y);
            const float edge = gradient(at_x, at_y, kEdgeStep * scale);
            const float paper =
                (film_kit::value_noise({.x = local_x / (kFineCell * scale), .y = local_y / (kFineCell * scale)}, kFineSeed) * kFineShare) +
                (film_kit::value_noise({.x = local_x / (kCoarseCell * scale), .y = local_y / (kCoarseCell * scale)}, kCoarseSeed) *
                 (1.0F - kFineShare));
            const float density = 1.0F + (core::at(paint_values, 0) * film_kit::smoothstep(kEdgeLow, kEdgeHigh, edge) * kEdgeDensity) +
                                  (core::at(flow_values, 3) * (paper - kHalf) * kGrainDensity);
            FilmRgb painted{};
            for (std::size_t channel = 0; channel < painted.size(); ++channel) {
                const float pigment_value = core::at(paint_here, channel);
                core::at(painted, channel) =
                    pigment_value - ((pigment_value - (pigment_value * pigment_value)) * (density - 1.0F));
            }
            const float shade = 1.0F - kPaperShade + (kPaperShade * paper);
            const FilmRgb paper_colour{core::at(kPaper, 0) * shade, core::at(kPaper, 1) * shade, core::at(kPaper, 2) * shade};
            painted = film_kit::mix3(painted, paper_colour, core::at(paint_values, 1) * film_kit::smoothstep(kWashLow, kWashHigh, luma(paint_here)));
            const float line = film_kit::smoothstep(kLineLow, kLineHigh,
                                                    gradient(local_x - (flow_x * core::at(flow_values, 1) * kLineBack),
                                                             local_y - (flow_y * core::at(flow_values, 1) * kLineBack), scale)) *
                               core::at(paint_values, 2);
            const FilmRgb drawn{core::at(painted, 0) * core::at(kGraphite, 0), core::at(painted, 1) * core::at(kGraphite, 1),
                                core::at(painted, 2) * core::at(kGraphite, 2)};
            painted = film_kit::mix3(painted, drawn, line);
            for (float& value : painted) {
                value = std::clamp(value, 0.0F, 1.0F);
            }
            const std::size_t at = (static_cast<std::size_t>(row) * size.width) + column;
            core::at(out, at) = film_kit::mix3(film_kit::finite3(core::at(colour, at)), painted, core::at(paint_values, 3));
        }
    }
    return out;
}

namespace film_kit {

namespace {

bool ensure_watercolour(Backend& state, const FilmTarget& target, VkPipeline* composite, std::string* error)
{
    WatercolourState& own = watercolour();
    if (!ensure_common(state, error)) {
        return false;
    }
    if (own.down == VK_NULL_HANDLE) {
        own.down = make_compute(state, std::string(glsl_common()) + kDownBody, error);
    }
    if (own.paint == VK_NULL_HANDLE && own.down != VK_NULL_HANDLE) {
        own.paint = make_compute(state, std::string(glsl_common()) + kPaintBody, error);
    }
    if (own.paint == VK_NULL_HANDLE) {
        return false;
    }
    *composite = fragment_pipeline(state, &own.composites, std::string(glsl_common()) + kCompositeBody,
                                   target.colour->format, target.colour->samples, error);
    const Extent half{.width = (target.colour->extent.width + 1) / 2, .height = (target.colour->extent.height + 1) / 2};
    if (*composite == VK_NULL_HANDLE || !ensure_work_image(state, &own.half, half) ||
        !ensure_work_image(state, &own.paint_image, half)) {
        if (error->empty()) {
            *error = "its images";
        }
        return false;
    }
    return true;
}

} // namespace

bool record_watercolour(Backend& state, const WatercolourSettings& settings, float strength, const FilmTarget& target)
{
    WatercolourState& own = watercolour();
    if (own.failed) {
        return false;
    }
    std::string error;
    VkPipeline composite = VK_NULL_HANDLE;
    if (!ensure_watercolour(state, target, &composite, &error)) {
        own.failed = true;
        if (first_report(state, kReportPipeline)) {
            get_log("vulkan: rt_style watercolour: the passes couldn't be made (%s) - no art style", error.c_str());
        }
        return false;
    }
    const FilmConstants constants = watercolour_constants(settings, strength, target.shown);
    const VkCommandBuffer commands = begin(state, target, constants, {&own.half, &own.paint_image});
    if (commands == VK_NULL_HANDLE) {
        return false;
    }
    const auto half_width = static_cast<uint32_t>(core::at(constants.info, 0));
    const auto half_height = static_cast<uint32_t>(core::at(constants.info, 1));
    dispatch(commands, own.down, {.source = source(), .out = &own.half}, half_width, half_height);
    dispatch(commands, own.paint, {.a = &own.half, .out = &own.paint_image}, half_width, half_height);
    draw(commands, composite, {.source = source(), .b = &own.paint_image}, target);
    end(state, commands, target, "watercolour", {&own.half, &own.paint_image});
    if (first_report(state, kReportFirstPass)) {
        get_log("vulkan: rt_style watercolour: the first pass (%ux%u, %u sample%s; paint taps %d half pixels apart, flow "
                "%.2f, granulation %.2f, edges %.2f, paper %.2f, outlines %.2f, strength %.2f)",
                core::at(target.shown, 2), core::at(target.shown, 3), static_cast<unsigned>(target.colour->samples),
                target.colour->samples == 1 ? "" : "s", core::at(constants.info, 2), static_cast<double>(settings.flow),
                static_cast<double>(settings.granulation), static_cast<double>(settings.edges),
                static_cast<double>(settings.paper), static_cast<double>(settings.outlines), static_cast<double>(strength));
    }
    return true;
}

bool read_watercolour_paint(Backend& state, std::vector<uint16_t>* halves, Extent* size)
{
    WatercolourState& own = watercolour();
    if (own.paint_image.image == VK_NULL_HANDLE) {
        return false;
    }
    constexpr std::size_t kTexelBytes = 4 * sizeof(uint16_t);
    std::vector<uint8_t> bytes(static_cast<std::size_t>(own.paint_image.extent.width) * own.paint_image.extent.height *
                               kTexelBytes);
    if (!state.memory.read_image_level(&own.paint_image, 0, &bytes)) {
        return false;
    }
    halves->resize(bytes.size() / sizeof(uint16_t));
    std::memcpy(halves->data(), bytes.data(), halves->size() * sizeof(uint16_t));
    *size = own.paint_image.extent;
    return true;
}

void release_watercolour(Backend& state)
{
    WatercolourState& own = watercolour();
    destroy_pipeline(state, &own.down);
    destroy_pipeline(state, &own.paint);
    destroy_pipelines(state, &own.composites);
    destroy_image(state, &own.half);
    destroy_image(state, &own.paint_image);
    own.failed = false;
}

} // namespace film_kit

} // namespace render::vulkan
