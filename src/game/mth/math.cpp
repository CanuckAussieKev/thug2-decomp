#include "game/mth/math.h"

#include <cmath>

#include "hook/game.h"
#include "hook/hook.h"

namespace Mth {

namespace {

constexpr float kPi = 3.14159265f;

// One step of the lookup table: the value at this angle and the difference to the next step.
struct SinCosEntry {
    float sin;
    float cos;
    float sin_delta;
    float cos_delta;
};

constexpr int kSinTableSize = 119;               // 0 .. pi/2 inclusive
constexpr float kSinTableStep = kPi / 2.0f / 118; // 0x006453EC

SinCosEntry* SinTable() { return &GameVariable<SinCosEntry>(0x00690FE0); }

} // namespace

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

// 0x00403EF0
float FastACos(float value)
{
    // Abramowitz & Stegun 4.4.45: acos(x) ~= sqrt(1 - x) * polynomial(x) for x >= 0.
    auto positive = [](float x) {
        return (((-0.0187293f * x + 0.0742610f) * x - 0.2121144f) * x + 1.5707288f) * std::sqrt(1.0f - x);
    };
    return value < 0.0f ? kPi - positive(-value) : positive(value);
}

// 0x00404010
float KeepWithin(float value, float target, float distance)
{
    float limit = std::fabs(distance);
    if (value >= target) {
        if (limit < value - target)
            return target + limit;
    } else if (limit < target - value) {
        return target - limit;
    }
    return value;
}

// 0x004058B0
void CreateRotate2D(float out[4], float angle)
{
    float s = std::sin(angle);
    float c = std::cos(angle);
    out[0] = c;
    out[1] = s;
    out[2] = -s;
    out[3] = c;
}

// 0x00403F70
void InitSinLookupTable()
{
    SinCosEntry* table = SinTable();
    for (int i = 0; i < kSinTableSize; ++i) {
        float angle = static_cast<float>(i) * kSinTableStep;
        table[i].sin = std::sin(angle);
        table[i].cos = std::cos(angle);
    }
    for (int i = 0; i < kSinTableSize - 1; ++i) {
        table[i].sin_delta = table[i + 1].sin - table[i].sin;
        table[i].cos_delta = table[i + 1].cos - table[i].cos;
    }
    // The original wraps the last entry's delta around to the first.
    table[kSinTableSize - 1].sin_delta = table[0].sin - table[kSinTableSize - 1].sin;
    table[kSinTableSize - 1].cos_delta = table[0].cos - table[kSinTableSize - 1].cos;
}

} // namespace Mth

REPLACE(0x004022C0, Mth::Clamp);
REPLACE(0x00403EF0, Mth::FastACos);
REPLACE(0x00403F70, Mth::InitSinLookupTable);
REPLACE(0x00404010, Mth::KeepWithin);
REPLACE(0x004058B0, Mth::CreateRotate2D);
REPLACE(0x00405FD0, Mth::Sign);
