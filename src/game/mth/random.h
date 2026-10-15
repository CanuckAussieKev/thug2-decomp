#pragma once

#include <cstdint>

namespace Mth {

// The game has two independent pseudo-random streams with the same algorithm. Rnd and PlusOrMinus share the
// first; Rnd2 uses the second, so code that needs repeatable sequences isn't disturbed by other random calls.

// Reset both streams to `seed`.
void InitialRand(uint32_t seed);
// Random integer in [0, n) for n > 0, from the first stream.
int Rnd(int n);
// Random integer in [0, n) for n > 0, from the second stream.
int Rnd2(int n);
// Random float in roughly [-range, range), from the first stream.
float PlusOrMinus(float range);

} // namespace Mth
