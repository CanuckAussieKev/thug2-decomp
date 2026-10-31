# Shared QB numeric arithmetic

`scripting/numeric.js` is the single implementation used by both expression
arithmetic and RandomRange. `qbNumeric` is immutable and installed once by the
native Runtime before the other helpers. Production generated modules do not
carry a helper copy.

The gameplay profile uses x87 precision control 24 (FPCW `0x007f`). Device
creation in `src/game/gfx/device.cpp` does not request FPU preservation, so the
Direct3D game-thread profile differs from the offline CRT/self-test profile.
`round(n,d,e,precision=24)` represents exact `(n/d)*2^e`, rounding nearest-even
to a 24-, 53- or 64-bit significand. Explicit offline profiles are available
for differential analysis, not selected implicitly from the host JS engine.
Extended exponents survive intermediate operations; `store32` alone performs
the float32 exponent/subnormal/overflow conversion. This distinction matters
for interpolation between extreme finite endpoints.

Other helpers are `dyadFloat(bits)`, `dyadInt(bits)`, `nearestEven(n,d)`,
`floatNumber(bits)`, `floatBits(number)` and `store32({m,e},negativeZero=false)`.
Dyads use signed BigInt mantissas. Integer inputs are signed int32 raw bits.
Float-to-Number conversion does not promise to preserve NaN payloads; finite
arithmetic rejects nonfinite operands. Numeric mantissas and exponents are
bounded before shifts/division (2048 input mantissa bits, absolute input
exponent at most 1024); those bounds exceed the finite float32/int32 and current
RandomRange domains. Oversized inputs reject, rather than performing unbounded
BigInt work. Negative zero is explicitly carried at the store boundary.

## Independent regression expectations

- `((2^80 + 2^56 + 1) * 2^-80)` rounds/stores to `0x3f800001` at precision 24;
  precision 53/64 first loses the tiny tail, then the float32 store ties to even
  at `0x3f800000`. This catches double rounding through a wider significand.
- Half a minimum subnormal stores as zero; one-and-a-half stores as raw word 2.
  Signed zero and overflow to signed infinity have separate expectations.
- For synthetic RandomRange(-0.5,0.5) at seed 3264, the gameplay profile stores
  `0x3f000000`; the previous precision-64 expectation was `0x3f000001`.

These run on Node and in embedded quickjs-ng via the review regression bundle.
The optional private original-x86 check (not a public project tool) was also
rerun with FPCW `0x007f`: 1,800 stream checks, 13,800 choice checks, 37,807 range
checks and 75,614 subsequent stream probes matched with no mismatches. This
establishes the exercised finite RNG/range cases, not every expression, script
or renderer/thread FPU state. No executable names/hashes or extracted assets
are published in this evidence.
