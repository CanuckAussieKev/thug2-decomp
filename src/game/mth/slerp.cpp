#include "game/mth/slerp.h"

#include <cstddef>
#include <new>

#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

namespace Mth {

static_assert(offsetof(SlerpInterpolator, axis) == 0x80 && offsetof(SlerpInterpolator, angle) == 0x90 &&
                  offsetof(SlerpInterpolator, small_rotation) == 0x94,
              "layout must match the original");

GAME_GLOBAL(float, g_slerpSmallRotationThreshold, 0x0067C648, 0.034906585f);

namespace {

// Below this angle the rotation counts as small. It lives in the exe's writable data (0x0067C648, 2 degrees
// by default), so it is read from there rather than copied.
float SmallRotationThreshold() { return g_slerpSmallRotationThreshold; }

} // namespace

// 0x00405E90
SlerpInterpolator::SlerpInterpolator(const Matrix& from, const Matrix& to)
{
    SetMatrices(from, to);
}

// 0x00405990
int SlerpInterpolator::SetMatrices(const Matrix& from, const Matrix& to)
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
void SlerpInterpolator::InvertDirection()
{
    axis = -axis;
    angle = 6.28318548f - angle; // 2 pi
}

// 0x00405A20
void SlerpInterpolator::GetMatrix(Matrix& out, float t) const
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
    Mth::SlerpInterpolator* Construct(const Mth::Matrix& from, const Mth::Matrix& to)
    {
        return new (this) Mth::SlerpInterpolator(from, to);
    }
};

} // namespace

REPLACE(0x004058E0, &Mth::SlerpInterpolator::InvertDirection);
REPLACE(0x00405990, &Mth::SlerpInterpolator::SetMatrices);
REPLACE(0x00405A20, &Mth::SlerpInterpolator::GetMatrix);
REPLACE(0x00405E90, &SlerpAdapter::Construct);
