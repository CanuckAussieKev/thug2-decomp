#pragma once

#include <string>

#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_context.h"
#include "engine/gfx/ui_backend.h"

// NOT ORIGINAL - Vulkan's render::Gpu and render::GpuContext (vulkan_gpu.cpp, vulkan_draws.cpp, vulkan_gpu_context.cpp;
// docs/design/vulkan.md, V6 and V7): the resources - textures in Direct3D 9's formats (BC, the 16-bit ones, P8
// expanded), vertex and index buffers, render targets, depth surfaces (D24S8, else D32S8), system-memory surfaces,
// copies and read-backs, occlusion queries, samplers - and the draws: pipelines from the state block, rendering passes
// inferred from the targets, the frames in flight, the present. app/ installs it with renderer=vulkan. No Vulkan header
// is seen through this one (tools/vulkan_headers.test.ts).
namespace render::vulkan {

// The Vulkan Gpu and its context (made on first use, never destroyed).
Gpu& gpu();
GpuContext& gpu_context();

// The UI backend for the editor's and the new front end's Dear ImGui parts: none yet on Vulkan (V9) - it refuses to
// start (logged once), so those parts aren't drawn and the old HUD / the original menus stand in.
UiBackend& ui_backend();

// Whether this machine can draw with the Vulkan Gpu: the loader, and the device the context would pick (`device_index`:
// -1 the best, discrete first) with what the draws need. Asked before the game's window is made.
struct Usable {
    bool usable = false;
    std::string device;  // the device's name (when one was found)
    std::string problem; // why not
};
Usable usable(int device_index);

// Before the context is made: the physical device to use (-1: the best) and the folder the shader and pipeline caches
// are kept in (ending in a separator; empty: none kept).
void configure(int device_index, const std::string& cache_folder);

} // namespace render::vulkan
