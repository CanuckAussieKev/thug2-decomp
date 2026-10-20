// NOT ORIGINAL - mutation testing: prove the self-tests catch deliberate bugs.
//
//     node tools/mutate.ts <spec.ts | spec.json | spec.py> [names...]
//
// <spec.ts> exports MUTS = [[file, name, oldText, newText], ...] (oldText must occur exactly once in the file) and
// may export BUILD = a command that builds out/build/verify (default: tools/verify_build.cmd), and
// TESTS = "TestA,TestB" - only the self-tests whose names contain one of those words run (THUG2_SELFTESTS; all of
// them run otherwise: a full check takes ~25 s, a batch's own tests a second or two). Each mutation is applied, built
// and checked with out/build/verify/thug2_check.exe, then the file is restored. BUILD needn't configure cmake again:
// `cmake --build` alone (after VsDevCmd) is enough once the folder is configured.
// With names, only those mutations run. A spec:
//
//     export const TESTS = "Pcm";
//     export const MUTS: [string, string, string, string][] = [
//         ["src/game/sys/pcm.cpp", "volume-off-by-one", "volume - 1", "volume"],
//         ["src/game/sys/pcm.cpp", "track-order", String.raw`a\b`, String.raw`b\a`], // (raw: backslashes as typed)
//     ];
//
// A .json spec ({"MUTS": [...], "TESTS": "..."}) works too, and so does a Python spec of tools/mutate.py's days
// (`MUTS = [(file, name, old, new), ...]`: Python reads it and hands the values over). The files are
// matched as Python's text mode read them ("\r\n" read as "\n": write oldText with "\n"); a mutated file is written with
// "\r\n" line ends and the original put back byte for byte.
//
// A mutation that makes the checker hang counts as caught. The checker's output goes to a file rather than a
// pipe: its helper child process would hold a pipe open past the timeout, and taskkill /T (by our process id)
// ends both.

import { spawn, spawnSync } from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { decodeCp1252, isMain, print, pyStrip, readTextPy, runMain, writeTextPy } from "./lib/py.ts";

const REPO = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const TIMEOUT_MS = 60_000;

export type Mutation = readonly [file: string, name: string, oldText: string, newText: string];

interface Spec {
    MUTS: readonly Mutation[];
    BUILD?: string;
    TESTS?: string;
}

// str.count: non-overlapping occurrences
export function countOccurrences(text: string, sub: string): number {
    if (sub === "") return text.length + 1;
    let n = 0;
    for (let i = text.indexOf(sub); i >= 0; i = text.indexOf(sub, i + sub.length)) n++;
    return n;
}

// open(path, errors="replace").read(): cp1252 (Windows' ANSI code page), U+FFFD for its five undefined bytes,
// universal newlines
const CP1252_UNDEFINED = new Set([0x81, 0x8d, 0x8f, 0x90, 0x9d]);
function readCp1252Replace(file: string): string {
    // (each byte is one character in cp1252)
    const data = fs.readFileSync(file);
    const text = decodeCp1252(data.map((b) => (CP1252_UNDEFINED.has(b) ? 0x3f : b)));
    let out = "";
    for (let i = 0; i < data.length; i++) out += CP1252_UNDEFINED.has(data[i]!) ? "�" : text[i];
    return out.replace(/\r\n?/g, "\n");
}

// The spec: a .ts / .js module, a .json file ({"MUTS": [...], "BUILD": ..., "TESTS": ...}), or a Python spec of
// tools/mutate.py's days (MUTS = [(file, name, old, new), ...]) - run by Python, which hands its values over as JSON.
async function loadSpec(specPath: string): Promise<Spec> {
    const full = path.resolve(specPath);
    if (full.toLowerCase().endsWith(".json")) return JSON.parse(fs.readFileSync(full, "utf-8")) as Spec;
    if (full.toLowerCase().endsWith(".py")) {
        const dump =
            "import json, runpy, sys; s = runpy.run_path(sys.argv[1]); " +
            "print(json.dumps({k: s[k] for k in ('MUTS', 'BUILD', 'TESTS') if k in s}))";
        const r = spawnSync("python", ["-I", "-c", dump, full], { encoding: "utf-8", maxBuffer: 1 << 30 });
        if (r.error) throw r.error;
        if (r.status !== 0) throw new Error(`${specPath}: ${r.stderr}`);
        return JSON.parse(r.stdout) as Spec;
    }
    return (await import(pathToFileURL(full).href)) as Spec;
}

const runBuild = (build: string): number | null =>
    spawnSync("cmd", ["/c", build], { stdio: ["inherit", "pipe", "pipe"], maxBuffer: 1 << 30 }).status;

// Runs the checker with its output into logPath: its exit code, or null when it ran past the timeout (then it and its
// children are ended).
function check(checker: string, logPath: string, env: NodeJS.ProcessEnv): Promise<number | null> {
    const fd = fs.openSync(logPath, "w");
    return new Promise((resolve, reject) => {
        const child = spawn(checker, [], { cwd: REPO, env, stdio: ["inherit", fd, fd] });
        let timedOut = false;
        const timer = setTimeout(() => {
            timedOut = true;
            // (this run's checker only, by its id: other checkouts' checks may be running)
            spawnSync("taskkill", ["/F", "/T", "/PID", String(child.pid)], { stdio: "pipe" });
        }, TIMEOUT_MS);
        child.on("error", (e) => {
            clearTimeout(timer);
            fs.closeSync(fd);
            reject(e);
        });
        child.on("exit", (code) => {
            clearTimeout(timer);
            fs.closeSync(fd);
            resolve(timedOut ? null : (code ?? 1));
        });
    });
}

async function main(): Promise<number> {
    const [specPath, ...only] = process.argv.slice(2);
    if (specPath === undefined) throw new RangeError("usage: node tools/mutate.ts <spec.ts> [names...]");
    const spec = await loadSpec(specPath);
    const build = spec.BUILD ?? path.join(REPO, "tools", "verify_build.cmd");
    const checker = path.join(REPO, "out", "build", "verify", "thug2_check.exe");
    const logPath = path.join(REPO, "out", "mutate_check_output.txt"); // (per checkout: parallel runs don't clash)
    const env: NodeJS.ProcessEnv = { ...process.env };
    if (spec.TESTS) env.THUG2_SELFTESTS = spec.TESTS;

    for (const [file, name, oldText, newText] of spec.MUTS) {
        if (only.length && !only.includes(name)) continue;
        const raw = fs.readFileSync(file);
        const original = readTextPy(file);
        const count = countOccurrences(original, oldText);
        if (count !== 1) throw new Error(`AssertionError: ${name}: old text found ${count} times (once wanted)`);
        const at = original.indexOf(oldText);
        writeTextPy(file, original.slice(0, at) + newText + original.slice(at + oldText.length));
        try {
            if (runBuild(build) !== 0) {
                print(name, "BUILD FAILED (fix the mutation)");
                continue;
            }
            const code = await check(checker, logPath, env);
            if (code === null) {
                print(name, "CAUGHT (hang)");
                continue;
            }
            const failed = readCp1252Replace(logPath)
                .split("\n")
                .filter(
                    (l) => l.includes("SELFTEST FAIL") || l.includes("SELFTEST CRASH") || l.includes("SELFTEST HANG"),
                )
                .map((l) => pyStrip(l));
            // (every failing test, so an unrelated flaky one doesn't hide which test caught it)
            print(
                name,
                code !== 0 ? "CAUGHT" : "missed",
                code,
                failed.length ? `- ${failed.slice(0, 4).join(" | ")}` : "",
            );
        } finally {
            fs.writeFileSync(file, raw);
        }
    }

    // Leave a build of the unmutated code behind (the last build was of a mutation).
    runBuild(build);
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
