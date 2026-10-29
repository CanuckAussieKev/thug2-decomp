#pragma once

#include <cstddef>
#include <cstdint>

namespace Mth {
class Vector;
}

// The engine's light manager (Nx::CLightManager's statics, cdecl): the world lights (0x0067DC98: an ambient colour,
// the directional lights' directions and colours, and the ambient / per-light modulation ranges used below and above
// the middle brightness), a stack of 4 saved copies of them (0x006FAF10), the world brightness moving towards a target
// (0x0067DC8C / 0x0067DC90) and the intensities it gives (the ambient's 0x0067DC94, each light's 0x006FAAEC, never below
// 0x006FAAE8), and the dynamic lights (256 slots at 0x006FAAF0, 0x006FAEF0 in use) - what's near a point found for the
// renderer's light constants (PC: pc_lights.h).
namespace Nx {

struct CDynamicLight;
struct CDynamicLightBase;

// The world lights (0x1C bytes: 0x0067DC98, and each of the stack's entries).
struct sWorldLights {
    uint32_t ambient;      // +0x00: RGBA
    Mth::Vector* directions; // +0x04: one per light
    uint32_t* colours;     // +0x08: one per light
    float ambientLow;      // +0x0C: the ambient's range below the middle brightness
    float ambientHigh;     // +0x10: ... above it
    float* lightLow;       // +0x14: each light's
    float* lightHigh;      // +0x18

    // 0x0048EAC0 (not a Ghidra function): grey ambient (0x48, 0x48, 0x48, 0x80), no arrays, a low range of 0.5.
    // Returns this.
    sWorldLights* Construct();
    // 0x0048EB00: the four arrays freed.
    void FreeArrays();
};
static_assert(offsetof(sWorldLights, lightLow) == 0x14 && sizeof(sWorldLights) == 0x1C, "layout must match the original");

// 0x0048EB70: the manager set up for `maxLights` world lights (their arrays - the stack's zeroed - and the
// intensities, all 1) and `maxModelLights` model lights.
void __cdecl InitLightManager(uint32_t maxLights, uint32_t maxModelLights);
// 0x0048FB60: "Uninitializing Light Manager...", the dynamic lights deleted.
void __cdecl UninitLightManager();
// 0x0048E8F0: the ambient colour set and sent (0x004B67E0). That one's result.
bool __cdecl SetLightAmbientColor(uint32_t rgba);
// 0x0048E900: the ambient colour into *out (0x004B6850). Returns out.
uint32_t* __cdecl GetLightAmbientColor(uint32_t* out);
// 0x0048E930: a light's colour set and sent (0x004B6860). That one's result.
bool __cdecl SetLightDiffuseColor(int light, uint32_t rgba);
// 0x0048E950: a light's colour into *out (0x004B68E0). Returns out.
uint32_t* __cdecl GetLightDiffuseColor(uint32_t* out, int light);
// 0x0048ED70: a light's direction set and sent (0x004B6A70). That one's result.
bool __cdecl SetLightDirection(int light, const Mth::Vector* direction);
// 0x0048E970 / 0x0048E990: the ambient's / a light's modulation ranges.
void __cdecl SetAmbientLightModulationFactor(float low, float high);
void __cdecl SetDiffuseLightModulationFactor(int light, float low, float high);
// 0x0048E9B0 / 0x0048E9D0: read back (`low` or not; a light's always from the low ranges, as the original).
float __cdecl GetAmbientLightModulationFactor(bool low);
float __cdecl GetDiffuseLightModulationFactor(int light, bool low);
// 0x0048EA00: the world lights saved on the stack (nothing when it's full).
void __cdecl PushWorldLights();
// 0x0048EA40: the world lights taken back from the stack; false when it's empty.
bool __cdecl PopWorldLights();
// 0x0048EDB0: the world brightness moved 0.02 towards `target` (not past it), the intensities from it (as
// CModelLights::UpdateBrightness, never below the minimum), all sent (0x004B6900). True.
bool __cdecl SetWorldBrightness(float target);

// 0x0048EF00: every dynamic light freed, none in use.
void __cdecl DeleteDynamicLights();
// 0x0048EF50: the slot of the dynamic light of an owner (null if none).
CDynamicLightBase** __cdecl FindDynamicLight(uint32_t owner);
// 0x0048EF90 (not a Ghidra function): the dynamic lights on and owned whose radius reaches `position`, into `out` by
// their falloff (virtual [2] of the distance) - at most `max`, kept in 0x0067DD38 / 0x0067DCB8 with their indexes;
// the original's insertion quirks kept (see the source). How many.
uint32_t __cdecl GetDynamicLightsNear(const Mth::Vector* position, uint32_t max, CDynamicLightBase** out);
// 0x0048F160: a new dynamic light of a type (1: a point light, zeroed, radius 50, not owned; others: none).
CDynamicLightBase* __cdecl CreateDynamicLight(int type);
// 0x0048FB80: a dynamic light: the first free slot's (made), else a let-go one (owner 0) reused, else a new slot's
// (none past 256 or when that slot isn't empty).
CDynamicLightBase* __cdecl NewDynamicLight(int type);

// A dynamic light's base (0x30 bytes, vtable 0x00649290; nx/dynamic_light.h's CDynamicLight is its layout): [0] on,
// [1] in reach, [2] the falloff.
struct CDynamicLightBase {
    void** vtable;
    float pos[4];          // +0x04
    int type;              // +0x14
    float radius;          // +0x18
    float inverseRadius;   // +0x1C
    float intensity;       // +0x20
    uint32_t colour;       // +0x24
    uint32_t owner;        // +0x28
    bool unknown2C;

    // 0x0048E8C0 (virtual [0]): its intensity is above 0 (1 / 0).
    int IsOn();
    // 0x0048EA90 (virtual [1]): a distance is within its radius (1 / 0; NaN: 0).
    int InReach(float distance);
    // 0x0048EAB0 (virtual [2]): the distance times the inverse radius (in ST0 unrounded: a double here).
    double Falloff(float distance);
};
static_assert(offsetof(CDynamicLightBase, owner) == 0x28 && sizeof(CDynamicLightBase) == 0x30,
              "layout must match the original");

} // namespace Nx
