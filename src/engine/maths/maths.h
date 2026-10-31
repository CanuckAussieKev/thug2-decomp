#pragma once

namespace maths {

// Limits `value` to [min, max]. The max limit is applied first, so if min > max the result is min.
float confine(float value, float min, float max);

// -1 for negative values, otherwise 1 (including 0 and -0).
float get_sign(float value);

// Fast arc cosine (polynomial approximation, error around 1e-4 radians) for value in [-1, 1].
float fast_a_cos(float value);

// atan2(y, x). Uses the FPU's exact arctangent when |x| < |y| and a polynomial otherwise.
// atan2(0, 0) gives NaN, as in the original.
float fast_atan2(float y, float x);

// If `value` is more than `distance` away from `target`, move it to exactly that distance (on the same side);
// otherwise return it unchanged. The sign of `distance` is ignored.
float keep_within(float value, float target, float distance);

// 2D rotation by `angle` radians, stored as {cos, sin, -sin, cos}.
void create_rotate_2d(float out[4], float angle);

// Fill the sine/cosine lookup table that the engine's fast trig functions read (a quarter circle in 118 steps).
void fill_sine_table();

// Sine / cosine from the lookup table with linear interpolation. Only valid for angles in [0, pi/2]: like the
// original there is no range reduction, so other angles read outside the table.
float fast_sin(float angle);
float fast_cos(float angle);

} // namespace Mth
