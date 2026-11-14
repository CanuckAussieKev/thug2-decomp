// NOT ORIGINAL - AMD FSR 3 frame generation (vulkan_framegen.h; docs/design/vulkan.md "Frame generation"): AMD's FSR SDK
// 2.4 (cmake/upscalers.cmake) through its C API - the signed, prebuilt amd_fidelityfx_framegeneration_vk.dll beside the
// loader (vulkan_ffx.h), AMD's binary licence (THIRD_PARTY_NOTICES.md). Its frame-generation swapchain replaces the
// context's (SwapchainOwner: made with the context's create info, its replacement calls used for the images, the acquire
// and the present); it presents the made frame and the drawn one from its own threads on its own queues (two of the
// device's, DeviceWants::present_queues), paced by itself. Each frame the frame-generation context is given the depth and
// motion vectors (the upscaler's) with the camera (the "prepare" dispatch, after the upscaler) and, before the present,
// whether to make a frame and the HUD-less colour - FSR finds the 2D by it and keeps it out of the made frame. 2x: FSR 3
// makes one frame between two drawn ones (FSR 4's frame generation on an RDNA 4 device, the same API).
#include "gpu_vulkan/vulkan_framegen.h"

#include <memory>
#include <string>

#ifdef THUG2_FSR
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <volk.h>

#include "base/core/checked_index.h"
#include "gpu_vulkan/ff_uniforms.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_ffx.h"
#include "gpu_vulkan/vulkan_motion.h"
#include "platform/run_log.h"

// (the SDK's headers: C, MIT)
#include <ffx_framegeneration.h>
#include <vk/ffx_api_framegeneration_vk.h>
#endif

namespace render::vulkan {

#ifdef THUG2_FSR
namespace {

constexpr uint32_t kFsrMultiplier = 2;
constexpr float kMetresPerUnit = 0.0254F; // (THUG2's inches, as the upscaler's)
constexpr VkFormat kHudlessFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr double kMillisecondsPerSecond = 1000.0;
constexpr float kLongestFrameMilliseconds = 100.0F;

// The frame-generation swapchain and context, made with the swapchain (one of each at a time).
class FsrFrameGen final : public SwapchainOwner {
public:
    // The present queues the device was made with (two: the present's and the image acquire's).
    void queues(const ContextAdditions::DeviceMade& made)
    {
        queues_ready_ = false;
        if (made.present_queues < 2) {
            return;
        }
        device_ = made.device;
        physical_ = made.physical;
        game_family_ = made.family;
        present_family_ = made.present_family;
        vkGetDeviceQueue(made.device, made.present_family, made.present_first, &present_queue_);
        vkGetDeviceQueue(made.device, made.present_family, made.present_first + 1, &acquire_queue_);
        queues_ready_ = present_queue_ != nullptr && acquire_queue_ != nullptr;
    }
    [[nodiscard]] bool queues_ready() const { return queues_ready_; }
    void set_game_queue(VkQueue queue) { game_queue_ = queue; }

    VkResult create(const VkSwapchainCreateInfoKHR& info, VkSwapchainKHR* swapchain) override
    {
        FfxFunctions& calls = ffx_functions();
        *swapchain = VK_NULL_HANDLE;
        ffxCreateContextDescFrameGenerationSwapChainVersionVK version{};
        version.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_VERSION_VK;
        version.version = FFX_FRAMEGENERATION_SWAPCHAIN_VK_VERSION;
        ffxCreateContextDescFrameGenerationSwapChainVK desc{};
        desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FGSWAPCHAIN_VK;
        desc.header.pNext = &version.header;
        desc.physicalDevice = physical_;
        desc.device = device_;
        desc.swapchain = swapchain;
        desc.allocator = nullptr;
        desc.createInfo = info;
        desc.createInfo.oldSwapchain = VK_NULL_HANDLE;
        desc.gameQueue = {.queue = game_queue_, .familyIndex = game_family_, .submitFunc = nullptr};
        // (no async work: the generation runs on the game's queue - the present queue stands in for the compute one)
        desc.asyncComputeQueue = {.queue = present_queue_, .familyIndex = present_family_, .submitFunc = nullptr};
        desc.presentQueue = {.queue = present_queue_, .familyIndex = present_family_, .submitFunc = nullptr};
        desc.imageAcquireQueue = {.queue = acquire_queue_, .familyIndex = present_family_, .submitFunc = nullptr};
        const ffxReturnCode_t made = calls.create(&swapchain_context_, &desc.header, nullptr);
        if (made != FFX_API_RETURN_OK || *swapchain == VK_NULL_HANDLE) {
            get_log("vulkan: frame generation: FSR's swapchain couldn't be made (ffxCreateContext returned %d) - Vulkan's own",
                    static_cast<int>(made));
            swapchain_context_ = nullptr;
            *swapchain = VK_NULL_HANDLE;
            fallen_back_ = true;
            return vkCreateSwapchainKHR(device_, &info, nullptr, swapchain);
        }
        fallen_back_ = false;
        replacements_ = {};
        replacements_.header.type = FFX_API_QUERY_DESC_TYPE_FGSWAPCHAIN_FUNCTIONS_VK;
        if (calls.query(&swapchain_context_, &replacements_.header) != FFX_API_RETURN_OK ||
            replacements_.pOutAcquireNextImageKHR == nullptr || replacements_.pOutQueuePresentKHR == nullptr ||
            replacements_.pOutGetSwapchainImagesKHR == nullptr || replacements_.pOutDestroySwapchainFFXAPI == nullptr) {
            get_log("vulkan: frame generation: FSR's swapchain gives none of its calls");
            destroy(*swapchain);
            *swapchain = VK_NULL_HANDLE;
            fallen_back_ = true;
            return vkCreateSwapchainKHR(device_, &info, nullptr, swapchain);
        }
        swapchain_ = *swapchain;
        presents_seen_ = 0;
        frame_id_ = 0;
        extent_ = {.width = info.imageExtent.width, .height = info.imageExtent.height};
        make_generation(info);
        return VK_SUCCESS;
    }

    void destroy(VkSwapchainKHR swapchain) override
    {
        if (fallen_back_ || swapchain_context_ == nullptr) {
            vkDestroySwapchainKHR(device_, swapchain, nullptr);
            return;
        }
        FfxFunctions& calls = ffx_functions();
        ffxDispatchDescFrameGenerationSwapChainWaitForPresentsVK wait{};
        wait.header.type = FFX_API_DISPATCH_DESC_TYPE_FGSWAPCHAIN_WAIT_FOR_PRESENTS_VK;
        calls.dispatch(&swapchain_context_, &wait.header);
        release_generation();
        calls.destroy(&swapchain_context_, nullptr);
        swapchain_context_ = nullptr;
        if (replacements_.pOutDestroySwapchainFFXAPI != nullptr) {
            replacements_.pOutDestroySwapchainFFXAPI(device_, swapchain, nullptr, nullptr);
        }
        replacements_ = {};
        swapchain_ = VK_NULL_HANDLE;
    }

    VkResult images(VkSwapchainKHR swapchain, uint32_t* count, VkImage* images) override
    {
        if (fallen_back_ || swapchain_context_ == nullptr) {
            return vkGetSwapchainImagesKHR(device_, swapchain, count, images);
        }
        return replacements_.pOutGetSwapchainImagesKHR(device_, swapchain, count, images);
    }

    VkResult acquire(VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore acquired, uint32_t* image) override
    {
        if (fallen_back_ || swapchain_context_ == nullptr) {
            return vkAcquireNextImageKHR(device_, swapchain, timeout, acquired, VK_NULL_HANDLE, image);
        }
        return replacements_.pOutAcquireNextImageKHR(device_, swapchain, timeout, acquired, VK_NULL_HANDLE, image);
    }

    VkResult present(VkQueue queue, const VkPresentInfoKHR& present) override
    {
        if (fallen_back_ || swapchain_context_ == nullptr) {
            return vkQueuePresentKHR(queue, &present);
        }
        return replacements_.pOutQueuePresentKHR(queue, &present);
    }

    // --- the frame (the provider's) ---------------------------------------------------------------------------------

    [[nodiscard]] bool generating() const { return generation_context_ != nullptr; }

    bool prepare(Backend& state, const FrameGenFrame& frame, std::string* error)
    {
        if (generation_context_ == nullptr) {
            *error = "no frame-generation context";
            return false;
        }
        const UpscaleFrame& upscaled = *frame.inputs;
        ffxDispatchDescFrameGenerationPrepareV2 prepare{};
        prepare.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2;
        prepare.frameID = frame_id_;
        prepare.flags = 0;
        prepare.commandList = frame.commands;
        prepare.renderSize = {.width = upscaled.render.width, .height = upscaled.render.height};
        // (as the upscaler: the offset that takes the jitter back out; the motion in 0 - 1 of the drawn picture)
        prepare.jitterOffset = {.x = -upscaled.jitter.at(0), .y = -upscaled.jitter.at(1)};
        prepare.motionVectorScale = {.x = static_cast<float>(upscaled.render.width), .y = static_cast<float>(upscaled.render.height)};
        const auto now = std::chrono::steady_clock::now();
        prepare.frameTimeDelta = frame_id_ == 0 ? upscaled.frame_milliseconds
                                                : std::min(kLongestFrameMilliseconds,
                                                           static_cast<float>(std::chrono::duration<double>(now - last_).count() *
                                                                              kMillisecondsPerSecond));
        last_ = now;
        prepare.reset = frame.reset;
        prepare.cameraNear = upscaled.near_z;
        prepare.cameraFar = upscaled.far_z;
        prepare.cameraFovAngleVertical = upscaled.vertical_fov;
        prepare.viewSpaceToMetersFactor = kMetresPerUnit;
        prepare.depth = ffx_resource(*upscaled.depth, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        prepare.motionVectors = ffx_resource(*upscaled.motion, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        // the camera's place and axes: the inverse view's rows (D3D's: x right, y up, z forward)
        Float4x4 view{};
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t column = 0; column < 4; ++column) {
                core::at(view, (row * 4) + column) = core::at(core::at(upscaled.view, row), column);
            }
        }
        Float4x4 to_world{};
        motion_inverse(view, &to_world);
        constexpr std::size_t kRow = 4;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            core::at(prepare.cameraRight, axis) = core::at(to_world, axis);
            core::at(prepare.cameraUp, axis) = core::at(to_world, kRow + axis);
            core::at(prepare.cameraForward, axis) = core::at(to_world, (2 * kRow) + axis);
            core::at(prepare.cameraPosition, axis) = core::at(to_world, (3 * kRow) + axis);
        }
        const ffxReturnCode_t done = ffx_functions().dispatch(&generation_context_, &prepare.header);
        if (done != FFX_API_RETURN_OK) {
            *error = "the prepare dispatch returned " + std::to_string(done);
            return false;
        }
        state.bound = BoundState{}; // (its own pipelines and descriptors)
        prepared_ = frame_id_;
        has_prepared_ = true;
        return true;
    }

    bool configure(const FrameGenFrame& frame, std::string* error)
    {
        if (generation_context_ == nullptr) {
            *error = "no frame-generation context";
            return false;
        }
        const bool on = frame.on && has_prepared_ && prepared_ == frame_id_;
        ffxConfigureDescFrameGeneration config{};
        config.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
        config.swapChain = swapchain_;
        config.presentCallback = nullptr;
        config.presentCallbackUserContext = nullptr;
        // (the swapchain asks for the made frame at the present: the frame-generation context's dispatch)
        config.frameGenerationCallback = [](ffxDispatchDescFrameGeneration* params, void* context) -> ffxReturnCode_t {
            return ffx_functions().dispatch(static_cast<ffxContext*>(context), &params->header);
        };
        config.frameGenerationCallbackUserContext = &generation_context_;
        config.frameGenerationEnabled = on;
        config.allowAsyncWorkloads = false;
        if (on && frame.hudless != nullptr) {
            config.HUDLessColor = ffx_resource(*frame.hudless, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        config.flags = 0;
        config.onlyPresentGenerated = false;
        config.generationRect = {.left = 0, .top = 0, .width = static_cast<int32_t>(extent_.width),
                                 .height = static_cast<int32_t>(extent_.height)};
        config.frameID = frame_id_;
        const ffxReturnCode_t done = ffx_functions().configure(&generation_context_, &config.header);
        if (done != FFX_API_RETURN_OK) {
            *error = "the frame-generation configure returned " + std::to_string(done);
            return false;
        }
        return true;
    }

    // The presents the swapchain made since the last call; the frame's id moved on (by one a frame, as FSR asks).
    uint64_t presented()
    {
        ++frame_id_;
        if (fallen_back_ || swapchain_context_ == nullptr || replacements_.pOutGetLastPresentCountFFXAPI == nullptr) {
            return 1;
        }
        const uint64_t count = replacements_.pOutGetLastPresentCountFFXAPI(swapchain_);
        const uint64_t added = count >= presents_seen_ ? count - presents_seen_ : 0;
        presents_seen_ = count;
        return added;
    }

    // The frame-generation context gone before the swapchain (shut down: the provider's release).
    void release_generation()
    {
        if (generation_context_ != nullptr) {
            ffxConfigureDescFrameGeneration config{};
            config.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
            config.swapChain = swapchain_;
            config.frameGenerationEnabled = false;
            config.frameID = frame_id_;
            ffx_functions().configure(&generation_context_, &config.header);
            ffx_functions().destroy(&generation_context_, nullptr);
            generation_context_ = nullptr;
            ++released_;
        }
        has_prepared_ = false;
    }

    [[nodiscard]] uint64_t made() const { return made_; }
    [[nodiscard]] uint64_t released() const { return released_; }
    [[nodiscard]] const std::string& version() const { return version_; }

private:
    void make_generation(const VkSwapchainCreateInfoKHR& info)
    {
        FfxFunctions& calls = ffx_functions();
        const VkDeviceContext device{.vkDevice = device_, .vkPhysicalDevice = physical_, .vkDeviceProcAddr = vkGetDeviceProcAddr};
        ffxCreateBackendVKDesc backend{};
        backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
        backend.deviceContext = device;
        ffxCreateContextDescFrameGenerationVersion version{};
        version.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_VERSION;
        version.version = FFX_FRAMEGENERATION_VERSION;
        ffxCreateContextDescFrameGenerationHudless hudless{};
        hudless.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_HUDLESS;
        hudless.hudlessBackBufferFormat = ffxApiGetSurfaceFormatVK(kHudlessFormat);
        ffxCreateContextDescFrameGeneration desc{};
        desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
        desc.header.pNext = &backend.header;
        backend.header.pNext = &version.header;
        version.header.pNext = &hudless.header;
        // (the depth isn't inverted nor infinite; the motion at the drawn size, the jitter taken out of it)
        desc.flags = 0;
        desc.displaySize = {.width = info.imageExtent.width, .height = info.imageExtent.height};
        desc.maxRenderSize = desc.displaySize;
        desc.backBufferFormat = ffxApiGetSurfaceFormatVK(info.imageFormat);
        const ffxReturnCode_t made = calls.create(&generation_context_, &desc.header, nullptr);
        if (made != FFX_API_RETURN_OK) {
            get_log("vulkan: frame generation: FSR's frame-generation context couldn't be made (ffxCreateContext returned %d)",
                    static_cast<int>(made));
            generation_context_ = nullptr;
            return;
        }
        ++made_;
        ffxQueryGetProviderVersion provider{};
        provider.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
        if (calls.query(&generation_context_, &provider.header) == FFX_API_RETURN_OK && provider.versionName != nullptr) {
            version_ = provider.versionName;
        }
    }

    VkDevice device_ = nullptr;
    VkPhysicalDevice physical_ = nullptr;
    VkQueue game_queue_ = nullptr;
    VkQueue present_queue_ = nullptr;
    VkQueue acquire_queue_ = nullptr;
    uint32_t game_family_ = 0;
    uint32_t present_family_ = 0;
    bool queues_ready_ = false;
    bool fallen_back_ = false;
    ffxContext swapchain_context_ = nullptr;
    ffxContext generation_context_ = nullptr;
    ffxQueryDescSwapchainReplacementFunctionsVK replacements_{};
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    Extent extent_{};
    uint64_t frame_id_ = 0;
    uint64_t prepared_ = 0;
    bool has_prepared_ = false;
    uint64_t presents_seen_ = 0;
    uint64_t made_ = 0;
    uint64_t released_ = 0;
    std::chrono::steady_clock::time_point last_;
    std::string version_;
};

FsrFrameGen& owner()
{
    static FsrFrameGen instance;
    return instance;
}

class FsrFrameGenProvider final : public FrameGenProvider {
public:
    FsrFrameGenProvider() = default;
    FsrFrameGenProvider(const FsrFrameGenProvider&) = delete;
    FsrFrameGenProvider& operator=(const FsrFrameGenProvider&) = delete;
    FsrFrameGenProvider(FsrFrameGenProvider&&) = delete;
    FsrFrameGenProvider& operator=(FsrFrameGenProvider&&) = delete;
    ~FsrFrameGenProvider() override = default;

    [[nodiscard]] const char* name() const override { return "FSR"; }
    [[nodiscard]] std::string describe() const override
    {
        const std::string& version = owner().version();
        return "AMD FSR frame generation " + (version.empty() ? std::string("(version not reported yet)") : version);
    }
    [[nodiscard]] uint32_t most_multiplier() const override { return kFsrMultiplier; }
    [[nodiscard]] bool wants_ui() const override { return false; }
    bool inputs(Backend& state, const FrameGenFrame& frame, std::string* error) override
    {
        return !owner().generating() || owner().prepare(state, frame, error);
    }
    bool before_present(Backend& /*state*/, const FrameGenFrame& frame, std::string* error) override
    {
        return !owner().generating() || owner().configure(frame, error);
    }
    uint64_t after_present(Backend& /*state*/) override { return owner().presented(); }
    void release(Backend& /*state*/) override { owner().release_generation(); }
    [[nodiscard]] uint64_t made() const override { return owner().made() - made_before_; }
    [[nodiscard]] uint64_t released() const override { return owner().released() - released_before_; }
    void start_counts()
    {
        made_before_ = owner().made();
        released_before_ = owner().released();
    }

private:
    uint64_t made_before_ = 0;
    uint64_t released_before_ = 0;
};

} // namespace

SwapchainOwner* fsr_swapchain_owner() { return &owner(); }

void fsr_framegen_queues(const ContextAdditions::DeviceMade& made) { owner().queues(made); }

std::unique_ptr<FrameGenProvider> make_fsr_framegen_provider(Backend& state, std::string* why)
{
    if (!load_ffx_loader(why)) {
        return nullptr;
    }
    if (!owner().queues_ready()) {
        *why = "the device has no spare queue that presents (FSR presents from queues of its own)";
        return nullptr;
    }
    if (state.context.swapchain() != VK_NULL_HANDLE) {
        *why = "the swapchain was made before FSR could take it";
        return nullptr;
    }
    owner().set_game_queue(state.context.queue());
    state.context.set_swapchain_owner(&owner());
    auto provider = std::make_unique<FsrFrameGenProvider>();
    provider->start_counts();
    return provider;
}

#else

SwapchainOwner* fsr_swapchain_owner() { return nullptr; }
void fsr_framegen_queues(const ContextAdditions::DeviceMade& /*made*/) {}

std::unique_ptr<FrameGenProvider> make_fsr_framegen_provider(Backend& /*state*/, std::string* why)
{
    *why = "this build has no FSR (a 64-bit build with THUG2_FSR on has it)";
    return nullptr;
}

#endif

} // namespace render::vulkan
