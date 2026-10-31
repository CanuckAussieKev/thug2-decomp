#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/objects/entity.h"

namespace Scripts {
class CStruct;
} // namespace Script

// The Velocity component (GameObjects::CVelocityTrackerModule, type 0x41272956 "Velocity", vtable 0x00646830, 0x1C bytes; made by
// 0x0043D430): moves its object by its velocity each frame - the object's position plus half its velocity times the
// frame's length (as the original does it: half). Not while the object's Suspend component says so (outside a
// network game).
//
// Vtable: 0 Destroy, 1 Update, 2 ConfigureFromData, 3 Finalize 0x0041C330 (shared: the object's Suspend component
// into +0x18), the empty 0x005480A0 / 0x005BAB90, the base's Suspend, 10 0x005144C0 (empty), 11 0x0045D720, 12
// InvokeMethodByName 0x0051ADC0 ("not mine").
namespace GameObjects {

class CVelocityTrackerModule : public CObjectModule {
public:
    static constexpr uint32_t kType = 0x41272956;

    // 0x0043D430 (registered with the composite object manager): a new one (0 if out of memory).
    static CObjectModule* __cdecl Fabricate();
    // 0x0043D2B0: the destructor body (the base's).
    void DestroyBody();
    // 0x0043D2C0 (vtable slot 0)
    CVelocityTrackerModule* Teardown(unsigned int flags);
    // 0x0043D2F0 (vtable slot 2): the object's velocity from `vel` (0x0C4C809E, asserted).
    void ConfigureFromData(Scripts::CStruct* params);
    // 0x0043D340 (vtable slot 1): the object's position (w too) plus half its velocity times the frame's length -
    // unless, outside a network game, the Suspend component's flag (0x00433E90) is set.
    void TickFrame();

    CObjectModule* m_suspend; // +0x18 the object's Suspend component (0xCE0CA665), from Finalize
};
static_assert(offsetof(CVelocityTrackerModule, m_suspend) == 0x18 && sizeof(CVelocityTrackerModule) == 0x1C,
              "layout must match the original");

} // namespace Obj
