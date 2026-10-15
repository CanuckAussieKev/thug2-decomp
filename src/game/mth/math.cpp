#include "game/mth/math.h"

#include <bit>
#include <cmath>
#include <cstdint>

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

constexpr int kSinTableSize = 119; // 0 .. pi/2 inclusive

// Constants taken bit-for-bit from the exe. (Typing them in as decimals once introduced an error in the
// 5th significant digit of kAtanC3, so they stay as exact bit patterns.)
constexpr float Bits(uint32_t bits) { return std::bit_cast<float>(bits); }

constexpr float kSinTableStep = Bits(0x3C5A19E1); // 0x006453EC, (pi / 2) / 118

// FastACos: Abramowitz & Stegun 4.4.45 coefficients (0x006453DC-0x006453E8).
constexpr float kAcosA0 = Bits(0x3FC90DA4); //  1.5707288
constexpr float kAcosA1 = Bits(0x3E593484); //  0.2121144 (subtracted)
constexpr float kAcosA2 = Bits(0x3D981627); //  0.0742610
constexpr float kAcosA3 = Bits(0xBC996E30); // -0.0187293

// FastAtan2: odd polynomial for atan(t), t in [-1, 0] (0x006453FC-0x00645418).
constexpr float kAtanC1 = Bits(0x3F7FFFF5);  //  0.99999934
constexpr float kAtanC3 = Bits(0x3EAAA61C);  //  0.33329856 (subtracted)
constexpr float kAtanC5 = Bits(0x3E4C40A6);  //  0.19946536
constexpr float kAtanC7 = Bits(0x3E0E6C63);  //  0.13908534 (subtracted)
constexpr float kAtanC9 = Bits(0x3DC577DF);  //  0.09642004
constexpr float kAtanC11 = Bits(0x3D6501C4); //  0.05590989 (subtracted)
constexpr float kAtanC13 = Bits(0x3CB31652); //  0.02186123
constexpr float kAtanC15 = Bits(0xBB84D7E7); // -0.00405406

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
        return (((kAcosA3 * x + kAcosA2) * x - kAcosA1) * x + kAcosA0) * std::sqrt(1.0f - x);
    };
    return value < 0.0f ? kPi - positive(-value) : positive(value);
}

// 0x004049B0
float FastAtan2(float y, float x)
{
    if (std::fabs(x) < std::fabs(y))
        return std::atan2(y, x);

    // atan(r) = pi/4 + atan((r - 1) / (r + 1)), with r = |y / x| <= 1, so the polynomial only needs t in [-1, 0].
    bool x_negative = x < 0.0f;
    bool y_not_positive = y <= 0.0f;
    float ratio = std::fabs(y) / std::fabs(x);
    float t = (ratio - 1.0f) / (ratio + 1.0f);
    float t2 = t * t;
    float p = (t * kAtanC13 + t * kAtanC15 * t2) * t2 - t * kAtanC11;
    p = (t * kAtanC9 + p * t2) * t2 - t * kAtanC7;
    p = (t * kAtanC5 + p * t2) * t2 - t * kAtanC3;
    p = t * kAtanC1 + p * t2;

    constexpr float kQuarterPi = kPi / 4.0f;
    if (x_negative)
        return y_not_positive ? (p + kQuarterPi) - kPi : 3.0f * kQuarterPi - p;
    return y_not_positive ? -kQuarterPi - p : p + kQuarterPi;
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

namespace {

// Table steps per radian, 118 / (pi / 2). Taken bit-for-bit from the exe (0x00645480).
constexpr float kSinStepsPerRadian = Bits(0x42963E05);

} // namespace

// 0x00405FF0
float FastSin(float angle)
{
    float steps = angle * kSinStepsPerRadian;
    int index = static_cast<int>(steps); // truncates, like the CRT's _ftol the original calls
    const SinCosEntry& entry = SinTable()[index];
    return (steps - static_cast<float>(index)) * entry.sin_delta + entry.sin;
}

// 0x00406020
float FastCos(float angle)
{
    float steps = angle * kSinStepsPerRadian;
    int index = static_cast<int>(steps);
    const SinCosEntry& entry = SinTable()[index];
    return (steps - static_cast<float>(index)) * entry.cos_delta + entry.cos;
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
REPLACE(0x004049B0, Mth::FastAtan2);
REPLACE(0x004058B0, Mth::CreateRotate2D);
REPLACE(0x00405FD0, Mth::Sign);
REPLACE(0x00405FF0, Mth::FastSin);
REPLACE(0x00406020, Mth::FastCos);
