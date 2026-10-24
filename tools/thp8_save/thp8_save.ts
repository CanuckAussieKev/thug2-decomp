// Tony Hawk's Project 8 (Xbox 360, Project8Recomp) save files: dump them, max them out. Format: save.ts; what "max"
// sets: max.ts. Notes: docs/pending/thp8-save.md.
//
//   node tools/thp8_save/thp8_save.ts dump <skater-progress> [--thp8 C:\Games\THP8]
//   node tools/thp8_save/thp8_save.ts max <in> <out> [--thp8 C:\Games\THP8]
//   node tools/thp8_save/thp8_save.ts diff <a> <b> [--thp8 C:\Games\THP8]

import * as fs from "node:fs";
import * as path from "node:path";
import { type GameData, loadGameData } from "./game_data.ts";
import { MAX_CHANGES, maxSave } from "./max.ts";
import {
    readSave,
    type SaveFile,
    type SaveStruct,
    structBytes,
    type Value,
    verifyChecksums,
    writeSave,
} from "./save.ts";

export function formatValue(v: Value, names: ReadonlyMap<number, string>, indent: string): string[] {
    const nm = (h: number): string => names.get(h >>> 0) ?? `#0x${(h >>> 0).toString(16).padStart(8, "0")}`;
    switch (v.kind) {
        case "int":
            return [String(v.value)];
        case "float":
            return [`${v.value}f`];
        case "string":
            return [JSON.stringify(v.value)];
        case "wide":
            return [`w${JSON.stringify(v.value)}`];
        case "pair":
        case "vector":
            return [`(${v.value.join(", ")})`];
        case "name":
            return [nm(v.value)];
        case "ref":
            return [`$${nm(v.value)}`];
        case "struct":
            return ["{", ...formatStruct(v.value, names, `${indent}    `), `${indent}}`];
        case "array": {
            if (v.value.length === 0) return [`[] (type 0x${v.elementType.toString(16)})`];
            const simple = v.value.every((e) => e.kind !== "struct" && e.kind !== "array");
            if (simple) {
                const parts = v.value.map((e) => formatValue(e, names, indent)[0]!);
                return [`[${parts.join(", ")}]`];
            }
            const out = ["["];
            for (const e of v.value) {
                const lines = formatValue(e, names, `${indent}    `);
                out.push(`${indent}    ${lines[0]}`, ...lines.slice(1));
            }
            out.push(`${indent}]`);
            return out;
        }
    }
}

export function formatStruct(s: SaveStruct, names: ReadonlyMap<number, string>, indent = ""): string[] {
    const out: string[] = [];
    for (const c of s.components) {
        const key = c.name === 0 ? "" : `${names.get(c.name) ?? `#0x${c.name.toString(16).padStart(8, "0")}`} = `;
        const lines = formatValue(c.value, names, indent);
        out.push(`${indent}${key}${lines[0]}`, ...lines.slice(1));
    }
    return out;
}

function dump(file: string, game: GameData): string {
    const data = fs.readFileSync(file);
    const save = readSave(data, game.tables);
    const lines = [
        `// ${path.basename(file)}: ${data.length} bytes, used ${save.stored.used}, summary ${save.stored.summarySize}, version 0x${save.version.toString(16)}`,
        ...verifyChecksums(data).map(([what, ok]) => `// ${what}: ${ok ? "ok" : "MISMATCH"}`),
        "summary = {",
        ...formatStruct(save.summary, game.names, "    "),
        "}",
        "main = {",
        ...formatStruct(save.main, game.names, "    "),
        "}",
    ];
    return lines.join("\n");
}

// Round trip: reading and writing the file gives its bytes back.
export function roundTrips(data: Buffer, game: GameData): boolean {
    const save = readSave(data, game.tables);
    return writeSave(save, data.length).equals(data);
}

function main(argv: string[]): void {
    let thp8 = "C:\\Games\\THP8";
    const args: string[] = [];
    for (let i = 0; i < argv.length; i++) {
        if (argv[i] === "--thp8") thp8 = argv[++i]!;
        else args.push(argv[i]!);
    }
    const [cmd, ...rest] = args;
    if (cmd === "dump" && rest.length === 1) {
        const game = loadGameData(thp8);
        process.stdout.write(`${dump(rest[0]!, game)}\n`);
        return;
    }
    if (cmd === "max" && rest.length === 2) {
        const game = loadGameData(thp8);
        const data = fs.readFileSync(rest[0]!);
        if (!roundTrips(data, game)) throw new Error(`${rest[0]}: doesn't round-trip - not writing`);
        const save = readSave(data, game.tables);
        const report = maxSave(save, game);
        const out = writeSave(save, data.length);
        const back = readSave(out, game.tables);
        if (!structBytes(back.main).equals(structBytes(save.main))) throw new Error("re-read differs");
        if (!writeSave(back, out.length).equals(out)) throw new Error("output doesn't round-trip");
        for (const [what, ok] of verifyChecksums(out)) if (!ok) throw new Error(`output: ${what} wrong`);
        const changes = diffSaves(readSave(data, game.tables), back, game.names);
        const stray = changes.filter((line) => !MAX_CHANGES.some((re) => re.test(line)));
        if (stray.length)
            throw new Error(`changes outside what max sets - not writing:\n${stray.slice(0, 20).join("\n")}`);
        fs.writeFileSync(rest[1]!, out);
        process.stdout.write(`${report.join("\n")}\nwrote ${rest[1]} (used ${back.stored.used} of ${out.length})\n`);
        return;
    }
    if (cmd === "diff" && rest.length === 2) {
        const game = loadGameData(thp8);
        const a = readSave(fs.readFileSync(rest[0]!), game.tables);
        const b = readSave(fs.readFileSync(rest[1]!), game.tables);
        process.stdout.write(`${diffSaves(a, b, game.names).join("\n")}\n`);
        return;
    }
    process.stderr.write(
        "usage: thp8_save.ts dump <file> | max <in> <out> | diff <a> <b>   [--thp8 C:\\Games\\THP8]\n",
    );
    process.exitCode = 2;
}

// The differences of two saves' trees, by path ("main.globalinfo.career.stokens: 4 -> 9999"); a structure's
// components are matched by name (and order among equal names), an array's elements by index.
export function diffSaves(a: SaveFile, b: SaveFile, names: ReadonlyMap<number, string>): string[] {
    const out: string[] = [];
    const nm = (h: number): string => names.get(h >>> 0) ?? `#0x${(h >>> 0).toString(16).padStart(8, "0")}`;
    const text = (v: Value): string => formatValue(v, names, "").join(" ").replace(/\s+/g, " ");
    const value = (path: string, x: Value | undefined, y: Value | undefined): void => {
        if (x === undefined || y === undefined) {
            out.push(`${path}: ${x === undefined ? "(none)" : text(x)} -> ${y === undefined ? "(none)" : text(y)}`);
            return;
        }
        if (x.kind === "struct" && y.kind === "struct") {
            struct(path, x.value, y.value);
            return;
        }
        if (x.kind === "array" && y.kind === "array") {
            const simple = (v: Value[]): boolean => v.every((e) => e.kind !== "struct" && e.kind !== "array");
            if (simple(x.value) && simple(y.value)) {
                const xs = x.value.map(text);
                const ys = y.value.map(text);
                if (xs.length === ys.length && xs.every((e, i) => e === ys[i])) return;
                const xset = new Set(xs);
                const yset = new Set(ys);
                const gone = xs.filter((e) => !yset.has(e));
                const added = ys.filter((e) => !xset.has(e));
                if (gone.length + added.length === 0 && xs.length === ys.length) out.push(`${path}: reordered`);
                else if (xs.length === ys.length && gone.length === xs.length) {
                    for (let i = 0; i < xs.length; i++)
                        if (xs[i] !== ys[i]) out.push(`${path}[${i}]: ${xs[i]} -> ${ys[i]}`);
                } else out.push(`${path}: ${xs.length} -> ${ys.length} (-[${gone.join(", ")}] +[${added.join(", ")}])`);
                return;
            }
            for (let i = 0; i < Math.max(x.value.length, y.value.length); i++)
                value(`${path}[${i}]`, x.value[i], y.value[i]);
            return;
        }
        const tx = text(x);
        const ty = text(y);
        if (tx !== ty || x.kind !== y.kind) out.push(`${path}: ${tx} -> ${ty}`);
    };
    const struct = (path: string, x: SaveStruct, y: SaveStruct): void => {
        const key = (s: SaveStruct): Map<string, Value> => {
            const m = new Map<string, Value>();
            const seen = new Map<number, number>();
            for (const c of s.components) {
                const n = seen.get(c.name) ?? 0;
                seen.set(c.name, n + 1);
                m.set(`${nm(c.name)}${n ? `#${n}` : ""}`, c.value);
            }
            return m;
        };
        const kx = key(x);
        const ky = key(y);
        for (const [k, v] of kx) value(`${path}.${k}`, v, ky.get(k));
        for (const [k, v] of ky) if (!kx.has(k)) value(`${path}.${k}`, undefined, v);
    };
    struct("summary", a.summary, b.summary);
    struct("main", a.main, b.main);
    return out;
}

if (import.meta.main) main(process.argv.slice(2));
