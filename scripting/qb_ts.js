// NOT ORIGINAL - the "qb" module of the readable TypeScript scripts (docs/qb-ts.md): maps their plain forms onto
// the exact value helpers (values.js) and the task runner (runtime.js), so a script written as TypeScript does what
// tools/qbjs/emitter.ts's JavaScript for the same QB does. Installed as globalThis.qbTs; a compiled module reads
// its imports from it (tools/qbjs/compile_ts.ts).
if (globalThis.qbTs !== undefined) throw Error("QB TypeScript helpers already installed");
globalThis.qbTs = (() => {
    const v = globalThis.qbValues;
    if (v?.apiVersion !== 2) throw Error("QB values API 2 required");
    const { floatBits } = globalThis.qbNumeric;

    // The game's checksum: CRC-32 of the name's bytes, ASCII folded to lower case, / counted as \.
    const table = Array.from({ length: 256 }, (_, n) => {
        for (let i = 0; i < 8; ++i) n = n & 1 ? (n >>> 1) ^ 0xedb88320 : n >>> 1;
        return n >>> 0;
    });
    function checksum(name) {
        const hex = /^_0x([0-9a-f]{8})$/.exec(name);
        if (hex) return Number.parseInt(hex[1], 16);
        let crc = 0xffffffff;
        for (const ch of unescape(encodeURIComponent(name))) {
            let byte = ch.charCodeAt(0);
            if (byte >= 65 && byte <= 90) byte += 32;
            if (byte === 47) byte = 92;
            crc = (crc >>> 8) ^ table[(crc ^ byte) & 0xff];
        }
        return crc >>> 0;
    }

    // What a value in script code is. QB values (objects of values.js) are told from the object literals written
    // in a script by membership here; every helper result is registered.
    const known = new WeakSet(); // a QB value
    const pending = new WeakSet(); // an operator's result: still an expression (materialised where it's used)
    const literalVector = new WeakSet(); // vec(...) written in the script (AddParameter's near-zero angles rule)
    const origin = new WeakMap(); // a parameter read: [parameters, name] (operators read it again as an expression)
    const SPREAD = Symbol("all parameters");
    const PARAMS = Symbol("parameter list");
    const mark = (x, ...sets) => {
        if (x !== undefined && x !== null && typeof x === "object") for (const s of [known, ...sets]) s.add(x);
        return x;
    };

    // The running script: its parameter list and checksum. A script's wrapper sets it around every resume of its
    // body, so interleaved tasks and nested calls each see their own.
    let frame = null;
    const site = (offset = 0) => ({ source: frame?.source ?? "<qb>", script: frame?.script ?? 0, offset });
    const ctx = () => globalThis.qb.context(site());

    function scalarNumber(n) {
        if (Number.isInteger(n) && !Object.is(n, -0)) {
            if (n < -0x80000000 || n > 0xffffffff) throw Error(`Integer out of range: ${n}`);
            return { type: 1, bits: n >>> 0 };
        }
        return { type: 2, bits: floatBits(Math.fround(n)) };
    }
    const bytes = (s) => Array.from(s, (c) => {
        const n = c.charCodeAt(0);
        if (n > 0xff) throw Error("QB strings are bytes: character above \\xff");
        return n;
    });
    // A value as QB stores it: written literals become literal values, operator results their expression result.
    function value(x) {
        if (x === undefined) return undefined;
        if (typeof x === "number") return scalarNumber(x);
        if (typeof x === "string") return { type: 3, bytes: bytes(x) };
        if (typeof x === "function") return value(x());
        if (known.has(x)) return pending.has(x) ? v.expressionResult(x, ctx()) : x;
        // all(p) as a value: a structure copied from the parameters (as a call argument it is spread instead).
        if (typeof x === "object" && x !== null && x[SPREAD]) return v.structure(x[SPREAD], ctx());
        if (Array.isArray(x)) {
            const items = x.map(value);
            const types = items.map((i) => i.type);
            const type = types.includes(2) && types.every((t) => t === 1 || t === 2) ? 2 : (types[0] ?? 0);
            return v.array(type, items, ctx());
        }
        if (typeof x === "object") return v.structure(fields([x]), ctx());
        throw Error(`Not a QB value: ${typeof x}`);
    }
    // An operand inside an expression: parameters are read as expressions (names standing for globals resolved).
    function operand(x) {
        const read = typeof x === "object" && x !== null ? origin.get(x) : undefined;
        if (read) return v.param(read[0], read[1], "expression", ctx());
        if (typeof x === "object" && x !== null && known.has(x)) return x;
        return value(x);
    }
    // Call arguments, a structure's members or a return: object literals are runs of named fields, anything else
    // an unnamed value (a structure among them merged in), all(p) every parameter.
    function entries(items) {
        const out = [];
        for (const item of items) {
            if (item === undefined) continue;
            if (item !== null && typeof item === "object" && item[SPREAD]) {
                out.push(null);
                continue;
            }
            if (item !== null && typeof item === "object" && !known.has(item) && !Array.isArray(item)) {
                for (const key of Object.keys(item)) {
                    const x = item[key];
                    out.push(() => [checksum(key), value(x), false, literalVector.has(x)]);
                }
                continue;
            }
            out.push(() => [0, value(item), true]);
        }
        return out;
    }
    const fields = (items, p = frame?.p ?? []) => v.fields(p, entries(items), ctx());

    function binary(op) {
        return (a, b) => mark(v.binary(op, operand(a), operand(b), ctx()), pending);
    }
    // A switch's selector or case label: the first value of its argument list.
    const first = (items) => fields(items)[0]?.[1];
    function is(selector, ...label) {
        const a = selector instanceof Selected ? selector.value : first([selector]);
        const b = first(label);
        return a !== undefined && b !== undefined && v.binary(7, a, b, ctx()).bits !== 0;
    }
    class Selected {
        constructor(value) {
            this.value = value;
        }
    }

    // p: the parameters as properties. Reading one copies it; writing one adds it (a missing value adds nothing).
    function parameters(list) {
        return new Proxy(list, {
            get(target, key) {
                if (key === PARAMS) return target;
                if (typeof key !== "string") return undefined;
                const n = checksum(key);
                const x = v.param(target, n, "value", ctx());
                if (x !== undefined) {
                    mark(x);
                    origin.set(x, [target, n]);
                }
                return x;
            },
            set(target, key, x) {
                const assigned = value(x);
                if (assigned !== undefined) v.append(target, [[checksum(key), assigned]], ctx());
                return true;
            },
        });
    }
    const listOf = (p) => p[PARAMS] ?? p;

    let source = "<qb>";
    function script(name, defaults, fn) {
        if (fn === undefined) [defaults, fn] = [undefined, defaults];
        const hash = checksum(name),
            module = source;
        globalThis.qb.define(hash, function* (args, root) {
            const mine = { p: [], script: hash, source: module };
            let saved = frame;
            frame = mine;
            let body;
            try {
                // (defaults written as a function get empty parameters: they are parsed without the caller's)
                const d = typeof defaults === "function" ? defaults(parameters([])) : defaults;
                mine.p = v.merge(d === undefined ? [] : fields([d], []), args, ctx());
                globalThis.qb.frame(mine.p, root);
                body = fn(parameters(mine.p));
            } finally {
                frame = saved;
            }
            let sent;
            for (;;) {
                saved = frame;
                frame = mine;
                let r;
                try {
                    r = body.next(sent);
                } finally {
                    frame = saved;
                }
                if (r.done) {
                    if (r.value === undefined) return root ? mine.p : [];
                    frame = mine;
                    try {
                        const out = returned(r.value);
                        return root ? v.merge(mine.p, out, ctx()) : out;
                    } finally {
                        frame = saved;
                    }
                }
                sent = yield r.value;
            }
        });
    }
    function returned(x) {
        if (x !== null && typeof x === "object" && known.has(x) && x.type === 10) return x.members;
        return fields([x]);
    }

    const cmd = new Proxy(
        {},
        {
            get(_, key) {
                if (typeof key !== "string") return undefined;
                const hash = checksum(key);
                return function* (...args) {
                    const p = frame.p;
                    return yield* globalThis.qb.call(hash, fields(args), p, site());
                };
            },
        },
    );
    // A name as read_name reads it: N.x itself; p.x / all(p) the name the parameter holds (0: none).
    function nameOf(named) {
        if (named === null || typeof named !== "object") return 0;
        if (named[SPREAD]) return v.readChecksum(named[SPREAD], 0, ctx())?.bits ?? 0;
        const read = origin.get(named);
        if (read) return v.readChecksum(read[0], read[1], ctx())?.bits ?? 0;
        return known.has(named) && named.type === 13 ? named.bits : 0;
    }
    // call(p.x, ...args): the command named by parameter x (call(all(p), ...): the parameters' unnamed name),
    // read when the statement runs; no name: nothing runs, false.
    function* call(named, ...args) {
        const p = frame.p;
        const hash = nameOf(named);
        return yield* globalThis.qb.call(hash, fields(args), p, site());
    }
    // run(x, ...args): (expression) args as a statement or an if's condition - a name is called, else its truth.
    // (x is the expression itself: its result is taken here - parameters read as in an expression.)
    function* run(x, ...args) {
        const p = frame.p;
        const result = pending.has(x) ? value(x) : v.expressionResult(operand(x), ctx());
        return yield* globalThis.qb.dispatch(result, fields(args), p, site());
    }
    // object:Command - on(N.object).Command(...args); callOn(object, p.x, ...args) when a parameter names the command.
    function* callOn(object, named, ...args) {
        const p = frame.p;
        const target = nameOf(object);
        const hash = nameOf(named);
        return yield* globalThis.qb.call(hash, fields(args), p, site(), target);
    }
    const on = (object) =>
        new Proxy(
            {},
            {
                get(_, key) {
                    if (typeof key !== "string") return undefined;
                    return function* (...args) {
                        const p = frame.p;
                        const target = nameOf(object);
                        return yield* globalThis.qb.call(checksum(key), fields(args), p, site(), target);
                    };
                },
            },
        );
    const N = new Proxy({}, { get: (_, key) => (typeof key === "string" ? mark({ type: 13, bits: checksum(key) }) : undefined) });
    // G.x: global x's value - or, when x is a C function, its result called with no arguments, there and then;
    // fn.x(...args) calls it with arguments. (A C function in an expression: its outputs are dropped, its result 0 / 1.)
    const G = new Proxy({}, { get: (_, key) => (typeof key === "string" ? mark(globalThis.qb.name(checksum(key), [], site())) : undefined) });
    const fn = new Proxy(
        {},
        {
            get: (_, key) =>
                typeof key === "string"
                    ? (...args) => mark(globalThis.qb.name(checksum(key), fields(args), site()))
                    : undefined,
        },
    );

    const coordinate = (c) => (typeof c === "object" ? c.bits : floatBits(Math.fround(c)));
    function choose(opcode) {
        return (key, ...rest) => {
            const choices = rest.at(-1);
            const weights = rest.length > 1 ? rest[0] : choices.map(() => 1);
            const random = globalThis.qb.random();
            const c = globalThis.qb.context(site(key));
            const picked = choices[random.choose(opcode, site(key), weights, c.owner, c.charge)];
            return typeof picked === "function" ? picked() : picked;
        };
    }
    const range = (second) => (a, b) =>
        mark(globalThis.qb.random().range(coordinate(a), coordinate(b), second, ctx().charge));

    return Object.freeze({
        checksum,
        script,
        global(name, x) {
            frame = { p: [], script: 0, source };
            try {
                globalThis.qb.defineGlobal(checksum(name), value(x), site());
            } finally {
                frame = null;
            }
        },
        // The loader names the module being defined (faults and random sites carry it).
        beginModule(name) {
            source = name;
        },
        cmd,
        call,
        run,
        on,
        callOn,
        N,
        G,
        fn,
        f: (n) => mark({ type: 2, bits: floatBits(Math.fround(n)) }),
        fbits: (bits) => mark({ type: 2, bits: bits >>> 0 }),
        vec: (x, y, z) => mark({ type: 6, coordinates: [x, y, z].map(coordinate) }, literalVector),
        pair: (x, y) => mark({ type: 5, coordinates: [x, y].map(coordinate) }),
        L: (s) => mark({ type: 4, bytes: bytes(s) }),
        all: (p) => ({ [SPREAD]: listOf(p) }),
        params: (p) => mark({ type: 20, target: { type: 10, members: listOf(p) } }),
        val: (x) => mark(v.expressionResult(operand(x), ctx())),
        test: (x) => v.statementTruth(value(x), ctx()),
        select: (...args) => new Selected(first(args)),
        is,
        s: (...items) => mark(v.structure(fields(items), ctx())),
        at: binary(0x05),
        eq: binary(0x07),
        dot: binary(0x08),
        sub: binary(0x0a),
        add: binary(0x0b),
        div: binary(0x0c),
        mul: binary(0x0d),
        lt: binary(0x12),
        gt: binary(0x14),
        or: binary(0x32),
        and: binary(0x33),
        // begin ... repeat <n>: the count is read once, at the end of the first pass, then counts down (0 never
        // ends: it wraps like the original's unsigned count).
        repeat() {
            let read = false,
                count;
            return {
                again(...args) {
                    // A pass costs one statement of the task's budget, so a runaway loop faults as the
                    // JavaScript path's does (QuickJS's interrupt handler is the game's backstop).
                    globalThis.qb.step(site());
                    if (!read) {
                        read = true;
                        count = v.lookupTyped(fields(args), 0, 1, ctx())?.bits;
                    }
                    if (count === undefined) return true;
                    count = (count - 1) >>> 0;
                    return count !== 0;
                },
            };
        },
        random: choose(0x2f),
        random2: choose(0x37),
        randomNoRepeat: choose(0x40),
        randomPermute: choose(0x41),
        randomRange: range(false),
        randomRange2: range(true),
    });
})();
