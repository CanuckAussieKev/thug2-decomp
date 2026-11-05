#pragma once

#include <cstdint>
#include <array>

// The pixel shader handles the pixel shader maker fills in (the original's table of 18 words at 0x007CA920; a slot is
// named by its offset from it), and the vertex-format ids of the mesh loader (6 words at 0x00775B64).

namespace render {

constexpr int kPixelShader920 = 0;
constexpr int kPixelShader924 = 1; // (grass)
constexpr int kPixelShader928 = 2;
constexpr int kPixelShader92c = 3;
constexpr int kPixelShader930 = 4;
constexpr int kPixelShader934 = 5;
constexpr int kPixelShader938 = 6;
constexpr int kPixelShader93c = 7;
constexpr int kPixelShader940 = 8;  // constant alpha x texture alpha
constexpr int kPixelShader944 = 9;  // vertex alpha x texture alpha
constexpr int kPixelShader948 = 10; // the bloom cut-off
constexpr int kPixelShader94c = 11; // vertex colour alone
constexpr int kPixelShader950 = 12; // texture x vertex colour x2, texture alpha
constexpr int kPixelShader954 = 13; // the factor alone
constexpr int kPixelShader958 = 14;
constexpr int kPixelShader95c = 15; // vertex colour x2 x the factor, vertex alpha
constexpr int kPixelShader960 = 16; // texture x constant alpha
constexpr int kPixelShader964 = 17; // the default: texture x vertex colour x4
constexpr int kPixelShaderCount = 18;

const std::array<void*, kPixelShaderCount>& pixel_shaders();

// The vertex-format ids, in address order (0x00775B64 + 4 * index; a mesh's type t is index 6 - t).

} // namespace render
