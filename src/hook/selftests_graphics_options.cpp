// Self-tests for the image-quality options (src/game/gfx/graphics_options.h; not original): the ini values' parsing
// and clamping, the anisotropy a device gets, the minification filter picked, the sample count picked, and that the
// default scales leave distances bit for bit as they were. (The device-bound parts - the device's multisampling,
// the scene surface and its resolves - are checked by game runs.)

#include <windows.h>

#include <d3d9.h>

#include <bit>
#include <cstdint>

#include "game/gfx/graphics_options.h"
#include "hook/selftest.h"

namespace {

// A device that has 2, 4 and 8 samples (not 3, 5-7 or above 8).
bool TwoFourEight(int samples, void* context)
{
    ++*static_cast<int*>(context);
    return samples == 2 || samples == 4 || samples == 8;
}
bool None(int, void* context)
{
    ++*static_cast<int*>(context);
    return false;
}

void TestGraphicsOptions()
{
    using namespace Nx::GraphicsOptions;
    Check(ParseAnisotropy(0) == 0 && ParseAnisotropy(1) == 0 && ParseAnisotropy(-4) == 0 && ParseAnisotropy(2) == 2 &&
              ParseAnisotropy(16) == 16 && ParseAnisotropy(17) == 16 && ParseAnisotropy(1000) == 16,
          "graphics options: anisotropy parsed");
    Check(ParseMultisample(0) == 0 && ParseMultisample(1) == 0 && ParseMultisample(2) == 2 && ParseMultisample(8) == 8 &&
              ParseMultisample(32) == 16,
          "graphics options: msaa parsed");
    Check(ParseScale(nullptr) == 1.0f && ParseScale("") == 1.0f && ParseScale("abc") == 1.0f && ParseScale("0") == 1.0f &&
              ParseScale("-2") == 1.0f && ParseScale("nan") == 1.0f && ParseScale("1") == 1.0f &&
              ParseScale("2.5") == 2.5f && ParseScale("0.1") == 0.25f && ParseScale("100") == 8.0f &&
              ParseScale("1.0 ; note") == 1.0f,
          "graphics options: scales parsed and clamped");
    Check(DeviceAnisotropy(0, true, 16) == 0 && DeviceAnisotropy(16, true, 16) == 16 && DeviceAnisotropy(16, true, 8) == 8 &&
              DeviceAnisotropy(4, true, 16) == 4 && DeviceAnisotropy(16, false, 16) == 0 &&
              DeviceAnisotropy(16, true, 1) == 0 && DeviceAnisotropy(8, true, 2) == 2,
          "graphics options: the device's anisotropy");
    Check(MinFilter(true, 0) == D3DTEXF_LINEAR && MinFilter(false, 0) == D3DTEXF_POINT &&
              MinFilter(true, 8) == D3DTEXF_ANISOTROPIC && MinFilter(false, 8) == D3DTEXF_POINT,
          "graphics options: minification filter");
    int asked = 0;
    Check(PickSamples(8, &TwoFourEight, &asked) == 8 && asked == 1, "graphics options: 8 samples where there are");
    asked = 0;
    Check(PickSamples(6, &TwoFourEight, &asked) == 4 && asked == 3, "graphics options: 6 asked, 4 given");
    asked = 0;
    Check(PickSamples(16, &TwoFourEight, &asked) == 8, "graphics options: 16 asked, 8 given");
    asked = 0;
    Check(PickSamples(0, &TwoFourEight, &asked) == 0 && asked == 0, "graphics options: msaa off asks nothing");
    asked = 0;
    Check(PickSamples(8, &None, &asked) == 0 && asked == 7, "graphics options: no multisampling on the device");
    // The default scale: the same bits (including odd ones); others multiply.
    const float odd[] = {96000.0f, 10710.0f, 0.1f, -3.5f, std::bit_cast<float>(0x7F800001u), 1e-40f};
    bool same = true;
    for (float d : odd)
        same = same && std::bit_cast<uint32_t>(Scaled(d, 1.0f)) == std::bit_cast<uint32_t>(d);
    Check(same && Scaled(96000.0f, 2.0f) == 192000.0f && Scaled(600.0f, 0.5f) == 300.0f,
          "graphics options: distances scaled");
    // The defaults (what an ini without the keys gives) are the original's.
    Options defaults;
    Check(defaults.anisotropy == 0 && defaults.msaa == 0 && defaults.drawDistanceScale == 1.0f &&
              defaults.lodDistanceScale == 1.0f,
          "graphics options: defaults off");
}

} // namespace

SELFTEST(Early, TestGraphicsOptions);
