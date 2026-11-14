#pragma once

// NOT ORIGINAL - AMD's FSR API loader, shared by the upscaler's FSR provider (vulkan_upscale_fsr.cpp, which defines it)
// and FSR 3's frame generation (vulkan_framegen_fsr.cpp): amd_fidelityfx_loader_vk.dll from beside the exe (a 64-bit
// build with THUG2_FSR only - cmake/upscalers.cmake), its five functions, and an image as the API's resource.
#ifdef THUG2_FSR
#include <SDL3/SDL_loadso.h>
#include <cstdint>
#include <string>
#include <volk.h>

#include "gpu_vulkan/vulkan_memory.h"

// (the SDK's headers: C, MIT)
#include <ffx_api.h>
#include <ffx_api_types.h>
#include <vk/ffx_api_vk.h>

namespace render::vulkan {

struct FfxFunctions {
    SDL_SharedObject* library = nullptr;
    PfnFfxCreateContext create = nullptr;
    PfnFfxDestroyContext destroy = nullptr;
    PfnFfxConfigure configure = nullptr;
    PfnFfxQuery query = nullptr;
    PfnFfxDispatch dispatch = nullptr;
};

FfxFunctions& ffx_functions();
// The loader loaded (once); false with the reason.
bool load_ffx_loader(std::string* why);
// An image as the API's resource, in `state` (FFX_API_RESOURCE_STATE_...).
[[nodiscard]] FfxApiResource ffx_resource(const Image& image, uint32_t state);

} // namespace render::vulkan
#endif
