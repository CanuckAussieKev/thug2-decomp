#include "engine/maths/rotation_blend.h"
#include "engine/core/checked_index.h"
#include "engine/maths/mat4.h"

#include <cstddef>
#include <new>

namespace maths {

namespace {
constexpr size_t kAxisOffset = 0x80; // ArcRotationBlender's layout
constexpr size_t kAngleOffset = 0x90;
constexpr size_t kSmallRotationOffset = 0x94;
} // namespace

void ArcRotationBlender::check_layout()
{
    static_assert(offsetof(ArcRotationBlender, axis_) == kAxisOffset && offsetof(ArcRotationBlender, angle_) == kAngleOffset &&
                      offsetof(ArcRotationBlender, small_rotation_) == kSmallRotationOffset,
                  "layout must match the original");
}

namespace {
constexpr float kSlerpSmallRotationThreshold{0.034906585f};
constexpr float kFullTurn = 6.28318548f; // 2 pi

// Below this angle the rotation counts as small. It lives in the exe's writable data (0x0067C648, 2 degrees
// by default), so it is read from there rather than copied.
float small_rotation_threshold() { return kSlerpSmallRotationThreshold; }

} // namespace

// 0x00405E90
ArcRotationBlender::ArcRotationBlender(const Mat4& from, const Mat4& to) { set_matrices(from, to); }

// 0x00405990
int ArcRotationBlender::set_matrices(const Mat4& from, const Mat4& to)
{
    start_ = from;
    end_ = to;
    Mat4 relative = start_;
    relative.upend();
    get_multiply(relative, relative, end_); // the rotation taking start to end
    relative.get_rotation_axis_angle(axis_, angle_);
    small_rotation_ = angle_ < small_rotation_threshold();
    return small_rotation_ ? 1 : 0;
}

// 0x004058E0
void ArcRotationBlender::flip_heading()
{
    axis_ = -axis_;
    angle_ = kFullTurn - angle_; // 2 pi
}

// 0x00405A20
void ArcRotationBlender::fetch_mat(Mat4& out, float t) const
{
    if (t <= 0.0f) {
        out = start_;
        return;
    }
    if (!(t < 1.0f)) {
        out = end_;
        return;
    }

    // The translation is read first, as in the original (so `out` may be one of the stored matrices).
    const Vec4 from = core::at(start_.row, 3);
    const Vec4 to = core::at(end_.row, 3);
    auto lerp = [t](const Vec4& a, const Vec4& b) {
        return Vec4{.x = ((b.x - a.x) * t) + a.x,
                    .y = ((b.y - a.y) * t) + a.y,
                    .z = ((b.z - a.z) * t) + a.z,
                    .w = ((b.w - a.w) * t) + a.w};
    };

    if (small_rotation_) {
        // Too small to rotate accurately: blend the rotation rows and renormalise them.
        for (int i = 0; i < 3; ++i)
            core::at(out.row, i) = lerp(core::at(start_.row, i), core::at(end_.row, i));
        for (int i = 0; i < 3; ++i)
            core::at(out.row, i).make_unit();
        core::at(out.row, 0).w = core::at(out.row, 1).w = core::at(out.row, 2).w = 0.0f;
        core::at(out.row, 3) = lerp(from, to);
        return;
    }

    out = start_;
    core::at(out.row, 3) = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    out.spin_by(axis_, t * angle_);
    core::at(out.row, 3) = lerp(from, to);
    core::at(out.row, 3).w = 1.0f;
}

} // namespace maths
