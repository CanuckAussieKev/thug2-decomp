#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/object.h"

namespace Nx {
struct CDynamicLight;
} // namespace Nx
namespace Script {
class CScript;
class CStruct;
} // namespace Script

namespace Obj {

// The dynamic light component (type 0xD8BFC9F7 - its name isn't in THUG2's scripts -, vtable 0x00645F58, 0x48 bytes;
// made by 0x0041D0F0): a dynamic light at its object's position that changes over the object's life (its wait-and-die
// component's elapsed / total time): radius, intensity and colour from their start values to their end values -
// through a middle value at a fraction of the life when one is given.
//
// Vtable: 0 the scalar deleting destructor, 1 Update, 2 InitFromStructure, 3 Finalize, 12 CallMemberFunction; the rest
// the base's / shared (4 0x005480A0, 5 0x004E3F90, 6 0x0048DCD0, 7 0x0045D760, 8 / 10 0x005BAB90, 9 0x005480A0, 11
// 0x0045D720).
class CDynamicLightComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0xD8BFC9F7;

    // 0x0041D0F0 (registered with the composite object manager by 0x004E4860): a new one (the constructor inlined: the
    // base's, its vtable and type, no light; 0 if out of memory).
    static CBaseComponent* __cdecl Create();
    // 0x0041CBD0: the light let go (its owner id cleared) and forgotten, the wait-and-die component forgotten; then
    // the base's body.
    void DestroyBody();
    // 0x0041D0C0 (vtable slot 0)
    CDynamicLightComponent* Destroy(unsigned int flags);
    // 0x0041CBF0 (vtable slot 2): the start values - StartRadius, StartIntensity, StartRGBA (an array of four bytes;
    // all asserted) - are also the middle and end values unless given: midTime (the middle's fraction of the life; -1
    // none), MidRadius, MidIntensity, MidRGBA; EndRadius, EndIntensity, EndRGBA.
    void InitFromStructure(Script::CStruct* params);
    // 0x0041CDA0 (vtable slot 3): the object's wait-and-die component (0x03AC8065).
    void Finalize();
    // 0x0041CDC0 (vtable slot 1): the light made the first time (0x0048FB80(1), owned by the object); then placed at
    // the object and its radius, intensity and colour (each byte truncated) interpolated by the life's fraction (from
    // the start to the end, or to / from the middle).
    void Update();
    // 0x0041CFC0 (vtable slot 12): 0xFAAD1C11 the light's radius (radius=), 0x86E1FA56 its intensity (intensity=),
    // 0x1A0F4C50 its colour (rgba=, four bytes; read whether given or not) - each asserted; 1. Anything else: 2.
    int CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);

    Nx::CDynamicLight* mp_light; // +0x18
    uint32_t m_startColor;       // +0x1C
    uint32_t m_middleColor;      // +0x20
    uint32_t m_endColor;         // +0x24
    float m_startRadius;         // +0x28
    float m_middleRadius;        // +0x2C
    float m_endRadius;           // +0x30
    float m_startIntensity;      // +0x34
    float m_middleIntensity;     // +0x38
    float m_endIntensity;        // +0x3C
    float m_middleTime;          // +0x40 the middle's fraction of the life (-1: none)
    uint8_t* mp_waitAndDie;      // +0x44 its +0x1C elapsed, +0x20 total (unsigned)
};
static_assert(offsetof(CDynamicLightComponent, mp_light) == 0x18 &&
                  offsetof(CDynamicLightComponent, m_startRadius) == 0x28 &&
                  offsetof(CDynamicLightComponent, m_middleTime) == 0x40 &&
                  offsetof(CDynamicLightComponent, mp_waitAndDie) == 0x44 && sizeof(CDynamicLightComponent) == 0x48,
              "layout must match the original");

} // namespace Obj
