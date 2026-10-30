// NOT ORIGINAL - format-2 IR -> generators. Requires the coordinator's v2
// context/module/global/call hooks; the scalar format-1 runtime rejects it.

import { writeFileSync } from "node:fs";
import { pathToFileURL } from "node:url";
import type { Call, Expr, Field, ModuleIR, Statement } from "./ir.ts";
import { readIR, readIRFile } from "./reader.ts";

export function requiresRandom(module: ModuleIR): boolean {
    const visit = (x: unknown): boolean => {
        if (!x || typeof x !== "object") return false;
        if (Array.isArray(x)) return x.some(visit);
        const record = x as Record<string, unknown>;
        return record.kind === "randomRange" || record.kind === "randomChoice" || Object.values(record).some(visit);
    };
    return visit(module.definitions);
}

export function emitIR(module: ModuleIR): string {
    let serial = 0;
    const site = (script: number, offset: number) => JSON.stringify({ source: module.source, script, offset });
    const ctx = (script: number, offset: number) => `qb.context(${site(script, offset)})`;
    const fields = (xs: Field[], script: number, offset: number, params = "p"): string =>
        `fields(${params}, [${xs
            .map((f) =>
                "spread" in f
                    ? "null"
                    : `() => [${f.name}, ${value(f.value, script, params)}${f.flattenStructure ? ", true" : ""}]`,
            )
            .join(", ")}], ${ctx(script, offset)})`;
    const value = (x: Expr, script: number, params = "p"): string => {
        const context = ctx(script, x.offset);
        switch (x.kind) {
            case "literal":
                return `v.copy(${JSON.stringify(x.value)}, "value", ${context})`;
            case "global":
                return `v.global(${x.name}, ${context})`;
            case "parameter":
                return `v.param(${params}, ${x.name}, ${JSON.stringify(x.mode)}, ${context})`;
            case "allParameters":
                return `({type:20, target:{type:10, members:${params}}})`;
            case "binary":
                return `v.binary(${x.op}, ${value(x.left, script, params)}, ${value(x.right, script, params)}, ${context})`;
            case "randomRange":
                return `randomRange(${x.a}, ${x.b}, ${x.second}, ${site(script, x.offset)})`;
            case "randomChoice":
                return `randomChoice(${x.opcode}, ${site(script, x.offset)}, ${JSON.stringify(x.weights)}, [${x.choices.map((choice) => `() => ${value(choice, script, params)}`).join(", ")}])`;
            case "structure":
                return `v.structure(${fields(x.fields, script, x.offset, params)}, ${context})`;
            case "array": {
                // The reader validates literal-array homogeneity, including
                // the original Integer -> Float promotion rule.
                const types = x.items.map((item) =>
                    item.kind === "literal"
                        ? item.value.type
                        : item.kind === "structure"
                          ? 10
                          : item.kind === "array"
                            ? 12
                            : 0,
                );
                const type = types.includes(2) && types.every((t) => t === 1 || t === 2) ? 2 : (types[0] ?? 0);
                return `v.array(${type}, [${x.items.map((item) => value(item, script, params)).join(", ")}], ${context})`;
            }
        }
    };
    const call = (x: Call, script: number) =>
        `(yield* qb.call(${x.name}, ${fields(x.fields, script, x.offset)}, p, ${site(script, x.offset)}))`;
    const statements = (
        xs: Statement[],
        script: number,
        loops = new Map<number, { label: string; args: string }>(),
    ): string =>
        xs
            .map((x) => {
                const context = ctx(script, x.offset),
                    mark = `qb.step(${site(script, x.offset)});\n`;
                switch (x.kind) {
                    case "call":
                        return `${mark}${call(x, script)};\n`;
                    case "assign":
                        return `${mark}{ const assigned = ${value(x.value, script)}; if (assigned !== undefined) v.append(p, [[${x.name}, assigned]], ${context}); }\n`;
                    case "return":
                        return `${mark}return root ? v.merge(p, ${fields(x.fields, script, x.offset)}, ${context}) : ${fields(x.fields, script, x.offset)};\n`;
                    case "if": {
                        const condition =
                            x.condition.kind === "call"
                                ? call(x.condition, script)
                                : `v.statementTruth(${value(x.condition, script)}, ${context})`;
                        return `${mark}if (${condition}) {\n${statements(x.yes, script, loops)}} else {\n${statements(x.no, script, loops)}}\n`;
                    }
                    case "loop": {
                        const label = `loop${serial++}`,
                            args = `${label}Args`,
                            nested = new Map(loops);
                        nested.set(x.offset, { label, args });
                        return `${mark}{ let read = false, count;\nconst ${args} = scope => ${fields(x.repeatFields, script, x.repeatOffset, "scope")};\n${label}: for (;;) {\nqb.step(${site(script, x.offset)});\n${statements(x.body, script, nested)}qb.step(${site(script, x.repeatOffset)});\nif (!read) { count = v.lookupTyped(${args}(p), 0, 1, ${ctx(script, x.repeatOffset)})?.bits; read = true; }\nif (count !== undefined) { count = (count - 1) >>> 0; if (count === 0) break; }\n}\n}\n`;
                    }
                    case "break": {
                        const target = loops.get(x.target);
                        if (!target)
                            throw Error(
                                `${module.source}: QB offset 0x${x.offset.toString(16)}: invalid lexical break target`,
                            );
                        return `${mark}${target.args}([]);\nbreak ${target.label};\n`;
                    }
                    case "switch": {
                        let code = `${mark}{ const selector = ${fields(x.fields, script, x.offset)}[0]?.[1]; let matched = false;\n`;
                        for (const group of x.cases) {
                            code += "if (!matched) { let groupMatch = false;\n";
                            for (const label of group.labels)
                                code += `qb.step(${site(script, label.offset)});\n{ const candidate = ${fields(label.fields, script, label.offset)}[0]?.[1]; if (!groupMatch && selector !== undefined && candidate !== undefined && v.binary(7, selector, candidate, ${ctx(script, label.offset)}).bits !== 0) groupMatch = true; }\n`;
                            code += `if (groupMatch) { matched = true;\n${statements(group.body, script, loops)}}\n}\n`;
                        }
                        if (x.fallback !== undefined)
                            code += `if (!matched) {\n${statements(x.fallback, script, loops)}}\n`;
                        return `${code}}\n`;
                    }
                }
                throw Error(`${module.source}: unsupported IR statement`);
            })
            .join("");
    const manifest = {
        format: 2,
        source: module.source,
        scripts: module.definitions.filter((d) => d.kind === "script").map((d) => ({ name: d.name, offset: d.offset })),
        requires: { values: 2, ...(requiresRandom(module) ? { random: 1 } : {}) },
    };
    let output = `// Generated from QB; do not edit.\n(() => {\nqb.module(${JSON.stringify(manifest)});\nconst v = globalThis.qbValues;\nif (v?.apiVersion !== 2) throw Error("QB values API 2 required");\nfunction fields(p, entries, ctx) {\nconst out = [];\nfor (const entry of entries) {\nif (entry === null) v.append(out, p, ctx);\nelse { const field = entry(); if (field[1] !== undefined) { if (field[2] && field[1].type === 10) v.append(out, field[1].members, ctx); else v.append(out, [[field[0], field[1]]], ctx); } }\n}\nreturn out;\n}\n`;
    if (requiresRandom(module))
        output +=
            "function randomRange(a,b,second,site) { const ctx=qb.context(site); return qb.random().range(a,b,second,ctx.charge); }\nfunction randomChoice(op,site,weights,choices) { const ctx=qb.context(site); const selected=qb.random().choose(op,site,weights,ctx.owner,ctx.charge); return choices[selected](); }\n";
    for (const def of module.definitions) {
        if (def.kind === "global")
            output += `qb.defineGlobal(${def.name}, ${value(def.value, 0, "[]")}, ${site(0, def.offset)});\n`;
        else
            output += `qb.define(${def.name}, function* (args, root) {\nlet p = v.merge(${fields(def.defaults, def.name, def.offset, "[]")}, args, ${ctx(def.name, def.offset)});\nqb.frame(p, root);\n${statements(def.body, def.name)}return root ? p : [];\n});\n`;
        if (output.length > 16 * 1024 * 1024) throw Error(`${module.source}: generated JS exceeds 16 MiB`);
    }
    output += "})();\n";
    if (output.length > 16 * 1024 * 1024) throw Error(`${module.source}: generated JS exceeds 16 MiB`);
    return output;
}

export const translateIR = (bytes: Uint8Array, source?: string): string => emitIR(readIR(bytes, source));
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    const [, , input, output] = process.argv;
    if (!input || !output || process.argv.length !== 4)
        throw Error("Usage: node tools/qbjs/emitter.ts INPUT.qb OUTPUT.js");
    writeFileSync(output, emitIR(readIRFile(input, output)));
}
