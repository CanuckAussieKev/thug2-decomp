// NOT ORIGINAL - a game folder's .qb scripts as the tools see them: every version of each file the game may load (Data's,
// an archive's, an overlay's - each named by its bytes' FNV-1a, as src/script_qb/ts_bridge.cpp hashes what it loaded),
// PartyMod's patched keyboard.qb, and where the game's Data is. Used by unpack_game.ts (the game's .ts), script_check.ts
// and the type tools.
import { existsSync, readdirSync, readFileSync, statSync } from "node:fs";
import { dirname, join, relative } from "node:path";
import { fileURLToPath } from "node:url";
import * as prx from "thug2-level-converter-core/level_port/prx.ts";
import { applyBps, bpsMadeFor } from "thug2-level-converter-core/lib/bps.ts";

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
