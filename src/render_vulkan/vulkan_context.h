#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <volk.h>

struct SDL_Window;

// NOT ORIGINAL - the Vulkan backend's context (vulkan_context.cpp; docs/design/vulkan.md "the frame"): the instance
// (with the Khronos validation layer when it is installed and asked for), the window's surface through SDL, the
// physical device and its one graphics-and-present queue, the logical device, and the swapchain with per-frame command
// buffers, fences and semaphores. The swapchain is made again when it goes out of date, the window is resized, or after
// a minimise. src/tests/vulkan_context_check.cpp makes one and clears frames with it.
namespace render::vulkan {

// Where the context's lines go: a step that failed, and the validation layer's messages.
using MessageSink = void (*)(const std::string& text);

struct ContextOptions {
    bool validation = false; // use the Khronos validation layer if it is installed
    int device_index = -1;   // the physical device to use, in the instance's order; -1: the best (discrete first)
    MessageSink on_message = nullptr;
};

// A queue family of a physical device, as the device lists it.
struct QueueFamilyInfo {
    uint32_t index = 0;
    VkQueueFlags flags = 0;
    uint32_t count = 0;
    bool presents = false; // it can present to the surface
};

// The queue families of `device`, with whether each presents to `surface`.
std::vector<QueueFamilyInfo> queue_families(VkPhysicalDevice device, VkSurfaceKHR surface);

// Whether the loader lists the instance layer `name`.
bool instance_layer_available(const char* name);

// The first of `candidates` that `device` can use as a depth / stencil attachment with optimal tiling;
// VK_FORMAT_UNDEFINED when none.
VkFormat first_depth_format(VkPhysicalDevice device, std::span<const VkFormat> candidates);

// What clear_frame did.
enum class FrameResult : uint8_t {
    kPresented, // the frame was cleared and presented
    kSkipped,   // the window is minimised (a zero-sized surface): nothing drawn
    kRemade,    // the swapchain was out of date and was made again: nothing presented this time
    kFailed,    // a Vulkan call failed (reported through the sink)
};

class Context {
public:
    // The frames recorded ahead of the GPU.
    static constexpr uint32_t kFramesInFlight = 2;

    Context() = default;
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    ~Context();

    // Opens the loader and makes everything for `window` (made with SDL_WINDOW_VULKAN); false (and the failed step
    // reported) when a step fails - what was made is released by destroy().
    bool create(SDL_Window* window, const ContextOptions& options);
    // Releases everything, the device idle first. Harmless when nothing was made.
    void destroy();

    // Clears the next swapchain image to `colour` (linear RGBA) and presents it.
    FrameResult clear_frame(const std::array<float, 4>& colour);
    // The window's size changed: the swapchain is made again after the next present.
    void window_resized() { resized_ = true; }
    // A line to the sink (the validation layer's messages come through here).
    void report(const std::string& text) const;

    [[nodiscard]] VkInstance instance() const { return instance_; }
    [[nodiscard]] VkSurfaceKHR surface() const { return surface_; }
    [[nodiscard]] VkPhysicalDevice physical_device() const { return physical_device_; }
    [[nodiscard]] VkDevice device() const { return device_; }
    [[nodiscard]] uint32_t queue_family() const { return queue_family_; }
    [[nodiscard]] uint32_t instance_version() const { return instance_version_; }
    [[nodiscard]] bool validation_on() const { return debug_messenger_ != VK_NULL_HANDLE; }
    [[nodiscard]] VkSurfaceFormatKHR surface_format() const { return surface_format_; }
    [[nodiscard]] VkPresentModeKHR present_mode() const { return present_mode_; }
    [[nodiscard]] VkExtent2D extent() const { return extent_; }
    [[nodiscard]] uint32_t image_count() const { return static_cast<uint32_t>(images_.size()); }
    [[nodiscard]] uint32_t swapchains_made() const { return swapchains_made_; }

private:
    // Per frame in flight: its commands, the fence its submit signals, the semaphore its acquire signals.
    struct Frame {
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer commands = nullptr;
        VkFence done = VK_NULL_HANDLE;
        VkSemaphore acquired = VK_NULL_HANDLE;
    };

    bool create_instance(bool validation);
    bool create_surface();
    bool pick_physical_device(int wanted);
    bool create_device();
    bool create_frames();
    // (Re)makes the swapchain at the surface's current size; false on a failure. A zero size makes none (minimised).
    bool create_swapchain();
    void destroy_swapchain(VkSwapchainKHR swapchain);
    bool remake_swapchain();
    // Reports `step` failing with `result` through the sink; false.
    bool fail(const char* step, VkResult result) const;

    MessageSink on_message_ = nullptr;
    SDL_Window* window_ = nullptr;
    uint32_t instance_version_ = 0;
    VkInstance instance_ = nullptr;
    VkDebugUtilsMessengerEXT debug_messenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = nullptr;
    uint32_t queue_family_ = 0;
    VkDevice device_ = nullptr;
    VkQueue queue_ = nullptr;
    std::array<Frame, kFramesInFlight> frames_{};
    uint32_t frame_index_ = 0;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkSurfaceFormatKHR surface_format_{};
    VkPresentModeKHR present_mode_ = VK_PRESENT_MODE_FIFO_KHR;
    VkExtent2D extent_{};
    std::vector<VkImage> images_;
    std::vector<VkSemaphore> presented_; // per swapchain image: its submit signals it, its present waits on it
    uint32_t swapchains_made_ = 0;
    bool resized_ = false;
};

} // namespace render::vulkan
