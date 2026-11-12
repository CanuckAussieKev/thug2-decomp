#include <cstdint>

#include "base/maths/random.h"

namespace maths {

namespace {

// State of one random stream. The original keeps the two streams in globals at 0x00690FC8-0x00690FDC.
struct RandomState {
    uint32_t add;
    uint32_t mul;
    uint32_t value;
};

RandomState& stream1()
{
    static RandomState stream1{};
    return stream1;
}
RandomState& stream2()
{
    static RandomState stream2{};
    return stream2;
}

constexpr uint32_t kInitialMul = 0x12B9B0A1;
constexpr uint32_t kInitialAdd = 0x0AA2FB3F;
constexpr int kAddStep = 0x10101010;
constexpr int kSampleMask = 0xFFFF; // a stream's sample is its low 16 bits
constexpr int kSampleBits = 16;
constexpr float kSpreadScale = 0.0002F; // 1 / 5000: the centred sample's range to +/-1

// Advance a stream and return its new 16-bit sample.
uint32_t get_next(RandomState& state)
{
    state.value = (state.mul * state.value) + state.add;
    auto v = static_cast<int32_t>(state.value);
    state.mul = (state.mul ^ state.value) + static_cast<uint32_t>(v >> 4);
    state.add = state.add - kAddStep + static_cast<uint32_t>(v >> 3);
    return state.value & kSampleMask;
}

int scale_tool(uint32_t sample, int limit)
{
    return static_cast<int>(sample * static_cast<uint32_t>(limit)) >> kSampleBits;
}

} // namespace

// 0x00403DA0
void seed_random(uint32_t seed)
{
    stream1() = {.add = kInitialAdd, .mul = kInitialMul, .value = seed};
    stream2() = {.add = kInitialAdd, .mul = kInitialMul, .value = seed};
}

// 0x00403DE0
int rnd(int limit) { return scale_tool(get_next(stream1()), limit); }

// 0x00403E30
int roll_side_stream(int limit) { return scale_tool(get_next(stream2()), limit); }

// 0x00403E80
float signed_spread(float range) { return static_cast<float>(plus_or_minus_exact(range)); }

// (0x00403E80's result as the x87 leaves it in ST0, unrounded)
double plus_or_minus_exact(float range)
{
    // A sample in [0, 10000), centred on 0 and scaled so that +/-5000 maps to +/-range.
    const double centred = static_cast<double>(scale_tool(get_next(stream1()), 10000)) - 5000.0F;
    return centred * range * kSpreadScale;
}

RandomStreams random_streams()
{
    return {stream1().add, stream1().mul, stream1().value, stream2().add, stream2().mul, stream2().value};
}

void set_random_streams(const RandomStreams& streams)
{
    stream1() = {.add = streams[0], .mul = streams[1], .value = streams[2]};
    stream2() = {.add = streams[3], .mul = streams[4], .value = streams[5]};
}

} // namespace maths
