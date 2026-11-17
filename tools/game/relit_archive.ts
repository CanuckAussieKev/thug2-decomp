// NOT ORIGINAL - the relit bake's archive (tools/game/relit_bake.ts): a bake is deterministic - the same level files and
// the same relit data version (src/engine/gfx/relit_data.h kRelitDataVersion) make the same .relit - so each bake's
// results are kept as a versioned set, and a level whose inputs are unchanged is restored instead of baked again.
//
//   <game>\thug2-decomp-relit\archive\v<version>\<level>-<inputs>.relit   the data file (<scene>.relit as the game wrote it)
//   <game>\thug2-decomp-relit\archive\v<version>\<level>-<inputs>.json    its scene, report line, state row, when
//   <game>\thug2-decomp-relit\archive\v<version>\<level>-<inputs>.log     the bake's game log
//
// Beside the data, in the game's own generated folder (never under Data, which is the user's): it goes with the folder
// the bake already writes, and a fresh install copies that folder.
//
// The inputs' identity: a hash over the level's own files - every file under levels\<level>\ in Data and in the overlay
// (thug2-decomp-data: the new front end's / ported levels' copies), by relative name, size and content. The bake reads the
// level's world from those; the data version covers the bake's own code. What the hash doesn't see: the game's shared
// files (textures shared by levels, scripts outside the level's folder) and the graphics settings the bake takes from
// the build - a change of those is a reason for --rebake (or a data version bump, as for the geometry settings in
// relit_data.h).

import { createHash } from "node:crypto";
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";

export const ARCHIVE_FOLDER = "archive";
// The data file's start: the magic "T2RD" and the version (relit_data.h, little-endian words).
const MAGIC = 0x44523254;

export interface ArchiveEntry {
    level: string;
    inputs: string; // the inputs' hash (16 hex)
    version: number;
    scene: string; // the data file's name without .relit (8 hex)
    line: string; // the level's bake-report line
    state: string | null; // its state-table row
    bakedAt: string; // ISO time
}

// The data version this checkout's game is built with: kRelitDataVersion in the source (the tool is run from the checkout
// the exe was built from; `--data-version` overrides when it isn't).
export function currentDataVersion(
    header = path.join(path.dirname(fileURLToPath(import.meta.url)), "../../src/engine/gfx/relit_data.h"),
): number | null {
    if (!fs.existsSync(header)) return null;
    const m = /kRelitDataVersion\s*=\s*(\d+)/.exec(fs.readFileSync(header, "utf8"));
    return m ? Number(m[1]) : null;
}

// The version a data file's head carries (null: not a relit data file).
export function fileVersion(bytes: Uint8Array): number | null {
    if (bytes.length < 8) return null;
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    return view.getUint32(0, true) === MAGIC ? view.getUint32(4, true) : null;
}

function filesUnder(dir: string, base = ""): string[] {
    if (!fs.existsSync(dir)) return [];
    const out: string[] = [];
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
        const rel = base ? `${base}/${entry.name}` : entry.name;
        if (entry.isDirectory()) out.push(...filesUnder(path.join(dir, entry.name), rel));
        else out.push(rel);
    }
    return out;
}

// The identity of what a level's bake reads: `roots` are the folders holding levels\<level>\ (Data, the overlay).
export function inputsHash(roots: readonly string[], level: string): string {
    const hash = createHash("sha256");
    const key = level.toLowerCase();
    roots.forEach((root, index) => {
        const dir = path.join(root, "levels", key);
        for (const rel of filesUnder(dir).sort((a, b) => (a.toLowerCase() < b.toLowerCase() ? -1 : 1))) {
            const file = path.join(dir, rel);
            hash.update(`${index}|${rel.toLowerCase()}|${fs.statSync(file).size}|`);
            hash.update(fs.readFileSync(file));
            hash.update("\n");
        }
    });
    return hash.digest("hex").slice(0, 16);
}

export function versionDir(folder: string, version: number): string {
    return path.join(folder, ARCHIVE_FOLDER, `v${version}`);
}

function stem(level: string, inputs: string): string {
    return `${level.toLowerCase()}-${inputs}`;
}

// Keep a level's finished bake: its data file (read from `dataFile`), report line, state row and log.
export function archiveBake(
    folder: string,
    version: number,
    entry: Omit<ArchiveEntry, "version" | "bakedAt">,
    dataFile: string,
    log: string,
): void {
    const dir = versionDir(folder, version);
    fs.mkdirSync(dir, { recursive: true });
    const base = path.join(dir, stem(entry.level, entry.inputs));
    fs.copyFileSync(dataFile, `${base}.relit`);
    fs.writeFileSync(`${base}.log`, log);
    const full: ArchiveEntry = { ...entry, version, bakedAt: new Date().toISOString() };
    fs.writeFileSync(`${base}.json`, `${JSON.stringify(full, null, 2)}\n`);
}

function readEntry(jsonFile: string): ArchiveEntry | null {
    try {
        const entry = JSON.parse(fs.readFileSync(jsonFile, "utf8")) as ArchiveEntry;
        return typeof entry.level === "string" && typeof entry.scene === "string" ? entry : null;
    } catch {
        return null;
    }
}

// The archived entry for this level, version and inputs - complete (data file of that version) - or null.
export function findArchived(folder: string, version: number, level: string, inputs: string): ArchiveEntry | null {
    const base = path.join(versionDir(folder, version), stem(level, inputs));
    const entry = readEntry(`${base}.json`);
    if (!entry || entry.version !== version || entry.inputs !== inputs) return null;
    if (!fs.existsSync(`${base}.relit`)) return null;
    if (fileVersion(fs.readFileSync(`${base}.relit`)) !== version) return null;
    return entry;
}

// Put an archived level's data back as the live file (<scene>.relit) and its log.
export function restoreEntry(folder: string, entry: ArchiveEntry): void {
    const base = path.join(versionDir(folder, entry.version), stem(entry.level, entry.inputs));
    fs.copyFileSync(`${base}.relit`, path.join(folder, `${entry.scene}.relit`));
    if (fs.existsSync(`${base}.log`)) {
        fs.mkdirSync(path.join(folder, "logs"), { recursive: true });
        fs.copyFileSync(`${base}.log`, path.join(folder, "logs", `${entry.level}.log`));
    }
}

export function archivedVersions(folder: string): number[] {
    const root = path.join(folder, ARCHIVE_FOLDER);
    if (!fs.existsSync(root)) return [];
    return fs
        .readdirSync(root, { withFileTypes: true })
        .filter((e) => e.isDirectory() && /^v\d+$/.test(e.name))
        .map((e) => Number(e.name.slice(1)))
        .sort((a, b) => a - b);
}

// A version's newest entry per level (a level baked again after its files changed has several).
export function entriesOfVersion(folder: string, version: number, levels?: readonly string[]): ArchiveEntry[] {
    const dir = versionDir(folder, version);
    if (!fs.existsSync(dir)) return [];
    const newest = new Map<string, ArchiveEntry>();
    const wanted = levels?.map((l) => l.toLowerCase());
    for (const name of fs.readdirSync(dir).filter((n) => n.endsWith(".json"))) {
        const entry = readEntry(path.join(dir, name));
        if (!entry || (wanted && !wanted.includes(entry.level.toLowerCase()))) continue;
        if (!fs.existsSync(path.join(dir, `${stem(entry.level, entry.inputs)}.relit`))) continue;
        const have = newest.get(entry.level.toLowerCase());
        if (!have || have.bakedAt < entry.bakedAt) newest.set(entry.level.toLowerCase(), entry);
    }
    return [...newest.values()].sort((a, b) => a.level.localeCompare(b.level));
}

export interface RestoreOutcome {
    restored: ArchiveEntry[];
    refused: string | null;
}

// `restore --version N`: the version's newest set of levels back as the live data (and bake-report.txt / state-table.txt
// made from the entries, when all levels are restored). Refused when the game's own data version differs - it would call
// those files out of date.
export function restoreVersion(
    folder: string,
    version: number,
    gameVersion: number | null,
    stateHeader: string,
    levels?: readonly string[],
): RestoreOutcome {
    if (gameVersion === null)
        return { restored: [], refused: "this build's relit data version is unknown (pass --data-version N)" };
    if (version !== gameVersion)
        return {
            restored: [],
            refused: `the game reads relit data version ${gameVersion}; version ${version}'s files would be reported out of date in the game - nothing restored`,
        };
    const entries = entriesOfVersion(folder, version, levels);
    if (!entries.length) return { restored: [], refused: `no archived data for version ${version}` };
    for (const entry of entries) restoreEntry(folder, entry);
    if (!levels) {
        fs.writeFileSync(path.join(folder, "bake-report.txt"), `${entries.map((e) => e.line).join("\n")}\n`);
        const rows = entries.flatMap((e) => (e.state ? [e.state] : []));
        fs.writeFileSync(path.join(folder, "state-table.txt"), `${[stateHeader, ...rows].join("\n")}\n`);
    }
    return { restored: entries, refused: null };
}

// Old versions out: the newest `keep` versions stay (the game's own version always does). Returns the versions removed.
export function pruneVersions(folder: string, keep: number, gameVersion: number | null): number[] {
    const versions = archivedVersions(folder);
    const kept = new Set(versions.slice(Math.max(0, versions.length - keep)));
    if (gameVersion !== null) kept.add(gameVersion);
    const removed: number[] = [];
    for (const version of versions) {
        if (kept.has(version)) continue;
        fs.rmSync(versionDir(folder, version), { recursive: true, force: true });
        removed.push(version);
    }
    return removed;
}
