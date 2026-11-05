#pragma once

#include <cstdint>
#include <vector>

#include "render_vulkan/ff_key.h"

// NOT ORIGINAL - the fixed-function shader check's GPU part (ff_shader_check_gpu.cpp): every compiled module handed to
// a Vulkan driver (vkCreateShaderModule), and a graphics pipeline made for each sampled pair of shaders - the vertex
// format's attributes, the uniform blocks' and textures' descriptor layout of render_vulkan/ff_uniforms.h, a colour and
// depth render pass - so the driver compiles each pair and checks that the two halves' interfaces agree.
namespace ff_check {

struct GpuInput {
    const std::vector<render::vulkan::VertexKey>* vertex_keys = nullptr;
    const std::vector<render::vulkan::FragmentKey>* fragment_keys = nullptr;
    std::vector<const std::vector<uint32_t>*> vertex_spirv;   // in vertex_keys' order
    std::vector<const std::vector<uint32_t>*> fragment_spirv; // in fragment_keys' order
    const std::vector<render::vulkan::ShaderKey>* pairs = nullptr;
    int device_index = -1;   // the physical device (-1: the context's choice)
    int pipeline_limit = -1; // at most this many pipelines (-1: every pair)
};

// Prints what it did; false when a module or a pipeline was refused (true, with a note, when there is no Vulkan).
bool check_on_gpu(const GpuInput& input);

} // namespace ff_check
