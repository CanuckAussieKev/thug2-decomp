#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "gpu_vulkan/ff_key.h"

// NOT ORIGINAL - GLSL to SPIR-V for the Vulkan backend (glsl_compiler.cpp), through glslang (the Khronos reference
// compiler, built from source with the game: cmake/vulkan_libraries.cmake). The fixed-function emulation's shaders are
// compiled at run time, the first time a key is drawn, and kept: the keys a game session draws come from its scenes'
// materials and from the Direct3D 9-style state left by the draws before (docs/design/vulkan.md), so they can't all be
// listed when the game is built - src/tests/ff_shader_check.cpp compiles the combinations the renderer's code can make.
// The SPIR-V targets Vulkan 1.0 (SPIR-V 1.0): every Vulkan device takes it.
namespace render::vulkan {

enum class ShaderStage : uint8_t {
    kVertex,
    kFragment,
};

struct CompiledShader {
    bool ok = false;
    std::vector<uint32_t> spirv; // the module's words
    std::string log;             // glslang's messages (the errors when !ok)
};

// The SPIR-V a shader is compiled for: Vulkan 1.0's (SPIR-V 1.0, every device), or - NOT ORIGINAL, ray tracing
// (engine/gfx/rt_scene.h) - Vulkan 1.2's (SPIR-V 1.4: GL_EXT_ray_query, used only where Context::ray_tracing_enabled()).
enum class ShaderTarget : uint8_t {
    kVulkan10,
    kVulkan12,
};

// Compiles GLSL 4.50 source for Vulkan. Thread-safe; glslang is set up on the first call and stays for the process.
[[nodiscard]] CompiledShader compile_glsl(ShaderStage stage, std::string_view source,
                                          ShaderTarget target = ShaderTarget::kVulkan10);

// The fixed-function emulation's shaders by key, generated (ff_shaders.h) and compiled the first time each is asked
// for, then kept. Not thread-safe: one per renderer thread.
class ShaderLibrary {
public:
    // The SPIR-V of a key's vertex / fragment shader (the key normalised: normalised(ShaderKey)); nullptr when it
    // doesn't compile, with glslang's messages and the source in `error` (a generator fault: the check program
    // compiles every combination the game makes).
    const std::vector<uint32_t>* vertex(const VertexKey& key, std::string* error = nullptr);
    const std::vector<uint32_t>* fragment(const FragmentKey& key, std::string* error = nullptr);

    [[nodiscard]] std::size_t vertex_count() const { return vertex_shaders_.size(); }
    [[nodiscard]] std::size_t fragment_count() const { return fragment_shaders_.size(); }

private:
    // A compiled shader, or the failure (kept too, so a bad key is reported once).
    using Entry = std::unique_ptr<CompiledShader>;
    std::unordered_map<VertexKey, Entry, VertexKeyHash> vertex_shaders_;
    std::unordered_map<FragmentKey, Entry, FragmentKeyHash> fragment_shaders_;
};

} // namespace render::vulkan
