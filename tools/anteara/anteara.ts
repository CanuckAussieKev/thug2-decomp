// NOT ORIGINAL - the Anteara Mod (the user's own THUG2 script mod, 2010 + 2011 merged; README.md) as an optional part
// of the data overlay. Only the mod's own changes are stored here (mod\): its new files, and for each game script it
// changes just the scripts / globals it changes or adds (and a manifest of what it removes, its qdir.txt changes and
// a few line edits). The game's scripts come from the user's install each time.
//
// install() (called by tools/menu_level/make_menu_level.ts on every run) writes <overlay>\anteara\: the complete
// script set the mod plays with - every file its qdir.txt lists, from the game's loose scripts (Data\scripts, which
// the mod was made from) with the mod's changes applied, plus the data overlay's own edits re-applied to the files the
// overlay also changes (Levels.qb, the pause menu, the level select, the front end's roundbar menu...). The DLL reads
// it first while thug2-decomp.ini [features] anteara_mod=1 (src/game/front/data_overlay.h).
//
//   node tools/anteara/anteara.ts extract --unified <scripts_unified folder> [--game C:\Games\THUG2]
//       (re)makes mod\ from a merged mod folder (its src\*.ns and qdir.txt) and the game's loose scripts
//   node tools/anteara/anteara.ts check --unified <scripts_unified folder> --overlay <game>\thug2-decomp-data
//       compares an installed anteara\ set with the merged mod's .ns, names ignored (the overlay's edits listed)
//   node tools/anteara/anteara.ts install [--game C:\Games\THUG2] [--overlay <folder>]
//       the set alone, without the overlay's edits (make_menu_level.ts installs it with them)
//
// .ns text is Latin-1 here: some strings hold bytes >= 0x80 (the fonts' button glyphs) that NeverScript reads and
// writes byte for byte.

import { spawnSync } from "node:child_process";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import * as zlib from "node:zlib";
import { parseCli } from "../lib/cli.ts";
import { unifiedDiff } from "../lib/difflib.ts";
import {
    compareStr,
    encodeLatin1,
    isMain,
    print,
    pyExit,
    pyJoin,
    pyJsonDumps,
    pySplit,
    pyStrip,
    pyStrRepr,
    runMain,
    writeTextPy,
} from "../lib/py.ts";
import { search, sub } from "../lib/re.ts";

const USAGE =
    "usage: anteara.ts {extract,check,install} [--unified UNIFIED] [--game GAME] [--overlay OVERLAY] [--ns NS]";

const HERE = path.dirname(fileURLToPath(import.meta.url));
export const MOD = path.join(HERE, "mod");
export const MANIFEST = path.join(MOD, "manifest.json");
const REPO = path.dirname(path.dirname(HERE));
export const QDIR_PREFIX = "c:\\ports\\THUG2\\THUG2XB\\Skate6\\data\\scripts\\";

// Python's whitespace (re's \s and str.strip() on str: \x1c-\x1f and \x85 too, not \ufeff).
const WS = "[ \\t\\n\\r\\x0b\\x0c\\x1c-\\x1f\\x85\\xa0\\u1680\\u2000-\\u200a\\u2028\\u2029\\u202f\\u205f\\u3000]";

// --- Latin-1 text files as Python's open(..., encoding="latin-1") ---------------------------------------------------

// open(path, encoding="latin-1").read(): universal newlines.
function readLatin1(file: string): string {
    return fs.readFileSync(file).toString("latin1").replace(/\r\n?/g, "\n");
}

// open(path, "w", encoding="latin-1", newline="").write(text): no newline translation (raises past U+00FF).
function writeLatin1(file: string, text: string): void {
    fs.writeFileSync(file, encodeLatin1(text));
}

// ntpath.dirname / basename
function ntDirname(p: string): string {
    const at = Math.max(p.lastIndexOf("\\"), p.lastIndexOf("/"));
    if (at < 0) return /^[A-Za-z]:/.test(p) ? p.slice(0, 2) : "";
    let head = p.slice(0, at + 1);
    if (!/^[\\/]+$/.test(head) && !/^[A-Za-z]:[\\/]+$/.test(head)) head = head.replace(/[\\/]+$/, "");
    return head;
}

function ntBasename(p: string): string {
    const at = Math.max(p.lastIndexOf("\\"), p.lastIndexOf("/"));
    return at < 0 ? p.replace(/^[A-Za-z]:/, "") : p.slice(at + 1);
}

// --- .ns text as units (scripts and globals) ----------------------------------------------------------------------

const UNIT_START = /^(?:script (\S+?) ?\{|(`[^`\n]+`|[A-Za-z_#][A-Za-z0-9_]*) ?= ?)/;

export function crc(name: string): number {
    return (zlib.crc32(encodeLatin1(name.toLowerCase())) ^ 0xffffffff) >>> 0;
}

// A unit name as NeverScript shows its checksum (#xxxxxxxx: the checksum's bytes in file order).
export function keyOf(name: string): string {
    name = name.replace(/^`+|`+$/g, "");
    if (/^#[0-9a-fA-F]{8}$/.test(name)) return name.toLowerCase();
    const bytes = Buffer.alloc(4);
    bytes.writeUInt32LE(crc(name));
    return `#${bytes.toString("hex")}`;
}

// A unit: [kind ("script" / "global"), name, text].
export type Unit = [string, string, string];

// [head, units, register]: units = [kind, name, text], each unit's text with the comment lines right before it;
// register = the __register_checksums__ line (or "").
export function split(text: string): [string, Unit[], string] {
    let register = "";
    const m = search(/^__register_checksums__.*$/m, text);
    if (m) {
        register = m.str(0);
        text = text.slice(0, m.start()) + text.slice(m.end());
    }
    const lines = text.split("\n");
    const head: string[] = [];
    const units: [string, string, string[]][] = [];
    let pending: string[] = [];
    let current: [string, string, string[]] | null = null;
    for (const line of lines) {
        const start = UNIT_START.exec(line);
        if (line.startsWith("//")) {
            pending.push(line);
            continue;
        }
        if (start) {
            const kind = start[1] !== undefined ? "script" : "global";
            const name = start[1] !== undefined ? start[1] : start[2]!;
            current = [kind, name, [...pending, line]];
            units.push(current);
            pending = [];
            continue;
        }
        const target = current ? current[2] : head;
        target.push(...pending);
        pending = [];
        target.push(line);
    }
    if (pending.length) (current ? current[2] : head).push(...pending);
    return [head.join("\n"), units.map(([k, n, t]): Unit => [k, n, t.join("\n")]), register];
}

export function join(head: string, units: readonly Unit[], register: string): string {
    let body = head ? [head, ...units.map((u) => u[2])].join("\n") : units.map((u) => u[2]).join("\n");
    body = `${body.replace(/\n+$/, "")}\n`;
    return register ? body + register : body;
}

const IDENT = /"(?:[^"\\\n]|\\.)*"|#[0-9a-fA-F]{8}|`[^`\n]+`|[A-Za-z_][A-Za-z0-9_]*/;

// A unit's code for comparing: comments out, every name as its checksum, spacing collapsed.
export function norm(unitText: string): string {
    const lines = unitText.split("\n").filter((l) => !l.replace(new RegExp(`^${WS}+`), "").startsWith("//"));
    const text = lines.join("\n").replace(/\/\*[\s\S]*?\*\//g, "");
    const one = (t: string): string => {
        if (t.startsWith('"')) return t;
        if (t.startsWith("#")) return t.toLowerCase();
        return keyOf(t);
    };
    return pyStrip(sub(IDENT, (m) => one(m.str(0)), text).replace(new RegExp(`${WS}+`, "g"), " "));
}

// --- NeverScript ---------------------------------------------------------------------------------------------------

// subprocess.run(..., capture_output=True, text=True, errors="replace")'s text: the locale's code page, "\r\n" as "\n"
function textReplace(data: Buffer | null | undefined): string {
    if (!data) return "";
    const decoder = new TextDecoder("windows-1252");
    return decoder.decode(data).replace(/\r\n?/g, "\n");
}

export function ns(tool: string, ...args: string[]): void {
    const result = spawnSync(tool, args, { maxBuffer: 1 << 30 });
    if (result.error) throw result.error;
    if (result.status !== 0) {
        pyExit(`ns ${args.join(" ")} failed:\n${textReplace(result.stdout)}${textReplace(result.stderr)}`);
    }
}

export function decompile(tool: string, file: string, work: string): string {
    const out = path.join(work, `d_${crc(file).toString(16).padStart(8, "0")}.ns`);
    const result = spawnSync(tool, ["-d", file, "-o", out], { maxBuffer: 1 << 30 });
    if (result.error) throw result.error;
    if (result.status !== 0) ns(tool, "-lenient", "-d", file, "-o", out); // (a few retail files end in 0x00 padding)
    return readLatin1(out);
}

export function compileTo(tool: string, text: string, target: string, work: string): void {
    const src = path.join(work, `c_${crc(target).toString(16).padStart(8, "0")}.ns`);
    writeLatin1(src, text);
    fs.mkdirSync(path.dirname(target), { recursive: true });
    ns(tool, "-c", src, "-o", target, "-targetGame", "thug2");
}

// --- qdir.txt ------------------------------------------------------------------------------------------------------

export function qdirFiles(text: string): string[] {
    const out: string[] = [];
    for (const line of text.split("\n")) {
        const m = search(/\\scripts\\(.+\.qb)/i, pyStrip(line));
        if (m) out.push(m.str(1));
    }
    return out;
}

// [file, the file it goes after ("" for the first)]
type After = [string, string];

export interface ModManifest {
    qdir: { remove: string[]; insert_after: After[]; move_after?: After[] };
    files: Record<string, FileEntry>;
}

export interface FileEntry {
    new?: boolean;
    path?: string;
    remove?: string[];
    insert_after?: Record<string, string>;
    edits?: { old: string; new: string }[];
}

function indexLower(files: readonly string[], name: string): number {
    const at = files.map((f) => f.toLowerCase()).indexOf(name.toLowerCase());
    if (at < 0) throw new Error(`${pyStrRepr(name.toLowerCase())} is not in list`);
    return at;
}

export function modQdir(gameQdir: readonly string[], manifest: ModManifest): string[] {
    const removed = new Set(manifest.qdir.remove.map((r) => r.toLowerCase()));
    let files = gameQdir.filter((f) => !removed.has(f.toLowerCase()));
    for (const [rel, after] of manifest.qdir.insert_after) {
        const at = after ? indexLower(files, after) + 1 : 0;
        files.splice(at, 0, rel);
    }
    for (const [rel, after] of manifest.qdir.move_after ?? []) {
        // (files the mod loads in another order)
        files = files.filter((f) => f.toLowerCase() !== rel.toLowerCase());
        files.splice(after ? indexLower(files, after) + 1 : 0, 0, rel);
    }
    return files;
}

// --- applying the mod to the game's text -----------------------------------------------------------------------------

// str.replace(old, new): every occurrence, taken literally.
function replaceAllLiteral(text: string, old: string, replacement: string): string {
    return text.split(old).join(replacement);
}

// The game's file text with the mod's units in place: changed ones replaced where they are, new ones after the unit
// the manifest names, removed ones dropped, then the manifest's line edits.
export function applyPatch(base: string, patchText: string, entry: FileEntry, rel: string): string {
    const [head, splitUnits, register] = split(base);
    let units = splitUnits;
    const indexOf = (): Map<string, number> => new Map(units.map(([, n], i) => [keyOf(n), i]));
    let index = indexOf();
    const remove = new Set((entry.remove ?? []).map(keyOf));
    const after = new Map(Object.entries(entry.insert_after ?? {}).map(([k, v]) => [keyOf(k), v]));
    if (patchText) {
        const [, patchUnits] = split(patchText);
        for (const [kind, name, text] of patchUnits) {
            const k = keyOf(name);
            const at = index.get(k);
            if (at !== undefined) {
                units[at] = [kind, name, text];
                continue;
            }
            const prev = after.get(k);
            if (prev === undefined) {
                units.push([kind, name, text]);
            } else {
                let to = 0;
                if (prev !== "") {
                    const found = units.findIndex((u) => keyOf(u[1]) === keyOf(prev));
                    if (found < 0) throw new Error("StopIteration");
                    to = found + 1;
                }
                units.splice(to, 0, [kind, name, text]);
            }
            index = indexOf();
        }
    }
    units = units.filter((u) => !remove.has(keyOf(u[1])));
    let text = join(head, units, register);
    for (const edit of entry.edits ?? []) {
        const n = text.split(edit.old).length - 1;
        if (n !== 1) {
            pyExit(`anteara: ${rel}: the line edit's text ${pyStrRepr(edit.old.slice(0, 60))} found ${n} times`);
        }
        text = replaceAllLiteral(text, edit.old, edit.new);
    }
    // names the mod adds, for decompilers (the checksum table)
    if (patchText && register) {
        const have = new Set(pySplit(register).map((w) => w.toLowerCase()));
        const names = new Set<string>();
        for (const m of patchText.matchAll(/^script ([A-Za-z_][A-Za-z0-9_]*)/gm)) names.add(m[1]!);
        for (const m of patchText.matchAll(/^([A-Za-z_][A-Za-z0-9_]*) =/gm)) names.add(m[1]!);
        const extra = [...names].sort(compareStr).filter((w) => !have.has(w.toLowerCase()));
        if (extra.length) text = replaceAllLiteral(text, register, `${register} ${extra.join(" ")}`);
    }
    return text;
}

// The data overlay's own changes to a file's text.
export type Edit = (text: string) => string;

function isDir(p: string): boolean {
    try {
        return fs.statSync(p).isDirectory();
    } catch {
        return false;
    }
}

function tempDir(): string {
    return fs.mkdtempSync(path.join(os.tmpdir(), "tmp"));
}

// Writes <out>\anteara\: every file of the mod's qdir.txt (scripts\...), its qdir.txt and localtext.qb.
// edits / frontEndEdits: {"scripts\\game\\levels.qb" (lower case): function(text) -> text}, the data overlay's own
// changes to re-apply - edits for anteara\, frontEndEdits for anteara\front_end\ (the skate-shop front end's
// versions). Returns a description, or null when the game has no loose scripts to build on.
export function install(
    game: string,
    out: string,
    tool: string,
    edits: ReadonlyMap<string, Edit> = new Map(),
    frontEndEdits: ReadonlyMap<string, Edit> = new Map(),
): string | null {
    const ownEdits = new Map([...edits].map(([k, v]) => [k.toLowerCase(), v]));
    const ownFrontEndEdits = new Map([...frontEndEdits].map(([k, v]) => [k.toLowerCase(), v]));
    const loose = pyJoin(game, "Data", "scripts");
    const target = pyJoin(out, "anteara");
    if (isDir(target)) fs.rmSync(target, { recursive: true, force: true });
    if (!fs.existsSync(path.join(loose, "qdir.txt"))) return null;
    const manifest = JSON.parse(fs.readFileSync(MANIFEST, "utf-8")) as ModManifest;
    const files = new Map(Object.entries(manifest.files).map(([k, v]) => [k.toLowerCase(), v]));
    const gameQdir = qdirFiles(readLatin1(path.join(loose, "qdir.txt")));
    const qdir = modQdir(gameQdir, manifest);
    const made = { patched: 0, new: 0, copied: 0, overlay: 0 };
    const work = tempDir();
    try {
        for (const rel of [...qdir, "localtext.qb"]) {
            const key = `scripts\\${rel.toLowerCase()}`;
            const entry = files.get(rel.toLowerCase());
            const dst = path.join(target, "scripts", rel);
            let text: string;
            if (entry?.new) {
                text = readLatin1(path.join(MOD, "new", entry.path!));
                made.new += 1;
            } else {
                const src = path.join(loose, rel);
                if (!fs.existsSync(src)) pyExit(`anteara: ${rel} missing from ${loose}`);
                if (!entry && !ownEdits.has(key) && !ownFrontEndEdits.has(key)) {
                    fs.mkdirSync(path.dirname(dst), { recursive: true });
                    fs.copyFileSync(src, dst);
                    made.copied += 1;
                    continue;
                }
                text = decompile(tool, src, work);
                if (entry) {
                    const patch = entry.path ? path.join(MOD, "patch", entry.path) : null;
                    text = applyPatch(text, patch ? readLatin1(patch) : "", entry, rel);
                    made.patched += 1;
                }
            }
            const frontEnd = ownFrontEndEdits.get(key);
            if (frontEnd) compileTo(tool, frontEnd(text), path.join(target, "front_end", "scripts", rel), work);
            const edit = ownEdits.get(key);
            if (edit) {
                text = edit(text);
                made.overlay += 1;
            }
            compileTo(tool, text, dst, work);
        }
        writeLatin1(path.join(target, "scripts", "qdir.txt"), qdir.map((rel) => `${QDIR_PREFIX}${rel}\r\n`).join(""));
    } finally {
        fs.rmSync(work, { recursive: true, force: true });
    }
    return (
        `Anteara Mod: ${qdir.length} files in its qdir.txt + localtext.qb -> ${target} (${made.patched} with the mod's ` +
        `changes, ${made.new} its own, ${made.copied} copied from Data\\scripts, ${made.overlay} with the overlay's ` +
        "edits too)"
    );
}

// --- extract: the mod's changes from a merged mod folder --------------------------------------------------------------

// For a huge global the mod changes in a line or two (localtext's Translations): line edits instead of the unit.
// null if it isn't that.
export function lineEdits(baseUnit: string, modUnit: string): [string, string][] | null {
    const a = baseUnit.split("\n");
    const b = modUnit.split("\n");
    if (a.length < 500 || a.length !== b.length) return null;
    const diff: [string, string][] = [];
    a.forEach((x, i) => {
        if (x !== b[i]) diff.push([x, b[i]!]);
    });
    if (!diff.length || diff.length > 8) return null;
    return diff;
}

export function extract(unified: string, game: string, tool: string): void {
    const srcRoot = pyJoin(unified, "src");
    const loose = pyJoin(game, "Data", "scripts");
    let unifiedQdir = qdirFiles(readLatin1(path.join(unified, "qdir.txt")));
    const gameQdir = qdirFiles(readLatin1(path.join(loose, "qdir.txt")));
    // one spelling per folder (the merged qdir has game\menu\Anteara_Stuff and game\menu\anteara_stuff)
    const spelling = new Map<string, string>();
    for (const f of unifiedQdir) {
        const folder = ntDirname(f);
        if (!spelling.has(folder.toLowerCase())) spelling.set(folder.toLowerCase(), folder);
    }
    unifiedQdir = unifiedQdir.map((f) =>
        ntDirname(f) ? pyJoin(spelling.get(ntDirname(f).toLowerCase())!, ntBasename(f)) : f,
    );
    const lowerGame = gameQdir.map((f) => f.toLowerCase());
    const lowerMod = unifiedQdir.map((f) => f.toLowerCase());
    const manifest: ModManifest = {
        qdir: { remove: gameQdir.filter((f) => !lowerMod.includes(f.toLowerCase())), insert_after: [] },
        files: {},
    };
    unifiedQdir.forEach((f, i) => {
        if (!lowerGame.includes(f.toLowerCase())) manifest.qdir.insert_after.push([f, i ? unifiedQdir[i - 1]! : ""]);
    });
    let current = modQdir(gameQdir, manifest);
    manifest.qdir.move_after = [];
    unifiedQdir.forEach((f, i) => {
        if (current[i]?.toLowerCase() !== f.toLowerCase()) {
            const after = i ? unifiedQdir[i - 1]! : "";
            manifest.qdir.move_after!.push([f, after]);
            current = modQdir(gameQdir, manifest);
        }
    });
    if (current.map((f) => f.toLowerCase()).join("\n") !== unifiedQdir.map((f) => f.toLowerCase()).join("\n")) {
        throw new Error("AssertionError");
    }
    for (const folder of ["new", "patch"]) {
        if (isDir(path.join(MOD, folder))) fs.rmSync(path.join(MOD, folder), { recursive: true, force: true });
    }
    const report: string[] = [];
    const work = tempDir();
    try {
        for (const rel of [...unifiedQdir, "localtext.qb"]) {
            const modText = readLatin1(path.join(srcRoot, `${rel}.ns`));
            const basePath = path.join(loose, rel);
            if (!fs.existsSync(basePath)) {
                const file = `${rel.replaceAll("\\", "/")}.ns`;
                const dst = path.join(MOD, "new", file);
                fs.mkdirSync(path.dirname(dst), { recursive: true });
                writeLatin1(dst, modText);
                manifest.files[rel] = { new: true, path: file };
                report.push(`new      ${rel}`);
                continue;
            }
            const [, baseUnits] = split(decompile(tool, basePath, work));
            const [, modUnits] = split(modText);
            const baseBy = new Map(baseUnits.map((u) => [keyOf(u[1]), u]));
            const modKeys = new Set(modUnits.map((u) => keyOf(u[1])));
            const entry: FileEntry = {};
            const outUnits: string[] = [];
            const insertAfter: Record<string, string> = {};
            let prev = "";
            for (const [, name, text] of modUnits) {
                const k = keyOf(name);
                const base = baseBy.get(k);
                if (base !== undefined) {
                    if (norm(base[2]) !== norm(text)) {
                        const edits = lineEdits(base[2], text);
                        if (edits) {
                            entry.edits ??= [];
                            entry.edits.push(...edits.map(([x, y]) => ({ old: `${x}\n`, new: `${y}\n` })));
                        } else {
                            outUnits.push(text);
                        }
                    }
                } else {
                    outUnits.push(text);
                    insertAfter[name] = prev;
                }
                prev = name;
            }
            const removed = baseUnits.filter((u) => !modKeys.has(keyOf(u[1]))).map((u) => u[1]);
            if (removed.length) entry.remove = removed;
            if (Object.keys(insertAfter).length) entry.insert_after = insertAfter;
            if (outUnits.length) {
                const file = `${rel.replaceAll("\\", "/")}.ns`;
                const dst = path.join(MOD, "patch", file);
                fs.mkdirSync(path.dirname(dst), { recursive: true });
                writeLatin1(dst, `${outUnits.join("\n").replace(/\n+$/, "")}\n`);
                entry.path = file;
            }
            if (Object.keys(entry).length) {
                manifest.files[rel] = entry;
                report.push(
                    `patched  ${rel.padEnd(48)} ${outUnits.length} units, ${removed.length} removed, ${(entry.edits ?? []).length} line edits`,
                );
            }
        }
    } finally {
        fs.rmSync(work, { recursive: true, force: true });
    }
    writeTextPy(MANIFEST, pyJsonDumps(manifest, 1));
    print(report.join("\n"));
    const all = Object.values(manifest.files);
    print(`${all.length} files: ${all.filter((e) => e.new).length} new, ${all.filter((e) => !e.new).length} patched`);
}

// --- check: an installed set against the merged mod ------------------------------------------------------------------

// A file's code with every name as its checksum (compiled with -removeChecksums, decompiled again).
function codeLines(tool: string, file: string, work: string, fromNs = false): string {
    let src = file;
    if (!fromNs) {
        src = path.join(work, `k_${crc(file).toString(16).padStart(8, "0")}.ns`);
        const text = decompile(tool, file, work);
        writeLatin1(src, text);
    }
    const q = path.join(work, `k_${crc(`${file}q`).toString(16).padStart(8, "0")}.qb`);
    ns(tool, "-c", src, "-o", q, "-removeChecksums");
    const d = path.join(work, `k_${crc(`${file}d`).toString(16).padStart(8, "0")}.d.ns`);
    ns(tool, "-d", q, "-o", d);
    return readLatin1(d);
}

export function check(unified: string, overlay: string, tool: string): void {
    const target = pyJoin(overlay, "anteara", "scripts");
    const unifiedQdir = qdirFiles(readLatin1(path.join(unified, "qdir.txt")));
    const installedQdir = qdirFiles(readLatin1(path.join(target, "qdir.txt")));
    const sameQdir =
        unifiedQdir.map((f) => f.toLowerCase()).join("\n") === installedQdir.map((f) => f.toLowerCase()).join("\n") &&
        unifiedQdir.length === installedQdir.length;
    print(`qdir.txt: ${sameQdir ? "same files, same order" : "DIFFERS"}`);
    let same = 0;
    let unitSame = 0;
    const differ: [string, string[]][] = [];
    const work = tempDir();
    try {
        for (const rel of [...unifiedQdir, "localtext.qb"]) {
            const a = codeLines(tool, path.join(unified, "src", `${rel}.ns`), work, true);
            const b = codeLines(tool, path.join(target, rel), work);
            if (a === b) {
                same += 1;
                continue;
            }
            // same units in another order?
            const ua = split(a)[1]
                .map((u) => norm(u[2]))
                .sort(compareStr);
            const ub = split(b)[1]
                .map((u) => norm(u[2]))
                .sort(compareStr);
            if (ua.length === ub.length && ua.every((x, i) => x === ub[i])) {
                unitSame += 1;
                continue;
            }
            const d = [...unifiedDiff(a.split("\n"), b.split("\n"), "merged", "installed", "", "", 0, "")];
            differ.push([rel, d]);
        }
    } finally {
        fs.rmSync(work, { recursive: true, force: true });
    }
    print(`${same} files the same code, ${unitSame} the same units in another order, ${differ.length} differ:`);
    for (const [rel, d] of differ) {
        print(`--- ${rel} (${d.length} diff lines)`);
        // (Latin-1 bytes, '?' for what isn't, as Python's .encode("latin-1", "replace"))
        const text = `${d.slice(0, 40).join("\n")}\n`.replace(/[^\x00-\xff]/gu, "?");
        process.stdout.write(Buffer.from(text, "latin1"));
    }
}

async function main(): Promise<number> {
    const { values, positionals } = parseCli(USAGE, {
        options: {
            unified: { type: "string" },
            game: { type: "string", default: "C:\\Games\\THUG2\\Game" },
            overlay: { type: "string" },
            ns: { type: "string", default: path.join(REPO, "out", "neverscript", "ns.exe") },
        },
        allowPositionals: true,
    });
    const action = positionals[0];
    if (positionals.length !== 1 || !["extract", "check", "install"].includes(action!)) {
        process.stderr.write(
            "usage: anteara.ts {extract,check,install} [--unified UNIFIED] [--game GAME] [--overlay OVERLAY] [--ns NS]\n",
        );
        return 2;
    }
    let game = values.game;
    // (a stock install keeps the game in a Game subfolder: C:\Games\THUG2 -> C:\Games\THUG2\Game)
    if (!isDir(path.join(game, "Data")) && isDir(path.join(game, "Game", "Data"))) game = pyJoin(game, "Game");
    const need = (value: string | undefined): string => {
        if (value === undefined) throw new TypeError("expected str, bytes or os.PathLike object, not NoneType");
        return value;
    };
    if (action === "extract") {
        extract(need(values.unified), game, values.ns);
    } else if (action === "check") {
        check(need(values.unified), values.overlay ?? pyJoin(game, "thug2-decomp-data"), values.ns);
    } else {
        // (without the overlay's edits - make_menu_level.ts installs it with them)
        print(install(game, values.overlay ?? pyJoin(game, "thug2-decomp-data"), values.ns) ?? "None");
    }
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
