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

// NOT ORIGINAL - what a library adds to the instance and the device it works on (the upscaler's Streamline - DLSS:
// vulkan_upscale.h): asked while they are made, told when the device is made and before it is destroyed.
class ContextAdditions {
public:
    ContextAdditions() = default;
    ContextAdditions(const ContextAdditions&) = delete;
    ContextAdditions& operator=(const ContextAdditions&) = delete;
    ContextAdditions(ContextAdditions&&) = delete;
    ContextAdditions& operator=(ContextAdditions&&) = delete;
    virtual ~ContextAdditions() = default;
    // The instance's extensions (added to `names`; the context drops repeats).
    virtual void instance_extensions(std::vector<const char*>* names) { (void)names; }
    // (added by frame generation) The window's surface made again by the library once the device is made (Streamline's
    // DLSS-G must see it made, to know the window of the swapchain it takes): true when it made one - the context's
    // (SDL's) is replaced by it; false: SDL's stays, as always.
    virtual bool create_surface(VkInstance instance, SDL_Window* window, VkSurfaceKHR* surface)
    {
        (void)instance, (void)window, (void)surface;
        return false;
    }
    // The device's: extensions, Vulkan 1.2 / 1.3 features (the context keeps only those `physical` has) and how many
    // queues of the graphics family beyond the context's own one.
    struct DeviceWants {
        std::vector<const char*> extensions;
        VkPhysicalDeviceVulkan12Features features12{};
        VkPhysicalDeviceVulkan13Features features13{};
        uint32_t extra_queues = 0;
        // (added by frame generation) queues that present, apart from the context's own and the extra ones above: of the
        // graphics family where it has that many more, else of another family that presents and computes (an AMD
        // device's graphics family has a single queue)
        uint32_t present_queues = 0;
        // Vulkan 1.0's features (kept where the device has them)
        bool shader_int16 = false;
        // other feature structures to chain (the library's own, filled with what the device has; kept alive by it until
        // device_made)
        std::vector<VkBaseOutStructure*> chain;
    };
    virtual void device_extensions(VkPhysicalDevice physical, DeviceWants* wants) { (void)physical, (void)wants; }
    // The device made: its queue family (the context's queue its first), and how many extra queues of it were made.
    struct DeviceMade {
        VkInstance instance = nullptr;
        VkPhysicalDevice physical = nullptr;
        VkDevice device = nullptr;
        uint32_t family = 0;
        uint32_t extra_queues = 0;
        // (added by frame generation) the present queues made (DeviceWants::present_queues; 0: none could be): their
        // family and the index of the first in it
        uint32_t present_family = 0;
        uint32_t present_first = 0;
        uint32_t present_queues = 0;
    };
    virtual void device_made(const DeviceMade& made) { (void)made; }
    // Before the device is destroyed (idle).
    virtual void before_destroy() {}
};

// NOT ORIGINAL - (added by frame generation: AMD FSR 3's, vulkan_framegen.h) a library that makes the swapchain itself
// and replaces its calls: the context makes, lists, acquires, presents and destroys its swapchain through it while it
// is set (Context::set_swapchain_owner, before the swapchain is made). The swapchain made again (a resize) is the old one
// destroyed, then a new one made.
class SwapchainOwner {
public:
    SwapchainOwner() = default;
    SwapchainOwner(const SwapchainOwner&) = delete;
    SwapchainOwner& operator=(const SwapchainOwner&) = delete;
    SwapchainOwner(SwapchainOwner&&) = delete;
    SwapchainOwner& operator=(SwapchainOwner&&) = delete;
    virtual ~SwapchainOwner() = default;
    virtual VkResult create(const VkSwapchainCreateInfoKHR& info, VkSwapchainKHR* swapchain) = 0;
    virtual void destroy(VkSwapchainKHR swapchain) = 0;
    virtual VkResult images(VkSwapchainKHR swapchain, uint32_t* count, VkImage* images) = 0;
    virtual VkResult acquire(VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore acquired, uint32_t* image) = 0;
    virtual VkResult present(VkQueue queue, const VkPresentInfoKHR& present) = 0;
};

struct ContextOptions {
    bool validation = false; // use the Khronos validation layer if it is installed
    int device_index = -1;   // the physical device to use, in the instance's order; -1: the best (discrete first)
    MessageSink on_message = nullptr;
    // NOT ORIGINAL - ray tracing (engine/gfx/rt_scene.h): VK_KHR_acceleration_structure, VK_KHR_ray_query and what they
    // need (buffer device addresses, descriptor indexing) enabled when the device has them all
    bool ray_tracing = false;
    // NOT ORIGINAL - a library's additions to the instance and the device (null: none)
    ContextAdditions* additions = nullptr;
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
    // The ray-tracing extensions and features are enabled (asked for in ContextOptions, and the device has them).
    [[nodiscard]] bool ray_tracing_enabled() const { return ray_tracing_; }

    // Clears the next swapchain image to `colour` (linear RGBA) and presents it.
    FrameResult clear_frame(const std::array<float, 4>& colour);
    // The window's size changed: the swapchain is made again after the next present.
    void window_resized() { resized_ = true; }
    // NOT ORIGINAL - [Graphics] present_mode: the present mode the next swapchain is made with where the surface offers
    // it (else FIFO, which every surface has).
    void set_wanted_present_mode(VkPresentModeKHR mode) { wanted_present_mode_ = mode; }
    // NOT ORIGINAL - (frame generation) the swapchain made and used through `owner` from the next one made (null: Vulkan's
    // own calls). Set before the swapchain is made, cleared after it is destroyed.
    void set_swapchain_owner(SwapchainOwner* owner) { owner_ = owner; }
    [[nodiscard]] SwapchainOwner* swapchain_owner() const { return owner_; }
    [[nodiscard]] VkSwapchainKHR swapchain() const { return swapchain_; }
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
    VkPresentModeKHR wanted_present_mode_ = VK_PRESENT_MODE_FIFO_KHR;
    VkExtent2D extent_{};
    std::vector<VkImage> images_;
    std::vector<VkSemaphore> presented_; // per swapchain image: its submit signals it, its present waits on it
    uint32_t swapchains_made_ = 0;
    bool resized_ = false;
    bool dynamic_rendering_ = false;
    bool push_descriptors_ = false;
    bool ray_tracing_wanted_ = false;
    bool ray_tracing_ = false;
    ContextAdditions* additions_ = nullptr; // (NOT ORIGINAL - ContextOptions::additions)
    SwapchainOwner* owner_ = nullptr;       // (NOT ORIGINAL - set_swapchain_owner)
    // (NOT ORIGINAL - DeviceWants::present_queues: where they were made)
    uint32_t present_queues_wanted_ = 0;
    uint32_t present_family_ = 0;
    uint32_t present_first_ = 0;
    uint32_t present_queues_ = 0;
    uint32_t add_requested(std::vector<const char*>* extensions, VkPhysicalDeviceVulkan12Features* features12,
                           VkPhysicalDeviceVulkan13Features* features13, const VkPhysicalDeviceFeatures& offered);
};

} // namespace render::vulkan
