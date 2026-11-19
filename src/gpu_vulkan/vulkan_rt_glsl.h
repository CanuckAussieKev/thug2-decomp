#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

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

// (round OM - opacity micromaps, vulkan_rt_micromaps.h) A shader that traverses an acceleration structure holding
// opacity micromaps must say so (Vulkan's "Ray Opacity Micromap": the SPIR-V capability RayTracingOpacityMicromapEXT of
// SPV_EXT_opacity_micromap); glslang (1.4.304) never declares it. While `declared` is set (the device was made with the
// extension - set before any shader is made, cleared when the device goes), compile_rt_glsl adds the capability and the
// extension to every module that uses ray queries; spirv_micromaps_declared() makes a module (one kept from an earlier
// run - the SPIR-V cache on disk) say so or not, as now. Thread-safe.
void set_micromaps_declared(bool declared);
[[nodiscard]] bool micromaps_declared();
void spirv_micromaps_declared(std::vector<uint32_t>* words, bool declared);

} // namespace render::vulkan
