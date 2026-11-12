#pragma once

#include <array>
#include <cstdint>

namespace maths {

// The game has two independent pseudo-random streams with the same algorithm. Rnd and signed_spread share the
// first; roll_side_stream uses the second, so code that needs repeatable sequences isn't disturbed by other random calls.

// Reset both streams to `seed`.
void seed_random(uint32_t seed);
// Random integer in [0, n) for n > 0, from the first stream.
int rnd(int limit);
// Random integer in [0, n) for n > 0, from the second stream.
int roll_side_stream(int limit);
// Random float in roughly [-range, range), from the first stream.
float signed_spread(float range);
// The same unrounded, as the original's x87 callers use it.
double plus_or_minus_exact(float range);

// NOT ORIGINAL - both streams' state (add, mul, value each), read and put back as a whole: a demo starts its run from
// the state it was recorded with (devtools/demo.h).
using RandomStreams = std::array<uint32_t, 6>;
RandomStreams random_streams();
void set_random_streams(const RandomStreams& streams);

} // namespace maths
