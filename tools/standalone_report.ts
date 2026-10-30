// NOT ORIGINAL - what stands between the source and a game built without the original exe: builds the
// thug2_standalone target (THUG2_STANDALONE: GameFunction / GameVariable declared but never defined, REPLACE lines
// empty, no hook DLL) and sorts the linker's unresolved symbols, plus the raw exe addresses the source still casts
// (vtables, strings, tables - the linker can't see those).
//   node tools/standalone_report.ts [--no-build] [--out <file>]
// --no-build reads out/standalone_build.txt from an earlier run. The full list goes to --out (default
// out/standalone_report.txt); the summary is printed.

import { execFileSync } from "node:child_process";
import * as fs from "node:fs";
import { globRecursive } from "./pipeline/lib/files.ts";
import { isMain, print, runMain } from "./pipeline/lib/py.ts";

const BUILD_LOG = "out/standalone_build.txt";
const BUILD_SCRIPT = String.raw`tools\build_standalone.cmd`;

export interface Unresolved {
    symbol: string; // undecorated, as the linker prints it
    from: string; // the function referencing it
    object: string; // its .obj
}

// `foo.cpp.obj : error LNK2019: unresolved external symbol "..." (?...) referenced in function "..." (?...)`
// (LNK2001 has no function).
export function parseLinkErrors(log: string): Unresolved[] {
    const found: Unresolved[] = [];
    for (const line of log.split(/\r?\n/)) {
        const m =
            /^(\S+?\.obj) : error LNK20(?:19|01): unresolved external symbol (.*?)(?: referenced in function (.*))?$/.exec(
                line.trim(),
            );
        if (!m) continue;
        const undecorated = (s: string): string => {
            const q = /^"(.*)" \(\S+\)$/.exec(s.trim());
            return q ? q[1]! : s.trim();
        };
        found.push({
            object: m[1]!.replace(/^.*[\\/]/, "").replace(/\.obj$/, ""),
            symbol: undecorated(m[2]!),
            from: m[3] ? undecorated(m[3]) : "",
        });
    }
    return found;
}

export function category(symbol: string): string {
    if (symbol.includes("GameFunction<")) return "GameFunction";
    if (symbol.includes("GameVariable<")) return "GameVariable";
    // (src/hook's: the log, the ini, the crash handler - to move into the game's source)
    if (/\b(Log\w*|SetLog\w*|Config\w*|SetCrashHook|GameFolderPath|_thug2_decomp_before_exit)\b/.test(symbol))
        return "hook: log / ini / crash";
    if (
        /\b(Original\w*|RegisterReplacement|WriteGameCode|RestoreOriginal|ReplacementFunction|RunStageOnce|SelfTest\w*|Shadow\w*|\w*TestFault\w*)\b/.test(
            symbol,
        )
    )
        return "hook: replacements / self-tests";
    if (/__imp_|Direct3D|D3DX|Bink|SDL_|WSA|socket|DirectSound/.test(symbol)) return "imports / libraries";
    return "other";
}

// The addresses GameFunction / GameVariable still name in the source (the linker only sees their types).
export function addressUses(text: string, which: "GameFunction" | "GameVariable"): number[] {
    const re = new RegExp(`${which}<[^;]*?>\\(\\s*0x([0-9A-Fa-f]{6,8})`, "g");
    return [...text.matchAll(re)].map((m) => parseInt(m[1]!, 16));
}
// Where an address is: our own game code, a library (docs/library_ranges.md), or the exe's data.
export function region(a: number): string {
    if ((a >= 0x401000 && a < 0x5beaae) || (a >= 0x5f2e00 && a < 0x5f6640)) return "game code (rewritten: call ours)";
    if (a < 0x5f2e00) return "D3DX9";
    if (a < 0x622e80) return "GameSpy";
    if (a < 0x645000) return "C runtime / STL";
    if (a < 0x67c000) return ".rdata (constants)";
    if (a < 0x6f0000) return ".data (initialised)";
    return ".bss (zeroed)";
}

// Raw addresses inside the exe's image that the source still turns into pointers (code 0x401000-, data to 0x7E0000).
export function rawAddressCasts(text: string): number {
    let n = 0;
    for (const m of text.matchAll(/reinterpret_cast<[^;]*?>\(\s*0x00([4-7][0-9A-Fa-f]{5})\b/g)) {
        const a = parseInt(m[1]!, 16);
        if (a >= 0x401000 && a < 0x7e0000) n++;
    }
    return n;
}

function main(): number {
    const args = process.argv.slice(2);
    const outAt = args.indexOf("--out");
    const out = outAt >= 0 ? args[outAt + 1]! : "out/standalone_report.txt";
    if (!args.includes("--no-build")) {
        print("building thug2_standalone (expected to fail at the link)...");
        let log = "";
        try {
            log = execFileSync("cmd", ["/c", BUILD_SCRIPT], { encoding: "utf-8", maxBuffer: 1 << 30 });
        } catch (e) {
            log = String((e as { stdout?: string }).stdout ?? "");
        }
        fs.writeFileSync(BUILD_LOG, log);
    }
    const log = fs.readFileSync(BUILD_LOG, "utf-8");
    const compileErrors = log.split(/\r?\n/).filter((l) => /: error C\d+/.test(l));
    if (compileErrors.length) {
        print(`${compileErrors.length} compile errors - fix those first:`);
        for (const l of compileErrors.slice(0, 20)) print(`  ${l.trim()}`);
        return 1;
    }
    const unresolved = parseLinkErrors(log);
    if (!unresolved.length && !/thug2_standalone\.exe/.test(log)) {
        print(`the build didn't get to the link - see ${BUILD_LOG}`);
        return 1;
    }
    const bySymbol = new Map<string, Unresolved[]>();
    for (const u of unresolved) {
        if (!bySymbol.has(u.symbol)) bySymbol.set(u.symbol, []);
        bySymbol.get(u.symbol)!.push(u);
    }
    const groups = new Map<string, { symbols: number; references: number; functions: Set<string> }>();
    for (const [symbol, refs] of bySymbol) {
        const c = category(symbol);
        if (!groups.has(c)) groups.set(c, { symbols: 0, references: 0, functions: new Set() });
        const g = groups.get(c)!;
        g.symbols++;
        g.references += refs.length;
        for (const r of refs) g.functions.add(r.from || r.object);
    }
    let casts = 0;
    const uses = { GameFunction: new Map<number, number>(), GameVariable: new Map<number, number>() };
    for (const file of [".cpp", ".h"].flatMap((s) => globRecursive("src/game", s))) {
        const text = fs.readFileSync(file, "utf-8");
        casts += rawAddressCasts(text);
        for (const which of ["GameFunction", "GameVariable"] as const)
            for (const a of addressUses(text, which)) uses[which].set(a, (uses[which].get(a) ?? 0) + 1);
    }

    print(`unresolved symbols: ${bySymbol.size} (${unresolved.length} references)`);
    for (const [c, g] of [...groups].sort((a, b) => b[1].references - a[1].references))
        print(
            `  ${c.padEnd(32)} ${String(g.symbols).padStart(5)} symbols, ${String(g.references).padStart(5)} references, ` +
                `${g.functions.size} functions`,
        );
    for (const which of ["GameFunction", "GameVariable"] as const) {
        const m = uses[which];
        print(`${which} in the source: ${[...m.values()].reduce((a, b) => a + b, 0)} uses of ${m.size} addresses`);
        const byRegion = new Map<string, [number, number]>();
        for (const [a, n] of m) {
            const r = byRegion.get(region(a)) ?? [0, 0];
            byRegion.set(region(a), [r[0] + 1, r[1] + n]);
        }
        for (const [r, [addresses, n]] of [...byRegion].sort((x, y) => y[1][1] - x[1][1]))
            print(`  ${r.padEnd(34)} ${String(addresses).padStart(5)} addresses, ${String(n).padStart(5)} uses`);
    }
    print(`raw exe addresses cast in src/game: ${casts}`);

    const lines: string[] = [];
    for (const [symbol, refs] of [...bySymbol].sort((a, b) => category(a[0]).localeCompare(category(b[0])))) {
        lines.push(`[${category(symbol)}] ${symbol}`);
        for (const r of refs) lines.push(`    ${r.object}: ${r.from}`);
    }
    fs.writeFileSync(out, `${lines.join("\n")}\n`);
    print(`full list: ${out}`);
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
