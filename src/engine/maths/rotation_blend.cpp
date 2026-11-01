#include "engine/maths/rotation_blend.h"

#include <cstddef>
#include <new>

namespace maths {

static_assert(offsetof(ArcRotationBlender, axis) == 0x80 && offsetof(ArcRotationBlender, angle) == 0x90 &&
                  offsetof(ArcRotationBlender, small_rotation_) == 0x94,
              "layout must match the original");

namespace {
float g_slerp_small_rotation_threshold{0.034906585f};

// Below this angle the rotation counts as small. It lives in the exe's writable data (0x0067C648, 2 degrees
// by default), so it is read from there rather than copied.
float small_rotation_threshold() { return g_slerp_small_rotation_threshold; }

} // namespace

// 0x00405E90
ArcRotationBlender::ArcRotationBlender(const Mat4& from, const Mat4& to) { set_matrices(from, to); }

// 0x00405990
int ArcRotationBlender::set_matrices(const Mat4& from, const Mat4& to)
{
    start = from;
    end = to;
    Mat4 relative = start;
    relative.upend();
    get_multiply(relative, relative, end); // the rotation taking start to end
    relative.get_rotation_axis_angle(axis, angle);
    small_rotation_ = angle < small_rotation_threshold();
    return small_rotation_ ? 1 : 0;
}

// 0x004058E0
void ArcRotationBlender::flip_heading()
{
    axis = -axis;
    angle = 6.28318548f - angle; // 2 pi
}

// 0x00405A20
void ArcRotationBlender::fetch_mat(Mat4& out, float t) const
{
    if (t <= 0.0f) {
        out = start;
        return;
    }
    if (!(t < 1.0f)) {
        out = end;
        return;
    }

    // The translation is read first, as in the original (so `out` may be one of the stored matrices).
    const Vec4 from = start.row[3];
    const Vec4 to = end.row[3];
    auto lerp = [t](const Vec4& a, const Vec4& b) {
        return Vec4{(b.x - a.x) * t + a.x, (b.y - a.y) * t + a.y, (b.z - a.z) * t + a.z, (b.w - a.w) * t + a.w};
    };

    if (small_rotation_) {
        // Too small to rotate accurately: blend the rotation rows and renormalise them.
        for (int i = 0; i < 3; ++i)
            out.row[i] = lerp(start.row[i], end.row[i]);
        for (int i = 0; i < 3; ++i)
            out.row[i].make_unit();
        out.row[0].w = out.row[1].w = out.row[2].w = 0.0f;
        out.row[3] = lerp(from, to);
        return;
    }

    out = start;
    out.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    out.spin_by(axis, t * angle);
    out.row[3] = lerp(from, to);
    out.row[3].w = 1.0f;
}

} // namespace maths

namespace {

struct SlerpAdapter {
    maths::ArcRotationBlender* make_new(const maths::Mat4& from, const maths::Mat4& to)
    {
        return new (this) maths::ArcRotationBlender(from, to);
    }
};

} // namespace
