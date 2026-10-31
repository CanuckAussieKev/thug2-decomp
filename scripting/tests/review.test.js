// NOT ORIGINAL - independent expectations for maintainer review regressions.
(() => {
    const check = (ok, why) => {
        if (!ok) throw Error("Review regression: " + why);
    };
    const v = qbValues,
        n = qbNumeric;
    const crc = (text) => {
        let c = 0xffffffff;
        for (let x of text) {
            let b = x.charCodeAt(0);
            if (b >= 65 && b <= 90) b += 32;
            c ^= b;
            for (let i = 0; i < 8; i++) c = (c >>> 1) ^ (c & 1 ? 0xedb88320 : 0);
        }
        return c >>> 0;
    };
    let work = 0;
    const ctx = {
        site: { source: "review.qb", script: 1, offset: 7 },
        owner: 1,
        symbol: () => undefined,
        charge: (k) => {
            work += k;
        },
    };
    const large = v.fields(
        [],
        Array.from({ length: 4096 }, (_, i) => () => [i + 1, v.integer(i)]),
        ctx,
    );
    check(large.length === 4096 && large[4095][1].bits === 4095, "documented field cap is reachable");
    check(work < 50000, "field construction has linear work cost");
    let limited = false;
    try {
        v.fields(
            [],
            Array.from({ length: 4097 }, (_, i) => () => [i + 1, v.integer(i)]),
            ctx,
        );
    } catch {
        limited = true;
    }
    check(limited, "field cap still enforced");
    check(v.binary(11, undefined, v.integer(1), ctx) === undefined, "empty operand propagates");
    check(!v.statementTruth(undefined, ctx), "empty condition is false");
    check(v.fields([], [() => [1, undefined]], ctx).length === 0, "empty expression omits argument");
    check(
        n.store32(n.round((1n << 80n) + (1n << 56n) + 1n, 1n, -80, 24)) === 0x3f800001,
        "precision24 avoids double rounding",
    );
    for (const precision of [53, 64])
        check(
            n.store32(n.round((1n << 80n) + (1n << 56n) + 1n, 1n, -80, precision)) === 0x3f800000,
            "offline double rounding profile",
        );
    check(n.store32({ m: 1n, e: -149 }) === 1, "minimum subnormal");
    check(n.store32({ m: 1n, e: -150 }) === 0, "subnormal tie rounds even");
    check(n.store32({ m: 3n, e: -150 }) === 2, "odd subnormal tie");
    check(n.store32({ m: 0n, e: 0 }, true) === 0x80000000, "negative zero");
    check(n.store32({ m: 1n, e: 128 }) === 0x7f800000, "overflow");
    for (const fn of [
        () => n.round(1n, 0n, 0),
        () => n.round(1n, 1n, 0, 25),
        () => n.round(1n << 4096n, 1n, 0),
        () => n.store32({ m: 1n, e: 99999 }),
    ]) {
        let failed = false;
        try {
            fn();
        } catch {
            failed = true;
        }
        check(failed, "numeric input limits");
    }
    const run = (name) => {
        const id = qb.start(crc(name), [], 100000);
        try {
            return qb.update(id, 0);
        } finally {
            qb.cancel(id);
        }
    };
    let result = run("reviewAngles");
    check(
        result.status === "finished" && !result.parameters.some(([name]) => name === crc("angles")),
        "zero literal angles omitted",
    );
    check(
        result.parameters.some(([name, value]) => name === crc("kept") && value.type === 6),
        "other zero vectors retained",
    );
    result = run("reviewExpressionAngles");
    check(
        result.parameters.some(([name, value]) => name === crc("angles") && value.type === 6),
        "expression vector keeps AddNamedExpression semantics",
    );
    result = run("reviewAssignmentAngles");
    check(
        result.parameters.some(([name, value]) => name === crc("angles") && value.type === 6),
        "assignment keeps ParseValue semantics",
    );
    result = run("reviewScriptValue");
    check(
        result.status === "finished" &&
            result.parameters.some(
                ([name, value]) => name === crc("callback") && value.type === 13 && value.bits === crc("reviewAngles"),
            ),
        "QScript expression is a Name without executing it",
    );
    const qctx = {
        ...ctx,
        symbol: (key) =>
            key === 10
                ? { kind: "callable", type: 7, name: 10 }
                : key === 11
                  ? { kind: "callable", type: 7, name: 11 }
                  : undefined,
    };
    check(v.binary(7, v.name(10), v.name(10), qctx).bits === 1, "same script symbols compare equal");
    check(v.binary(7, v.name(10), v.name(11), qctx).bits === 0, "different script symbols compare unequal");
    check(v.binary(18, v.name(10), v.name(11), qctx).bits === 0, "script ordering returns false");
    const savedHost = globalThis.__qbHost;
    try {
        globalThis.__qbHost = () => undefined;
        qb.define(0xf0000100, function* (p) {
            qb.frame(p, true);
            const found = yield* qb.call(0xf000ffff, [], p, ctx.site);
            return [[1, v.integer(found ? 1 : 0)]];
        });
        let id = qb.start(0xf0000100, [], 100);
        result = qb.update(id, 0);
        qb.cancel(id);
        check(
            result.status === "finished" && result.parameters[0][1].bits === 0,
            "missing command is false, not a fault",
        );
        qb.defineGlobal(0xf0000101, v.integer(8), ctx.site);
        globalThis.__qbHost = () => {
            throw Error("data symbol dispatched as host");
        };
        qb.define(0xf0000102, function* (p) {
            qb.frame(p, true);
            return [[1, v.integer((yield* qb.call(0xf0000101, [], p, ctx.site)) ? 1 : 0)]];
        });
        id = qb.start(0xf0000102, [], 100);
        result = qb.update(id, 0);
        qb.cancel(id);
        check(result.status === "finished" && result.parameters[0][1].bits === 0, "data symbol is not a command");
        qb.define(0xf0000103, function* (p) {
            qb.frame(p, true);
            for (;;) {
                qb.step(ctx.site);
                yield { frames: 1 };
            }
        });
        id = qb.start(0xf0000103, [], 2);
        for (let frame = 0; frame < 128; frame++)
            check(qb.update(id, frame).status === "waiting", "fuel refills on every resume");
        qb.cancel(id);
    } finally {
        globalThis.__qbHost = savedHost;
    }
    globalThis.qbReviewTestResult = { completed: true };
})();
