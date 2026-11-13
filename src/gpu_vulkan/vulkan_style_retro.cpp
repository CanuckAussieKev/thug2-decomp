// NOT ORIGINAL - the retro art style's picture: the low resolution, the limited palette, the ordered dither
// (vulkan_style_film.h; the geometry - snapped vertices, affine mapping - is the fixed-function shaders' VertexKey::retro,
// ff_shaders.cpp). One full-screen pass; retro_reference is its twin on the processor.
//
// FilmConstants:
//   info: x, y the cells across and down (retro_cells), z the levels a channel has less one (2^bits - 1);
//   a: x the dither (0 - 1), y the strength.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_formats.h"
#include "gpu_vulkan/vulkan_style_kit.h"
#include "gpu_vulkan/vulkan_style_film.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

constexpr uint64_t kReportPipeline = 0x53545921U;
constexpr uint64_t kReportFirstPass = 0x53545922U;
constexpr float kLeastBits = 2.0F;
constexpr float kMostBits = 8.0F;
constexpr int kBayerSide = 4;
constexpr float kBayerSteps = 16.0F;
constexpr std::array<float, 16> kBayer{0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

constexpr const char* kBody = R"(
layout(location = 0) out vec4 o_colour;

const float kBayer[16] = float[](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 local = p - pc.rect.xy;
    ivec2 cells = pc.info.xy;
    // the cell, and the pixel at its middle (integers: exact)
    ivec2 cell = min(local * cells / pc.rect.zw, cells - 1);
    ivec2 middle = min((2 * cell + 1) * pc.rect.zw / (2 * cells), pc.rect.zw - 1);
    vec3 c = finite3(texelFetch(u_source, pc.rect.xy + middle, 0).rgb);
    float levels = float(pc.info.z);
    float threshold = (kBayer[(cell.y & 3) * 4 + (cell.x & 3)] + 0.5) / 16.0;
    vec3 quantised = clamp(floor(c * levels + mix(0.5, threshold, pc.a.x)) / levels, 0.0, 1.0);
    vec3 own = finite3(texelFetch(u_source, p, 0).rgb);
    o_colour = vec4(mix(own, quantised, pc.a.y), 1.0);
}
)";

struct RetroState {
    std::vector<film_kit::FragmentPipeline> pipelines;
    bool failed = false;
};

RetroState& retro()
{
    static RetroState state;
    return state;
}

} // namespace

FilmConstants retro_constants(const RetroSettings& settings, float strength, const std::array<uint32_t, 4>& shown)
{
    FilmConstants constants;
    constants.rect = {static_cast<int32_t>(core::at(shown, 0)), static_cast<int32_t>(core::at(shown, 1)),
                      static_cast<int32_t>(core::at(shown, 2)), static_cast<int32_t>(core::at(shown, 3))};
    const std::array<int32_t, 2> cells = retro_cells(settings.lines, {.width = core::at(shown, 2), .height = core::at(shown, 3)});
    const float bits = std::isfinite(settings.bits) ? std::clamp(std::round(settings.bits), kLeastBits, kMostBits) : kRetroBits;
    const int32_t levels = (1 << static_cast<int32_t>(bits)) - 1;
    constants.info = {core::at(cells, 0), core::at(cells, 1), levels, 0};
    const float dither = std::isfinite(settings.dither) ? std::clamp(settings.dither, 0.0F, 1.0F) : 1.0F;
    core::at(constants.values, 0) = {dither, std::clamp(strength, 0.0F, 1.0F), 0.0F, 0.0F};
    return constants;
}

std::vector<FilmRgb> retro_reference(const FilmConstants& constants, const std::vector<FilmRgb>& colour)
{
    constexpr float kHalf = 0.5F;
    const Extent size = film_kit::size_of(constants);
    const int32_t width = core::at(constants.rect, 2);
    const int32_t height = core::at(constants.rect, 3);
    const int32_t across = core::at(constants.info, 0);
    const int32_t down = core::at(constants.info, 1);
    const auto levels = static_cast<float>(core::at(constants.info, 2));
    const float dither = core::at(core::at(constants.values, 0), 0);
    const float strength = core::at(core::at(constants.values, 0), 1);
    std::vector<FilmRgb> out(colour.size());
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t column = 0; column < width; ++column) {
            const int32_t cell_x = std::min(column * across / width, across - 1);
            const int32_t cell_y = std::min(row * down / height, down - 1);
            const int32_t middle_x = std::min(((2 * cell_x) + 1) * width / (2 * across), width - 1);
            const int32_t middle_y = std::min(((2 * cell_y) + 1) * height / (2 * down), height - 1);
            const FilmRgb sample = film_kit::finite3(film_kit::texel(colour, size, middle_x, middle_y));
            const std::size_t bayer = (static_cast<std::size_t>(cell_y & (kBayerSide - 1)) * kBayerSide) +
                                      static_cast<std::size_t>(cell_x & (kBayerSide - 1));
            const float threshold = (core::at(kBayer, bayer) + kHalf) / kBayerSteps;
            const float offset = film_kit::mix(kHalf, threshold, dither);
            FilmRgb quantised{};
            for (std::size_t channel = 0; channel < quantised.size(); ++channel) {
                core::at(quantised, channel) =
                    std::clamp(std::floor((core::at(sample, channel) * levels) + offset) / levels, 0.0F, 1.0F);
            }
            const std::size_t at = (static_cast<std::size_t>(row) * size.width) + static_cast<std::size_t>(column);
            core::at(out, at) = film_kit::mix3(film_kit::finite3(core::at(colour, at)), quantised, strength);
        }
    }
    return out;
}

namespace film_kit {

bool record_retro(Backend& state, const RetroSettings& settings, float strength, const FilmTarget& target)
{
    RetroState& own = retro();
    if (own.failed) {
        return false;
    }
    std::string error;
    const VkPipeline pipeline = ensure_common(state, &error)
                                    ? fragment_pipeline(state, &own.pipelines, std::string(glsl_common()) + kBody,
                                                        target.colour->format, target.colour->samples, &error)
                                    : VK_NULL_HANDLE;
    if (pipeline == VK_NULL_HANDLE) {
        own.failed = true;
        if (first_report(state, kReportPipeline)) {
            get_log("vulkan: rt_style retro: the pass couldn't be made (%s) - no art style", error.c_str());
        }
        return false;
    }
    const FilmConstants constants = retro_constants(settings, strength, target.shown);
    const VkCommandBuffer commands = begin(state, target, constants, {});
    if (commands == VK_NULL_HANDLE) {
        return false;
    }
    draw(commands, pipeline, {.source = source()}, target);
    end(state, commands, target, "retro", {});
    if (first_report(state, kReportFirstPass)) {
        get_log("vulkan: rt_style retro: the first pass (%ux%u, %u sample%s; %dx%d cells, %d levels a channel, dither "
                "%.2f, vertex snap %.2f, affine %.2f, strength %.2f)",
                core::at(target.shown, 2), core::at(target.shown, 3), static_cast<unsigned>(target.colour->samples),
                target.colour->samples == 1 ? "" : "s", core::at(constants.info, 0), core::at(constants.info, 1),
                core::at(constants.info, 2) + 1, static_cast<double>(core::at(core::at(constants.values, 0), 0)),
                static_cast<double>(settings.snap), static_cast<double>(settings.affine), static_cast<double>(strength));
    }
    return true;
}

void release_retro(Backend& state)
{
    RetroState& own = retro();
    destroy_pipelines(state, &own.pipelines);
    own.failed = false;
}

} // namespace film_kit

} // namespace render::vulkan
