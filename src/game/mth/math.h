#pragma once

namespace Mth {

// Limits `value` to [min, max]. The max limit is applied first, so if min > max the result is min.
float Clamp(float value, float min, float max);

} // namespace Mth
