#include "game/mth/math.h"

#include "hook/hook.h"

namespace Mth {

// 0x004022C0
float Clamp(float value, float min, float max)
{
    if (value > max)
        value = max;
    if (value < min)
        value = min;
    return value;
}

} // namespace Mth

REPLACE(0x004022C0, Mth::Clamp);
