#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/vector.h"
#include "hook/game_global.h"

// Lighting on PC (Nx engine): the scene's lights (the engine's ambient colour 0x0067DC98, its scale 0x0067DC94, the
// directional lights' directions 0x0067DC9C and colours 0x0067DCA0, their intensities 0x006FAAEC) and the PC model
// lights - a model's own ambient / light colours and directions where it overrides the scene's, plus the nearby
// dynamic lights - all sent to the renderer's light constants: the ambient colour (0x00786F38) and 0x20 bytes per
// light (0x00786F48: a direction, then a colour at +0x10), colours as bytes times the scale times 1/128.
namespace Nx {

// The PC model lights (0x30 bytes, vtable 0x0064A440; the engine's model lights - constructor 0x0048E5B0, vtable
// 0x006491DC, destructor body 0x0048E630 - for its first 0x1C bytes: +0x10 a scale, +0x14 each light's intensity).
struct CPcModelLights {
    void** vtable;
    uint8_t unknown4[0xC];
    float scale;             // +0x10
    float* intensities;      // +0x14: one per light (0x006FAAE0 of them)
    uint32_t unknown18;
    uint32_t overrides;      // +0x1C: bit 0 the ambient colour, bit 1 + n light n's
    uint32_t firstDynamic;   // +0x20: the renderer light the dynamic lights start at (2)
    uint32_t ambient;        // +0x24: RGBA
    uint32_t* colors;        // +0x28: one per light (0x006FAAE4 of them), RGBA
    Mth::Vector* directions; // +0x2C

    // 0x004B6270: construct - nothing overridden, every direction (0, 1, 0). Returns this.
    CPcModelLights* Construct();
    // 0x004B6360: the destructor's body.
    void DestroyBody();
    // 0x004B67B0 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    CPcModelLights* Destroy(unsigned int flags);
    // 0x004B6160 / 0x004B6170 (virtual [1] / [4]): the ambient colour. Set returns true; Get returns out.
    bool SetAmbientColor(const uint32_t* color);
    uint32_t* GetAmbientColor(uint32_t* out);
    // 0x004B6780 / 0x004B6180 (virtual [2] / [5]): a light's direction (the scene's unless overridden). Set
    // returns true.
    bool SetLightDirection(int light, const Mth::Vector* direction);
    const Mth::Vector* GetLightDirection(int light);
    // 0x004B61B0 / 0x004B61D0 (virtual [3] / [6]): a light's colour. Set returns true; Get returns out.
    bool SetLightDiffuseColor(int light, const uint32_t* color);
    uint32_t* GetLightDiffuseColor(uint32_t* out, int light);
    // 0x004B63B0 (virtual [7]): the renderer's light constants for something at `position`: the ambient colour,
    // each light (overridden or the scene's; directions negated), then - with `radius` - the dynamic lights near it
    // (0x0048EF90; their directions from them to the position, type 1 coloured by 1 - their falloff), or the first
    // dynamic light's colour cleared when there are none.
    void Update(const Mth::Vector* position, uint32_t radius);
    // 0x004B61F0 / 0x004B6240 (virtual [9] / [11]): whether the ambient colour is overridden.
    void EnableAmbientOverride(bool on);
    bool IsAmbientOverridden();
    // 0x004B6210 / 0x004B6250 (virtual [10] / [12]): whether a light is overridden.
    void EnableLightOverride(int light, bool on);
    bool IsLightOverridden(int light);
};
static_assert(offsetof(CPcModelLights, overrides) == 0x1C && offsetof(CPcModelLights, directions) == 0x2C &&
                  sizeof(CPcModelLights) == 0x30,
              "layout must match the original");

// 0x004B67E0: the scene's ambient colour sent to the renderer. True.
bool __cdecl UpdateAmbientLight();
// 0x004B6850: the scene's ambient colour into *out. Returns out.
uint32_t* __cdecl GetAmbientLightColor(uint32_t* out);
// 0x004B6860: a scene light's colour (times its intensity) sent to the renderer. True.
bool __cdecl UpdateLightColor(int light);
// 0x004B68E0: a scene light's colour into *out. Returns out.
uint32_t* __cdecl GetLightColor(uint32_t* out, int light);
// 0x004B6900: the ambient colour and the colours of lights 0-30 sent.
void __cdecl UpdateAllLights();
// 0x004B6920: model lights deleted (virtually; none: nothing). True.
bool __cdecl DeleteModelLights(CPcModelLights* lights);
// 0x004B6940: the renderer's lights back to the scene's - ambient, lights 0 and 1 - and light 2's colour cleared.
void __cdecl ResetLights();
// 0x004B6A70: a scene light's direction (negated) sent to the renderer. True.
bool __cdecl UpdateLightDirection(int light);
// 0x004B6AB0: a scene light's direction (w 1; a static vector, 0x0070166C).
Mth::Vector* __cdecl GetLightDirection(int light);
// 0x004B6B10: new model lights (0x30 bytes).
CPcModelLights* __cdecl CreateModelLights();

GAME_GLOBAL_EXTERN(float[28], g_lightConstants);

} // namespace Nx
