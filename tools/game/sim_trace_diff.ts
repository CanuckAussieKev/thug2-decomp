// NOT ORIGINAL - two simulation traces compared (sim_check.ts --trace: a playback's trace.txt, src/devtools/demo.cpp
// trace_start - every script command with its arguments, every random draw with the script running then and its
// callers, each check point's moving objects): the first entry where they part, with the entries around it.
//
//   node tools/game/sim_trace_diff.ts <trace A> <trace B> [--context 8] [--skip <regex>]
//
// The callers' places (" at <offsets>") are left out of the comparison - two builds' differ - and shown; --skip leaves
// out the entries it matches (tested on the whole line, callers included: e.g. a script launched by the drawing code -
// "d4f107cb@" - or the music's own draws). Typical use: the same demo in two modes (QB / TypeScript scripts), or before
// and after a change; `tools/symbolize.ts --pdb-dir <build> <offsets>` names the callers.
import * as fs from "node:fs";
import { exitWith, isMain, parseCli, print, runMain } from "thug2-level-converter-core/lib/cli.ts";

const USAGE = "usage: node tools/game/sim_trace_diff.ts <trace A> <trace B> [--context 8] [--skip <regex>]";

// An entry as compared: without its callers.
export const comparable = (line: string): string => line.replace(/ at [0-9a-f ]+$/, "");

export interface TraceDifference {
    a: number; // index of the first differing entry in A's entries (A's length: none there)
    b: number;
    same: boolean; // every entry equal, the same count
}

// The traces' entries (comment and blank lines, and those `skip` matches, left out).
export function entries(text: string, skip?: RegExp): string[] {
    return text.split(/\r?\n/).filter((l) => l.trim() !== "" && !l.startsWith("#") && !skip?.test(l));
}

export function firstDifference(a: string[], b: string[]): TraceDifference {
    let i = 0;
    while (i < a.length && i < b.length && comparable(a[i]!) === comparable(b[i]!)) ++i;
    return { a: i, b: i, same: i === a.length && i === b.length };
}

async function main(): Promise<number> {
    const { values, positionals } = parseCli(USAGE, {
        allowPositionals: true,
        options: { context: { type: "string", default: "8" }, skip: { type: "string" } },
    });
    if (positionals.length !== 2) exitWith(USAGE);
    const skip = values.skip ? new RegExp(values.skip) : undefined;
    const [a, b] = positionals.map((f) => entries(fs.readFileSync(f!, "utf8"), skip)) as [string[], string[]];
    const d = firstDifference(a, b);
    if (d.same) {
        print(`the same: ${a.length} entries`);
        return 0;
    }
    const context = Number(values.context);
    print(`first difference: entry ${d.a + 1} (of ${a.length} in A, ${b.length} in B)`);
    for (const [label, list, at] of [
        ["A", a, d.a],
        ["B", b, d.b],
    ] as const) {
        print(`--- ${label}`);
        for (let k = Math.max(0, at - context); k < Math.min(list.length, at + context); ++k)
            print(`${k === at ? ">" : " "} ${list[k]!.slice(0, 300)}`);
    }
    return 1;
}

if (isMain(import.meta.url)) await runMain(() => main());
