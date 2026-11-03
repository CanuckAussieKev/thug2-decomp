// The PC gamma: see pc_gamma.h.

#include "engine/gfx/pc_gamma.h"

#include <windows.h>

#include <d3d9.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <array>

#include "engine/core/checked_index.h"
#include "engine/core/no_destroy.h"
#include "engine/gfx/renderer.h"

namespace render {

namespace {
// NOT ORIGINAL grouping - pc_gamma.cpp's state (the original's globals). Made on first use, never destroyed
// (pc_gamma_state()).
struct PcGammaState {
    std::array<float, 3> gammas_{{1.0f, 1.0f, 1.0f}};
    std::array<uint16_t, 768> gamma_ramp_{};
};

PcGammaState& pc_gamma_state()
{
    static core::NoDestroy<PcGammaState> state;
    return state.get();
}

std::array<float, 3>& gammas() { return pc_gamma_state().gammas_; }
std::array<uint16_t, 768>& get_ramp() { return pc_gamma_state().gamma_ramp_; } // red, then green and blue, 256 each

// A value kept within 0-1 (a NaN kept, as the original's tests let it through).
float clamp01(float value)
{
    if (value < 0.0f)
        return 0.0f;
    return value > 1.0f ? 1.0f : value;
}

} // namespace

// 0x004D06A0
std::array<float, 3> get_device_gamma()
{
    return {{static_cast<float>((static_cast<double>(core::at(gammas(), 0)) - 0.5f) * 0.5f),
             static_cast<float>((static_cast<double>(core::at(gammas(), 1)) - 0.5f) * 0.5f),
             static_cast<float>((static_cast<double>(core::at(gammas(), 2)) - 0.5f) * 0.5f)}};
}

// 0x004D06F0
void build_gamma_ramp()
{
    std::array<float, 3> exponent{};
    for (int k = 0; k < 3; ++k)
        core::at(exponent, k) = static_cast<float>(1.0f / static_cast<double>(core::at(gammas(), k)));
    for (int i = 0; i < 0x100; ++i) {
        auto level = static_cast<float>(i) * (1.0f / 256.0f);
        for (int k = 0; k < 3; ++k)
            core::at(get_ramp(), 0x100 * k + i) =
                static_cast<uint16_t>(static_cast<int>(std::pow(static_cast<double>(level), core::at(exponent, k)) * 65536.0f));
    }
}

// 0x004D07B0
void set_device_gamma(float r, float g, float b)
{
    r = clamp01(r);
    g = clamp01(g);
    b = clamp01(b);
    core::at(gammas(), 0) = static_cast<float>(static_cast<double>(r) + r + 0.5f);
    core::at(gammas(), 1) = static_cast<float>(static_cast<double>(g) + g + 0.5f);
    core::at(gammas(), 2) = static_cast<float>(static_cast<double>(b) + b + 0.5f);
    build_gamma_ramp();
    D3DGAMMARAMP ramp{}; // (red, green, blue: the table's three runs)
    static_assert(sizeof(ramp) == sizeof(pc_gamma_state().gamma_ramp_));
    std::memcpy(&ramp, pc_gamma_state().gamma_ramp_.data(), sizeof(ramp));
    renderer().d3d_device()->SetGammaRamp(0, 0, &ramp);
}

} // namespace render
