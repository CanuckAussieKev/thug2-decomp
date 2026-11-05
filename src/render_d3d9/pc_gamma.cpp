// The PC gamma: see pc_gamma.h.

#include "render_d3d9/pc_gamma.h"

#include <windows.h>

#include <d3d9.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <array>

#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "render_d3d9/d3d9_state.h"

namespace render {

constexpr std::size_t kRampEntries = 768; // three channels of 256
constexpr float kGammaOffset = 0.5F;
constexpr float kGammaScale = 0.5F;
constexpr int kRampChannelSize = 0x100;
constexpr float kRampLevels = 256.0F;
constexpr float kRampMax = 65536.0F;

namespace {
// NOT ORIGINAL grouping - pc_gamma.cpp's state (the original's globals). Made on first use, never destroyed
// (pc_gamma_state()).
struct PcGammaState {
    std::array<float, 3> gammas_{{1.0F, 1.0F, 1.0F}};
    std::array<uint16_t, kRampEntries> gamma_ramp_{};
};

PcGammaState& pc_gamma_state()
{
    static core::NoDestroy<PcGammaState> state;
    return state.get();
}

std::array<float, 3>& gammas() { return pc_gamma_state().gammas_; }
std::array<uint16_t, kRampEntries>& get_ramp() { return pc_gamma_state().gamma_ramp_; } // red, then green and blue, 256 each

// A value kept within 0-1 (a NaN kept, as the original's tests let it through).
float clamp01(float value)
{
    if (value < 0.0F) {
        return 0.0F;
    }
    return value > 1.0F ? 1.0F : value;
}

} // namespace

// 0x004D06A0
std::array<float, 3> get_device_gamma()
{
    return {{static_cast<float>((static_cast<double>(core::at(gammas(), 0)) - kGammaOffset) * kGammaScale),
             static_cast<float>((static_cast<double>(core::at(gammas(), 1)) - kGammaOffset) * kGammaScale),
             static_cast<float>((static_cast<double>(core::at(gammas(), 2)) - kGammaOffset) * kGammaScale)}};
}

// 0x004D06F0
void build_gamma_ramp()
{
    std::array<float, 3> exponent{};
    for (int k = 0; k < 3; ++k) {
        core::at(exponent, k) = static_cast<float>(1.0F / static_cast<double>(core::at(gammas(), k)));
    }
    for (int i = 0; i < kRampChannelSize; ++i) {
        auto level = static_cast<float>(i) * (1.0F / kRampLevels);
        for (int k = 0; k < 3; ++k) {
            core::at(get_ramp(), (kRampChannelSize * k) + i) = static_cast<uint16_t>(
                static_cast<int>(std::pow(static_cast<double>(level), core::at(exponent, k)) * kRampMax));
        }
    }
}

// 0x004D07B0
void set_device_gamma(float red, float green, float b)
{
    red = clamp01(red);
    green = clamp01(green);
    b = clamp01(b);
    core::at(gammas(), 0) = static_cast<float>(static_cast<double>(red) + red + kGammaOffset);
    core::at(gammas(), 1) = static_cast<float>(static_cast<double>(green) + green + kGammaOffset);
    core::at(gammas(), 2) = static_cast<float>(static_cast<double>(b) + b + kGammaOffset);
    build_gamma_ramp();
    D3DGAMMARAMP ramp{}; // (red, green, blue: the table's three runs)
    static_assert(sizeof(ramp) == sizeof(pc_gamma_state().gamma_ramp_));
    std::memcpy(&ramp, pc_gamma_state().gamma_ramp_.data(), sizeof(ramp));
    d3d9::state().d3d_device()->SetGammaRamp(0, 0, &ramp);
}

} // namespace render
