// NOT ORIGINAL - generates src/game/script/script_tables.cpp: the script CFunction table (name, function; in the
// reference build's .data) and the member-function name table as source, for standalone step 4e. Each name is a
// GAME_TEXT, each function a GAME_CODE of the function our source REPLACEs at that address (hook/game_code.h);
// thug2_check proves the two the same as the reference build's.
//   node tools/gen_script_tables.ts [--exe <reference build>] [--out <file>]

import * as fs from "node:fs";
import * as path from "node:path";
import { Image } from "./code_coverage.ts";
import { registrations } from "./game_function_map.ts";
import { REFERENCE_EXE } from "./pipeline/lib/decomp.ts";
import { globRecursive } from "./pipeline/lib/files.ts";
import { isMain, print, runMain } from "./pipeline/lib/py.ts";

const CFUNCTION_TABLE = 0x0067f3a0;
const CFUNCTION_COUNT = 1254;
const MEMBER_TABLE = 0x00681ad0;
const MEMBER_COUNT = 250;

const hex = (n: number): string => `0x${n.toString(16).toUpperCase().padStart(8, "0")}`;

function cString(img: Image, address: number): string {
    const bytes: number[] = [];
    for (let a = address; ; a++) {
        const b = img.byte(a);
        if (b === undefined) throw new RangeError(`unreadable string at ${hex(address)}`);
        if (b === 0) break;
        bytes.push(b);
    }
    // (script names are plain ASCII)
    let out = "";
    for (const b of bytes) {
        if (b === 0x22 || b === 0x5c) out += `\\${String.fromCharCode(b)}`;
        else if (b >= 0x20 && b < 0x7f && b !== 0x3f)
            out += String.fromCharCode(b); // (? : no trigraphs)
        else out += `\\x${b.toString(16).padStart(2, "0")}""`;
    }
    return out;
}

// The namespaces open where a file's REPLACE line for `address` stands (a REPLACE line inside `namespace X {` names an
// unqualified function; anonymous namespaces don't count).
function enclosingNamespace(file: string, address: number): string {
    const stack: string[] = [];
    const lines = fs.readFileSync(file, "utf-8").split(/\r?\n/);
    for (const line of lines) {
        if (line.includes("REPLACE") && line.includes(hex(address))) break;
        const open = /^\s*namespace\s*([\w:]*)\s*\{/.exec(line);
        if (open) stack.push(open[1]!);
        else if (/^\s*\}\s*\/\/\s*namespace\b/.test(line)) stack.pop();
    }
    return stack.filter((s) => s !== "").join("::");
}

function main(): number {
    const args = process.argv.slice(2);
    const at = (flag: string): string | undefined => {
        const i = args.indexOf(flag);
        return i >= 0 ? args[i + 1] : undefined;
    };
    const exe = at("--exe") ?? REFERENCE_EXE;
    const out = at("--out") ?? "src/game/script/script_tables.cpp";
    const img = new Image(fs.readFileSync(exe));
    const files = [".cpp", ".h"].flatMap((s) => globRecursive("src/game", s)).map((f) => f.replaceAll("\\", "/"));
    const regs = registrations(files);
    const headers = files.filter((f) => f.endsWith(".h"));
    const headerText = new Map(headers.map((h) => [h, fs.readFileSync(h, "utf-8")]));

    const includes = new Set<string>();
    const undeclared: string[] = [];
    const functionOf = (code: number, name: string): string => {
        const r = regs.get(code);
        if (!r) throw new Error(`${hex(code)} (${name}) has no REPLACE line`);
        const pick = r.find((x) => x.kind === "REPLACE") ?? r.find((x) => x.kind !== "REPLACE_SOURCE_ONLY") ?? r[0]!;
        // (the function's own name, to find the header that declares it: the last identifier of the qualified one)
        const expr = pick.name;
        const inner = /^static_cast<.*>\(\s*&?\s*([\w:]+)\s*\)$/.exec(expr)?.[1] ?? expr;
        const base = /([A-Za-z_]\w*)$/.exec(inner)?.[1];
        const re = base ? new RegExp(`\\b${base}\\s*\\(`) : undefined;
        let found = false;
        // (the pick's own header, or the first one declaring it)
        const own = pick.file.replace(/\.cpp$/, ".h");
        for (const h of [own, ...headers]) {
            const t = headerText.get(h);
            if (t && re?.test(t)) {
                includes.add(h.replace(/^src\//, ""));
                found = true;
                break;
            }
        }
        if (!found) undeclared.push(`${hex(code)} ${expr} (${pick.file})`);
        const scope = enclosingNamespace(pick.file, code);
        const qualified = (n: string): string => (scope && !n.includes("::") ? `${scope}::${n}` : n);
        if (expr.startsWith("static_cast")) return expr.replace(inner, qualified(inner));
        return `&${qualified(expr)}`;
    };

    const lines: string[] = [];
    for (let i = 0; i < CFUNCTION_COUNT; i++) {
        const nameAt = img.dword(CFUNCTION_TABLE + 8 * i);
        const code = img.dword(CFUNCTION_TABLE + 8 * i + 4);
        const name = cString(img, nameAt);
        lines.push(`    {GAME_TEXT(${hex(nameAt)}, "${name}"), GAME_CODE(${hex(code)}, ${functionOf(code, name)})},`);
    }
    const members: string[] = [];
    for (let i = 0; i < MEMBER_COUNT; i++) {
        const nameAt = img.dword(MEMBER_TABLE + 4 * i);
        members.push(`    GAME_TEXT(${hex(nameAt)}, "${cString(img, nameAt)}"),`);
    }
    // (no trailing commas inside GAME_GLOBAL's variadic: it is fine, but the last element is kept clean)
    const strip = (l: string[]): string[] => l.map((s, i) => (i === l.length - 1 ? s.replace(/,$/, "") : s));

    const text = [
        "// GENERATED by `node tools/gen_script_tables.ts` - do not edit by hand.",
        "// The script CFunction table (name, function: 0x0067F3A0, 1254 entries) and the member-function name table",
        "// (0x00681AD0, 250 names) as source. thug2_check proves each entry the same as the original's.",
        "",
        '#include "game/script/script_tables.h"',
        "",
        ...[...includes].sort().map((i) => `#include "${i}"`),
        '#include "hook/game_code.h"',
        '#include "hook/game_global.h"',
        "",
        "namespace Script {",
        "",
        `GAME_GLOBAL(CFunctionEntry[${CFUNCTION_COUNT}], g_cfunctionTable, ${hex(CFUNCTION_TABLE)},`,
        ...strip(lines),
        ");",
        "",
        `GAME_GLOBAL(const char*[${MEMBER_COUNT}], g_memberFunctionNames, ${hex(MEMBER_TABLE)},`,
        ...strip(members),
        ");",
        "",
        "} // namespace Script",
        "",
    ].join("\n");
    fs.mkdirSync(path.dirname(out), { recursive: true });
    fs.writeFileSync(out, text);
    print(`${CFUNCTION_COUNT} CFunctions, ${MEMBER_COUNT} member names -> ${out} (${includes.size} headers)`);
    if (undeclared.length) {
        print(`${undeclared.length} functions not declared in any header:`);
        for (const u of undeclared) print(`  ${u}`);
    }
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
