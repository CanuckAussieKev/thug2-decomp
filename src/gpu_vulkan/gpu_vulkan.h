#pragma once

#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_context.h"

// NOT ORIGINAL - Vulkan's render::Gpu and render::GpuContext (vulkan_gpu.cpp, vulkan_gpu_context.cpp; docs/design/
// vulkan.md, V6): the resources - textures in Direct3D 9's formats (BC, the 16-bit ones, P8 expanded), vertex and index
// buffers (static, dynamic with discard / no-overwrite, the ring the draws from memory copy into), render targets, depth
// surfaces (D24S8, else D32S8), system-memory surfaces, copies and read-backs, occlusion queries, samplers - on the
// device the context makes for the game's window. Draws, pipelines and the frame are V7's: until then the draws record
// nothing, present shows nothing, and app/ doesn't install it (renderer=vulkan still runs Direct3D 9). No Vulkan header
// is seen through this one (tools/vulkan_headers.test.ts).
namespace render::vulkan {

// The Vulkan Gpu and its context (made on first use, never destroyed).
Gpu& gpu();
GpuContext& gpu_context();

} // namespace render::vulkan
