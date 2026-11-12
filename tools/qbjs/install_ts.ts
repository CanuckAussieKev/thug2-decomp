// NOT ORIGINAL - translates a game folder's .qb scripts to TypeScript and installs them where the game looks for them
// (docs/qb-ts.md "In the game"). A module is named by its .qb file's bytes (FNV-1a, as src/script_qb/ts_bridge.cpp
// hashes what it loaded): <game>\thug2-decomp-ts\by-hash\<hash>.js - so it is only ever used for the very file it was
// translated from (the stock file, an overlay's or a mod's version: each its own). The readable TypeScript is kept
// beside, in source\<folder>\<path>.ts; the module is its scripts compiled (its globals are the game's own, read from
// the same file). The game uses them with [features] ts_scripts=1 (ts_scripts=2, the game without .qb files: unpack_game.ts).
//
//   node tools/qbjs/install_ts.ts --game <folder> [--only <path>[,...]] [--clear] [--check]
//
// Sources: every .qb under Data\scripts, Data\levels and the overlay folders thug2-decomp-* (but -ts); --only: just
// these (paths under the game folder, e.g. Data\scripts\game\misc\level_flags.qb). --clear: the folder emptied first.
// Files that don't translate are listed and stay QB.
import { existsSync, mkdirSync, readdirSync, readFileSync, rmSync, statSync, writeFileSync } from "node:fs";
import { dirname, join, relative } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import * as prx from "thug2-level-converter-core/level_port/prx.ts";
import { applyBps, bpsMadeFor } from "thug2-level-converter-core/lib/bps.ts";
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

// One version of a .qb file the game may load: its game path (lower case, "levels\\x\\x.qb"), where it comes from
// (a data overlay's folder, a Data\\pre archive's file name, or "Data") and its bytes. The game looks in the data
// overlay first, then in the archives it has loaded, then in Data's loose files - which version runs depends on the
// archives loaded at the time, so every version gets its module (each named by its bytes).
export type QbVersion = { name: string; origin: string; bytes: Buffer; path?: string };

export function overlayFolders(root: string): string[] {
    return readdirSync(root).filter(
        (name) =>
            name.toLowerCase().startsWith("thug2-decomp-") &&
            name.toLowerCase() !== "thug2-decomp-ts" &&
            !name.toLowerCase().startsWith("thug2-decomp-check"), // (the script check's plan and sources)
    );
}

export function qbVersions(root: string): QbVersion[] {
    const out: QbVersion[] = [];
    const data = join(root, "Data");
    for (const file of walk(data)) {
        const name = relative(data, file).replace(/\//g, "\\").toLowerCase();
        if (!name.startsWith("pre\\")) out.push({ name, origin: "Data", bytes: readFileSync(file), path: file });
    }
    const pre = join(data, "pre");
    if (existsSync(pre))
        for (const archive of readdirSync(pre).sort()) {
            if (!archive.toLowerCase().endsWith(".prx")) continue;
            for (const [entry, bytes] of prx.entries(join(pre, archive), (n) => n.toLowerCase().endsWith(".qb")))
                out.push({ name: entry.replace(/\//g, "\\").toLowerCase(), origin: archive, bytes });
        }
    for (const overlay of overlayFolders(root))
        for (const file of walk(join(root, overlay)))
            out.push({
                name: relative(join(root, overlay), file).replace(/\//g, "\\").toLowerCase(),
                origin: overlay,
                bytes: readFileSync(file),
                path: file,
            });
    // (the files the game loads with a binary patch applied - src/engine/script/patched.cpp: the patched bytes are what
    // it runs, a version of their own, "<origin>+partymod"; a copy the patch wasn't made for is loaded as it is)
    for (const v of [...out]) {
        const patch = SCRIPT_PATCHES[v.name];
        if (!patch || !existsSync(patch)) continue;
        const bytes = readFileSync(patch);
        if (bpsMadeFor(bytes, v.bytes))
            out.push({ name: v.name, origin: `${v.origin}${PATCHED}`, bytes: applyBps(bytes, v.bytes) });
    }
    return out;
}

// The patched scripts (patched.cpp's register_patch) and their patches: PartyMod's patches/keyboard.bps, kept by the
// pipeline (tools/pipeline/menu_level/partymod_keyboard.bps).
const PATCHED = "+partymod";
const SCRIPT_PATCHES: Record<string, string> = {
    "scripts\\engine\\menu\\keyboard.qb": join(
        dirname(fileURLToPath(import.meta.url)),
        "..",
        "pipeline",
        "menu_level",
        "partymod_keyboard.bps",
    ),
};

// The bytes the game runs for game file `name` read as `bytes`: with PartyMod's binary patch applied when it is the file
// the patch was made for (src/engine/script/patched.cpp), else as they are.
export function scriptBytes(name: string, bytes: Buffer): Buffer {
    const patch = SCRIPT_PATCHES[name.replace(/\//g, "\\").toLowerCase()];
    if (!patch || !existsSync(patch)) return bytes;
    const made = readFileSync(patch);
    return bpsMadeFor(made, bytes) ? applyBps(made, bytes) : bytes;
}

// A version's origin without its patch mark (the folder or archive it was read from).
export const unpatchedOrigin = (origin: string): string =>
    origin.endsWith(PATCHED) ? origin.slice(0, -PATCHED.length) : origin;

// check: the script check's translation (emit_ts.ts) - for its runs only, never the game's.
export function installTS(game: string, only: string[] = [], clear = false, check = false): InstallResult {
    const root = gameRoot(game);
    const out = join(root, "thug2-decomp-ts");
    if (clear) rmSync(out, { recursive: true, force: true });
    const wanted = only.map((path) =>
        path
            .replace(/[\\/]+/g, "\\")
            .toLowerCase()
            .replace(/^data\\/, ""),
    );
    const versions = qbVersions(root).filter((v) => !wanted.length || wanted.includes(v.name));
    const result: InstallResult = { installed: [], failed: [] };
    const seen = new Set<string>();
    for (const version of versions) {
        // (its readable source beside: under the folder it came from - Data, an overlay, or pre\\<archive>)
        const origin =
            version.origin === "Data"
                ? "Data"
                : unpatchedOrigin(version.origin).toLowerCase().endsWith(".prx")
                  ? `pre\\${version.origin}`
                  : version.origin;
        const name = `${origin}\\${version.name}`;
        try {
            const bytes = version.bytes;
            const hash = fileHash(bytes);
            if (seen.has(hash)) continue;
            seen.add(hash);
            const ir = readIR(bytes, name);
            const ts = emitTS(ir, {}, { check });
            // The game's module has only the scripts: the globals stay the ones the game made from these very bytes
            // (read_qb_file, just before) - making them again would only cost (a ported level's node arrays run to
            // megabytes), and a global drawing a random number would draw twice.
            const js = compileTS(
                emitTS({ ...ir, definitions: ir.definitions.filter((d) => d.kind === "script") }, {}, { check }),
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
    if (!game)
        throw Error("Usage: node tools/qbjs/install_ts.ts --game <folder> [--only a.qb,b.qb] [--clear] [--check]");
    const only = (value("--only") ?? "").split(",").filter(Boolean);
    const result = installTS(game, only, args.includes("--clear"), args.includes("--check"));
    for (const [name, why] of result.failed) console.log(`not translated: ${name}: ${why}`);
    console.log(`${result.installed.length} installed, ${result.failed.length} left to QB`);
}
