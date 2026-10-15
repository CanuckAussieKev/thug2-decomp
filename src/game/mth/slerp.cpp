#include "game/mth/slerp.h"

#include <cstddef>
#include <new>

#include "hook/game.h"
#include "hook/hook.h"

namespace Mth {

static_assert(offsetof(SlerpInterpolator, axis) == 0x80 && offsetof(SlerpInterpolator, angle) == 0x90 &&
                  offsetof(SlerpInterpolator, small_rotation) == 0x94,
              "layout must match the original");

namespace {

// Below this angle the rotation counts as small. It lives in the exe's writable data (0x0067C648, 2 degrees
// by default), so it is read from there rather than copied.
float SmallRotationThreshold() { return GameVariable<float>(0x0067C648); }

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
REPLACE(0x00405E90, &SlerpAdapter::Construct);
