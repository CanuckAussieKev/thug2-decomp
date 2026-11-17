#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// NOT ORIGINAL - (round L) the relit world's light store holds physical light (docs/design/vulkan.md "Round L: a
// physical light store").
//
// The relit world's per-vertex light (rt_cel.h's stream, the light field, the bounce's hits) is light in the global unit
// (rt_sky.h: 1 a white diffuser under the reference day - about 26 000 cd/m^2), NOT pre-exposed: what the frame's
// pre-exposure does (rt_daylight.h pre_expose - x 1 by day, up to x 65 536 at night) is applied where the light is drawn
// (the draws' vertex uniforms, the ray-traced passes' geometry records: one value a frame, frame_pre_exposure), so a
// change of the pre-exposure changes nothing that is stored - no relight.
//
// A light word is RGB9E5 - a shared exponent and a 9-bit mantissa per channel (Vulkan's E5B9G9R9_UFLOAT_PACK32 layout,
// EXT_texture_shared_exponent's encoding: red in bits 0 - 8, green 9 - 17, blue 18 - 26, the exponent 27 - 31, bias 15).
// 32 bits a colour like the 8-bit words before, but a range of 2^-24 .. 65 408 with 9 bits of precision from 2^-15 up.
// The light is stored x kLightStoreScale (2^10) so that range is 6e-8 .. 64 units - 1.5e-3 .. 1.7e6 cd/m^2, its
// 9 bits from 7.8e-4 cd/m^2 (a moonless night's ground ~ 6e-4 cd/m^2 still within 0.3 %; 1e-4 cd/m^2 within 1 %), a
// lamp's lens (10^4 - 10^6 cd/m^2) and the noon sun's light on a white wall (~ 3 x 10^4) without clipping.
// Bandwidth: the same 4 bytes a word as the 8-bit store; the stream grew from 6 words to 8 (the flags the light words'
// alphas held, and a vertex's own emission) - 32 bytes a vertex.
namespace render::rt {

inline constexpr float kLightStoreScale = 1024.0F;
inline constexpr int kStoreMantissaBits = 9;
inline constexpr int kStoreExponentBias = 15;
inline constexpr int kStoreMostExponent = 31;
inline constexpr uint32_t kStoreMantissaMask = (1U << kStoreMantissaBits) - 1U;
// The largest stored value (the format's), the smallest positive one.
inline constexpr float kStoreMostValue = 65408.0F;
inline constexpr float kStoreLeastValue = 5.9604645e-8F; // 2^-24

// Light (the global unit, linear, at least 0) as a word, and back.
[[nodiscard]] inline uint32_t store_light(const std::array<float, 3>& light)
{
    std::array<float, 3> scaled{};
    float most = 0.0F;
    for (std::size_t ch = 0; ch < scaled.size(); ++ch) {
        const float value = light[ch] * kLightStoreScale;
        scaled[ch] = std::isfinite(value) ? std::clamp(value, 0.0F, kStoreMostValue) : 0.0F;
        most = std::max(most, scaled[ch]);
    }
    if (!(most >= kStoreLeastValue * 0.5F)) {
        return 0U;
    }
    // the shared exponent: the brightest channel's, so its mantissa is < 2^9 (one more if it rounds up to 2^9)
    int exponent = std::max(-kStoreExponentBias - 1, static_cast<int>(std::floor(std::log2(most)))) + 1 + kStoreExponentBias;
    const auto step = [](int e) { return std::ldexp(1.0, e - kStoreExponentBias - kStoreMantissaBits); };
    if (std::floor((static_cast<double>(most) / step(exponent)) + 0.5) >= static_cast<double>(1U << kStoreMantissaBits)) {
        ++exponent;
    }
    exponent = std::clamp(exponent, 0, kStoreMostExponent);
    uint32_t out = static_cast<uint32_t>(exponent) << 27U;
    for (std::size_t ch = 0; ch < scaled.size(); ++ch) {
        const double mantissa = std::floor((static_cast<double>(scaled[ch]) / step(exponent)) + 0.5);
        out |= std::min(static_cast<uint32_t>(mantissa), kStoreMantissaMask) << (kStoreMantissaBits * static_cast<int>(ch));
    }
    return out;
}
[[nodiscard]] inline std::array<float, 3> stored_light(uint32_t word)
{
    const int exponent = static_cast<int>(word >> 27U);
    const double step = std::ldexp(1.0, exponent - kStoreExponentBias - kStoreMantissaBits) / kLightStoreScale;
    return {static_cast<float>(static_cast<double>(word & kStoreMantissaMask) * step),
            static_cast<float>(static_cast<double>((word >> 9U) & kStoreMantissaMask) * step),
            static_cast<float>(static_cast<double>((word >> 18U) & kStoreMantissaMask) * step)};
}

// The same decode in GLSL (paste before use): stored_light(w) the light in the global unit.
inline constexpr const char* kLightStoreGlsl =
    "#ifndef THUG2_LIGHT_STORE\n"
    "#define THUG2_LIGHT_STORE\n"
    "const float LIGHT_STORE_SCALE = 1024.0;\n"
    "vec3 stored_light(uint w)\n"
    "{\n"
    "    float unit = exp2(float(int(w >> 27)) - 24.0) / LIGHT_STORE_SCALE;\n"
    "    return vec3(float(w & 511u), float((w >> 9) & 511u), float((w >> 18) & 511u)) * unit;\n"
    "}\n"
    "#endif\n";

} // namespace render::rt
