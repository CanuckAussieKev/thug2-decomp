#pragma once

#include <cstdint>

#include "game/mth/vector.h"

namespace Obj {
class CBaseComponent;
class CCompositeObject;
} // namespace Obj

// The walk component's hanging, ladder and rail-climbing code (WalkHangUtil.cpp in the original: 0x0043DA20-
// 0x004425B0): its member functions are declared with the walk component (Obj::CWalkComponent, game/skater/
// walk_physics.h - the "walk_hang.cpp" block); walk_hang.cpp names the fields they use. Here: the helpers on other
// classes that live in the same code.
namespace Walk {

// A composite object's movable contact component (the walk component's +0x1784, the skater core's +0x152C): what the
// skater rides on.
class MovableContact {
public:
    // 0x0043EB80: riding `object` from now: an old ride let go (its object kept at +0x6C, the time at +0x70), a new
    // ride tracker (Obj::CRideTracker) made for it.
    void StartRiding(Obj::CCompositeObject* object);
};

// 0x0043D990 (cdecl): out = a + (b - a) * t, each component rounded as the original's x87 code does. Returns out.
Mth::Vector* __cdecl Lerp(Mth::Vector* out, const Mth::Vector* a, const Mth::Vector* b, float t);
// 0x0043DA20 (thiscall on the skater core physics component, or the walk component's control at +0x1770's +0x20):
// whether its +0x228 angle is within 45 degrees of a half turn, with nothing at +0x234 and +0x22C not 0. Returns
// 1 / 0.
struct SkaterTurn {
    int TurnedRound() const;
};

} // namespace Walk

namespace Obj {
// 0x0043D930: a component of type 0x03AC8065 (WaitAndDie): the base component made (0x28 bytes, cleared), its vtable
// 0x00646898 and its type. (Its other code, 0x0043D850-0x0043D8E0, isn't rewritten here.)
CBaseComponent* __cdecl CreateWaitAndDieComponent();
} // namespace Obj
