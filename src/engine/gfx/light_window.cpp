// NOT ORIGINAL - round W2: the light store round the camera (light_window.h).
#include "engine/gfx/light_window.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/relit_data.h"

namespace render::rt {

namespace {

constexpr std::size_t kTexelHalves = 4;

std::size_t texel_at(int plane, int x, int layer, int z)
{
    const std::size_t row = (static_cast<std::size_t>(plane) * kLightWindowLayers) + static_cast<std::size_t>(layer);
    return (((row * kLightWindowCells) + static_cast<std::size_t>(z)) * kLightWindowCells + static_cast<std::size_t>(x)) *
           kTexelHalves;
}

} // namespace

std::array<int, 3> light_window_first(const FieldPoint& centre)
{
    return {static_cast<int>(std::floor(std::get<0>(centre) / kLightWindowCell)) - (kLightWindowCells / 2),
            static_cast<int>(std::floor(std::get<1>(centre) / kLightWindowCell)) - (kLightWindowLayers / 2),
            static_cast<int>(std::floor(std::get<2>(centre) / kLightWindowCell)) - (kLightWindowCells / 2)};
}

LightWindow light_window(const LightField& field, const FieldPoint& centre)
{
    LightWindow out;
    out.first = light_window_first(centre);
    out.field = &field;
    out.texels.resize(static_cast<std::size_t>(kLightWindowPlanes) * kLightWindowLayers * kLightWindowCells *
                      kLightWindowCells * kTexelHalves);
    float least = 3.0e38F;
    float most = 0.0F;
    for (int layer = 0; layer < kLightWindowLayers; ++layer) {
        for (int z = 0; z < kLightWindowCells; ++z) {
            for (int x = 0; x < kLightWindowCells; ++x) {
                const FieldPoint point{(static_cast<float>(out.first[0] + x) + 0.5F) * kLightWindowCell,
                                       (static_cast<float>(out.first[1] + layer) + 0.5F) * kLightWindowCell,
                                       (static_cast<float>(out.first[2] + z) + 0.5F) * kLightWindowCell};
                const PointLight light = field.light_at(point);
                // (the whole indirect light: the lamps pass lights only what was drawn before it - the blades and items
                // come after, so all of the lamps' light is theirs to take from the store)
                const ProbeL1 probe = light.valid ? light.indirect(1.0F) : ProbeL1{};
                for (int plane = 0; plane < kLightWindowPlanes; ++plane) {
                    const auto channel = static_cast<std::size_t>(plane);
                    const std::size_t at = texel_at(plane, x, layer, z);
                    const float c0 = core::at(probe.c0, channel);
                    const FieldPoint& c1 = core::at(probe.c1, channel);
                    core::at(out.texels, at) = to_half(c0);
                    core::at(out.texels, at + 1) = to_half(std::get<0>(c1));
                    core::at(out.texels, at + 2) = to_half(std::get<1>(c1));
                    core::at(out.texels, at + 3) = to_half(std::get<2>(c1));
                    least = std::min(least, c0);
                    most = std::max(most, c0);
                }
            }
        }
    }
    out.least = least;
    out.most = most;
    return out;
}

ProbeL1 light_window_probe(const LightWindow& window, const FieldPoint& point)
{
    ProbeL1 out;
    if (window.texels.empty()) {
        return out;
    }
    // (the cells' centres: the GLSL's c = (world - origin) / cell - 0.5, clamped to the window)
    const auto local = [&](std::size_t axis, int cells) {
        const float origin = static_cast<float>(core::at(window.first, axis)) * kLightWindowCell;
        const float c = ((core::at(point, axis) - origin) / kLightWindowCell) - 0.5F;
        return std::clamp(c, 0.0F, static_cast<float>(cells - 1));
    };
    const float cx = local(0, kLightWindowCells);
    const float cy = local(1, kLightWindowLayers);
    const float cz = local(2, kLightWindowCells);
    const int x0 = static_cast<int>(std::floor(cx));
    const int y0 = static_cast<int>(std::floor(cy));
    const int z0 = static_cast<int>(std::floor(cz));
    const int x1 = std::min(x0 + 1, kLightWindowCells - 1);
    const int y1 = std::min(y0 + 1, kLightWindowLayers - 1);
    const int z1 = std::min(z0 + 1, kLightWindowCells - 1);
    const float tx = cx - static_cast<float>(x0);
    const float ty = cy - static_cast<float>(y0);
    const float tz = cz - static_cast<float>(z0);
    for (int plane = 0; plane < kLightWindowPlanes; ++plane) {
        std::array<float, kTexelHalves> sum{};
        for (int corner = 0; corner < 8; ++corner) {
            const bool dx = (corner & 1) != 0;
            const bool dy = (corner & 2) != 0;
            const bool dz = (corner & 4) != 0;
            const float weight = (dx ? tx : 1.0F - tx) * (dy ? ty : 1.0F - ty) * (dz ? tz : 1.0F - tz);
            const std::size_t at = texel_at(plane, dx ? x1 : x0, dy ? y1 : y0, dz ? z1 : z0);
            for (std::size_t k = 0; k < kTexelHalves; ++k) {
                core::at(sum, k) += weight * from_half(core::at(window.texels, at + k));
            }
        }
        const auto channel = static_cast<std::size_t>(plane);
        core::at(out.c0, channel) = std::get<0>(sum);
        core::at(out.c1, channel) = {std::get<1>(sum), std::get<2>(sum), std::get<3>(sum)};
    }
    return out;
}

} // namespace render::rt
