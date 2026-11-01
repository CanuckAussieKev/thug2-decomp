#include "engine/maths/maths.h"
#include "engine/core/checked_index.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <array>
#include <span>

namespace maths {

namespace {

constexpr float kPi = 3.14159265f;

// One step of the lookup table: the value at this angle and the difference to the next step.
struct SinCosEntry {
    float sin;
    float cos;
    float sin_delta_;
    float cos_delta_;
};

constexpr int kSinTableSize = 119; // 0 .. pi/2 inclusive

std::array<SinCosEntry, kSinTableSize> g_sin_table;

// Constants taken bit-for-bit from the exe. (Typing them in as decimals once introduced an error in the
// 5th significant digit of kAtanC3, so they stay as exact bit patterns.)
constexpr float get_bits(uint32_t bits) { return std::bit_cast<float>(bits); }

constexpr float kSinTableStep = get_bits(0x3C5A19E1); // 0x006453EC, (pi / 2) / 118

// fast_a_cos: Abramowitz & Stegun 4.4.45 coefficients (0x006453DC-0x006453E8).
constexpr float kAcosA0 = get_bits(0x3FC90DA4); //  1.5707288
constexpr float kAcosA1 = get_bits(0x3E593484); //  0.2121144 (subtracted)
constexpr float kAcosA2 = get_bits(0x3D981627); //  0.0742610
constexpr float kAcosA3 = get_bits(0xBC996E30); // -0.0187293

// fast_atan2: odd polynomial for atan(t), t in [-1, 0] (0x006453FC-0x00645418).
constexpr float kAtanC1 = get_bits(0x3F7FFFF5);  //  0.99999934
constexpr float kAtanC3 = get_bits(0x3EAAA61C);  //  0.33329856 (subtracted)
constexpr float kAtanC5 = get_bits(0x3E4C40A6);  //  0.19946536
constexpr float kAtanC7 = get_bits(0x3E0E6C63);  //  0.13908534 (subtracted)
constexpr float kAtanC9 = get_bits(0x3DC577DF);  //  0.09642004
constexpr float kAtanC11 = get_bits(0x3D6501C4); //  0.05590989 (subtracted)
constexpr float kAtanC13 = get_bits(0x3CB31652); //  0.02186123
constexpr float kAtanC15 = get_bits(0xBB84D7E7); // -0.00405406
// Its quadrant offsets (0x006453F0, 0x006453F4, 0x006453F8; pi 0x006453D8).
constexpr float kMinusQuarterPi = get_bits(0xBF490FDB), kThreeQuarterPi = get_bits(0x4016CBE4),
                kQuarterPi = get_bits(0x3F490FDB), kPiF = get_bits(0x40490FDB);

SinCosEntry* sin_table() { return g_sin_table.data(); }

} // namespace

float confine(float value, float min, float max)
{
    if (value > max)
        value = max;
    if (value < min)
        value = min;
    return value;
}

// 0x00405FD0
float get_sign(float value) { return value < 0.0f ? -1.0f : 1.0f; }

// 0x00403EF0
float fast_a_cos(float value)
{
    // Abramowitz & Stegun 4.4.45: acos(x) ~= sqrt(1 - x) * polynomial(x) for x >= 0.
    auto positive = [](float x) {
        return (((kAcosA3 * x + kAcosA2) * x - kAcosA1) * x + kAcosA0) * std::sqrt(1.0f - x);
    };
    return value < 0.0f ? kPi - positive(-value) : positive(value);
}

// 0x004049B0 (x87: the arctangent FPATAN's, the polynomial worked on the FPU and rounded once, as the callers store)
float fast_atan2(float y, float x)
{
    if (std::fabs(x) < std::fabs(y))
        return static_cast<float>(std::atan2(static_cast<double>(y), static_cast<double>(x)));

    // atan(r) = pi/4 + atan((r - 1) / (r + 1)), with r = |y / x| <= 1, so the polynomial only needs t in [-1, 0].
    const bool x_below_zero = x < 0.0f;
    const bool y_not_positive = y <= 0.0f;
    const double ratio = (y_not_positive ? -static_cast<double>(y) : y) / (x_below_zero ? -static_cast<double>(x) : x);
    const double t = (ratio - 1.0) / (ratio + 1.0);
    const double t2 = t * t;
    double p = (t * kAtanC15 * t2 + t * kAtanC13) * t2 - t * kAtanC11;
    p = (p * t2 + t * kAtanC9) * t2 - t * kAtanC7;
    p = (p * t2 + t * kAtanC5) * t2 - t * kAtanC3;
    p = p * t2 + t * kAtanC1;

    double result = 0.0;
    if (x_below_zero)
        result = y_not_positive ? (p + kQuarterPi) - kPiF : kThreeQuarterPi - p;
    else
        result = y_not_positive ? kMinusQuarterPi - p : p + kQuarterPi;
    return static_cast<float>(result);
}

// 0x00404010
float keep_within(float value, float target, float distance)
{
    const float limit = std::fabs(distance);
    if (value >= target) {
        if (limit < value - target)
            return target + limit;
    } else if (limit < target - value) {
        return target - limit;
    }
    return value;
}

// 0x004058B0
void create_rotate_2d(std::span<float, 4> out, float angle)
{
    const float s = std::sin(angle);
    const float c = std::cos(angle);
    core::at(out, 0) = c;
    core::at(out, 1) = s;
    core::at(out, 2) = -s;
    core::at(out, 3) = c;
}

namespace {

// Table steps per radian, 118 / (pi / 2). Taken bit-for-bit from the exe (0x00645480).
constexpr float kSinStepsPerRadian = get_bits(0x42963E05);

} // namespace

// 0x00405FF0
float fast_sin(float angle)
{
    const float steps = angle * kSinStepsPerRadian;
    const int index = static_cast<int>(steps); // truncates, like the CRT's _ftol the original calls
    const SinCosEntry& entry = sin_table()[index];
    return (steps - static_cast<float>(index)) * entry.sin_delta_ + entry.sin;
}

// 0x00406020
float fast_cos(float angle)
{
    const float steps = angle * kSinStepsPerRadian;
    const int index = static_cast<int>(steps);
    const SinCosEntry& entry = sin_table()[index];
    return (steps - static_cast<float>(index)) * entry.cos_delta_ + entry.cos;
}

// 0x00403F70
void fill_sine_table()
{
    SinCosEntry* table = sin_table();
    for (int i = 0; i < kSinTableSize; ++i) {
        const double angle = static_cast<double>(i) * kSinTableStep; // (the FPU's product, not rounded to float)
        table[i].sin = static_cast<float>(std::sin(angle));
        table[i].cos = static_cast<float>(std::cos(angle));
    }
    for (int i = 0; i < kSinTableSize - 1; ++i) {
        table[i].sin_delta_ = table[i + 1].sin - table[i].sin;
        table[i].cos_delta_ = table[i + 1].cos - table[i].cos;
    }
    // The original wraps the last entry's delta around to the first.
    table[kSinTableSize - 1].sin_delta_ = table[0].sin - table[kSinTableSize - 1].sin;
    table[kSinTableSize - 1].cos_delta_ = table[0].cos - table[kSinTableSize - 1].cos;
}

} // namespace maths
