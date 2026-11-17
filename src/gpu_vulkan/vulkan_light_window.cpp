// NOT ORIGINAL - round W2: the light store round the camera on the device (vulkan_light_window.h).
#include "gpu_vulkan/vulkan_light_window.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <future>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/light_window.h"
#include "engine/gfx/rt_daylight.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_samplers.h"
#include "gpu_vulkan/vulkan_surface_trail.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

constexpr VkFormat kFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr double kMillisecondsPerSecond = 1000.0;

struct Filled {
    std::array<int, 3> first{};
    const rt::LightField* field = nullptr;
    std::vector<uint8_t> bytes;
    float least = 0.0F;
    float most = 0.0F;
    double milliseconds = 0.0;
};

struct WindowState {
    Image image;
    bool on = false;
    std::array<int, 3> first{};
    std::shared_ptr<const rt::LightField> field; // the field the window on the device came from (kept: identity)
    std::future<Filled> filling;
    std::shared_ptr<const rt::LightField> asked_field;
    bool asking = false;
    uint64_t windows = 0;
};

WindowState& window_state()
{
    static WindowState state;
    return state;
}

bool far_from(const std::array<int, 3>& first, const std::array<int, 3>& wanted)
{
    for (std::size_t a = 0; a < 3; ++a) {
        if (std::abs(core::at(first, a) - core::at(wanted, a)) > rt::kLightWindowRecentre) {
            return true;
        }
    }
    return false;
}

bool upload(Backend& state, WindowState& own, const Filled& filled)
{
    constexpr VkImageUsageFlags kUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    constexpr auto kWidth = static_cast<uint32_t>(rt::kLightWindowCells);
    constexpr auto kHeight =
        static_cast<uint32_t>(rt::kLightWindowCells * rt::kLightWindowLayers * rt::kLightWindowPlanes);
    own.image.resting = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if (!ensure_trail_image(state, &own.image, kFormat, {.width = kWidth, .height = kHeight}, kUsage)) {
        return false;
    }
    own.image.resting = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    std::array<VkBufferImageCopy, 1> region{image_region(0, 0, 0, kWidth, kHeight)};
    return copy_into_image(state, &own.image, filled.bytes, region);
}

std::array<float, 4> four(float x, float y, float z, float w) { return {x, y, z, w}; }

} // namespace

rt::ProbeL1 fallback_probe(const std::array<float, 3>& camera)
{
    const rt::PointLight light = rt::point_light_at({camera[0], camera[1], camera[2]});
    if (light.valid) {
        return light.indirect(1.0F);
    }
    rt::ProbeL1 out;
    const rt::DayLight day = rt::day_light();
    for (std::size_t ch = 0; ch < 3; ++ch) {
        // (an open sky of light S: S (1 + n.y) / 2; without the clock the reference day's light, 1 on a level surface)
        const float sky = day.on ? core::at(day.sky_light, ch) : 1.0F;
        core::at(out.c0, ch) = 0.5F * sky;
        core::at(out.c1, ch) = {0.0F, 0.5F * sky, 0.0F};
    }
    return out;
}

SheetLighting update_light_window(Backend& state, const std::array<float, 3>& camera)
{
    WindowState& own = window_state();
    SheetLighting out;
    const std::shared_ptr<const rt::LightField> field = rt::light_field();
    // the direct light: the field's (what the probes were made with - the objects' key light), else the clock's
    const rt::PointLight at_camera = rt::point_light_at({camera[0], camera[1], camera[2]});
    if (at_camera.valid) {
        out.toward = four(std::get<0>(at_camera.toward), std::get<1>(at_camera.toward), std::get<2>(at_camera.toward),
                          1.0F);
        out.sun = four(std::get<0>(at_camera.sun), std::get<1>(at_camera.sun), std::get<2>(at_camera.sun), 0.0F);
    } else if (const rt::DayLight day = rt::day_light(); day.on) {
        out.toward = four(core::at(day.light, 0), core::at(day.light, 1), core::at(day.light, 2), 1.0F);
        out.sun = four(core::at(day.sun_light, 0), core::at(day.sun_light, 1), core::at(day.sun_light, 2), 0.0F);
    } else {
        out.toward = four(0.0F, 1.0F, 0.0F, 0.0F);
    }
    const rt::ProbeL1 fallback = fallback_probe(camera);
    for (std::size_t ch = 0; ch < 3; ++ch) {
        const rt::FieldPoint& c1 = core::at(fallback.c1, ch);
        core::at(out.fallback, ch) =
            four(core::at(fallback.c0, ch), std::get<0>(c1), std::get<1>(c1), std::get<2>(c1));
    }
    // a window come back from the worker
    if (own.asking && own.filling.valid() &&
        own.filling.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        own.asking = false;
        const Filled filled = own.filling.get();
        if (filled.field == field.get() && field != nullptr && upload(state, own, filled)) {
            own.on = true;
            own.first = filled.first;
            own.field = field;
            if (++own.windows == 1 || own.windows % 64 == 0) {
                get_log("vulkan: light window: the light store round the camera on the device (%dx%dx%d probes of %.0f "
                        "units, made in %.0f ms; its light %.3g - %.3g; %llu so far)",
                        rt::kLightWindowCells, rt::kLightWindowLayers, rt::kLightWindowCells,
                        static_cast<double>(rt::kLightWindowCell), filled.milliseconds,
                        static_cast<double>(filled.least), static_cast<double>(filled.most),
                        static_cast<unsigned long long>(own.windows));
            }
        }
    }
    if (field == nullptr) {
        own.on = false; // (no field: the original's look, or another level's being made - the fallback everywhere)
        own.field = nullptr;
    } else if (!own.asking) {
        const std::array<int, 3> wanted = rt::light_window_first({camera[0], camera[1], camera[2]});
        if (!own.on || own.field != field || far_from(own.first, wanted)) {
            own.asking = true;
            own.asked_field = field;
            own.filling = std::async(std::launch::async, [field, camera]() {
                const auto began = std::chrono::steady_clock::now();
                const rt::LightWindow window = rt::light_window(*field, {camera[0], camera[1], camera[2]});
                Filled filled;
                filled.first = window.first;
                filled.field = window.field;
                filled.bytes.resize(window.texels.size() * sizeof(uint16_t));
                std::memcpy(filled.bytes.data(), window.texels.data(), filled.bytes.size());
                filled.least = window.least;
                filled.most = window.most;
                filled.milliseconds =
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count() *
                    kMillisecondsPerSecond;
                return filled;
            });
        }
    }
    SamplerKey key;
    key.address_u = gpu_value::kAddressClamp;
    key.address_v = gpu_value::kAddressClamp;
    key.address_w = gpu_value::kAddressClamp;
    key.mag_filter = gpu_value::kFilterLinear;
    key.min_filter = gpu_value::kFilterLinear;
    out.window = own.on && own.image.image != VK_NULL_HANDLE;
    const Image& image = out.window ? own.image : state.blank;
    out.image = {.sampler = state.samplers.sampler(key),
                 .imageView = image.view,
                 .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    if (out.window) {
        out.origin = four(static_cast<float>(own.first[0]) * rt::kLightWindowCell,
                          static_cast<float>(own.first[1]) * rt::kLightWindowCell,
                          static_cast<float>(own.first[2]) * rt::kLightWindowCell, 1.0F);
    }
    return out;
}

std::string light_window_glsl(const char* sampler, const char* block)
{
    const std::string cells = std::to_string(rt::kLightWindowCells) + ".0";
    const std::string layers = std::to_string(rt::kLightWindowLayers) + ".0";
    const std::string cell = std::to_string(static_cast<int>(rt::kLightWindowCell)) + ".0";
    const std::string b = block;
    const std::string s = sampler;
    return "\n// (gpu_vulkan/vulkan_light_window.h: the light store round the camera - per channel (c0, c1))\n"
           "vec4 window_texel(vec2 xz, float layer, float plane)\n{\n"
           "    vec2 size = vec2(" + cells + ", " + cells + " * " + layers + " * 3.0);\n"
           "    return texture(" + s + ", vec2(xz.x, (plane * " + layers + " + layer) * " + cells + " + xz.y) / size);\n}\n"
           "void window_probe(vec3 world, out vec4 r, out vec4 g, out vec4 b)\n{\n"
           "    r = " + b + ".light_fallback[0];\n    g = " + b + ".light_fallback[1];\n    b = " + b +
           ".light_fallback[2];\n"
           "    if (" + b + ".light_origin.w < 0.5) {\n        return;\n    }\n"
           "    vec3 c = clamp((world - " + b + ".light_origin.xyz) / " + cell + " - 0.5, vec3(0.0), vec3(" + cells +
           " - 1.0, " + layers + " - 1.0, " + cells + " - 1.0));\n"
           "    float y0 = floor(c.y);\n    float y1 = min(y0 + 1.0, " + layers + " - 1.0);\n    float t = c.y - y0;\n"
           "    vec2 xz = c.xz + 0.5;\n"
           "    r = mix(window_texel(xz, y0, 0.0), window_texel(xz, y1, 0.0), t);\n"
           "    g = mix(window_texel(xz, y0, 1.0), window_texel(xz, y1, 1.0), t);\n"
           "    b = mix(window_texel(xz, y0, 2.0), window_texel(xz, y1, 2.0), t);\n}\n"
           "vec3 probe_light(vec4 r, vec4 g, vec4 b, vec3 n)\n{\n"
           "    return max(vec3(r.x + dot(r.yzw, n), g.x + dot(g.yzw, n), b.x + dot(b.yzw, n)), vec3(0.0));\n}\n";
}

bool light_window_on() { return window_state().on; }

void release_light_window(Backend& state)
{
    WindowState& own = window_state();
    if (own.filling.valid()) {
        own.filling.wait();
        own.filling = {};
    }
    own.asking = false;
    own.on = false;
    own.field = nullptr;
    own.asked_field = nullptr;
    if (state.memory.ready()) {
        state.memory.destroy_image(&own.image); // (the device's shutdown: its work finished)
    }
}

} // namespace render::vulkan
