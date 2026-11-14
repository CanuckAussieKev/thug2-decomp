#pragma once

// NOT ORIGINAL - NVIDIA Streamline's shared state (a 64-bit build with THUG2_DLSS only - cmake/upscalers.cmake): the
// library loaded once and initialised before the instance by the upscaler's DLSS provider (vulkan_upscale_dlss.cpp,
// which owns it), its functions looked up by name, and what frame generation's DLSS-G and Reflex
// (vulkan_framegen_dlss.cpp) use of it: their functions, and the frame token every feature of a frame shares.
#ifdef THUG2_DLSS
#include <SDL3/SDL_loadso.h>
#include <bit>
#include <cstdint>
#include <string>

#include "gpu_vulkan/vulkan_upscale.h"

// (Streamline's headers: C++, MIT)
#include <sl.h>
#include <sl_consts.h>
#include <sl_dlss.h>
#include <sl_dlss_d.h>
#include <sl_dlss_g.h>
#include <sl_helpers_vk.h>
#include <sl_pcl.h>
#include <sl_reflex.h>

namespace render::vulkan {

// sl.interposer.dll's functions (looked up by name: no import library), and the features' own (slGetFeatureFunction).
struct SlFunctions {
    SDL_SharedObject* library = nullptr;
    PFun_slInit* init = nullptr;
    PFun_slShutdown* shutdown = nullptr;
    PFun_slIsFeatureSupported* supported = nullptr;
    PFun_slGetFeatureRequirements* requirements = nullptr;
    PFun_slGetFeatureVersion* version = nullptr;
    PFun_slGetFeatureFunction* feature_function = nullptr;
    PFun_slGetNewFrameToken* frame_token = nullptr;
    PFun_slSetConstants* constants = nullptr;
    PFun_slSetTagForFrame* tag = nullptr;
    PFun_slEvaluateFeature* evaluate = nullptr;
    PFun_slFreeResources* free_resources = nullptr;
    PFun_slSetVulkanInfo* vulkan_info = nullptr;
    PFun_slSetFeatureLoaded* set_loaded = nullptr;
    PFun_slDLSSSetOptions* dlss_options = nullptr;
    PFun_slDLSSDSetOptions* dlssd_options = nullptr;
    // (added by frame generation)
    PFun_slDLSSGSetOptions* dlssg_options = nullptr;
    PFun_slDLSSGGetState* dlssg_state = nullptr;
    PFun_slReflexSetOptions* reflex_options = nullptr;
    PFun_slReflexGetState* reflex_state = nullptr;
    PFun_slReflexSleep* reflex_sleep = nullptr;
    PFun_slPCLSetMarker* pcl_marker = nullptr;
};

// Streamline's state across the context's life: initialised (before the instance), the device given, which features
// loaded and supported.
struct SlState {
    SlFunctions functions;
    bool initialised = false;
    bool device_set = false;
    bool nvidia = false;
    bool dlss = false;
    bool ray_reconstruction = false;
    // (added by frame generation) asked for at slInit (frame_generation / reflex), and supported on the device
    bool latency_loaded = false;     // Reflex and PCL
    bool generation_loaded = false;  // DLSS-G
    bool reflex = false;
    bool frame_generation = false;
    std::string problem; // why it can't be used
    std::string version;
    // (added by frame generation) the frame being made: its index and token (frame_token below), and whether its common
    // constants were given (once a frame - DLSS's provider gives them when it runs)
    uint32_t frame_index = 0;
    sl::FrameToken* token = nullptr;
    bool constants_given = false;
};

SlState& streamline();

// A pointer of one type as another, through its address (a DLL's export, Streamline's function lookups, a Vulkan
// handle): the same bits.
template <typename To, typename From> To same_address(From from)
{
    return std::bit_cast<To>(std::bit_cast<std::uintptr_t>(from));
}

// (frame generation) Whether the frames share one token (Reflex or DLSS-G loaded: their markers, constants and tags
// must name the frame presented); and the frame's, made on first use and let go at its present (end_streamline_frame).
[[nodiscard]] bool streamline_frames_shared();
sl::FrameToken* streamline_frame_token();
void end_streamline_frame();
// The common constants of a frame from the upscaler's (DLSS's provider and DLSS-G give the same), `reset` its history.
[[nodiscard]] sl::Constants streamline_constants(const Backend& state, const UpscaleFrame& frame, bool reset);
// An image as Streamline's resource (VK_IMAGE_LAYOUT_GENERAL).
[[nodiscard]] sl::Resource streamline_resource(const Image& image);
// (frame generation - vulkan_framegen_dlss.cpp) DLSS-G's, Reflex's and PCL's functions looked up and their support
// asked, Reflex's mode set, once the device is given to Streamline.
void streamline_latency_device_made(const sl::AdapterInfo& adapter);

} // namespace render::vulkan
#endif
