#pragma once

#include <cstdint>

namespace Maths {

// The game has two independent pseudo-random streams with the same algorithm. Rnd and SignedSpread share the
// first; RollSideStream uses the second, so code that needs repeatable sequences isn't disturbed by other random calls.

// Reset both streams to `seed`.
void SeedRandom(uint32_t seed);
// Random integer in [0, n) for n > 0, from the first stream.
int Rnd(int n);
// Random integer in [0, n) for n > 0, from the second stream.
int RollSideStream(int n);
// Random float in roughly [-range, range), from the first stream.
float SignedSpread(float range);
// The same unrounded, as the original's x87 callers use it.
double PlusOrMinusExact(float range);

} // namespace Mth
