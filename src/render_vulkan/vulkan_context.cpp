// NOT ORIGINAL - the Vulkan backend's context: instance, surface, device, swapchain and the frames (vulkan_context.h).
#include "render_vulkan/vulkan_context.h"

#include <SDL2/SDL_stdinc.h>
#include <SDL2/SDL_video.h>
#include <SDL2/SDL_vulkan.h>
#include <algorithm>
#include <array>
#include <cstdint>
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
#include "render_vulkan/vulkan_loader.h"

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

// The swapchain's size: the surface's, or the window's drawable size within the surface's limits.
VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR& caps, SDL_Window* window)
{
    if (caps.currentExtent.width != kExtentFromWindow) {
        return caps.currentExtent;
    }
    int width = 0;
    int height = 0;
    SDL_Vulkan_GetDrawableSize(window, &width, &height);
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
    on_message_ = options.on_message;
    window_ = window;
    if (!open_loader()) {
        return fail("opening vulkan-1.dll", VK_ERROR_INITIALIZATION_FAILED);
    }
    instance_version_ = loader_version();
    return create_instance(options.validation) && create_surface() && pick_physical_device(options.device_index) && create_device() &&
           create_frames() && create_swapchain();
}

bool Context::create_instance(bool validation)
{
    unsigned int sdl_count = 0;
    if (SDL_Vulkan_GetInstanceExtensions(window_, &sdl_count, nullptr) != SDL_TRUE) {
        return fail("SDL_Vulkan_GetInstanceExtensions", VK_ERROR_EXTENSION_NOT_PRESENT);
    }
    std::vector<const char*> extensions(sdl_count);
    SDL_Vulkan_GetInstanceExtensions(window_, &sdl_count, extensions.data());
    const bool use_validation = validation && instance_layer_available(kValidationLayer) &&
                                has_extension(instance_extensions(), VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    std::vector<const char*> layers;
    if (use_validation) {
        layers.push_back(kValidationLayer);
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
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
    }
    return true;
}

bool Context::create_surface()
{
    if (SDL_Vulkan_CreateSurface(window_, instance_, &surface_) != SDL_TRUE) {
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
    const std::array<const char*, 1> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    const VkDeviceCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
        .pEnabledFeatures = nullptr,
    };
    const VkResult result = vkCreateDevice(physical_device_, &info, nullptr, &device_);
    if (result != VK_SUCCESS) {
        return fail("vkCreateDevice", result);
    }
    volkLoadDevice(device_);
    vkGetDeviceQueue(device_, queue_family_, 0, &queue_);
    return true;
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
    present_mode_ = VK_PRESENT_MODE_FIFO_KHR; // (every device has it: the game's default present interval)
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

void Context::destroy()
{
    if (device_ != nullptr) {
        vkDeviceWaitIdle(device_);
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

} // namespace render::vulkan
