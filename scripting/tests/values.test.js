// NOT ORIGINAL - synthetic values tests; executed in QuickJS, no Node APIs.
// Concatenate values.js, this file and the existing generated scalar fixtures.
(() => {
    const v = qbValues;
    let assertions = 0;
    const assert = (condition, message) => {
        ++assertions;
        if (!condition) throw Error("Values test: " + message);
    };
    const fails = (code, fn) => {
        let error;
        try {
            fn();
        } catch (e) {
            error = e;
        }
        assert(error?.code === code, `expected ${code}, got ${error}`);
        if (error?.site)
            assert(error.message.includes("values.qb") && error.message.includes("0x17"), "fault includes QB site");
    };
    const bitBuffer = new DataView(new ArrayBuffer(4));
    const bits = (n) => {
        bitBuffer.setFloat32(0, n, true);
        return bitBuffer.getUint32(0, true);
    };
    const fromBits = (n) => {
        bitBuffer.setUint32(0, n, true);
        return bitBuffer.getFloat32(0, true);
    };
    const f = (n) => v.float(bits(n));
    const i = (n) => v.integer(n >>> 0);
    const n = v.name;
    const str = (s, local = false) =>
        v.string(
            Array.from(s, (c) => c.charCodeAt(0)),
            local,
        );
    const symbols = new Map();
    let charged = 0;
    const ctx = {
        site: { source: "values.qb", script: 100, offset: 0x17 },
        owner: 1,
        symbol: (key) => symbols.get(key),
        charge: (cost) => {
            charged += cost;
        },
        limits: { depth: 128, nodes: 250000 },
    };
    const eqBits = (actual, type, expected) =>
        assert(actual.type === type && actual.bits === expected >>> 0, "typed bits");
    const get = (p, key) => v.param(p, key, "value", ctx);
    const bin = (op, a, b) => v.binary(op, a, b, ctx);

    assert(v.apiVersion === 2, "version");
    eqBits(i(-1), 1, 0xffffffff);
    eqBits(v.float(0x80000000), 2, 0x80000000);
    eqBits(n(1), 13, 1);
    eqBits(v.copy(v.float(0x7fc00123), "value", ctx), 2, 0x7fc00123);
    for (const bad of [-1, 0x100000000, NaN, 1.25, "1"]) fails("type", () => v.integer(bad));
    fails("type", () => v.string([256]));
    fails("type", () => v.pair([0]));
    fails("type", () => v.vector([0, 0, -1]));
    const rawBytes = [65, 0, 66],
        s = v.string(rawBytes);
    rawBytes[0] = 90;
    assert(s.bytes.length === 1 && s.bytes[0] === 65, "C string termination and copy");
    assert(str("a").type === 3 && str("a", true).type === 4, "local string type distinct");

    const p = v.merge(
        [[9, i(1)]],
        [
            [9, n(5)],
            [9, f(2)],
        ],
        ctx,
    );
    assert(p.length === 2 && p[0][1].type === 13 && p[1][1].type === 2, "typed replacement and ordering");
    const flags = v.merge(
        [],
        [
            [0, n(10)],
            [0, n(11)],
            [0, n(10)],
        ],
        ctx,
    );
    assert(flags.length === 2 && flags[0][1].bits === 11 && flags[1][1].bits === 10, "unnamed name flags");
    assert(get([], 9) === undefined, "missing distinct from zero");
    eqBits(get([[9, i(0)]], 9), 1, 0);
    const parent = v.structure([[1, i(7)]], ctx),
        copied = v.copy(parent, "value", ctx),
        ref = v.copy(parent, "reference", ctx);
    assert(copied !== parent && ref.type === 20 && ref.target === parent, "owning copy vs reference");
    v.append(parent.members, [[2, i(8)]], ctx);
    eqBits(get(ref.target.members, 2), 1, 8);
    assert(get(copied.members, 2) === undefined, "mutation visible only to aliases");
    const savedMembers = parent.members,
        oldChild = v.structure([[7, parent]], ctx).members[0][1];
    v.append(savedMembers, [[3, i(9)]], ctx);
    assert(savedMembers === parent.members, "append preserves list identity");
    assert(oldChild !== parent && get(oldChild.members, 3) === undefined, "structure factory owns copy");
    const holding = v.structure([[1, ref]], ctx),
        holdingCopy = v.copy(holding, "value", ctx);
    assert(holdingCopy.members[0][1].target === parent, "existing reference survives ordinary copy");
    const dst = [[8, parent]];
    v.append(dst, [[9, i(1)]], ctx);
    assert(dst[0][1] === parent, "append does not clone untouched fields");
    function* suspended() {
        let owner = v.structure([[1, i(7)]], ctx);
        const borrowed = v.copy(owner, "reference", ctx),
            snapshot = v.copy(owner, "value", ctx);
        owner = undefined;
        yield borrowed;
        return [get(borrowed.target.members, 2), get(snapshot.members, 2)];
    }
    const suspendedValues = suspended(),
        live = suspendedValues.next().value;
    v.append(live.target.members, [[2, i(9)]], ctx);
    const resumedValues = suspendedValues.next();
    assert(
        resumedValues.done && resumedValues.value[0].bits === 9 && resumedValues.value[1] === undefined,
        "reference retains target across generator suspension",
    );
    const self = [[1, i(2)]];
    v.append(self, self, ctx);
    assert(self.length === 1, "self append bounded snapshot");
    fails("type", () => v.copy(parent, "bad", ctx));
    fails("type", () => v.merge([], [[1, undefined]], ctx));
    fails("type", () => v.copy({ type: 20, target: i(1) }, "value", ctx));

    const list = v.array(2, [i(16777217), f(-0)], ctx);
    assert(
        list.elementType === 2 && list.elements[0].bits === bits(16777216) && list.elements[1].bits === 0x80000000,
        "integer-to-float array conversion",
    );
    assert(v.array(1, [], ctx).elementType === 0, "empty array untyped");
    fails("type", () => v.array(99, [], ctx));
    fails("type", () => v.array(1, [f(1)], ctx));
    fails("type", () => v.array(0, [i(1)], ctx));
    const outer = v.array(10, [parent], ctx),
        arrayRef = v.copy(outer, "reference", ctx);
    const ownedElement = bin(5, outer, i(0)),
        borrowedElement = bin(5, arrayRef, i(0));
    assert(
        ownedElement !== outer.elements[0] &&
            borrowedElement.type === 20 &&
            borrowedElement.target === outer.elements[0],
        "array element ownership",
    );
    v.append(outer.elements[0].members, [[4, i(10)]], ctx);
    eqBits(get(borrowedElement.target.members, 4), 1, 10);
    assert(get(ownedElement.members, 4) === undefined, "element alias mutation");
    fails("arithmetic", () => bin(5, outer, i(-1)));
    fails("arithmetic", () => bin(5, outer, i(1)));
    fails("type", () => bin(5, outer, f(0)));
    fails("unsupported", () => bin(5, v.array(4, [str("x", true)], ctx), i(0)));
    const memberRef = bin(8, v.copy(v.structure([[10, parent]], ctx), "reference", ctx), n(10));
    assert(memberRef.type === 20, "referenced structure member is borrowed");
    assert(bin(8, parent, n(999)) === undefined, "missing member omitted");
    fails("type", () => bin(8, parent, i(1)));

    symbols.set(700, i(-1));
    symbols.set(701, n(700));
    symbols.set(702, n(999));
    eqBits(v.lookupGlobal(701, ctx), 1, 0xffffffff);
    eqBits(v.lookupGlobal(702, ctx), 13, 999);
    assert(v.lookupGlobal(999, ctx) === undefined, "missing global");
    eqBits(v.resolve(n(999), ctx), 13, 999);
    const included = v.structure(
        [
            [12, i(0)],
            [0, n(42)],
        ],
        ctx,
    );
    symbols.set(800, included);
    symbols.set(801, n(800));
    const params = [
        [12, i(1)],
        [0, n(801)],
        [12, str("wrong")],
    ];
    eqBits(v.lookupTyped(params, 12, 1, ctx), 1, 0);
    eqBits(v.lookupTyped([[1, n(701)]], 1, 2, ctx), 2, bits(-1));
    eqBits(v.lookupTyped([[1, i(16777217)]], 1, 2, ctx), 2, bits(16777216));
    eqBits(v.lookupTyped([[1, n(800)]], 1, 13, ctx), 13, 800);
    assert(v.lookupTyped([[1, ref]], 1, 10, ctx) === undefined, "no blanket reference normalization");
    assert(v.contains([[0, n(801)]], 12, ctx) && v.contains([[0, n(42)]], 42, ctx), "included and bare flag presence");
    assert(v.contains([[12, i(0)]], 12, ctx) && !v.contains([], 12, ctx), "zero field present");
    eqBits(
        v.readChecksum(
            [
                [0, n(42)],
                [0, n(801)],
            ],
            0,
            ctx,
        ),
        13,
        42,
    );
    assert(
        v.readChecksum([[0, n(801)]], 0, ctx) === undefined,
        "GetChecksum skips unnamed structure inclusion without expansion",
    );
    eqBits(v.readChecksum([[1, n(800)]], 1, ctx), 13, 800);
    const selected = v.readString(
        [
            [1, str("plain")],
            [1, str("local", true)],
        ],
        1,
        ctx,
    );
    assert(selected.type === 3 && selected.bytes[0] === 112, "plain String search precedes LocalString search");
    assert(v.readString([[1, str("local", true)]], 1, ctx).type === 4, "local fallback");
    const caller = [[3, parent]];
    assert(
        v.param(caller, 3, "value", ctx) !== parent && v.param(caller, 3, "expression", ctx).target === parent,
        "parameter read modes",
    );
    symbols.set(810, parent);
    assert(v.global(810, ctx).target === parent, "global expression borrows");
    eqBits(v.global(999, ctx), 13, 999);
    eqBits(v.param([[1, n(701)]], 1, "expression", ctx), 1, 0xffffffff);
    const code = { kind: "callable", type: 7, name: 820 };
    symbols.set(820, code);
    assert(v.lookupGlobal(820, ctx) === code, "callable registry descriptor");
    eqBits(v.resolve(n(820), ctx), 13, 820);
    eqBits(v.global(820, ctx),13,820);
    eqBits(bin(7,n(820),n(820)),1,1);

    symbols.set(900, n(901));
    symbols.set(901, n(900));
    fails("cycle", () => v.lookupGlobal(900, ctx));
    const recursive = { type: 10, members: [] };
    recursive.members.push([1, recursive]);
    fails("cycle", () => v.copy(recursive, "value", ctx));
    const recursiveInclude = { type: 10, members: [[0, n(910)]] };
    symbols.set(910, recursiveInclude);
    fails("cycle", () => v.lookupTyped([[0, n(910)]], 999, 1, ctx));
    fails("cycle", () => v.contains([[0, n(910)]], 999, ctx));
    fails("limit", () => v.copy(parent, "value", { ...ctx, limits: { depth: 0, nodes: 100 } }));
    fails("limit", () =>
        v.merge(
            [],
            [
                [1, i(1)],
                [2, i(2)],
            ],
            { ...ctx, limits: { depth: 128, nodes: 3 } },
        ),
    );
    fails("type", () => v.copy({ type: 12, elementType: 1, elements: [str("x")] }, "value", ctx));
    fails("unsupported", () => v.copy({ type: 99 }, "value", ctx));
    fails("limit", () => v.merge(new Array(4097).fill([1, i(1)]), [], ctx));
    fails("type", () => v.merge([], [[1, i(1)]], { ...ctx, owner: -1 }));

    eqBits(bin(11, i(-1), i(1)), 1, 0);
    eqBits(bin(10, i(0), i(1)), 1, 0xffffffff);
    eqBits(bin(13, i(0x7fffffff), i(3)), 1, 0x7ffffffd);
    eqBits(bin(12, i(-7), i(2)), 1, 0xfffffffd);
    eqBits(bin(11, i(1), f(0.5)), 2, bits(1.5));
    eqBits(bin(13, f(1.5), i(2)), 2, bits(3));
    eqBits(bin(12, f(3), i(2)), 2, bits(1.5));
    eqBits(bin(10, f(1), i(2)), 2, bits(-1));
    // Exact product n*m has 55 significant bits. Binary64 first rounds it
    // to a float32 halfway point; x87 precision-64 retains the deciding bit.
    eqBits(bin(13, i(1090519041), v.float(0x3fffffff)), 2, 0x4f01ffff);
    assert(bits(1090519041 * fromBits(0x3fffffff)) === 0x4f020000, "independent naive-double counterexample");
    eqBits(bin(13, f(-0), f(1)), 2, 0x80000000);
    eqBits(bin(11, f(-0), f(-0)), 2, 0x80000000);
    eqBits(bin(11, f(1), f(-1)), 2, 0);
    eqBits(bin(12, v.float(1), f(2)), 2, 0);
    eqBits(bin(12, v.float(3), f(2)), 2, 2);
    eqBits(bin(13, v.float(0x80000001), f(0.5)), 2, 0x80000000);
    eqBits(bin(12, f(1), f(1e-6)), 2, bits(1000000));
    eqBits(bin(13, v.float(0x7f7fffff), f(2)), 2, 0x7f800000);
    eqBits(bin(11, n(0xffffffff), i(2)), 13, 1);
    fails("arithmetic", () => bin(12, i(1), i(0)));
    fails("arithmetic", () => bin(12, f(1), f(1e-7)));
    fails("arithmetic", () => bin(12, i(-2147483648), i(-1)));
    fails("unsupported", () => bin(11, v.float(0x7f800000), i(1)));
    eqBits(bin(50, i(2), i(1)), 1, 3);
    eqBits(bin(51, i(2), i(1)), 1, 0);
    eqBits(bin(50, v.float(0x80000000), i(0)), 1, 0);
    eqBits(bin(50, f(NaN), i(0)), 1, 1);
    assert(v.statementTruth(v.float(0x80000000), ctx), "statement truth uses raw -0 bits");
    assert(!v.statementTruth(i(0), ctx) && v.statementTruth(str(""), ctx), "zero vs nonnull empty-string payload");
    assert(!v.statementTruth(undefined,ctx),"empty statement is false");
    fails("unsupported", () => v.statementTruth(n(1), ctx));
    eqBits(bin(7, f(-0), f(0)), 1, 1);
    eqBits(bin(7, i(1), f(1)), 1, 1);
    eqBits(bin(7, f(NaN), f(NaN)), 1, 0);
    eqBits(bin(7, str("A"), str("a")), 1, 0);
    eqBits(bin(7, str("a"), str("a", true)), 1, 0);
    eqBits(bin(7, parent, parent), 1, 1);
    eqBits(bin(7, parent, copied), 1, 0);
    eqBits(bin(7, ref, ref), 1, 0);
    eqBits(bin(7, n(701), i(-1)), 1, 1);
    eqBits(bin(18, str("z"), str("aa")), 1, 1);
    eqBits(bin(20, f(2), i(1)), 1, 1);
    const small = v.structure([[1, i(1)]], ctx),
        large = v.structure(
            [
                [1, i(1)],
                [2, i(2)],
            ],
            ctx,
        );
    eqBits(bin(18, small, large), 1, 1);
    eqBits(bin(20, large, small), 1, 1);
    eqBits(bin(18, small, small), 1, 1);
    const joined = bin(11, str("ab"), str("cd"));
    assert(joined.bytes.join(",") === "97,98,99,100", "string addition");
    fails("arithmetic", () => bin(11, v.string(new Array(199).fill(65)), str("x")));
    const combined = bin(11, small, large);
    assert(combined.members.length === 2, "structure addition");
    assert(bin(10, large, n(1)).members.length === 1, "structure minus name");
    assert(bin(10, large, v.array(13, [n(1)], ctx)).members.length === 1, "structure minus name array");
    assert(bin(10, large, small).members.length === 1, "structure minus typed fields");
    const concatenated = bin(11, v.array(1, [i(1)], ctx), v.array(1, [i(2)], ctx));
    assert(concatenated.elements.length === 2, "array concatenation");
    fails("type", () => bin(11, v.array(1, [i(1)], ctx), v.array(2, [f(1)], ctx)));
    const a = v.pair([bits(1), bits(2)]),
        b = v.pair([bits(3), bits(4)]);
    const vec = v.vector([bits(1), bits(2), bits(3)]);
    assert(v.copy(vec, "value", ctx).coordinates.join(",") === vec.coordinates.join(","), "vector bit copy");
    assert(
        bin(11, vec, vec).coordinates.join(",") === [bits(2), bits(4), bits(6)].join(","),
        "vector coordinate addition",
    );
    assert(bin(11, a, b).coordinates.join(",") === [bits(4), bits(6)].join(","), "coordinate addition");
    assert(bin(10, b, a).coordinates.join(",") === [bits(2), bits(2)].join(","), "coordinate subtraction");
    eqBits(bin(7, a, v.copy(a, "value", ctx)), 1, 1);
    fails("unsupported", () => bin(8, a, b)); // Compound coordinate dot/cross/scaling await precision tests.
    fails("unsupported", () => bin(0xffff, i(1), i(1)));
    assert(bin(11,undefined,i(1))===undefined,"empty operand propagates");
    for(const op of [7,18,20,50,51]) {
        assert(bin(op,undefined,i(1))===undefined,'empty left operand propagates');
        assert(bin(op,i(1),undefined)===undefined,'empty right operand propagates');
    }
    eqBits(bin(18,n(820),n(820)),1,0);
    eqBits(bin(20,n(820),n(820)),1,0);
    assert(charged > 0, "all contextual work charged");
    globalThis.qbValuesTestResult = { assertions, charged, apiVersion: v.apiVersion };
})();
