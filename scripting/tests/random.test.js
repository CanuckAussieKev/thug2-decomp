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

    const floatView = new DataView(new ArrayBuffer(4));
    const fbits = (n) => {
        floatView.setFloat32(0, n, true);
        return floatView.getUint32(0, true);
    };
    const fval = (b) => {
        floatView.setUint32(0, b, true);
        return floatView.getFloat32(0, true);
    };

    const ra = r.create(123);
    const rb = r.create(123);
    const equalWhole = ra.range(fbits(2), fbits(2));
    assert(equalWhole.type === 1 && equalWhole.bits === 2, "equal whole bounds give integer 2");
    const model = r.create(123);
    model.draw(1, 0);
    const afterEqual = ra.range(fbits(0), fbits(32767));
    assert(afterEqual.type === 1 && afterEqual.bits === model.draw(32768, 0), "equal whole bounds still consume one draw");
    const secondStream = ra.range(fbits(0), fbits(32767), true);
    const untouchedSecond = rb.range(fbits(0), fbits(32767), true);
    assert(secondStream.type === 1 && secondStream.bits === untouchedSecond.bits, "range second stream is independent");

    ra.seed(55);
    rb.seed(55);
    const forward = ra.range(fbits(-5), fbits(6));
    const reverse = rb.range(fbits(6), fbits(-5));
    assert(forward.type === 1 && reverse.type === 1 && forward.bits === reverse.bits, "reversed whole endpoints match");

    const atTolerance = ra.range(0x3727c5ac, 0x3727c5ac); // 1e-5f
    assert(atTolerance.type === 1 && atTolerance.bits === 0, "whole classification includes exact 1e-5f tolerance");
    const justOutside = 0x3727c5ad; // nextafter(1e-5f, +inf)
    const outsideResult = ra.range(justOutside, justOutside);
    assert(outsideResult.type === 2 && outsideResult.bits === justOutside, "next float outside tolerance remains float");

    ra.seed(3264);
    const overshoot = ra.range(fbits(-0.5), fbits(0.5));
    assert(overshoot.type === 2 && overshoot.bits === 0x3f000000, "gameplay precision rounds the overshoot to the endpoint");

    const equalFraction = ra.range(fbits(0.5), fbits(0.5));
    assert(equalFraction.type === 2 && equalFraction.bits === fbits(0.5), "equal fractional bounds preserve float bits");

    const negZeroFixed = ra.range(0x80000000, 0x80000000);
    assert(negZeroFixed.type === 1 && negZeroFixed.bits === 0, "-0.0 whole endpoints classify as integer 0");

    const extreme = ra.range(0xff7fffff, 0x7f7fffff);
    assert(extreme.type === 2 && Number.isFinite(fval(extreme.bits)), "large finite bounds keep finite x87 interpolation");

    const wrappedInt = ra.range(fbits(-2147483648.0), fbits(2147483520.0));
    assert(wrappedInt.type === 1, "extreme int32 bounds wrap width without overflow trap");

    const aboveInt32 = ra.range(fbits(2147483648.0), fbits(2147483648.0));
    assert(aboveInt32.type === 2 && aboveInt32.bits === fbits(2147483648.0), "2^31 endpoint exceeds int32 and takes float path");

    for (const badBits of [-1, 0x100000000, 1.5, NaN, "0"]) {
        fails("type", () => ra.range(badBits, 0));
        fails("type", () => ra.range(0, badBits));
    }
    fails("type", () => ra.range(0, 0, 1));
    fails("type", () => ra.range(0, 0, false, "not-a-fn"));

    ra.seed(77);
    rb.seed(77);
    for (const nonfinite of [0x7f800000, 0xff800000, 0x7fc00000, 0xffffffff]) {
        fails("arithmetic", () => ra.range(nonfinite, 0));
        fails("arithmetic", () => ra.range(0, nonfinite));
    }
    assert(ra.range(0, fbits(32767)).bits === rb.range(0, fbits(32767)).bits, "nonfinite bounds fault before consuming RNG");

    ra.seed(88);
    rb.seed(88);
    let charged = 0;
    let chargeFailed = false;
    try {
        ra.range(fbits(1), fbits(6), false, (n) => {
            charged += n;
            throw new Error("fuel exhausted");
        });
    } catch {
        chargeFailed = true;
    }
    assert(chargeFailed && charged === 1, "range charges work before drawing");
    assert(ra.range(0, fbits(32767)).bits === rb.range(0, fbits(32767)).bits, "failed charge does not advance RNG stream");

    const site = (script = 1, offset = 0x20) => ({ source: "random.qb", script, offset });
    const selector = r.create(55);
    for (let i = 0; i < 100; ++i) {
        assert(selector.choose(0x2f, site(1, 0), [0, 1, 0], 1) === 1, "zero-weight alternatives skipped");
    }
    const selA = r.create(777);
    const selB = r.create(777);
    for (let i = 0; i < 40; ++i) {
        const c0 = selA.choose(0x2f, site(1, 10), [1, 2, 7], 1);
        const c1 = selB.choose(0x37, site(1, 10), [1, 2, 7], 1);
        assert(c0 === c1, "0x2f and 0x37 draw identical sequence on freshly seeded streams");
    }
    eqState(selA.state(1), [0x0aa2fb3f, 0x12b9b0a1, 777], "0x2f leaves stream 1 untouched");
    eqState(selB.state(0), [0x0aa2fb3f, 0x12b9b0a1, 777], "0x37 leaves stream 0 untouched");

    // Signed Scale overflow on total weight 65534 (32767 + 32767) when sample >= 32769:
    // negative pick selects index 0 even if index 0 has weight 0.
    const overflowSel = r.create(0x7fffffff);
    overflowSel.raw(0);
    overflowSel.raw(0);
    assert(overflowSel.choose(0x2f, site(1, 12), [0, 32767, 32767], 1) === 0, "signed Scale overflow picks entry 0");

    let previous = -1;
    for (let i = 0; i < 100; ++i) {
        const pick = selector.choose(0x40, site(1, 1), [1, 1], 1);
        assert((pick === 0 || pick === 1) && pick !== previous, "NoRepeat consecutive picks differ");
        previous = pick;
    }

    let retryCharges = 0;
    const singlePick = selector.choose(0x40, site(1, 2), [0], 1, (n) => {
        retryCharges += n;
    });
    assert(singlePick === 0 && retryCharges >= 102, "single NoRepeat choice hits bounded 100-retry fallback");

    const permute = r.create(123);
    const picks = [];
    for (let i = 0; i < 20; ++i) {
        picks.push(permute.choose(0x41, site(2, 0), [1, 1, 1], 1));
    }
    // Initial shuffle sets index=0 and first choose increments to index=1, so first cycle has 2 remaining items,
    // and every subsequent 3-pick cycle is a full permutation of {0, 1, 2} whose first element != previous last.
    for (let i = 2; i + 3 <= picks.length; i += 3) {
        const cycle = picks.slice(i, i + 3).sort((x, y) => x - y);
        assert(cycle[0] === 0 && cycle[1] === 1 && cycle[2] === 2, "complete permutation cycle");
        assert(picks[i] !== picks[i - 1], "permutation reshuffle avoids repeating last element");
    }

    // Owner isolation, forget(owner) reuse, seed retention and clearStored().
    const owners = r.create(321);
    const pOwner1 = owners.choose(0x41, site(5, 0x10), [1, 1, 1], 1);
    let lookupCost = 0;
    owners.choose(0x41, site(5, 0x10), [1, 1, 1], 2, (n) => {
        lookupCost += n;
    });
    // Scanned 1 record (1) + init count (3) + 30 swaps * 2 draws (60) = 64.
    assert(lookupCost === 64, "second owner scans 1 existing record then initializes its own");

    owners.forget(1);
    lookupCost = 0;
    const pReused = owners.choose(0x41, site(5, 0x10), [1, 1, 1], 3, (n) => {
        lookupCost += n;
    });
    // Weight validation (3) + scan 2 records (2) + advance index from 1 to 2 with 0 draws = 5.
    assert(lookupCost === 5 && pReused !== pOwner1, "forgotten record is adopted by owner 3 without re-init");

    // Warmed stored-choice record charges per weight element and does not advance RNG/history if fuel fails first.
    const bigWeights = new Array(16384).fill(1);
    const warmRng = r.create(1);
    const firstWarm = warmRng.choose(0x40, site(6, 0), bigWeights, 1);
    let warmCharged = 0;
    warmRng.choose(0x40, site(6, 0), bigWeights, 1, (n) => {
        warmCharged += n;
    });
    assert(warmCharged >= 16386, "warmed stored-choice call charges for all validated weights");

    const beforeFailState = warmRng.state(0);
    let limitedSteps = 0;
    let warmFailed = false;
    try {
        warmRng.choose(0x40, site(6, 0), bigWeights, 1, (n) => {
            limitedSteps += n;
            if (limitedSteps > 100) throw new Error("fuel exhausted");
        });
    } catch {
        warmFailed = true;
    }
    assert(warmFailed && limitedSteps === 101, "insufficient fuel aborts during weight validation on warmed record");
    eqState(
        warmRng.state(0),
        [beforeFailState.add, beforeFailState.mul, beforeFailState.value],
        "insufficient fuel before draw leaves RNG state untouched",
    );
    const refRng = r.create(1);
    assert(refRng.choose(0x40, site(6, 0), bigWeights, 1) === firstWarm, "baseline first warm pick");
    refRng.choose(0x40, site(6, 0), bigWeights, 1);
    assert(
        warmRng.choose(0x40, site(6, 0), bigWeights, 1) === refRng.choose(0x40, site(6, 0), bigWeights, 1),
        "aborted warmed call leaves stored history untouched",
    );

    // Seed reinitializes streams while retaining stored-choice history.
    owners.seed(321);
    lookupCost = 0;
    owners.choose(0x40, site(5, 0x20), [1, 1], 3, (n) => {
        lookupCost += n;
    });
    // Scanned the 2 retained 0x41 records (2) + init count (2) + initial draw (1) + pick draw (1+).
    assert(lookupCost >= 6, "seed retains stored history across reseed");

    // History cap of 100 records with oldest-insertion eviction (not LRU).
    const capRng = r.create(99);
    for (let i = 0; i < 100; ++i) {
        capRng.choose(0x40, site(10, i), [1, 1], 1);
    }
    // Touch the oldest record (offset 0): it must still be evicted next because eviction is oldest-insertion, not LRU.
    capRng.choose(0x40, site(10, 0), [1, 1], 1);
    capRng.choose(0x40, site(10, 100), [1, 1], 1);
    let costAfterEvict = 0;
    capRng.choose(0x40, site(10, 0), [1, 1], 1, (n) => {
        costAfterEvict += n;
    });
    // Scanned all 100 records (cost 100) + initialized new record of count 2 (cost 2) + initial draw (1) + pick draw (1+).
    assert(costAfterEvict >= 104, "oldest inserted record evicted at 100-record cap despite recent use");

    capRng.clearStored();
    let costAfterClear = 0;
    capRng.choose(0x40, site(10, 100), [1, 1], 1, (n) => {
        costAfterClear += n;
    });
    assert(costAfterClear >= 4 && costAfterClear < 100, "clearStored drops all stored records");

    // Aborted initialization when fuel runs out leaves no partial stored record.
    const aborted = r.create(42);
    let abortFailed = false;
    let stepCount = 0;
    try {
        aborted.choose(0x41, site(7, 0), [1, 1, 1], 1, () => {
            if (++stepCount > 2) throw new Error("out of fuel");
        });
    } catch {
        abortFailed = true;
    }
    assert(abortFailed, "fuel exhaustion aborts permutation init");
    let costVerifyEmpty = 0;
    aborted.choose(0x40, site(7, 1), [1, 1], 1, (n) => {
        costVerifyEmpty += n;
    });
    // Zero existing records scanned (0) + init count (2) + init draw (1) + pick draw (1+).
    assert(costVerifyEmpty >= 4, "aborted initialization did not leave a stored record");

    // Validation and unsupported cases.
    fails("type", () => selector.choose(0x2f, null, [1], 1));
    fails("type", () => selector.choose(0x2f, { source: "", script: 1, offset: 0 }, [1], 1));
    fails("type", () => selector.choose(0x2f, site(1, 0), "not-array", 1));
    fails("syntax", () => selector.choose(0x2f, site(1, 0), [], 1));
    fails("syntax", () => selector.choose(0x2f, site(1, 0), [0, 0], 1));
    fails("type", () => selector.choose(0x2f, site(1, 0), [-1], 1));
    fails("type", () => selector.choose(0x2f, site(1, 0), [0x10000], 1));
    fails("unsupported", () => selector.choose(0x2f, site(1, 0), [0x8000], 1));
    fails("unsupported", () => selector.choose(0x2f, site(1, 0), [0xffff], 1));
    fails("unsupported", () => selector.choose(0x41, site(1, 0), [1], 1));
    fails("unsupported", () => selector.choose(0x41, site(1, 0), new Array(67).fill(1), 1));
    fails("unsupported", () => selector.choose(0x40, site(1, 0), [1, 1], 0));
    fails("unsupported", () => selector.choose(0x41, site(1, 0), [1, 1], 0));
    fails("unsupported", () => selector.choose(0x99, site(1, 0), [1, 1], 1));
    fails("type", () => selector.forget(-1));
    fails("type", () => selector.forget(0x100000000));
    selector.forget(0);

    assert(assertions >= 250, "expected full random assertion coverage");
    globalThis.qbRandomTestResult = {assertions,apiVersion:r.apiVersion};
})();
