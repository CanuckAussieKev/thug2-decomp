// NOT ORIGINAL - the legal audit's line match: which lines of the repositories' files
// are verbatim game script text. A line counts when, trimmed, it has 20+ characters and is a line of the reference
// scripts (decompiled game scripts as NeverScript .ns: THUG2's Data\scripts and Data\pre archives with ns.exe, THAW's
// with tools/pipeline/thaw_bmx/dump_scripts.ts - made in a scratch folder, never committed). For .edits files
// (tools/pipeline/lib/edits.ts) only the text they store counts: "+" lines and the text of "~" lines.
//
//   node tools/verbatim_scan.ts --ref <folder of .ns> [--ref ...] [--min 20] [--runs] [--lines] [--runs-at N] <file or folder>...
//
// --runs: each file's longest run of matching lines; --lines: every matching line; --runs-at N: where runs reach N.

import * as fs from "node:fs";
import * as path from "node:path";

const args = process.argv.slice(2);
const refs: string[] = [];
const targets: string[] = [];
let min = 20;
let showRuns = false;
let showLines = false;
let runsAt = 0;
for (let i = 0; i < args.length; i++) {
    const a = args[i]!;
    if (a === "--ref") refs.push(args[++i]!);
    else if (a === "--min") min = Number(args[++i]);
    else if (a === "--runs") showRuns = true;
    else if (a === "--lines") showLines = true;
    else if (a === "--runs-at") runsAt = Number(args[++i]);
    else targets.push(a);
}
if (!refs.length || !targets.length) {
    process.stderr.write(
        "usage: verbatim_scan.ts --ref <folder> [--ref ...] [--min N] [--runs] [--lines] [--runs-at N] <path>...\n",
    );
    process.exit(2);
}

function files(p: string, out: string[] = []): string[] {
    if (fs.statSync(p).isDirectory()) {
        for (const e of fs.readdirSync(p)) {
            if (e === "node_modules" || e === ".git" || e === "out") continue;
            files(path.join(p, e), out);
        }
    } else out.push(p);
    return out;
}

const ref = new Set<string>();
for (const r of refs) {
    for (const f of files(r)) {
        if (!f.endsWith(".ns")) continue;
        for (const l of fs.readFileSync(f).toString("latin1").split(/\r?\n/)) {
            const t = l.trim();
            if (t.length >= min) ref.add(t);
        }
    }
}
let totalLines = 0;
let totalHit = 0;
const rows: [string, number, number, number][] = [];
for (const t of targets) {
    for (const f of files(t)) {
        if (!/\.(ts|ns|json|md|txt|cpp|h|py|edits|inc|c|ps1|cmd)$/i.test(f)) continue;
        const lines = fs.readFileSync(f).toString("latin1").split(/\r?\n/);
        let n = 0;
        let hit = 0;
        let run = 0;
        let maxRun = 0;
        lines.forEach((raw, li) => {
            let l = raw;
            if (f.endsWith(".edits")) {
                if (l.startsWith("+ ")) l = l.slice(2);
                else if (l.startsWith("~ ")) l = /^~ \d+ \d+ \d+ ?(.*)$/.exec(l)?.[1] ?? "";
                else return;
            }
            const s = l.trim();
            if (s.length < min) return;
            n++;
            if (!ref.has(s)) {
                run = 0;
                return;
            }
            hit++;
            run++;
            maxRun = Math.max(maxRun, run);
            if (runsAt && run === runsAt) console.log(`  run of ${run}+ lines at ${f}:${li + 1}`);
            if (showLines) console.log(`  ${f}:${li + 1}: ${s}`);
        });
        totalLines += n;
        totalHit += hit;
        if (hit) rows.push([f, hit, n, maxRun]);
    }
}
rows.sort((a, b) => b[1] - a[1]);
for (const [f, hit, n, maxRun] of rows) {
    const share = ((100 * hit) / n).toFixed(1).padStart(5);
    console.log(
        `${String(hit).padStart(5)}/${String(n).padEnd(6)} ${share}%${showRuns ? `  longest run ${maxRun}` : ""}  ${f}`,
    );
}
const all = ((100 * totalHit) / Math.max(1, totalLines)).toFixed(1);
console.log(`total: ${totalHit} of ${totalLines} lines of ${min}+ characters match (${all}%)`);
