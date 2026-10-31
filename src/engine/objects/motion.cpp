#include <cstdint>

#include "engine/objects/motion.h"

#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"
#include "engine/script/param_struct.h"

namespace GameObjects {

namespace {

constexpr uint32_t kRelative = 0x91A4C826; // relative
constexpr uint32_t kAbsolute = 0x592089A9; // absolute
constexpr uint32_t kConvert = 0x26AF9DC9;  // FLAG_MAX_COORDS: the angles converted (z negated, NegateXRotateX90)

// The angles given as `name`, in radians (false if not given).
bool ReadTurnAngles(Scripts::CStruct* params, uint32_t name, Maths::Vec4* angles)
{
    *angles = {0.0f, 0.0f, 0.0f, 1.0f};
    if (!params->FetchVec(name, reinterpret_cast<Scripts::VectorValue*>(angles), 0))
        return false;
    if (params->HasBareFlag(kConvert)) {
        angles->z = -angles->z;
        angles->NegateXRotateX90();
    }
    angles->DegreeToRadianConv();
    return true;
}

} // namespace

// 0x00425600
void CKinematicsModule::RotateTo(Scripts::CStruct* params)
{
    Maths::Vec4 angles;
    Maths::Mat4 target;
    if (ReadTurnAngles(params, kRelative, &angles)) {
        target = m_entity->m_basisFrame;
        target.TurnInOwnFrame(angles);
        StartRotation(params, &target);
        return;
    }
    if (ReadTurnAngles(params, kAbsolute, &angles)) {
        target.SetFromEulerAngles(angles.x, angles.y, angles.z);
        StartRotation(params, &target);
    }
}

} // namespace Obj
