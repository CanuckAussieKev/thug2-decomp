// NOT ORIGINAL - writes docs/PROGRESS.md: decompilation progress per subsystem of THUG2PM.exe.
//   node tools/progress_report.ts
// Inputs: data/subsystems.csv (the exe map), data/derived/functions.json (function inventory from
// tools/ghidra/export_functions.py) and the REPLACE(...) lines under src/game/ (what we have rewritten).

import * as fs from "node:fs";
import { globRecursive, readCsvDicts } from "./lib/files.ts";
import { formatFixed, isMain, print, pyIntHex, readTextPy, runMain, today, writeTextPy } from "./lib/py.ts";

const BAR_WIDTH = 20;

interface Fn {
    addr: number;
    size: number;
}

function bar(fraction: number): string {
    // Round down, so a system only shows a full bar / 100% when it really is complete.
    const filled = Math.trunc(fraction * BAR_WIDTH);
    return "█".repeat(filled) + "░".repeat(BAR_WIDTH - filled);
}

function percent(fraction: number): string {
    return fraction >= 1.0 ? "100%" : `${Math.trunc(fraction * 100)}%`;
}

// f"{n:,}" of an int: thousands separated by ","
export function commas(n: number): string {
    const [whole, frac] = String(n).split(".");
    const sign = whole!.startsWith("-") ? "-" : "";
    const digits = whole!.replace("-", "").replace(/\B(?=(\d{3})+(?!\d))/g, ",");
    return sign + digits + (frac !== undefined ? `.${frac}` : "");
}

// f"{x:,.<digits>f}"
export function commasFixed(x: number, digits: number): string {
    const [whole, frac] = formatFixed(x, digits).split(".");
    const sign = whole!.startsWith("-") ? "-" : "";
    const grouped = whole!.replace("-", "").replace(/\B(?=(\d{3})+(?!\d))/g, ",");
    return sign + grouped + (frac !== undefined ? `.${frac}` : "");
}

// f"{x:.<digits>%}": x * 100 (as a float) in fixed notation, then "%"
const pct = (x: number, digits: number): string => `${formatFixed(x * 100, digits)}%`;

const kb = (n: number): string => `${commasFixed(n / 1024, 1)} KB`;

interface Entry {
    row: Record<string, string>;
    funcs: number;
    doneFuncs: number;
    bytes: number;
    doneBytes: number;
}

function main(): number {
    const functions = new Map<number, Fn>();
    for (const f of JSON.parse(fs.readFileSync("data/derived/functions.json", "utf-8")) as Fn[])
        functions.set(f.addr, f);
    const rewritten = new Set<number>();
    for (const p of globRecursive("src/game", ".cpp")) {
        for (const m of fs
            .readFileSync(p, "utf-8")
            .matchAll(/REPLACE(?:_SHADOW|_UNCHECKED|_SOURCE_ONLY)?\(0x([0-9A-Fa-f]{8})/g)) {
            rewritten.add(Number.parseInt(m[1]!, 16));
        }
    }

    const rows: Entry[] = [];
    const libraries: [Record<string, string>, number, number][] = [];
    const total = { funcs: 0, doneFuncs: 0, bytes: 0, doneBytes: 0 };
    for (const r of readCsvDicts(readTextPy("data/subsystems.csv"))) {
        const lo = pyIntHex(r.start!);
        const hi = pyIntHex(r.end!);
        const funcs = [...functions.values()].filter((f) => lo <= f.addr && f.addr < hi);
        const size = funcs.reduce((n, f) => n + f.size, 0);
        if (r.kind !== "game") {
            libraries.push([r, funcs.length, size]);
            continue;
        }
        const done = funcs.filter((f) => rewritten.has(f.addr));
        const entry: Entry = {
            row: r,
            funcs: funcs.length,
            doneFuncs: done.length,
            bytes: size,
            doneBytes: done.reduce((n, f) => n + f.size, 0),
        };
        rows.push(entry);
        total.funcs += entry.funcs;
        total.doneFuncs += entry.doneFuncs;
        total.bytes += entry.bytes;
        total.doneBytes += entry.doneBytes;
    }

    const out: string[] = [];
    out.push("# Decompilation progress\n");
    out.push(`Generated ${today()} by \`tools/progress_report.py\` - regenerate after each batch.\n`);
    const overall = total.doneBytes / total.bytes;
    out.push(
        `**Game code: ${bar(overall)} ${pct(overall, 1)}** of the code rewritten ` +
            `(${commas(total.doneFuncs)} of ${commas(total.funcs)} functions, ` +
            `${kb(total.doneBytes)} of ${kb(total.bytes)}).\n`,
    );
    out.push(
        "Progress is by code size: function counts make a 10-byte helper look like as much work as a 2 KB " +
            "routine. Subsystem boundaries come from the exe map (`docs/subsystems.md`) and are approximate " +
            "(give or take a few functions); totals are exact.\n",
    );
    out.push("| Subsystem | Range | Progress (code size) | Functions | Code |");
    out.push("|---|---|---|---:|---:|");
    for (const e of rows) {
        const r = e.row;
        const fraction = e.bytes ? e.doneBytes / e.bytes : 0.0;
        out.push(
            `| ${r.subsystem} | \`${r.start}\`-\`${r.end}\` | ${bar(fraction)} ${percent(fraction)} | ` +
                `${e.doneFuncs} / ${e.funcs} | ${kb(e.doneBytes)} / ${kb(e.bytes)} |`,
        );
    }
    out.push("");
    out.push("## Not decompiled (libraries)\n");
    out.push("Linked as libraries in the final build rather than rewritten:\n");
    for (const [r, n, size] of libraries)
        out.push(`- ${r.subsystem} (\`${r.start}\`-\`${r.end}\`): ${n} functions, ${kb(size)}`);
    out.push(
        "- GameSpy SDK, MSVC C runtime, STL helpers and compiler-generated code after `0x5F8130` " +
            "(see `docs/library_ranges.md`)",
    );
    out.push("");
    out.push("## PartyMod\n");
    out.push(
        "PartyMod's changes (MIT, (c) 2024 PARTYMANX, see `THIRD_PARTY_NOTICES.md`) are part of the game's source: the SDL window and " +
            "input, widescreen, the settings file and the rest, by subsystem. Still written into the exe at start-up: " +
            "the GameSpy SDK's host names (library code, `src/game/net/online_hosts.cpp`), the online identity when it's set (`src/game/net/online_identity.cpp`) and, with PS2 controls, " +
            "the CFunction table's IsPS2 entry (`src/game/sys/platform_cfuncs.cpp`).",
    );

    // (text mode, as the Python wrote it: "\r\n" line ends)
    writeTextPy("docs/PROGRESS.md", `${out.join("\n")}\n`);
    print(
        `docs/PROGRESS.md: ${total.doneFuncs}/${total.funcs} functions, ` +
            `${total.doneBytes}/${total.bytes} bytes (${pct(overall, 2)})`,
    );
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
