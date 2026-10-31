#pragma once

#include <cstddef>
#include <cstdint>

namespace Maths {
class Vec4;
}

// The engine's model lights (Engine3D::CModelLights - the platform-independent part of the PC model lights, CPcModelLights
// in pc_lights.h, its first 0x1C bytes; vtable 0x006491DC): a brightness moving towards a target, from which a scale
// for the ambient colour and an intensity for each light follow (the light manager's ranges, 0x0067DCA4 - 0x0067DCB0:
// one below and one above the middle brightness 0.5), and the position the lights are for. The base's slots [1] -
// [12] are stubs ("STUB: CModelLights::...") that CPcModelLights replaces.
namespace Engine3D {

class CModelLights {
public:
    // 0x0048E5B0: construct - brightness 0, scale 1, an intensity of 1 for each of the manager's lights (0x006FAAE0).
    // Returns this.
    CModelLights* MakeNew();
    // 0x0048E630: the destructor's body - the intensities freed.
    void DestroyBody();
    // 0x0048E7F0 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    CModelLights* Teardown(unsigned int flags);

    // 0x0048E450: the ambient colour set (virtual [1]), then UpdateBrightness(1, 0) (virtual [8]). [1]'s result.
    bool ChangeAmbientTint(const uint32_t* color);
    // 0x0048E480: a light's colour set (virtual [3]), then UpdateBrightness(0, 1). [3]'s result.
    bool ChangeDiffuseHue(int light, const uint32_t* color);
    // 0x0048E4D0: the position the lights are for. True.
    bool BindPositionSource(const Maths::Vec4* position);
    // 0x0048E660: the scale and the intensities from the brightness (0 to 1: 2b - 1 times the dark range below 0.5 and
    // at it, else the bright one, plus 1; nothing below 0), then UpdateBrightness(1, 1).
    void UpdateBrightness();
    // 0x0048E830: the brightness moved `step` towards `target` (not past it); the target kept. True.
    bool MoveBrightness(float target, float step);
    // 0x0048E8A0: the brightness (and the target) set. True.
    bool ChangeBrightness(float brightness);

    // The base's platform calls (each prints its "STUB: CModelLights::...\n"):
    bool PutAmbientLightTint(const uint32_t* color);                   // 0x0048E4E0 [1]: false
    bool PutLightAim(int light, const Maths::Vec4* direction);    // 0x0048E500 [2]: false
    bool PutDiffuseLightTint(int light, const uint32_t* color);        // 0x0048E520 [3]: false
    uint32_t* ReadAmbientLightTint(uint32_t* out);                      // 0x0048E730 [4]: 0
    Maths::Vec4* ReadLightAim(int light);                          // 0x0048E760 [5]: a static 0 vector
    uint32_t* ReadDiffuseLightTint(uint32_t* out, int light);           // 0x0048E7C0 [6]: 0
    void PlatApplySceneLights(const Maths::Vec4* position, uint32_t radius); // 0x0048E5A0 [7]
    void ApplyLightBrightness(bool ambient, bool diffuse);                  // 0x0048E540 [8]
    void SwitchAmbientLight(bool on);                                   // 0x0048E550 [9]
    void SwitchDiffuseLight(int light, bool on);                        // 0x0048E560 [10]
    bool AmbientLightIsOn();                                       // 0x0048E570 [11]: false
    bool DiffuseLightIsOn(int light);                              // 0x0048E580 [12]: false (not a Ghidra function)

    void** m_vtable;
    uint32_t m_unknown4;
    float m_brightness;            // +0x08
    float m_intensityGoal;      // +0x0C
    float m_sizeFactor;                 // +0x10: the ambient colour's
    float* m_intensities;         // +0x14: each light's
    const Maths::Vec4* m_placeRef; // +0x18
};
static_assert(offsetof(CModelLights, m_intensities) == 0x14 && sizeof(CModelLights) == 0x1C,
              "layout must match the original");

extern uint32_t g_lightCount;

} // namespace Nx
