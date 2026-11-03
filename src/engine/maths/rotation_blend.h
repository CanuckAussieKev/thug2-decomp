#pragma once

#include "engine/maths/mat4.h"

namespace maths {

// Interpolates between two orientation matrices. Setting the matrices works out the rotation that takes
// `start` to `end` as an axis and an angle; the interpolation (0x00405A20, not yet rewritten) turns part of
// that angle about the axis.
class ArcRotationBlender {
public:
    // Nothing set (an owner made in zeroed memory leaves it so until it sets the matrices).
    ArcRotationBlender() = default;
    ArcRotationBlender(const Mat4& from, const Mat4& to);

    // Store both matrices and compute the relative rotation. Returns the "small rotation" flag (the original
    // leaves it in EAX).
    int set_matrices(const Mat4& from, const Mat4& to);

    // Go the other way round: negate the axis and use 2*pi - angle.
    void flip_heading();

    // The orientation a fraction `t` of the way from start to end (t <= 0 gives start; t >= 1, or NaN, gives
    // end). Small rotations blend the rows linearly and renormalise; others rotate start by t * angle about the
    // axis. The translation row is always blended linearly.
    void fetch_mat(Mat4& out, float t) const;

    // The rotation's angle (radians, 0..pi).
    [[nodiscard]] float angle() const { return angle_; }

private:
    Mat4 start_{};
    Mat4 end_{};
    Vec4 axis_{};           // w holds 1 / (2 sin angle) when computed from the matrices, else 1
    float angle_{};         // radians, 0..pi
    bool small_rotation_{}; // angle below the game's threshold (2 degrees by default)

    static void check_layout();
};

} // namespace maths
