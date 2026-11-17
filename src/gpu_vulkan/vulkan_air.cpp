// NOT ORIGINAL - round W: the level's air transport on the device (vulkan_air.h).
#include "gpu_vulkan/vulkan_air.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/air_volume.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_samplers.h"
#include "gpu_vulkan/vulkan_surface_trail.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

constexpr VkFormat kFormat = VK_FORMAT_R8G8_UNORM;
constexpr int kRecentre = air::kWindowCells / 8; // (window cells off the centre before a new window is asked for)

struct AirState {
    Image image;
    bool on = false;
    std::array<int, 3> first{}; // the window on the device
    uint64_t serial = 0;        // ... and the volume it came from
    std::future<air::Window> filling;
    std::array<int, 3> asked{}; // the window being filled
    uint64_t asked_serial = 0;
    bool asking = false;
    uint64_t windows = 0;
};

AirState& air_state()
{
    static AirState state;
    return state;
}

bool far_from(const std::array<int, 3>& first, const std::array<int, 3>& wanted)
{
    for (std::size_t a = 0; a < 3; ++a) {
        if (std::abs(core::at(first, a) - core::at(wanted, a)) > kRecentre) {
            return true;
        }
    }
    return false;
}

bool upload(Backend& state, AirState& own, const air::Window& window)
{
    constexpr VkImageUsageFlags kUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    constexpr auto kWidth = static_cast<uint32_t>(air::kWindowCells);
    constexpr auto kHeight = static_cast<uint32_t>(air::kWindowCells * air::kWindowLayers);
    own.image.resting = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if (!ensure_trail_image(state, &own.image, kFormat, {.width = kWidth, .height = kHeight}, kUsage)) {
        return false;
    }
    own.image.resting = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    std::array<VkBufferImageCopy, 1> region{image_region(0, 0, 0, kWidth, kHeight)};
    return copy_into_image(state, &own.image, window.texels, region);
}

} // namespace

void update_air_map(Backend& state, const std::array<float, 3>& camera)
{
    AirState& own = air_state();
    const std::shared_ptr<const air::Volume> volume = air::volume();
    if (volume == nullptr) {
        if (own.on) {
            own.on = false; // (another level being built: open air until its volume comes)
        }
        return;
    }
    // a window come back from the worker
    if (own.asking && own.filling.valid() &&
        own.filling.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        own.asking = false;
        const air::Window window = own.filling.get();
        if (window.serial == volume->serial() && upload(state, own, window)) {
            own.on = true;
            own.first = window.first;
            own.serial = window.serial;
            if (++own.windows == 1 || own.windows % 64 == 0) {
                get_log("vulkan: air: the window round the camera on the device (%dx%dx%d cells of %.0f units; %llu so "
                        "far)",
                        air::kWindowCells, air::kWindowLayers, air::kWindowCells, static_cast<double>(air::kWindowCell),
                        static_cast<unsigned long long>(own.windows));
            }
        }
    }
    if (own.asking) {
        return;
    }
    const std::array<int, 3> wanted = air::window_first(camera);
    if (own.on && own.serial == volume->serial() && !far_from(own.first, wanted)) {
        return;
    }
    own.asking = true;
    own.asked = wanted;
    own.asked_serial = volume->serial();
    own.filling = std::async(std::launch::async, [volume, camera]() { return air::air_window(*volume, camera); });
}

AirMap air_map(Backend& state)
{
    AirState& own = air_state();
    SamplerKey key;
    key.address_u = gpu_value::kAddressClamp;
    key.address_v = gpu_value::kAddressClamp;
    key.address_w = gpu_value::kAddressClamp;
    key.mag_filter = gpu_value::kFilterLinear;
    key.min_filter = gpu_value::kFilterLinear;
    AirMap out;
    const bool on = own.on && own.image.image != VK_NULL_HANDLE;
    const Image& image = on ? own.image : state.blank;
    out.on = on;
    out.image = {.sampler = state.samplers.sampler(key),
                 .imageView = image.view,
                 .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    if (on) {
        out.origin = {static_cast<float>(own.first[0]) * air::kWindowCell,
                      static_cast<float>(own.first[1]) * air::kWindowCell,
                      static_cast<float>(own.first[2]) * air::kWindowCell, 1.0F};
    }
    return out;
}

std::string air_map_glsl(const char* sampler, const char* origin)
{
    const std::string cells = std::to_string(air::kWindowCells) + ".0";
    const std::string layers = std::to_string(air::kWindowLayers) + ".0";
    const std::string cell = std::to_string(static_cast<int>(air::kWindowCell)) + ".0";
    return std::string(
               "\n// (gpu_vulkan/vulkan_air.h: the level's air round the camera - x the wind's share, y the sky's)\n"
               "vec2 air_at(vec3 world)\n{\n    if (") +
           origin + ".w < 0.5) {\n        return vec2(1.0);\n    }\n    vec3 c = (world - " + origin + ".xyz) / " +
           cell + " - 0.5;\n    if (any(lessThan(c, vec3(-0.5))) || c.x > " + cells + " - 0.5 || c.z > " + cells +
           " - 0.5 || c.y > " + layers +
           " - 0.5) {\n        return vec2(1.0);\n    }\n"
           "    float y = clamp(c.y, 0.0, " +
           layers +
           " - 1.0);\n    float y0 = floor(y);\n"
           "    float y1 = min(y0 + 1.0, " +
           layers +
           " - 1.0);\n"
           "    vec2 xz = clamp(c.xz, vec2(0.0), vec2(" +
           cells +
           " - 1.0)) + 0.5;\n"
           "    vec2 size = vec2(" +
           cells + ", " + cells + " * " + layers +
           ");\n"
           "    vec2 a = texture(" +
           sampler + ", vec2(xz.x, y0 * " + cells +
           " + xz.y) / size).rg;\n"
           "    vec2 b = texture(" +
           sampler + ", vec2(xz.x, y1 * " + cells +
           " + xz.y) / size).rg;\n"
           "    vec2 v = mix(a, b, y - y0);\n"
           "    return v;\n}\n"
           "float air_wind(vec3 world) { return min(air_at(world).x, 1.0); }\n"
           "float air_sky(vec3 world) { return air_at(world).y; }\n";
}

void release_air(Backend& state)
{
    AirState& own = air_state();
    if (own.filling.valid()) {
        own.filling.wait();
        own.filling = {};
    }
    own.asking = false;
    own.on = false;
    retire_image(state, &own.image);
}

} // namespace render::vulkan
