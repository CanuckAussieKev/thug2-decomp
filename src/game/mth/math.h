#pragma once

namespace Mth {

// Limits `value` to [min, max]. The max limit is applied first, so if min > max the result is min.
float Clamp(float value, float min, float max);

// -1 for negative values, otherwise 1 (including 0 and -0).
float Sign(float value);

// Fast arc cosine (polynomial approximation, error around 1e-4 radians) for value in [-1, 1].
float FastACos(float value);

// If `value` is more than `distance` away from `target`, move it to exactly that distance (on the same side);
// otherwise return it unchanged. The sign of `distance` is ignored.
float KeepWithin(float value, float target, float distance);

// 2D rotation by `angle` radians, stored as {cos, sin, -sin, cos}.
void CreateRotate2D(float out[4], float angle);

// Fill the sine/cosine lookup table that the engine's fast trig functions read (a quarter circle in 118 steps).
void InitSinLookupTable();

} // namespace Mth
