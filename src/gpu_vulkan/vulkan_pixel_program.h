#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "gpu_vulkan/ff_key.h"

// NOT ORIGINAL - which of the game's ps.1.1 pixel shaders a shader's text is (Gpu::create_pixel_shader is given the
// text the PC layer assembles: render_pc/pixel_shaders.cpp), as the fragment key's program (ff_key.h's PixelProgram):
// one of the six fixed shaders, or a material's generated shader with its passes, their blend modes and their alpha
// flags read back from the lines the generator writes. Text the classifier doesn't know is the texture stages
// (kStages), and says so.
namespace render::vulkan {

struct ShaderProgram {
    bool known = false; // the text was recognised
    PixelProgram program = PixelProgram::kStages;
    uint8_t material_passes = 0;
    uint8_t material_alpha_flags = 0;
    std::array<uint8_t, 4> material_modes{};
};

[[nodiscard]] ShaderProgram classify_pixel_shader(std::string_view source);

// The program's part of a fragment key.
void apply_program(const ShaderProgram& program, FragmentKey* key);

} // namespace render::vulkan
