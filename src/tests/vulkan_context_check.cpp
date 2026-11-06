// NOT ORIGINAL - thug2_vulkan_context_check.exe: tries the Vulkan backend's context (gpu_vulkan/vulkan_context.h)
// on this machine. It opens an SDL window with a Vulkan surface, makes the instance (the validation layer on when it is
// installed), picks a device, makes the device and the swapchain, clears and presents frames (resizing the window half
// way, then minimising and restoring it, so the swapchain is made again), and shuts down. It prints what it found: the process's width, the loader's
// version, every device (name, type, API and driver versions, queue families, memory heaps, the depth and texture
// formats the renderer needs) and the swapchain.
//   thug2_vulkan_context_check [--frames N] [--display N] [--device N] [--no-validation]
// --display: the monitor the window opens on (1 = the first; default: the second when there are two, else the first).
// --device: the physical device to use, counted from 0 in the order printed (default: the context's choice).
// Exit code: 0 when frames were presented with no step failing and everything was released, 1 when a step failed,
// 2 when this machine has no Vulkan for this process (no loader, or no device).
#include <SDL2/SDL.h>
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_timer.h>
#include <SDL2/SDL_video.h>
#include <SDL2/SDL_vulkan.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <volk.h>

#include "tests/no_error_dialogs.h"
#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "base/core/parse_number.h"
#include "gpu_vulkan/vulkan_context.h"
#include "gpu_vulkan/vulkan_loader.h"

namespace {

constexpr int kDefaultFrames = 180;
constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 720;
constexpr int kResizedWidth = 960;
constexpr int kResizedHeight = 540;
constexpr int kBitsPerByte = 8;
constexpr uint32_t kVendorNvidia = 0x10DE;
constexpr uint32_t kVendorIntel = 0x8086;
constexpr uint32_t kBytesPerMegabyte = 1024U * 1024U;
// The clear colours the frames cycle through (linear RGBA).
constexpr std::array<std::array<float, 4>, 3> kColours{{
    {0.10F, 0.20F, 0.45F, 1.0F},
    {0.45F, 0.15F, 0.10F, 1.0F},
    {0.10F, 0.40F, 0.15F, 1.0F},
}};
constexpr int kFramesPerColour = 30;
constexpr int kMinimisedFrames = 20;
constexpr uint32_t kSkippedFrameMs = 16;

struct Options {
    int frames = kDefaultFrames;
    int display = 0; // 0: the default (above)
    int device = -1; // -1: the context's choice
    bool validation = true;
};

Options read_options(std::span<char*> arguments)
{
    Options options;
    for (size_t i = 1; i < arguments.size(); ++i) {
        const std::string_view argument = core::at(arguments, i);
        const bool has_value = i + 1 < arguments.size();
        if (argument == "--no-validation") {
            options.validation = false;
        } else if (argument == "--frames" && has_value) {
            options.frames = text::to_int(core::at(arguments, ++i));
        } else if (argument == "--device" && has_value) {
            options.device = text::to_int(core::at(arguments, ++i));
        } else if (argument == "--display" && has_value) {
            options.display = text::to_int(core::at(arguments, ++i));
        }
    }
    return options;
}

void print_line(const std::string& text)
{
    text::print("  %s\n", text.c_str());
}

std::string version_text(uint32_t version)
{
    return text::format("%u.%u.%u", VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version),
                        VK_API_VERSION_PATCH(version));
}

// The driver's version as its vendor numbers it (NVIDIA's and Intel's Windows drivers pack it their own way).
std::string driver_version_text(const VkPhysicalDeviceProperties& properties)
{
    constexpr uint32_t kNvidiaMajorShift = 22;
    constexpr uint32_t kNvidiaMinorShift = 14;
    constexpr uint32_t kNvidiaMinorMask = 0xFF;
    constexpr uint32_t kIntelMajorShift = 14;
    constexpr uint32_t kIntelMinorMask = 0x3FFF;
    const uint32_t version = properties.driverVersion;
    if (properties.vendorID == kVendorNvidia) {
        return text::format("%u.%u", version >> kNvidiaMajorShift, (version >> kNvidiaMinorShift) & kNvidiaMinorMask);
    }
    if (properties.vendorID == kVendorIntel) {
        return text::format("%u.%u", version >> kIntelMajorShift, version & kIntelMinorMask);
    }
    return version_text(version);
}

const char* type_name(VkPhysicalDeviceType type)
{
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return "discrete GPU";
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return "integrated GPU";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        return "virtual GPU";
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        return "CPU";
    case VK_PHYSICAL_DEVICE_TYPE_OTHER:
    default:
        return "other";
    }
}

std::string queue_flags_text(VkQueueFlags flags)
{
    std::string text;
    for (const auto& [bit, name] : {std::pair{VK_QUEUE_GRAPHICS_BIT, "graphics"}, std::pair{VK_QUEUE_COMPUTE_BIT, "compute"},
                                    std::pair{VK_QUEUE_TRANSFER_BIT, "transfer"},
                                    std::pair{VK_QUEUE_SPARSE_BINDING_BIT, "sparse"}}) {
        if ((flags & static_cast<VkQueueFlags>(bit)) != 0) {
            text += text.empty() ? name : std::string(" ") + name;
        }
    }
    return text;
}

struct NamedFormat {
    VkFormat format;
    const char* name;
};

// The depth formats the renderer may use (Direct3D 9's D24S8 / D24X8 / D16 and the stencil fallback), in the order
// the backend would take them.
constexpr std::array<NamedFormat, 6> kDepthFormats{{
    {.format = VK_FORMAT_D24_UNORM_S8_UINT, .name = "D24_UNORM_S8_UINT"},
    {.format = VK_FORMAT_D32_SFLOAT_S8_UINT, .name = "D32_SFLOAT_S8_UINT"},
    {.format = VK_FORMAT_X8_D24_UNORM_PACK32, .name = "X8_D24_UNORM_PACK32"},
    {.format = VK_FORMAT_D32_SFLOAT, .name = "D32_SFLOAT"},
    {.format = VK_FORMAT_D16_UNORM, .name = "D16_UNORM"},
    {.format = VK_FORMAT_D16_UNORM_S8_UINT, .name = "D16_UNORM_S8_UINT"},
}};

// The texture formats the game's textures map to (A8R8G8B8 / X8R8G8B8, DXT1 / DXT5, the 16-bit ones).
constexpr std::array<NamedFormat, 8> kTextureFormats{{
    {.format = VK_FORMAT_B8G8R8A8_UNORM, .name = "B8G8R8A8_UNORM"},
    {.format = VK_FORMAT_R8G8B8A8_UNORM, .name = "R8G8B8A8_UNORM"},
    {.format = VK_FORMAT_BC1_RGBA_UNORM_BLOCK, .name = "BC1_RGBA_UNORM_BLOCK"},
    {.format = VK_FORMAT_BC3_UNORM_BLOCK, .name = "BC3_UNORM_BLOCK"},
    {.format = VK_FORMAT_A1R5G5B5_UNORM_PACK16, .name = "A1R5G5B5_UNORM_PACK16"},
    {.format = VK_FORMAT_R5G6B5_UNORM_PACK16, .name = "R5G6B5_UNORM_PACK16"},
    {.format = VK_FORMAT_B5G6R5_UNORM_PACK16, .name = "B5G6R5_UNORM_PACK16"},
    {.format = VK_FORMAT_B4G4R4A4_UNORM_PACK16, .name = "B4G4R4A4_UNORM_PACK16"},
}};

void print_formats(VkPhysicalDevice device)
{
    for (const NamedFormat& depth : kDepthFormats) {
        const VkFormat one = depth.format;
        const bool usable = render::vulkan::first_depth_format(device, std::span(&one, 1)) != VK_FORMAT_UNDEFINED;
        print_line(text::format("depth %-22s %s", depth.name, usable ? "yes" : "NO"));
    }
    for (const NamedFormat& texture : kTextureFormats) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(device, texture.format, &properties);
        const bool sampled = (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;
        const bool filtered =
            (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
        const bool target = (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0;
        print_line(text::format("texture %-22s sampled %s, linear filter %s, render target %s", texture.name,
                                sampled ? "yes" : "NO", filtered ? "yes" : "NO", target ? "yes" : "NO"));
    }
}

// The driver's own name and description (Vulkan 1.2's driver properties), when the device has them.
void print_driver_properties(VkPhysicalDevice device, uint32_t api_version, uint32_t instance_version)
{
    if (api_version < VK_API_VERSION_1_2 || instance_version < VK_API_VERSION_1_1) {
        return;
    }
    VkPhysicalDeviceDriverProperties driver; // (filled by the call below)
    driver.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
    driver.pNext = nullptr;
    VkPhysicalDeviceProperties2 properties;
    properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties.pNext = &driver;
    vkGetPhysicalDeviceProperties2(device, &properties);
    print_line(text::format("driver: %s (%s), conformance %u.%u.%u.%u", std::data(driver.driverName),
                            std::data(driver.driverInfo), unsigned{driver.conformanceVersion.major},
                            unsigned{driver.conformanceVersion.minor}, unsigned{driver.conformanceVersion.subminor},
                            unsigned{driver.conformanceVersion.patch}));
}

void print_device(const render::vulkan::Context& context, VkPhysicalDevice device)
{
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device, &properties);
    text::print("device: %s%s\n", std::data(properties.deviceName),
                device == context.physical_device() ? "  (chosen)" : "");
    print_line(text::format("type %s, vendor 0x%04X, device 0x%04X", type_name(properties.deviceType),
                            properties.vendorID, properties.deviceID));
    print_line(text::format("API version %s, driver version %s (0x%08X)", version_text(properties.apiVersion).c_str(),
                            driver_version_text(properties).c_str(),
                            properties.driverVersion));
    print_driver_properties(device, properties.apiVersion, context.instance_version());
    for (const render::vulkan::QueueFamilyInfo& family : render::vulkan::queue_families(device, context.surface())) {
        print_line(text::format("queue family %u: %s, %u queues, presents %s", family.index,
                                queue_flags_text(family.flags).c_str(), family.count, family.presents ? "yes" : "no"));
    }
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(device, &memory);
    for (const VkMemoryHeap& heap : std::span(memory.memoryHeaps).first(memory.memoryHeapCount)) {
        const bool local = (heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;
        print_line(text::format("memory heap: %u MB%s", static_cast<uint32_t>(heap.size / kBytesPerMegabyte),
                                local ? ", device local" : ""));
    }
    print_formats(device);
}

void print_devices(const render::vulkan::Context& context)
{
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(context.instance(), &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(context.instance(), &count, devices.data());
    devices.resize(count);
    for (VkPhysicalDevice device : devices) {
        print_device(context, device);
    }
}

void print_swapchain(const render::vulkan::Context& context)
{
    const VkSurfaceFormatKHR format = context.surface_format();
    text::print("swapchain: %ux%u, %u images, format %d, colour space %d, present mode %d (FIFO is 2)\n",
                context.extent().width, context.extent().height, context.image_count(), static_cast<int>(format.format),
                static_cast<int>(format.colorSpace), static_cast<int>(context.present_mode()));
}

// Pumps the window's events: false when the window was closed. A resize is passed on to the context.
bool pump_events(render::vulkan::Context& context)
{
    SDL_Event event{};
    while (SDL_PollEvent(&event) != 0) {
        if (event.type == SDL_QUIT) {
            return false;
        }
        if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            context.window_resized();
        }
    }
    return true;
}

struct FrameCounts {
    int presented = 0;
    int skipped = 0;
    int remade = 0;
    bool failed = false;
};

FrameCounts clear_frames(render::vulkan::Context& context, SDL_Window* window, int frames)
{
    FrameCounts counts;
    for (int frame = 0; frame < frames && !counts.failed; ++frame) {
        // half way: resized (the swapchain is made again); three quarters: minimised for a while, then restored
        if (frame == frames / 2) {
            SDL_SetWindowSize(window, kResizedWidth, kResizedHeight);
        } else if (frame == (frames * 3) / 4) {
            SDL_MinimizeWindow(window);
        } else if (frame == ((frames * 3) / 4) + kMinimisedFrames) {
            SDL_RestoreWindow(window);
        }
        if (!pump_events(context)) {
            break;
        }
        const auto& colour = core::at(kColours, static_cast<size_t>(frame / kFramesPerColour) % kColours.size());
        switch (context.clear_frame(colour)) {
        case render::vulkan::FrameResult::kPresented:
            ++counts.presented;
            break;
        case render::vulkan::FrameResult::kSkipped:
            ++counts.skipped;
            SDL_Delay(kSkippedFrameMs); // (no present to wait on)
            break;
        case render::vulkan::FrameResult::kRemade:
            ++counts.remade;
            break;
        case render::vulkan::FrameResult::kFailed:
            counts.failed = true;
            break;
        }
    }
    return counts;
}

int window_position(int display)
{
    const int displays = SDL_GetNumVideoDisplays();
    int chosen = display;
    if (chosen <= 0) {
        chosen = displays > 1 ? 2 : 1;
    }
    if (chosen > displays) {
        return static_cast<int>(SDL_WINDOWPOS_CENTERED);
    }
    return static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(chosen - 1));
}

// Makes the context and clears the frames; the exit code.
int run(const Options& options, SDL_Window* window)
{
    render::vulkan::Context context;
    const render::vulkan::ContextOptions context_options{
        .validation = options.validation, .device_index = options.device, .on_message = print_line};
    if (!context.create(window, context_options)) {
        const bool no_vulkan = render::vulkan::loader_version() == 0 || context.physical_device() == nullptr;
        text::print("the context could not be made%s\n", no_vulkan ? ": no Vulkan for this process" : "");
        if (context.instance() != nullptr) {
            print_devices(context); // (what the instance found)
        }
        context.destroy();
        return no_vulkan ? 2 : 1;
    }
    text::print("validation layer: %s\n", context.validation_on() ? "on" : "off (not installed, or not asked for)");
    print_devices(context);
    print_swapchain(context);
    const FrameCounts counts = clear_frames(context, window, options.frames);
    text::print("frames: %d presented, %d skipped (minimised), %d with the swapchain remade; %u swapchains made\n",
                counts.presented, counts.skipped, counts.remade, context.swapchains_made());
    print_swapchain(context);
    context.destroy();
    text::print("%s\n", counts.failed ? "a frame failed" : "shut down cleanly");
    return counts.failed ? 1 : 0;
}

} // namespace

int main(int argc, char** argv)
{
    tests::no_error_dialogs();
    const Options options = read_options(std::span(argv, static_cast<size_t>(argc)));
    text::print("thug2_vulkan_context_check: a %d-bit process\n", static_cast<int>(sizeof(void*)) * kBitsPerByte);
    if (!render::vulkan::open_loader()) {
        text::print("no Vulkan loader (vulkan-1.dll) for this process\n");
        return 2;
    }
    text::print("loader: Vulkan %s\n", version_text(render::vulkan::loader_version()).c_str());
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        text::print("SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    const int position = window_position(options.display);
    SDL_Window* window = SDL_CreateWindow("thug2 Vulkan context check", position, position, kWindowWidth,
                                          kWindowHeight, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (window == nullptr) {
        text::print("SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 2;
    }
    text::print("window: on display %d of %d\n", SDL_GetWindowDisplayIndex(window) + 1, SDL_GetNumVideoDisplays());
    const int result = run(options, window);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
