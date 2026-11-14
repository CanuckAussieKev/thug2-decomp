#pragma once

// NOT ORIGINAL - transforms blended between two simulation ticks (the render-only frames of [Graphics] max_fps:
// engine/sys/frame_pacing.h, render_pc/frame_interp.h). A transform here is a D3D row-vector matrix: rows 0-2 the
// X, Y and Z axes (their lengths the scale), row 3 the translation. Blended as a transform, never element by element:
//   - the rotation as a unit quaternion, spherically (slerp, the shorter way round): a fast spin or flip keeps its shape
//     and its size at every step between;
//   - the scale (each axis' length) and the translation linearly;
//   - a mirrored matrix (negative determinant) keeps its mirror (the X axis' scale negative on both sides);
//   - a matrix that isn't a rotation and scale (sheared, or an axis of length 0) is not blended: the later one is used.
// Alpha 0 is the earlier transform and alpha 1 the later one, bit for bit (and two equal matrices give that matrix).
// A jump - a move too far or a turn too sharp for one tick (a teleport, a respawn, a camera cut) - is not blended
// either: is_jump tells, and the caller draws the later state.

#include <array>

#include "base/maths/mat4.h"

namespace maths::transform_blend {

// A unit quaternion (x, y, z, w).
using Quat = std::array<float, 4>;

// A transform taken apart.
struct Pose {
    Quat rotation{0.0F, 0.0F, 0.0F, 1.0F};
    std::array<float, 3> scale{1.0F, 1.0F, 1.0F};
    std::array<float, 3> translation{};
};

// Whether a step is a jump: the translation moved more than `distance` (inches) or the rotation turned more than
// `angle` (radians).
struct JumpLimits {
    float distance;
    float angle;
};
// An object (a model's instance): 10 feet or a quarter turn in one tick (1/60 s) - 600 feet a second, 5400 degrees a
// second: far beyond anything that moves in the game - is a teleport.
inline constexpr JumpLimits kObjectJump{.distance = 120.0F, .angle = 1.5707964F};
// A camera: 10 feet or an eighth of a turn in one tick is a cut.
inline constexpr JumpLimits kCameraJump{.distance = 120.0F, .angle = 0.7853982F};
// A bone of a skinned model's palette (model space): a quarter turn in one tick is an animation snapped to a new pose.
inline constexpr JumpLimits kBoneJump{.distance = 120.0F, .angle = 1.5707964F};

// The pose of a rotation-and-scale matrix; false when it isn't one (sheared, an axis of zero length, not finite).
bool decompose(const Mat4& matrix, Pose* pose);
// The matrix of a pose (row 3's w 1, the axes' w 0).
Mat4 compose(const Pose& pose);
// Spherical blend of two unit quaternions, the shorter way round (normalised linear blend when they are nearly equal).
Quat slerp(const Quat& from, const Quat& to, float alpha);
// The angle (radians, 0 - pi) of the turn from one rotation to the other.
float angle_between(const Quat& from, const Quat& to);

// `from` blended toward `to` by alpha (0 - 1, clamped): see the top of the file.
Mat4 blend(const Mat4& from, const Mat4& to, float alpha);
// Whether the step from `from` to `to` is a jump (by the limits). Matrices that can't be taken apart are a jump.
bool is_jump(const Mat4& from, const Mat4& to, const JumpLimits& limits);

} // namespace maths::transform_blend
