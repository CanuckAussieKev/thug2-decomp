#include <cstdint>

#include "game/obj/motion.h"

#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/object.h"
#include "game/script/struct.h"

namespace Obj {

namespace {

constexpr uint32_t kRelative = 0x91A4C826; // relative
constexpr uint32_t kAbsolute = 0x592089A9; // absolute
constexpr uint32_t kConvert = 0x26AF9DC9;  // FLAG_MAX_COORDS: the angles converted (z negated, NegateXRotateX90)

// The angles given as `name`, in radians (false if not given).
bool GetAngles(Script::CStruct* params, uint32_t name, Mth::Vector* angles)
{
    *angles = {0.0f, 0.0f, 0.0f, 1.0f};
    if (!params->GetVector(name, reinterpret_cast<Script::VectorValue*>(angles), 0))
        return false;
    if (params->ContainsFlag(kConvert)) {
        angles->z = -angles->z;
        angles->NegateXRotateX90();
    }
    angles->DegreesToRadians();
    return true;
}

} // namespace

// 0x00425600
void CMotionComponent::RotateTo(Script::CStruct* params)
{
    Mth::Vector angles;
    Mth::Matrix target;
    if (GetAngles(params, kRelative, &angles)) {
        target = m_object->m_matrix;
        target.RotateLocal(angles);
        StartRotation(params, &target);
        return;
    }
    if (GetAngles(params, kAbsolute, &angles)) {
        target.SetFromEulerAngles(angles.x, angles.y, angles.z);
        StartRotation(params, &target);
    }
}

} // namespace Obj
