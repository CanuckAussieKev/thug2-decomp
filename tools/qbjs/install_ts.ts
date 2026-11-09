// NOT ORIGINAL - translates a game folder's .qb scripts to TypeScript and installs them where the game looks for them
// (docs/qb-ts.md "In the game"). A module is named by its .qb file's bytes (FNV-1a, as src/script_qb/ts_bridge.cpp
// hashes what it loaded): <game>\thug2-decomp-ts\by-hash\<hash>.js - so it is only ever used for the very file it was
// translated from (the stock file, an overlay's or a mod's version: each its own). The readable TypeScript is kept
// beside, in source\<folder>\<path>.ts; the module is its scripts compiled (its globals are the game's own, read from
// the same file). The game uses them with [features] ts_scripts=1.
//
//   node tools/qbjs/install_ts.ts --game <folder> [--only <path>[,...]] [--clear]
//
// Sources: every .qb under Data\scripts, Data\levels and the overlay folders thug2-decomp-* (but -ts); --only: just
// these (paths under the game folder, e.g. Data\scripts\game\misc\level_flags.qb). --clear: the folder emptied first.
// Files that don't translate are listed and stay QB.
import { existsSync, mkdirSync, readdirSync, readFileSync, rmSync, statSync, writeFileSync } from "node:fs";
import { dirname, join, relative } from "node:path";
import { pathToFileURL } from "node:url";
import { compileTS } from "./compile_ts.ts";
import { emitTS } from "./emit_ts.ts";
import { readIR } from "./reader.ts";

export type InstallResult = { installed: string[]; failed: [string, string][] };

// FNV-1a 32 over the bytes (ts_bridge.cpp file_hash).
export function fileHash(bytes: Uint8Array): string {
    let hash = 0x811c9dc5;
    for (const byte of bytes) hash = Math.imul(hash ^ byte, 0x01000193) >>> 0;
    return hash.toString(16).padStart(8, "0");
}

const walk = (path: string): string[] =>
    !existsSync(path)
        ? []
        : statSync(path).isDirectory()
          ? readdirSync(path).flatMap((name) => walk(join(path, name)))
          : path.toLowerCase().endsWith(".qb")
            ? [path]
            : [];

// The folder with the game's Data (a stock install's root steps into its Game folder).
export function gameRoot(folder: string): string {
    if (existsSync(join(folder, "Data", "scripts"))) return folder;
    if (existsSync(join(folder, "Game", "Data", "scripts"))) return join(folder, "Game");
    throw Error(`${folder}: no Data\\scripts - not a game folder`);
}

export function installTS(game: string, only: string[] = [], clear = false): InstallResult {
    const root = gameRoot(game);
    const out = join(root, "thug2-decomp-ts");
    if (clear) rmSync(out, { recursive: true, force: true });
    const overlays = readdirSync(root).filter(
        (name) => name.toLowerCase().startsWith("thug2-decomp-") && name.toLowerCase() !== "thug2-decomp-ts",
    );
    const files = only.length
        ? only.map((path) => join(root, path.replace(/[\\/]+/g, "/")))
        : [
              ...walk(join(root, "Data", "scripts")),
              ...walk(join(root, "Data", "levels")),
              ...overlays.flatMap((name) => walk(join(root, name))),
          ];
    const result: InstallResult = { installed: [], failed: [] };
    const seen = new Set<string>();
    for (const file of files) {
        const name = relative(root, file).replace(/\//g, "\\");
        try {
            const bytes = readFileSync(file);
            const hash = fileHash(bytes);
            if (seen.has(hash)) continue;
            seen.add(hash);
            const ir = readIR(bytes, name);
            const ts = emitTS(ir);
            // The game's module has only the scripts: the globals stay the ones the game made from these very bytes
            // (read_qb_file, just before) - making them again would only cost (a ported level's node arrays run to
            // megabytes), and a global drawing a random number would draw twice.
            const js = compileTS(
                emitTS({ ...ir, definitions: ir.definitions.filter((d) => d.kind === "script") }),
                name,
            );
            const source = join(out, "source", name.replace(/\.qb$/i, ".ts"));
            mkdirSync(dirname(source), { recursive: true });
            writeFileSync(source, ts);
            mkdirSync(join(out, "by-hash"), { recursive: true });
            writeFileSync(join(out, "by-hash", `${hash}.js`), js);
            result.installed.push(name);
        } catch (e) {
            result.failed.push([name, String(e).split("\n")[0]!]);
        }
    }
    return result;
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    const args = process.argv.slice(2);
    const value = (flag: string) => {
        const i = args.indexOf(flag);
        return i >= 0 ? args[i + 1] : undefined;
    };
    const game = value("--game");
    if (!game) throw Error("Usage: node tools/qbjs/install_ts.ts --game <folder> [--only a.qb,b.qb] [--clear]");
    const only = (value("--only") ?? "").split(",").filter(Boolean);
    const result = installTS(game, only, args.includes("--clear"));
    for (const [name, why] of result.failed) console.log(`not translated: ${name}: ${why}`);
    console.log(`${result.installed.length} installed, ${result.failed.length} left to QB`);
}
