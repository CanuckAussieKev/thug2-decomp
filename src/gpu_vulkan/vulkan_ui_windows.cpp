// NOT ORIGINAL - the UI's own windows on Vulkan (vulkan_backend.h, UiWindow): Dear ImGui's platform windows - a panel of
// the level editor dragged out of the game's window - each with its own surface and swapchain on the game's device and
// queue. They are drawn in the stream of the frame being recorded (record_ui_window_draws, vulkan_draws.cpp), so what
// they show - the Scene panel's picture too - is what the frame drew; the frame's submission waits for their acquires
// and signals their presents, and they are presented after it (present_ui_windows). They never wait for the display:
// their presents are immediate (or mailbox) where the surface offers it, so a panel's window never slows the game.
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_context.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

constexpr uint64_t kNoTimeout = std::numeric_limits<uint64_t>::max();

// The swapchain's images' views and per-image semaphores, and the swapchain, released (its surface kept).
void release_swapchain(VkDevice device, UiWindow* window)
{
    for (const VkImageView view : window->views) {
        vkDestroyImageView(device, view, nullptr);
    }
    for (const VkSemaphore semaphore : window->presented) {
        vkDestroySemaphore(device, semaphore, nullptr);
    }
    window->views.clear();
    window->presented.clear();
    window->images.clear();
    if (window->swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, window->swapchain, nullptr);
        window->swapchain = VK_NULL_HANDLE;
    }
    window->extent = {};
}

// The present mode that never waits for the display: immediate, then mailbox, else FIFO (every surface has it).
VkPresentModeKHR quickest_present_mode(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &count, nullptr);
    std::vector<VkPresentModeKHR> modes(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &count, modes.data());
    modes.resize(count);
    for (const VkPresentModeKHR wanted : {VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR}) {
        if (std::ranges::find(modes, wanted) != modes.end()) {
            return wanted;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

// Logged once a window.
void report(UiWindow* window, const char* step, VkResult result)
{
    if (!window->failed_logged) {
        get_log("vulkan: a UI window can't be drawn (%s: %d)", step, static_cast<int>(result));
    }
    window->failed_logged = true;
}

// The swapchain made again at the surface's size (the device idle first); false (nothing to draw into) when the window
// is minimised or a step fails.
bool make_swapchain(Backend& state, UiWindow* window)
{
    const Context& context = state.context;
    const VkDevice device = context.device();
    finish_work(state); // (the old swapchain's images may still be in use)
    release_swapchain(device, window);
    window->stale = false;
    VkSurfaceCapabilitiesKHR caps; // (filled by the call below)
    VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context.physical_device(), window->surface, &caps);
    if (result != VK_SUCCESS) {
        report(window, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR", result);
        return false;
    }
    const VkExtent2D extent = surface_extent_for(caps, window->window);
    if (extent.width == 0 || extent.height == 0) {
        window->stale = true; // (minimised: tried again when next drawn)
        return false;
    }
    const VkSurfaceFormatKHR format = surface_format_for(context.physical_device(), window->surface);
    uint32_t wanted_images = caps.minImageCount + 1;
    if (caps.maxImageCount != 0) {
        wanted_images = std::min(wanted_images, caps.maxImageCount);
    }
    const VkSwapchainCreateInfoKHR info{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = 0,
        .surface = window->surface,
        .minImageCount = wanted_images,
        .imageFormat = format.format,
        .imageColorSpace = format.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .preTransform = caps.currentTransform,
        .compositeAlpha = composite_alpha_for(caps.supportedCompositeAlpha),
        .presentMode = quickest_present_mode(context.physical_device(), window->surface),
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE,
    };
    result = vkCreateSwapchainKHR(device, &info, nullptr, &window->swapchain);
    if (result != VK_SUCCESS) {
        window->swapchain = VK_NULL_HANDLE;
        report(window, "vkCreateSwapchainKHR", result);
        return false;
    }
    window->format = format.format;
    window->extent = extent;
    uint32_t count = 0;
    vkGetSwapchainImagesKHR(device, window->swapchain, &count, nullptr);
    window->images.resize(count);
    vkGetSwapchainImagesKHR(device, window->swapchain, &count, window->images.data());
    window->images.resize(count);
    const VkSemaphoreCreateInfo semaphore{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = nullptr, .flags = 0};
    for (const VkImage image : window->images) {
        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format.format;
        view.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                 .baseMipLevel = 0,
                                 .levelCount = 1,
                                 .baseArrayLayer = 0,
                                 .layerCount = 1};
        VkImageView made_view = VK_NULL_HANDLE;
        result = vkCreateImageView(device, &view, nullptr, &made_view);
        if (result != VK_SUCCESS) {
            report(window, "vkCreateImageView", result);
            release_swapchain(device, window);
            return false;
        }
        window->views.push_back(made_view);
        VkSemaphore presented = VK_NULL_HANDLE;
        result = vkCreateSemaphore(device, &semaphore, nullptr, &presented);
        if (result != VK_SUCCESS) {
            report(window, "vkCreateSemaphore", result);
            release_swapchain(device, window);
            return false;
        }
        window->presented.push_back(presented);
    }
    return true;
}

} // namespace

UiWindow* create_ui_window(Backend& state, SDL_Window* window)
{
    if (!state.draws_made || window == nullptr) {
        return nullptr;
    }
    const Context& context = state.context;
    auto made = std::make_unique<UiWindow>();
    made->window = window;
    if (!SDL_Vulkan_CreateSurface(window, context.instance(), nullptr, &made->surface)) {
        get_log("vulkan: no surface for a UI window (%s)", SDL_GetError());
        return nullptr;
    }
    VkBool32 presents = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(context.physical_device(), context.queue_family(), made->surface, &presents);
    const VkSemaphoreCreateInfo semaphore{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = nullptr, .flags = 0};
    bool semaphores_made = true;
    for (VkSemaphore& acquired : made->acquired) {
        semaphores_made = semaphores_made && vkCreateSemaphore(context.device(), &semaphore, nullptr, &acquired) == VK_SUCCESS;
    }
    if (presents != VK_TRUE || !semaphores_made) {
        get_log("vulkan: a UI window can't be drawn (%s)",
                presents != VK_TRUE ? "the queue doesn't present to its surface" : "no semaphores");
        UiWindow* const failed = made.get();
        state.ui_windows.push_back(std::move(made));
        destroy_ui_window(state, failed);
        return nullptr;
    }
    state.ui_windows.push_back(std::move(made));
    return state.ui_windows.back().get();
}

void destroy_ui_window(Backend& state, UiWindow* window)
{
    const auto found = std::ranges::find_if(
        state.ui_windows, [window](const std::unique_ptr<UiWindow>& each) { return each.get() == window; });
    if (found == state.ui_windows.end()) {
        return;
    }
    const VkDevice device = state.context.device();
    // (its semaphores in the next submission's lists: that submission made, everything waited for)
    finish_work(state);
    vkDeviceWaitIdle(device);
    release_swapchain(device, window);
    for (VkSemaphore& acquired : window->acquired) {
        if (acquired != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, acquired, nullptr);
            acquired = VK_NULL_HANDLE;
        }
    }
    if (window->surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(state.context.instance(), window->surface, nullptr);
    }
    state.ui_windows.erase(found);
}

void destroy_ui_windows(Backend& state)
{
    while (!state.ui_windows.empty()) {
        destroy_ui_window(state, state.ui_windows.back().get());
    }
}

bool acquire_ui_window(Backend& state, UiWindow* window)
{
    if (!state.draws_made || window == nullptr || window->pending) {
        return false; // (drawn once a frame)
    }
    if ((window->stale || window->swapchain == VK_NULL_HANDLE) && !make_swapchain(state, window)) {
        return false;
    }
    const VkSemaphore acquired = core::at(window->acquired, state.frames.slot_index());
    uint32_t image = 0;
    const VkResult result =
        vkAcquireNextImageKHR(state.context.device(), window->swapchain, kNoTimeout, acquired, VK_NULL_HANDLE, &image);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        window->stale = true; // (made again when next drawn)
        return false;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        report(window, "vkAcquireNextImageKHR", result);
        window->stale = true;
        return false;
    }
    window->stale = result == VK_SUBOPTIMAL_KHR;
    window->image = image;
    window->pending = true;
    return true;
}

void present_ui_windows(Backend& state)
{
    for (const std::unique_ptr<UiWindow>& window : state.ui_windows) {
        if (!window->pending) {
            continue;
        }
        window->pending = false;
        const VkSemaphore presented = core::at(window->presented, window->image);
        const VkPresentInfoKHR present{
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext = nullptr,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &presented,
            .swapchainCount = 1,
            .pSwapchains = &window->swapchain,
            .pImageIndices = &window->image,
            .pResults = nullptr,
        };
        const VkResult result = vkQueuePresentKHR(state.context.queue(), &present);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            window->stale = true;
        } else if (result != VK_SUCCESS) {
            report(window.get(), "vkQueuePresentKHR", result);
            window->stale = true;
        }
    }
}

} // namespace render::vulkan
