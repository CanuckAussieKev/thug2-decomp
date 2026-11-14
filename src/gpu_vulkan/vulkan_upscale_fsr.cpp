// NOT ORIGINAL - the FSR provider of the upscaler (vulkan_upscale.h; docs/design/vulkan.md "Upscaling"): AMD's FSR SDK
// (github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK, cmake/upscalers.cmake) through its C API - the headers MIT, the
// signed, prebuilt amd_fidelityfx_loader_vk.dll and amd_fidelityfx_upscaler_vk.dll under AMD's binary licence
// (THIRD_PARTY_NOTICES.md), loaded at run time from beside the exe (no import library: any compiler builds this). The
// upscaler DLL holds FSR 3.1 and FSR 4 and picks the newest the device runs (fsr_version=3: FSR 3.1).
#include "gpu_vulkan/vulkan_upscale.h"

#include <memory>
#include <string>

#ifdef THUG2_FSR
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_loadso.h>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <string_view>
#include <volk.h>

#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_ffx.h"
#include "platform/run_log.h"

// (the SDK's headers: C, MIT - the API's through vulkan_ffx.h)
#include <ffx_upscale.h>
#endif

namespace render::vulkan {

#ifdef THUG2_FSR
namespace {

constexpr const char* kLoaderName = "amd_fidelityfx_loader_vk.dll";
// FSR's jitter convention (UpscaleJitterTerms; measured by thug2_vulkan_upscale_check)
constexpr UpscaleJitterTerms kFsrTerms{.sign = {-1.0F, -1.0F}, .align = {-1.0F, -1.0F}};
// THUG2's world units are inches (a skater is about 72 tall): FSR's scale to metres.
constexpr float kMetresPerUnit = 0.0254F;

template <typename Function> Function function_of(SDL_SharedObject* library, const char* name)
{
    // (a C function's address as its type: the same bits)
    return std::bit_cast<Function>(std::bit_cast<std::uintptr_t>(SDL_LoadFunction(library, name)));
}

// FSR's messages into the log (wide text: its ASCII kept).
void on_message(uint32_t type, const wchar_t* message)
{
    constexpr wchar_t kFirstNonAscii = 0x80;
    std::string text;
    if (message != nullptr) {
        for (const wchar_t letter : std::wstring_view(message)) {
            text += letter < kFirstNonAscii ? static_cast<char>(letter) : '?';
        }
    }
    get_log("vulkan: upscaler: FSR says (%u): %s", type, text.c_str());
}

class FsrProvider final : public UpscaleProvider {
public:
    FsrProvider() = default;
    FsrProvider(const FsrProvider&) = delete;
    FsrProvider& operator=(const FsrProvider&) = delete;
    FsrProvider(FsrProvider&&) = delete;
    FsrProvider& operator=(FsrProvider&&) = delete;
    ~FsrProvider() override = default;

    bool create(Backend& state, Extent output, unsigned major, std::string* why)
    {
        device_ = {.vkDevice = state.context.device(),
                   .vkPhysicalDevice = state.context.physical_device(),
                   .vkDeviceProcAddr = vkGetDeviceProcAddr};
        ffxCreateBackendVKDesc backend{};
        backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
        backend.deviceContext = device_;
        ffxCreateContextDescUpscaleVersion version{};
        version.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION;
        version.version = FFX_UPSCALER_VERSION;
        ffxCreateContextDescUpscale desc{};
        desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
        // (the colour is the display's 0 - 1 after the tone mapping: perceptual, not HDR)
        desc.flags = FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE;
        desc.maxRenderSize = {.width = output.width, .height = output.height};
        desc.maxUpscaleSize = {.width = output.width, .height = output.height};
        desc.fpMessage = on_message;
        desc.header.pNext = &backend.header;
        backend.header.pNext = &version.header;
        // a version asked for (fsr_version): the first the DLL lists whose name starts with it
        ffxOverrideVersion override_version{};
        std::array<uint64_t, kMostVersions> ids{};
        std::array<const char*, kMostVersions> names{};
        if (major != 0) {
            uint64_t count = ids.size();
            ffxQueryDescGetVersions versions{};
            versions.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
            versions.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
            versions.device = &device_;
            versions.outputCount = &count;
            versions.versionIds = ids.data();
            versions.versionNames = names.data();
            if (ffx_functions().query(nullptr, &versions.header) == FFX_API_RETURN_OK) {
                const std::string wanted = std::to_string(major) + ".";
                for (uint64_t i = 0; i < count && i < ids.size(); ++i) {
                    if (names.at(i) != nullptr && std::string(names.at(i)).starts_with(wanted)) {
                        override_version.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
                        override_version.versionId = ids.at(i);
                        version.header.pNext = &override_version.header;
                        break;
                    }
                }
            }
        }
        const ffxReturnCode_t made = ffx_functions().create(&context_, &desc.header, nullptr);
        if (made != FFX_API_RETURN_OK) {
            *why = "ffxCreateContext returned " + std::to_string(made);
            context_ = nullptr;
            return false;
        }
        ffxQueryGetProviderVersion provider{};
        provider.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
        if (ffx_functions().query(&context_, &provider.header) == FFX_API_RETURN_OK && provider.versionName != nullptr) {
            version_ = provider.versionName;
        }
        return true;
    }

    [[nodiscard]] const char* name() const override { return "FSR"; }
    [[nodiscard]] std::string describe() const override
    {
        return "AMD FSR upscaler " + (version_.empty() ? std::string("(version not reported)") : version_);
    }

    bool evaluate(Backend& /*state*/, const UpscaleFrame& frame, std::string* error) override
    {
        ffxDispatchDescUpscale dispatch{};
        dispatch.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
        dispatch.commandList = frame.commands;
        dispatch.color = ffx_resource(*frame.colour, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        dispatch.depth = ffx_resource(*frame.depth, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        dispatch.motionVectors = ffx_resource(*frame.motion, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        dispatch.reactive = ffx_resource(*frame.reactive, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        dispatch.output = ffx_resource(*frame.output, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        // (FSR is given the offset that takes the jitter back out: the projection moved the picture by +jitter)
        dispatch.jitterOffset = {.x = -frame.jitter.at(0), .y = -frame.jitter.at(1)};
        // (the motion is in 0 - 1 of the drawn picture: times its size in pixels)
        dispatch.motionVectorScale = {.x = static_cast<float>(frame.render.width),
                                      .y = static_cast<float>(frame.render.height)};
        dispatch.renderSize = {.width = frame.render.width, .height = frame.render.height};
        dispatch.upscaleSize = {.width = frame.output_size.width, .height = frame.output_size.height};
        dispatch.enableSharpening = frame.sharpness > 0.0F;
        dispatch.sharpness = frame.sharpness;
        dispatch.frameTimeDelta = frame.frame_milliseconds;
        dispatch.preExposure = 1.0F;
        dispatch.reset = frame.reset;
        dispatch.cameraNear = frame.near_z;
        dispatch.cameraFar = frame.far_z;
        dispatch.cameraFovAngleVertical = frame.vertical_fov;
        dispatch.viewSpaceToMetersFactor = kMetresPerUnit;
        dispatch.flags = FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB;
        const ffxReturnCode_t done = ffx_functions().dispatch(&context_, &dispatch.header);
        if (done != FFX_API_RETURN_OK) {
            *error = "ffxDispatch returned " + std::to_string(done);
            return false;
        }
        return true;
    }

    [[nodiscard]] UpscaleJitterTerms jitter_terms() const override { return kFsrTerms; }

    void release(Backend& /*state*/) override
    {
        if (context_ != nullptr) {
            if (ffx_functions().destroy != nullptr) {
                ffx_functions().destroy(&context_, nullptr);
            }
            context_ = nullptr;
        }
    }

private:
    static constexpr std::size_t kMostVersions = 16;
    ffxContext context_ = nullptr;
    VkDeviceContext device_{};
    std::string version_;
};

} // namespace

FfxFunctions& ffx_functions()
{
    static FfxFunctions loaded;
    return loaded;
}

bool load_ffx_loader(std::string* why)
{
    FfxFunctions& loaded = ffx_functions();
    if (loaded.library != nullptr) {
        return true;
    }
    const char* base = SDL_GetBasePath();
    const std::string path = std::string(base != nullptr ? base : "") + kLoaderName;
    loaded.library = SDL_LoadObject(path.c_str());
    if (loaded.library == nullptr) {
        *why = std::string(kLoaderName) + " isn't beside the exe";
        return false;
    }
    loaded.create = function_of<PfnFfxCreateContext>(loaded.library, "ffxCreateContext");
    loaded.destroy = function_of<PfnFfxDestroyContext>(loaded.library, "ffxDestroyContext");
    loaded.configure = function_of<PfnFfxConfigure>(loaded.library, "ffxConfigure");
    loaded.query = function_of<PfnFfxQuery>(loaded.library, "ffxQuery");
    loaded.dispatch = function_of<PfnFfxDispatch>(loaded.library, "ffxDispatch");
    if (loaded.create == nullptr || loaded.destroy == nullptr || loaded.query == nullptr || loaded.dispatch == nullptr) {
        SDL_UnloadObject(loaded.library);
        loaded = FfxFunctions{};
        *why = std::string(kLoaderName) + " lacks the FSR API's functions";
        return false;
    }
    return true;
}

FfxApiResource ffx_resource(const Image& image, uint32_t state)
{
    const VkImageCreateInfo info{.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                                 .pNext = nullptr,
                                 .flags = 0,
                                 .imageType = VK_IMAGE_TYPE_2D,
                                 .format = image.format,
                                 .extent = {.width = image.extent.width, .height = image.extent.height, .depth = 1},
                                 .mipLevels = image.levels,
                                 .arrayLayers = 1,
                                 .samples = image.samples,
                                 .tiling = VK_IMAGE_TILING_OPTIMAL,
                                 .usage = image.usage,
                                 .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                                 .queueFamilyIndexCount = 0,
                                 .pQueueFamilyIndices = nullptr,
                                 .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    return ffxApiGetResourceVK(image.image, ffxApiGetImageResourceDescriptionVK(image.image, info, 0), state);
}

std::unique_ptr<UpscaleProvider> make_fsr_provider(Backend& state, Extent output, std::string* why)
{
    if (!load_ffx_loader(why)) {
        return nullptr;
    }
    auto provider = std::make_unique<FsrProvider>();
    if (!provider->create(state, output, upscale_settings_locked().fsr_major, why)) {
        return nullptr;
    }
    return provider;
}

#else

std::unique_ptr<UpscaleProvider> make_fsr_provider(Backend& /*state*/, Extent /*output*/, std::string* why)
{
    *why = "this build has no FSR (a 64-bit build with THUG2_FSR on has it)";
    return nullptr;
}

#endif

} // namespace render::vulkan
