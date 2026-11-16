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
constexpr int kSpreadSamples = 10000;   // signed_spread's sample range

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

namespace {
DrawListener& draw_listener()
{
    static DrawListener listener = nullptr;
    return listener;
}
} // namespace

void set_draw_listener(DrawListener listener) { draw_listener() = listener; }

// 0x00403DE0
int rnd(int limit)
{
    const int result = scale_tool(get_next(stream1()), limit);
    if (const DrawListener listener = draw_listener()) { // NOT ORIGINAL (random.h)
        listener(0, limit, result);
    }
    return result;
}

// 0x00403E30
int roll_side_stream(int limit)
{
    const int result = scale_tool(get_next(stream2()), limit);
    if (const DrawListener listener = draw_listener()) { // NOT ORIGINAL (random.h)
        listener(1, limit, result);
    }
    return result;
}

// 0x00403E80
float signed_spread(float range) { return static_cast<float>(plus_or_minus_exact(range)); }

// (0x00403E80's result as the x87 leaves it in ST0, unrounded)
double plus_or_minus_exact(float range)
{
    // A sample in [0, 10000), centred on 0 and scaled so that +/-5000 maps to +/-range.
    const int sample = scale_tool(get_next(stream1()), kSpreadSamples);
    if (const DrawListener listener = draw_listener()) { // NOT ORIGINAL (random.h)
        listener(0, kSpreadSamples, sample);
    }
    const double centred = static_cast<double>(sample) - 5000.0F;
    return centred * range * kSpreadScale;
}

RandomStreams random_streams()
{
    return {stream1().add, stream1().mul, stream1().value, stream2().add, stream2().mul, stream2().value};
}

void set_random_streams(const RandomStreams& streams)
{
    const auto& [add1, mul1, value1, add2, mul2, value2] = streams;
    stream1() = {.add = add1, .mul = mul1, .value = value1};
    stream2() = {.add = add2, .mul = mul2, .value = value2};
}

RandomStreams seeded_streams(uint32_t seed)
{
    return {kInitialAdd, kInitialMul, seed, kInitialAdd, kInitialMul, seed};
}

OwnStreams::OwnStreams(RandomStreams& own) : own_(&own), game_(random_streams()), listener_(draw_listener())
{
    set_random_streams(own);
    draw_listener() = nullptr;
}

OwnStreams::~OwnStreams()
{
    *own_ = random_streams();
    set_random_streams(game_);
    draw_listener() = listener_;
}

} // namespace maths
