// The PC gamma: see pc_gamma.h.

#include "engine/gfx/pc_gamma.h"

#include <windows.h>

#include <d3d9.h>

#include <cmath>
#include <cstdint>

#include "engine/gfx/device.h"

namespace render {

namespace {
float g_gammas[3]{1.0f, 1.0f, 1.0f};
uint16_t g_gamma_ramp[768];

float* gammas() { return g_gammas; }
uint16_t* get_ramp() { return g_gamma_ramp; } // red, then green and blue, 256 each

// A value kept within 0-1 (a NaN kept, as the original's tests let it through).
float clamp01(float value)
{
    if (value < 0.0f)
        return 0.0f;
    return value > 1.0f ? 1.0f : value;
}

} // namespace

// 0x004D06A0
void get_device_gamma(float* r, float* g, float* b)
{
    *r = static_cast<float>((static_cast<double>(gammas()[0]) - 0.5f) * 0.5f);
    *g = static_cast<float>((static_cast<double>(gammas()[1]) - 0.5f) * 0.5f);
    *b = static_cast<float>((static_cast<double>(gammas()[2]) - 0.5f) * 0.5f);
}

// 0x004D06F0
void build_gamma_ramp()
{
    float exponent[3];
    for (int k = 0; k < 3; ++k)
        exponent[k] = static_cast<float>(1.0f / static_cast<double>(gammas()[k]));
    for (int i = 0; i < 0x100; ++i) {
        auto level = static_cast<float>(i) * (1.0f / 256.0f);
        for (int k = 0; k < 3; ++k)
            get_ramp()[0x100 * k + i] =
                static_cast<uint16_t>(static_cast<int>(std::pow(static_cast<double>(level), exponent[k]) * 65536.0f));
    }
}

// 0x004D07B0
void set_device_gamma(float r, float g, float b)
{
    r = clamp01(r);
    g = clamp01(g);
    b = clamp01(b);
    gammas()[0] = static_cast<float>(static_cast<double>(r) + r + 0.5f);
    gammas()[1] = static_cast<float>(static_cast<double>(g) + g + 0.5f);
    gammas()[2] = static_cast<float>(static_cast<double>(b) + b + 0.5f);
    build_gamma_ramp();
    g_d3d_device->SetGammaRamp(0, 0, reinterpret_cast<const D3DGAMMARAMP*>(get_ramp()));
}

} // namespace render
