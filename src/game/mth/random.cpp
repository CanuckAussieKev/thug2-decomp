#include <cstdint>

#include "game/mth/random.h"

namespace Mth {

namespace {

// State of one random stream. The original keeps the two streams in globals at 0x00690FC8-0x00690FDC.
struct RandomState {
    uint32_t add;
    uint32_t mul;
    uint32_t value;
};

RandomState g_stream1;
RandomState g_stream2;

RandomState& Stream1() { return g_stream1; }
RandomState& Stream2() { return g_stream2; }

constexpr uint32_t kInitialMul = 0x12B9B0A1;
constexpr uint32_t kInitialAdd = 0x0AA2FB3F;

// Advance a stream and return its new 16-bit sample.
uint32_t Next(RandomState& s)
{
    s.value = s.mul * s.value + s.add;
    int32_t v = static_cast<int32_t>(s.value);
    s.mul = (s.mul ^ s.value) + static_cast<uint32_t>(v >> 4);
    s.add = s.add - 0x10101010 + static_cast<uint32_t>(v >> 3);
    return s.value & 0xFFFF;
}

int Scale(uint32_t sample, int n)
{
    return static_cast<int>(sample * static_cast<uint32_t>(n)) >> 16;
}

} // namespace

// 0x00403DA0
void InitialRand(uint32_t seed)
{
    Stream1() = {kInitialAdd, kInitialMul, seed};
    Stream2() = {kInitialAdd, kInitialMul, seed};
}

// 0x00403DE0
int Rnd(int n)
{
    return Scale(Next(Stream1()), n);
}

// 0x00403E30
int Rnd2(int n)
{
    return Scale(Next(Stream2()), n);
}

// 0x00403E80
float PlusOrMinus(float range) { return static_cast<float>(PlusOrMinusExact(range)); }

// (0x00403E80's result as the x87 leaves it in ST0, unrounded)
double PlusOrMinusExact(float range)
{
    // A sample in [0, 10000), centred on 0 and scaled so that +/-5000 maps to +/-range.
    double centred = static_cast<double>(Scale(Next(Stream1()), 10000)) - 5000.0f;
    return centred * range * 0.0002f;
}

} // namespace Mth
