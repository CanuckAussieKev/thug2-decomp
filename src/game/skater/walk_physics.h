#pragma once

#include <cstdint>

#include "game/mth/vector.h"
#include "game/obj/object.h"

// The Walk component (type 0x726E85AA; made by 0x0052A5C0, its member function table 0x006847F8 registered by
// 0x00525E40): the skater on foot - walking, running, jumping, hanging, climbing. Only the pieces at
// 0x00524E10-0x00529F62 are here (walk_physics.cpp); its layout is only partly known, so fields are reached by
// offset (the names below say what each is).
namespace Obj {

class CWalkComponent : public CBaseComponent {
public:
    // Its states (+0x18): 0 on the ground, 1 in the air, 2 / 3 / 4 the hanging and climbing ones.
    // 0x00524E10: the d-pad steers it (the left stick centred - the input's +0x228 / +0x22C exactly 0 - and the
    // flag +0x98 set).
    bool UsingDpad();
    // 0x00524E60: the camera's offset: its facing (+0x48) x -18 in states 2-4, and in the air for 150 ms after
    // leaving states 2 / 3; else (0, 0, 0, 1). Returns `out`.
    Mth::Vector* GetCameraOffset(Mth::Vector* out);
};

} // namespace Obj
