#include "game/obj/motion.h"

#include "game/front/menu_level.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/object.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

constexpr uint32_t kRelative = 0x91A4C826; // relative
constexpr uint32_t kAbsolute = 0x592089A9; // absolute
constexpr uint32_t kConvert = 0x26AF9DC9;  // (name unknown): the angles converted (z negated, NegateXRotateX90)

// 0x00425520 (thiscall, RET 8; not rewritten yet): start turning the object to `target` (its time from `params`).
void StartRotation(CMotionComponent* motion, Script::CStruct* params, const Mth::Matrix* target)
{
    GameFunction<void(__thiscall*)(CMotionComponent*, Script::CStruct*, const Mth::Matrix*)>(0x00425520)(motion, params,
                                                                                                         target);
}

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
        target = mp_object->m_matrix;
        target.RotateLocal(angles);
        StartRotation(this, params, &target);
        return;
    }
    if (GetAngles(params, kAbsolute, &angles)) {
        target.SetFromEulerAngles(angles.x, angles.y, angles.z);
        // Not original - the menu_level feature (game/front/menu_level.h): in the front end built in Los Angeles, an
        // orientation given in the skate shop's terms is turned with the shop's move into LA (the wheel spins in its
        // own plane instead of turning back to the shop's heading).
        // Original behaviour: the orientation of the angles as given.
        Front::MenuLevel::TurnOrientation(&target);
        StartRotation(this, params, &target);
    }
}

} // namespace Obj

REPLACE(0x00425600, &Obj::CMotionComponent::RotateTo);
