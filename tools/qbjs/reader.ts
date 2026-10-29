// NOT ORIGINAL - bounded QB -> format-2 IR reader, phase one of issue #3.
// References: script/qb.*, load.cpp and parse.cpp. Does not evaluate or emit JS.
// Random tables, section markers and cached pointers
// still reject explicitly. Existing format-1 translate.ts is unchanged.
// This reads raw token streams, not the separate PC precompiled-image format.
import { closeSync, existsSync, fstatSync, openSync, readSync, statSync, writeFileSync } from "node:fs";
import { pathToFileURL } from "node:url";
import type { Call, Definition, Expr, Field, ModuleIR, Statement, SwitchLabel } from "./ir.ts";

export type ReaderLimits = { nodes?: number; depth?: number };
export function readIR(bytes: Uint8Array, source = "input.qb", limits: ReaderLimits = {}): ModuleIR {
    let p = 0,
        nodes = 0;
    const fail = (message: string): never => {
        throw new Error(`${source}: QB offset 0x${p.toString(16)}: ${message}`);
    };
    const limit = (n: number | undefined, cap: number): number => {
        if (n !== undefined && (!Number.isInteger(n) || n < 0)) fail("invalid reader limit");
        return Math.min(n ?? cap, cap);
    };
    const nodeLimit = limit(limits.nodes, 100000),
        depthLimit = limit(limits.depth, 128);
    if (bytes.length > 4 * 1024 * 1024) fail("module exceeds 4 MiB");
    const b = Buffer.from(bytes);
    if (b.length >= 4 && b.readUInt32LE(0) === 0xb075ef6f)
        fail("precompiled QB images are not supported by raw-token reader");
    const need = (n: number) => {
        if (n > b.length - p) fail("truncated token");
    };
    const node = () => {
        if (++nodes > nodeLimit) fail("IR node limit");
    };
    const byte = () => {
        need(1);
        return b[p++]!;
    };
    const word = () => {
        need(4);
        const n = b.readUInt32LE(p);
        p += 4;
        return n;
    };
    const short = () => {
        need(2);
        const n = b.readUInt16LE(p);
        p += 2;
        return n;
    };
    const name = () => {
        if (byte() !== 0x16) fail("expected checksum name");
        return word();
    };
    const eol = () => {
        const op = byte();
        if (op === 2) word();
        else if (op !== 1) fail("expected end of line");
    };
    const lines = (): Set<number> => {
        const positions = new Set([p]);
        while (b[p] === 1 || b[p] === 2) {
            eol();
            positions.add(p);
        }
        return positions;
    };
    const separators = () => {
        while (b[p] === 1 || b[p] === 2 || b[p] === 9) {
            if (b[p] === 9) ++p;
            else eol();
        }
    };
    const endLine = () => {
        if (b[p] !== 0x24) eol();
        lines();
    };
    const heights = new WeakMap<Expr, number>();
    const bounded = (v: Expr): Expr => {
        const children =
            v.kind === "binary"
                ? [v.left, v.right]
                : v.kind === "array"
                  ? v.items
                  : v.kind === "structure"
                    ? v.fields.flatMap((f) => ("spread" in f ? [] : [f.value]))
                    : [];
        const height = children.reduce((h, child) => Math.max(h, 1 + (heights.get(child) ?? 0)), 0);
        if (height > depthLimit) fail("value depth limit");
        heights.set(v, height);
        return v;
    };
    // These are the evaluator's priorities, deliberately different from JS/C.
    const precedence = new Map([
        [5, 100],
        [8, 100],
        [13, 100],
        [12, 99],
        [10, 98],
        [11, 98],
        [18, 80],
        [20, 78],
        [7, 76],
        [51, 60],
        [50, 58],
    ]);
    const expression = (depth: number, rawName = false): Expr => {
        if (depth > depthLimit) fail("value depth limit");
        const values: Expr[] = [];
        const operators: { op: number; offset: number }[] = [];
        let expecting = true,
            afterDot = rawName,
            brackets = 0;
        const reduce = () => {
            const operator = operators.pop()!,
                right = values.pop()!,
                left = values.pop()!;
            node();
            values.push(bounded({ kind: "binary", ...operator, left, right }));
        };
        while (true) {
            lines();
            need(1);
            if (b[p] === 6) {
                if (expecting || !brackets) fail("unmatched expression index bracket");
                --brackets;
                ++p;
                continue;
            }
            if (b[p] === 15) {
                if (expecting || brackets) fail("incomplete expression");
                ++p;
                while (operators.length) reduce();
                return values[0]!;
            }
            if (expecting) {
                if (b[p] === 14) {
                    ++p;
                    values.push(expression(depth + 1, afterDot));
                } else if (b[p] === 0x2c) {
                    node();
                    values.push({ kind: "allParameters", offset: p++ });
                } else values.push(value(depth + 1, true, afterDot));
                expecting = false;
                continue;
            }
            const offset = p;
            let op = b[p]!,
                implicit: Expr | undefined;
            // Negative literals adjacent to a value encode subtraction.
            if (op === 0x17 || op === 0x1a) {
                need(5);
                const bits = b.readUInt32LE(p + 1);
                const negative = op === 0x17 ? (bits | 0) < 0 : b.readFloatLE(p + 1) < 0;
                if (!negative) fail("adjacent expression values");
                if (op === 0x17 && bits === 0x80000000) fail("implicit subtraction of INT_MIN is unsupported");
                node();
                implicit = {
                    kind: "literal",
                    offset,
                    value: { type: op === 0x17 ? 1 : 2, bits: op === 0x17 ? -bits >>> 0 : (bits ^ 0x80000000) >>> 0 },
                };
                p += 5;
                op = 10;
            } else {
                if (!precedence.has(op)) fail(`unsupported expression operator 0x${op.toString(16)}`);
                ++p;
                if (op === 5) ++brackets;
            }
            while (operators.length && precedence.get(operators.at(-1)!.op)! >= precedence.get(op)!) reduce();
            operators.push({ op, offset });
            afterDot = op === 8;
            expecting = !implicit;
            if (implicit) values.push(implicit);
        }
    };
    const value = (depth: number, inExpression = false, rawName = false): Expr => {
        if (depth > depthLimit) fail("value depth limit");
        node();
        const offset = p,
            op = byte();
        if (inExpression && op === 0x18) fail("unsupported expression operand token 0x18");
        if (op === 0x16) {
            const n = word();
            return inExpression && !rawName
                ? { kind: "global", offset, name: n }
                : { kind: "literal", offset, value: { type: 13, bits: n } };
        }
        if (op === 0x17 || op === 0x18 || op === 0x1a)
            return { kind: "literal", offset, value: { type: op === 0x1a ? 2 : 1, bits: word() } };
        if (op === 0x2d)
            return { kind: "parameter", offset, name: name(), mode: inExpression ? "expression" : "value" };
        if (op === 0x1b || op === 0x1c) {
            const length = word();
            need(length);
            const zero = b.indexOf(0, p);
            if (length === 0 || zero < p || zero >= p + length) fail("unterminated QB string");
            if (zero - p > 250000) fail("string byte limit");
            const data = [...b.subarray(p, zero)];
            p += length;
            return { kind: "literal", offset, value: { type: op === 0x1b ? 3 : 4, bytes: data } };
        }
        if (op === 0x1e || op === 0x1f) {
            const coordinates = Array.from({ length: op === 0x1e ? 3 : 2 }, () => word());
            return { kind: "literal", offset, value: { type: op === 0x1e ? 6 : 5, coordinates } };
        }
        if (op === 3) {
            const fields = parameters(depth + 1, true);
            if (byte() !== 4) fail("expected close structure");
            return bounded({ kind: "structure", offset, fields });
        }
        if (op === 5) {
            if (inExpression) fail("expected expression operand, found index operator");
            const items: Expr[] = [];
            let elementType = 0;
            separators();
            while (b[p] !== 6) {
                need(1);
                if (![3, 5, 0x16, 0x17, 0x18, 0x1a, 0x1b, 0x1c, 0x1e, 0x1f].includes(b[p]!))
                    fail("unsupported literal-array element");
                const item = value(depth + 1);
                const type =
                    item.kind === "array"
                        ? 12
                        : item.kind === "structure"
                          ? 10
                          : item.kind === "literal"
                            ? item.value.type
                            : 0;
                if (elementType && type !== elementType && !([1, 2].includes(type) && [1, 2].includes(elementType)))
                    fail("unsafe mixed literal-array types");
                elementType = elementType === 2 && type === 1 ? 2 : type;
                items.push(item);
                separators();
            }
            ++p;
            return bounded({ kind: "array", offset, items });
        }
        if (op === 0x0e) {
            return expression(depth + 1, rawName);
        }
        return fail(`unsupported value token 0x${op.toString(16)}`);
    };
    const parameters = (depth: number, structure = false): Field[] => {
        if (depth > depthLimit) fail("value depth limit");
        const fields: Field[] = [];
        if (structure) separators();
        while (structure ? b[p] !== 4 : b[p] !== 1 && b[p] !== 2 && b[p] !== 0x24) {
            need(1);
            if (b[p] === 9) {
                ++p;
                continue;
            }
            node();
            const offset = p;
            if (fields.length >= 4096) fail("field count limit");
            if (b[p] === 0x2c) {
                ++p;
                fields.push({ spread: true, offset });
            } else {
                let n = 0;
                let assigned = false;
                if (b[p] === 0x16) {
                    const start = p,
                        candidate = name();
                    if (structure) lines();
                    if (b[p] === 7) {
                        n = candidate;
                        assigned = true;
                        ++p;
                        if (structure) lines();
                    } else p = start;
                }
                const item = value(depth);
                // ParseArguments/ParseStructure recursively append anonymous
                // brace blocks into the same destination, rather than store
                // an unnamed nested Structure component.
                if (!assigned && item.kind === "structure") {
                    if (fields.length + item.fields.length > 4096) fail("field count limit");
                    fields.push(...item.fields);
                } else fields.push({ name: n, value: item, offset });
            }
            if (structure) separators();
        }
        return fields;
    };
    const staticDefaults = (fields: Field[]): void => {
        const check = (v: Expr): void => {
            if (v.kind === "parameter" || v.kind === "allParameters")
                fail("defaults/globals cannot read caller parameters");
            if (v.kind === "binary") {
                check(v.left);
                check(v.right);
            }
            if (v.kind === "structure") staticDefaults(v.fields);
            if (v.kind === "array") v.items.forEach(check);
        };
        for (const f of fields) {
            if ("spread" in f) fail("defaults/globals cannot spread caller parameters");
            else check(f.value);
        }
    };
    const call = (): Call => {
        const offset = p,
            n = name();
        return { kind: "call", offset, name: n, fields: parameters(0) };
    };
    const block = (depth: number, loops: number[] = []): Statement[] => {
        if (depth > 64) fail("statement nesting limit");
        const body: Statement[] = [];
        lines();
        while (![0x21, 0x24, 0x28, 0x3d, 0x3e, 0x3f, 0x48, 0x49].includes(b[p]!)) {
            need(1);
            node();
            const offset = p;
            if (b[p] === 0x3c) {
                ++p;
                const fields = parameters(0);
                eol();
                lines();
                const cases: { labels: SwitchLabel[]; body: Statement[] }[] = [];
                const exits: number[] = [];
                let labels: SwitchLabel[] = [];
                while (b[p] === 0x3e) {
                    node();
                    const labelOffset = p++;
                    if (byte() !== 0x49) fail("expected compiled case jump");
                    const next = p + short();
                    labels.push({ offset: labelOffset, fields: parameters(0) });
                    eol();
                    lines();
                    const branch = block(depth + 1, loops);
                    if (byte() !== 0x49) fail("expected case exit jump");
                    exits.push(p + short());
                    // Only an immediate next case after an empty body is a
                    // grouped label. Its arguments still parse after a match.
                    const grouped = branch.length === 0 && b[p] === 0x3e;
                    lines();
                    if (next !== p || ![0x3e, 0x3f, 0x3d].includes(b[p]!))
                        fail("case jump does not target next label/endswitch boundary");
                    if (!grouped) {
                        cases.push({ labels, body: branch });
                        labels = [];
                    }
                }
                if (labels.length) fail("unterminated grouped case labels");
                let fallback: Statement[] | undefined;
                if (b[p] === 0x3f) {
                    ++p;
                    if (byte() !== 0x49) fail("expected compiled default jump");
                    exits.push(p + short());
                    fallback = block(depth + 1, loops);
                }
                const end = p;
                if (byte() !== 0x3d) fail("expected endswitch (pre-case statements are unsupported)");
                if (exits.some((destination) => destination !== end)) fail("case exit does not target endswitch");
                body.push({ kind: "switch", offset, fields, cases, ...(fallback === undefined ? {} : { fallback }) });
                lines();
                continue;
            }
            if (b[p] === 0x20) {
                if (loops.length >= 10) fail("loop nesting exceeds original loop storage");
                ++p;
                const nested = block(depth + 1, [...loops, offset]);
                const repeatOffset = p;
                if (byte() !== 0x21) fail("expected repeat for begin");
                const repeatFields = parameters(0);
                body.push({ kind: "loop", offset, body: nested, repeatOffset, repeatFields });
                endLine();
                continue;
            }
            if (b[p] === 0x47) {
                ++p;
                const destination = p + short();
                const condition =
                    b[p] === 0x16
                        ? call()
                        : b[p] === 0x0e
                          ? value(0, true)
                          : fail("unsupported condition command token");
                eol();
                const yes = block(depth + 1, loops);
                let no: Statement[] = [],
                    endDestination: number | undefined;
                if (b[p] === 0x48) {
                    ++p;
                    const end = p + short(),
                        starts = lines();
                    if (!starts.has(destination)) fail("if jump does not target else body boundary");
                    no = block(depth + 1, loops);
                    endDestination = end;
                }
                const endOffset = p;
                if (byte() !== 0x28) fail("expected endif");
                const ends = lines();
                ends.add(endOffset);
                if (endDestination !== undefined) {
                    if (!ends.has(endDestination)) fail("else jump does not target endif boundary");
                } else if (!ends.has(destination)) fail("if jump does not target endif boundary");
                body.push({ kind: "if", offset, condition, yes, no });
                continue;
            }
            if (b[p] === 0x22) {
                const target = loops.at(-1) ?? fail("break outside local loop (cross-call loop stack is unsupported)");
                ++p;
                body.push({ kind: "break", offset, target });
            } else if (b[p] === 0x29) {
                ++p;
                body.push({ kind: "return", offset, fields: parameters(0) });
            } else if ((b[p] === 0x16 && b[p + 5] === 7) || (b[p] === 0x2d && b[p + 1] === 0x16 && b[p + 6] === 7)) {
                if (b[p] === 0x2d) ++p;
                const n = name();
                ++p;
                body.push({ kind: "assign", offset, name: n, value: value(0) });
            } else body.push(call());
            endLine();
        }
        return body;
    };
    const definitions: Definition[] = [];
    lines();
    while (b[p] !== 0) {
        need(1);
        node();
        const offset = p;
        if (b[p] === 0x2b) {
            ++p;
            word();
            const zero = b.indexOf(0, p);
            if (zero < 0) fail("unterminated checksum-name record");
            p = zero + 1;
            lines();
            continue;
        }
        if (b[p] === 0x23) {
            ++p;
            const n = name(),
                defaults = parameters(0);
            staticDefaults(defaults);
            eol();
            const body = block(0);
            if (byte() !== 0x24) fail("expected endscript");
            definitions.push({ kind: "script", offset, name: n, defaults, body });
        } else if (b[p] === 0x16) {
            const n = name();
            lines();
            if (byte() !== 7) fail("expected global assignment");
            lines();
            const rhs = value(0);
            staticDefaults([{ name: n, value: rhs, offset }]);
            definitions.push({ kind: "global", offset, name: n, value: rhs });
        } else fail(`unsupported definition token 0x${b[p]!.toString(16)}`);
        lines();
    }
    ++p;
    if (p !== b.length) fail("trailing bytes after EOF");
    return { format: 2, source, definitions };
}

// Shared by the IR and JS CLIs: bound reads and protect the original QB file.
export function readQBFile(input: string, output?: string): Uint8Array {
    const fd = openSync(input, "r");
    let result: Uint8Array;
    try {
        const metadata = fstatSync(fd, { bigint: true });
        if (!metadata.isFile()) throw new Error("QB input must be a regular file");
        if (output !== undefined && existsSync(output)) {
            const target = statSync(output, { bigint: true });
            if (metadata.dev === target.dev && metadata.ino === target.ino)
                throw new Error("QB input and output must differ");
        }
        const cap = 4 * 1024 * 1024;
        if (metadata.size > cap) throw new Error(`${input}: QB offset 0x0: module exceeds 4 MiB`);
        const bytes = Buffer.allocUnsafe(cap + 1);
        let count = 0;
        for (;;) {
            const n = readSync(fd, bytes, count, bytes.length - count, null);
            if (n === 0) break;
            count += n;
            if (count > cap) throw new Error(`${input}: QB offset 0x0: module exceeds 4 MiB`);
        }
        result = bytes.subarray(0, count);
    } finally {
        closeSync(fd);
    }
    return result;
}
export const readIRFile = (input: string, output?: string): ModuleIR => readIR(readQBFile(input, output), input);
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    const [, , input, output] = process.argv;
    if (!input || !output || process.argv.length !== 4)
        throw new Error("Usage: node tools/qbjs/reader.ts INPUT.qb OUTPUT.ir.json");
    writeFileSync(output, JSON.stringify(readIRFile(input, output), null, 2) + "\n");
}
