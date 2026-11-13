// NOT ORIGINAL - per-pixel lighting and surface maps on the Vulkan Gpu (vulkan_surface.h).
#include "gpu_vulkan/vulkan_surface.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <span>
#include <unordered_map>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/surface_maps.h"
#include "gpu_vulkan/ff_key.h"
#include "gpu_vulkan/ff_uniforms.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_objects.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

constexpr std::size_t kTexel = 4;
constexpr float kByteHalf = 127.5F;
constexpr float kByte = 255.0F;
constexpr std::size_t kViewSlot = 1;
constexpr std::size_t kRows = 4;
constexpr uint32_t kFvfNormalBit = 0x010U;
constexpr float kLeastAlpha = 0.0064F; // (the shader's floor on GGX's alpha)

struct MapEntry {
    Texture* map = nullptr;
    float f0 = kRtPbrF0;
    uint8_t material_class = 0;
};

// The frame's sun as the draws use it (looked up once a frame: rt_scene's getters take locks).
struct FrameSun {
    uint64_t serial = 0;
    bool known = false;
    std::array<float, 3> toward{};
    float weight = 0.0F;
    std::array<float, 3> colour{1.0F, 1.0F, 1.0F};
};

struct SurfaceState {
    std::mutex mutex;
    RtPbrSettings settings; // (set_rt_pbr_settings: at start-up and on a live change - the Options screen, game thread)
    RtPbrSettings draw_settings; // (the draw thread's: set with settings, read without the lock)
    FrameSun sun;
    std::unordered_map<const Texture*, MapEntry> maps;
    std::atomic<uint64_t> attached{0};
};

SurfaceState& surface_state()
{
    static SurfaceState state;
    return state;
}

float decode(uint8_t value) { return (static_cast<float>(value) - kByteHalf) / kByteHalf; }
uint8_t encode_signed(float value)
{
    return static_cast<uint8_t>(std::lround(std::clamp((value * kByteHalf) + kByteHalf, 0.0F, kByte)));
}
uint8_t encode_unit(float value) { return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0F, 1.0F) * kByte)); }

// The next mip level of a map (rows of RGBA texels): each texel from the 2 x 2 above it (one row or column at an odd
// edge) - the normals averaged and renormalised, the roughness widened by how far the average falls short of a unit
// normal (Toksvig: alpha^2 + (1 - |n|) / |n|), the height averaged.
struct Extent {
    uint32_t width = 0;
    uint32_t height = 0;
};

std::vector<uint8_t> next_level(std::span<const uint8_t> texels, Extent* size)
{
    const uint32_t width = size->width;
    const uint32_t height = size->height;
    const uint32_t down_width = std::max(1U, width / 2);
    const uint32_t down_height = std::max(1U, height / 2);
    std::vector<uint8_t> out(static_cast<std::size_t>(down_width) * down_height * kTexel);
    for (uint32_t row = 0; row < down_height; ++row) {
        for (uint32_t column = 0; column < down_width; ++column) {
            std::array<float, 3> normal{};
            float alpha_squared = 0.0F;
            float height_sum = 0.0F;
            float count = 0.0F;
            for (uint32_t dy = 0; dy < 2; ++dy) {
                for (uint32_t dx = 0; dx < 2; ++dx) {
                    const uint32_t sx = std::min((column * 2) + dx, width - 1);
                    const uint32_t sy = std::min((row * 2) + dy, height - 1);
                    const std::span<const uint8_t> texel =
                        texels.subspan(((static_cast<std::size_t>(sy) * width) + sx) * kTexel, kTexel);
                    const float nx = decode(core::at(texel, 0));
                    const float ny = decode(core::at(texel, 1));
                    const float nz = std::sqrt(std::max(0.0F, 1.0F - (nx * nx) - (ny * ny)));
                    const float length = std::sqrt((nx * nx) + (ny * ny) + (nz * nz));
                    const float scale = length > 0.0F ? 1.0F / length : 0.0F;
                    normal = {core::at(normal, 0) + (nx * scale), core::at(normal, 1) + (ny * scale), core::at(normal, 2) + (nz * scale)};
                    const float rough = static_cast<float>(core::at(texel, 2)) / kByte;
                    alpha_squared += rough * rough * rough * rough;
                    height_sum += static_cast<float>(core::at(texel, 3)) / kByte;
                    count += 1.0F;
                }
            }
            const std::array<float, 3> mean{core::at(normal, 0) / count, core::at(normal, 1) / count, core::at(normal, 2) / count};
            const float length = std::sqrt((core::at(mean, 0) * core::at(mean, 0)) + (core::at(mean, 1) * core::at(mean, 1)) + (core::at(mean, 2) * core::at(mean, 2)));
            const float spread = length > 0.0F ? (1.0F - std::min(length, 1.0F)) / length : 1.0F;
            const float alpha = std::sqrt(std::max((alpha_squared / count) + spread, kLeastAlpha * kLeastAlpha));
            const std::span<uint8_t> target =
                std::span<uint8_t>(out).subspan(((static_cast<std::size_t>(row) * down_width) + column) * kTexel, kTexel);
            const float inverse = length > 0.0F ? 1.0F / length : 0.0F;
            core::at(target, 0) = encode_signed(core::at(mean, 0) * inverse);
            core::at(target, 1) = encode_signed(core::at(mean, 1) * inverse);
            core::at(target, 2) = encode_unit(std::sqrt(alpha));
            core::at(target, 3) = encode_unit(height_sum / count);
        }
    }
    *size = {.width = down_width, .height = down_height};
    return out;
}

// One level into a locked A8R8G8B8 texture: the file's R, G, B, A bytes as Direct3D's B, G, R, A in memory.
void copy_level(const GpuLockedRect& locked, std::span<const uint8_t> texels, uint32_t width, uint32_t height)
{
    const auto pitch = static_cast<std::size_t>(locked.pitch);
    const std::span<uint8_t> level(static_cast<uint8_t*>(locked.bits), (pitch * (height - 1)) + (std::size_t{width} * kTexel));
    for (uint32_t line = 0; line < height; ++line) {
        const std::span<uint8_t> row = level.subspan(line * pitch, static_cast<std::size_t>(width) * kTexel);
        const std::span<const uint8_t> from = texels.subspan(static_cast<std::size_t>(line) * width * kTexel, row.size());
        for (std::size_t at = 0; at < row.size(); at += kTexel) {
            core::at(row, at) = core::at(from, at + 2);
            core::at(row, at + 1) = core::at(from, at + 1);
            core::at(row, at + 2) = core::at(from, at);
            core::at(row, at + 3) = core::at(from, at + 3);
        }
    }
}

uint32_t level_count(uint32_t width, uint32_t height)
{
    uint32_t levels = 1;
    while (width > 1 || height > 1) {
        width = std::max(1U, width / 2);
        height = std::max(1U, height / 2);
        ++levels;
    }
    return levels;
}

class Registry final : public surface::SurfaceMaps {
public:
    bool attach(GpuTexture* colour, const surface::MapInfo& info, std::span<const uint8_t> texels) override
    {
        if (colour == nullptr || info.width == 0 || info.height == 0 ||
            texels.size() != static_cast<std::size_t>(info.width) * info.height * kTexel) {
            return false;
        }
        Gpu& device = gpu();
        const uint32_t levels = level_count(info.width, info.height);
        GpuTexture* made = nullptr;
        if (device.create_texture({.width = info.width,
                                   .height = info.height,
                                   .levels = levels,
                                   .usage = 0,
                                   .format = gpu_value::kFormatA8R8G8B8,
                                   .pool = gpu_value::kPoolManaged},
                                  &made) != gpu_value::kResultOk ||
            made == nullptr) {
            return false;
        }
        std::vector<uint8_t> level(texels.begin(), texels.end());
        Extent size{.width = info.width, .height = info.height};
        for (uint32_t index = 0; index < levels; ++index) {
            GpuLockedRect locked{};
            if (device.lock_texture(made, index, &locked, nullptr, 0) == gpu_value::kResultOk && locked.bits != nullptr) {
                copy_level(locked, level, size.width, size.height);
                device.unlock_texture(made, index);
            }
            if (index + 1 < levels) {
                level = next_level(level, &size);
            }
        }
        Texture* replaced = nullptr;
        {
            SurfaceState& state = surface_state();
            const std::scoped_lock lock(state.mutex);
            MapEntry& entry = state.maps[object(colour)];
            replaced = entry.map;
            entry = {.map = object(made), .f0 = std::clamp(info.f0, 0.0F, 1.0F), .material_class = info.material_class};
        }
        if (replaced != nullptr) {
            device.release_texture(texture_handle(replaced));
        }
        surface_state().attached.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
};

Registry& registry()
{
    static Registry maps;
    return maps;
}

// A direction (row vector) through the view's 3 x 3: world into camera space.
Float4 to_camera(const std::array<float, kMatrixFloats>& view, const std::array<float, 3>& direction, float w)
{
    Float4 out{};
    for (std::size_t column = 0; column < 3; ++column) {
        float sum = 0.0F;
        for (std::size_t row = 0; row < 3; ++row) {
            sum += core::at(direction, row) * core::at(view, (row * kRows) + column);
        }
        core::at(out, column) = sum;
    }
    const float length = std::sqrt((core::at(out, 0) * core::at(out, 0)) + (core::at(out, 1) * core::at(out, 1)) + (core::at(out, 2) * core::at(out, 2)));
    if (length > 0.0F) {
        out = {core::at(out, 0) / length, core::at(out, 1) / length, core::at(out, 2) / length, w};
    } else {
        out = {};
    }
    return out;
}

} // namespace

void set_rt_pbr_settings(const RtPbrSettings& settings)
{
    SurfaceState& state = surface_state();
    {
        const std::scoped_lock lock(state.mutex);
        state.settings = settings;
        state.draw_settings = settings;
        state.sun = {};
    }
    surface::set_surface_maps(settings.on ? &registry() : nullptr);
    if (settings.on && !rt::baked_sun_wanted()) {
        rt::set_baked_sun_wanted(true); // (the bake's sun: fitted when ray tracing is on)
    }
    if (settings.on) {
        get_log("rt_pbr: per-pixel lighting on (specular %.2f, normals %.2f%s)", static_cast<double>(settings.specular),
                static_cast<double>(settings.normal_strength), settings.objects ? ", objects too" : "");
    }
}

RtPbrSettings rt_pbr_settings()
{
    SurfaceState& state = surface_state();
    const std::scoped_lock lock(state.mutex);
    return state.settings;
}

SurfaceDraw surface_draw(const GpuStateBlock& block, const ShaderKey& key)
{
    SurfaceDraw draw;
    SurfaceState& state = surface_state();
    if (!state.draw_settings.on) {
        return draw;
    }
    const FragmentKey& fragment = key.fragment;
    const bool program = fragment.program == PixelProgram::kMaterial || fragment.program == PixelProgram::kTextureVertexX4 ||
                         fragment.program == PixelProgram::kTextureConstantAlpha;
    if (!program || fragment.unlit != 0 || fvf_pre_transformed(key.vertex.fvf) || key.vertex.points ||
        (key.vertex.coord_mask & 1U) == 0U) {
        return draw;
    }
    const bool lit = key.vertex.lighting && key.vertex.light_count > 0 && (key.vertex.fvf & kFvfNormalBit) != 0U;
    GpuTexture* const first = block.textures.front();
    const std::scoped_lock lock(state.mutex);
    if (first != nullptr) {
        const auto found = state.maps.find(object(first));
        if (found != state.maps.end()) {
            draw.map = found->second.map;
            draw.f0 = found->second.f0;
            draw.flags = static_cast<uint8_t>(kSurfaceLighting | kSurfaceMap | (lit && state.settings.objects ? kSurfaceLit : 0));
            return draw;
        }
    }
    if (lit && state.settings.objects) {
        draw.flags = kSurfaceLighting | kSurfaceLit;
        draw.f0 = state.settings.f0;
    }
    return draw;
}

void fill_surface_uniforms(const GpuStateBlock& block, const VertexUniforms& vertex, const SurfaceDraw& draw, uint64_t frame,
                           SurfaceUniforms* uniforms)
{
    SurfaceState& state = surface_state();
    const RtPbrSettings& settings = state.draw_settings;
    *uniforms = {};
    for (std::size_t index = 0; index < kMaxLights; ++index) {
        const Float4& from = core::at(vertex.light_direction, index);
        const float length = std::sqrt((core::at(from, 0) * core::at(from, 0)) + (core::at(from, 1) * core::at(from, 1)) + (core::at(from, 2) * core::at(from, 2)));
        if (length > 0.0F) {
            core::at(uniforms->light_toward, index) = {-core::at(from, 0) / length, -core::at(from, 1) / length, -core::at(from, 2) / length, 0.0F};
            core::at(uniforms->light_colour, index) = core::at(vertex.light_diffuse, index);
        }
    }
    const std::array<float, kMatrixFloats>& view = core::at(block.transforms, kViewSlot);
    // The sun: the bake's (fitted with ray tracing on; the console's `rt sun` before it), else the game's shown one
    // at its own weight, else none (the sky's detail alone) - looked up once a frame.
    FrameSun& sun = state.sun;
    if (!sun.known || sun.serial != frame) {
        sun = {.serial = frame, .known = true};
        const rt::BakedSun& baked = rt::baked_sun();
        if (rt::sun_override(&sun.toward)) {
            sun.weight = 1.0F;
        } else if (baked.found) {
            sun.toward = baked.toward;
            sun.weight = 1.0F;
        } else if (settings.game_sun > 0.0F && rt::game_sun(&sun.toward)) {
            sun.weight = settings.game_sun;
        }
        const rt::SunLight light = rt::sun_light();
        if (light.on) {
            sun.colour = light.colour;
        }
    }
    uniforms->sun = sun.weight > 0.0F ? to_camera(view, sun.toward, sun.weight) : Float4{};
    uniforms->sun_colour = {core::at(sun.colour, 0), core::at(sun.colour, 1), core::at(sun.colour, 2), settings.ambient_share};
    uniforms->sky = to_camera(view, {0.0F, 1.0F, 0.0F}, settings.sky_detail);
    uniforms->params = {settings.specular, settings.normal_strength, settings.roughness, draw.f0};
    uniforms->shade = {settings.shade_low, settings.shade_high, settings.detail_low, settings.detail_high};
}

Texture* surface_map_for(const Texture* colour)
{
    SurfaceState& state = surface_state();
    const std::scoped_lock lock(state.mutex);
    const auto found = state.maps.find(colour);
    return found != state.maps.end() ? found->second.map : nullptr;
}

float surface_map_f0(const Texture* colour)
{
    SurfaceState& state = surface_state();
    const std::scoped_lock lock(state.mutex);
    const auto found = state.maps.find(colour);
    return found != state.maps.end() ? found->second.f0 : 0.0F;
}

Texture* take_surface_map(const Texture* colour)
{
    SurfaceState& state = surface_state();
    const std::scoped_lock lock(state.mutex);
    if (state.maps.empty()) {
        return nullptr;
    }
    const auto found = state.maps.find(colour);
    if (found == state.maps.end()) {
        return nullptr;
    }
    Texture* const map = found->second.map;
    state.maps.erase(found);
    return map;
}

uint64_t surface_maps_attached() { return surface_state().attached.load(std::memory_order_relaxed); }

} // namespace render::vulkan
