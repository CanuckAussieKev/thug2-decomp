// NOT ORIGINAL - standalone step 4: the exe addresses the source still uses as literals (outside comments, REPLACE
// lines, GameFunction / GameVariable - standalone_report counts those), by what lives there in the reference build:
//   code-ours      the start of a function we have (REPLACE line): a function pointer - use ours
//   code-library   a library function (data/library_functions.csv, D3DX9 / GameSpy / CRT ranges)
//   code-other     inside code, not a function start we know (a return address, a patch site...)
//   vtable         .rdata words that point into code (a class's virtual table, a member / command table)
//   string         .rdata / .data text ending in a zero byte
//   rdata          other read-only data (float / int constants, tables)
//   data / bss     writable globals (should be GAME_GLOBALs - step 3)
//   import         the import table
//   other          outside the image
//   node tools/raw_addresses.ts [--exe <reference build>] [--out <csv>] [--list <kind>]
// The csv (default out/raw_addresses.csv): address, kind, uses, files, detail.

import * as fs from "node:fs";
import { Image } from "./code_coverage.ts";
import { REFERENCE_EXE } from "./pipeline/lib/decomp.ts";
import { globRecursive, readCsvDicts } from "./pipeline/lib/files.ts";
import { isMain, print, runMain } from "./pipeline/lib/py.ts";

const hex = (n: number): string => `0x${n.toString(16).toUpperCase().padStart(8, "0")}`;

// The source with comments and string literals blanked (so addresses mentioned in them don't count).
export function codeOnly(text: string): string {
    return text
        .replace(/\/\*[\s\S]*?\*\//g, (m) => m.replace(/[^\n]/g, " "))
        .replace(/\/\/[^\n]*/g, (m) => " ".repeat(m.length))
        .replace(/"(?:[^"\\\n]|\\.)*"/g, (m) => " ".repeat(m.length));
}

function main(): number {
    const args = process.argv.slice(2);
    const at = (flag: string): string | undefined => {
        const i = args.indexOf(flag);
        return i >= 0 ? args[i + 1] : undefined;
    };
    const img = new Image(fs.readFileSync(at("--exe") ?? REFERENCE_EXE));
    const out = at("--out") ?? "out/raw_addresses.csv";
    const listKind = at("--list");

    const replaced = new Set<number>();
    const uses = new Map<number, Map<string, number>>();
    for (const file of [".cpp", ".h"].flatMap((s) => globRecursive("src/game", s))) {
        const raw = fs.readFileSync(file, "utf-8");
        for (const m of raw.matchAll(/REPLACE\w*\(0x([0-9A-Fa-f]{8})/g)) replaced.add(parseInt(m[1]!, 16));
        const text = codeOnly(raw)
            .split("\n")
            .filter((l) => !/^\s*REPLACE\w*\(/.test(l))
            .join("\n")
            .replace(/Game(Function|Variable)<[^;]*?>\(\s*0x[0-9A-Fa-f]{6,8}\s*\)/g, " ")
            // (the address in a GAME_GLOBAL / GAME_TEXT / GAME_CODE / GAME_VTABLE / GAME_LIBRARY_SLOT is the map back to
            // the exe, like a REPLACE line's; a GAME_VTABLE's slots are our functions)
            .replace(/GAME_(GLOBAL\w*|VTABLE)\([^;]*?0x[0-9A-Fa-f]{6,8}/g, " ")
            .replace(/GAME_(TEXT|CODE|LIBRARY_SLOT)\(\s*0x[0-9A-Fa-f]{6,8}/g, " ");
        const f = file.replaceAll("\\", "/").replace(/^src\/game\//, "");
        for (const m of text.matchAll(/\b0x00([4-7][0-9A-Fa-f]{5})\b/g)) {
            const a = parseInt(m[1]!, 16);
            if (a < 0x401000 || a >= 0x7e0000) continue;
            if (!uses.has(a)) uses.set(a, new Map());
            uses.get(a)!.set(f, (uses.get(a)!.get(f) ?? 0) + 1);
        }
    }
    const library = new Set(
        readCsvDicts(fs.readFileSync("data/library_functions.csv", "utf-8")).map((r) => parseInt(r["address"]!, 16)),
    );
    const inCode = (v: number): boolean => v >= 0x401000 && v < 0x645000;
    const isLibraryCode = (a: number): boolean =>
        library.has(a) || (a >= 0x5beaae && a < 0x5f2e00) || (a >= 0x5f6640 && a < 0x645000);
    const looksLikeString = (a: number): boolean => {
        let n = 0;
        for (let x = a; x < a + 256; x++) {
            const b = img.byte(x);
            if (b === undefined) return false;
            if (b === 0) return n >= 2;
            if (b < 0x09 || (b > 0x0d && b < 0x20) || b > 0x7e) return false;
            n++;
        }
        return false;
    };
    const classify = (a: number): [string, string] => {
        if (a < 0x645000) {
            if (replaced.has(a)) return ["code-ours", ""];
            if (isLibraryCode(a)) return ["code-library", ""];
            return ["code-other", ""];
        }
        if (a < 0x645400) return ["import", ""];
        // (pointers first: little-endian code addresses can read as a short string ending in a zero byte)
        let code = 0;
        while (a < 0x6f0000 && code < 64 && inCode(img.dword(a + 4 * code))) code++;
        if (code >= 1 && a < 0x67c000) return ["vtable", `${code} code pointer${code > 1 ? "s" : ""}`];
        if (a < 0x6f0000 && looksLikeString(a)) {
            let s = "";
            for (let x = a; img.byte(x) && s.length < 40; x++) s += String.fromCharCode(img.byte(x)!);
            return ["string", JSON.stringify(s)];
        }
        if (a < 0x67c000) return ["rdata", `0x${img.dword(a).toString(16)}`];
        if (a < 0x6f0000) return ["data", ""];
        return ["bss", ""];
    };

    const rows = ["address,kind,uses,files,detail"];
    const totals = new Map<string, [number, number]>();
    for (const a of [...uses.keys()].sort((x, y) => x - y)) {
        const [kind, detail] = classify(a);
        const files = uses.get(a)!;
        const n = [...files.values()].reduce((s, v) => s + v, 0);
        const t = totals.get(kind) ?? [0, 0];
        totals.set(kind, [t[0] + 1, t[1] + n]);
        rows.push([hex(a), kind, n, [...files.keys()].join(" "), detail.replaceAll(",", ";")].join(","));
        if (listKind === kind) print(`${hex(a)} ${n} ${[...files.keys()].join(" ")} ${detail}`);
    }
    fs.mkdirSync("out", { recursive: true });
    fs.writeFileSync(out, `${rows.join("\n")}\n`);
    print(`${uses.size} exe addresses used as literals in src/game:`);
    for (const [k, [addresses, n]] of [...totals].sort((x, y) => y[1][1] - x[1][1]))
        print(`  ${k.padEnd(13)} ${String(addresses).padStart(5)} addresses, ${String(n).padStart(5)} uses`);
    print(`-> ${out}`);
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
