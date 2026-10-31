// NOT ORIGINAL - generates src/game/script/member_function_code.cpp: the code half of the script member-function tables
// (standalone step 6). The reference build's tables hold each record's name and component type as data; its static
// initialisers (in the C++ initialiser table run before WinMain) write each record's function - a 16-byte member
// function pointer: the code, then three zero words - and the terminator's {0, 0, 0, -1}. This decodes those
// initialisers (straight-line `mov reg, imm` / `mov [address], reg` code), checks that pattern, and writes each
// function as a GAME_CODE of the function our source REPLACEs there.
//   node tools/gen_member_tables.ts [--exe <reference build>] [--out <file>]

import * as fs from "node:fs";
import * as path from "node:path";
import { Image } from "./code_coverage.ts";
import { functionNamer } from "./gen_script_tables.ts";
import { REFERENCE_EXE } from "./pipeline/lib/decomp.ts";
import { globRecursive } from "./pipeline/lib/files.ts";
import { isMain, print, runMain } from "./pipeline/lib/py.ts";

// The C++ initialiser table (__xc_a .. __xc_z: _cinit's second _initterm, 0x006249D1).
export const CPP_INITIALISERS = { start: 0x0067c000, end: 0x0067c13c };

const hex = (n: number): string => `0x${n.toString(16).toUpperCase().padStart(8, "0")}`;

// The absolute stores of a straight-line initialiser, or null when it does anything else (calls, branches, stack
// stores...). Registers are tracked through `mov reg, imm`, `xor reg, reg` and `or reg, -1`; a pop forgets one.
export function decodeStores(img: Image, start: number): Map<number, number> | null {
    const regs: (number | undefined)[] = new Array(8).fill(undefined);
    const stores = new Map<number, number>();
    const store = (address: number, value: number | undefined): boolean => {
        if (value === undefined) return false;
        stores.set(address, value >>> 0);
        return true;
    };
    for (let at = start; at < start + 0x2000; ) {
        const op = img.byte(at)!;
        if (op >= 0x50 && op <= 0x57)
            at += 1; // push reg
        else if (op >= 0x58 && op <= 0x5f) {
            regs[op - 0x58] = undefined; // pop reg
            at += 1;
        } else if (op >= 0xb8 && op <= 0xbf) {
            regs[op - 0xb8] = img.dword(at + 1);
            at += 5;
        } else if (op === 0xa3) {
            if (!store(img.dword(at + 1), regs[0])) return null;
            at += 5;
        } else if (op === 0x89 && (img.byte(at + 1)! & 0xc7) === 0x05) {
            if (!store(img.dword(at + 2), regs[(img.byte(at + 1)! >> 3) & 7])) return null;
            at += 6;
        } else if (op === 0xc7 && img.byte(at + 1) === 0x05) {
            store(img.dword(at + 2), img.dword(at + 6));
            at += 10;
        } else if ((op === 0x33 || op === 0x31) && (img.byte(at + 1)! & 0xc0) === 0xc0) {
            const m = img.byte(at + 1)!;
            if (((m >> 3) & 7) !== (m & 7)) return null;
            regs[m & 7] = 0;
            at += 2;
        } else if (op === 0x83 && (img.byte(at + 1)! & 0xf8) === 0xc8 && img.byte(at + 2) === 0xff) {
            regs[img.byte(at + 1)! & 7] = 0xffffffff;
            at += 3;
        } else if (op === 0xc3) return stores;
        else return null;
    }
    return null;
}

interface Global {
    type: string;
    name: string;
    address: number;
    size: number;
    file: string;
    scope: string;
    anonymous: boolean;
}

// The GAME_GLOBALs of uint32_t arrays named *MemberFunctions, with the namespace each is defined in.
function memberTables(files: readonly string[]): Global[] {
    const found: Global[] = [];
    for (const file of files.filter((f) => f.endsWith(".cpp"))) {
        const lines = fs.readFileSync(file, "utf-8").split(/\r?\n/);
        const stack: string[] = [];
        for (const line of lines) {
            const open = /^\s*namespace\s*([\w:]*)\s*\{/.exec(line);
            if (open) stack.push(open[1]!);
            else if (/^\s*\}\s*\/\/\s*namespace\b/.test(line)) stack.pop();
            const m = /^\s*GAME_GLOBAL\((uint32_t\[(\d+)\]),\s*(\w+MemberFunctions),\s*0x([0-9A-Fa-f]{8})/.exec(line);
            if (!m) continue;
            found.push({
                type: m[1]!,
                name: m[3]!,
                address: parseInt(m[4]!, 16),
                size: 4 * Number(m[2]),
                file,
                scope: stack.join("::"),
                anonymous: stack.includes(""),
            });
        }
    }
    return found;
}

function main(): number {
    const args = process.argv.slice(2);
    const at = (flag: string): string | undefined => {
        const i = args.indexOf(flag);
        return i >= 0 ? args[i + 1] : undefined;
    };
    const exe = at("--exe") ?? REFERENCE_EXE;
    const out = at("--out") ?? "src/game/script/member_function_code.cpp";
    const img = new Image(fs.readFileSync(exe));
    const files = [".cpp", ".h"].flatMap((s) => globRecursive("src/game", s)).map((f) => f.replaceAll("\\", "/"));
    const tables = memberTables(files);
    const { functionOf, includes, undeclared } = functionNamer();

    const externs = new Map<string, string[]>();
    const fills: string[] = [];
    const calls: string[] = [];
    let total = 0;
    for (let slot = CPP_INITIALISERS.start; slot < CPP_INITIALISERS.end; slot += 4) {
        const init = img.dword(slot);
        if (!init) continue;
        const stores = decodeStores(img, init);
        if (!stores || stores.size === 0) continue;
        const first = Math.min(...stores.keys());
        const table = tables.find((t) => first >= t.address && first < t.address + t.size);
        if (!table) continue;
        if (table.anonymous) throw new Error(`${table.file}: ${table.name} is in an anonymous namespace`);
        // Every store inside the table; record by record: the code (none for the terminator), then 0, 0, 0 (-1 last).
        for (const a of stores.keys())
            if (a < table.address || a >= table.address + table.size)
                throw new Error(`${hex(init)}: a store at ${hex(a)} outside ${table.name}`);
        const codes: string[] = [];
        for (let record = 0; ; record++) {
            const r = table.address + 0x18 * record;
            if (r + 0x18 > table.address + table.size) throw new Error(`${hex(init)}: ${table.name} has no terminator`);
            const terminator = img.dword(r) === 0;
            const words = [8, 0xc, 0x10, 0x14].map((o) => stores.get(r + o));
            const expect = terminator ? [0, 0, 0, 0xffffffff] : [words[0], 0, 0, 0];
            if (words.some((w, i) => w !== expect[i]) || words[0] === undefined)
                throw new Error(
                    `${hex(init)}: record ${record} of ${table.name} isn't {code, 0, 0, 0} / {0, 0, 0, -1}`,
                );
            if (terminator) break;
            codes.push(`        GAME_CODE(${hex(words[0]!)}, ${functionOf(words[0]!, table.name)}),`);
        }
        if (stores.size !== 4 * (codes.length + 1))
            throw new Error(`${hex(init)}: ${stores.size} stores, ${codes.length} records`);
        total += codes.length;
        const qualified = table.scope ? `${table.scope}::${table.name}` : table.name;
        if (!externs.has(table.scope)) externs.set(table.scope, []);
        externs.get(table.scope)!.push(`GAME_GLOBAL_EXTERN(${table.type}, ${table.name});`);
        const fill = `Fill${table.name.replace(/^g_/, "").replace(/^./, (c) => c.toUpperCase())}`;
        fills.push(`// ${hex(init)}: ${qualified} (${hex(table.address)}), ${codes.length} functions.`);
        fills.push(`void ${fill}()`, "{");
        if (codes.length) {
            fills.push("    void* const code[] = {", ...codes, "    };");
            fills.push(`    FillMemberFunctionCode(${qualified}, code, ${codes.length});`);
        } else fills.push(`    FillMemberFunctionCode(${qualified}, nullptr, 0);`);
        fills.push("}", "");
        calls.push(`    ${fill}(); // ${hex(init)}`);
    }

    const externLines: string[] = [];
    for (const [scope, lines] of [...externs].sort((a, b) => a[0].localeCompare(b[0]))) {
        if (scope) externLines.push(`namespace ${scope} {`);
        externLines.push(...lines);
        if (scope) externLines.push(`} // namespace ${scope}`);
        externLines.push("");
    }
    const text = [
        "// GENERATED by `node tools/gen_member_tables.ts` - do not edit by hand.",
        `// The code half of the script member-function tables (${calls.length} tables, ${total} functions): what the original's`,
        "// static initialisers write into them before WinMain - each record's function (a member function pointer: the code,",
        "// three zero words) and the terminator's {0, 0, 0, -1}. The standalone build runs them at start-up in the original's",
        "// order (game/sys/startup_init.cpp); in the hooked build each GAME_CODE is the original's address, so the start-up",
        "// self-test compares them with the original initialisers word for word.",
        "",
        "#include <cstdint>",
        "",
        ...[...includes].sort().map((i) => `#include "${i}"`),
        '#include "game/script/names.h"',
        '#include "hook/game_code.h"',
        '#include "hook/game_global.h"',
        "",
        ...externLines,
        "namespace Script {",
        "",
        "namespace {",
        "",
        ...fills,
        "} // namespace",
        "",
        "void FillMemberFunctionTables()",
        "{",
        ...calls,
        "}",
        "",
        "} // namespace Script",
        "",
    ].join("\n");
    fs.mkdirSync(path.dirname(out), { recursive: true });
    fs.writeFileSync(out, text);
    print(`${calls.length} member-function tables, ${total} functions -> ${out} (${includes.size} headers)`);
    if (undeclared.length) {
        print(`${undeclared.length} functions not declared in any header:`);
        for (const u of undeclared) print(`  ${u}`);
    }
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
