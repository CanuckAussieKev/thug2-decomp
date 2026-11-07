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

// Whether a physical device can run the Vulkan Gpu's draws (vulkan_gpu.cpp), asked before the game's window is made
// (app/ falls back to Direct3D 9 otherwise): the loader, an instance, and the device the context would pick (`wanted`,
// or the best) with a graphics queue, the swapchain extension, Vulkan 1.3's dynamic rendering and push descriptors.
struct DeviceProbe {
    bool usable = false;
    std::string device;  // the device's name (when one was found)
    std::string problem; // why not, when not usable
};
DeviceProbe probe_device(int wanted);

// What the context chooses for its swapchain, for another window's (the UI's own windows: vulkan_ui_windows.cpp): the
// surface's format (8-bit BGRA without sRGB conversion when offered), its size (the surface's, or the window's
// drawable size within the surface's limits) and its composition (opaque when offered).
VkSurfaceFormatKHR surface_format_for(VkPhysicalDevice device, VkSurfaceKHR surface);
VkExtent2D surface_extent_for(const VkSurfaceCapabilitiesKHR& caps, SDL_Window* window);
VkCompositeAlphaFlagBitsKHR composite_alpha_for(VkCompositeAlphaFlagsKHR supported);

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
    // reported) when a step fails - what was made is released by destroy(). open() and make_swapchain() in turn.
    bool create(SDL_Window* window, const ContextOptions& options);
    // Everything but the swapchain: the loader, the instance, the surface, the physical and logical device and the frames
    // (the Gpu's context opens the device first and makes the swapchain at the device's creation).
    bool open(SDL_Window* window, const ContextOptions& options);
    // The swapchain at the surface's size (none while it is zero-sized); false on a failure.
    bool make_swapchain() { return create_swapchain(); }
    // The swapchain made again at the surface's current size (the device idle first).
    bool remake() { return remake_swapchain(); }
    // Releases everything, the device idle first. Harmless when nothing was made.
    void destroy();

    // The swapchain for a Gpu that records its own frames (vulkan_frames.h): the next image acquired (its semaphore
    // signalled when it may be written), and presented once `present_semaphore(image)` is signalled. The results are
    // Vulkan's (VK_ERROR_OUT_OF_DATE_KHR / VK_SUBOPTIMAL_KHR: make it again).
    VkResult acquire(VkSemaphore acquired, uint32_t* image);
    VkResult queue_present(uint32_t image);
    [[nodiscard]] VkImage swapchain_image(uint32_t image) const;
    [[nodiscard]] VkSemaphore present_semaphore(uint32_t image) const;
    // No swapchain now (minimised) or the window resized since it was made: make it again before the next frame.
    [[nodiscard]] bool swapchain_stale() const { return swapchain_ == VK_NULL_HANDLE || resized_; }
    // Whether the device runs the Gpu's draws: Vulkan 1.3's dynamic rendering and push descriptors were switched on.
    [[nodiscard]] bool draws_supported() const { return dynamic_rendering_ && push_descriptors_; }

    // Clears the next swapchain image to `colour` (linear RGBA) and presents it.
    FrameResult clear_frame(const std::array<float, 4>& colour);
    // The window's size changed: the swapchain is made again after the next present.
    void window_resized() { resized_ = true; }
    // Whether the surface's size is no longer the swapchain's (the window resized, minimised or restored without the
    // presents being told - not every driver reports it as out of date).
    [[nodiscard]] bool surface_changed() const;
    // A line to the sink (the validation layer's messages come through here).
    void report(const std::string& text) const;

    [[nodiscard]] VkInstance instance() const { return instance_; }
    [[nodiscard]] VkSurfaceKHR surface() const { return surface_; }
    [[nodiscard]] VkPhysicalDevice physical_device() const { return physical_device_; }
    [[nodiscard]] VkDevice device() const { return device_; }
    [[nodiscard]] uint32_t queue_family() const { return queue_family_; }
    [[nodiscard]] VkQueue queue() const { return queue_; }
    // The API version the instance was made for (the loader's, at most 1.3).
    [[nodiscard]] uint32_t api_version() const { return api_version_; }
    // The device features switched on where the device has them: anisotropic filtering, BC textures, wireframe.
    [[nodiscard]] const VkPhysicalDeviceFeatures& enabled_features() const { return enabled_features_; }
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
    uint32_t api_version_ = 0;
    VkPhysicalDeviceFeatures enabled_features_{};
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
    bool dynamic_rendering_ = false;
    bool push_descriptors_ = false;
};

} // namespace render::vulkan
