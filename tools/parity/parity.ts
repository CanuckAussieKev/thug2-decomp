// NOT ORIGINAL - the Python -> TypeScript parity harness (docs/typescript.md, "Proving equivalence"): runs each case's
// Python command and its TypeScript twin on the same inputs, each writing into its own scratch folder, and compares
// their exit codes, their stdout and every file they wrote, byte for byte.
//
//   node tools/parity/parity.ts <cases.ts> [--scratch <folder>] [--only <text>] [--jobs <n>] [--keep]
//
// A cases module (tools/parity/<area>/cases.ts) default-exports a function returning the cases:
//   { name, python: [args after `python -I`], ts: [args after `node`], stdout?: "exact" | "sorted" | "ignore",
//     setup?: [python args run once before the case, for both sides' inputs], nonzeroOk?: true }
// "{out}" in the arguments is the side's own output folder (<scratch>\<case>\py or \ts), "{in}" the case's shared
// input folder (<scratch>\<case>\in). Python runs with PYTHONIOENCODING=utf-8 and PYTHONHASHSEED=0; stdout is
// compared with "\r\n" read as "\n" (Python on Windows writes "\r\n", Node "\n").

import { spawn } from "node:child_process";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { pathToFileURL } from "node:url";
import { parseArgs } from "node:util";

export interface Case {
    name: string;
    python: string[];
    ts: string[];
    stdout?: "exact" | "sorted" | "ignore";
    setup?: string[];
    // a non-zero exit (the same on both sides) is part of the result (col_check's 1: unreachable faces; an error
    // case) - otherwise a case whose two sides both fail is a failure
    nonzeroOk?: boolean;
}

interface Run {
    code: number | null;
    stdout: string;
    stderr: string;
    seconds: number;
}

function run(command: string, args: string[], env: NodeJS.ProcessEnv): Promise<Run> {
    const start = Date.now();
    return new Promise((resolve) => {
        const child = spawn(command, args, { env, windowsHide: true });
        const out: Buffer[] = [];
        const err: Buffer[] = [];
        child.stdout.on("data", (b: Buffer) => out.push(b));
        child.stderr.on("data", (b: Buffer) => err.push(b));
        child.on("error", (e) => resolve({ code: -1, stdout: "", stderr: String(e), seconds: 0 }));
        child.on("close", (code) =>
            resolve({
                code,
                stdout: Buffer.concat(out).toString("utf-8"),
                stderr: Buffer.concat(err).toString("utf-8"),
                seconds: (Date.now() - start) / 1000,
            }),
        );
    });
}

function files(root: string): string[] {
    const found: string[] = [];
    const walk = (dir: string, rel: string): void => {
        if (!fs.existsSync(dir)) return;
        for (const entry of fs.readdirSync(dir, { withFileTypes: true }).sort((a, b) => (a.name < b.name ? -1 : 1))) {
            const r = rel ? `${rel}/${entry.name}` : entry.name;
            if (entry.isDirectory()) walk(path.join(dir, entry.name), r);
            else found.push(r);
        }
    };
    walk(root, "");
    return found;
}

function firstDifference(a: Buffer, b: Buffer): string {
    const n = Math.min(a.length, b.length);
    let i = 0;
    while (i < n && a[i] === b[i]) i++;
    const textual = (buf: Buffer): boolean => !buf.subarray(0, 4096).includes(0);
    if (textual(a) && textual(b)) {
        const line = a.subarray(0, i).toString("latin1").split("\n").length;
        const la = a.toString("latin1").split("\n")[line - 1] ?? "";
        const lb = b.toString("latin1").split("\n")[line - 1] ?? "";
        return `line ${line}:\n      py: ${la.slice(0, 300)}\n      ts: ${lb.slice(0, 300)}`;
    }
    return `byte 0x${i.toString(16)} (sizes ${a.length} / ${b.length})`;
}

const normalise = (text: string): string => text.replace(/\r\n/g, "\n");

async function runCase(c: Case, scratch: string, keep: boolean): Promise<{ ok: boolean; report: string[] }> {
    const base = path.join(scratch, c.name.replace(/[^\w.-]+/g, "_"));
    const py = path.join(base, "py");
    const ts = path.join(base, "ts");
    const input = path.join(base, "in");
    fs.rmSync(base, { recursive: true, force: true });
    for (const d of [py, ts, input]) fs.mkdirSync(d, { recursive: true });
    const fill = (args: string[], out: string): string[] =>
        args.map((a) => a.replaceAll("{out}", out).replaceAll("{in}", input));
    const env = { ...process.env, PYTHONIOENCODING: "utf-8", PYTHONHASHSEED: "0" };
    const report: string[] = [];
    if (c.setup) {
        const s = await run("python", ["-I", ...fill(c.setup, input)], env);
        if (s.code !== 0) return { ok: false, report: [`  setup failed (${s.code}): ${s.stderr.slice(-2000)}`] };
    }
    const [p, t] = await Promise.all([
        run("python", ["-I", ...fill(c.python, py)], env),
        run(process.execPath, fill(c.ts, ts), env),
    ]);
    let ok = true;
    const fail = (line: string): void => {
        ok = false;
        report.push(`  ${line}`);
    };
    if (p.code !== t.code) fail(`exit codes differ: python ${p.code}, ts ${t.code}`);
    else if (p.code !== 0 && !c.nonzeroOk)
        fail(`both exited with ${p.code} (nonzeroOk: true where that is the result)`);
    if (p.code !== 0 || t.code !== 0) {
        report.push(`  python stderr: ${p.stderr.slice(-1500)}`, `  ts stderr: ${t.stderr.slice(-1500)}`);
    }
    const mode = c.stdout ?? "exact";
    if (mode !== "ignore") {
        let a = normalise(p.stdout);
        let b = normalise(t.stdout);
        if (mode === "sorted") {
            a = a.split("\n").sort().join("\n");
            b = b.split("\n").sort().join("\n");
        }
        if (a !== b) fail(`stdout differs: ${firstDifference(Buffer.from(a, "latin1"), Buffer.from(b, "latin1"))}`);
    }
    const pf = files(py);
    const tf = files(ts);
    const onlyPy = pf.filter((f) => !tf.includes(f));
    const onlyTs = tf.filter((f) => !pf.includes(f));
    if (onlyPy.length) fail(`only python wrote: ${onlyPy.slice(0, 10).join(", ")}${onlyPy.length > 10 ? " ..." : ""}`);
    if (onlyTs.length) fail(`only ts wrote: ${onlyTs.slice(0, 10).join(", ")}${onlyTs.length > 10 ? " ..." : ""}`);
    let differing = 0;
    let bytes = 0;
    const both = pf.filter((f) => tf.includes(f));
    for (const f of both) {
        const a = fs.readFileSync(path.join(py, f));
        const b = fs.readFileSync(path.join(ts, f));
        bytes += a.length;
        if (!a.equals(b)) {
            differing++;
            if (differing <= 5) fail(`${f} differs: ${firstDifference(a, b)}`);
        }
    }
    if (differing > 5) fail(`... ${differing} files differ`);
    report.unshift(
        `${ok ? "ok  " : "FAIL"} ${c.name}: ${both.length} files (${bytes} bytes), stdout ${normalise(p.stdout).length} chars; ` +
            `python ${p.seconds.toFixed(1)} s, ts ${t.seconds.toFixed(1)} s`,
    );
    if (ok && !keep) fs.rmSync(base, { recursive: true, force: true });
    return { ok, report };
}

async function main(): Promise<number> {
    const { values, positionals } = parseArgs({
        allowPositionals: true,
        options: {
            scratch: { type: "string", default: path.join(os.tmpdir(), "thug2-decomp-parity") },
            only: { type: "string" },
            jobs: { type: "string", default: "3" },
            keep: { type: "boolean", default: false },
        },
    });
    if (positionals.length !== 1) {
        console.error(
            "usage: node tools/parity/parity.ts <cases.ts> [--scratch <folder>] [--only <text>] [--jobs <n>] [--keep]",
        );
        return 2;
    }
    const module = (await import(pathToFileURL(path.resolve(positionals[0]!)).href)) as { default: () => Case[] };
    let cases = module.default();
    if (values.only) cases = cases.filter((c) => c.name.includes(values.only!));
    const scratch = path.resolve(values.scratch);
    fs.mkdirSync(scratch, { recursive: true });
    const jobs = Math.max(1, Number.parseInt(values.jobs, 10) || 1);
    let failed = 0;
    let next = 0;
    const summary: string[] = [];
    const worker = async (): Promise<void> => {
        while (next < cases.length) {
            const c = cases[next++]!;
            const { ok, report } = await runCase(c, scratch, values.keep);
            if (!ok) failed++;
            process.stdout.write(`${report.join("\n")}\n`);
            summary.push(...report);
        }
    };
    await Promise.all(Array.from({ length: jobs }, worker));
    const line = `${cases.length - failed} of ${cases.length} cases the same${failed ? ` - ${failed} differ (outputs kept in ${scratch})` : ""}`;
    summary.push(line);
    fs.writeFileSync(path.join(scratch, "report.txt"), `${summary.join("\n")}\n`);
    console.log(line);
    return failed ? 1 : 0;
}

process.exitCode = await main();
