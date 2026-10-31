#include "game/mth/slerp.h"

#include <cstddef>
#include <new>

namespace Maths {

static_assert(offsetof(ArcRotationBlender, axis) == 0x80 && offsetof(ArcRotationBlender, angle) == 0x90 &&
                  offsetof(ArcRotationBlender, small_rotation) == 0x94,
              "layout must match the original");

float g_slerpSmallRotationThreshold{0.034906585f};

namespace {

// Below this angle the rotation counts as small. It lives in the exe's writable data (0x0067C648, 2 degrees
// by default), so it is read from there rather than copied.
float SmallRotationThreshold() { return g_slerpSmallRotationThreshold; }

} // namespace

// 0x00405E90
ArcRotationBlender::ArcRotationBlender(const Matrix& from, const Matrix& to)
{
    SetMatrices(from, to);
}

// 0x00405990
int ArcRotationBlender::SetMatrices(const Matrix& from, const Matrix& to)
{
    start = from;
    end = to;
    Matrix relative = start;
    relative.Invert();
    Multiply(relative, relative, end); // the rotation taking start to end
    relative.GetRotationAxisAngle(axis, angle);
    small_rotation = angle < SmallRotationThreshold();
    return small_rotation ? 1 : 0;
}

// 0x004058E0
void ArcRotationBlender::InvertDirection()
{
    axis = -axis;
    angle = 6.28318548f - angle; // 2 pi
}

// 0x00405A20
void ArcRotationBlender::GetMatrix(Matrix& out, float t) const
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
    const Vector from = start.row[3];
    const Vector to = end.row[3];
    auto lerp = [t](const Vector& a, const Vector& b) {
        return Vector{(b.x - a.x) * t + a.x, (b.y - a.y) * t + a.y, (b.z - a.z) * t + a.z, (b.w - a.w) * t + a.w};
    };

    if (small_rotation) {
        // Too small to rotate accurately: blend the rotation rows and renormalise them.
        for (int i = 0; i < 3; ++i)
            out.row[i] = lerp(start.row[i], end.row[i]);
        for (int i = 0; i < 3; ++i)
            out.row[i].Normalize();
        out.row[0].w = out.row[1].w = out.row[2].w = 0.0f;
        out.row[3] = lerp(from, to);
        return;
    }

    out = start;
    out.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    out.Rotate(axis, t * angle);
    out.row[3] = lerp(from, to);
    out.row[3].w = 1.0f;
}

} // namespace Mth

namespace {

struct SlerpAdapter {
    Maths::ArcRotationBlender* Construct(const Maths::Matrix& from, const Maths::Matrix& to)
    {
        return new (this) Maths::ArcRotationBlender(from, to);
    }
};

} // namespace
