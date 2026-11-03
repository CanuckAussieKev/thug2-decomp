#pragma once

#include <cmath>
#include <concepts>
#include <span>

namespace maths {

// The game's distances are in inches: a foot (0x0064546C).
constexpr float kInchesPerFoot = 12.0f;
// Converting doubles to integers: past kExactDoubleLimit a double no longer converts to a 64-bit integer; kUnsignedWrap
// (2^32) brings a negative 32-bit count back to the unsigned value it stands for.
constexpr double kExactDoubleLimit = 9.2e18;
constexpr double kUnsignedWrap = 4294967296.0;

// `value` plus a half, its fraction dropped (truncated toward zero, as a conversion to an integer does): the nearest
// whole number, halves up, for a value that is never negative. The same arithmetic as converting `value + 0.5` (in
// `value`'s own type): the sum is truncated by std::trunc first, so the conversion itself drops nothing.
template <std::integral Int, std::floating_point Float>
[[nodiscard]] Int round_non_negative(Float value)
{
    constexpr Float kHalf = 0.5;
    return static_cast<Int>(std::trunc(value + kHalf));
}

// Limits `value` to [min, max]. The max limit is applied first, so if min > max the result is min.
float confine(float value, float min, float max);

// -1 for negative values, otherwise 1 (including 0 and -0).
float get_sign(float value);

// Fast arc cosine (polynomial approximation, error around 1e-4 radians) for value in [-1, 1].
float fast_a_cos(float value);

// atan2(y, x). Uses the FPU's exact arctangent when |x| < |y| and a polynomial otherwise.
// atan2(0, 0) gives NaN, as in the original.
float fast_atan2(float y, float x);

// A target and the distance a value is kept within of it.
struct Within {
    float target{};
    float distance{};
};

// If `value` is more than `distance` away from `target`, move it to exactly that distance (on the same side);
// otherwise return it unchanged. The sign of `distance` is ignored.
float keep_within(float value, Within within);

// 2D rotation by `angle` radians, stored as {cos, sin, -sin, cos}.
void create_rotate_2d(std::span<float, 4> out, float angle);

// Fill the sine/cosine lookup table that the engine's fast trig functions read (a quarter circle in 118 steps).
void fill_sine_table();

// Sine / cosine from the lookup table with linear interpolation. Only valid for angles in [0, pi/2]: like the
// original there is no range reduction, so other angles read outside the table.
float fast_sin(float angle);
float fast_cos(float angle);

} // namespace maths
