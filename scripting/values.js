// NOT ORIGINAL - format-2 QB value helpers embedded by the native Runtime.
// Behavior: struct.cpp, parse.cpp, eval.cpp; API: docs/qb-js-contract.md.
// Verified subset: value/reference operations, getters, scalar arithmetic,
// string/container operators and coordinate +/-; compound coordinate dot/cross/
// scaling and nonfinite arithmetic reject explicitly pending separate tests.
if (globalThis.qbValues !== undefined) throw Error("QB value helpers already installed");
globalThis.qbValues = (() => {
    const { floatNumber, floatBits, dyadFloat, dyadInt, round: roundPrecision, store32 } = globalThis.qbNumeric;
    const numeric = (v) => v.type === 1 || v.type === 2;
    const isStructure = (v) => v.type === 10 || v.type === 20;
    const isArray = (v) => v.type === 12 || v.type === 21;
    const callable = (v) => v && v.kind === "callable";
    const dataTypes = [1, 2, 3, 4, 5, 6, 10, 12, 13, 20, 21];
    function fail(code, message, w) {
        const site = w?.ctx.site;
        const suffix = site
            ? ` at ${site.source} script 0x${site.script.toString(16)} QB offset 0x${site.offset.toString(16)}`
            : "";
        const error = new Error(`QB ${code}: ${message}${suffix}`);
        error.code = code;
        error.site = site;
        throw error;
    }
    function uint(n, w) {
        if (!Number.isInteger(n) || n < 0 || n > 0xffffffff) fail("type", "Expected uint32", w);
        return n;
    }
    function scalar(type, bits) {
        return { type, bits: uint(bits) };
    }
    const integer = (bits) => scalar(1, bits);
    const floating = (bits) => scalar(2, bits);
    const name = (bits) => scalar(13, bits);
    const number = (v) => (v.type === 1 ? v.bits | 0 : floatNumber(v.bits));
    const dyad = (v) => (v.type === 1 ? dyadInt(v.bits) : dyadFloat(v.bits));
    function floatArithmetic(op, a, b, w) {
        w.step(8);
        const x = dyad(a),
            y = dyad(b);
        let rounded;
        if (op === 10 || op === 11) {
            const e = Math.min(x.e, y.e);
            rounded = roundPrecision(
                (x.m << BigInt(x.e - e)) + (op === 11 ? y.m : -y.m) * (1n << BigInt(y.e - e)),
                1n,
                e,
            );
        } else if (op === 13) rounded = roundPrecision(x.m * y.m, 1n, x.e + y.e);
        else rounded = roundPrecision(x.m, y.m, x.e - y.e);
        const zeroSign =
            op === 11
                ? number(a) + number(b)
                : op === 10
                  ? number(a) - number(b)
                  : op === 13
                    ? number(a) * number(b)
                    : number(a) / number(b);
        return floating(store32(rounded, Object.is(zeroSign, -0)));
    }
    function bytes(input, w) {
        if (!Array.isArray(input)) fail("type", "Expected string bytes", w);
        if (input.length > 250000) fail("limit", "String byte limit", w);
        const out = [];
        let ended = false;
        for (const c of input) {
            w?.step();
            if (!Number.isInteger(c) || c < 0 || c > 255) fail("type", "Invalid string byte", w);
            if (c === 0) ended = true;
            if (!ended) out.push(c);
        }
        return out;
    }
    function coordinates(input, length, w) {
        if (!Array.isArray(input) || input.length !== length) fail("type", "Invalid coordinate count", w);
        return input.map((n) => {
            w?.step();
            return uint(n, w);
        });
    }
    function string(input, local = false) {
        return { type: local ? 4 : 3, bytes: bytes(input) };
    }
    function pair(input) {
        return { type: 5, coordinates: coordinates(input, 2) };
    }
    function vector(input) {
        return { type: 6, coordinates: coordinates(input, 3) };
    }
    class Work {
        constructor(ctx) {
            if (
                !ctx ||
                typeof ctx.symbol !== "function" ||
                typeof ctx.charge !== "function" ||
                !ctx.site ||
                typeof ctx.site.source !== "string"
            )
                fail("type", "Invalid values context");
            uint(ctx.site.script);
            uint(ctx.site.offset);
            uint(ctx.owner);
            this.ctx = ctx;
            this.nodes = 0;
            this.path = new Set();
            this.depth = Math.min(uint(ctx.limits?.depth ?? 128), 128);
            this.limit = Math.min(uint(ctx.limits?.nodes ?? 250000), 250000);
        }
        step(n = 1) {
            if (n > this.limit - this.nodes) fail("limit", "Value work limit", this);
            this.nodes += n;
            this.ctx.charge(n);
        }
        inside(object, fn) {
            if (this.path.has(object)) fail("cycle", "Cyclic value traversal", this);
            if (this.path.size >= this.depth) fail("limit", "Value depth limit", this);
            this.path.add(object);
            try {
                return fn();
            } finally {
                this.path.delete(object);
            }
        }
    }
    function check(v, w) {
        w.step();
        if (!v || typeof v !== "object") fail("type", "Expected a QB value", w);
        if (!dataTypes.includes(v.type)) fail("unsupported", "Unsupported value type", w);
        if ([1, 2, 13].includes(v.type)) uint(v.bits, w);
        else if (v.type === 10) params(v.members, w);
        else if (v.type === 12) {
            if (!Array.isArray(v.elements) || ![0, 1, 2, 3, 4, 5, 6, 10, 12, 13].includes(v.elementType))
                fail("type", "Invalid array shape", w);
            if ((v.elements.length === 0) !== (v.elementType === 0)) fail("type", "Empty array type mismatch", w);
            if (v.elements.length > 250000) fail("limit", "Array element limit", w);
        } else if (v.type === 20 || v.type === 21) {
            if (!v.target || v.target.type !== (v.type === 20 ? 10 : 12)) fail("type", "Invalid reference target", w);
        } else if (v.type === 3 || v.type === 4) {
            if (!Array.isArray(v.bytes)) fail("type", "Invalid string shape", w);
        } else if (!Array.isArray(v.coordinates) || v.coordinates.length !== (v.type === 5 ? 2 : 3))
            fail("type", "Invalid coordinate shape", w);
        return v;
    }
    function params(p, w) {
        if (!Array.isArray(p)) fail("type", "Expected parameters", w);
        if (p.length > 4096) fail("limit", "Parameter count limit", w);
    }
    function field(f, w) {
        w.step();
        if (!Array.isArray(f) || f.length !== 2) fail("type", "Invalid parameter field", w);
        uint(f[0], w);
        check(f[1], w);
        return f;
    }
    function target(v, w) {
        check(v, w);
        const out = v.type === 20 || v.type === 21 ? v.target : v;
        return check(out, w);
    }
    function clone(v, mode, w) {
        check(v, w);
        if (v.type === 20 || v.type === 21) return { type: v.type, target: v.target };
        if (mode === "reference" && (v.type === 10 || v.type === 12))
            return { type: v.type === 10 ? 20 : 21, target: v };
        if ([1, 2, 13].includes(v.type)) return { type: v.type, bits: v.bits };
        if (v.type === 3 || v.type === 4) return { type: v.type, bytes: bytes(v.bytes, w) };
        if (v.type === 5 || v.type === 6)
            return { type: v.type, coordinates: coordinates(v.coordinates, v.type === 5 ? 2 : 3, w) };
        return w.inside(v, () => {
            if (v.type === 10) return { type: 10, members: mergeInternal([], v.members, w) };
            return {
                type: 12,
                elementType: v.elementType,
                elements: v.elements.map((child) => {
                    check(child, w);
                    if (child.type !== v.elementType) fail("type", "Array element type mismatch", w);
                    return clone(child, "value", w);
                }),
            };
        });
    }
    function mode(m, w) {
        if (m !== "value" && m !== "reference") fail("type", "Invalid copy mode", w);
    }
    function copy(v, m, ctx) {
        const w = new Work(ctx);
        mode(m, w);
        return clone(v, m, w);
    }
    function expressionResult(v, ctx) {
        const w = new Work(ctx);
        // An expression with no value produces no component. Binary/truth
        // operations still diagnose missing operands themselves.
        if (v === undefined) {
            w.step();
            return undefined;
        }
        check(v, w);
        // GetResult temporarily retags the outer reference as owning before
        // CopyComponent(reference=0). Do not mutate any source object. Walk
        // owners explicitly so the QB depth bound is checked independently
        // of the host JS/C stack; nested references stay terminal leaves.
        const root = v.type === 20 || v.type === 21 ? target(v, w) : v;
        const frames = [{ source: root }];
        let result;
        const finish = (value) => {
            frames.pop();
            const parent = frames.at(-1);
            if (!parent) result = value;
            else if (parent.out.type === 10) put(parent.out.members, parent.name, value, w);
            else parent.out.elements.push(value);
        };
        while (frames.length) {
            const frame = frames.at(-1),
                source = frame.source;
            if (!frame.out) {
                check(source, w);
                if (source.type !== 10 && source.type !== 12) {
                    finish(clone(source, "value", w));
                    continue;
                }
                if (w.path.has(source)) fail("cycle", "Cyclic value traversal", w);
                if (w.path.size >= w.depth) fail("limit", "Value depth limit", w);
                w.path.add(source);
                frame.index = 0;
                frame.out =
                    source.type === 10
                        ? { type: 10, members: [] }
                        : { type: 12, elementType: source.elementType, elements: [] };
            }
            const children = source.type === 10 ? source.members : source.elements;
            if (frame.index === children.length) {
                w.path.delete(source);
                if (frame.out.type === 10) frame.out.members = materialized(frame.out.members, w);
                finish(frame.out);
                continue;
            }
            let child;
            if (source.type === 10) {
                const member = field(children[frame.index++], w);
                frame.name = member[0];
                child = member[1];
            } else {
                child = check(children[frame.index++], w);
                if (child.type !== source.elementType) fail("type", "Array element type mismatch", w);
            }
            frames.push({ source: child });
        }
        return result;
    }
    function mergeKey(n, v) {
        return n + ":" + (numeric(v) ? "numeric" : v.type === 13 && n === 0 ? "flag:" + v.bits : v.type);
    }
    function put(out, n, v, w) {
        w.step();
        w.merges ??= new WeakMap();
        let index = w.merges.get(out);
        if (!index) {
            index = new Map(out.map(([key, value]) => [mergeKey(key, value), [key, value]]));
            w.merges.set(out, index);
        }
        const key = mergeKey(n, v);
        index.delete(key);
        if (index.size >= 4096) fail("limit", "Parameter count limit", w);
        index.set(key, [n, v]);
    }
    function materialized(out, w) {
        return w.merges?.has(out) ? [...w.merges.get(out).values()] : out;
    }
    function mergeInternal(a, b, w) {
        params(a, w);
        params(b, w);
        const out = [];
        for (const source of [a, b])
            for (const f of source) {
                field(f, w);
                put(out, f[0], clone(f[1], "value", w), w);
            }
        return materialized(out, w);
    }
    function merge(a, b, ctx) {
        return mergeInternal(a, b, new Work(ctx));
    }
    function append(dst, src, ctx) {
        const w = new Work(ctx);
        params(dst, w);
        params(src, w);
        const out = dst.map((f) => {
            field(f, w);
            return [f[0], f[1]];
        });
        const incoming = src.map((f) => {
            field(f, w);
            return [f[0], clone(f[1], "value", w)];
        });
        for (const [n, v] of incoming) put(out, n, v, w);
        dst.splice(0, dst.length, ...materialized(out, w));
        return dst;
    }
    function fields(p, entries, ctx) {
        const w = new Work(ctx),
            out = [];
        const add = (source) => {
            params(source, w);
            for (const f of source) {
                field(f, w);
                put(out, f[0], clone(f[1], "value", w), w);
            }
        };
        for (const entry of entries) {
            w.step();
            if (entry === null) {
                add(p);
                continue;
            }
            const f = entry();
            if (f[1] === undefined) continue;
            if (
                f[3] &&
                f[0] === 0x9d2d0915 &&
                f[1].type === 6 &&
                f[1].coordinates.every((bits) => Math.abs(floatNumber(bits)) < floatNumber(0x3c23d70a))
            )
                continue;
            if (f[2] && f[1].type === 10) add(f[1].members);
            else add([[f[0], f[1]]]);
        }
        return materialized(out, w);
    }
    function structure(fields, ctx) {
        return { type: 10, members: merge([], fields, ctx) };
    }
    function array(elementType, elements, ctx) {
        const w = new Work(ctx);
        uint(elementType, w);
        if (![0, 1, 2, 3, 4, 5, 6, 10, 12, 13].includes(elementType)) fail("type", "Invalid array element type", w);
        if (!Array.isArray(elements)) fail("type", "Expected array elements", w);
        if (elements.length > 250000) fail("limit", "Array element limit", w);
        const out = { type: 12, elementType: elements.length ? elementType : 0, elements: [] };
        check({ ...out, elements }, w);
        for (let child of elements) {
            check(child, w);
            if (elementType === 2 && child.type === 1) child = floating(floatBits(child.bits | 0));
            if (child.type !== elementType) fail("type", "Array element type mismatch", w);
            out.elements.push(clone(child, "value", w));
        }
        return out;
    }
    function lookup(name, w) {
        uint(name, w);
        const visited = new Set();
        let current = w.ctx.symbol(name);
        if (current === undefined) return undefined;
        for (;;) {
            w.step();
            if (callable(current)) {
                if (![7, 8, 9].includes(current.type)) fail("type", "Invalid callable descriptor", w);
                uint(current.name, w);
                return current;
            }
            check(current, w);
            if (current.type !== 13) return current;
            if (visited.has(name)) fail("cycle", "Global alias cycle", w);
            if (visited.size >= w.depth) fail("limit", "Global alias depth limit", w);
            visited.add(name);
            name = current.bits;
            const next = w.ctx.symbol(name);
            if (next === undefined) return current;
            current = next;
        }
    }
    function resolved(v, w, rejectCallable = false) {
        check(v, w);
        if (v.type !== 13) return v;
        const symbol = lookup(v.bits, w);
        if (symbol === undefined) return v;
        if (callable(symbol)) {
            if (rejectCallable) {
                if (symbol.type === 7) return symbol;
                fail("unsupported", "Callable value comparison is not implemented", w);
            }
            return v;
        }
        return symbol;
    }
    function included(f, w) {
        if (f[0] !== 0 || f[1].type !== 13) return undefined;
        const v = lookup(f[1].bits, w);
        return v?.type === 10 ? v.members : undefined;
    }
    function search(p, n, kind, w) {
        params(p, w);
        uint(n, w);
        return w.inside(p, () => {
            let found;
            for (const f of p) {
                field(f, w);
                if (f[0] === n) {
                    let v = f[1];
                    if (v.type === kind) {
                        found = v;
                        continue;
                    }
                    if (v.type === 13) v = lookup(v.bits, w);
                    if (v?.type === kind) found = v;
                    else if (v?.type === 1 && kind === 2) found = floating(floatBits(v.bits | 0));
                } else {
                    const inner = included(f, w);
                    if (inner) {
                        const v = search(inner, n, kind, w);
                        if (v !== undefined) found = v;
                    }
                }
            }
            return found;
        });
    }
    function findLast(p, n, w) {
        params(p, w);
        uint(n, w);
        return w.inside(p, () => {
            let found;
            for (const f of p) {
                field(f, w);
                if (f[0] === n) found = f[1];
                else {
                    const inner = included(f, w);
                    if (inner) {
                        const v = findLast(inner, n, w);
                        if (v !== undefined) found = v;
                    }
                }
            }
            return found;
        });
    }
    function param(p, n, m, ctx) {
        const w = new Work(ctx);
        if (m !== "value" && m !== "expression") fail("type", "Invalid parameter mode", w);
        const v = findLast(p, n, w);
        if (v === undefined) return undefined;
        return clone(m === "expression" ? resolved(v, w) : v, m === "expression" ? "reference" : "value", w);
    }
    function global(name, ctx) {
        const w = new Work(ctx);
        const v = lookup(name, w);
        if (v === undefined) return scalar(13, name);
        if (callable(v)) {
            if (v.type === 7) return scalar(13, v.name);
            fail("unsupported", "Expression calls are not implemented", w);
        }
        return clone(v, "reference", w);
    }
    function checksumSearch(p, n, w) {
        params(p, w);
        uint(n, w);
        return w.inside(p, () => {
            let found;
            for (const f of p) {
                field(f, w);
                if (f[0] === n) {
                    if (f[1].type === 13 && (n !== 0 || lookup(f[1].bits, w)?.type !== 10)) found = f[1];
                } else {
                    const inner = included(f, w);
                    if (inner) {
                        const v = checksumSearch(inner, n, w);
                        if (v !== undefined) found = v;
                    }
                }
            }
            return found;
        });
    }
    function containsInternal(p, n, w) {
        params(p, w);
        uint(n, w);
        return w.inside(p, () => {
            for (const f of p) {
                field(f, w);
                if (f[0] === n || (f[0] === 0 && f[1].type === 13 && f[1].bits === n)) return true;
                const inner = included(f, w);
                if (inner && containsInternal(inner, n, w)) return true;
            }
            return false;
        });
    }
    function equal(a, b, w) {
        a = resolved(a, w, true);
        b = resolved(b, w, true);
        if (numeric(a) && numeric(b)) return number(a) === number(b);
        if (a.type !== b.type) return false;
        if (callable(a) || callable(b)) return callable(a) && callable(b) && a.type === b.type && a.name === b.name;
        if (a.type === 13) return a.bits === b.bits;
        if (a.type === 10 || a.type === 12) return a === b;
        if (a.type === 3 || a.type === 4) {
            const x = bytes(a.bytes, w),
                y = bytes(b.bytes, w);
            return x.length === y.length && x.every((v, i) => v === y[i]);
        }
        if (a.type === 5 || a.type === 6) {
            const x = coordinates(a.coordinates, a.type === 5 ? 2 : 3, w),
                y = coordinates(b.coordinates, b.type === 5 ? 2 : 3, w);
            return x.every((v, i) => floatNumber(v) === floatNumber(y[i]));
        }
        return false; // ComponentsEqual's reference-tag cases fall through to false.
    }
    function subset(a, b, w) {
        params(a, w);
        params(b, w);
        for (const f of a) {
            field(f, w);
            let found = false;
            for (const g of b) {
                field(g, w);
                if (f[0] === g[0] && equal(f[1], g[1], w)) {
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
        return true;
    }
    function getBool(v) {
        return v.type === 1 ? v.bits : v.type === 2 && floatNumber(v.bits) !== 0 ? 1 : 0;
    }
    function operation(op, a, b, w) {
        uint(op, w);
        if (a === undefined || b === undefined) return undefined;
        check(a, w);
        check(b, w);
        if (op === 7) return integer(equal(a, b, w) ? 1 : 0);
        if (op === 50 || op === 51)
            return integer((op === 50 ? getBool(a) | getBool(b) : getBool(a) & getBool(b)) >>> 0);
        if (op === 18 || op === 20) {
            a = resolved(a, w, true);
            b = resolved(b, w, true);
            let yes = false;
            if (numeric(a) && numeric(b)) yes = op === 18 ? number(a) < number(b) : number(a) > number(b);
            else if ((a.type === 3 || a.type === 4) && a.type === b.type) {
                const x = bytes(a.bytes, w).length,
                    y = bytes(b.bytes, w).length;
                yes = op === 18 ? x < y : x > y;
            } else if (isStructure(a) && isStructure(b)) {
                const x = target(a, w).members,
                    y = target(b, w).members;
                yes = op === 18 ? subset(x, y, w) : subset(y, x, w);
            }
            return integer(yes ? 1 : 0);
        }
        if (op === 5) {
            if (!isArray(a) || b.type !== 1) fail("type", "Indexing requires Array and Integer", w);
            const data = target(a, w);
            if (b.bits >= data.elements.length) fail("arithmetic", "Array index out of bounds", w);
            if (data.elementType === 4)
                fail("unsupported", "LocalString array indexing is not implemented in original []", w);
            const v = check(data.elements[b.bits], w);
            if (v.type !== data.elementType) fail("type", "Array element type mismatch", w);
            return clone(v, a.type === 21 ? "reference" : "value", w);
        }
        if (op === 8 && isStructure(a)) {
            if (b.type !== 13) fail("type", "Member selector must be a Name", w);
            const v = findLast(target(a, w).members, b.bits, w);
            return v === undefined ? undefined : clone(v, a.type === 20 ? "reference" : "value", w);
        }
        if (op === 11 && a.type === 13 && (b.type === 1 || b.type === 13)) return name((a.bits + b.bits) >>> 0);
        if ([10, 11, 12, 13].includes(op) && numeric(a) && numeric(b)) {
            const x = number(a),
                y = number(b);
            if (!Number.isFinite(x) || !Number.isFinite(y))
                fail("unsupported", "Nonfinite arithmetic is outside the verified subset", w);
            // 0x358637bd is the reference evaluator's 1e-6f threshold.
            if (
                op === 12 &&
                ((b.type === 1 && b.bits === 0) || (b.type === 2 && Math.abs(y) < floatNumber(0x358637bd)))
            )
                fail("arithmetic", "Division by zero or near-zero float", w);
            if (a.type === 1 && b.type === 1) {
                if (op === 12 && x === -2147483648 && y === -1) fail("arithmetic", "Integer division overflow", w);
                return integer(
                    (op === 11
                        ? a.bits + b.bits
                        : op === 10
                          ? a.bits - b.bits
                          : op === 13
                            ? Math.imul(a.bits, b.bits)
                            : Math.trunc(x / y)) >>> 0,
                );
            }
            return floatArithmetic(op, a, b, w);
        }
        if (op === 11 && (a.type === 3 || a.type === 4) && a.type === b.type) {
            const x = bytes(a.bytes, w),
                y = bytes(b.bytes, w);
            if (x.length + y.length > 199) fail("arithmetic", "String addition exceeds 199 bytes", w);
            return { type: a.type, bytes: x.concat(y) };
        }
        if (op === 11 && isArray(a) && isArray(b)) {
            const x = target(a, w),
                y = target(b, w);
            if (x.elements.length && y.elements.length && x.elementType !== y.elementType)
                fail("type", "Array addition would reinterpret mixed types", w);
            const elementType = x.elements.length ? x.elementType : y.elementType;
            if (x.elements.length + y.elements.length > 250000) fail("limit", "Array element limit", w);
            const elements = x.elements.concat(y.elements);
            return clone({ type: 12, elementType, elements }, "value", w);
        }
        if (isStructure(a)) {
            if (op === 11 && isStructure(b))
                return { type: 10, members: mergeInternal(target(a, w).members, target(b, w).members, w) };
            if (op === 10) {
                let out = mergeInternal([], target(a, w).members, w);
                const remove = (n, kind) => {
                    out = out.filter((f) => {
                        w.step();
                        return !(
                            (f[0] === n && (kind === undefined || f[1].type === kind)) ||
                            (kind === undefined && f[0] === 0 && f[1].type === 13 && f[1].bits === n)
                        );
                    });
                };
                if (b.type === 13) remove(b.bits);
                else if (isArray(b)) {
                    const names = target(b, w);
                    if (![0, 13].includes(names.elementType))
                        fail("type", "Structure subtraction needs a Name array", w);
                    for (const v of names.elements) {
                        check(v, w);
                        if (v.type !== 13) fail("type", "Name array shape mismatch", w);
                        remove(v.bits);
                    }
                } else if (isStructure(b))
                    for (const f of target(b, w).members) {
                        field(f, w);
                        if (f[0]) remove(f[0], f[1].type);
                        else if (f[1].type === 13) remove(f[1].bits);
                    }
                else fail("type", "Invalid structure subtraction", w);
                return { type: 10, members: out };
            }
        }
        if ((op === 10 || op === 11) && (a.type === 5 || a.type === 6) && a.type === b.type) {
            const n = a.type === 5 ? 2 : 3,
                x = coordinates(a.coordinates, n, w),
                y = coordinates(b.coordinates, n, w);
            if ([...x, ...y].some((bits) => !Number.isFinite(floatNumber(bits))))
                fail("unsupported", "Nonfinite coordinate arithmetic is outside the verified subset", w);
            return {
                type: a.type,
                coordinates: x.map((bits, i) => floatArithmetic(op, floating(bits), floating(y[i]), w).bits),
            };
        }
        fail("unsupported", `Operator ${op} for types ${a.type}/${b.type} is not implemented`, w);
    }
    return Object.freeze({
        apiVersion: 2,
        integer,
        float: floating,
        name,
        string,
        pair,
        vector,
        structure,
        array,
        copy,
        expressionResult,
        merge,
        fields,
        append,
        param,
        global,
        lookupGlobal(n, ctx) {
            return lookup(n, new Work(ctx));
        },
        resolve(v, ctx) {
            return resolved(v, new Work(ctx));
        },
        lookupTyped(p, n, type, ctx) {
            const w = new Work(ctx);
            if (!dataTypes.includes(type)) fail("unsupported", "Unsupported getter type", w);
            return search(p, n, type, w);
        },
        readChecksum(p, n, ctx) {
            return checksumSearch(p, n, new Work(ctx));
        },
        readString(p, n, ctx) {
            const w = new Work(ctx);
            return search(p, n, 3, w) ?? search(p, n, 4, w);
        },
        contains(p, n, ctx) {
            return containsInternal(p, n, new Work(ctx));
        },
        binary(op, a, b, ctx) {
            return operation(op, a, b, new Work(ctx));
        },
        statementTruth(v, ctx) {
            const w = new Work(ctx);
            if (v === undefined) return false;
            check(v, w);
            if (v.type === 13) fail("unsupported", "Checksum expression dispatch is not implemented", w);
            return numeric(v) ? v.bits !== 0 : true;
        },
    });
})();
