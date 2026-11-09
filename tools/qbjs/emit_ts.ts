// NOT ORIGINAL - format-2 IR -> readable TypeScript. The output is meant to be read and edited as the scripts'
// source: names from the module's checksum-name records, QB's control flow as TypeScript's, values as literals.
// Its run-time API (the imports from "qb") is described in docs/qb-ts.md.

import { writeFileSync } from "node:fs";
import { pathToFileURL } from "node:url";
import { checksum } from "../qb_checksum.ts";
import type { Call, Definition, Expr, Field, Literal, ModuleIR, Statement } from "./ir.ts";
import { readIRFile } from "./reader.ts";

const WIDTH = 120;
const INDENT = "    ";

// Words a command can't be bound to as a plain identifier (called as cmd.X instead).
const RESERVED = new Set(
    (
        "break case catch class const continue debugger default delete do else enum export extends false finally for " +
        "function if import in instanceof new null return super switch this throw true try typeof var void while with " +
        "yield let static implements interface package private protected public await arguments eval undefined NaN " +
        "Infinity"
    ).split(" "),
);
// The "qb" module's exports, imported by name.
const HELPERS = [
    "script",
    "call",
    "run",
    "on",
    "callOn",
    "global",
    "cmd",
    "N",
    "fn",
    "G",
    "f",
    "fbits",
    "vec",
    "pair",
    "L",
    "all",
    "params",
    "val",
    "test",
    "is",
    "select",
    "add",
    "sub",
    "mul",
    "div",
    "eq",
    "lt",
    "gt",
    "and",
    "or",
    "at",
    "dot",
    "s",
    "repeat",
    "random",
    "random2",
    "randomNoRepeat",
    "randomPermute",
    "randomRange",
    "randomRange2",
    "code",
] as const;
const OPERATORS: Record<number, string> = {
    5: "at",
    7: "eq",
    8: "dot",
    10: "sub",
    11: "add",
    12: "div",
    13: "mul",
    18: "lt",
    20: "gt",
    50: "or",
    51: "and",
};
const RANDOM: Record<number, string> = { 47: "random", 55: "random2", 64: "randomNoRepeat", 65: "randomPermute" };

const isIdentifier = (s: string) => /^[A-Za-z_$][\w$]*$/.test(s);
const hex = (n: number) => `_0x${n.toString(16).padStart(8, "0")}`;

// A float32's shortest decimal spelling that reads back to the same bits, or undefined when it can't be written as
// a plain number (an integral value would read as an Integer; -0, infinities and NaNs keep their bits).
function floatText(bits: number): string {
    const f32 = new Float32Array(new Uint32Array([bits]).buffer)[0]!;
    if (!Number.isFinite(f32)) return `fbits(0x${bits.toString(16)})`;
    if (Object.is(f32, -0)) return "f(-0)";
    for (let digits = 1; digits <= 9; ++digits) {
        const text = String(Number(f32.toPrecision(digits)));
        if (Math.fround(Number(text)) === f32) return Number.isInteger(Number(text)) ? `f(${text})` : text;
    }
    return `fbits(0x${bits.toString(16)})`;
}
const stringText = (bytes: number[]) =>
    `"${bytes
        .map((c) =>
            c === 0x22
                ? '\\"'
                : c === 0x5c
                  ? "\\\\"
                  : c === 0x0a
                    ? "\\n"
                    : c === 0x09
                      ? "\\t"
                      : c >= 0x20 && c < 0x7f
                        ? String.fromCharCode(c)
                        : `\\x${c.toString(16).padStart(2, "0")}`,
        )
        .join("")}"`;

// A rendered piece of code: flat text, or a head / items / tail that can break over lines (broken, an object's
// "{ " / " }" lose their spaces). A call whose only argument is an object or array hugs it: Foo({ ... }).
// sep joins the items (", " unless set); last ends the last item of a broken list ("," unless set). A head-less
// list with its own sep (a || chain) breaks into one item per line at the same indent.
type Doc = string | { head: string; items: Doc[]; tail: string; sep?: string; last?: string };
// (a string may hold lines - a script written as a value, code(function* ...): its lines after the first are indented
// where it lands, and what holds it is always broken over lines)
function flat(d: Doc): string {
    return typeof d === "string" ? d : d.head + d.items.map(flat).join(d.sep ?? ", ") + d.tail;
}
const bracketed = (d: Doc) => typeof d !== "string" && (d.head === "{ " || d.head === "[") && d.items.length > 0;
function layout(d: Doc, indent: string, used: number): string {
    const text = flat(d);
    if (typeof d === "string") return text.replace(/\n/g, `\n${indent}`);
    if ((used + text.length <= WIDTH && !text.includes("\n")) || !d.items.length) return text;
    if (d.items.length === 1 && d.head.endsWith("(") && bracketed(d.items[0]!))
        return d.head + layout(d.items[0]!, indent, used + d.head.length) + d.tail;
    if (d.items.length === 1 && !d.head.endsWith("(") && !d.head.endsWith("[") && d.head !== "{ ")
        return d.head + layout(d.items[0]!, indent, used + d.head.length) + d.tail;
    if (d.head === "" && d.sep !== undefined)
        return (
            d.items
                .map((item, i) => (i ? indent : "") + layout(item, indent, i ? indent.length : used))
                .join(`${d.sep.trimEnd()}\n`) + d.tail
        );
    const inner = indent + INDENT;
    const head = d.head === "{ " ? "{" : d.head.trimEnd();
    const tail = d.tail === " }" ? "}" : d.tail.trimStart();
    const sep = (d.sep ?? ", ").trimEnd();
    const lines = d.items.map(
        (item, i) => inner + layout(item, inner, inner.length) + (i < d.items.length - 1 ? sep : (d.last ?? ",")),
    );
    return `${head}\n${lines.join("\n")}\n${indent}${tail}`;
}

export function emitTS(module: ModuleIR, names: Record<number, string> = {}): string {
    // A spelling is used only when it checksums back to the name (else the name is written as _0x<checksum>).
    const nameOf = (n: number) => {
        const s = module.names?.[n] ?? names[n];
        return s !== undefined && checksum(s) === n ? s : undefined;
    };
    const key = (n: number) => {
        const s = nameOf(n);
        return s === undefined ? hex(n) : isIdentifier(s) ? s : JSON.stringify(s);
    };
    const member = (object: string, n: number) => {
        const s = nameOf(n);
        return s === undefined
            ? `${object}.${hex(n)}`
            : isIdentifier(s)
              ? `${object}.${s}`
              : `${object}[${JSON.stringify(s)}]`;
    };
    const used = new Set<string>();
    const use = (h: (typeof HELPERS)[number]) => {
        used.add(h);
        return h;
    };
    // Commands bound by destructuring cmd; any other (unnamed, reserved, clashing) is called as cmd.X.
    const commands = new Map<number, string>();
    const commandRef = (n: number) => {
        const s = nameOf(n);
        if (
            s !== undefined &&
            isIdentifier(s) &&
            !RESERVED.has(s) &&
            !(HELPERS as readonly string[]).includes(s) &&
            s !== "p" &&
            !/^(on|loop)\d*$/.test(s)
        ) {
            commands.set(n, s);
            return s;
        }
        return member(use("cmd"), n);
    };
    let randomSite = 0;
    // A float where only a float can be (vector / pair coordinates, random ranges): whole numbers need no f().
    const coordinate = (c: number) =>
        floatText(c)
            .replace(/^f\((-?\d+)\)$/, "$1")
            .replace(/^fbits/, () => use("fbits"));
    // random(k, [weights,] [choices]) - k numbers the site in the module (no-repeat / permute state is per site).
    const randomDoc = (opcode: number, weights: number[], choices: Doc[]): Doc => ({
        head: `${use(RANDOM[opcode] as (typeof HELPERS)[number])}(${++randomSite}, `,
        items: [
            ...(weights.every((w) => w === 1) ? [] : [`[${weights.join(", ")}]`]),
            { head: "[", items: choices, tail: "]" },
        ],
        tail: ")",
    });
    // () => body; an object literal body in parentheses.
    const arrow = (body: Doc): Doc =>
        flat(body).startsWith("{")
            ? { head: "() => (", items: [body], tail: ")", last: "" }
            : { head: "() => ", items: [body], tail: "" };

    const literal = (v: Literal): string => {
        switch (v.type) {
            case 1:
                return String(v.bits | 0);
            case 2:
                return floatText(v.bits).replace(/^(f|fbits)\(/, (_m, h: (typeof HELPERS)[number]) => `${use(h)}(`);
            case 13:
                return member(use("N"), v.bits);
            case 3:
                return stringText(v.bytes);
            case 4:
                return `${use("L")}(${stringText(v.bytes)})`;
            case 5:
            case 6:
                return `${use(v.type === 6 ? "vec" : "pair")}(${v.coordinates.map(coordinate).join(", ")})`;
        }
    };
    // Expressions; `top` is false inside an operator's operands (where QB's grouping already shows).
    const expr = (x: Expr): Doc => {
        switch (x.kind) {
            case "literal":
                return literal(x.value);
            case "parameter":
                return member("p", x.name);
            case "global":
                return member(use("G"), x.name);
            case "functionCall":
                return { head: `${member(use("fn"), x.name)}(`, items: fieldGroups(x.fields), tail: ")" };
            case "allParameters":
                if (x.copy) return `${use("all")}(p)`;
                return `${use("params")}(p)`;
            case "expressionResult":
                return x.value.kind === "binary"
                    ? expr(x.value)
                    : { head: `${use("val")}(`, items: [expr(x.value)], tail: ")" };
            case "binary": {
                const op = OPERATORS[x.op];
                if (!op)
                    throw Error(
                        `${module.source}: QB offset 0x${x.offset.toString(16)}: operator 0x${x.op.toString(16)}`,
                    );
                return {
                    head: `${use(op as (typeof HELPERS)[number])}(`,
                    items: [expr(x.left), expr(x.right)],
                    tail: ")",
                };
            }
            case "randomRange":
                return `${use(x.second ? "randomRange2" : "randomRange")}(${coordinate(x.a)}, ${coordinate(x.b)})`;
            case "randomChoice": {
                const pure = x.choices.every((c) => c.kind === "literal");
                return randomDoc(
                    x.opcode,
                    x.weights,
                    x.choices.map((c) => (pure ? expr(c) : arrow(expr(c)))),
                );
            }
            case "structure":
                return fieldsDoc(x.fields, true);
            case "array":
                return { head: "[", items: x.items.map(expr), tail: "]" };
            case "inlineScript": {
                // code(defaults, function* (p) { ... }): its statements written as a script's, at their own indent.
                const [saved, savedSerial] = [lines, serial];
                lines = [];
                serial = 0;
                block(x.body, INDENT, new Map());
                const body = lines;
                [lines, serial] = [saved, savedSerial];
                return [`${use("code")}(${defaultsText(x.defaults)}function* (p) {`, ...body, "})"].join("\n");
            }
        }
    };
    // A field list as call arguments (args) or a structure: runs of named fields become object literals, unnamed
    // values stay in their place. A structure that is one object literal is written as just that.
    const fieldGroups = (xs: Field[]): Doc[] => {
        const out: Doc[] = [];
        let group: Doc[] | undefined,
            keys = new Set<string>();
        for (const x of xs) {
            if ("spread" in x) {
                out.push(`${use("all")}(p)`);
                group = undefined;
                continue;
            }
            // A Random among the fields: ...random(k, [[branch 1's arguments], [branch 2's], ...]) spread in place
            // (a branch that draws or calls anything itself is written as a function).
            if ("kind" in x) {
                out.push({
                    head: "...",
                    items: [
                        randomDoc(
                            x.opcode,
                            x.weights,
                            x.branches.map((branch) => {
                                const doc: Doc = { head: "[", items: fieldGroups(branch), tail: "]" };
                                return /"kind":"(random\w*|functionCall)"/.test(JSON.stringify(branch))
                                    ? arrow(doc)
                                    : doc;
                            }),
                        ),
                    ],
                    tail: "",
                });
                group = undefined;
                continue;
            }
            if (x.name === 0) {
                out.push(expr(x.value));
                group = undefined;
                continue;
            }
            const k = key(x.name);
            if (!group || keys.has(k)) {
                group = [];
                keys = new Set();
                out.push({ head: "{ ", items: group, tail: " }" });
            }
            keys.add(k);
            group.push({ head: `${k}: `, items: [expr(x.value)], tail: "" });
        }
        return out;
    };
    const fieldsDoc = (xs: Field[], structure: boolean): Doc => {
        const groups = fieldGroups(xs);
        if (structure && groups.length === 1 && typeof groups[0] !== "string" && groups[0]!.head === "{ ")
            return groups[0]!;
        if (structure && !groups.length) return "{}";
        return structure ? { head: `${use("s")}(`, items: groups, tail: ")" } : { head: "", items: groups, tail: "" };
    };
    // A command named by a parameter: call(p.x, ...) (call(all(p), ...) for <...>). On an object (object:Command):
    // on(N.object).Command(...), callOn(N.object, p.x, ...) when the command is named by a parameter.
    const viaDoc = (via: number) => (via ? member("p", via) : `${use("all")}(p)`);
    const callDoc = (c: Call): Doc => {
        const args = fieldGroups(c.fields);
        // (expression) args: run(expression, ...args); a statement naming nothing: call(0, ...args).
        if (c.byExpr !== undefined)
            return { head: `yield* ${use("run")}(`, items: [runOperand(c.byExpr), ...args], tail: ")" };
        if (c.via === undefined && c.on === undefined && c.name === 0)
            return { head: `yield* ${use("call")}(`, items: ["0", ...args], tail: ")" };
        if (c.on !== undefined || c.onExpr !== undefined) {
            const object: Doc =
                c.onExpr !== undefined
                    ? arrow(runOperand(c.onExpr))
                    : c.on !== undefined && "name" in c.on
                      ? member(use("N"), c.on.name)
                      : viaDoc((c.on as { via: number }).via);
            if (c.via !== undefined)
                return { head: `yield* ${use("callOn")}(`, items: [object, viaDoc(c.via), ...args], tail: ")" };
            return { head: `yield* ${member(`${use("on")}(${flat(object)})`, c.name)}(`, items: args, tail: ")" };
        }
        return c.via === undefined
            ? { head: `yield* ${commandRef(c.name)}(`, items: args, tail: ")" }
            : { head: `yield* ${use("call")}(`, items: [viaDoc(c.via), ...args], tail: ")" };
    };

    // An if's condition: a call's result or an expression's truth, `!` in front for NOT.
    // test(x): an expression's truth - when it can't be a name; else (yield* run(x)), which calls a name.
    const truth = (e: Expr): Doc => {
        const top = e.kind === "expressionResult" ? e.value : e;
        const numeric = top.kind === "binary" && top.op !== 0x05 && top.op !== 0x08;
        return numeric || top.kind === "literal"
            ? { head: `${use("test")}(`, items: [expr(e)], tail: ")" }
            : { head: `yield* ${use("run")}(`, items: [runOperand(e)], tail: ")" };
    };
    // run takes (x) as an expression result itself: run(p.x), not run(val(p.x)).
    const runOperand = (e: Expr): Doc => expr(e.kind === "expressionResult" ? e.value : e);
    const conditionDoc = (x: Extract<Statement, { kind: "if" }>): Doc => {
        // (an assignment condition is written as a statement before the if: here it is just true)
        const c: Doc =
            x.condition.kind === "call"
                ? callDoc(x.condition)
                : x.condition.kind === "assign"
                  ? "true"
                  : truth(x.condition);
        if (!x.negate) return c;
        return typeof c !== "string" && c.head.startsWith("yield*")
            ? { head: "!(", items: [c], tail: ")", last: "" }
            : { head: "!", items: [c], tail: "" };
    };
    let lines: string[] = [];
    let serial = 0;
    const emit = (indent: string, d: Doc, suffix = "") =>
        lines.push(indent + layout(d, indent, indent.length) + suffix);
    const pure = (fs: Field[]) => {
        const f = fs[0];
        return (
            fs.length === 1 &&
            f !== undefined &&
            "name" in f &&
            f.name === 0 &&
            ["parameter", "literal", "global"].includes(f.value.kind)
        );
    };

    const block = (xs: Statement[], indent: string, loops: Map<number, string>) => {
        for (const x of xs) statement(x, indent, loops);
    };
    const statement = (x: Statement, indent: string, loops: Map<number, string>) => {
        const inner = indent + INDENT;
        switch (x.kind) {
            case "call":
                return emit(indent, callDoc(x), ";");
            case "assign":
                return emit(indent, { head: `${member("p", x.name)} = `, items: [expr(x.value)], tail: "" }, ";");
            case "return": {
                if (!x.fields.length) return lines.push(`${indent}return;`);
                return emit(indent, { head: "return ", items: [fieldsDoc(x.fields, true)], tail: "" }, ";");
            }
            case "if": {
                // `if x = 1`: the assignment runs, then the if (always true).
                if (x.condition.kind === "assign") statement(x.condition, indent, loops);
                emit(indent, { head: "if (", items: [conditionDoc(x)], tail: ") {", last: "" });
                block(x.yes, inner, loops);
                // else { if ... } reads as else if.
                let no = x.no;
                while (no.length === 1 && no[0]!.kind === "if" && no[0]!.condition.kind !== "assign") {
                    const y = no[0]! as Extract<Statement, { kind: "if" }>;
                    emit(indent, { head: "} else if (", items: [conditionDoc(y)], tail: ") {", last: "" });
                    block(y.yes, inner, loops);
                    no = y.no;
                }
                if (no.length) {
                    lines.push(`${indent}} else {`);
                    block(no, inner, loops);
                }
                return lines.push(`${indent}}`);
            }
            case "loop": {
                const nested = new Map(loops);
                if (!x.repeatFields.length) {
                    nested.set(x.offset, "");
                    lines.push(`${indent}while (true) {`);
                    block(x.body, inner, nested);
                    return lines.push(`${indent}}`);
                }
                const name = `loop${++serial === 1 ? "" : serial}`;
                nested.set(x.offset, name);
                lines.push(`${indent}const ${name} = ${use("repeat")}();`);
                lines.push(`${indent}do {`);
                block(x.body, inner, nested);
                // The count is read once, at the end of the first pass: a non-literal one is passed as a function.
                const groups = fieldGroups(x.repeatFields);
                const lazy = x.repeatFields.some((f) => !("name" in f) || f.value.kind !== "literal");
                return emit(indent, {
                    head: `} while (${name}.again(`,
                    items: lazy ? groups.map(arrow) : groups,
                    tail: "));",
                });
            }
            case "randomBlock": {
                // A Random among statements: const pickN = random(k, [0, 1, ...]); then one branch per value.
                const choices = x.branches.map((_, i) => String(i));
                if (x.branches.length === 1) return emit(indent, randomDoc(x.opcode, x.weights, choices), ";");
                const name = `pick${++serial}`;
                emit(
                    indent,
                    { head: `const ${name} = `, items: [randomDoc(x.opcode, x.weights, choices)], tail: "" },
                    ";",
                );
                x.branches.forEach((branch, i) => {
                    const last = i === x.branches.length - 1;
                    lines.push(`${indent}${i ? "} else " : ""}${last ? "{" : `if (${name} === ${i}) {`}`);
                    block(branch, inner, loops);
                });
                return lines.push(`${indent}}`);
            }
            case "break":
                return lines.push(`${indent}break;`);
            case "switch": {
                let selector: Doc;
                if (pure(x.fields)) selector = expr((x.fields[0] as Extract<Field, { name: number }>).value);
                else {
                    const name = `on${++serial}`;
                    emit(
                        indent,
                        { head: `const ${name} = ${use("select")}(`, items: fieldGroups(x.fields), tail: ")" },
                        ";",
                    );
                    selector = name;
                }
                x.cases.forEach((group, i) => {
                    const labels: Doc[] = group.labels.map((l) => ({
                        head: `${use("is")}(`,
                        items: [selector, ...fieldGroups(l.fields)],
                        tail: ")",
                    }));
                    const condition: Doc =
                        labels.length === 1 ? labels[0]! : { head: "", items: labels, tail: "", sep: " || " };
                    emit(indent, { head: i ? "} else if (" : "if (", items: [condition], tail: ") {", last: "" });
                    block(group.body, inner, loops);
                });
                if (x.fallback?.length) {
                    lines.push(x.cases.length ? `${indent}} else {` : `${indent}{`);
                    block(x.fallback, inner, loops);
                }
                if (x.cases.length || x.fallback?.length) lines.push(`${indent}}`);
                return;
            }
        }
    };
    // A script's defaults, made at each call: written as a function when making them draws a random number or reads
    // parameters (none: defaults are parsed without the caller's - run_sub_script).
    function defaultsText(defaults: Field[]): string {
        if (!defaults.length) return "";
        const made = flat(fieldsDoc(defaults, true));
        return /\brandom\w*\(|\bp\b/.test(made) ? `(p) => (${made}), ` : `${made}, `;
    }
    const definition = (d: Definition) => {
        if (d.kind === "global")
            return emit(
                "",
                {
                    head: `${use("global")}(${JSON.stringify(nameOf(d.name) ?? hex(d.name))}, `,
                    items: [expr(d.value)],
                    tail: ")",
                },
                ";",
            );
        serial = 0;
        const head = `${use("script")}(${JSON.stringify(nameOf(d.name) ?? hex(d.name))}, `;
        lines.push(`${head}${defaultsText(d.defaults)}function* (p) {`);
        block(d.body, INDENT, new Map());
        lines.push("});");
    };
    const body: string[] = [];
    module.definitions.forEach((d, i) => {
        lines = [];
        const previous = module.definitions[i - 1];
        if (i && (d.kind === "script" || previous?.kind === "script")) body.push("");
        definition(d);
        body.push(...lines);
    });
    const sorted = [...new Set(commands.values())].sort((a, b) => a.localeCompare(b, "en", { sensitivity: "base" }));
    if (sorted.length) used.add("cmd");
    const header = [`// ${module.source.replace(/\\/g, "/")} - translated from QB.`];
    header.push(`import { ${HELPERS.filter((h) => used.has(h)).join(", ")} } from "qb";`);
    if (sorted.length) header.push(layout({ head: "const { ", items: sorted, tail: " } = cmd;" }, "", 0));
    return `${header.join("\n")}\n\n${body.join("\n")}\n`;
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    const [, , input, output] = process.argv;
    if (!input || !output || process.argv.length !== 4)
        throw Error("Usage: node tools/qbjs/emit_ts.ts INPUT.qb OUTPUT.ts");
    writeFileSync(output, emitTS(readIRFile(input, output)));
}
