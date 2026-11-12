#pragma once

#include <cstdint>
#include <string_view>

#include "gpu_vulkan/glsl_compiler.h"

// NOT ORIGINAL - GLSL for the ray-tracing work (vulkan_rt_glsl.cpp; docs/design/vulkan.md "Ray tracing"): what reads the
// material table (vulkan_rt_materials.h), traces ray queries or runs as a compute pass, compiled through glslang as
// glsl_compiler.h does, but for Vulkan 1.2 and SPIR-V 1.5 (descriptor indexing, scalar layout and ray queries are
// core there - the device has them when ray tracing is enabled). GLSL 4.60.
namespace render::vulkan {

enum class RtShaderStage : uint8_t {
    kVertex,
    kFragment,
    kCompute,
};

// Compiles GLSL for Vulkan 1.2 / SPIR-V 1.5. Thread-safe.
[[nodiscard]] CompiledShader compile_rt_glsl(RtShaderStage stage, std::string_view source);

} // namespace render::vulkan
