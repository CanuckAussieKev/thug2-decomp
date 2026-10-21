#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/object.h"

namespace Script {
class CStruct;
} // namespace Script

namespace Obj {

class CModelComponent;

// CModelLightUpdateComponent::m_lightFlags (+0x18)
constexpr uint32_t MODEL_LIGHT_ONCE = 0x1;    // (flag 0xE7375C4C) sampled once, then the component suspends itself
constexpr uint32_t MODEL_LIGHT_SAMPLED = 0x2; // the first sample taken

// The model light update component (type 0xE3FCA939 "modellightupdate", vtable 0x00646198, 0x2C bytes; made by
// 0x00422DC0): an object's model lit by the ground under it - the brightness of the collision face below (two feelers:
// down from just above the object, nudged a thousandth along its up vector, then again from the hit) given to the model
// component (ApplyBrightness), every `interval` frames (staggered by the component's address), its model's lights lerped
// towards it in between.
//
// Vtable: 0 the scalar deleting destructor, 1 Update, 2 InitFromStructure, 3 Finalize; the rest the base's / shared.
class CModelLightUpdateComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0xE3FCA939;

    // 0x00422DC0 (registered with the composite object manager): a new one (the constructor inlined: the base's, its
    // vtable, no flags, interval or brightness, its type; 0 if out of memory).
    static CBaseComponent* __cdecl Create();
    // 0x00422D00: its vtable back, then the base's body.
    void DestroyBody();
    // 0x00422D10 (vtable slot 0)
    CModelLightUpdateComponent* Destroy(unsigned int flags);
    // 0x00422D40 (vtable slot 2): MODEL_LIGHT_ONCE from flag 0xE7375C4C; the interval from 0x470B8AE9 (default 0: every
    // frame), the counter starting at (this component's address % interval) + 1 - spreading objects' samples over the
    // frames (an interval of 0 given: a division by zero, as the original).
    void InitFromStructure(Script::CStruct* params);
    // 0x00422DA0 (vtable slot 3): its object's model component.
    void Finalize();
    // 0x004230B0 (vtable slot 1): the first time, a sample (set at once); then, until the counter passes the
    // interval, the lights lerped towards the brightness (by 0.02); once it does, back to 0 and a sample (lerped) -
    // or, MODEL_LIGHT_ONCE, the component suspended.
    void Update();
    // 0x00422E40: the brightness of the face under the object given to the model component (lerped by `rate` with
    // `lerp`) and kept; nothing hit: nothing.
    void Sample(bool lerp, float rate);

    uint32_t m_lightFlags;      // +0x18 MODEL_LIGHT_*
    uint32_t m_interval;        // +0x1C 0x470B8AE9 (frames)
    uint32_t m_counter;         // +0x20
    float m_brightness;         // +0x24 the last sample's
    CModelComponent* mp_model;  // +0x28
};
static_assert(offsetof(CModelLightUpdateComponent, m_lightFlags) == 0x18 &&
                  offsetof(CModelLightUpdateComponent, m_brightness) == 0x24 &&
                  offsetof(CModelLightUpdateComponent, mp_model) == 0x28 && sizeof(CModelLightUpdateComponent) == 0x2C,
              "layout must match the original (0x00422DC0 allocates 0x2C bytes)");

} // namespace Obj
