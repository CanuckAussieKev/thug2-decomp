// NOT ORIGINAL - is a workspace (C:\Games\THUG2-n) the main install's game? The content a look depends on (the game's
// Data with its building relief and surface-map files, the data overlays, the texture sets, the material library, the
// relit data, the TypeScript scripts, the DLLs beside the exe) and the settings, compared file by file - and brought
// to the main install's, without make_workspace.ps1's full mirror (which needs the lock free and drops the
// workspace's logs).
//
//   node tools/game/workspace_parity.ts check --workspace C:\Games\THUG2-4     what differs (exit 1 if any content does)
//   node tools/game/workspace_parity.ts sync --workspace C:\Games\THUG2-4 [--owner <lock owner>]
//        [--settings main|<screenshot sidecar .txt>|none] [--dry-run]
//
// sync copies what's missing or different from the main install (read only: nothing is written there) and removes
// what only the workspace has in those places (its own relit archive and bake logs stay - the game doesn't read them),
// then writes the settings: `main` (the default) - the main install's ini as it is, `<sidecar>` - the settings a user's
// screenshot was taken with (its "Graphics / features / debug <key>: <value>" lines: "(default)" ones taken out of the
// ini, the rest set), `none` - the ini left alone. Either way the workspace keeps its own window
// ([Graphics] ResolutionX / ResolutionY / Borderless / WindowDisplay) and test keys ([features] automation, mute_audio,
// allow_multiple_instances, start_level / start_at / edit_level). Under the game lock (--owner its holder) the ini
// goes back to the lock's copy at release - the content stays; with the lock free the ini change stays too.
// Refuses while a game runs from the workspace, and when the copy wouldn't fit on the disk.

import * as fs from "node:fs";
import * as path from "node:path";
import { exitWith, isMain, parseCli, print, runMain } from "thug2-level-converter-core/lib/cli.ts";
import { resolveGameFolder, runningGames } from "./edit_level.ts";
import { ensureWorkspaceMuted, isWorkspace, setIniValue } from "./workspace_ini.ts";

const USAGE = `usage: workspace_parity.ts {check,sync} --workspace WORKSPACE [--from FROM] [--owner OWNER]
                           [--settings SETTINGS] [--dry-run]

Compare a workspace with the main install (the content a look depends on, and the settings), or bring it to match.

options:
  -h, --help             show this help message and exit
  --workspace WORKSPACE  C:\\Games\\THUG2-n
  --from FROM            the install to match (default C:\\Games\\THUG2 - read only)
  --owner OWNER          sync: the game lock's holder, when the workspace's lock is held
  --settings SETTINGS    sync: main (the main install's ini, the default), a screenshot sidecar .txt, or none
  --dry-run              sync: say what would be copied and removed, change nothing`;

// The places compared: whole trees, the relit folder's own files (not its archive / logs), single files, and the
// DLLs beside the exe.
export const TREES = [
    "Data",
    "thug2-decomp-data",
    "thug2-decomp-textures",
    "thug2-decomp-ts",
    "thug2-decomp-props",
    "thug2-decomp-layouts",
    "thug2-decomp-thaw",
    "thug2-decomp-thaw-bmx",
    "thug2-decomp-thp8",
    "thug2-decomp-thpg",
    "thug2-decomp-thps2",
    "thug2-decomp-thps3",
    "thug2-decomp-thps4",
    "thug2-decomp-thug1",
] as const;
export const FLAT = ["thug2-decomp-relit"] as const;
export const FILES = ["thug2-decomp-materials.t2dm", "gamecontrollerdb.txt"] as const;

// The workspace's own keys: its window and its test switches (make_workspace.ps1, workspace_ini.ts).
export const WORKSPACE_KEYS: Readonly<Record<string, readonly string[]>> = {
    graphics: ["ResolutionX", "ResolutionY", "Borderless", "WindowDisplay"],
    features: ["automation", "mute_audio", "allow_multiple_instances", "start_level", "start_at", "edit_level"],
};

export interface Entry {
    size: number;
    mtime: number;
}
export type Listing = Map<string, Entry>;

export interface Diff {
    missing: string[];
    different: string[];
    extra: string[];
}

// What `to` lacks, has otherwise (size, or a modification time more than 2 s apart - FAT-era copies round to 2 s)
// and has on its own, by relative path (case-insensitive, as Windows').
export function diffListings(from: Listing, to: Listing): Diff {
    const lower = new Map<string, string>();
    for (const k of to.keys()) lower.set(k.toLowerCase(), k);
    const missing: string[] = [];
    const different: string[] = [];
    const seen = new Set<string>();
    for (const [rel, a] of from) {
        const key = lower.get(rel.toLowerCase());
        if (key === undefined) {
            missing.push(rel);
            continue;
        }
        seen.add(key);
        const b = to.get(key)!;
        if (a.size !== b.size || Math.abs(a.mtime - b.mtime) > 2000) different.push(rel);
    }
    const extra = [...to.keys()].filter((k) => !seen.has(k));
    return { missing: missing.sort(), different: different.sort(), extra: extra.sort() };
}

function list(root: string, rel: string, recurse: boolean, out: Listing): void {
    const dir = path.join(root, rel);
    let names: fs.Dirent[];
    try {
        names = fs.readdirSync(dir, { withFileTypes: true });
    } catch {
        return;
    }
    for (const d of names) {
        const r = rel ? path.join(rel, d.name) : d.name;
        if (d.isDirectory()) {
            if (recurse) list(root, r, true, out);
        } else if (d.isFile()) {
            const st = fs.statSync(path.join(root, r));
            out.set(r, { size: st.size, mtime: st.mtimeMs });
        }
    }
}

// Every compared file of a game folder, by its path relative to the folder.
export function listContent(game: string): Listing {
    const out: Listing = new Map();
    for (const t of TREES) list(game, t, true, out);
    for (const f of FLAT) list(game, f, false, out);
    for (const f of FILES) {
        const p = path.join(game, f);
        if (fs.existsSync(p)) {
            const st = fs.statSync(p);
            out.set(f, { size: st.size, mtime: st.mtimeMs });
        }
    }
    for (const d of fs.readdirSync(game, { withFileTypes: true })) {
        if (d.isFile() && /\.dll$/i.test(d.name)) {
            const st = fs.statSync(path.join(game, d.name));
            out.set(d.name, { size: st.size, mtime: st.mtimeMs });
        }
    }
    return out;
}

// The place a relative path belongs to (its first folder, or the file itself), for the report.
export function placeOf(rel: string): string {
    const i = rel.search(/[\\/]/);
    return i < 0 ? rel : rel.slice(0, i);
}

// The settings a screenshot sidecar lists ("Graphics rt_gi:   1", "features hd_ui:   1 (default)",
// "Graphics rt_relit:   1 -> 1 (On - ...)"): section (lower case), key, value (null: the default - the ini shouldn't
// have the key). "(empty)" is an empty value. The "running ..." lines are what the game made of them - not settings.
export function parseSidecarSettings(text: string): { section: string; key: string; value: string | null }[] {
    const out: { section: string; key: string; value: string | null }[] = [];
    for (const raw of text.split(/\r?\n/)) {
        const m = /^(Graphics|features|debug|Miscellaneous|online)\s+([A-Za-z0-9_]+):\s+(.*)$/.exec(raw);
        if (!m) continue;
        let v = m[3]!;
        const arrow = v.indexOf(" -> ");
        if (arrow >= 0) v = v.slice(0, arrow);
        v = v.trim();
        let value: string | null = v;
        if (/\(default\)$/.test(v)) value = null;
        else if (v === "(empty)") value = "";
        out.push({ section: m[1]!.toLowerCase(), key: m[2]!, value });
    }
    return out;
}

// The ini's text without key in [section] (case-insensitive).
export function removeIniValue(text: string, section: string, key: string): string {
    const nl = text.includes("\r\n") ? "\r\n" : "\n";
    const lines = text.split(/\r?\n/);
    const want = section.toLowerCase();
    let current = "";
    const keep: string[] = [];
    for (const line of lines) {
        const h = /^\[(.+)\]$/.exec(line.trim());
        if (h) current = h[1]!.trim().toLowerCase();
        else if (current === want && line.trim().toLowerCase().startsWith(`${key.toLowerCase()}=`)) continue;
        keep.push(line);
    }
    return keep.join(nl);
}

// key's value in [section], or null.
export function getIniValue(text: string, section: string, key: string): string | null {
    const want = section.toLowerCase();
    let current = "";
    for (const line of text.split(/\r?\n/)) {
        const t = line.trim();
        const h = /^\[(.+)\]$/.exec(t);
        if (h) current = h[1]!.trim().toLowerCase();
        else if (current === want) {
            const eq = t.indexOf("=");
            if (eq > 0 && t.slice(0, eq).trim().toLowerCase() === key.toLowerCase()) return t.slice(eq + 1).trim();
        }
    }
    return null;
}

function isWorkspaceKey(section: string, key: string): boolean {
    return (WORKSPACE_KEYS[section.toLowerCase()] ?? []).some((k) => k.toLowerCase() === key.toLowerCase());
}

// The workspace's ini made from the source's settings: `source` an ini's text (taken whole) or a sidecar's settings
// (applied over the workspace's ini); the workspace's own keys as the workspace had them.
export function settingsFor(
    workspaceIni: string,
    source: { ini: string } | { sidecar: ReturnType<typeof parseSidecarSettings> },
): string {
    let text: string;
    if ("ini" in source) {
        text = source.ini;
        for (const [section, keys] of Object.entries(WORKSPACE_KEYS)) {
            for (const key of keys) {
                const mine = getIniValue(workspaceIni, section, key);
                text = mine === null ? removeIniValue(text, section, key) : setIniValue(text, section, key, mine);
            }
        }
    } else {
        text = workspaceIni;
        for (const s of source.sidecar) {
            if (isWorkspaceKey(s.section, s.key)) continue;
            text =
                s.value === null
                    ? removeIniValue(text, s.section, s.key)
                    : setIniValue(text, s.section, s.key, s.value);
        }
    }
    // (a workspace's: the test channel on, several games at once, muted - make_workspace.ps1)
    text = setIniValue(text, "features", "automation", "1");
    text = setIniValue(text, "features", "allow_multiple_instances", "1");
    return setIniValue(text, "features", "mute_audio", "1");
}

// The settings that differ between two inis' [Graphics] / [features] / [debug], the workspace's own keys left out:
// "section key: a -> b" ("-" for a key one of them lacks).
export function settingsDiff(a: string, b: string): string[] {
    const read = (text: string): Map<string, string> => {
        const out = new Map<string, string>();
        let current = "";
        for (const line of text.split(/\r?\n/)) {
            const t = line.trim();
            const h = /^\[(.+)\]$/.exec(t);
            if (h) current = h[1]!.trim().toLowerCase();
            else if (["graphics", "features", "debug"].includes(current) && !t.startsWith(";")) {
                const eq = t.indexOf("=");
                if (eq > 0) {
                    const key = t.slice(0, eq).trim();
                    if (!isWorkspaceKey(current, key))
                        out.set(`${current} ${key.toLowerCase()}`, t.slice(eq + 1).trim());
                }
            }
        }
        return out;
    };
    const ma = read(a);
    const mb = read(b);
    const keys = [...new Set([...ma.keys(), ...mb.keys()])].sort();
    return keys.filter((k) => ma.get(k) !== mb.get(k)).map((k) => `${k}: ${ma.get(k) ?? "-"} -> ${mb.get(k) ?? "-"}`);
}

function megabytes(n: number): string {
    return `${(n / 1048576).toFixed(1)} MB`;
}

function report(diff: Diff, from: Listing): string[] {
    const places = new Map<string, { missing: number; different: number; extra: number; bytes: number }>();
    const at = (rel: string) => {
        const p = placeOf(rel);
        if (!places.has(p)) places.set(p, { missing: 0, different: 0, extra: 0, bytes: 0 });
        return places.get(p)!;
    };
    for (const r of diff.missing) {
        at(r).missing++;
        at(r).bytes += from.get(r)!.size;
    }
    for (const r of diff.different) {
        at(r).different++;
        at(r).bytes += from.get(r)!.size;
    }
    for (const r of diff.extra) at(r).extra++;
    const lines: string[] = [];
    for (const [p, c] of [...places].sort(([a], [b]) => a.localeCompare(b))) {
        lines.push(
            `  ${p}: ${c.missing} missing, ${c.different} different, ${c.extra} only in the workspace (${megabytes(c.bytes)} to copy)`,
        );
    }
    return lines;
}

// What the game's start-up line "settings: capabilities" finds in a folder (src/app/settings_capabilities.cpp).
export function capabilities(listing: Listing): string {
    const keys = [...listing.keys()].map((k) => k.toLowerCase());
    const relief = keys.some(
        (k) => k.endsWith(".relief") && (k.startsWith("data\\levels") || k.startsWith("thug2-decomp-data\\levels")),
    );
    // (round GRIT) the levels' maps apart from the models' (as the game's line says)
    const pbr = keys.filter((k) => k.endsWith(".pbr"));
    const levelMaps = pbr.some((k) => k.startsWith("data\\levels") || k.startsWith("thug2-decomp-data\\levels"));
    const maps = levelMaps ? "found" : pbr.length ? "found for the models only - none for the levels" : "none";
    const library = keys.includes("thug2-decomp-materials.t2dm");
    const sets = new Set(
        keys
            .filter((k) => k.startsWith("thug2-decomp-textures\\"))
            .map((k) => k.split("\\")[1]!)
            .filter((s) => !s.includes(".")),
    );
    return `surface maps ${maps}, material library ${library ? "found" : "none"}, texture sets ${sets.size ? [...sets].sort().join(" ") : "none"}, building relief files ${relief ? "found" : "none"}`;
}

function copyFile(fromGame: string, toGame: string, rel: string): void {
    const dst = path.join(toGame, rel);
    fs.mkdirSync(path.dirname(dst), { recursive: true });
    fs.copyFileSync(path.join(fromGame, rel), dst);
    const st = fs.statSync(path.join(fromGame, rel));
    fs.utimesSync(dst, st.atime, st.mtime);
}

function main(): number {
    const { values, positionals } = parseCli(USAGE, {
        options: {
            workspace: { type: "string" },
            from: { type: "string", default: "C:\\Games\\THUG2" },
            owner: { type: "string" },
            settings: { type: "string", default: "main" },
            "dry-run": { type: "boolean", default: false },
        },
        allowPositionals: true,
    });
    const action = positionals[0];
    if (action !== "check" && action !== "sync") exitWith(`${USAGE}\nerror: check or sync`);
    if (!values.workspace) exitWith("give --workspace C:\\Games\\THUG2-n");
    const ws = resolveGameFolder(values.workspace);
    const from = resolveGameFolder(values.from);
    if (!isWorkspace(ws)) exitWith(`${ws} isn't a workspace (C:\\Games\\THUG2-n) - only a workspace is ever written`);
    if (path.resolve(ws).toLowerCase() === path.resolve(from).toLowerCase())
        exitWith("--workspace and --from are the same folder");
    const fromList = listContent(from);
    const wsList = listContent(ws);
    const diff = diffListings(fromList, wsList);
    const fromIni = fs.readFileSync(path.join(from, "thug2-decomp.ini"), "utf8");
    const wsIniPath = path.join(ws, "thug2-decomp.ini");
    const wsIni = fs.existsSync(wsIniPath) ? fs.readFileSync(wsIniPath, "utf8") : "";
    print(`${from}: ${capabilities(fromList)}`);
    print(`${ws}: ${capabilities(wsList)}`);
    const lines = report(diff, fromList);
    print(lines.length ? `content that differs (${ws} against ${from}):` : "content: the same");
    for (const l of lines) print(l);
    const settings = settingsDiff(wsIni, fromIni);
    print(
        settings.length
            ? `settings that differ (the workspace's -> ${from}'s; its window and test keys left out):`
            : "settings: the same",
    );
    for (const s of settings) print(`  ${s}`);
    const differs = diff.missing.length + diff.different.length + diff.extra.length > 0;
    if (action === "check") return differs ? 1 : 0;

    // sync
    const here = path.resolve(ws).toLowerCase();
    if (runningGames().some((p) => p === null || path.dirname(path.resolve(p)).toLowerCase() === here))
        exitWith(`a game is running from ${ws} (or one whose folder can't be read) - nothing changed`);
    const lock = path.join(ws, ".game-lock", "owner.txt");
    const holder = fs.existsSync(lock) ? fs.readFileSync(lock, "utf8").trim() : null;
    if (holder !== null && !(values.owner && holder.startsWith(`${values.owner} `)))
        exitWith(`${ws}'s game lock is held (${holder}) - give --owner as its holder, or wait for it`);
    const copy = [...diff.missing, ...diff.different];
    const bytes = copy.reduce((n, r) => n + fromList.get(r)!.size, 0);
    const free = fs.statfsSync(ws).bavail * fs.statfsSync(ws).bsize;
    const freed = diff.extra.reduce((n, r) => n + wsList.get(r)!.size, 0);
    if (bytes > free + freed - 2 * 1073741824)
        exitWith(
            `the copy (${megabytes(bytes)}) wouldn't leave 2 GB free on ${ws}'s disk (${megabytes(free)} free) - nothing changed`,
        );
    let newIni: string | null = null;
    if (values.settings === "main") newIni = settingsFor(wsIni, { ini: fromIni });
    else if (values.settings !== "none") {
        if (!fs.existsSync(values.settings)) exitWith(`no file ${values.settings}`);
        const parsed = parseSidecarSettings(fs.readFileSync(values.settings, "utf8"));
        if (!parsed.length)
            exitWith(`${values.settings} has no "Graphics <key>: <value>" lines - not a screenshot sidecar`);
        newIni = settingsFor(wsIni, { sidecar: parsed });
    }
    if (values["dry-run"]) {
        print(
            `would copy ${copy.length} files (${megabytes(bytes)}), remove ${diff.extra.length}${newIni !== null ? ", write the ini" : ""}`,
        );
        return 0;
    }
    for (const r of diff.extra) fs.rmSync(path.join(ws, r), { force: true });
    for (const r of copy) copyFile(from, ws, r);
    print(`copied ${copy.length} files (${megabytes(bytes)}), removed ${diff.extra.length}`);
    if (newIni !== null) {
        fs.writeFileSync(wsIniPath, newIni, "utf8");
        ensureWorkspaceMuted(ws);
        print(
            `wrote ${wsIniPath} from ${values.settings === "main" ? path.join(from, "thug2-decomp.ini") : values.settings}` +
                (holder !== null ? " (the lock's release puts the ini it backed up back)" : ""),
        );
    }
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
