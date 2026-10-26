#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/vector.h"
#include "game/obj/object.h"

namespace Nx {
class CFeeler;
}

namespace Obj {

struct CRideTracker;

// The movable contact component (type 0x408D2AA9 "MovableContact", vtable 0x00646250, 0x74 bytes; made by 0x00427AB0):
// what a skater stands or rides on when it's a moving object - the ride tracker that carries it along with the
// object, where on the object it is, and the last object ridden. The skater's core physics keeps it (+0x152C).
//
// Vtable: 0 Destroy, 1 0x0051B970 (shared), 2 / 8 / 10 the empty 0x005BAB90, 3 / 4 / 9 the empty 0x005480A0, 5
// 0x004E3F90, 6 0x0048DCD0, 7 the base's Suspend, 11 the base's 0x0045D720, 12 0x0051ADC0 (shared).
class CMovableContactComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0x408D2AA9;

    // 0x00427AB0 (registered with the component types): a new one (Mem::Calloc; the constructor inlined: the base's,
    // its vtable and type, no ride, no stop time).
    static CBaseComponent* __cdecl Create();
    // 0x004276C0: the destructor body (its vtable, then the base's - the ride tracker isn't freed).
    void DestroyBody();
    // 0x004276D0 (vtable slot 0): the scalar deleting destructor.
    CMovableContactComponent* Destroy(unsigned int flags);

    // 0x00427700 (thiscall, RET 8; `this` unused): the moving object under `point` - a feeler from 400 over the point
    // down to 6 over it hits a movable object that's still there, and nothing is in the way back up from the point
    // to just over the hit (else the feeler starts again just under that hit; 9 tries). The hit point into `hit`;
    // null when there's none.
    CCompositeObject* FindContact(const Mth::Vector* point, Mth::Vector* hit);
    // 0x00427B30: the object under `pos` (null: none). Nothing under `next`: `pos` = `next`. Else, nothing two
    // frames of the object's velocity ahead of `pos`: `pos` = that point; else `pos` = the hit under it, a unit
    // up.
    CCompositeObject* FindContactAhead(Mth::Vector* pos, const Mth::Vector* next);
    // 0x00427A00: the contact point in the world (relative to the last object ridden, by its orientation and
    // position; none: as kept); returns `out` (callers read it).
    Mth::Vector* GetContactPoint(Mth::Vector* out);
    // 0x00427C70: no longer riding: the ride's object becomes the last ridden, the tracker freed, the time noted.
    void StopRiding();
    // 0x00427CB0: whether riding an object that's still there (one that's gone: stops riding).
    bool IsRiding();
    // 0x00427CD0: riding what `feeler` hit - false (no longer riding) when that's gone; false when already riding
    // it; else a new ride tracker on it (from the manager: none made still counts) and true.
    bool StartRiding(Nx::CFeeler* feeler);
    // 0x00427DD0: the contact point kept relative to the object ridden (or the last one ridden): its own position
    // less the object's, through the object's inverted orientation.
    void UpdateContactPoint();

    CRideTracker* mp_ride;            // +0x18
    Mth::Vector m_contactPoint;       // +0x1C in the object's frame
    uint8_t m_unknown2C[0x40];        // +0x2C
    CCompositeObject* mp_lastObject;  // +0x6C the object last ridden
    int m_stopTime;                   // +0x70 the game clock when it stopped riding
};
static_assert(offsetof(CMovableContactComponent, mp_ride) == 0x18 &&
                  offsetof(CMovableContactComponent, m_contactPoint) == 0x1C &&
                  offsetof(CMovableContactComponent, mp_lastObject) == 0x6C &&
                  offsetof(CMovableContactComponent, m_stopTime) == 0x70 && sizeof(CMovableContactComponent) == 0x74,
              "layout must match the original (0x00427AB0 allocates 0x74 bytes)");

} // namespace Obj
