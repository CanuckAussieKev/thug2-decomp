#pragma once

#include <cstddef>
#include <cstdint>

namespace Script {
class CStruct;
}
namespace Mth {
class Matrix;
}

namespace Obj {

class CCompositeObject;

// The motion component (an object's movement and rotation over time; the rest of it has no source yet).
class CMotionComponent {
public:
    // 0x00425600 (thiscall; called by its member function for Obj_Rotate, 0x00426770): turn the object to an
    // orientation over time - `relative = (x, y, z)`: the object's orientation now turned by those angles about its
    // own axes (Mth::Matrix::RotateLocal); `absolute = (x, y, z)`: the orientation of those angles
    // (Mth::Matrix::SetFromEulerAngles). Degrees; with the flag 0x26AF9DC9 the vector is first converted (z negated,
    // then NegateXRotateX90). Neither given: nothing. The turn is started by 0x00425520 (`time` from the params).
    void RotateTo(Script::CStruct* params);

    void** m_vtable;
    uint32_t m_base04[2];         // +0x04..+0x08: the base component's
    CCompositeObject* mp_object; // +0x0C
};
static_assert(offsetof(CMotionComponent, mp_object) == 0x0C);

} // namespace Obj
