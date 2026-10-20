// Compares converted levels' .qb script by script (NeverScript decompiles), new vs reference staging folders.
// usage: node tools/level_port/script_diff.ts <new staging> <reference staging> [show N]
// (Its examples come in the order the scripts are found - new's, then the reference's own - where its Python
// version's came in a set's order, different from run to run.)

import { spawnSync } from "node:child_process";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { unifiedDiff } from "../lib/difflib.ts";
import { isMain, print, pyJoin, pyParseInt, pyRepr, pySplitlines, readTextPy, runMain } from "../lib/py.ts";
import { finditer, sub } from "../lib/re.ts";

export const NS = "C:\\Dev\\thug2-decomp\\out\\neverscript\\ns.exe";

const SCRIPT = /^script (\S+) \{(.*?)^\} *$/ms;

function scripts(file: string, tmp: string): [Map<string, string>, string] {
    const out = path.join(tmp, "x.ns");
    const result = spawnSync(NS, ["-d", file, "-o", out]);
    if (result.error) throw result.error;
    let text = readTextPy(out, "replace");
    const found = new Map<string, string>();
    for (const m of finditer(SCRIPT, text)) found.set(m.str(1), m.str(0));
    text = sub(/^__register_checksums__.*$/m, "", text);
    const rest = sub(SCRIPT, "", text);
    return [found, rest];
}

// Python's glob.glob(<root>\levels\*\*.qb), sorted: no names starting with ".", the extension in any case (Windows).
function qbFiles(root: string): string[] {
    const levels = pyJoin(root, "levels");
    const found: string[] = [];
    if (!fs.existsSync(levels) || !fs.statSync(levels).isDirectory()) return found;
    for (const dir of fs.readdirSync(levels)) {
        if (dir.startsWith(".")) continue;
        const folder = pyJoin(levels, dir);
        if (!fs.statSync(folder).isDirectory()) continue;
        for (const name of fs.readdirSync(folder)) {
            if (!name.startsWith(".") && name.toLowerCase().endsWith(".qb")) found.push(pyJoin(folder, name));
        }
    }
    return found.sort((a, b) => (a < b ? -1 : a > b ? 1 : 0));
}

export function main(argv: readonly string[] = process.argv.slice(2)): number {
    if (argv.length < 2)
        throw new RangeError("list index out of range (usage: script_diff <new staging> <reference staging> [show N])");
    const [newRoot, refRoot] = argv as [string, string];
    const show = argv.length > 2 ? pyParseInt(argv[2]!) : 0;
    const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "tmp"));
    try {
        const total = new Map([
            ["same", 0],
            ["changed", 0],
            ["only new", 0],
            ["only ref", 0],
            ["data differs", 0],
        ]);
        const bump = (key: string): void => {
            total.set(key, total.get(key)! + 1);
        };
        const examples: [string, string, string, string, string][] = [];
        for (const f of qbFiles(newRoot)) {
            const r = pyJoin(refRoot, path.relative(newRoot, f));
            if (!fs.existsSync(r)) {
                print("no reference:", r);
                continue;
            }
            if (fs.readFileSync(f).equals(fs.readFileSync(r))) continue;
            const [a, ra] = scripts(f, tmp);
            const [b, rb] = scripts(r, tmp);
            if (ra !== rb) bump("data differs");
            const level = path.basename(f);
            for (const name of new Set([...a.keys(), ...b.keys()])) {
                if (!b.has(name)) {
                    bump("only new");
                    examples.push([level, name, "only new", a.get(name)!, ""]);
                } else if (!a.has(name)) {
                    bump("only ref");
                    examples.push([level, name, "only ref", "", b.get(name)!]);
                } else if (a.get(name) !== b.get(name)) {
                    bump("changed");
                    examples.push([level, name, "changed", a.get(name)!, b.get(name)!]);
                } else {
                    bump("same");
                }
            }
        }
        print(pyRepr(total));
        for (const [level, name, kind, x, y] of examples.slice(0, show)) {
            print("=====", level, name, kind);
            for (const line of unifiedDiff(pySplitlines(y), pySplitlines(x), "ref", "new", "", "", 1, "")) print(line);
        }
        return 0;
    } finally {
        fs.rmSync(tmp, { recursive: true, force: true });
    }
}

if (isMain(import.meta.url)) await runMain(() => main());
