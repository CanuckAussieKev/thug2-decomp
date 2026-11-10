// node tools/qbjs/check_types_against_runs.ts <types.json> <qb.txt>: the type checker's proof tested against the
// script check's runs (docs/script-check.md). Each run ends with the script's parameters as they were (qb.txt
// "end ... {member=value ...}"); each member's kind must be one the checker proved that script's reads of it can see
// (types.json readKinds). A kind outside the proven set is a bug in the checker or a model.
import { createReadStream, readFileSync } from "node:fs";
import { createInterface } from "node:readline";
import { checksum } from "../qb_checksum.ts";

// The kind of one value as the trace writes it (script_check.cpp text_of_value).
export function traceKind(v: string): string {
    if (v.startsWith('"')) return "string";
    if (v.startsWith('l"') || v.startsWith('L"')) return "localstring";
    if (v.startsWith("#")) return "name";
    if (v.startsWith("{")) return "struct";
    if (v.startsWith("[")) return "array";
    if (v.startsWith("(")) return v.split(",").length >= 3 ? "vector" : "pair";
    if (/^f[0-9a-f]+$/i.test(v)) return "float";
    if (/^-?\d+$/.test(v)) return "int";
    if (v.startsWith("script")) return "script";
    return `?${v.slice(0, 12)}`;
}

// The top-level members of a "{k=v k=v}" text: [member hex, value text].
export function traceMembers(text: string): [string, string][] {
    const body = text.trim().replace(/^\{/, "").replace(/\}$/, "");
    const out: [string, string][] = [];
    let i = 0;
    while (i < body.length) {
        while (body[i] === " ") ++i;
        if (i >= body.length) break;
        const eq = body.indexOf("=", i);
        if (eq < 0) break;
        const key = body.slice(i, eq);
        let j = eq + 1;
        let depth = 0;
        let quoted = false;
        for (; j < body.length; ++j) {
            const ch = body[j]!;
            if (quoted) {
                if (ch === "\\") ++j;
                else if (ch === '"') quoted = false;
                continue;
            }
            if (ch === '"') quoted = true;
            else if (ch === "{" || ch === "[" || ch === "(") ++depth;
            else if (ch === "}" || ch === "]" || ch === ")") --depth;
            else if (ch === " " && depth === 0) break;
        }
        out.push([key, body.slice(eq + 1, j)]);
        i = j + 1;
    }
    return out;
}

if (process.argv[1]?.endsWith("check_types_against_runs.ts")) {
    const [typesPath, tracePath] = process.argv.slice(2);
    const report = JSON.parse(readFileSync(typesPath!, "utf8")) as { readKinds: Record<string, string> };
    // "<script hex>|<member hex>" -> kinds
    const proven = new Map<string, Set<string>>();
    const hex = (name: string) =>
        (/^_0x([0-9a-f]{8})$/i.exec(name)?.[1] ?? checksum(name).toString(16).padStart(8, "0")).toLowerCase();
    for (const [k, kinds] of Object.entries(report.readKinds)) {
        const [script, member] = k.split("|") as [string, string];
        proven.set(`${hex(script)}|${hex(member)}`, new Set(kinds.split("|").map((x) => x.replace(/<.*$/, ""))));
    }
    let script = "";
    let runs = 0;
    let checked = 0;
    const bad = new Map<string, number>();
    const lines = createInterface({ input: createReadStream(tracePath!), crlfDelay: Infinity });
    for await (const line of lines) {
        if (line.startsWith("run ")) script = line.split(" ")[1]!.toLowerCase();
        else if (line.startsWith("end ") && line.includes("{")) {
            ++runs;
            for (const [member, value] of traceMembers(line.slice(line.indexOf("{")))) {
                const kinds = proven.get(`${script}|${member.toLowerCase()}`);
                if (!kinds) continue; // (no read of it in that script)
                ++checked;
                const kind = traceKind(value);
                if (!kinds.has(kind)) {
                    const k = `${script}|${member} ${kind} not in ${[...kinds].join("|")}`;
                    bad.set(k, (bad.get(k) ?? 0) + 1);
                }
            }
        }
    }
    console.log(`${runs} runs with parameters, ${checked} members checked against the proof, ${bad.size} outside it`);
    for (const [k, n] of [...bad].sort((a, b) => b[1] - a[1]).slice(0, 40)) console.log(`  ${n} ${k}`);
    process.exitCode = bad.size ? 1 : 0;
}
