// NOT ORIGINAL - offline QB -> typed IR -> JavaScript generator translator.
// Format/behavior references: src/game/script/qb.* and docs/script-runtime.md.
import { readFileSync, writeFileSync } from "node:fs";
import { pathToFileURL } from "node:url";

export type Scalar = { type: 1 | 2 | 13; bits: number };
export type Value = { kind: "literal"; value: Scalar } | { kind: "parameter"; name: number };
export type Field = { name: number; value: Value } | { spread: true };
export type Call = { kind: "call"; offset: number; name: number; fields: Field[] };
export type Statement =
    | Call
    | { kind: "assign"; offset: number; name: number; value: Value }
    | { kind: "return"; offset: number; fields: Field[] }
    | { kind: "if"; offset: number; condition: Call | Value; yes: Statement[]; no: Statement[] };
export type Script = { name: number; offset: number; defaults: Field[]; body: Statement[] };
export type Module = { source: string; scripts: Script[] };
export { checksum } from "../qb_checksum.ts";

export function parse(bytes: Uint8Array, source = "input.qb"): Module {
    if (bytes.length > 4 * 1024 * 1024) throw new Error(`${source}: module exceeds 4 MiB`);
    const b = Buffer.from(bytes);
    let p = 0;
    let count = 0;
    const fail = (message: string): never => {
        throw new Error(`${source}: QB offset 0x${p.toString(16)}: ${message}`);
    };
    const need = (n: number) => {
        if (n > b.length - p) fail("truncated token");
    };
    const byte = () => {
        need(1);
        return b[p++]!;
    };
    const word = () => {
        need(4);
        const v = b.readUInt32LE(p);
        p += 4;
        return v;
    };
    const short = () => {
        need(2);
        const v = b.readUInt16LE(p);
        p += 2;
        return v;
    };
    const step = () => {
        if (++count > 100000) fail("IR node limit");
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
    const skip = () => {
        while (b[p] === 1 || b[p] === 2) eol();
    };
    const value = (): Value => {
        step();
        const op = byte();
        if (op === 0x2d) return { kind: "parameter", name: name() };
        if (op === 0x16 || op === 0x17 || op === 0x18 || op === 0x1a)
            return { kind: "literal", value: { type: op === 0x16 ? 13 : op === 0x1a ? 2 : 1, bits: word() } };
        return fail(`unsupported value token 0x${op.toString(16)}`);
    };
    const fields = (): Field[] => {
        const out: Field[] = [];
        while (b[p] !== 1 && b[p] !== 2 && b[p] !== 0x24) {
            need(1);
            step();
            if (b[p] === 0x2c) {
                ++p;
                out.push({ spread: true });
                continue;
            }
            let n = 0;
            if (b[p] === 0x16 && p + 5 < b.length && b[p + 5] === 7) {
                n = name();
                ++p;
            }
            out.push({ name: n, value: value() });
        }
        return out;
    };
    const call = (): Call => {
        const offset = p;
        const n = name();
        return { kind: "call", offset, name: n, fields: fields() };
    };
    const block = (depth: number): Statement[] => {
        if (depth > 64) fail("block nesting limit");
        const out: Statement[] = [];
        skip();
        while (b[p] !== 0x24 && b[p] !== 0x28 && b[p] !== 0x48) {
            step();
            const offset = p;
            need(1);
            if (b[p] === 0x47) {
                ++p;
                const target = p + short();
                let condition: Call | Value;
                if (b[p] === 0x16) condition = call();
                else {
                    if (byte() !== 0x0e) fail("expected parenthesized condition");
                    condition = value();
                    if (byte() !== 0x0f) fail("expected close parenthesis");
                }
                eol();
                const yes = block(depth + 1);
                let no: Statement[] = [];
                if (b[p] === 0x48) {
                    ++p;
                    const end = p + short();
                    if (target !== p) fail("if jump does not target the else body");
                    skip();
                    no = block(depth + 1);
                    if (end !== p) fail("else jump does not target endif");
                } else if (target !== p) fail("if jump does not target endif");
                if (byte() !== 0x28) fail("expected endif");
                out.push({ kind: "if", offset, condition, yes, no });
                skip();
                continue;
            }
            if (b[p] === 0x29) {
                ++p;
                out.push({ kind: "return", offset, fields: fields() });
            } else if ((b[p] === 0x16 && b[p + 5] === 7) || (b[p] === 0x2d && b[p + 1] === 0x16 && b[p + 6] === 7)) {
                if (b[p] === 0x2d) ++p;
                const n = name();
                ++p;
                out.push({ kind: "assign", offset, name: n, value: value() });
            } else out.push(call());
            if (b[p] !== 0x24) eol();
            skip();
        }
        return out;
    };
    const scripts: Script[] = [];
    const seen = new Set<number>();
    skip();
    while (b[p] !== 0) {
        need(1);
        const offset = p;
        if (byte() !== 0x23) fail("only script definitions are supported");
        const n = name();
        if (seen.has(n)) fail("duplicate script definition");
        seen.add(n);
        const defaults = fields();
        if (defaults.some((f) => "spread" in f || f.value.kind === "parameter"))
            fail("defaults cannot read caller parameters");
        eol();
        const body = block(0);
        if (byte() !== 0x24) fail("expected endscript");
        scripts.push({ name: n, offset, defaults, body });
        skip();
    }
    ++p;
    if (p !== b.length) fail("trailing bytes after EOF");
    return { source, scripts };
}

export function emit(module: Module): string {
    const v = (x: Value): string => (x.kind === "literal" ? JSON.stringify(x.value) : `qb.get(p, ${x.name})`);
    const f = (xs: Field[]): string =>
        `qb.fields(p, [${xs.map((x) => ("spread" in x ? "{spread:true}" : `[${x.name}, ${v(x.value)}]`)).join(", ")}])`;
    const site = (script: Script, offset: number) =>
        JSON.stringify({ source: module.source, script: script.name, offset });
    const c = (x: Call, s: Script) => `yield* qb.call(${x.name}, ${f(x.fields)}, p, ${site(s, x.offset)})`;
    const statements = (xs: Statement[], s: Script, indent = "  "): string =>
        xs
            .map((x) => {
                const mark = `${indent}qb.step(${site(s, x.offset)});\n`;
                if (x.kind === "call") return `${mark}${indent}${c(x, s)};\n`;
                if (x.kind === "assign") return `${mark}${indent}qb.assign(p, ${x.name}, ${v(x.value)});\n`;
                if (x.kind === "return")
                    return `${mark}${indent}return root ? qb.merge(p, ${f(x.fields)}) : ${f(x.fields)};\n`;
                const condition =
                    x.condition.kind === "call" ? `(${c(x.condition, s)})` : `qb.truth(${v(x.condition)})`;
                return `${mark}${indent}if (${condition}) {\n${statements(x.yes, s, `${indent}  `)}${indent}} else {\n${statements(x.no, s, `${indent}  `)}${indent}}\n`;
            })
            .join("");
    const manifest = {
        format: 1,
        source: module.source,
        scripts: module.scripts.map((s) => ({ name: s.name, offset: s.offset })),
    };
    return (
        `// Generated from QB; do not edit.\nqb.module(${JSON.stringify(manifest)});\n` +
        module.scripts
            .map(
                (s) =>
                    `qb.define(${s.name}, function* (args, root) {\n  let p = [];\n  p = qb.merge(${f(s.defaults)}, args);\n  qb.frame(p, root);\n${statements(s.body, s)}  return root ? p : [];\n});\n`,
            )
            .join("")
    );
}
export const translate = (bytes: Uint8Array, source?: string): string => emit(parse(bytes, source));
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    const [, , input, output] = process.argv;
    if (!input || !output || process.argv.length !== 4)
        throw new Error("Usage: node tools/qbjs/translate.ts INPUT.qb OUTPUT.js");
    writeFileSync(output, translate(readFileSync(input), input));
}
