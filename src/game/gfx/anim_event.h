#pragma once

// An animation's event (0x14 bytes; base vtable 0x006487B0: 0 the scalar deleting destructor 0x004819F0, 1
// 0x004860C0, 2 pure): a blend timeline (Gfx::CBlendTimeline) keeps an array of them, each fired once while armed.

#include <cstddef>
#include <cstdint>

namespace Gfx {

struct CAnimEvent {
    // 0x004819E0: the base's vtable back (its destructor body).
    void BaseDestroyBody();

    void** m_vtable;
    uint32_t m_unknown04;
    bool m_armed;               // +0x08 (0x00486150 sets it)
    uint8_t m_pad09[3];
    uint32_t m_unknown0C[2];
};
static_assert(offsetof(CAnimEvent, m_armed) == 8 && sizeof(CAnimEvent) == 0x14, "layout must match the original");

} // namespace Gfx
