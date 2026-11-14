// NOT ORIGINAL - the Vulkan backend's context: instance, surface, device, swapchain and the frames (vulkan_context.h).
#include "gpu_vulkan/vulkan_context.h"

#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "gpu_vulkan/vulkan_loader.h"

namespace render::vulkan {

namespace {

constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";
// The instance version asked for: what the loader offers, up to 1.3 (dynamic rendering, synchronization2 - the
// backend's later steps may use them where the device has them).
constexpr uint32_t kHighestApiVersion = VK_API_VERSION_1_3;
// The surface's "the size is the window's" value (VkSurfaceCapabilitiesKHR::currentExtent).
constexpr uint32_t kExtentFromWindow = std::numeric_limits<uint32_t>::max();
constexpr uint64_t kNoTimeout = std::numeric_limits<uint64_t>::max();

std::string_view name_of(const char* text) { return text; }

// NOT ORIGINAL - ContextAdditions: the most queues of the graphics family made.
constexpr std::size_t kMostQueues = 8;

// What a library asks for of a features structure, and what the device has.
template <typename Features> struct Asked {
    const Features* wanted = nullptr;
    const Features* offered = nullptr;
};

// The VkBool32 flags of a features structure (from `first`: after its sType and pNext) or'ed into `into` where the
// library asks and the device has them; whether any was.
template <typename Features> bool merge_flags(const Asked<Features>& asked_for, std::size_t first, Features* into)
{
    const Features& wanted = *asked_for.wanted;
    const Features& offered = *asked_for.offered;
    constexpr std::size_t kFlag = sizeof(VkBool32);
    using Bytes = std::array<std::byte, sizeof(Features)>;
    const auto asked = std::bit_cast<Bytes>(wanted);
    const auto has = std::bit_cast<Bytes>(offered);
    auto set = std::bit_cast<Bytes>(*into);
    const auto flag_at = [](const Bytes& bytes, std::size_t at) {
        VkBool32 value = VK_FALSE;
        std::memcpy(&value, std::span(bytes).subspan(at, kFlag).data(), kFlag);
        return value;
    };
    bool any = false;
    for (std::size_t at = first; at + kFlag <= sizeof(Features); at += kFlag) {
        if (flag_at(asked, at) == VK_TRUE && flag_at(has, at) == VK_TRUE && flag_at(set, at) != VK_TRUE) {
            const VkBool32 on = VK_TRUE;
            std::memcpy(std::span(set).subspan(at, kFlag).data(), &on, kFlag);
            any = true;
        }
    }
    *into = std::bit_cast<Features>(set);
    return any;
}

// The structures a library chains (ContextAdditions::DeviceWants::chain) after the last of `head`'s chain.
void chain_last(VkPhysicalDeviceVulkan13Features* head, const std::vector<VkBaseOutStructure*>& chain)
{
    if (chain.empty()) {
        return;
    }
    for (std::size_t index = 0; index + 1 < chain.size(); ++index) {
        core::at(chain, index)->pNext = core::at(chain, index + 1);
    }
    chain.back()->pNext = nullptr;
    if (head->pNext == nullptr) {
        head->pNext = chain.front();
        return;
    }
    auto* last = static_cast<VkBaseOutStructure*>(head->pNext);
    while (last->pNext != nullptr) {
        last = last->pNext;
    }
    last->pNext = chain.front();
}

// A library's Vulkan 1.2 / 1.3 features (ContextAdditions::DeviceWants) added where the device has them; whether any
// 1.2 feature was.
bool add_features(VkPhysicalDevice physical, const ContextAdditions::DeviceWants& wants,
                  VkPhysicalDeviceVulkan12Features* features12, VkPhysicalDeviceVulkan13Features* features13)
{
    VkPhysicalDeviceVulkan12Features offered12{};
    offered12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    VkPhysicalDeviceVulkan13Features offered13{};
    offered13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    offered12.pNext = &offered13;
    VkPhysicalDeviceFeatures2 asked{};
    asked.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    asked.pNext = &offered12;
    vkGetPhysicalDeviceFeatures2(physical, &asked);
    const bool any12 = merge_flags(Asked<VkPhysicalDeviceVulkan12Features>{.wanted = &wants.features12, .offered = &offered12},
                                   offsetof(VkPhysicalDeviceVulkan12Features, samplerMirrorClampToEdge), features12);
    merge_flags(Asked<VkPhysicalDeviceVulkan13Features>{.wanted = &wants.features13, .offered = &offered13},
                offsetof(VkPhysicalDeviceVulkan13Features, robustImageAccess), features13);
    return any12;
}

// The instance extensions the loader lists.
std::vector<VkExtensionProperties> instance_extensions()
{
    uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> found(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, found.data());
    found.resize(count);
    return found;
}

bool has_extension(const std::vector<VkExtensionProperties>& extensions, std::string_view name)
{
    return std::ranges::any_of(extensions, [name](const VkExtensionProperties& extension) {
        return name_of(std::data(extension.extensionName)) == name;
    });
}

std::vector<VkExtensionProperties> device_extensions(VkPhysicalDevice device)
{
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> found(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, found.data());
    found.resize(count);
    return found;
}

// The validation layer's messages, to the context's sink (the context is the callback's user data).
VKAPI_ATTR VkBool32 VKAPI_CALL on_validation_message(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                     VkDebugUtilsMessageTypeFlagsEXT /*types*/,
                                                     const VkDebugUtilsMessengerCallbackDataEXT* data, void* user)
{
    const auto* context = static_cast<const Context*>(user);
    if (context != nullptr && data != nullptr && data->pMessage != nullptr) {
        const char* level = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0 ? "error" : "warning";
        context->report(text::format("validation %s: %s", level, data->pMessage));
    }
    return VK_FALSE;
}

// How much a device type is preferred: a discrete GPU, then an integrated one, then the rest.
int preference(VkPhysicalDeviceType type)
{
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return 3;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return 2;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
    case VK_PHYSICAL_DEVICE_TYPE_OTHER:
    default:
        return 1;
    }
}

// The first queue family that draws and presents; none when there is no such family.
std::optional<uint32_t> graphics_and_present(const std::vector<QueueFamilyInfo>& families)
{
    const auto found = std::ranges::find_if(families, [](const QueueFamilyInfo& family) {
        return family.presents && (family.flags & VK_QUEUE_GRAPHICS_BIT) != 0;
    });
    return found == families.end() ? std::nullopt : std::optional<uint32_t>(found->index);
}

VkSurfaceFormatKHR choose_surface_format(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, formats.data());
    formats.resize(count);
    // The game's back buffer is 8-bit BGRA without sRGB conversion (Direct3D 9's X8R8G8B8): the same when offered.
    const auto same = std::ranges::find_if(formats, [](const VkSurfaceFormatKHR& format) {
        return format.format == VK_FORMAT_B8G8R8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    });
    if (same != formats.end()) {
        return *same;
    }
    return formats.empty() ? VkSurfaceFormatKHR{} : formats.front();
}

// NOT ORIGINAL - [Graphics] present_mode: `wanted` where the surface offers it; else mailbox or immediate for the other
// (both don't wait for the display), else FIFO (every surface has it).
VkPresentModeKHR choose_present_mode(VkPhysicalDevice device, VkSurfaceKHR surface, VkPresentModeKHR wanted)
{
    if (wanted == VK_PRESENT_MODE_FIFO_KHR) {
        return wanted;
    }
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &count, nullptr);
    std::vector<VkPresentModeKHR> modes(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &count, modes.data());
    modes.resize(count);
    const VkPresentModeKHR other =
        wanted == VK_PRESENT_MODE_MAILBOX_KHR ? VK_PRESENT_MODE_IMMEDIATE_KHR : VK_PRESENT_MODE_MAILBOX_KHR;
    for (const VkPresentModeKHR mode : {wanted, other}) {
        if (std::ranges::find(modes, mode) != modes.end()) {
            return mode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

// The swapchain's size: the surface's, or the window's drawable size within the surface's limits.
VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR& caps, SDL_Window* window)
{
    if (caps.currentExtent.width != kExtentFromWindow) {
        return caps.currentExtent;
    }
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window, &width, &height);
    return VkExtent2D{
        .width = std::clamp(static_cast<uint32_t>(width), caps.minImageExtent.width, caps.maxImageExtent.width),
        .height = std::clamp(static_cast<uint32_t>(height), caps.minImageExtent.height, caps.maxImageExtent.height),
    };
}

VkCompositeAlphaFlagBitsKHR choose_composite_alpha(VkCompositeAlphaFlagsKHR supported)
{
    for (const VkCompositeAlphaFlagBitsKHR mode :
         {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
          VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR}) {
        if ((supported & mode) != 0) {
            return mode;
        }
    }
    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

// An image's whole colour range.
constexpr VkImageSubresourceRange kColourRange{
    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    .baseMipLevel = 0,
    .levelCount = 1,
    .baseArrayLayer = 0,
    .layerCount = 1,
};

VkImageMemoryBarrier layout_change(VkImage image, VkImageLayout from, VkImageLayout to, VkAccessFlags before,
                                   VkAccessFlags after)
{
    return VkImageMemoryBarrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = before,
        .dstAccessMask = after,
        .oldLayout = from,
        .newLayout = to,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = kColourRange,
    };
}

// Records `image` cleared to `colour` and left ready to present.
void record_clear(VkCommandBuffer commands, VkImage image, const std::array<float, 4>& colour)
{
    const VkCommandBufferBeginInfo begin{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr,
    };
    vkBeginCommandBuffer(commands, &begin);
    const VkImageMemoryBarrier to_clear = layout_change(image, VK_IMAGE_LAYOUT_UNDEFINED,
                                                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                                                        VK_ACCESS_TRANSFER_WRITE_BIT);
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &to_clear);
    VkClearColorValue value{};
    std::ranges::copy(colour, std::begin(value.float32));
    vkCmdClearColorImage(commands, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1, &kColourRange);
    const VkImageMemoryBarrier to_present = layout_change(image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                          VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                                          VK_ACCESS_TRANSFER_WRITE_BIT, 0);
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &to_present);
    vkEndCommandBuffer(commands);
}

// Whether both the instance and the device speak Vulkan 1.3 (dynamic rendering is core there).
bool draws_api(VkPhysicalDevice device, uint32_t instance_api)
{
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device, &properties);
    return instance_api >= VK_API_VERSION_1_3 && properties.apiVersion >= VK_API_VERSION_1_3;
}

bool dynamic_rendering_offered(VkPhysicalDevice device)
{
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    VkPhysicalDeviceFeatures2 features{};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &features13;
    vkGetPhysicalDeviceFeatures2(device, &features);
    return features13.dynamicRendering == VK_TRUE;
}

} // namespace

std::vector<QueueFamilyInfo> queue_families(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> properties(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, properties.data());
    properties.resize(count);
    std::vector<QueueFamilyInfo> families;
    uint32_t index = 0;
    for (const VkQueueFamilyProperties& family : properties) {
        VkBool32 presents = VK_FALSE;
        if (surface != VK_NULL_HANDLE) {
            vkGetPhysicalDeviceSurfaceSupportKHR(device, index, surface, &presents);
        }
        families.push_back({.index = index, .flags = family.queueFlags, .count = family.queueCount,
                            .presents = presents == VK_TRUE});
        ++index;
    }
    return families;
}

bool instance_layer_available(const char* name)
{
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    layers.resize(count);
    return std::ranges::any_of(
        layers, [name](const VkLayerProperties& layer) { return name_of(std::data(layer.layerName)) == name; });
}

VkFormat first_depth_format(VkPhysicalDevice device, std::span<const VkFormat> candidates)
{
    for (const VkFormat format : candidates) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(device, format, &properties);
        if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) {
            return format;
        }
    }
    return VK_FORMAT_UNDEFINED;
}

Context::~Context()
{
    destroy();
}

void Context::report(const std::string& text) const
{
    if (on_message_ != nullptr) {
        on_message_(text);
    }
}

bool Context::fail(const char* step, VkResult result) const
{
    report(text::format("%s failed: VkResult %d", step, static_cast<int>(result)));
    return false;
}

bool Context::create(SDL_Window* window, const ContextOptions& options)
{
    return open(window, options) && create_swapchain();
}

bool Context::open(SDL_Window* window, const ContextOptions& options)
{
    on_message_ = options.on_message;
    ray_tracing_wanted_ = options.ray_tracing;
    additions_ = options.additions;
    window_ = window;
    if (!open_loader()) {
        return fail("opening vulkan-1.dll", VK_ERROR_INITIALIZATION_FAILED);
    }
    instance_version_ = loader_version();
    return create_instance(options.validation) && create_surface() && pick_physical_device(options.device_index) &&
           create_device() && create_frames();
}

bool Context::create_instance(bool validation)
{
    Uint32 sdl_count = 0;
    const char* const* sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_count);
    if (!sdl_extensions) {
        return fail("SDL_Vulkan_GetInstanceExtensions", VK_ERROR_EXTENSION_NOT_PRESENT);
    }
    const std::span<const char* const> sdl_listed(sdl_extensions, sdl_count);
    std::vector<const char*> extensions(sdl_listed.begin(), sdl_listed.end());
    const bool use_validation = validation && instance_layer_available(kValidationLayer) &&
                                has_extension(instance_extensions(), VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (validation && !use_validation) {
        report("validation asked for, but the layer or VK_EXT_debug_utils isn't there - going on without it");
    }
    std::vector<const char*> layers;
    if (use_validation) {
        layers.push_back(kValidationLayer);
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    // NOT ORIGINAL - a library's additions (ContextAdditions), without repeats
    if (additions_ != nullptr) {
        std::vector<const char*> added;
        additions_->instance_extensions(&added);
        for (const char* name : added) {
            if (std::ranges::none_of(extensions, [name](const char* had) { return std::string_view(had) == name; })) {
                extensions.push_back(name);
            }
        }
    }
    const VkApplicationInfo application{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = "thug2-decomp",
        .applicationVersion = 1,
        .pEngineName = "thug2-decomp",
        .engineVersion = 1,
        .apiVersion = std::min(instance_version_, kHighestApiVersion),
    };
    api_version_ = application.apiVersion;
    const VkInstanceCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .pApplicationInfo = &application,
        .enabledLayerCount = static_cast<uint32_t>(layers.size()),
        .ppEnabledLayerNames = layers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };
    const VkResult result = vkCreateInstance(&info, nullptr, &instance_);
    if (result == VK_ERROR_LAYER_NOT_PRESENT && use_validation) {
        // (the layer is listed but can't be loaded into this process - a 64-bit layer for a 32-bit one): without it
        report("the validation layer is listed but can't be loaded into this process - going on without it");
        return create_instance(false);
    }
    if (result != VK_SUCCESS) {
        return fail("vkCreateInstance", result);
    }
    volkLoadInstance(instance_);
    if (use_validation) {
        const VkDebugUtilsMessengerCreateInfoEXT messenger{
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .pNext = nullptr,
            .flags = 0,
            .messageSeverity =
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = on_validation_message,
            .pUserData = this,
        };
        const VkResult made = vkCreateDebugUtilsMessengerEXT(instance_, &messenger, nullptr, &debug_messenger_);
        if (made != VK_SUCCESS) {
            return fail("vkCreateDebugUtilsMessengerEXT", made);
        }
        report("the validation layer is on (its warnings and errors logged)");
    }
    return true;
}

bool Context::create_surface()
{
    if (!SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_)) {
        return fail("SDL_Vulkan_CreateSurface", VK_ERROR_INITIALIZATION_FAILED);
    }
    return true;
}

bool Context::pick_physical_device(int wanted)
{
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());
    devices.resize(count);
    int best = 0;
    int index = -1;
    for (VkPhysicalDevice device : devices) {
        if (++index != wanted && wanted >= 0) {
            continue;
        }
        const std::optional<uint32_t> family = graphics_and_present(queue_families(device, surface_));
        if (!family || !has_extension(device_extensions(device), VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            continue;
        }
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);
        const int score = preference(properties.deviceType);
        if (score > best) {
            best = score;
            physical_device_ = device;
            queue_family_ = *family;
        }
    }
    if (physical_device_ == nullptr) {
        return fail(count == 0 ? "finding a Vulkan device" : "finding a device that draws and presents to the window",
                    VK_ERROR_INCOMPATIBLE_DRIVER);
    }
    return true;
}

bool Context::create_device()
{
    const float priority = 1.0F;
    const VkDeviceQueueCreateInfo queue{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queueFamilyIndex = queue_family_,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };
    std::vector<const char*> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    // The features the Gpu's resources and draws use, where the device has them (none is required here: the Gpu asks
    // draws_supported()).
    VkPhysicalDeviceFeatures offered{};
    vkGetPhysicalDeviceFeatures(physical_device_, &offered);
    enabled_features_ = VkPhysicalDeviceFeatures{};
    enabled_features_.samplerAnisotropy = offered.samplerAnisotropy;
    enabled_features_.textureCompressionBC = offered.textureCompressionBC;
    enabled_features_.fillModeNonSolid = offered.fillModeNonSolid;
    // NOT ORIGINAL - the upscaler's motion attachment blends apart from the colour (vulkan_motion.h)
    enabled_features_.independentBlend = offered.independentBlend;
    // the draws: Vulkan 1.3's dynamic rendering (no render pass objects) and push descriptors
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    dynamic_rendering_ = draws_api(physical_device_, api_version_) && dynamic_rendering_offered(physical_device_);
    features13.dynamicRendering = dynamic_rendering_ ? VK_TRUE : VK_FALSE;
    push_descriptors_ = has_extension(device_extensions(physical_device_), VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
    if (push_descriptors_) {
        extensions.push_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
    }
    // NOT ORIGINAL - ray tracing (ContextOptions::ray_tracing): the extensions and the features they need, chained after
    // Vulkan 1.3's - all of them or none
    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR structures{};
    structures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    VkPhysicalDeviceRayQueryFeaturesKHR queries{};
    queries.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    ray_tracing_ = false;
    if (ray_tracing_wanted_ && dynamic_rendering_) {
        const std::vector<VkExtensionProperties> offered_extensions = device_extensions(physical_device_);
        const std::array<const char*, 3> wanted{VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
                                                VK_KHR_RAY_QUERY_EXTENSION_NAME,
                                                VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME};
        bool all = true;
        for (const char* name : wanted) {
            all = all && has_extension(offered_extensions, name);
        }
        if (all) {
            VkPhysicalDeviceVulkan12Features offered12{};
            offered12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
            VkPhysicalDeviceAccelerationStructureFeaturesKHR offered_structures{};
            offered_structures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
            VkPhysicalDeviceRayQueryFeaturesKHR offered_queries{};
            offered_queries.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
            offered12.pNext = &offered_structures;
            offered_structures.pNext = &offered_queries;
            VkPhysicalDeviceFeatures2 asked{};
            asked.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            asked.pNext = &offered12;
            vkGetPhysicalDeviceFeatures2(physical_device_, &asked);
            all = offered12.bufferDeviceAddress && offered12.descriptorIndexing && offered12.runtimeDescriptorArray &&
                  offered12.descriptorBindingPartiallyBound && offered12.descriptorBindingVariableDescriptorCount &&
                  offered12.shaderSampledImageArrayNonUniformIndexing &&
                  offered12.descriptorBindingSampledImageUpdateAfterBind && offered12.scalarBlockLayout &&
                  offered_structures.accelerationStructure && offered_queries.rayQuery;
        }
        if (all) {
            for (const char* name : wanted) {
                extensions.push_back(name);
            }
            features12.bufferDeviceAddress = VK_TRUE;
            features12.descriptorIndexing = VK_TRUE;
            features12.runtimeDescriptorArray = VK_TRUE;
            features12.descriptorBindingPartiallyBound = VK_TRUE;
            features12.descriptorBindingVariableDescriptorCount = VK_TRUE;
            features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
            features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
            features12.scalarBlockLayout = VK_TRUE;
            structures.accelerationStructure = VK_TRUE;
            queries.rayQuery = VK_TRUE;
            features13.pNext = &features12;
            features12.pNext = &structures;
            structures.pNext = &queries;
            // (ray-traced shadows trace each sample of a multisampled target: vulkan_rt_shadows.h)
            enabled_features_.sampleRateShading = offered.sampleRateShading;
            ray_tracing_ = true;
        }
    }
    // NOT ORIGINAL - a library's additions (ContextAdditions): its extensions, features and extra queues
    std::array<float, kMostQueues> priorities{};
    priorities.fill(priority);
    VkDeviceQueueCreateInfo queues = queue;
    uint32_t extra_queues = 0;
    if (additions_ != nullptr) {
        extra_queues = add_requested(&extensions, &features12, &features13, offered);
        queues.queueCount = 1 + extra_queues;
        queues.pQueuePriorities = priorities.data();
    }
    const VkDeviceCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = dynamic_rendering_ ? &features13 : nullptr,
        .flags = 0,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queues,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
        .pEnabledFeatures = &enabled_features_,
    };
    const VkResult result = vkCreateDevice(physical_device_, &info, nullptr, &device_);
    if (result != VK_SUCCESS) {
        return fail("vkCreateDevice", result);
    }
    volkLoadDevice(device_);
    vkGetDeviceQueue(device_, queue_family_, 0, &queue_);
    if (additions_ != nullptr) {
        additions_->device_made({.instance = instance_,
                                 .physical = physical_device_,
                                 .device = device_,
                                 .family = queue_family_,
                                 .extra_queues = extra_queues});
    }
    return true;
}

// NOT ORIGINAL - a library's additions (ContextAdditions): its extensions (without repeats, the device's only), the
// features the device has of those it asks for (Vulkan 1.0's, 1.2's, 1.3's, then its own structures chained last), and
// how many extra queues of the graphics family it gets.
uint32_t Context::add_requested(std::vector<const char*>* extensions, VkPhysicalDeviceVulkan12Features* features12,
                                VkPhysicalDeviceVulkan13Features* features13, const VkPhysicalDeviceFeatures& offered)
{
    ContextAdditions::DeviceWants wants;
    wants.features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    wants.features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    additions_->device_extensions(physical_device_, &wants);
    const std::vector<VkExtensionProperties> offered_extensions = device_extensions(physical_device_);
    for (const char* name : wants.extensions) {
        const bool repeated =
            std::ranges::any_of(*extensions, [name](const char* had) { return std::string_view(had) == name; });
        if (!repeated && has_extension(offered_extensions, name)) {
            extensions->push_back(name);
        }
    }
    if (wants.shader_int16) {
        enabled_features_.shaderInt16 = offered.shaderInt16;
    }
    if (dynamic_rendering_) {
        if (add_features(physical_device_, wants, features12, features13) && features13->pNext == nullptr) {
            features13->pNext = features12; // (not chained by ray tracing: chained now)
        }
        chain_last(features13, wants.chain);
    }
    uint32_t family_queues = 1;
    for (const QueueFamilyInfo& family : queue_families(physical_device_, surface_)) {
        if (family.index == queue_family_) {
            family_queues = family.count;
        }
    }
    return std::min({wants.extra_queues, family_queues - 1, static_cast<uint32_t>(kMostQueues) - 1});
}

bool Context::create_frames()
{
    for (Frame& frame : frames_) {
        const VkCommandPoolCreateInfo pool{
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
            .queueFamilyIndex = queue_family_,
        };
        VkResult result = vkCreateCommandPool(device_, &pool, nullptr, &frame.pool);
        if (result != VK_SUCCESS) {
            return fail("vkCreateCommandPool", result);
        }
        const VkCommandBufferAllocateInfo allocate{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = nullptr,
            .commandPool = frame.pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        result = vkAllocateCommandBuffers(device_, &allocate, &frame.commands);
        if (result != VK_SUCCESS) {
            return fail("vkAllocateCommandBuffers", result);
        }
        // (made signalled: the first wait on it returns at once)
        const VkFenceCreateInfo fence{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .pNext = nullptr, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
        result = vkCreateFence(device_, &fence, nullptr, &frame.done);
        if (result != VK_SUCCESS) {
            return fail("vkCreateFence", result);
        }
        const VkSemaphoreCreateInfo semaphore{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = nullptr, .flags = 0};
        result = vkCreateSemaphore(device_, &semaphore, nullptr, &frame.acquired);
        if (result != VK_SUCCESS) {
            return fail("vkCreateSemaphore", result);
        }
    }
    return true;
}

bool Context::create_swapchain()
{
    VkSurfaceCapabilitiesKHR caps; // (filled by the call below)
    VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &caps);
    if (result != VK_SUCCESS) {
        return fail("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", result);
    }
    const VkExtent2D extent = choose_extent(caps, window_);
    if (extent.width == 0 || extent.height == 0) {
        // minimised: no swapchain until the window has a size again
        destroy_swapchain(std::exchange(swapchain_, VK_NULL_HANDLE));
        return true;
    }
    surface_format_ = choose_surface_format(physical_device_, surface_);
    // (FIFO: every device has it - the game's default present interval; not original: [Graphics] present_mode's choice)
    const VkPresentModeKHR previous_mode = present_mode_;
    present_mode_ = choose_present_mode(physical_device_, surface_, wanted_present_mode_);
    if (present_mode_ != wanted_present_mode_ || (present_mode_ != previous_mode && swapchains_made_ > 0)) {
        report(std::string("present mode: ") +
               (present_mode_ == VK_PRESENT_MODE_FIFO_KHR      ? "FIFO (vsync)"
                : present_mode_ == VK_PRESENT_MODE_MAILBOX_KHR ? "mailbox"
                                                               : "immediate") +
               (present_mode_ != wanted_present_mode_ ? " (the one asked for isn't offered)" : ""));
    }
    uint32_t wanted_images = caps.minImageCount + 1;
    if (caps.maxImageCount != 0) {
        wanted_images = std::min(wanted_images, caps.maxImageCount);
    }
    // (copied from later, for screenshots, where the surface allows it)
    const VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                    (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    const VkSwapchainKHR old = swapchain_;
    const VkSwapchainCreateInfoKHR info{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = 0,
        .surface = surface_,
        .minImageCount = wanted_images,
        .imageFormat = surface_format_.format,
        .imageColorSpace = surface_format_.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = usage,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .preTransform = caps.currentTransform,
        .compositeAlpha = choose_composite_alpha(caps.supportedCompositeAlpha),
        .presentMode = present_mode_,
        .clipped = VK_TRUE,
        .oldSwapchain = old,
    };
    result = vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_);
    destroy_swapchain(old);
    if (result != VK_SUCCESS) {
        swapchain_ = VK_NULL_HANDLE;
        return fail("vkCreateSwapchainKHR", result);
    }
    extent_ = extent;
    ++swapchains_made_;
    uint32_t count = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr);
    images_.resize(count);
    vkGetSwapchainImagesKHR(device_, swapchain_, &count, images_.data());
    images_.resize(count);
    presented_.assign(count, VK_NULL_HANDLE);
    for (VkSemaphore& semaphore : presented_) {
        const VkSemaphoreCreateInfo semaphore_info{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = nullptr, .flags = 0};
        result = vkCreateSemaphore(device_, &semaphore_info, nullptr, &semaphore);
        if (result != VK_SUCCESS) {
            return fail("vkCreateSemaphore", result);
        }
    }
    return true;
}

// Releases `swapchain` and the per-image semaphores (which belong to the swapchain being replaced or released).
void Context::destroy_swapchain(VkSwapchainKHR swapchain)
{
    for (const VkSemaphore semaphore : presented_) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, semaphore, nullptr);
        }
    }
    presented_.clear();
    images_.clear();
    if (swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, swapchain, nullptr);
    }
}

bool Context::surface_changed() const
{
    if (swapchain_ == VK_NULL_HANDLE || resized_) {
        return false;
    }
    VkSurfaceCapabilitiesKHR caps; // (filled by the call below)
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &caps) != VK_SUCCESS) {
        return false;
    }
    const VkExtent2D now = choose_extent(caps, window_);
    return now.width != extent_.width || now.height != extent_.height;
}

bool Context::remake_swapchain()
{
    vkDeviceWaitIdle(device_);
    resized_ = false;
    return create_swapchain();
}

FrameResult Context::clear_frame(const std::array<float, 4>& colour)
{
    if (swapchain_ == VK_NULL_HANDLE || resized_) {
        // (minimised before, or resized: try at the current size)
        if (!remake_swapchain()) {
            return FrameResult::kFailed;
        }
        if (swapchain_ == VK_NULL_HANDLE) {
            return FrameResult::kSkipped;
        }
    }
    const Frame& frame = core::at(frames_, frame_index_);
    vkWaitForFences(device_, 1, &frame.done, VK_TRUE, kNoTimeout);
    uint32_t image = 0;
    VkResult result = vkAcquireNextImageKHR(device_, swapchain_, kNoTimeout, frame.acquired, VK_NULL_HANDLE, &image);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        return remake_swapchain() ? FrameResult::kRemade : FrameResult::kFailed;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        fail("vkAcquireNextImageKHR", result);
        return FrameResult::kFailed;
    }
    vkResetFences(device_, 1, &frame.done);
    vkResetCommandPool(device_, frame.pool, 0);
    record_clear(frame.commands, core::at(images_, image), colour);
    const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    const VkSemaphore presented = core::at(presented_, image);
    const VkSubmitInfo submit{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &frame.acquired,
        .pWaitDstStageMask = &wait_stage,
        .commandBufferCount = 1,
        .pCommandBuffers = &frame.commands,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &presented,
    };
    result = vkQueueSubmit(queue_, 1, &submit, frame.done);
    if (result != VK_SUCCESS) {
        fail("vkQueueSubmit", result);
        return FrameResult::kFailed;
    }
    const VkPresentInfoKHR present{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = nullptr,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &presented,
        .swapchainCount = 1,
        .pSwapchains = &swapchain_,
        .pImageIndices = &image,
        .pResults = nullptr,
    };
    result = vkQueuePresentKHR(queue_, &present);
    frame_index_ = (frame_index_ + 1) % kFramesInFlight;
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || resized_) {
        return remake_swapchain() ? FrameResult::kPresented : FrameResult::kFailed;
    }
    if (result != VK_SUCCESS) {
        fail("vkQueuePresentKHR", result);
        return FrameResult::kFailed;
    }
    return FrameResult::kPresented;
}

VkResult Context::acquire(VkSemaphore acquired, uint32_t* image)
{
    if (swapchain_ == VK_NULL_HANDLE) {
        return VK_ERROR_OUT_OF_DATE_KHR;
    }
    return vkAcquireNextImageKHR(device_, swapchain_, kNoTimeout, acquired, VK_NULL_HANDLE, image);
}

VkResult Context::queue_present(uint32_t image)
{
    const VkSemaphore presented = core::at(presented_, image);
    const VkPresentInfoKHR present{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = nullptr,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &presented,
        .swapchainCount = 1,
        .pSwapchains = &swapchain_,
        .pImageIndices = &image,
        .pResults = nullptr,
    };
    const VkResult result = vkQueuePresentKHR(queue_, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        resized_ = true; // (made again before the next frame)
    }
    return result;
}

VkImage Context::swapchain_image(uint32_t image) const { return core::at(images_, image); }

VkSemaphore Context::present_semaphore(uint32_t image) const { return core::at(presented_, image); }

DeviceProbe probe_device(int wanted)
{
    DeviceProbe probe;
    if (!open_loader()) {
        probe.problem = "no Vulkan loader (vulkan-1.dll)";
        return probe;
    }
    const uint32_t api = std::min(loader_version(), kHighestApiVersion);
    const VkApplicationInfo application{.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                        .pNext = nullptr,
                                        .pApplicationName = "thug2-decomp",
                                        .applicationVersion = 1,
                                        .pEngineName = "thug2-decomp",
                                        .engineVersion = 1,
                                        .apiVersion = api};
    const VkInstanceCreateInfo info{.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                    .pNext = nullptr,
                                    .flags = 0,
                                    .pApplicationInfo = &application,
                                    .enabledLayerCount = 0,
                                    .ppEnabledLayerNames = nullptr,
                                    .enabledExtensionCount = 0,
                                    .ppEnabledExtensionNames = nullptr};
    VkInstance instance = nullptr;
    if (vkCreateInstance(&info, nullptr, &instance) != VK_SUCCESS) {
        probe.problem = "vkCreateInstance failed";
        return probe;
    }
    volkLoadInstanceOnly(instance);
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());
    devices.resize(count);
    // the device the context would pick (its preference, without the window's surface)
    VkPhysicalDevice chosen = nullptr;
    int best = 0;
    int index = -1;
    for (VkPhysicalDevice device : devices) {
        if (++index != wanted && wanted >= 0) {
            continue;
        }
        const std::vector<QueueFamilyInfo> families = queue_families(device, VK_NULL_HANDLE);
        const bool draws = std::ranges::any_of(
            families, [](const QueueFamilyInfo& family) { return (family.flags & VK_QUEUE_GRAPHICS_BIT) != 0; });
        if (!draws || !has_extension(device_extensions(device), VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            continue;
        }
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);
        const int score = preference(properties.deviceType);
        if (score > best) {
            best = score;
            chosen = device;
        }
    }
    if (chosen == nullptr) {
        probe.problem = wanted >= 0 ? text::format("no device %d that draws", wanted) : std::string("no device that draws");
    } else {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(chosen, &properties);
        probe.device = std::data(properties.deviceName);
        if (!draws_api(chosen, api)) {
            probe.problem = "it has no Vulkan 1.3";
        } else if (!dynamic_rendering_offered(chosen)) {
            probe.problem = "it has no dynamic rendering";
        } else if (!has_extension(device_extensions(chosen), VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME)) {
            probe.problem = "it has no push descriptors (VK_KHR_push_descriptor)";
        } else {
            probe.usable = true;
        }
    }
    vkDestroyInstance(instance, nullptr);
    return probe;
}

void Context::destroy()
{
    if (device_ != nullptr) {
        vkDeviceWaitIdle(device_);
        if (additions_ != nullptr) {
            additions_->before_destroy(); // (NOT ORIGINAL - ContextAdditions)
        }
        destroy_swapchain(std::exchange(swapchain_, VK_NULL_HANDLE));
        for (Frame& frame : frames_) {
            if (frame.acquired != VK_NULL_HANDLE) {
                vkDestroySemaphore(device_, frame.acquired, nullptr);
            }
            if (frame.done != VK_NULL_HANDLE) {
                vkDestroyFence(device_, frame.done, nullptr);
            }
            if (frame.pool != VK_NULL_HANDLE) {
                vkDestroyCommandPool(device_, frame.pool, nullptr); // (frees its command buffer)
            }
            frame = Frame{};
        }
        vkDestroyDevice(std::exchange(device_, nullptr), nullptr);
    }
    if (instance_ != nullptr) {
        if (surface_ != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance_, std::exchange(surface_, VK_NULL_HANDLE), nullptr);
        }
        if (debug_messenger_ != VK_NULL_HANDLE) {
            vkDestroyDebugUtilsMessengerEXT(instance_, std::exchange(debug_messenger_, VK_NULL_HANDLE), nullptr);
        }
        vkDestroyInstance(std::exchange(instance_, nullptr), nullptr);
    }
    physical_device_ = nullptr;
    queue_ = nullptr;
    frame_index_ = 0;
}

VkSurfaceFormatKHR surface_format_for(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    return choose_surface_format(device, surface);
}

VkExtent2D surface_extent_for(const VkSurfaceCapabilitiesKHR& caps, SDL_Window* window)
{
    return choose_extent(caps, window);
}

VkCompositeAlphaFlagBitsKHR composite_alpha_for(VkCompositeAlphaFlagsKHR supported)
{
    return choose_composite_alpha(supported);
}

} // namespace render::vulkan
