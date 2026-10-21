// NOT ORIGINAL - two conversions of the same levels compared node by node as THUG2 resolves them: every node's fields
// with the global structures it names put in place (nodes.sharedDifferences' form), so a level whose node fields were
// shared differently (or not at all) compares equal when its nodes still mean the same. Shared structures themselves
// (`<level>_inc_<n>`) aren't compared as globals; every other global is.
//
//   node tools/level_port/compare_nodes.ts <levels folder, before> <levels folder, after> [--verbose]
//
// Each folder: <folder>\<level>\<level>.qb (a converter's staging levels folder or an installed overlay's). One line a
// level: its nodes, the nodes that resolve differently, the other globals that differ, and its script component
// count (components.ts) before / after. --verbose: the differing nodes' names.

import * as fs from "node:fs";
import * as path from "node:path";
import { isMain, print, runMain } from "../lib/py.ts";
import { components } from "./components.ts";
import { resolvedFields } from "./nodes.ts";
import { Name, nameText, QbFile, type QbValue, Struct } from "./qb.ts";
import { encodeValue } from "./qb_write.ts";

const INC = /_inc_\d+$/i;

export interface Comparison {
    nodes: [number, number];
    differing: string[]; // the nodes (by Name, else index) that resolve differently
    globals: string[]; // other globals that differ or are missing on one side
    components: [number, number];
}

export function compareLevel(before: Buffer, after: Buffer, level: string): Comparison {
    const a = new QbFile(before, `${level}.qb`, true);
    const b = new QbFile(after, `${level}.qb`, true);
    const ga = new Map(a.globals.map(([k, v]): [number, QbValue] => [k.value, v]));
    const gb = new Map(b.globals.map(([k, v]): [number, QbValue] => [k.value, v]));
    const isArray = (k: number, names: ReadonlyMap<number, string>): boolean =>
        (names.get(k) ?? "").toLowerCase() === `${level.toLowerCase()}_nodearray`;
    const arrayKey = [...ga.keys()].find((k) => isArray(k, a.names)) ?? [...gb.keys()].find((k) => isArray(k, b.names));
    const na = arrayKey === undefined ? [] : ((ga.get(arrayKey) as QbValue[] | undefined) ?? []);
    const nb = arrayKey === undefined ? [] : ((gb.get(arrayKey) as QbValue[] | undefined) ?? []);
    // (matched by Name where the node has one - its first, second... node of that name -, else by index)
    const keys = (array: readonly QbValue[], names: ReadonlyMap<number, string>): string[] => {
        const seen = new Map<string, number>();
        return array.map((n, i) => {
            const v = n instanceof Struct ? n.get("Name") : undefined;
            if (!(v instanceof Name)) return `#${i}`;
            const name = nameText(v.value, names);
            const k = seen.get(name) ?? 0;
            seen.set(name, k + 1);
            return k ? `${name}(${k + 1})` : name;
        });
    };
    const differing: string[] = [];
    const byName = new Map<string, QbValue>();
    const keysB = keys(nb, b.names);
    nb.forEach((n, i) => {
        byName.set(keysB[i]!, n);
    });
    const keysA = keys(na, a.names);
    na.forEach((n, i) => {
        const key = keysA[i]!;
        const other = byName.get(key);
        if (other === undefined || resolvedFields(n, ga) !== resolvedFields(other, gb)) differing.push(key);
    });
    const globals: string[] = [];
    const names = new Map([...a.names, ...b.names]);
    for (const k of new Set([...ga.keys(), ...gb.keys()])) {
        if (k === arrayKey || INC.test(names.get(k) ?? "")) continue;
        const x = ga.get(k);
        const y = gb.get(k);
        if (x === undefined || y === undefined || encodeValue(x).toString("hex") !== encodeValue(y).toString("hex"))
            globals.push(nameText(k, names));
    }
    const count = (q: QbFile): number => q.globals.reduce((n, [, v]) => n + components(v), 0);
    return { nodes: [na.length, nb.length], differing, globals, components: [count(a), count(b)] };
}

function main(argv: string[]): number {
    const verbose = argv.includes("--verbose");
    const [beforeDir, afterDir] = argv.filter((x) => x !== "--verbose");
    if (!beforeDir || !afterDir) {
        print(
            "usage: node tools/level_port/compare_nodes.ts <levels folder, before> <levels folder, after> [--verbose]",
        );
        return 2;
    }
    print("level\tnodes\tdiffering nodes\tother globals differing\tcomponents before\tafter\tsaved");
    let bad = 0;
    for (const level of fs.readdirSync(afterDir).sort()) {
        const fa = path.join(beforeDir, level, `${level}.qb`);
        const fb = path.join(afterDir, level, `${level}.qb`);
        if (!fs.existsSync(fa) || !fs.existsSync(fb)) continue;
        const c = compareLevel(fs.readFileSync(fa), fs.readFileSync(fb), level);
        if (c.differing.length || c.globals.length || c.nodes[0] !== c.nodes[1]) bad++;
        const nodes = c.nodes[0] === c.nodes[1] ? `${c.nodes[0]}` : `${c.nodes[0]} -> ${c.nodes[1]}`;
        print(
            `${level}\t${nodes}\t${c.differing.length}\t${c.globals.length}\t${c.components[0]}\t${c.components[1]}\t` +
                `${c.components[0] - c.components[1]}`,
        );
        if (verbose && c.differing.length) print(`  nodes: ${c.differing.slice(0, 20).join(" ")}`);
        if (verbose && c.globals.length) print(`  globals: ${c.globals.slice(0, 20).join(" ")}`);
    }
    print(bad ? `${bad} level(s) differ` : "every level's nodes resolve to the same fields");
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main(process.argv.slice(2)));
