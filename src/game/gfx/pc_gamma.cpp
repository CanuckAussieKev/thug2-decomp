// The PC gamma: see pc_gamma.h.

#include "game/gfx/pc_gamma.h"

#include <windows.h>

#include <d3d9.h>

#include <cmath>
#include <cstdint>

#include "game/gfx/device.h"

namespace Nx {

float g_gammas[3]{1.0f, 1.0f, 1.0f};
uint16_t g_gammaRamp[768];

namespace {

float* Gammas() { return g_gammas; }
uint16_t* Ramp() { return g_gammaRamp; } // red, then green and blue, 256 each

// A value kept within 0-1 (a NaN kept, as the original's tests let it through).
float Clamp01(float value)
{
    if (value < 0.0f)
        return 0.0f;
    return value > 1.0f ? 1.0f : value;
}

} // namespace

// 0x004D06A0
void __cdecl GetDeviceGamma(float* r, float* g, float* b)
{
    *r = static_cast<float>((static_cast<double>(Gammas()[0]) - 0.5f) * 0.5f);
    *g = static_cast<float>((static_cast<double>(Gammas()[1]) - 0.5f) * 0.5f);
    *b = static_cast<float>((static_cast<double>(Gammas()[2]) - 0.5f) * 0.5f);
}

// 0x004D06F0
void __cdecl BuildGammaRamp()
{
    float exponent[3];
    for (int k = 0; k < 3; ++k)
        exponent[k] = static_cast<float>(1.0f / static_cast<double>(Gammas()[k]));
    for (int i = 0; i < 0x100; ++i) {
        float level = static_cast<float>(i * (1.0f / 256.0f));
        for (int k = 0; k < 3; ++k)
            Ramp()[0x100 * k + i] =
                static_cast<uint16_t>(static_cast<int>(std::pow(static_cast<double>(level), exponent[k]) * 65536.0f));
    }
}

// 0x004D07B0
void __cdecl SetDeviceGamma(float r, float g, float b)
{
    r = Clamp01(r);
    g = Clamp01(g);
    b = Clamp01(b);
    Gammas()[0] = static_cast<float>(static_cast<double>(r) + r + 0.5f);
    Gammas()[1] = static_cast<float>(static_cast<double>(g) + g + 0.5f);
    Gammas()[2] = static_cast<float>(static_cast<double>(b) + b + 0.5f);
    BuildGammaRamp();
    g_d3dDevice->SetGammaRamp(0, 0, reinterpret_cast<const D3DGAMMARAMP*>(Ramp()));
}

} // namespace Nx
