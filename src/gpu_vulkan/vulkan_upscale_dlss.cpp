// NOT ORIGINAL - the DLSS provider of the upscaler (vulkan_upscale.h; docs/design/vulkan.md "Upscaling"): NVIDIA
// Streamline 2.14 (github.com/NVIDIA-RTX/Streamline, MIT; cmake/upscalers.cmake) in its manual-hooking mode - nothing
// of Vulkan goes through it but what the upscaler hands it: sl.interposer.dll is loaded from beside the exe (its
// signature checked), initialised before the instance is made (ContextAdditions: its instance and device extensions,
// features and queues added), told the device (slSetVulkanInfo), and given each frame's constants, tagged images and
// one evaluation. Super Resolution and DLAA are sl.dlss (nvngx_dlss.dll); Ray Reconstruction is sl.dlss_d
// (nvngx_dlssd.dll) - NVIDIA's RTX SDKs licence for those two (THIRD_PARTY_NOTICES.md).
#include "gpu_vulkan/vulkan_upscale.h"

#include <memory>
#include <string>

#ifdef THUG2_DLSS
#include <windows.h>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_loadso.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_video.h>
#include <vulkan/vulkan_win32.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>
#include <volk.h>
#include <vector>

#include "base/core/checked_index.h"
#include "gpu_vulkan/ff_uniforms.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_context.h"
#include "gpu_vulkan/vulkan_motion.h"
#include "platform/run_log.h"

#include "gpu_vulkan/vulkan_framegen.h"
#include "gpu_vulkan/vulkan_streamline.h"

// (Streamline's headers: C++, MIT - the rest through vulkan_streamline.h; its Windows signature check)
#include <sl_security.h>
#endif

namespace render::vulkan {

#ifdef THUG2_DLSS
namespace {

constexpr uint32_t kVendorNvidia = 0x10DE;
// The project's id for Streamline (a GUID of its own: no NVIDIA application id - "custom engine" integrations give
// one), and the engine's version.
constexpr const char* kProjectId = "6a0d2b41-6c1e-4f3e-9a52-7d1c8e3b5f20";
constexpr const char* kEngineVersion = "1.0";
constexpr uint32_t kViewport = 0;
// DLSS's jitter convention (UpscaleJitterTerms; measured by thug2_vulkan_upscale_check)
constexpr UpscaleJitterTerms kDlssTerms{.sign = {-1.0F, -1.0F}, .align = {-1.0F, -1.0F}};
constexpr sl::ResourceLifecycle kKept = sl::ResourceLifecycle::eValidUntilPresent;

template <typename Function> Function* function_of(SDL_SharedObject* library, const char* name)
{
    return same_address<Function*>(SDL_LoadFunction(library, name));
}

// The features slInit was asked to load (initialise).
std::vector<sl::Feature>& loaded_features()
{
    static std::vector<sl::Feature> features;
    return features;
}

// Whether the upscaler may be DLSS (dlss, auto): DLSS's features loaded.
bool upscaler_wants_streamline()
{
    const Upscaler upscaler = upscale_settings_locked().upscaler;
    return upscaler == Upscaler::kDlss || upscaler == Upscaler::kAuto;
}

void on_log(sl::LogType type, const char* message)
{
    if (type != sl::LogType::eInfo && message != nullptr) {
        std::string text(message);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
            text.pop_back();
        }
        get_log("vulkan: upscaler: Streamline %s: %s", type == sl::LogType::eError ? "error" : "warning", text.c_str());
    }
}

std::wstring wide(const std::string& text) { return {text.begin(), text.end()}; }

// sl.interposer.dll loaded (signed by NVIDIA) and initialised for Vulkan with DLSS and DLSS-RR; false with the reason.
bool initialise(std::string* why)
{
    SlState& sl_state = streamline();
    if (sl_state.initialised) {
        return true;
    }
    const char* base = SDL_GetBasePath();
    const std::string folder = base != nullptr ? base : "";
    const std::string path = folder + "sl.interposer.dll";
    const std::wstring wide_path = wide(path);
    if (!sl::security::verifyEmbeddedSignature(wide_path.c_str())) {
        *why = "sl.interposer.dll isn't beside the exe, or isn't signed by NVIDIA";
        return false;
    }
    SlFunctions& calls = sl_state.functions;
    calls.library = SDL_LoadObject(path.c_str());
    if (calls.library == nullptr) {
        *why = "sl.interposer.dll can't be loaded";
        return false;
    }
    calls.init = function_of<PFun_slInit>(calls.library, "slInit");
    calls.shutdown = function_of<PFun_slShutdown>(calls.library, "slShutdown");
    calls.supported = function_of<PFun_slIsFeatureSupported>(calls.library, "slIsFeatureSupported");
    calls.requirements = function_of<PFun_slGetFeatureRequirements>(calls.library, "slGetFeatureRequirements");
    calls.version = function_of<PFun_slGetFeatureVersion>(calls.library, "slGetFeatureVersion");
    calls.feature_function = function_of<PFun_slGetFeatureFunction>(calls.library, "slGetFeatureFunction");
    calls.frame_token = function_of<PFun_slGetNewFrameToken>(calls.library, "slGetNewFrameToken");
    calls.constants = function_of<PFun_slSetConstants>(calls.library, "slSetConstants");
    calls.tag = function_of<PFun_slSetTagForFrame>(calls.library, "slSetTagForFrame");
    calls.evaluate = function_of<PFun_slEvaluateFeature>(calls.library, "slEvaluateFeature");
    calls.free_resources = function_of<PFun_slFreeResources>(calls.library, "slFreeResources");
    calls.vulkan_info = function_of<PFun_slSetVulkanInfo>(calls.library, "slSetVulkanInfo");
    calls.set_loaded = function_of<PFun_slSetFeatureLoaded>(calls.library, "slSetFeatureLoaded");
    if (calls.init == nullptr || calls.shutdown == nullptr || calls.requirements == nullptr || calls.frame_token == nullptr ||
        calls.constants == nullptr || calls.tag == nullptr || calls.evaluate == nullptr || calls.vulkan_info == nullptr ||
        calls.feature_function == nullptr) {
        *why = "sl.interposer.dll lacks Streamline's functions";
        return false;
    }
    static const std::wstring kPluginFolder = wide(folder);
    static std::array<const wchar_t*, 1> plugin_paths{kPluginFolder.c_str()}; // (Streamline takes them as non-const)
    // the features: DLSS's two when the upscaler may be DLSS; (added by frame generation) DLSS-G, Reflex and PCL when
    // frame generation may be DLSS-G or Reflex is asked for (nothing of them otherwise)
    std::vector<sl::Feature>& features = loaded_features();
    features.clear();
    if (upscaler_wants_streamline()) {
        features.insert(features.end(), {sl::kFeatureDLSS, sl::kFeatureDLSS_RR});
    }
    sl_state.generation_loaded = frame_generation_wants_dlss();
    sl_state.latency_loaded = sl_state.generation_loaded || frame_generation_settings_locked().reflex != Reflex::kOff;
    if (sl_state.generation_loaded) {
        features.push_back(sl::kFeatureDLSS_G);
    }
    if (sl_state.latency_loaded) {
        features.insert(features.end(), {sl::kFeatureReflex, sl::kFeaturePCL});
    }
    sl::Preferences preferences{};
    preferences.showConsole = false;
    preferences.logLevel = sl::LogLevel::eDefault;
    preferences.pathsToPlugins = plugin_paths.data();
    preferences.numPathsToPlugins = 1;
    preferences.pathToLogsAndData = nullptr;
    preferences.logMessageCallback = on_log;
    // (manual hooking: Vulkan is the game's own; no updates fetched over the network)
    preferences.flags = sl::PreferenceFlags::eDisableCLStateTracking | sl::PreferenceFlags::eUseManualHooking |
                        sl::PreferenceFlags::eDisableDebugText | sl::PreferenceFlags::eUseFrameBasedResourceTagging;
    preferences.featuresToLoad = features.data();
    preferences.numFeaturesToLoad = static_cast<uint32_t>(features.size());
    preferences.engine = sl::EngineType::eCustom;
    preferences.engineVersion = kEngineVersion;
    preferences.projectId = kProjectId;
    preferences.renderAPI = sl::RenderAPI::eVulkan;
    const sl::Result result = calls.init(preferences, sl::kSDKVersion);
    if (result != sl::Result::eOk) {
        *why = "slInit returned " + std::to_string(static_cast<int>(result));
        return false;
    }
    sl_state.initialised = true;
    return true;
}

// The instance and device additions Streamline's features ask for.
class StreamlineAdditions final : public ContextAdditions {
public:
    void instance_extensions(std::vector<const char*>* names) override
    {
        SlState& sl_state = streamline();
        std::string why;
        if (!initialise(&why)) {
            sl_state.problem = why;
            get_log("vulkan: upscaler: Streamline isn't there (%s) - no DLSS", why.c_str());
            return;
        }
        for (const sl::Feature feature : loaded_features()) {
            sl::FeatureRequirements needs{};
            if (sl_state.functions.requirements(feature, needs) != sl::Result::eOk) {
                continue;
            }
            for (const char* name : std::span<const char*>(needs.vkInstanceExtensions, needs.vkNumInstanceExtensions)) {
                names->push_back(name);
            }
        }
    }

    // (frame generation) DLSS-G finds the window of the swapchain it takes from the surface Streamline saw made: with
    // DLSS-G loaded, the window's surface is made through Streamline's vkCreateWin32SurfaceKHR (one of its mandatory
    // hooks) rather than SDL's.
    bool create_surface(VkInstance instance, SDL_Window* window, VkSurfaceKHR* surface) override
    {
        const SlState& sl_state = streamline();
        if (!sl_state.initialised || !sl_state.generation_loaded) {
            return false;
        }
        auto* const proxy = function_of<std::remove_pointer_t<PFN_vkGetInstanceProcAddr>>(sl_state.functions.library,
                                                                                          "vkGetInstanceProcAddr");
        auto* const hwnd = static_cast<HWND>(
            SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        if (proxy == nullptr || hwnd == nullptr) {
            return false;
        }
        const auto create = same_address<PFN_vkCreateWin32SurfaceKHR>(proxy(instance, "vkCreateWin32SurfaceKHR"));
        if (create == nullptr) {
            return false;
        }
        const VkWin32SurfaceCreateInfoKHR info{.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
                                               .pNext = nullptr,
                                               .flags = 0,
                                               .hinstance = GetModuleHandleW(nullptr),
                                               .hwnd = hwnd};
        return create(instance, &info, nullptr, surface) == VK_SUCCESS;
    }

    void device_extensions(VkPhysicalDevice physical, DeviceWants* wants) override
    {
        SlState& sl_state = streamline();
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical, &properties);
        sl_state.nvidia = properties.vendorID == kVendorNvidia;
        if (!sl_state.initialised || !sl_state.nvidia) {
            return;
        }
        graphics_queues_ = 0;
        compute_queues_ = 0;
        uint32_t added_graphics = 0;
        uint32_t added_compute = 0;
        for (const sl::Feature feature : loaded_features()) {
            sl::FeatureRequirements needs{};
            if (sl_state.functions.requirements(feature, needs) != sl::Result::eOk) {
                continue;
            }
            // (buffer device addresses are Vulkan 1.2's - asked for as its feature below: the extensions that once
            // gave them would clash with it)
            for (const char* name : std::span<const char*>(needs.vkDeviceExtensions, needs.vkNumDeviceExtensions)) {
                const std::string_view extension(name);
                if (extension != "VK_EXT_buffer_device_address" && extension != "VK_KHR_buffer_device_address") {
                    wants->extensions.push_back(name);
                }
            }
            const VkPhysicalDeviceVulkan12Features features12 =
                sl::getVkPhysicalDeviceVulkan12Features(needs.vkNumFeatures12, needs.vkFeatures12);
            const VkPhysicalDeviceVulkan13Features features13 =
                sl::getVkPhysicalDeviceVulkan13Features(needs.vkNumFeatures13, needs.vkFeatures13);
            or_flags(features12, &wants->features12);
            or_flags(features13, &wants->features13);
            // (DLSS's two share their queues, as they always did here; frame generation's features each add theirs,
            // as Streamline's own device creation counts them)
            if (feature == sl::kFeatureDLSS || feature == sl::kFeatureDLSS_RR) {
                graphics_queues_ = std::max(graphics_queues_, needs.vkNumGraphicsQueuesRequired);
                compute_queues_ = std::max(compute_queues_, needs.vkNumComputeQueuesRequired);
            } else {
                added_graphics += needs.vkNumGraphicsQueuesRequired;
                added_compute += needs.vkNumComputeQueuesRequired;
            }
        }
        graphics_queues_ += added_graphics;
        compute_queues_ += added_compute;
        wants->extra_queues = graphics_queues_ + compute_queues_;
        // (Streamline keeps data on Vulkan objects: Vulkan 1.3's private data)
        wants->features13.privateData = VK_TRUE;
    }

    void device_made(const DeviceMade& made) override
    {
        SlState& sl_state = streamline();
        if (!sl_state.initialised || !sl_state.nvidia) {
            return;
        }
        if (made.extra_queues < graphics_queues_ + compute_queues_) {
            sl_state.problem = "the device has too few queues for Streamline";
            return;
        }
        sl::VulkanInfo info{};
        info.device = made.device;
        info.instance = made.instance;
        info.physicalDevice = made.physical;
        info.graphicsQueueFamily = made.family;
        info.graphicsQueueIndex = 1; // (after the context's own queue)
        info.computeQueueFamily = made.family;
        info.computeQueueIndex = 1 + graphics_queues_;
        if (sl_state.functions.vulkan_info(info) != sl::Result::eOk) {
            sl_state.problem = "slSetVulkanInfo failed";
            return;
        }
        sl_state.device_set = true;
        route_through_streamline(made.device);
        sl::AdapterInfo adapter{};
        adapter.vkPhysicalDevice = made.physical;
        const SlFunctions& calls = sl_state.functions;
        sl_state.dlss = calls.supported != nullptr && calls.supported(sl::kFeatureDLSS, adapter) == sl::Result::eOk;
        sl_state.ray_reconstruction =
            calls.supported != nullptr && calls.supported(sl::kFeatureDLSS_RR, adapter) == sl::Result::eOk;
        void* options = nullptr;
        if (sl_state.dlss && sl_state.functions.feature_function(sl::kFeatureDLSS, "slDLSSSetOptions", options) ==
                                 sl::Result::eOk) {
            sl_state.functions.dlss_options = same_address<PFun_slDLSSSetOptions*>(options);
        }
        options = nullptr;
        if (sl_state.ray_reconstruction &&
            sl_state.functions.feature_function(sl::kFeatureDLSS_RR, "slDLSSDSetOptions", options) == sl::Result::eOk) {
            sl_state.functions.dlssd_options = same_address<PFun_slDLSSDSetOptions*>(options);
        }
        sl::FeatureVersion version{};
        if (sl_state.functions.version != nullptr &&
            sl_state.functions.version(upscaler_wants_streamline() ? sl::kFeatureDLSS : sl::kFeatureDLSS_G, version) ==
                sl::Result::eOk) {
            sl_state.version = "Streamline " + version.versionSL.toStr() + ", NGX " + version.versionNGX.toStr();
        }
        if (upscaler_wants_streamline()) {
            get_log("vulkan: upscaler: Streamline ready on the device (DLSS %s, Ray Reconstruction %s%s%s)",
                    sl_state.dlss ? "supported" : "not supported",
                    sl_state.ray_reconstruction ? "supported" : "not supported", sl_state.version.empty() ? "" : "; ",
                    sl_state.version.c_str());
        }
        // (added by frame generation) DLSS-G, Reflex and PCL, where they were loaded (vulkan_framegen_dlss.cpp uses them)
        streamline_latency_device_made(adapter);
    }

    void before_destroy() override
    {
        SlState& sl_state = streamline();
        restore_routes();
        if (sl_state.initialised) {
            sl_state.functions.shutdown();
            sl_state.initialised = false;
            sl_state.device_set = false;
        }
    }

private:
    // Streamline's mandatory hooks in manual hooking (sl_hooks.h: the present, the swapchain's calls, the device's
    // wait): volk's function pointers for them set to Streamline's proxies (they call the driver's after Streamline's
    // bookkeeping), the driver's put back before it shuts down.
    struct Routes {
        PFN_vkQueuePresentKHR present = nullptr;
        PFN_vkCreateSwapchainKHR create_swapchain = nullptr;
        PFN_vkDestroySwapchainKHR destroy_swapchain = nullptr;
        PFN_vkGetSwapchainImagesKHR swapchain_images = nullptr;
        PFN_vkAcquireNextImageKHR acquire = nullptr;
        PFN_vkDeviceWaitIdle wait_idle = nullptr;
    };
    static Routes current()
    {
        return {.present = vkQueuePresentKHR,
                .create_swapchain = vkCreateSwapchainKHR,
                .destroy_swapchain = vkDestroySwapchainKHR,
                .swapchain_images = vkGetSwapchainImagesKHR,
                .acquire = vkAcquireNextImageKHR,
                .wait_idle = vkDeviceWaitIdle};
    }
    static void install(const Routes& routes)
    {
        vkQueuePresentKHR = routes.present;
        vkCreateSwapchainKHR = routes.create_swapchain;
        vkDestroySwapchainKHR = routes.destroy_swapchain;
        vkGetSwapchainImagesKHR = routes.swapchain_images;
        vkAcquireNextImageKHR = routes.acquire;
        vkDeviceWaitIdle = routes.wait_idle;
    }
    void route_through_streamline(VkDevice device)
    {
        auto* const proxy = function_of<std::remove_pointer_t<PFN_vkGetDeviceProcAddr>>(
            streamline().functions.library, "vkGetDeviceProcAddr");
        if (proxy == nullptr) {
            return;
        }
        const auto get = [proxy, device]<typename Function>(const char* name, Function fallback) {
            const PFN_vkVoidFunction found = proxy(device, name);
            return found != nullptr ? same_address<Function>(found) : fallback;
        };
        const Routes driver = current();
        install({.present = get("vkQueuePresentKHR", driver.present),
                 .create_swapchain = get("vkCreateSwapchainKHR", driver.create_swapchain),
                 .destroy_swapchain = get("vkDestroySwapchainKHR", driver.destroy_swapchain),
                 .swapchain_images = get("vkGetSwapchainImagesKHR", driver.swapchain_images),
                 .acquire = get("vkAcquireNextImageKHR", driver.acquire),
                 .wait_idle = get("vkDeviceWaitIdle", driver.wait_idle)});
        driver_ = driver;
        routed_ = true;
    }
    void restore_routes()
    {
        if (routed_) {
            install(driver_);
            routed_ = false;
        }
    }

    Routes driver_;
    bool routed_ = false;

    template <typename Features> static void or_flags(const Features& from, Features* into)
    {
        using Bytes = std::array<std::byte, sizeof(Features)>;
        const auto source = std::bit_cast<Bytes>(from);
        auto target = std::bit_cast<Bytes>(*into);
        const std::size_t first = offsetof(Features, pNext) + sizeof(void*);
        for (std::size_t at = first; at < target.size(); ++at) {
            core::at(target, at) |= core::at(source, at);
        }
        *into = std::bit_cast<Features>(target);
    }

    uint32_t graphics_queues_ = 0;
    uint32_t compute_queues_ = 0;
};

StreamlineAdditions& additions()
{
    static StreamlineAdditions instance;
    return instance;
}

sl::float4x4 as_sl(const std::array<std::array<float, 4>, 4>& rows)
{
    sl::float4x4 matrix{};
    for (std::size_t row = 0; row < rows.size(); ++row) {
        const std::array<float, 4>& from = core::at(rows, row);
        core::at(matrix.row, row) = {core::at(from, 0), core::at(from, 1), core::at(from, 2), core::at(from, 3)};
    }
    return matrix;
}

sl::float4x4 as_sl(const Float4x4& values)
{
    std::array<std::array<float, 4>, 4> rows{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            core::at(core::at(rows, row), column) = core::at(values, (row * 4) + column);
        }
    }
    return as_sl(rows);
}

Float4x4 as_floats(const std::array<std::array<float, 4>, 4>& rows)
{
    Float4x4 values{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            core::at(values, (row * 4) + column) = core::at(core::at(rows, row), column);
        }
    }
    return values;
}

sl::DLSSMode mode_of(UpscaleQuality quality)
{
    switch (quality) {
    case UpscaleQuality::kNative:
        return sl::DLSSMode::eDLAA;
    case UpscaleQuality::kQuality:
        return sl::DLSSMode::eMaxQuality;
    case UpscaleQuality::kBalanced:
        return sl::DLSSMode::eBalanced;
    case UpscaleQuality::kPerformance:
        return sl::DLSSMode::eMaxPerformance;
    case UpscaleQuality::kUltra:
        return sl::DLSSMode::eUltraPerformance;
    }
    return sl::DLSSMode::eMaxQuality;
}

class DlssProvider final : public UpscaleProvider {
public:
    explicit DlssProvider(bool ray_reconstruction) : ray_reconstruction_(ray_reconstruction) {}
    DlssProvider(const DlssProvider&) = delete;
    DlssProvider& operator=(const DlssProvider&) = delete;
    DlssProvider(DlssProvider&&) = delete;
    DlssProvider& operator=(DlssProvider&&) = delete;
    ~DlssProvider() override = default;

    [[nodiscard]] const char* name() const override { return "DLSS"; }
    [[nodiscard]] std::string describe() const override
    {
        const SlState& sl_state = streamline();
        return std::string(ray_reconstruction_ ? "NVIDIA DLSS Ray Reconstruction" : "NVIDIA DLSS Super Resolution") +
               (sl_state.version.empty() ? "" : " (" + sl_state.version + ")");
    }

    bool evaluate(Backend& state, const UpscaleFrame& frame, std::string* error) override
    {
        const SlFunctions& calls = streamline().functions;
        if (calls.frame_token == nullptr || calls.constants == nullptr || calls.tag == nullptr || calls.evaluate == nullptr) {
            *error = "Streamline's functions are missing";
            return false;
        }
        // (added by frame generation) one token a frame, shared with DLSS-G's and Reflex's markers when they're loaded;
        // else the provider's own count, as before
        sl::FrameToken* token = nullptr;
        if (streamline_frames_shared()) {
            token = streamline_frame_token();
        } else {
            const uint32_t index = frame_index_++;
            if (calls.frame_token(token, &index) != sl::Result::eOk) {
                token = nullptr;
            }
        }
        if (token == nullptr) {
            *error = "slGetNewFrameToken failed";
            return false;
        }
        const sl::ViewportHandle viewport(kViewport);
        // the constants: the cameras without the jitter, the jitter apart
        const sl::Constants constants = streamline_constants(state, frame, frame.reset);
        if (calls.constants(constants, *token, viewport) != sl::Result::eOk) {
            *error = "slSetConstants failed";
            return false;
        }
        streamline().constants_given = true;
        const Float4x4 view = as_floats(frame.view);
        Float4x4 to_world{};
        motion_inverse(view, &to_world);
        // the options
        const UpscaleSettings& settings = upscale_settings_locked();
        sl::Result set = sl::Result::eErrorInvalidParameter;
        if (ray_reconstruction_ && calls.dlssd_options != nullptr) {
            sl::DLSSDOptions options{};
            options.mode = mode_of(settings.quality);
            options.outputWidth = frame.output_size.width;
            options.outputHeight = frame.output_size.height;
            options.colorBuffersHDR = sl::Boolean::eFalse;
            options.normalRoughnessMode = sl::DLSSDNormalRoughnessMode::ePacked;
            options.worldToCameraView = as_sl(view);
            options.cameraViewToWorld = as_sl(to_world);
            set = calls.dlssd_options(viewport, options);
        } else if (calls.dlss_options != nullptr) {
            sl::DLSSOptions options{};
            options.mode = mode_of(settings.quality);
            options.outputWidth = frame.output_size.width;
            options.outputHeight = frame.output_size.height;
            options.colorBuffersHDR = sl::Boolean::eFalse;
            set = calls.dlss_options(viewport, options);
        }
        if (set != sl::Result::eOk) {
            *error = "the DLSS options weren't taken (" + std::to_string(static_cast<int>(set)) + ")";
            return false;
        }
        // the images
        const sl::Extent render{.top = 0, .left = 0, .width = frame.render.width, .height = frame.render.height};
        const sl::Extent shown{.top = 0, .left = 0, .width = frame.output_size.width, .height = frame.output_size.height};
        sl::Resource colour = streamline_resource(*frame.colour);
        sl::Resource output = streamline_resource(*frame.output);
        sl::Resource depth = streamline_resource(*frame.depth);
        sl::Resource motion_vectors = streamline_resource(*frame.motion);
        sl::Resource reactive = streamline_resource(*frame.reactive);
        std::vector<sl::ResourceTag> tags{
            // (the images are the upscaler's own, unchanged until the next frame's preparation: valid until the present
            // - Streamline needn't copy them)
            sl::ResourceTag(&colour, sl::kBufferTypeScalingInputColor, kKept, &render),
            sl::ResourceTag(&output, sl::kBufferTypeScalingOutputColor, kKept, &shown),
            sl::ResourceTag(&depth, sl::kBufferTypeDepth, kKept, &render),
            sl::ResourceTag(&motion_vectors, sl::kBufferTypeMotionVectors, kKept, &render),
            sl::ResourceTag(&reactive, sl::kBufferTypeBiasCurrentColorHint, kKept, &render),
        };
        sl::Resource albedo = frame.albedo != nullptr ? streamline_resource(*frame.albedo) : sl::Resource{};
        sl::Resource specular = frame.specular_albedo != nullptr ? streamline_resource(*frame.specular_albedo) : sl::Resource{};
        sl::Resource normals = frame.normal_roughness != nullptr ? streamline_resource(*frame.normal_roughness) : sl::Resource{};
        if (ray_reconstruction_) {
            tags.emplace_back(&albedo, sl::kBufferTypeAlbedo, kKept, &render);
            tags.emplace_back(&specular, sl::kBufferTypeSpecularAlbedo, kKept, &render);
            tags.emplace_back(&normals, sl::kBufferTypeNormalRoughness, kKept, &render);
        }
        auto* commands = same_address<sl::CommandBuffer*>(frame.commands);
        if (calls.tag(*token, viewport, tags.data(), static_cast<uint32_t>(tags.size()), commands) != sl::Result::eOk) {
            *error = "slSetTagForFrame failed";
            return false;
        }
        std::array<const sl::BaseStructure*, 1> inputs{&viewport};
        const sl::Result evaluated = calls.evaluate(ray_reconstruction_ ? sl::kFeatureDLSS_RR : sl::kFeatureDLSS, *token,
                                                inputs.data(), static_cast<uint32_t>(inputs.size()), commands);
        if (evaluated != sl::Result::eOk) {
            *error = "slEvaluateFeature returned " + std::to_string(static_cast<int>(evaluated));
            return false;
        }
        return true;
    }

    [[nodiscard]] UpscaleJitterTerms jitter_terms() const override { return kDlssTerms; }

    void release(Backend& /*state*/) override
    {
        const SlState& sl_state = streamline();
        if (sl_state.device_set && sl_state.functions.free_resources != nullptr) {
            sl_state.functions.free_resources(ray_reconstruction_ ? sl::kFeatureDLSS_RR : sl::kFeatureDLSS,
                                              sl::ViewportHandle(kViewport));
        }
    }

private:
    bool ray_reconstruction_ = false;
    uint32_t frame_index_ = 0;
};

} // namespace

SlState& streamline()
{
    static SlState state;
    return state;
}

sl::Resource streamline_resource(const Image& image)
{
    sl::Resource resource(sl::ResourceType::eTex2d, image.image, nullptr, image.view,
                          static_cast<uint32_t>(VK_IMAGE_LAYOUT_GENERAL));
    resource.width = image.extent.width;
    resource.height = image.extent.height;
    resource.nativeFormat = static_cast<uint32_t>(image.format);
    resource.mipLevels = image.levels;
    resource.arrayLayers = 1;
    resource.flags = 0;
    resource.usage = static_cast<uint32_t>(image.usage);
    return resource;
}

sl::Constants streamline_constants(const Backend& state, const UpscaleFrame& frame, bool reset)
{
    const MotionVectors& motion = state.motion;
    const Float4x4 view = as_floats(frame.view);
    const Float4x4 projection = as_floats(frame.projection);
    Float4x4 to_view{};
    Float4x4 to_world{};
    Float4x4 clip_to_world{};
    motion_inverse(projection, &to_view);
    motion_inverse(view, &to_world);
    motion_inverse(motion_multiply(view, projection), &clip_to_world);
    const Float4x4 to_before = motion_multiply(clip_to_world, motion_multiply(motion.before.view, motion.before.projection));
    Float4x4 from_before{};
    motion_inverse(to_before, &from_before);
    sl::Constants constants{};
    constants.cameraViewToClip = as_sl(projection);
    constants.clipToCameraView = as_sl(to_view);
    constants.clipToLensClip = as_sl(std::array<std::array<float, 4>, 4>{
        {{1.0F, 0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F}}});
    constants.clipToPrevClip = as_sl(to_before);
    constants.prevClipToClip = as_sl(from_before);
    // (DLSS is given the offset that takes the jitter back out, as FSR)
    constants.jitterOffset = {-frame.jitter.at(0), -frame.jitter.at(1)};
    constants.mvecScale = {1.0F, 1.0F}; // (the motion is already in 0 - 1 of the picture)
    constants.cameraPinholeOffset = {0.0F, 0.0F};
    // the camera's place and axes: the inverse view's rows (D3D's: x right, y up, z forward)
    constexpr std::size_t kRow = 4;
    constants.cameraRight = {core::at(to_world, 0), core::at(to_world, 1), core::at(to_world, 2)};
    constants.cameraUp = {core::at(to_world, kRow), core::at(to_world, kRow + 1), core::at(to_world, kRow + 2)};
    constants.cameraFwd = {core::at(to_world, 2 * kRow), core::at(to_world, (2 * kRow) + 1),
                           core::at(to_world, (2 * kRow) + 2)};
    constants.cameraPos = {core::at(to_world, 3 * kRow), core::at(to_world, (3 * kRow) + 1),
                           core::at(to_world, (3 * kRow) + 2)};
    constants.cameraNear = frame.near_z;
    constants.cameraFar = frame.far_z;
    constants.cameraFOV = frame.vertical_fov;
    constants.cameraAspectRatio =
        static_cast<float>(frame.render.width) / static_cast<float>(std::max(frame.render.height, 1U));
    constants.depthInverted = sl::Boolean::eFalse;
    constants.cameraMotionIncluded = sl::Boolean::eTrue;
    constants.motionVectors3D = sl::Boolean::eFalse;
    constants.reset = reset ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    constants.orthographicProjection = sl::Boolean::eFalse;
    constants.motionVectorsDilated = sl::Boolean::eFalse;
    constants.motionVectorsJittered = sl::Boolean::eFalse;
    return constants;
}

bool streamline_frames_shared()
{
    const SlState& sl_state = streamline();
    return sl_state.device_set && (sl_state.reflex || sl_state.frame_generation);
}

sl::FrameToken* streamline_frame_token()
{
    SlState& sl_state = streamline();
    if (sl_state.token == nullptr && sl_state.functions.frame_token != nullptr) {
        const uint32_t index = ++sl_state.frame_index;
        if (sl_state.functions.frame_token(sl_state.token, &index) != sl::Result::eOk) {
            sl_state.token = nullptr;
        }
        sl_state.constants_given = false;
    }
    return sl_state.token;
}

void end_streamline_frame()
{
    SlState& sl_state = streamline();
    sl_state.token = nullptr;
    sl_state.constants_given = false;
}

ContextAdditions* dlss_context_additions() { return &additions(); }

bool ray_reconstruction_ready() { return streamline().ray_reconstruction; }

std::unique_ptr<UpscaleProvider> make_dlss_provider(Backend& /*state*/, Extent /*output*/, bool ray_reconstruction,
                                                    std::string* why)
{
    const SlState& sl_state = streamline();
    if (!sl_state.initialised) {
        *why = sl_state.problem.empty() ? "Streamline wasn't initialised before the device" : sl_state.problem;
        return nullptr;
    }
    if (!sl_state.nvidia) {
        *why = "not an NVIDIA device";
        return nullptr;
    }
    if (!sl_state.device_set || !sl_state.dlss || sl_state.functions.dlss_options == nullptr) {
        *why = sl_state.problem.empty() ? "DLSS isn't supported on this device or driver" : sl_state.problem;
        return nullptr;
    }
    bool reconstruct = ray_reconstruction;
    if (reconstruct && (!sl_state.ray_reconstruction || sl_state.functions.dlssd_options == nullptr)) {
        get_log("vulkan: upscaler: Ray Reconstruction isn't supported here - DLSS Super Resolution alone");
        reconstruct = false;
    }
    return std::make_unique<DlssProvider>(reconstruct);
}

#else

ContextAdditions* dlss_context_additions() { return nullptr; }

bool ray_reconstruction_ready() { return false; }

std::unique_ptr<UpscaleProvider> make_dlss_provider(Backend& /*state*/, Extent /*output*/, bool /*ray_reconstruction*/,
                                                    std::string* why)
{
    *why = "this build has no DLSS (a 64-bit build with THUG2_DLSS on has it)";
    return nullptr;
}

#endif

} // namespace render::vulkan
