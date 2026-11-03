// What a point riding on a moving object is carried by. See ride_tracker.h.

#include "engine/objects/ride_tracker.h"
#include "engine/maths/mat4.h"
#include "engine/objects/entity.h"
#include "platform/d3dx.h"
#include "engine/core/checked_index.h"

namespace objects {

namespace {

float get_difference(float a, float b) { return static_cast<float>(static_cast<double>(a) - b); }

// The rotation part only: no translation (the fourth row 0, 0, 0, 1) and a zero fourth column.
void keep_rotation(maths::Mat4& m)
{
    core::at(m.row, 3) = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    core::at(m.row, 0).w = 0.0F;
    core::at(m.row, 1).w = 0.0F;
    core::at(m.row, 2).w = 0.0F;
}

// out = a * b (the statically linked D3DXMatrixMultiply, 0x005BFA8C: a library function, not decompiled).
void d3d_matrix_multiply(maths::Mat4* out, const maths::Mat4* a, const maths::Mat4* b)
{
    d3dx::matrix_multiply(out, a, b);
}

} // namespace

// 0x00416E70
RideTracker::RideTracker(GameEntity* object) : previous_xform_(object->basis_frame()), last_pos_(object->at_point())
{
    entity_.construct_from(object);
    carried_ = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
}

// 0x00416F00
bool RideTracker::tick_frame(const maths::Vec4* point)
{
    const GameEntity* object = entity_.get_pull();
    if (!object)
        return false;
    const maths::Vec4& position = object->at_point();
    carried_ = {.x = get_difference(position.x, last_pos_.x), .y = get_difference(position.y, last_pos_.y),
                .z = get_difference(position.z, last_pos_.z), .w = get_difference(position.w, last_pos_.w)};
    if (object->basis_frame() == previous_xform_) {
        turned_ = false;
    } else {
        turned_ = true;
        // The point about the old position, turned by the orientation's change (new times the old's inverse).
        const maths::Vec4 offset{.x = get_difference(point->x, last_pos_.x), .y = get_difference(point->y, last_pos_.y),
                                 .z = get_difference(point->z, last_pos_.z), .w = get_difference(point->w, last_pos_.w)};
        maths::Mat4 undo = previous_xform_;
        keep_rotation(undo);
        undo.invert_orthonormal();
        turn_ = object->basis_frame();
        keep_rotation(turn_);
        d3d_matrix_multiply(&turn_, &turn_, &undo);
        maths::Vec4 turned{};
        turn_.xform(turned, offset);
        carried_.x = static_cast<float>((static_cast<double>(turned.x) - offset.x) + carried_.x);
        carried_.y = static_cast<float>((static_cast<double>(turned.y) - offset.y) + carried_.y);
        carried_.z = get_difference(turned.z, offset.z) + carried_.z;
        carried_.w = get_difference(turned.w, offset.w) + carried_.w;
        previous_xform_ = object->basis_frame();
    }
    last_pos_ = object->at_point();
    return true;
}

} // namespace objects
