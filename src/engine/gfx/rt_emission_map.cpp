// NOT ORIGINAL - (round SUBPIX) a surface's emission map (rt_emission_map.h).
#include "engine/gfx/rt_emission_map.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstddef>
#include <utility>

#include "base/core/checked_index.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/relit_data.h"
#include "engine/gfx/rt_display.h"
#include "engine/gfx/rt_lights.h"

namespace render::rt {

namespace {

constexpr float kByte = 1.0F / 255.0F;

// The texels of a row (or column) `from` texels long that the texel `at` of one `to` long covers, each with the share of
// it covered (the footprint [at x from / to, (at + 1) x from / to), in texels of the larger) - the box filter's weights.
std::vector<std::pair<uint32_t, double>> footprint(uint32_t at, uint32_t from, uint32_t to)
{
    std::vector<std::pair<uint32_t, double>> out;
    const double scale = static_cast<double>(from) / static_cast<double>(to);
    const double begin = static_cast<double>(at) * scale;
    const double end = static_cast<double>(at + 1) * scale;
    for (auto i = static_cast<uint32_t>(begin); i < from && static_cast<double>(i) < end; ++i) {
        const double covered = std::min(end, static_cast<double>(i) + 1.0) - std::max(begin, static_cast<double>(i));
        if (covered > 0.0) {
            out.emplace_back(i, covered / scale);
        }
    }
    return out;
}

EmissionLevel next_level(const EmissionLevel& above)
{
    EmissionLevel out;
    out.width = std::max(above.width / 2, 1U);
    out.height = std::max(above.height / 2, 1U);
    out.texels.assign(static_cast<std::size_t>(out.width) * out.height, std::array<float, 3>{});
    for (uint32_t y = 0; y < out.height; ++y) {
        const auto rows = footprint(y, above.height, out.height);
        for (uint32_t x = 0; x < out.width; ++x) {
            const auto columns = footprint(x, above.width, out.width);
            std::array<double, 3> sum{};
            for (const auto& [row, row_share] : rows) {
                for (const auto& [column, column_share] : columns) {
                    const std::array<float, 3>& texel =
                        core::at(above.texels, (static_cast<std::size_t>(row) * above.width) + column);
                    for (std::size_t k = 0; k < 3; ++k) {
                        core::at(sum, k) += row_share * column_share * static_cast<double>(core::at(texel, k));
                    }
                }
            }
            std::array<float, 3>& made = core::at(out.texels, (static_cast<std::size_t>(y) * out.width) + x);
            for (std::size_t k = 0; k < 3; ++k) {
                core::at(made, k) = static_cast<float>(core::at(sum, k));
            }
        }
    }
    return out;
}

} // namespace

std::array<float, 3> emission_texel(const std::array<uint8_t, 4>& texel, uint32_t code)
{
    const std::array<float, 3> linear{linear_from_display(static_cast<float>(std::get<0>(texel)) * kByte),
                                      linear_from_display(static_cast<float>(std::get<1>(texel)) * kByte),
                                      linear_from_display(static_cast<float>(std::get<2>(texel)) * kByte)};
    const float alpha = static_cast<float>(std::get<3>(texel)) * kByte;
    return lamp_texel_coded(linear, alpha, code) ? linear : std::array<float, 3>{};
}

std::vector<EmissionLevel> emission_map(const texel::Image& image, uint32_t code)
{
    std::vector<EmissionLevel> levels;
    EmissionLevel first;
    if (image.empty() || image.width == 0 || image.height == 0 ||
        image.texels.size() < static_cast<std::size_t>(image.width) * image.height) {
        first.width = 1;
        first.height = 1;
        first.texels.push_back(emission_texel({255, 255, 255, 255}, code));
    } else {
        first.width = image.width;
        first.height = image.height;
        first.texels.reserve(static_cast<std::size_t>(image.width) * image.height);
        for (std::size_t i = 0; i < static_cast<std::size_t>(image.width) * image.height; ++i) {
            first.texels.push_back(emission_texel(core::at(image.texels, i), code));
        }
    }
    levels.push_back(std::move(first));
    while (levels.back().width > 1 || levels.back().height > 1) {
        levels.push_back(next_level(levels.back()));
    }
    return levels;
}

float emission_splat_share(float drawn_pixels)
{
    // (log-linear between the two areas: 1 at kSplatWholeAtMost, 0 at kSplatNoneAtLeast)
    if (!(drawn_pixels > kSplatWholeAtMost)) {
        return 1.0F;
    }
    return std::clamp(std::log2(kSplatNoneAtLeast / drawn_pixels) / std::log2(kSplatNoneAtLeast / kSplatWholeAtMost), 0.0F,
                      1.0F);
}

const char* emission_splat_glsl()
{
    static_assert(kSplatWholeAtMost == 2.0F && kSplatNoneAtLeast == 8.0F, "the GLSL twin's constants");
    return "// (round SUBPIX - engine/gfx/rt_emission_map.h emission_splat_share's twin)\n"
           "float emission_splat_share(float drawn_pixels)\n{\n"
           "    if (!(drawn_pixels > 2.0)) {\n        return 1.0;\n    }\n"
           "    return clamp(log2(8.0 / drawn_pixels) / log2(8.0 / 2.0), 0.0, 1.0);\n}\n";
}

std::array<float, 3> emission_mean(const EmissionLevel& level)
{
    std::array<double, 3> sum{};
    for (const std::array<float, 3>& texel : level.texels) {
        for (std::size_t k = 0; k < 3; ++k) {
            core::at(sum, k) += static_cast<double>(core::at(texel, k));
        }
    }
    const double count = std::max<double>(static_cast<double>(level.texels.size()), 1.0);
    return {static_cast<float>(std::get<0>(sum) / count), static_cast<float>(std::get<1>(sum) / count),
            static_cast<float>(std::get<2>(sum) / count)};
}

// (round SUBPIX) An emission map's levels as a texture of the Gpu's: four halves a texel (R G B, 1), every level written.
GpuTexture* emission_map_texture(Gpu& gpu, const std::vector<EmissionLevel>& levels)
{
    if (levels.empty()) {
        return nullptr;
    }
    GpuTexture* texture = nullptr;
    const GpuTextureDesc desc{.width = levels.front().width,
                              .height = levels.front().height,
                              .levels = static_cast<uint32_t>(levels.size()),
                              .usage = 0,
                              .format = gpu_value::kFormatA16B16G16R16F,
                              .pool = gpu_value::kPoolManaged};
    if (gpu.create_texture(desc, &texture) != gpu_value::kResultOk || texture == nullptr) {
        return nullptr;
    }
    constexpr std::size_t kHalves = 4;
    const uint16_t one = to_half(1.0F);
    for (std::size_t level = 0; level < levels.size(); ++level) {
        const EmissionLevel& made = core::at(levels, level);
        GpuLockedRect locked;
        if (gpu.lock_texture(texture, static_cast<uint32_t>(level), &locked, nullptr, 0) != gpu_value::kResultOk ||
            locked.bits == nullptr || locked.pitch <= 0) {
            gpu.release_texture(texture);
            return nullptr;
        }
        auto* const bytes = static_cast<uint8_t*>(locked.bits);
        std::vector<uint16_t> row(static_cast<std::size_t>(made.width) * kHalves);
        for (uint32_t y = 0; y < made.height; ++y) {
            for (uint32_t x = 0; x < made.width; ++x) {
                const std::array<float, 3>& texel = core::at(made.texels, (static_cast<std::size_t>(y) * made.width) + x);
                for (std::size_t k = 0; k < 3; ++k) {
                    core::at(row, (x * kHalves) + k) = to_half(core::at(texel, k));
                }
                core::at(row, (x * kHalves) + 3) = one;
            }
            std::memcpy(bytes + (static_cast<std::size_t>(y) * static_cast<uint32_t>(locked.pitch)), row.data(),
                        row.size() * sizeof(uint16_t));
        }
        gpu.unlock_texture(texture, static_cast<uint32_t>(level));
    }
    return texture;
}

} // namespace render::rt
