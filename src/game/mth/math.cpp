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

// 0x00405FD0
float Sign(float value)
{
    return value < 0.0f ? -1.0f : 1.0f;
}

} // namespace Mth

REPLACE(0x004022C0, Mth::Clamp);
REPLACE(0x00405FD0, Mth::Sign);
