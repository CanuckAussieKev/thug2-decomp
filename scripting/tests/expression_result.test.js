// NOT ORIGINAL - independent GetResult expectations corroborated by x86 oracle.
(() => {
    const v = qbValues;
    let assertions = 0;
    const assert = (ok, why) => {
        ++assertions;
        if (!ok) throw Error("Expression result: " + why);
    };
    const ctx = {
        site: { source: "expression-result.qb", script: 1, offset: 0x17 },
        owner: 1,
        symbol: () => undefined,
        charge() {},
    };
    const integer = (bits) => ({ type: 1, bits });
    const array = () => ({ type: 12, elementType: 1, elements: [integer(7)] });
    const expect = (code, fn) => {
        let error;
        try { fn(); } catch (e) { error = e; }
        assert(error?.code === code, "expected " + code + ", got " + error);
        assert(error?.site?.offset === 0x17, "error retains completion site");
    };
    for (const original of [integer(0xffffffff), { type: 2, bits: 0x80000000 }, { type: 13, bits: 0x12345678 }]) {
        const out = v.expressionResult(original, ctx);
        assert(out !== original && out.type === original.type && out.bits === original.bits, "raw scalar copy");
    }
    for (const original of [
        { type: 3, bytes: [255, 128] }, { type: 4, bytes: [65] },
        { type: 5, coordinates: [0x80000000, 0x3f800000] },
        { type: 6, coordinates: [0, 0x3f800000, 0x7fc00123] },
    ]) {
        const out = v.expressionResult(original, ctx);
        assert(out !== original && JSON.stringify(out) === JSON.stringify(original), "payload/tag copy");
        assert((out.bytes ?? out.coordinates) !== (original.bytes ?? original.coordinates), "detached payload");
    }
    const a = array(), borrow = Object.freeze({ type: 21, target: a });
    const ordinary = v.copy(borrow, "value", ctx);
    const completed = v.expressionResult(borrow, ctx);
    assert(ordinary.type === 21 && ordinary.target === a, "ordinary reference copy remains borrowed");
    assert(completed.type === 12 && completed !== a, "completed array becomes owning");
    a.elements[0].bits = 9;
    assert(ordinary.target.elements[0].bits === 9 && completed.elements[0].bits === 7, "live versus detached read");
    assert(v.binary(7, borrow, ordinary, ctx).bits === 0, "reference ComponentsEqual stays false");
    assert(borrow.type === 21 && borrow.target === a, "completion never retags source");
    const ownedChild = array();
    const structure = { type: 10, members: [[1, { type: 21, target: a }], [2, ownedChild]] };
    const result = v.expressionResult({ type: 20, target: structure }, ctx);
    assert(result.type === 10 && result !== structure && result.members !== structure.members, "outer structure copy");
    assert(result.members[0][1].type === 21 && result.members[0][1].target === a, "nested reference keeps target");
    assert(result.members[1][1] !== ownedChild, "nested owner is copied");
    a.elements[0].bits = 11;
    ownedChild.elements[0].bits = 12;
    structure.members.push([3, integer(77)]);
    assert(result.members[0][1].target.elements[0].bits === 11, "nested reference observes mutation");
    assert(result.members[1][1].elements[0].bits === 7 && result.members.length === 2, "owned state stays historical");
    const innerStructure = { type: 10, members: [[1, integer(7)]] };
    const withStructureRef = { type: 10, members: [[2, { type: 20, target: innerStructure }]] };
    assert(v.expressionResult({ type: 20, target: withStructureRef }, ctx).members[0][1].target === innerStructure,
        "nested structure reference keeps target");
    const nestedArray = { type: 12, elementType: 12, elements: [array()] };
    const nestedCopy = v.expressionResult({ type: 21, target: nestedArray }, ctx);
    assert(nestedCopy !== nestedArray && nestedCopy.elements[0] !== nestedArray.elements[0], "nested owned arrays detach");
    for (const empty of [{ type: 10, members: [] }, { type: 12, elementType: 0, elements: [] }]) {
        const out = v.expressionResult(empty, ctx);
        assert(out !== empty && JSON.stringify(out) === JSON.stringify(empty), "empty owner copy");
    }
    assert(v.expressionResult(undefined, ctx) === undefined, "absent result is omitted");
    expect("unsupported", () => v.expressionResult({ type: 8, bits: 0 }, ctx));
    expect("type", () => v.expressionResult({ type: 20, target: a }, ctx));
    expect("limit", () => v.expressionResult(borrow, { ...ctx, limits: { nodes: 1 } }));
    assert(borrow.type === 21 && a.elements[0].bits === 11, "budget failure preserves input");
    const cycle = { type: 10, members: [] };
    cycle.members.push([1, cycle]);
    expect("cycle", () => v.expressionResult({ type: 20, target: cycle }, ctx));
    const refCycle = { type: 10, members: [] };
    refCycle.members.push([1, { type: 20, target: refCycle }]);
    assert(v.expressionResult({ type: 20, target: refCycle }, ctx).members[0][1].target === refCycle,
        "reference back-edge is preserved without recursive export");
    expect("type", () => v.expressionResult({ type: 12, elementType: 1, elements: [{ type: 3, bytes: [65] }] }, ctx));
    const duplicates = { type: 10, members: [
        [9, integer(1)], [9, { type: 2, bits: 0x3f800000 }],
        [0, { type: 13, bits: 11 }], [0, { type: 13, bits: 12 }], [0, { type: 13, bits: 11 }],
    ] };
    const normalized = v.expressionResult(duplicates, ctx);
    assert(normalized.members.length === 3 && normalized.members[0][1].type === 2 &&
        normalized.members[1][1].bits === 12 && normalized.members[2][1].bits === 11,
        "completion uses AppendStructure typed replacement");
    assert(JSON.stringify(normalized) === JSON.stringify(v.copy(duplicates, "value", ctx)), "owning copy semantics unchanged");
    let exact = array();
    for (let i = 1; i < 128; ++i) exact = { type: 12, elementType: 12, elements: [exact] };
    let exactCopy = v.expressionResult(exact, ctx), count = 0;
    while (exactCopy.type === 12) { ++count; exactCopy = exactCopy.elements[0]; }
    assert(count === 128 && exactCopy.bits === 7, "maximum QB depth does not depend on host recursion");
    let deep = array();
    for (let i = 0; i < 129; ++i) deep = { type: 12, elementType: 12, elements: [deep] };
    expect("limit", () => v.expressionResult({ type: 21, target: deep }, ctx));
    let failed = false;
    try { v.expressionResult(borrow, { ...ctx, charge() { throw Error("synthetic fuel"); } }); }
    catch (error) { failed = String(error).includes("synthetic fuel"); }
    assert(failed && borrow.type === 21 && borrow.target === a, "host charge failure preserves wrapper/target");
    globalThis.qbExpressionResultHelperTest = { completed: true, assertions };
})();
