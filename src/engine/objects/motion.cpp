#include <cstdint>

#include "engine/objects/motion.h"

#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"
#include "engine/script/param_struct.h"

namespace objects {

namespace {

constexpr uint32_t kRelative = 0x91A4C826; // relative
constexpr uint32_t kAbsolute = 0x592089A9; // absolute
constexpr uint32_t kConvert = 0x26AF9DC9;  // FLAG_MAX_COORDS: the angles converted (z negated, negate_x_rotate_x90)

// The angles given as `name`, in radians (false if not given).
bool read_turn_angles(script::Struct* params, uint32_t name, maths::Vec4* angles)
{
    *angles = {0.0f, 0.0f, 0.0f, 1.0f};
    if (!params->fetch_vec(name, reinterpret_cast<script::VectorValue*>(angles), 0))
        return false;
    if (params->has_bare_flag(kConvert)) {
        angles->z = -angles->z;
        angles->negate_x_rotate_x90();
    }
    angles->degree_to_radian_conv();
    return true;
}

} // namespace

// 0x00425600
void KinematicsModule::rotate_to(script::Struct* params)
{
    maths::Vec4 angles{};
    maths::Mat4 target{};
    if (read_turn_angles(params, kRelative, &angles)) {
        target = entity_->basis_frame_;
        target.turn_in_own_frame(angles);
        start_rotation(params, &target);
        return;
    }
    if (read_turn_angles(params, kAbsolute, &angles)) {
        target.set_from_euler_angles(angles.x, angles.y, angles.z);
        start_rotation(params, &target);
    }
}

} // namespace objects
