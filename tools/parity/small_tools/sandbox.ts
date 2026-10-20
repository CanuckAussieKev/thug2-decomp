// NOT ORIGINAL - runs one of the small tools inside a scratch copy of the repo, for the parity cases
// (tools/parity/small_tools/cases.ts): those tools read and write paths relative to the current folder
// (docs/PROGRESS.md, src/game/...) or to their own location (src/game/front/automation_tables.inc, out/...), so each
// side gets its own copy of what the tool needs and runs there - the real repo is never written.
//
//   node tools/parity/small_tools/sandbox.ts <root> [--copy <repo path>]... [--put <file>=<root path>]...
//       [--mkdir <root path>]... [--qbnorm <root path>]... [--tiesort <root path>]...
//       -- python|node <tool path in the repo> [args...]
//
// The tool itself (and tools/lib for a TypeScript one) is copied too and run from <root> (its working folder); TEMP /
// TMP point at <root>\tmp. Afterwards every copied file that is unchanged is deleted, so <root> holds only what the
// tool wrote or changed - which the harness compares. "<root>" replaces the root's path in the tool's stdout / stderr
// (the two sides' roots differ). The exit code is the tool's. --qbnorm: that .qb's 0x2B name entries are sorted
// afterwards (tools/parity/converters/qb_norm.ts: ns.exe writes them in a different order on every run).
// --tiesort: in that file, neighbouring "quoted" names equal but for case are put in code point order (Python's
// sorted(set, key=str.lower) leaves them in the set's hash order, different on every run; they have the same length,
// so the lines stay as they were).
// Python's stdout and stderr are read as cp1252: under -I it ignores PYTHONIOENCODING and writes the ANSI code page to
// a pipe (Node writes UTF-8) - compared as text.

import { spawnSync } from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { decodeCp1252 } from "../../lib/py.ts";
import { normalise } from "../converters/qb_norm.ts";

const REPO = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..", "..");

export function tieSort(text: string): string {
    const names = [...text.matchAll(/"([^"\n]*)"/g)].map((m) => m[1]!);
    const sorted: string[] = [];
    for (let i = 0; i < names.length; ) {
        let j = i + 1;
        while (j < names.length && names[j]!.toLowerCase() === names[i]!.toLowerCase()) j++;
        sorted.push(...names.slice(i, j).sort((a, b) => (a < b ? -1 : a > b ? 1 : 0)));
        i = j;
    }
    let k = 0;
    return text.replace(/"([^"\n]*)"/g, () => `"${sorted[k++]}"`);
}

function main(): number {
    const argv = process.argv.slice(2);
    const sep = argv.indexOf("--");
    const [root0, ...opts] = argv.slice(0, sep);
    const [lang, tool, ...args] = argv.slice(sep + 1);
    if (sep < 0 || root0 === undefined || lang === undefined || tool === undefined) {
        process.stderr.write("usage: sandbox.ts <root> [--copy p]... [--put f=p]... -- python|node <tool> [args...]\n");
        return 2;
    }
    const root = path.resolve(root0);
    const copies: [string, string][] = []; // [source, destination]
    const folders: string[] = [];
    const qbs: string[] = [];
    const tiesorts: string[] = [];
    for (let i = 0; i < opts.length; i += 2) {
        const value = opts[i + 1]!;
        if (opts[i] === "--copy") copies.push([path.join(REPO, value), path.join(root, value)]);
        else if (opts[i] === "--put") {
            const [from, to] = value.split("=") as [string, string];
            copies.push([from, path.join(root, to)]);
        } else if (opts[i] === "--mkdir") folders.push(path.join(root, value));
        else if (opts[i] === "--qbnorm") qbs.push(path.join(root, value));
        else if (opts[i] === "--tiesort") tiesorts.push(path.join(root, value));
        else throw new Error(`unknown option ${opts[i]}`);
    }
    copies.push([path.join(REPO, tool), path.join(root, tool)]);
    if (tool.endsWith(".ts")) copies.push([path.join(REPO, "tools", "lib"), path.join(root, "tools", "lib")]);

    // copy, remembering each copied file's bytes
    const copied = new Map<string, Buffer>();
    const copy = (from: string, to: string): void => {
        if (fs.statSync(from).isDirectory()) {
            for (const name of fs.readdirSync(from)) copy(path.join(from, name), path.join(to, name));
            return;
        }
        fs.mkdirSync(path.dirname(to), { recursive: true });
        fs.copyFileSync(from, to);
        copied.set(to, fs.readFileSync(to));
    };
    for (const [from, to] of copies) copy(from, to);
    for (const f of folders) fs.mkdirSync(f, { recursive: true });
    const tmp = path.join(root, "tmp");
    fs.mkdirSync(tmp, { recursive: true });

    const command = lang === "python" ? "python" : process.execPath;
    const r = spawnSync(command, [...(lang === "python" ? ["-I"] : []), path.join(root, tool), ...args], {
        cwd: root,
        env: { ...process.env, TEMP: tmp, TMP: tmp },
        maxBuffer: 1 << 30,
    });
    if (r.error) throw r.error;
    const mask = (b: Buffer): string => {
        let text = lang === "python" ? decodeCp1252(b) : b.toString("utf-8");
        for (const form of [root, root.replaceAll("\\", "/")]) text = text.split(form).join("<root>");
        return text;
    };
    process.stdout.write(mask(r.stdout));
    process.stderr.write(mask(r.stderr));

    for (const q of qbs) if (fs.existsSync(q)) fs.writeFileSync(q, normalise(fs.readFileSync(q)));
    for (const f of tiesorts) if (fs.existsSync(f)) fs.writeFileSync(f, tieSort(fs.readFileSync(f, "utf-8")));

    // leave only what the tool wrote or changed
    for (const [file, bytes] of copied) {
        if (fs.existsSync(file) && fs.readFileSync(file).equals(bytes)) fs.rmSync(file);
    }
    const prune = (dir: string): void => {
        for (const e of fs.readdirSync(dir, { withFileTypes: true }))
            if (e.isDirectory()) prune(path.join(dir, e.name));
        if (dir !== root && fs.readdirSync(dir).length === 0) fs.rmdirSync(dir);
    };
    prune(root);
    return r.status ?? 1;
}

process.exitCode = main();
