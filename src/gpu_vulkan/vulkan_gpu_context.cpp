// NOT ORIGINAL - Vulkan's render::GpuContext (gpu_vulkan.h; docs/design/vulkan.md, V5e / V6): the instance, the
// physical and logical device and the swapchain for the game's window (vulkan_context.h), the present parameters kept
// as settings, the capabilities from the device's features and limits. The device is opened with the instance (the
// sample counts and the depth formats are the physical device's), the swapchain and the device's surfaces at
// create_device; a reset is the swapchain made again.

#include "gpu_vulkan/gpu_vulkan.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <volk.h>

#include "base/core/no_destroy.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_context.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_context.h"
#include "platform/run_log.h"
#include "platform/sdl_window.h"

namespace render::vulkan {

namespace {

VkPhysicalDeviceProperties device_properties(const Backend& state)
{
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(state.context.physical_device(), &properties);
    return properties;
}

class VulkanContext final : public GpuContext {
public:
    VulkanContext() = default;
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;
    VulkanContext(VulkanContext&&) = delete;
    VulkanContext& operator=(VulkanContext&&) = delete;
    ~VulkanContext() = default;

    bool create_instance() override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (state.opened) {
            return true;
        }
        if (state.window == nullptr) {
            state.window = sys::sdl_window();
        }
        state.opened = state.window != nullptr && state.context.open(state.window, state.options);
        if (!state.opened) {
            state.context.destroy();
        }
        return state.opened;
    }

    bool adapter_ready() override
    {
        const std::scoped_lock lock(backend().mutex);
        return backend().opened;
    }

    // 1 (Vulkan has no quality levels) for every sample count both the colour and the depth attachments can have.
    uint32_t sample_quality_levels(const GpuSampleQuery& query) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (!state.opened) {
            return 0;
        }
        if (query.samples <= 1) {
            return 1;
        }
        const VkPhysicalDeviceLimits limits = device_properties(state).limits;
        const VkSampleCountFlags both = limits.framebufferColorSampleCounts & limits.framebufferDepthSampleCounts;
        return (both & query.samples) != 0U && std::has_single_bit(query.samples) ? 1U : 0U;
    }

    void configure(const GpuSettings& settings) override
    {
        const std::scoped_lock lock(backend().mutex);
        backend().settings = settings;
    }

    long create_device(const GpuDeviceRequest& request) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (!state.opened || state.device_made) {
            return gpu_value::kResultInvalidCall;
        }
        state.settings.width = request.width;
        state.settings.height = request.height;
        state.settings.samples = request.samples;
        state.settings.sample_quality = request.sample_quality;
        if (!state.context.make_swapchain()) {
            return gpu_value::kResultInvalidCall;
        }
        const long made = create_device_objects(state);
        if (made != gpu_value::kResultOk) {
            release_device_objects(state);
            return made;
        }
        // the adapter, asked after the device as the original did
        const VkPhysicalDeviceProperties properties = device_properties(state);
        state.adapter = {};
        const std::string_view name(std::data(properties.deviceName));
        std::ranges::copy(name.substr(0, state.adapter.description.size() - 1), state.adapter.description.begin());
        state.adapter.vendor_id = properties.vendorID;
        state.adapter.device_id = properties.deviceID;
        state.device_made = true;
        return gpu_value::kResultOk;
    }

    [[nodiscard]] GpuSettings settings() const override
    {
        const std::scoped_lock lock(backend().mutex);
        return backend().settings;
    }

    [[nodiscard]] const GpuAdapterInfo& adapter() const override { return backend().adapter; }

    // The depth / stencil format the device has (D24S8, else D32S8) can also be sampled.
    bool depth_textures_supported() override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (!state.device_made || state.formats.depth_stencil == VK_FORMAT_UNDEFINED) {
            return false;
        }
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(state.context.physical_device(), state.formats.depth_stencil, &properties);
        return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;
    }

    // The renderer's six questions: pixel shaders (made from V3's keys), stream offsets (vkCmdBindVertexBuffers'),
    // border addressing and blend factors (core Vulkan) always; anisotropy where the device has it.
    GpuCaps caps() override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        const bool anisotropy = state.opened && state.context.enabled_features().samplerAnisotropy == VK_TRUE;
        const uint32_t max_anisotropy =
            anisotropy ? static_cast<uint32_t>(device_properties(state).limits.maxSamplerAnisotropy) : 1U;
        return {.pixel_shaders_1_1 = true,
                .stream_offset = true,
                .max_anisotropy = max_anisotropy,
                .min_filter_anisotropic = anisotropy,
                .border_addressing = true,
                .blend_factor = true};
    }

    [[nodiscard]] bool device_made() const override
    {
        const std::scoped_lock lock(backend().mutex);
        return backend().device_made;
    }

    void mark_ready() override
    {
        const std::scoped_lock lock(backend().mutex);
        backend().ready = backend().device_made;
    }

    [[nodiscard]] bool ready() const override
    {
        const std::scoped_lock lock(backend().mutex);
        return backend().ready;
    }

    // Nothing is lost in Vulkan as Direct3D 9's device is: the swapchain is made again at the window's size and the
    // device's bindings put back as Direct3D 9's Reset leaves them - the back buffer made again when the settings' size
    // changed (the PC layer has released and makes again its default-pool objects around this, as for Direct3D 9).
    bool reset() override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        return state.device_made && finish_work(state) && state.context.remake() && reset_device_bindings(state);
    }

    // (the back buffer made again at it by the next reset: reset_device_bindings)
    void set_back_buffer_size(const GpuBackBufferSize& size) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        state.settings.width = size.width;
        state.settings.height = size.height;
    }

    void shutdown() override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        release_device_objects(state);
        state.context.destroy();
        state.opened = false;
        state.device_made = false;
        state.ready = false;
    }
};

} // namespace

GpuContext& gpu_context()
{
    static core::NoDestroy<VulkanContext> context;
    return context.get();
}

namespace {
void log_message(const std::string& text) { get_log("vulkan: %s", text.c_str()); }
} // namespace

Usable usable(int device_index)
{
    DeviceProbe probe = probe_device(device_index);
    return {.usable = probe.usable, .device = std::move(probe.device), .problem = std::move(probe.problem)};
}

void configure(const Choices& choices)
{
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    state.options.device_index = choices.device_index;
    state.options.validation = choices.validation;
    state.options.ray_tracing = choices.ray_tracing;
    state.options.on_message = log_message;
    state.cache_folder = choices.cache_folder;
}

void set_context_options(const ContextOptions& options, SDL_Window* window)
{
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    state.options = options;
    state.window = window;
}

} // namespace render::vulkan
