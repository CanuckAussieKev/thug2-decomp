// NOT ORIGINAL - bounded QB -> format-2 IR reader, phase one of issue #3.
// References: script/qb.*, load.cpp and parse.cpp. Does not evaluate or emit JS.
// Statement random branches, random arrays, section markers and cached pointers
// still reject explicitly. Existing format-1 translate.ts is unchanged.
// This reads raw token streams, not the separate PC precompiled-image format.
import { closeSync, existsSync, fstatSync, openSync, readSync, statSync, writeFileSync } from "node:fs";
import { pathToFileURL } from "node:url";
import type {
    Assign,
    Call,
    Definition,
    Expr,
    Field,
    ModuleIR,
    NameRef,
    RandomFields,
    Statement,
    SwitchLabel,
} from "./ir.ts";

// Tokens that can start a C function's arguments after a name in an expression (a negative number there is a
// subtraction; '[' an index).
const ARGUMENT_STARTS = [0x03, 0x0e, 0x16, 0x17, 0x18, 0x1a, 0x1b, 0x1c, 0x1e, 0x1f, 0x2c, 0x2d];

// Tokens that can start a statement (after a statement-ending Random value its line has ended).
const STATEMENT_STARTS = (t: number) =>
    [
        0x16, 0x2c, 0x2d, 0x0e, 0x20, 0x21, 0x22, 0x28, 0x29, 0x2f, 0x37, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x47,
        0x48, 0x49,
    ].includes(t);

const MODULE_CAP = 64 * 1024 * 1024;

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
    // (generous: a ported level's node array runs to millions of nodes in a 5 MB file)
    const nodeLimit = limit(limits.nodes, 20_000_000),
        depthLimit = limit(limits.depth, 128);
    if (bytes.length > MODULE_CAP) fail("module exceeds 64 MiB");
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
        if (lineEnded) lineEnded = false;
        else if (b[p] !== 0x24) eol();
        lines();
    };
    // A Random's table: 2F/37/40/41, count, 16-bit weights, then each branch's 32-bit offset.
    const randomTable = () => {
        const offset = p,
            opcode = byte();
        const count = word();
        if (count < 1 || count > 4096) fail("random value choice count must be 1..4096");
        if (opcode === 0x41 && (count < 2 || count > 66)) fail("RandomPermute requires 2..66 choices");
        const weights = Array.from({ length: count }, () => short());
        if ((opcode === 0x2f || opcode === 0x37) && (weights.some((w) => w >= 0x8000) || weights.every((w) => w === 0)))
            fail("unsafe weighted random value table");
        const targets = Array.from({ length: count }, () => {
            const field = p;
            return field + 4 + word();
        });
        return { offset, opcode, weights, targets };
    };
    // Branches of a Random among statements or fields: each starts at its target and (all but the last) ends with
    // a 2E jump to the join; the last ends at the join. parse(until) reads one branch, stopping at a 2E or `until`.
    const randomBranches = <T>(
        targets: number[],
        parse: (until: number) => T[],
        allowLines: boolean,
        exit?: (offset: number) => T,
    ): T[][] => {
        if (!exit) return joinedBranches(targets, parse, allowLines);
        const start = p,
            count = nodes,
            ended = lineEnded;
        try {
            return joinedBranches(targets, parse, allowLines);
        } catch (e) {
            p = start;
            nodes = count;
            lineEnded = ended;
            const branches = tailBranches(targets, parse, exit);
            if (!branches) throw e;
            return branches;
        }
    };
    // Statement branches whose jumps go to the script's tail (SK6Ped_UniqueStateLogic: two branches leave the loop
    // for the endscript, the last runs on into the repeat): those end with a return; the others join where the last
    // one ends. null: not that shape.
    const tailBranches = <T>(targets: number[], parse: (until: number) => T[], exit: (offset: number) => T) => {
        const branches: T[][] = [],
            ends: number[] = [];
        for (let i = 0; i < targets.length - 1; i++) {
            while (p !== targets[i] && (b[p] === 1 || b[p] === 2)) eol();
            if (p !== targets[i]) return null;
            const branch = parse(-1);
            if (b[p] !== 0x2e) return null;
            const jump = p++;
            const end = p + 4 + word();
            if (isTail(end)) branch.push(exit(jump));
            branches.push(branch);
            ends.push(end);
        }
        const joins = ends.filter((end) => !isTail(end));
        if (joins.length === ends.length) return null;
        while (p !== targets.at(-1) && (b[p] === 1 || b[p] === 2)) eol();
        if (p !== targets.at(-1)) return null;
        branches.push(parse(joins.length ? Math.min(...joins) : -1));
        if (joins.some((end) => end !== p && !onlyLines(Math.min(end, p), Math.max(end, p)))) return null;
        return branches;
    };
    const joinedBranches = <T>(targets: number[], parse: (until: number) => T[], allowLines: boolean): T[][] => {
        const branches: T[][] = [];
        let join = -1;
        // One branch: nothing to jump over (the draw still happens).
        if (targets.length === 1) {
            while (allowLines && p !== targets[0] && (b[p] === 1 || b[p] === 2)) eol();
            if (p !== targets[0]) fail("random target does not match branch boundary");
            return [[]];
        }
        for (let i = 0; i < targets.length; i++) {
            while (allowLines && p !== targets[i] && (b[p] === 1 || b[p] === 2)) eol();
            if (p !== targets[i]) fail("random target does not match branch boundary");
            const last = i === targets.length - 1;
            if (last && join < 0) fail("random branches without a join jump");
            branches.push(parse(last ? join : -1));
            // (the last branch may end with a jump to the join too)
            // (a 2E at or past the join belongs to the code around the Random)
            if (last && (b[p] !== 0x2e || p >= join)) break;
            if (b[p] !== 0x2e) fail("random branch requires join jump");
            ++p;
            const field = p;
            const end = field + 4 + word();
            // (branches may join at different points of one run of line ends - the same place)
            if (join >= 0 && end !== join && !onlyLines(Math.min(end, join), Math.max(end, join)))
                fail("random branches join at different places");
            join = join < 0 ? end : Math.min(join, end);
        }
        // The join may fall among the line ends the last branch's statement has already passed.
        if (targets.length > 1 && p !== join && !(allowLines && join < p && onlyLines(join, p)))
            fail("random branch does not end at the join");
        return branches;
    };
    // Whether [from, to) holds nothing but line ends.
    const onlyLines = (from: number, to: number) => {
        let at = from;
        while (at < to && (b[at] === 1 || b[at] === 2)) at += b[at] === 2 ? 5 : 1;
        return at === to;
    };
    // A script's tail: nothing but endifs, endswitches and line ends up to its endscript - the statement loop steps
    // over those, so from anywhere in it the script just ends. Returns where the script's code ends (past the '$'),
    // or -1. A few scripts were compiled with their closers out of order (CA_Scripts: a switch inside two ifs, its
    // endswitch after their endifs) or jump into the tail from inside a loop (a Random's branches past the repeat):
    // such a jump is a return, and closers in the tail close whatever is still open.
    const tailEnd = (at: number): number => {
        while (at < b.length && (b[at] === 1 || b[at] === 2 || b[at] === 0x28 || b[at] === 0x3d))
            at += b[at] === 2 ? 5 : 1;
        return b[at] === 0x24 ? at + 1 : -1;
    };
    // (a jump may land just past the '$' - outside the stored code: the original read whatever followed; here it
    // ends the script as the tail does)
    const pastEnd = (at: number) =>
        at >= 2 &&
        b[at - 1] === 0x24 &&
        (b[at - 2] === 1 || b[at - 6] === 2) &&
        [0, 1, 2, 0x23, 0x16, 0x2b].includes(b[at]!);
    const isTail = (at: number) => tailEnd(at) >= 0 || pastEnd(at);
    const tailHasEndswitch = (at: number) => {
        for (; b[at] !== 0x24; at += b[at] === 2 ? 5 : 1) if (b[at] === 0x3d) return true;
        return false;
    };
    const returning = (offset: number): Statement => ({ kind: "return", offset, fields: [] });
    let lineEnded = false;
    // (the last statement read was a command that ended at a Random's jump, no line end before it)
    let commandAtJump = false;
    const heights = new WeakMap<Expr, number>();
    const bounded = (v: Expr): Expr => {
        const children =
            v.kind === "expressionResult"
                ? [v.value]
                : v.kind === "binary"
                  ? [v.left, v.right]
                  : v.kind === "randomChoice"
                    ? v.choices
                    : v.kind === "array"
                      ? v.items
                      : v.kind === "structure"
                        ? v.fields.flatMap((f) => ("spread" in f || "kind" in f ? [] : [f.value]))
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
                } else {
                    const operand = value(depth + 1, true, afterDot);
                    // A name followed by a value (not an operator): a C function's arguments, up to the ')'.
                    if (operand.kind === "global" && ARGUMENT_STARTS.includes(b[p]!)) {
                        node();
                        values.push(
                            bounded({
                                kind: "functionCall",
                                offset: operand.offset,
                                name: operand.name,
                                fields: parameters(depth + 1),
                            }),
                        );
                    } else values.push(operand);
                }
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
        if (op === 0x30 || op === 0x38) {
            // parse_expression's operand switch contains 30, not 38.
            if (inExpression && op === 0x38) fail("RandomRange2 inside expressions is unsupported by original parser");
            // The bounds are a pair token (parse_random_range reads the floats after its 1F).
            if (byte() !== 0x1f) fail("expected RandomRange bounds pair");
            return { kind: "randomRange", offset, a: word(), b: word(), second: op === 0x38 };
        }
        if ([0x2f, 0x37, 0x40, 0x41].includes(op)) {
            --p;
            const { opcode, weights, targets } = randomTable();
            const count = weights.length;
            // A choice may sit on lines of its own (Random(@<a> <newline> @<b>)): the statement then ends with it.
            let lined = false;
            const choices: Expr[] = [],
                ends: number[] = [];
            for (let i = 0; i < count; i++) {
                while (p !== targets[i] && (b[p] === 1 || b[p] === 2) && !inExpression) {
                    eol();
                    lined = true;
                }
                if (p !== targets[i]) fail("random target does not match contiguous value boundary");
                choices.push(value(depth + 1, inExpression, rawName));
                while ((b[p] === 1 || b[p] === 2) && !inExpression && i < count - 1) {
                    eol();
                    lined = true;
                }
                if (b[p] === 0x2e) {
                    ++p;
                    const field = p;
                    ends.push(field + 4 + word());
                } else if (i < count - 1) fail("random value branch requires join jump");
            }
            // The join lands among the line ends after the last choice (some a nested Random already passed).
            const before = p;
            const at = lined || ends.some((end) => end !== p) ? lines() : new Set([p]);
            if (ends.some((end) => !at.has(end) && !(end <= before && onlyLines(end, before))))
                fail("random value jump does not target join boundary");
            if (p !== before || lined) {
                // On lines: the statement ends with it - or, in a structure, its line ends only separate members.
                if (b[p] !== 0x04) {
                    if (b[p] !== 0x24 && !STATEMENT_STARTS(b[p]!)) fail("random value on lines inside a statement");
                    lineEnded = true;
                }
            }
            return bounded({ kind: "randomChoice", offset, opcode, weights, choices });
        }
        if (op === 0x16) {
            const n = word();
            return inExpression && !rawName
                ? { kind: "global", offset, name: n }
                : { kind: "literal", offset, value: { type: 13, bits: n } };
        }
        if (op === 0x17 || op === 0x18 || op === 0x1a)
            return { kind: "literal", offset, value: { type: op === 0x1a ? 2 : 1, bits: word() } };
        if (op === 0x2c) return { kind: "allParameters", offset, ...(inExpression ? {} : { copy: true as const }) };
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
        if (op === 0x23) {
            if (inExpression) fail("unsupported expression operand token 0x23");
            const { name: n, defaults, body } = scriptParts();
            return { kind: "inlineScript", offset, name: n, defaults, body };
        }
        if (op === 0x0e) {
            const result = expression(depth + 1, rawName);
            // Only parse_value -> parse_expression calls GetResult. A '(' reached
            // via a random branch inside an active expression is grouping in
            // that same evaluator, just like the direct grouping path above.
            return inExpression ? result : bounded({ kind: "expressionResult", offset, value: result });
        }
        return fail(`unsupported value token 0x${op.toString(16)}`);
    };
    const parameters = (depth: number, structure = false, until = -1): Field[] => {
        if (depth > depthLimit) fail("value depth limit");
        const fields: Field[] = [];
        let flagEnd = -1; // (where the last field, a bare name, ended)
        if (structure) separators();
        while (
            (until < 0 || p < until) &&
            b[p] !== 0x2e &&
            (structure ? b[p] !== 4 : b[p] !== 1 && b[p] !== 2 && b[p] !== 0x24 && b[p] !== 0x0f)
        ) {
            need(1);
            if (lineEnded) break;
            // Right after a bare name (only line ends between, in a structure): the name's look-ahead for '=' has
            // resolved a Random here once already (resolve_random_jumps draws) - it is drawn twice.
            const afterFlag = flagEnd >= 0 && (p === flagEnd || (structure && onlyLines(flagEnd, p)));
            flagEnd = -1;
            // A Random among the fields: a value choice where it is one (below), else one branch's fields.
            if (!structure && [0x2f, 0x37, 0x40, 0x41].includes(b[p]!)) {
                const start = p,
                    count = nodes;
                try {
                    const item = value(depth);
                    if (afterFlag && item.kind === "randomChoice") item.twice = true;
                    fields.push({ name: 0, value: item, offset: start, flattenStructure: true });
                    continue;
                } catch {
                    p = start;
                    nodes = count;
                    lineEnded = false;
                }
                const table = randomTable();
                const branches = randomBranches(table.targets, (end) => parameters(depth + 1, false, end), false);
                node();
                fields.push({
                    kind: "randomFields",
                    offset: table.offset,
                    opcode: table.opcode,
                    weights: table.weights,
                    branches,
                    ...(afterFlag ? { twice: true as const } : {}),
                } satisfies RandomFields);
                continue;
            }
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
                if (afterFlag && !assigned && item.kind === "randomChoice") item.twice = true;
                // (a bare name: its look-ahead may draw a Random just after it)
                if (!assigned && item.kind === "literal" && item.value.type === 13) flagEnd = p;
                // parse_arguments/parse_structure recursively append anonymous
                // brace blocks into the same destination, rather than store
                // an unnamed nested Structure component.
                // (a script value is named by its own name, whatever it is assigned to - add_parameter)
                if (item.kind === "inlineScript") fields.push({ name: item.name, value: item, offset });
                else if (!assigned && item.kind === "structure") {
                    if (fields.length + item.fields.length > 4096) fail("field count limit");
                    fields.push(...item.fields);
                } else
                    fields.push({
                        name: n,
                        value: item,
                        offset,
                        ...(!assigned && ["parameter", "binary", "randomChoice", "expressionResult"].includes(item.kind)
                            ? { flattenStructure: true as const }
                            : {}),
                    });
            }
            if (structure) separators();
        }
        return fields;
    };
    // A script's defaults are parsed with no parameters (run_sub_script: parse_arguments(..., nullptr)): <x> and
    // <...> there add nothing (allowParams). A global's value may not read them at all.
    const staticDefaults = (fields: Field[], allowParams = false): void => {
        const check = (v: Expr): void => {
            if (!allowParams && (v.kind === "parameter" || v.kind === "allParameters"))
                fail("defaults/globals cannot read caller parameters");
            if (v.kind === "expressionResult") check(v.value);
            if (v.kind === "binary") {
                check(v.left);
                check(v.right);
            }
            if (v.kind === "randomChoice") {
                if (v.opcode === 0x40 || v.opcode === 0x41) fail("stored random defaults/globals are unsupported");
                v.choices.forEach(check);
            }
            if (v.kind === "structure") staticDefaults(v.fields, allowParams);
            if (v.kind === "array") v.items.forEach(check);
        };
        for (const f of fields) {
            if ("spread" in f) {
                if (!allowParams) fail("defaults/globals cannot spread caller parameters");
            } else if ("kind" in f) {
                for (const branch of f.branches) staticDefaults(branch, allowParams);
            } else check(f.value);
        }
    };
    // A global is parsed with no parameters (parse_symbol_definition: nullptr): in its structures <x> and <...> add
    // nothing (add_parameter / parse_arguments) - those fields are left out. Elsewhere (the value itself, an
    // expression) they still fail below.
    const withoutCallerParameters = (v: Expr): Expr => {
        if (v.kind === "array") return { ...v, items: v.items.map(withoutCallerParameters) };
        if (v.kind !== "structure") return v;
        const fields: Field[] = [];
        for (const f of v.fields) {
            if ("spread" in f) continue;
            if ("kind" in f) fields.push({ ...f, branches: f.branches.map((branch) => strip(branch)) });
            else if (f.value.kind === "parameter" || f.value.kind === "allParameters") continue;
            else fields.push({ ...f, value: withoutCallerParameters(f.value) });
        }
        return { ...v, fields };
    };
    const strip = (fields: Field[]): Field[] =>
        (withoutCallerParameters({ kind: "structure", offset: 0, fields }) as { fields: Field[] }).fields;
    // A command's name as read_name reads it: written, or from a parameter (<x>; <...> its unnamed name).
    const nameRef = (): NameRef => {
        if (b[p] === 0x2c) {
            ++p;
            return { via: 0 };
        }
        if (b[p] === 0x2d) {
            ++p;
            return { via: name() };
        }
        // Anything else names nothing (read_name: 0, not moved past) - the statement still reads its arguments.
        if (b[p] !== 0x16) return { name: 0 };
        return { name: name() };
    };
    const call = (): Call => {
        const offset = p;
        if (b[p] === 0x0e) {
            const byExpr = value(0);
            if (b[p] === 0x42) {
                ++p;
                const command = nameRef();
                return {
                    kind: "call",
                    offset,
                    name: "name" in command ? command.name : 0,
                    ...("via" in command ? { via: command.via } : {}),
                    fields: parameters(0),
                    onExpr: byExpr,
                };
            }
            return { kind: "call", offset, name: 0, byExpr, fields: parameters(0) };
        }
        let command = nameRef(),
            on: NameRef | undefined;
        if (b[p] === 0x42) {
            ++p;
            on = command;
            command = nameRef();
        }
        return {
            kind: "call",
            offset,
            name: "name" in command ? command.name : 0,
            ...("via" in command ? { via: command.via } : {}),
            fields: parameters(0),
            ...(on ? { on } : {}),
        };
    };
    const block = (depth: number, loops: number[] = [], until = -1): Statement[] => {
        if (depth > 64) fail("statement nesting limit");
        const body: Statement[] = [];
        lines();
        while ((until < 0 || p < until) && ![0x21, 0x24, 0x28, 0x2e, 0x3d, 0x3e, 0x3f, 0x48, 0x49].includes(b[p]!)) {
            need(1);
            node();
            const offset = p;
            lineEnded = false;
            commandAtJump = false;
            // A Random among statements (a Random value starts with a name or a value, never a statement).
            if ([0x2f, 0x37, 0x40, 0x41].includes(b[p]!)) {
                const table = randomTable();
                // (which branches end with a command right at their jump - no line end: see below)
                // (by where each branch starts: a branch may be parsed twice - randomBranches' retry)
                const atJump = new Map<number, number>(); // branch start -> its jump's target

                const branches = randomBranches(
                    table.targets,
                    (end) => {
                        const start = p;
                        commandAtJump = false;
                        const statements = block(depth + 1, loops, end);
                        if (b[p] === 0x2e && commandAtJump) atJump.set(start, p + 5 + b.readUInt32LE(p + 1));
                        return statements;
                    },
                    true,
                    returning,
                );
                // A branch's command that ends at the jump, not at a line end, goes on reading its arguments after the
                // join (parse_arguments follows the jump) up to the line end: those tokens are its arguments, and that
                // line runs as statements only in the other branches (pc_ is past it after the command).
                const joins = table.targets.map((target) => atJump.get(target));
                const join = joins.find((j) => j !== undefined);
                const consumed = joins.map((j) => j !== undefined && ![1, 2, 0x24].includes(b[j]!));
                if (join !== undefined && consumed.some(Boolean)) {
                    if (joins.some((j) => j !== undefined && j !== join))
                        fail("commands reading on past different joins");
                    const after = p;
                    p = join;
                    const extra = parameters(0);
                    const lineEnd = p;
                    if (![1, 2].includes(b[lineEnd]!))
                        fail(
                            "a command reading on past a Random's join into a token it can't read (the original hangs)",
                        );
                    p = join;
                    const line = block(depth + 1, loops, lineEnd);
                    // (the statement took its line end too: back to it)
                    if (p < lineEnd) fail("a command reading on past a Random's join: its line isn't whole statements");
                    p = Math.max(lineEnd, after);
                    branches.forEach((branch, i) => {
                        if (consumed[i]) {
                            const call = branch.at(-1) as Call;
                            branch[branch.length - 1] = { ...call, fields: [...call.fields, ...extra] };
                        } else branch.push(...line);
                    });
                }
                body.push({ kind: "randomBlock", offset, opcode: table.opcode, weights: table.weights, branches });
                lines();
                continue;
            }
            if (b[p] === 0x3c) {
                ++p;
                const fields = parameters(0);
                eol();
                lines();
                // Statements before the first case never run (run_multi_branch steps over every token to a case).
                if (![0x3d, 0x3e, 0x3f].includes(b[p]!)) block(depth + 1, loops);
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
                    // A case's statements end with a jump past the switch - or, the last case, run into the
                    // endswitch (run_multi_branch / the statement loop step over it).
                    if (b[p] === 0x49) {
                        ++p;
                        exits.push(p + short());
                    } else if (b[p] !== 0x3d && tailEnd(p) < 0) fail("expected case exit jump");
                    // Only an immediate next case after an empty body is a
                    // grouped label. Its arguments still parse after a match.
                    const grouped = branch.length === 0 && b[p] === 0x3e;
                    lines();
                    if ((next !== p || ![0x3e, 0x3f, 0x3d].includes(b[p]!)) && !(tailEnd(p) >= 0 && isTail(next)))
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
                    // (the default's own jump is never taken: run_multi_branch steps over 3F 49 xx xx)
                    if (byte() !== 0x49) fail("expected compiled default jump");
                    short();
                    fallback = block(depth + 1, loops);
                }
                const end = p;
                // (in the script's tail its endswitch may come after other closers: left to the tail - when there is
                // one: without it run_multi_branch never gets past the endscript. One difference stays: CA_Scripts'
                // last case's jump goes straight to its endscript, so there a selector no case matches holds the
                // original in its switch for ever; here the script ends.)
                const tail = b[p] !== 0x3d && tailEnd(p) >= 0 && tailHasEndswitch(p);
                if (!tail && byte() !== 0x3d) fail("expected endswitch (pre-case statements are unsupported)");
                // An exit may land on the endswitch or past it, on the lines that follow.
                const after = tail ? new Set<number>() : lines();
                after.add(end);
                const exitsTo = (at: number) => after.has(at) || (tailEnd(end) >= 0 && isTail(at));
                if (!exits.every(exitsTo)) fail("case exit does not target endswitch");
                body.push({ kind: "switch", offset, fields, cases, ...(fallback === undefined ? {} : { fallback }) });
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
                const negate = b[p] === 0x39;
                if (negate) ++p;
                // `if name = value`: an assignment - it runs, and a statement that isn't a command counts as true.
                const assigning =
                    (b[p] === 0x16 && b[p + 5] === 7) || (b[p] === 0x2d && b[p + 1] === 0x16 && b[p + 6] === 7);
                const condition: Call | Expr | Assign = assigning
                    ? (() => {
                          const at = p;
                          if (b[p] === 0x2d) ++p;
                          const n = name();
                          ++p;
                          return { kind: "assign" as const, offset: at, name: n, value: value(0) };
                      })()
                    : [0x16, 0x2c, 0x2d].includes(b[p]!)
                      ? call()
                      : b[p] === 0x0e
                        ? value(0)
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
                // An if whose false jump goes to the script's tail while its statements run on into the code around
                // it (z_el: `if c ... repeat endif`): if not, the script ends - its endif is one of the tail's.
                if (b[p] !== 0x28 && endDestination === undefined && isTail(destination) && tailEnd(p) < 0) {
                    no = [returning(offset)];
                    body.push({ kind: "if", offset, condition, ...(negate ? { negate: true as const } : {}), yes, no });
                    continue;
                }
                const tail = b[p] !== 0x28 && tailEnd(p) >= 0;
                if (!tail && byte() !== 0x28) fail("expected endif");
                const ends = tail ? new Set<number>() : lines();
                ends.add(endOffset);
                const lands = (at: number) => ends.has(at) || (tailEnd(endOffset) >= 0 && isTail(at));
                if (endDestination !== undefined) {
                    if (!lands(endDestination)) fail("else jump does not target endif boundary");
                } else if (!lands(destination)) fail("if jump does not target endif boundary");
                body.push({ kind: "if", offset, condition, ...(negate ? { negate: true as const } : {}), yes, no });
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
                // An assignment takes one value: another statement may follow on the same line (a = 1 b = 2).
                if (!lineEnded && b[p] !== 1 && b[p] !== 2 && b[p] !== 0x24 && STATEMENT_STARTS(b[p]!)) continue;
            } else body.push(call());
            // A statement in a Random's branch may end at the branch's jump (the argument loop follows it).
            if (b[p] === 0x2e && !lineEnded) {
                commandAtJump = body.at(-1)?.kind === "call";
                continue;
            }
            endLine();
        }
        return body;
    };
    // A script after its 23: its name, its defaults' line, its statements up to the endscript.
    function scriptParts() {
        const n = name(),
            defaults = parameters(0);
        staticDefaults(defaults, true);
        eol();
        const ended = lineEnded;
        lineEnded = false;
        const body = block(0);
        // (closers left over in the tail: see tailEnd)
        if (b[p] !== 0x24 && tailEnd(p) >= 0) p = tailEnd(p) - 1;
        if (byte() !== 0x24) fail("expected endscript");
        lineEnded = ended;
        return { name: n, defaults, body };
    }
    const definitions: Definition[] = [];
    const names = new Map<number, string>();
    lines();
    while (b[p] !== 0) {
        need(1);
        node();
        const offset = p;
        if (b[p] === 0x2b) {
            ++p;
            const checksum = word();
            const zero = b.indexOf(0, p);
            if (zero < 0) fail("unterminated checksum-name record");
            names.set(checksum, b.toString("latin1", p, zero));
            p = zero + 1;
            lines();
            continue;
        }
        if (b[p] === 0x23) {
            ++p;
            const { name: n, defaults, body } = scriptParts();
            definitions.push({ kind: "script", offset, name: n, defaults, body });
        } else if (b[p] === 0x16) {
            const n = name();
            lines();
            if (byte() !== 7) fail("expected global assignment");
            lines();
            const rhs = withoutCallerParameters(value(0));
            staticDefaults([{ name: n, value: rhs, offset }]);
            definitions.push({ kind: "global", offset, name: n, value: rhs });
        } else fail(`unsupported definition token 0x${b[p]!.toString(16)}`);
        lines();
    }
    // (bytes after the end-of-file token are never read: read_qb_file and its names pass stop there)
    ++p;
    return { format: 2, source, definitions, ...(names.size ? { names: Object.fromEntries(names) } : {}) };
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
        const cap = MODULE_CAP;
        if (metadata.size > cap) throw new Error(`${input}: QB offset 0x0: module exceeds 64 MiB`);
        const bytes = Buffer.allocUnsafe(cap + 1);
        let count = 0;
        for (;;) {
            const n = readSync(fd, bytes, count, bytes.length - count, null);
            if (n === 0) break;
            count += n;
            if (count > cap) throw new Error(`${input}: QB offset 0x0: module exceeds 64 MiB`);
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
