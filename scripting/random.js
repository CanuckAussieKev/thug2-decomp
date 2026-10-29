// NOT ORIGINAL - standalone QB RNG streams and signed-wrap scale.
// Behavior: mth/random.cpp (InitialRand 0x00403DA0, Rnd 0x00403DE0, Rnd2 0x00403E30);
// API: docs/qb-js-contract.md.
globalThis.qbRandom = (() => {
    const INITIAL_ADD = 0x0aa2fb3f;
    const INITIAL_MUL = 0x12b9b0a1;
    const floatBuffer = new DataView(new ArrayBuffer(4));

    function floatNumber(bits) {
        floatBuffer.setUint32(0, bits, true);
        return floatBuffer.getFloat32(0, true);
    }

    const RANGE_NUDGE = floatNumber(0x3dcccccd); // 0x00645D74 (0.1f)
    const RANGE_TOLERANCE = floatNumber(0x3727c5ac); // 0x006453B8 (1e-5f)
    const RANGE_SCALE = floatNumber(0x38000801); // 0x0064840C (1.0f / 32760.0f)

    function fail(code, message, site) {
        const suffix = site
            ? ` at ${site.source} script 0x${site.script.toString(16)} QB offset 0x${site.offset.toString(16)}`
            : "";
        const error = new Error(`QB ${code}: ${message}${suffix}`);
        error.code = code;
        error.site = site;
        throw error;
    }

    function uint(n, label = "uint32", site) {
        if (!Number.isInteger(n) || n < 0 || n > 0xffffffff) fail("type", `Expected ${label}`, site);
        return n;
    }

    function streamIndex(stream) {
        if (stream !== 0 && stream !== 1) fail("type", "Expected RNG stream 0 or 1");
        return stream;
    }

    function scale(sample, rangeBits) {
        if (!Number.isInteger(sample) || sample < 0 || sample > 0xffff) fail("type", "Expected uint16 RNG sample");
        uint(rangeBits, "range uint32");
        return (Math.imul(sample, rangeBits) >> 16) >>> 0;
    }

    // Exact bounded rational -> x87 64-bit significand (nearest-even) -> float32 bits.
    function dyadFloat(bits) {
        const exponent = (bits >>> 23) & 255;
        const magnitude = BigInt((bits & 0x7fffff) | (exponent ? 0x800000 : 0));
        return { m: bits & 0x80000000 ? -magnitude : magnitude, e: exponent ? exponent - 150 : -149 };
    }

    function nearestEven(n, d) {
        const q = n / d,
            remainder = n % d;
        return q + (remainder * 2n > d || (remainder * 2n === d && q & 1n) ? 1n : 0n);
    }

    function round64(n, d, exponent) {
        const negative = n < 0n;
        if (negative) n = -n;
        if (!n) return { m: 0n, e: 0 };
        let log = n.toString(2).length - d.toString(2).length;
        if (log >= 0 ? n < d << BigInt(log) : n << BigInt(-log) < d) --log;
        const shift = 63 - log;
        let m = shift >= 0 ? nearestEven(n << BigInt(shift), d) : nearestEven(n, d << BigInt(-shift));
        let e = exponent + log - 63;
        if (m === 1n << 64n) {
            m >>= 1n;
            ++e;
        }
        return { m: negative ? -m : m, e };
    }

    function store32(value, negativeZero) {
        const sign = value.m < 0n || (value.m === 0n && negativeZero) ? 0x80000000 : 0;
        let m = value.m < 0n ? -value.m : value.m;
        if (!m) return sign >>> 0;
        const log = m.toString(2).length - 1 + value.e;
        if (log > 127) return (sign | 0x7f800000) >>> 0;
        let e = Math.max(log - 23, -149);
        const shift = value.e - e;
        m = shift >= 0 ? m << BigInt(shift) : nearestEven(m, 1n << BigInt(-shift));
        if (!m) return sign >>> 0;
        if (m === 1n << 24n) {
            m >>= 1n;
            ++e;
        }
        const finalLog = m.toString(2).length - 1 + e;
        if (finalLog > 127) return (sign | 0x7f800000) >>> 0;
        if (finalLog < -126) return (sign | Number(m)) >>> 0;
        return (sign | ((finalLog + 127) << 23) | (Number(m) - 0x800000)) >>> 0;
    }

    function wholeEndpoint(f) {
        const nudged = f + (f >= 0 ? RANGE_NUDGE : -RANGE_NUDGE);
        const n = Math.trunc(nudged);
        if (n < -0x80000000 || n > 0x7fffffff) return null;
        return Math.abs(f - n) <= RANGE_TOLERANCE ? n | 0 : null;
    }

    function create(seedBits = 0) {
        uint(seedBits, "seed uint32");
        const streams = [
            { add: INITIAL_ADD, mul: INITIAL_MUL, value: seedBits },
            { add: INITIAL_ADD, mul: INITIAL_MUL, value: seedBits },
        ];

        function seed(nextSeedBits) {
            uint(nextSeedBits, "seed uint32");
            for (const s of streams) {
                s.add = INITIAL_ADD;
                s.mul = INITIAL_MUL;
                s.value = nextSeedBits;
            }
        }

        function raw(stream = 0) {
            const s = streams[streamIndex(stream)];
            s.value = (Math.imul(s.mul, s.value) + s.add) >>> 0;
            const signed = s.value | 0;
            s.mul = ((s.mul ^ s.value) + (signed >> 4)) >>> 0;
            s.add = (s.add - 0x10101010 + (signed >> 3)) >>> 0;
            return s.value & 0xffff;
        }

        function draw(rangeBits, stream = 0) {
            uint(rangeBits, "range uint32");
            streamIndex(stream);
            return scale(raw(stream), rangeBits);
        }

        function state(stream = 0) {
            const s = streams[streamIndex(stream)];
            return { add: s.add, mul: s.mul, value: s.value };
        }

        // ParseRandomRange (0x0046FBD0): integer when both endpoints are within
        // 1e-5f of a truncated (+/-0.1f nudged) int32, otherwise x87 64-bit float.
        function range(aFloatBits, bFloatBits, second = false, charge) {
            uint(aFloatBits, "range endpoint uint32");
            uint(bFloatBits, "range endpoint uint32");
            if (typeof second !== "boolean") fail("type", "Expected boolean second stream flag");
            if (charge !== undefined && typeof charge !== "function") fail("type", "Expected charge callback");
            if ((aFloatBits & 0x7f800000) === 0x7f800000 || (bFloatBits & 0x7f800000) === 0x7f800000)
                fail("arithmetic", "RandomRange requires finite bounds");
            if (charge) charge(1);
            const a = floatNumber(aFloatBits);
            const b = floatNumber(bFloatBits);
            const low = wholeEndpoint(a);
            const high = wholeEndpoint(b);
            const stream = second ? 1 : 0;
            if (low !== null && high !== null) {
                const min = low <= high ? low : high;
                const max = low <= high ? high : low;
                const width = ((max >>> 0) - (min >>> 0) + 1) >>> 0;
                const pick = draw(width, stream);
                return { type: 1, bits: (pick + (min >>> 0)) >>> 0 };
            }
            const lowBits = a <= b ? aFloatBits : bFloatBits;
            const highBits = a <= b ? bFloatBits : aFloatBits;
            const lowFloat = a <= b ? a : b;
            const highFloat = a <= b ? b : a;
            const pick = draw(0x7ff9, stream) | 0;
            const da = dyadFloat(lowBits);
            const db = dyadFloat(highBits);
            const eSpan = Math.min(da.e, db.e);
            const span = round64((db.m << BigInt(db.e - eSpan)) - (da.m << BigInt(da.e - eSpan)), 1n, eSpan);
            const scaledPick = round64(span.m * BigInt(pick), 1n, span.e);
            const scaledRange = round64(scaledPick.m * 0x800801n, 1n, scaledPick.e - 38);
            const eSum = Math.min(scaledRange.e, da.e);
            const total = round64(
                (scaledRange.m << BigInt(scaledRange.e - eSum)) + (da.m << BigInt(da.e - eSum)),
                1n,
                eSum,
            );
            const negativeZero = Object.is((highFloat - lowFloat) * pick * RANGE_SCALE + lowFloat, -0);
            return { type: 2, bits: store32(total, negativeZero) };
        }

        return Object.freeze({
            raw,
            scale,
            draw,
            state,
            seed,
            range,
        });
    }

    return Object.freeze({
        apiVersion: 1,
        scale,
        create,
    });
})();
