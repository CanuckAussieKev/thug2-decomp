// NOT ORIGINAL - standalone QB RNG streams and signed-wrap scale.
// Behavior: mth/random.cpp (InitialRand 0x00403DA0, Rnd 0x00403DE0, Rnd2 0x00403E30);
// API: docs/qb-js-contract.md.
globalThis.qbRandom = (() => {
    const INITIAL_ADD = 0x0aa2fb3f;
    const INITIAL_MUL = 0x12b9b0a1;
    const {floatNumber, dyadFloat, round: roundPrecision, store32} = globalThis.qbNumeric;
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

    // Exact bounded rational -> x87 gameplay 24-bit significand (nearest-even) -> float32 bits.
    function wholeEndpoint(f) {
        const nudged = f + (f >= 0 ? RANGE_NUDGE : -RANGE_NUDGE);
        const n = Math.trunc(nudged);
        if (n < -0x80000000 || n > 0x7fffffff) return null;
        return Math.abs(f - n) <= RANGE_TOLERANCE ? n | 0 : null;
    }

    function validateSite(site) {
        if (!site || typeof site !== "object" || typeof site.source !== "string" || site.source.length === 0)
            fail("type", "Expected QB site with source");
        if (!Number.isInteger(site.script) || site.script < 0 || site.script > 0xffffffff)
            fail("type", "Expected uint32 site script");
        if (!Number.isInteger(site.offset) || site.offset < 0 || site.offset > 0xffffffff)
            fail("type", "Expected uint32 site offset");
    }

    function create(seedBits = 0) {
        uint(seedBits, "seed uint32");
        const streams = [
            { add: INITIAL_ADD, mul: INITIAL_MUL, value: seedBits },
            { add: INITIAL_ADD, mul: INITIAL_MUL, value: seedBits },
        ];
        const stored = [];

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
        // 1e-5f of a truncated (+/-0.1f nudged) int32, otherwise x87 gameplay 24-bit float.
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
            const span = roundPrecision((db.m << BigInt(db.e - eSpan)) - (da.m << BigInt(da.e - eSpan)), 1n, eSpan);
            const scaledPick = roundPrecision(span.m * BigInt(pick), 1n, span.e);
            const scaledRange = roundPrecision(scaledPick.m * 0x800801n, 1n, scaledPick.e - 38);
            const eSum = Math.min(scaledRange.e, da.e);
            const total = roundPrecision(
                (scaledRange.m << BigInt(scaledRange.e - eSum)) + (da.m << BigInt(da.e - eSum)),
                1n,
                eSum,
            );
            const negativeZero = Object.is((highFloat - lowFloat) * pick * RANGE_SCALE + lowFloat, -0);
            return { type: 2, bits: store32(total, negativeZero) };
        }

        // ResolveRandomJumps (0x00470C60), FindStoredRandom (0x0046F7A0),
        // NewStoredRandom (0x00470BB0) and CStoredRandom::Shuffle (0x0046F8A0).
        function choose(opcode, site, weights, owner, charge) {
            validateSite(site);
            uint(owner, "owner uint32", site);
            if (charge !== undefined && typeof charge !== "function")
                fail("type", "Expected charge callback", site);
            const cost = (n = 1) => {
                if (charge) charge(n);
            };
            const drawCharged = (n, stream = 0) => {
                cost(1);
                return draw(n >>> 0, stream);
            };
            if (!Array.isArray(weights)) fail("type", "Expected random weights array", site);
            const count = weights.length;
            if (count < 1 || count > 0xffff) fail("syntax", "Random choice count must be 1..65535", site);
            if (opcode === 0x2f || opcode === 0x37) {
                let total = 0;
                for (let i = 0; i < count; ++i) {
                    cost(1);
                    const w = weights[i];
                    if (!Number.isInteger(w) || w < 0 || w > 0xffff)
                        fail("type", "Expected uint16 random weight", site);
                    if (w & 0x8000) fail("unsupported", "Negative random weights are unsafe", site);
                    total = (total + w) | 0;
                }
                if (total <= 0) fail("syntax", "Random table has zero total weight", site);
                let pick = drawCharged(total >>> 0, opcode === 0x37 ? 1 : 0) | 0;
                for (let i = 0; i < count; ++i) {
                    cost(1);
                    pick = (pick - weights[i]) | 0;
                    if (pick < 0) return i;
                }
                fail("syntax", "Random choice escaped its table", site);
            }
            if (opcode !== 0x40 && opcode !== 0x41) fail("unsupported", "Unsupported random opcode", site);
            if (opcode === 0x41 && (count < 2 || count > 66))
                fail("unsupported", "RandomPermute requires 2..66 choices", site);
            if (owner === 0)
                fail("unsupported", "Stored random choices require a nonzero task owner", site);
            for (let i = 0; i < count; ++i) {
                cost(1);
                const w = weights[i];
                if (!Number.isInteger(w) || w < 0 || w > 0xffff)
                    fail("type", "Expected uint16 random weight", site);
            }

            const shuffle = (record, avoidRepeat) => {
                const last = record.order[count - 1];
                for (let swaps = count * 10; swaps > 0; --swaps) {
                    const a = drawCharged(count, 0) | 0;
                    const b = drawCharged(count, 0) | 0;
                    const tmp = record.order[a];
                    record.order[a] = record.order[b];
                    record.order[b] = tmp;
                }
                if (avoidRepeat && record.order[0] === last) {
                    const other = ((drawCharged(count - 1, 0) | 0) + 1) | 0;
                    const tmp = record.order[0];
                    record.order[0] = record.order[other];
                    record.order[other] = tmp;
                }
            };

            let record = null;
            for (let i = 0; i < stored.length; ++i) {
                cost(1);
                const s = stored[i];
                if (
                    s.script === site.script &&
                    s.offset === site.offset &&
                    s.opcode === opcode &&
                    s.count === count &&
                    (s.owner === owner || s.owner === 0)
                ) {
                    record = s;
                    break;
                }
            }
            if (!record) {
                const created = {
                    script: site.script,
                    offset: site.offset,
                    owner,
                    opcode,
                    count,
                    last: 0,
                    index: 0,
                    order: [],
                };
                if (opcode === 0x40) {
                    created.last = drawCharged(count, 0) & 0xffff;
                } else {
                    created.order = Array.from({ length: count }, (_, i) => i);
                    shuffle(created, false);
                }
                if (stored.length >= 100) stored.pop();
                stored.unshift(created);
                record = created;
            }
            record.owner = owner;
            if (opcode === 0x40) {
                let pick = drawCharged(count, 0);
                for (let tries = 0; (pick & 0xffff) === record.last; ) {
                    if (++tries > 100) break;
                    pick = drawCharged(count, 0);
                }
                const choice = pick & 0xffff;
                if (choice >= count) fail("syntax", "RandomNoRepeat choice escaped its table", site);
                record.last = choice;
                return choice;
            }
            if (++record.index >= count) {
                record.index = 0;
                shuffle(record, true);
            }
            return record.order[record.index];
        }

        // ForgetStoredRandomsOf (0x0046F870): mark records reusable with owner 0.
        function forget(owner) {
            uint(owner, "owner uint32");
            if (owner === 0) return;
            for (let i = 0; i < stored.length; ++i) {
                if (stored[i].owner === owner) stored[i].owner = 0;
            }
        }

        function clearStored() {
            stored.length = 0;
        }

        return Object.freeze({
            raw,
            scale,
            draw,
            state,
            seed,
            range,
            choose,
            forget,
            clearStored,
        });
    }

    return Object.freeze({
        apiVersion: 1,
        scale,
        create,
    });
})();
