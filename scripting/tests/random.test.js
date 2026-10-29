// NOT ORIGINAL - synthetic RNG stream and scale tests; executed in QuickJS, no Node APIs.
// Reference functions: InitialRand (0x00403DA0), Rnd (0x00403DE0), Rnd2 (0x00403E30).
(() => {
    const r = qbRandom;
    let assertions = 0;
    const assert = (condition, message) => {
        ++assertions;
        if (!condition) throw Error("Random test: " + message);
    };
    const fails = (code, fn) => {
        let error;
        try {
            fn();
        } catch (e) {
            error = e;
        }
        assert(error?.code === code, `expected ${code}, got ${error}`);
    };
    const eqState = (actual, expected, label) => {
        assert(
            actual.add === expected[0] && actual.mul === expected[1] && actual.value === expected[2],
            `${label}: got ${actual.add.toString(16)}/${actual.mul.toString(16)}/${actual.value.toString(16)}`,
        );
    };

    assert(r.apiVersion === 1, "apiVersion");
    const rng = r.create();
    eqState(rng.state(0), [0x0aa2fb3f, 0x12b9b0a1, 0], "default seed stream 0");
    eqState(rng.state(1), [0x0aa2fb3f, 0x12b9b0a1, 0], "default seed stream 1");

    const snapshot = rng.state(0);
    snapshot.value = 123456;
    assert(rng.state(0).value === 0, "state returns a defensive copy");

    for (const badSeed of [-1, 0x100000000, 1.5, NaN, "0", null]) {
        fails("type", () => r.create(badSeed));
        fails("type", () => rng.seed(badSeed));
    }
    for (const badStream of [-1, 2, 0.5, NaN, "0", true, null]) {
        fails("type", () => rng.raw(badStream));
        fails("type", () => rng.draw(10, badStream));
        fails("type", () => rng.state(badStream));
    }
    for (const badSample of [-1, 0x10000, 1.5, NaN, "1"]) {
        fails("type", () => r.scale(badSample, 10));
        fails("type", () => rng.scale(badSample, 10));
    }
    for (const badRange of [-1, 0x100000000, 1.5, NaN, "10"]) {
        fails("type", () => r.scale(1, badRange));
        fails("type", () => rng.draw(badRange, 0));
    }

    assert(r.scale(0, 100) === 0, "scale zero sample");
    assert(r.scale(0xffff, 0) === 0, "scale zero range");
    assert(r.scale(0x8000, 2) === 1, "scale midpoint");
    assert(r.scale(0xffff, 0xffff) === 0xfffffffe, "scale signed wrap overflow");
    assert(r.scale(0x8001, 0x80000000) === 0xffff8000, "scale negative range high bit");
    assert(r.scale(1, 0xffffffff) === 0xffffffff, "scale -1 range sign extension");

    const vectors = [
        {
            seed: 0x0,
            draws: [0x62, 0x742, 0x401, 0xffffffff, 0xffffffff, 0xffff8000],
            state: [0xa2d2eb0f, 0x98a6ecd3, 0xa3fdeb33],
        },
        {
            seed: 0x1,
            draws: [0x43, 0xa65, 0x4eb3, 0xffffffff, 0xffffffff, 0x0],
            state: [0xbee98100, 0x8098741e, 0x524b08d6],
        },
        {
            seed: 0x7b,
            draws: [0x54, 0x5f15, 0x5616, 0x7fff, 0xffffffff, 0x0],
            state: [0xa76d9e2d, 0xe00038dc, 0xae6235ac],
        },
        {
            seed: 0x7fffffff,
            draws: [0x1d, 0x2bd9, 0xffffa281, 0xffffffff, 0xffffffff, 0xffff8000],
            state: [0xe40ecb77, 0xc121b101, 0xd4eafbc3],
        },
        {
            seed: 0x80000000,
            draws: [0x62, 0x742, 0x401, 0xffffffff, 0xffffffff, 0xffff8000],
            state: [0x91e5eb0f, 0x2c3b6cd3, 0xe6cdeb33],
        },
        {
            seed: 0xffffffff,
            draws: [0x1d, 0x2bd9, 0xffffa281, 0xffffffff, 0xffffffff, 0xffff8000],
            state: [0x9a0ecb77, 0xe421b101, 0xa4eafbc3],
        },
    ];
    const ranges = [100, 32768, 65535, 0x7fffffff, 0xffffffff, 0x80000000];
    for (const v of vectors) {
        const a = r.create(v.seed);
        const b = r.create(999);
        b.seed(v.seed);
        for (let i = 0; i < ranges.length; ++i) {
            const d0 = a.draw(ranges[i], 0);
            const s1 = b.raw(1);
            const d1 = b.scale(s1, ranges[i]);
            assert(d0 === v.draws[i], `seed ${v.seed.toString(16)} draw ${i}`);
            assert(d1 === v.draws[i], `seed ${v.seed.toString(16)} raw+scale ${i}`);
        }
        eqState(a.state(0), v.state, `seed ${v.seed.toString(16)} stream 0 final state`);
        eqState(a.state(1), [0x0aa2fb3f, 0x12b9b0a1, v.seed], `seed ${v.seed.toString(16)} stream 1 untouched`);
        eqState(b.state(1), v.state, `seed ${v.seed.toString(16)} stream 1 final state`);
        eqState(b.state(0), [0x0aa2fb3f, 0x12b9b0a1, v.seed], `seed ${v.seed.toString(16)} stream 0 untouched`);
    }

    const interleaved = r.create(123);
    const stream0Only = r.create(123);
    const stream1Only = r.create(123);
    for (let i = 0; i < 300; ++i) {
        const range = ranges[i % ranges.length];
        if (i % 3 === 0) {
            assert(interleaved.draw(range, 1) === stream1Only.draw(range, 1), `interleaved stream 1 step ${i}`);
        } else {
            assert(interleaved.draw(range, 0) === stream0Only.draw(range, 0), `interleaved stream 0 step ${i}`);
        }
    }
    eqState(interleaved.state(0), [stream0Only.state(0).add, stream0Only.state(0).mul, stream0Only.state(0).value], "interleaved final 0");
    eqState(interleaved.state(1), [stream1Only.state(1).add, stream1Only.state(1).mul, stream1Only.state(1).value], "interleaved final 1");
    assert(assertions >= 100, "expected stream and scale assertion coverage");
})();
