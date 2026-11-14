// NOT ORIGINAL - the fisheye lens' pass (vulkan_fisheye.h; docs/design/vulkan.md "Fisheye lens"): its shader, its
// constants and fisheye_reference, its processor twin - operation for operation. The lens' maths are
// engine/gfx/fisheye_lens.h's (the shader's angle_of is fisheye::angle_of).
#include "gpu_vulkan/vulkan_fisheye.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "engine/gfx/fisheye_lens.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_formats.h"
#include "gpu_vulkan/vulkan_style_film.h"
#include "gpu_vulkan/vulkan_style_kit.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

constexpr uint64_t kReportPipeline = 0xF15E'0001ULL;
constexpr uint64_t kReportFirstPass = 0xF15E'0002ULL;
constexpr int32_t kFlagFramed = 1;
constexpr float kHalf = 0.5F;
constexpr float kLeast = 1e-6F;
constexpr std::array<float, 3> kTaps{0.25F, 0.5F, 0.25F};

std::string number(float value)
{
    std::array<char, 32> text{};
    std::snprintf(text.data(), text.size(), "%.9g", static_cast<double>(value));
    std::string out = text.data();
    if (out.find_first_of(".e") == std::string::npos) {
        out += ".0";
    }
    return out;
}

// (after film_kit's common part: the constants, source_at, finite3)
constexpr const char* kBody = R"(
layout(location = 0) out vec4 o_colour;

const float kMostAngle = KMOST_ANGLE;
const float kVignetteDepth = KVIGNETTE_DEPTH;
const float kVignetteFrom = KVIGNETTE_FROM;
const float kTaps[3] = float[](0.25, 0.5, 0.25);

// fisheye_lens.cpp's angle_of: a radius in the picture (focal units) -> the angle off the axis
float angle_of(float radius, float s)
{
    float r = max(radius, 0.0);
    float angle = r;
    if (s < 0.5) {
        float k = 1.0 - 2.0 * s;
        if (k >= 1e-4) {
            angle = atan(k * r) / k;
        }
    } else {
        float k = 2.0 * s - 1.0;
        if (k >= 1e-4) {
            angle = asin(min(k * r, 1.0)) / k;
        }
    }
    return min(angle, kMostAngle);
}

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec2 size = vec2(pc.rect.zw);
    vec2 half_size = size * 0.5;
    vec2 local = vec2(p - pc.rect.xy) + 0.5;
    // 1. the lens
    vec2 focal = half_size / pc.a.xy;
    vec2 n = (local - half_size) / focal;
    float r = length(n);
    float plane = r > 1e-6 ? tan(angle_of(r, pc.a.w)) / r : 1.0;
    vec2 offset = n * plane * focal / pc.a.z;
    // 2. - 3. the fringes and the softness, along the line from the centre
    float edge = r / pc.b.x;
    float e2 = edge * edge;
    float fringe = pc.b.z * e2;
    float along = length(offset);
    vec2 radial = along > 1e-6 ? offset / along : vec2(0.0);
    vec2 spacing = radial * (pc.b.w * e2);
    vec3 rgb = vec3(0.0);
    for (int c = 0; c < 3; ++c) {
        vec2 at = half_size + offset * (1.0 + fringe * float(1 - c));
        float sum = 0.0;
        for (int i = 0; i < 3; ++i) {
            sum += kTaps[i] * source_at(at + spacing * float(i - 1))[c];
        }
        rgb[c] = sum;
    }
    // 4. the vignette, the frame
    float shade = 1.0 - pc.b.y * kVignetteDepth * smoothstep(kVignetteFrom, 1.0, edge);
    if ((pc.info.x & 1) != 0) {
        shade *= clamp((pc.b.x - r) * focal.y / pc.c.x + 0.5, 0.0, 1.0);
    }
    o_colour = vec4(finite3(rgb * shade), 1.0);
}
)";

std::string shader_source()
{
    std::string body = kBody;
    const auto put = [&body](const std::string& name, const std::string& value) {
        for (std::size_t at = body.find(name); at != std::string::npos; at = body.find(name, at + value.size())) {
            body.replace(at, name.size(), value);
        }
    };
    put("KMOST_ANGLE", number(fisheye::kMostAngle));
    put("KVIGNETTE_DEPTH", number(kFisheyeVignetteDepth));
    put("KVIGNETTE_FROM", number(kFisheyeVignetteFrom));
    return std::string(film_kit::glsl_common()) + body;
}

struct FisheyeState {
    std::vector<film_kit::FragmentPipeline> pipelines;
    std::string source;
    bool failed = false;
};

FisheyeState& fisheye_state()
{
    static FisheyeState state;
    return state;
}

} // namespace

FilmConstants fisheye_constants(const rt::ViewDrawn::Lens& lens, float vignette, const std::array<uint32_t, 4>& shown)
{
    FilmConstants constants;
    constants.rect = {static_cast<int32_t>(core::at(shown, 0)), static_cast<int32_t>(core::at(shown, 1)),
                      static_cast<int32_t>(core::at(shown, 2)), static_cast<int32_t>(core::at(shown, 3))};
    const float s = std::clamp(lens.strength, 0.0F, 1.0F);
    const auto height = static_cast<float>(core::at(shown, 3));
    constants.info = {lens.framed ? kFlagFramed : 0, 0, 0, 0};
    auto& values = constants.values;
    core::at(values, 0) = {std::max(core::at(lens.tangents, 0), kLeast), std::max(core::at(lens.tangents, 1), kLeast),
                           std::max(lens.widen, 1.0F), s};
    core::at(values, 1) = {std::max(lens.reach, kLeast), std::clamp(vignette, 0.0F, 1.0F), kFisheyeFringe * s,
                           kFisheyeSoftShare * height * s};
    core::at(values, 2) = {std::max(1.0F, kFisheyeEdgeShare * height), 0.0F, 0.0F, 0.0F};
    return constants;
}

std::vector<FilmRgb> fisheye_reference(const FilmConstants& constants, const std::vector<FilmRgb>& colour)
{
    const Extent size = film_kit::size_of(constants);
    const auto width = static_cast<float>(size.width);
    const auto height = static_cast<float>(size.height);
    const auto& a = core::at(constants.values, 0);
    const auto& b = core::at(constants.values, 1);
    const auto& c = core::at(constants.values, 2);
    const float half_x = width * kHalf;
    const float half_y = height * kHalf;
    const float focal_x = half_x / core::at(a, 0);
    const float focal_y = half_y / core::at(a, 1);
    const bool framed = (core::at(constants.info, 0) & kFlagFramed) != 0;
    const auto source_at = [&](float x, float y) {
        const float clamped_x = std::clamp(x, kHalf, width - kHalf);
        const float clamped_y = std::clamp(y, kHalf, height - kHalf);
        return film_kit::finite3(film_kit::bilinear(colour, size, {.x = clamped_x, .y = clamped_y}));
    };
    std::vector<FilmRgb> out(colour.size());
    for (uint32_t row = 0; row < size.height; ++row) {
        for (uint32_t column = 0; column < size.width; ++column) {
            const float local_x = static_cast<float>(column) + kHalf;
            const float local_y = static_cast<float>(row) + kHalf;
            // 1. the lens
            const float n_x = (local_x - half_x) / focal_x;
            const float n_y = (local_y - half_y) / focal_y;
            const float r = std::sqrt((n_x * n_x) + (n_y * n_y));
            const float plane = r > kLeast ? std::tan(fisheye::angle_of(r, core::at(a, 3))) / r : 1.0F;
            const float offset_x = n_x * plane * focal_x / core::at(a, 2);
            const float offset_y = n_y * plane * focal_y / core::at(a, 2);
            // 2. - 3. the fringes and the softness
            const float edge = r / core::at(b, 0);
            const float e2 = edge * edge;
            const float fringe = core::at(b, 2) * e2;
            const float along = std::sqrt((offset_x * offset_x) + (offset_y * offset_y));
            const float radial_x = along > kLeast ? offset_x / along : 0.0F;
            const float radial_y = along > kLeast ? offset_y / along : 0.0F;
            const float step_x = radial_x * (core::at(b, 3) * e2);
            const float step_y = radial_y * (core::at(b, 3) * e2);
            FilmRgb rgb{};
            for (std::size_t channel = 0; channel < rgb.size(); ++channel) {
                const float scale = 1.0F + (fringe * static_cast<float>(1 - static_cast<int>(channel)));
                const float at_x = half_x + (offset_x * scale);
                const float at_y = half_y + (offset_y * scale);
                float sum = 0.0F;
                for (std::size_t tap = 0; tap < kTaps.size(); ++tap) {
                    const auto t = static_cast<float>(static_cast<int>(tap) - 1);
                    const FilmRgb sample = source_at(at_x + (step_x * t), at_y + (step_y * t));
                    sum += core::at(kTaps, tap) * core::at(sample, channel);
                }
                core::at(rgb, channel) = sum;
            }
            // 4. the vignette, the frame
            float shade = 1.0F - (core::at(b, 1) * kFisheyeVignetteDepth * film_kit::smoothstep(kFisheyeVignetteFrom, 1.0F, edge));
            if (framed) {
                shade *= std::clamp(((core::at(b, 0) - r) * focal_y / core::at(c, 0)) + kHalf, 0.0F, 1.0F);
            }
            for (float& channel : rgb) {
                channel *= shade;
            }
            core::at(out, (static_cast<std::size_t>(row) * size.width) + column) = film_kit::finite3(rgb);
        }
    }
    return out;
}

bool record_fisheye(Backend& state, const FilmConstants& constants, const FilmTarget& target)
{
    FisheyeState& own = fisheye_state();
    if (own.failed) {
        return false;
    }
    if (own.source.empty()) {
        own.source = shader_source();
    }
    std::string error;
    const VkPipeline pipeline =
        film_kit::ensure_common(state, &error)
            ? film_kit::fragment_pipeline(state, &own.pipelines, own.source, target.colour->format, target.colour->samples, &error)
            : VK_NULL_HANDLE;
    if (pipeline == VK_NULL_HANDLE) {
        own.failed = true;
        if (film_kit::first_report(state, kReportPipeline)) {
            get_log("vulkan: rt_fisheye: the pass couldn't be made (%s) - no lens", error.c_str());
        }
        return false;
    }
    const VkCommandBuffer commands = film_kit::begin(state, target, constants, {});
    if (commands == VK_NULL_HANDLE) {
        return false;
    }
    film_kit::draw(commands, pipeline, {.source = film_kit::source()}, target);
    film_kit::end(state, commands, target, "fisheye", {});
    if (film_kit::first_report(state, kReportFirstPass)) {
        const auto& a = core::at(constants.values, 0);
        const auto& b = core::at(constants.values, 1);
        get_log("vulkan: rt_fisheye: the first pass (%ux%u; strength %.2f, the view widened x%.3f, reach %.3f%s, vignette "
                "%.2f)",
                core::at(target.shown, 2), core::at(target.shown, 3), static_cast<double>(core::at(a, 3)),
                static_cast<double>(core::at(a, 2)), static_cast<double>(core::at(b, 0)),
                (core::at(constants.info, 0) & kFlagFramed) != 0 ? ", framed" : "", static_cast<double>(core::at(b, 1)));
    }
    return true;
}

void release_fisheye(Backend& state)
{
    FisheyeState& own = fisheye_state();
    film_kit::destroy_pipelines(state, &own.pipelines);
    own.failed = false;
}

} // namespace render::vulkan
