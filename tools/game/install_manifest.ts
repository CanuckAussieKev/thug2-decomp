// NOT ORIGINAL - a manifest of a THUG2 install's files - path, size and SHA-256 of each - and a check of another
// install against it, to see how a different version of the game (another release, a patch, a different PartyMod
// build) differs from the one this project is built against.
//
//     node tools/pipeline/game/install_manifest.ts make  [--game C:\Games\THUG2] [--out notes/data/install_manifest.json]
//     node tools/pipeline/game/install_manifest.ts check --game D:\Other\THUG2 [--manifest notes/data/install_manifest.json]
//
// Left out: what this project or the player made, not the game - our dinput8.dll, thug2-decomp* (ini, logs, overlays,
// layouts, converted levels), the game lock, Backup, Save and the Faces folder. A file's "kind" says where it comes
// from: exe (the reference build, which every replacement address is for), partymod (PartyMod's SDL2 files), game.

import * as crypto from "node:crypto";
import * as fs from "node:fs";
import * as path from "node:path";
import { parseArgs } from "node:util";
import { helpFromHeader } from "../lib/cli.ts";
import { DECOMP, GAME_EXE_NAMES, LOCAL } from "../lib/decomp.ts";
import { findInstall } from "../lib/install.ts";
import { formatFixed, isMain, print, pyExit, pyJsonDumps, pyParseInt, runMain } from "../lib/py.ts";

// (the decomp's notes/data/install_manifest.json - the private notes repo, lib/decomp.ts; on its own, --manifest / --out)
const DEFAULT_MANIFEST = DECOMP ? path.join(DECOMP, "notes", "data", "install_manifest.json") : "";
// Game executables by SHA-256, and whether the development DLL runs on them (it's never modified): read from the
// private notes repo (notes/data/known_exes.json: { "<sha256>": [runs, "description"] }); without it every exe is
// "unknown". (Once the game is built from this source, only the data files matter.)
const KNOWN_EXES_FILE = DECOMP ? path.join(DECOMP, "notes", "data", "known_exes.json") : "";
const KNOWN_EXES = new Map<string, [boolean, string]>(
    KNOWN_EXES_FILE && fs.existsSync(KNOWN_EXES_FILE)
        ? Object.entries(JSON.parse(fs.readFileSync(KNOWN_EXES_FILE, "utf-8")) as Record<string, [boolean, string]>)
        : [],
);
const EXE_NAMES = new Set(GAME_EXE_NAMES.map((n) => n.toLowerCase()));
// Ours or the player's (top-level names, lower case; "thug2-decomp" also covers every thug2-decomp-* name).
const SKIP = new Set(["dinput8.dll", "backup", "save", "faces", ".game-lock", ".anteara-backup"]);
const PARTYMOD = new Set(["sdl2.dll", "gamecontrollerdb.txt"]);

type Kind = "exe" | "partymod" | "game";

interface FileEntry {
    size: number;
    sha256: string;
    kind: Kind;
}

function skipped(top: string): boolean {
    const low = top.toLowerCase();
    // (a Ghidra project kept in the game folder: its lock files)
    const ghidraLock = LOCAL.ghidraProject ? `${LOCAL.ghidraProject.toLowerCase()}.lock` : null;
    return SKIP.has(low) || low.startsWith("thug2-decomp") || (ghidraLock !== null && low.startsWith(ghidraLock));
}

// A stock install keeps the game in a Game subfolder (Game\Data, Game\THUG2.exe); PartyMod's, ours and a stock one
// whose launcher and docs were deleted have it at the top (tools/pipeline/lib/install.ts).
export function gameRoot(folder: string): string {
    return findInstall(folder, "thug2") ?? folder;
}

function kind(rel: string): Kind {
    const low = rel.toLowerCase();
    if (EXE_NAMES.has(low)) return "exe";
    return PARTYMOD.has(low) ? "partymod" : "game";
}

function sha256(file: string): string {
    const h = crypto.createHash("sha256");
    const fd = fs.openSync(file, "r");
    try {
        const block = Buffer.alloc(1 << 20);
        for (;;) {
            const n = fs.readSync(fd, block, 0, block.length, null);
            if (n === 0) break;
            h.update(block.subarray(0, n));
        }
    } finally {
        fs.closeSync(fd);
    }
    return h.digest("hex");
}

// os.walk(game), top-level names we or the player made left out; paths relative with "/".
function scan(game: string): Map<string, FileEntry> {
    const files = new Map<string, FileEntry>();
    const walk = (dir: string, top: boolean): void => {
        let entries: fs.Dirent[];
        try {
            entries = fs.readdirSync(dir, { withFileTypes: true });
        } catch {
            return;
        }
        const dirs: string[] = [];
        let fileCount = 0;
        for (const e of entries) {
            if (top && skipped(e.name)) continue;
            if (e.isDirectory()) {
                dirs.push(e.name);
                continue;
            }
            fileCount++;
            const p = path.join(dir, e.name);
            const rel = path.relative(game, p).split(path.sep).join("/");
            files.set(rel, { size: fs.statSync(p).size, sha256: sha256(p), kind: kind(rel) });
        }
        if (files.size && files.size % 2000 < fileCount) process.stderr.write(`  ${files.size} files...\n`);
        for (const d of dirs) walk(path.join(dir, d), false);
    };
    walk(game, true);
    return files;
}

function make(game: string, out: string): number {
    const files = scan(game);
    const sorted = [...files.entries()].sort((x, y) => {
        const a = x[0].toLowerCase();
        const b = y[0].toLowerCase();
        return a < b ? -1 : a > b ? 1 : 0;
    });
    const manifest = new Map<string, unknown>([
        [
            "about",
            "THUG2 install manifest (tools/pipeline/game/install_manifest.ts): every game file's size and SHA-256; " +
                "this project's files, Backup, Save and Faces left out",
        ],
        ["source", path.resolve(game)],
        ["files", files.size],
        ["bytes", [...files.values()].reduce((n, f) => n + f.size, 0)],
        ["exe_sha256", GAME_EXE_NAMES.map((n) => files.get(n)?.sha256).find((h) => h) ?? null],
        ["manifest", new Map(sorted.map(([p, f]) => [p, new Map<string, unknown>(Object.entries(f))]))],
    ]);
    fs.mkdirSync(path.dirname(path.resolve(out)), { recursive: true });
    fs.writeFileSync(out, `${pyJsonDumps(manifest, 0)}\n`, "utf-8");
    print(
        `${out}: ${manifest.get("files")} files, ${formatFixed((manifest.get("bytes") as number) / 1e6, 1)} MB; ` +
            `reference exe ${manifest.get("exe_sha256") ?? "None"}`,
    );
    return 0;
}

function check(game: string, manifestPath: string, list: number): number {
    const all = (JSON.parse(fs.readFileSync(manifestPath, "utf-8")) as { manifest: Record<string, FileEntry> })
        .manifest;
    const root = gameRoot(game);
    const scanned = scan(root);
    // The executables apart: which one this is, by its hash; the rest compared file by file.
    const exes = new Map([...scanned].filter(([, f]) => f.kind === "exe"));
    const files = new Map([...scanned].filter(([, f]) => f.kind !== "exe"));
    const reference = new Map(Object.entries(all).filter(([, f]) => f.kind !== "exe"));
    const refLower = new Map([...reference.keys()].map((k) => [k.toLowerCase(), k]));
    const haveLower = new Map([...files.keys()].map((k) => [k.toLowerCase(), k]));
    const byCodePoint = (a: string, b: string): number => (a < b ? -1 : a > b ? 1 : 0);
    const missing = [...refLower.keys()]
        .filter((k) => !haveLower.has(k))
        .map((k) => refLower.get(k)!)
        .sort(byCodePoint);
    const extra = [...haveLower.keys()]
        .filter((k) => !refLower.has(k))
        .map((k) => haveLower.get(k)!)
        .sort(byCodePoint);
    const common = [...refLower.keys()].filter((k) => haveLower.has(k));
    const changed = common
        .filter((k) => reference.get(refLower.get(k)!)!.sha256 !== files.get(haveLower.get(k)!)!.sha256)
        .map((k) => refLower.get(k)!)
        .sort(byCodePoint);
    const same = common.length - changed.length;

    const byKind = (paths: readonly string[]): string => {
        const counts = new Map<string, number>();
        for (const p of paths) {
            const k = reference.get(p)?.kind ?? kind(p);
            counts.set(k, (counts.get(k) ?? 0) + 1);
        }
        return (
            [...counts.entries()]
                .sort((x, y) => byCodePoint(x[0], y[0]) || x[1] - y[1])
                .map(([k, n]) => `${k} ${n}`)
                .join(", ") || "none"
        );
    };

    print(`${root} against ${manifestPath}:`);
    print("  executables:");
    for (const [p, f] of [...exes.entries()].sort((x, y) => byCodePoint(x[0], y[0]))) {
        const known = KNOWN_EXES.get(f.sha256) ?? [
            false,
            `unknown (sha256 ${f.sha256}) - not a build this project knows`,
        ];
        print(`    ${p}: ${known[1]}`);
    }
    if (exes.size === 0) print("    none found");
    if (![...exes.values()].some((f) => KNOWN_EXES.get(f.sha256)?.[0] ?? false)) {
        print("    -> no exe the development DLL is known to run on");
    }
    print("  game files (the executables apart):");
    print(`    identical: ${same} of ${reference.size}`);
    print(`    changed:   ${changed.length} (${byKind(changed)})`);
    print(`    missing:   ${missing.length} (${byKind(missing)})`);
    print(`    extra:     ${extra.length} (${byKind(extra)})`);
    for (const [title, paths] of [
        ["changed", changed],
        ["missing", missing],
        ["extra", extra],
    ] as const) {
        // (Python's paths[:n] with a negative n: all but the last -n)
        for (const p of paths.slice(0, list < 0 ? Math.max(0, paths.length + list) : list)) print(`    ${title} ${p}`);
        if (paths.length > list) print(`    ... ${paths.length - list} more ${title} (--list N shows more)`);
    }
    return changed.length || missing.length ? 1 : 0;
}

const USAGE = "usage: install_manifest.ts [-h] {make,check} ...";

function main(): number {
    const [action, ...rest] = process.argv.slice(2);
    if (action !== "make" && action !== "check") {
        process.stderr.write(`${USAGE}\ninstall_manifest.ts: error: argument action: invalid choice\n`);
        return 2;
    }
    let values: {
        game?: string | undefined;
        out?: string | undefined;
        manifest?: string | undefined;
        list?: string | undefined;
    };
    try {
        values = parseArgs({
            args: rest,
            options:
                action === "make"
                    ? { game: { type: "string" }, out: { type: "string" } }
                    : { game: { type: "string" }, manifest: { type: "string" }, list: { type: "string" } },
        }).values as typeof values;
    } catch (e) {
        process.stderr.write(`${USAGE}\ninstall_manifest.ts: error: ${(e as Error).message}\n`);
        return 2;
    }
    const game = values.game ?? (action === "make" ? "C:\\Games\\THUG2\\Game" : undefined);
    if (game === undefined) {
        process.stderr.write(
            `${USAGE}\ninstall_manifest.ts check: error: the following arguments are required: --game\n`,
        );
        return 2;
    }
    if (!(fs.statSync(game, { throwIfNoEntry: false })?.isDirectory() ?? false)) pyExit(`not a folder: ${game}`);
    const manifest = (action === "make" ? values.out : values.manifest) ?? DEFAULT_MANIFEST;
    if (!manifest)
        pyExit(
            `no decomp checkout around this repository (its notes/data/install_manifest.json): pass --${action === "make" ? "out" : "manifest"}`,
        );
    if (action === "make") return make(game, manifest);
    return check(game, manifest, values.list === undefined ? 40 : pyParseInt(values.list));
}

if (isMain(import.meta.url)) helpFromHeader(import.meta.url);
if (isMain(import.meta.url)) await runMain(() => main());
