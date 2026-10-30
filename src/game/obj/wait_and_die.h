#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/object.h"

namespace Script {
class CStruct;
} // namespace Script

// The WaitAndDie component (Obj::CWaitAndDieComponent, type 0x03AC8065 "WaitAndDie", vtable 0x00646898, 0x28 bytes;
// made by 0x0043D930): its object dies (MarkAsDead) when its LifeTime is up or, given an object to wait for, as soon
// as that object is gone.
//
// Vtable: 0 Destroy, 1 Update, 2 InitFromStructure; the empty 0x005480A0 for Finalize, 0x004E3F90, 0x0048DCD0, the
// base's Suspend, 11 0x0045D720, 12 CallMemberFunction 0x0051ADC0 (not mine). (Create, 0x0043D930, is outside this
// file's range: see component_types.cpp.)
namespace Obj {

class CWaitAndDieComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0x03AC8065;

    // 0x0043D840: the destructor body (the base's).
    void DestroyBody();
    // 0x0043D850 (vtable slot 0)
    CWaitAndDieComponent* Destroy(unsigned int flags);
    // 0x0043D880 (vtable slot 2): the object to wait for (0x1869F559, a checksum; none: 0) - without one, the
    // LifeTime (0xC218CF77, seconds, asserted) in ms, truncated; with one the lifetime is 0. Started now.
    void InitFromStructure(Script::CStruct* params);
    // 0x0043D8E0 (vtable slot 1): the time since the start kept; the object dies (vtable slot 6, MarkAsDead) when the
    // object it waits for can't be found, or - waiting for none - once the time is past the lifetime.
    void Update();

    int m_start;         // +0x18 the game clock (with its offset) at InitFromStructure
    uint32_t m_elapsed;  // +0x1C ms since then (each update)
    uint32_t m_lifetime; // +0x20 ms
    uint32_t m_waitFor;  // +0x24 the id of the object to wait for (0: none)
};
static_assert(offsetof(CWaitAndDieComponent, m_start) == 0x18 && offsetof(CWaitAndDieComponent, m_waitFor) == 0x24 &&
                  sizeof(CWaitAndDieComponent) == 0x28,
              "layout must match the original");

// 0x00646898: waitAndDieVtable (defined where the class's functions are).
extern void** const g_waitAndDieVtable;

} // namespace Obj
