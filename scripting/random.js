// NOT ORIGINAL - standalone QB RNG streams and signed-wrap scale.
// Behavior: mth/random.cpp (InitialRand 0x00403DA0, Rnd 0x00403DE0, Rnd2 0x00403E30);
// API: docs/qb-js-contract.md.
globalThis.qbRandom = (() => {
    const INITIAL_ADD = 0x0aa2fb3f;
    const INITIAL_MUL = 0x12b9b0a1;

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

        return Object.freeze({
            raw,
            scale,
            draw,
            state,
            seed,
        });
    }

    return Object.freeze({
        apiVersion: 1,
        scale,
        create,
    });
})();
