// NOT ORIGINAL - a game folder without archives or .qb files: the game run from loose files and TypeScript
// ([features] ts_scripts=1, docs/qb-ts.md "Unpacked"). Every archive in Data\pre (.prx, the French and German .prf /
// .prd and their patches .ppf / .ppd) and every .qb (Data's, the data overlay's, the ported levels' overlays) is moved
// to <game>\thug2-decomp-backup at the same path; each .qb becomes a .ts: its TypeScript (emit_ts.ts), which the game
// runs as it loads the file (src/script_qb/ts_bridge.cpp load_standalone), and a .json beside it: its globals
// (emit_data.ts, qb_data.ts - read by the game straight into its cells); and every file of the archives goes loose
// where its name says in Data (Data\levels\bo\BO.scn.xbx, Data\levels\bo\BO_scripts.ts...), the archive's copy over a
// loose one that differs (that one backed up too) - the game loads no archive and reads it all loose. The French and
// German archives aren't unpacked: their scripts are the English ones with other text, so the scripts' text becomes
// tokens with a table per language - Data\lang\en.json, fr.json, de.json (localize.ts). --check keeps every archive in
// its folder, its text in the scripts - the script check runs each version of a file. The .ts's last line -
// `// qb-file {...}` - is its manifest: the file's source name, the hash of the .qb it was made from and the file's
// checksum names.
//
//   node tools/qbjs/unpack_game.ts --game <folder>            unpack (the game folder as installed)
//   node tools/qbjs/unpack_game.ts --game <folder> --refresh  put back, then unpack again (after make_menu_level.ts or
//                                                             a converter rewrote an overlay, or a new emitter)
//   node tools/qbjs/unpack_game.ts --game <folder> --restore  everything put back as it was, the made files removed
//   [--check]  the script check's translation (emit_ts.ts check) - for its runs only, never the game's
//   [--hd-textures pbrify|seedvr2|none]  after unpacking (or on a game already unpacked: only this) the levels'
//              textures upscaled by that AI model and installed - none (the default) puts the originals back;
//              [--hd-levels BO,AU] only those levels, [--hd-scope all] every .tex.xbx, not only the levels' - the
//              pipeline's hd_textures/base.ts (its README: the models' setup, caches, what is skipped). --restore
//              puts the original textures back first.
//
// A .qb that doesn't translate is an error (listed; the exit code 1): the game reads no .qb - that file isn't loaded. <game>\thug2-decomp-backup\
// unpacked.json lists what was moved and made, for --restore.
import { existsSync, mkdirSync, readdirSync, readFileSync, renameSync, rmSync, statSync, writeFileSync } from "node:fs";
import { basename, dirname, join, relative } from "node:path";
import { pathToFileURL } from "node:url";
import * as prx from "thug2-level-converter-core/level_port/prx.ts";
import * as hdBase from "../pipeline/hd_textures/base.ts";
import { emitData } from "./emit_data.ts";
import { emitTS } from "./emit_ts.ts";
import { fileHash, gameRoot, scriptBytes } from "./game_scripts.ts";
import type { ModuleIR } from "./ir.ts";
import { Localizer, notLocalised } from "./localize.ts";
import { readIR } from "./reader.ts";

const BACKUP = "thug2-decomp-backup";
type Record = { moved: string[]; made: string[]; folders: string[] };

const walk = (path: string): string[] =>
    !existsSync(path)
        ? []
        : statSync(path).isDirectory()
          ? readdirSync(path).flatMap((name) => walk(join(path, name)))
          : [path];

// The script folders: Data and the overlays the game reads .qb from (thug2-decomp-data and the ported levels'),
// not ours that hold no game files.
function scriptFolders(root: string): string[] {
    const skip = new Set([
        BACKUP,
        "thug2-decomp-ts",
        "thug2-decomp-layouts",
        "thug2-decomp-previous",
        "thug2-decomp-props",
    ]);
    return [
        "Data",
        ...readdirSync(root).filter(
            (name) =>
                name.toLowerCase().startsWith("thug2-decomp-") &&
                !skip.has(name.toLowerCase()) &&
                !name.toLowerCase().startsWith("thug2-decomp-check") &&
                statSync(join(root, name)).isDirectory(),
        ),
    ];
}

// A .qb's TypeScript with its manifest line; null when it doesn't translate. `source`: its name in the runtime
// ("Data\\levels\\bo\\bo_scripts.qb", lower case - as game_scripts.ts qbVersions names them), `name` its game path.
// localize: the IR's text given tokens before it's written (localize.ts).
// The globals are the module's data (`json`, null when it has none: emit_data.ts, docs/qb-ts.md "Data") - the file's
// <name>.json beside its <name>.ts; the manifest's `globals` counts them (the game wants the .json when it isn't 0).
export function scriptTS(
    source: string,
    name: string,
    read: Buffer,
    check = false,
    localize?: (ir: ModuleIR) => void,
): { ts: string; json: string | null } | null {
    const bytes = scriptBytes(name, read);
    try {
        const ir = readIR(bytes, source);
        localize?.(ir);
        const from = fileHash(bytes);
        const data = basename(name).replace(/\.qb$/i, ".json");
        const json = emitData(ir, from);
        const ts = emitTS(ir, {}, { check, data });
        const names = Object.entries(ir.names ?? {}).flatMap(([n, text]) => [Number(n), text]);
        const globals = ir.definitions.filter((d) => d.kind === "global").length;
        const manifest = JSON.stringify({ source, from, globals, names });
        return { ts: `${ts.endsWith("\n") ? ts : `${ts}\n`}// qb-file ${manifest}\n`, json };
    } catch {
        return null;
    }
}

// A French / German patch (.ppf / .ppd) applied to the English archive's bytes (pre_format::apply_patch).
function applyPatch(archive: Buffer, patch: Buffer): Buffer {
    const patchedSize = patch.readUInt32LE(0);
    const count = patch.readUInt32LE(4);
    const out = Buffer.alloc(Math.max(patchedSize, archive.length));
    archive.copy(out);
    let at = 8;
    for (let k = 0; k < count; k++) {
        const offset = patch.readUInt32LE(at);
        const length = patch.readUInt32LE(at + 4);
        patch.copy(out, offset, at + 8, at + 8 + length);
        at += 8 + length;
    }
    return out.subarray(0, out.readUInt32LE(0));
}

function readRecord(root: string): Record | null {
    const path = join(root, BACKUP, "unpacked.json");
    return existsSync(path) ? (JSON.parse(readFileSync(path, "utf8")) as Record) : null;
}

export function restore(game: string): { moved: number; removed: number } {
    const root = gameRoot(game);
    const record = readRecord(root);
    if (!record) throw Error(`${root}: not unpacked (no ${BACKUP}\\unpacked.json)`);
    hdBase.restore(root); // (the HD textures' originals first: they're kept in the backup folder too)
    for (const made of record.made) rmSync(join(root, made), { force: true });
    for (const folder of record.folders) rmSync(join(root, folder), { recursive: true, force: true });
    let moved = 0;
    for (const file of record.moved) {
        const from = join(root, BACKUP, file);
        const to = join(root, file);
        if (!existsSync(from)) continue;
        // (an overlay remade since - make_menu_level.ts, a converter -: its new file kept, the old one dropped)
        if (existsSync(to)) rmSync(from);
        else {
            mkdirSync(dirname(to), { recursive: true });
            renameSync(from, to);
            moved++;
        }
    }
    rmSync(join(root, BACKUP), { recursive: true, force: true });
    return { moved, removed: record.made.length + record.folders.length };
}

export function unpack(game: string, check = false): { record: Record; failed: string[] } {
    const root = gameRoot(game);
    if (readRecord(root)) throw Error(`${root}: already unpacked (--refresh to do it again, --restore to put it back)`);
    // (unpacked with its record gone - the backup folder moved away: the boot scripts' archive missing, its .ts there -
    // unpacking again would take the made files for originals)
    if (
        !existsSync(join(root, "Data", "pre", "qb_scripts.prx")) &&
        existsSync(join(root, "Data", "scripts", "game", "levels.ts"))
    )
        throw Error(
            `${root}: already unpacked, without ${BACKUP}\\unpacked.json (moved away?) - put that folder back first`,
        );
    const record: Record = { moved: [], made: [], folders: [] };
    const failed: string[] = [];
    const rel = (path: string) => relative(root, path);
    const backup = (path: string) => join(root, BACKUP, rel(path));
    const move = (path: string) => {
        mkdirSync(dirname(backup(path)), { recursive: true });
        renameSync(path, backup(path));
        record.moved.push(rel(path));
    };
    const make = (path: string, data: string | Buffer) => {
        mkdirSync(dirname(path), { recursive: true });
        writeFileSync(path, data);
        record.made.push(rel(path));
    };
    const save = () => writeFileSync(join(root, BACKUP, "unpacked.json"), `${JSON.stringify(record, null, 1)}\n`);
    mkdirSync(join(root, BACKUP), { recursive: true });
    save(); // (first: a stopped run can still be put back)

    // The archives: moved, then unpacked into their folders from the backup.
    const pre = join(root, "Data", "pre");
    const archives = existsSync(pre)
        ? readdirSync(pre).filter((name) => /\.(prx|pre|prf|prd|ppf|ppd)$/i.test(name))
        : [];
    for (const name of archives) move(join(pre, name));
    const backedUp = (name: string) => join(root, BACKUP, "Data", "pre", name);
    const lower = new Map(archives.map((name) => [name.toLowerCase(), name]));
    // The French and German archives (complete, or a patch for the English one): their scripts by archive and name -
    // the text tables are made from them (localize.ts). They aren't unpacked: the rest of their files are English's,
    // and the English scripts with a language's table are that language's.
    const LANGUAGES: { [kind: string]: string } = { ".prf": "fr", ".ppf": "fr", ".prd": "de", ".ppd": "de" };
    const localized = new Map<string, { [language: string]: Buffer }>(); // ("<archive stem>|<name>", lower case)
    for (const name of archives) {
        const kind = name.slice(-4).toLowerCase();
        const language = LANGUAGES[kind];
        if (!language) continue;
        const stem = name.replace(/\.[^.]+$/, "");
        let bytes: Buffer;
        if (kind === ".prf" || kind === ".prd") bytes = readFileSync(backedUp(name));
        else {
            // (a patch: used only when there's no complete localised archive)
            const english = lower.get(`${stem}.prx`.toLowerCase());
            if (lower.has(`${stem}${kind === ".ppf" ? ".prf" : ".prd"}`.toLowerCase()) || !english) continue;
            bytes = applyPatch(readFileSync(backedUp(english)), readFileSync(backedUp(name)));
        }
        for (const entry of prx.readArchive(bytes).entries) {
            if (!/\.qb$/i.test(entry.name)) continue;
            const key = `${stem}|${entry.name}`.toLowerCase();
            localized.set(key, { ...localized.get(key), [language]: prx.entryData(entry) });
        }
    }
    const localizer = new Localizer();
    const archiveScripts = new Set<string>(); // (the archives' scripts' names, lower case)
    const unpackInto = (bytes: Buffer, folder: string, stem: string) => {
        record.folders.push(rel(folder));
        for (const entry of prx.readArchive(bytes).entries) {
            const data = prx.entryData(entry);
            const path = join(folder, ...entry.name.split(/[\\/]/));
            if (/\.qb$/i.test(entry.name)) {
                const source = `Data\\${entry.name.replace(/\//g, "\\").toLowerCase()}`;
                const languages = check ? undefined : localized.get(`${stem}|${entry.name}`.toLowerCase());
                const localize = (ir: ModuleIR) => {
                    const versions = Object.entries(languages ?? {}).map(([language, version]) => [
                        language,
                        readIR(scriptBytes(entry.name, version), source),
                    ]);
                    // (a file the game patches - keyboard.qb -: the languages paired with the unpatched one)
                    const patched = scriptBytes(entry.name, data) !== data;
                    const unpatched = patched ? readIR(data, source) : undefined;
                    localizer.localize(source, ir, Object.fromEntries(versions), unpatched);
                };
                archiveScripts.add(entry.name.replace(/\//g, "\\").toLowerCase());
                const made = scriptTS(source, entry.name, data, check, check ? undefined : localize);
                if (made !== null) {
                    mkdirSync(dirname(path), { recursive: true });
                    writeFileSync(path.replace(/\.qb$/i, ".ts"), made.ts);
                    if (made.json !== null) writeFileSync(path.replace(/\.qb$/i, ".json"), made.json);
                    continue;
                }
                failed.push(`${rel(folder)}\\${entry.name}`);
                continue; // (no .qb: the game reads none - the file isn't loaded, an error in its log)
            }
            mkdirSync(dirname(path), { recursive: true });
            writeFileSync(path, data);
        }
    };
    for (const name of archives) {
        if (!/\.(prx|pre)$/i.test(name)) continue;
        const stem = name.replace(/\.[^.]+$/, "");
        unpackInto(readFileSync(backedUp(name)), join(pre, stem), stem);
        save();
    }

    // The loose .qb files: Data's and the overlays', each a .ts where it was - its text as tokens too (but a loose
    // file an archive's copy replaces below, and the check's).
    for (const folder of scriptFolders(root)) {
        for (const path of walk(join(root, folder))) {
            if (!/\.qb$/i.test(path) || path.toLowerCase().startsWith(pre.toLowerCase())) continue;
            const name = relative(join(root, folder), path).replace(/\//g, "\\").toLowerCase();
            const source = `${folder}\\${name}`;
            // (Anteara Mod - the overlay's anteara folder - is English only: its text stays in its scripts)
            const english = folder.toLowerCase() === "thug2-decomp-data" && name.startsWith("anteara\\");
            const plain = (folder === "Data" && archiveScripts.has(name)) || notLocalised(name) || english;
            const localize = (ir: ModuleIR) => {
                localizer.localize(source, ir, {});
            };
            const made = scriptTS(source, name, readFileSync(path), check, check || plain ? undefined : localize);
            move(path); // (one that doesn't translate too: the game reads no .qb)
            if (made === null) {
                failed.push(rel(path));
                continue;
            }
            make(path.replace(/\.qb$/i, ".ts"), made.ts);
            if (made.json !== null) make(path.replace(/\.qb$/i, ".json"), made.json);
        }
        save();
    }
    // (the text tables: en.json and each language's)
    if (Object.keys(localizer.tables.en ?? {}).length)
        for (const language of Object.keys(localizer.tables))
            make(join(root, "Data", "lang", `${language}.json`), localizer.json(language));
    for (const left of localizer.leftOut) console.log(`text not localised (not the same code as English's): ${left}`);

    // No archive at all (not --check: the check runs every version, each archive's in its folder). The game reads a
    // loaded archive's copy of a file before the loose one and loads each archive before its files - qb_scripts at boot,
    // a level's archives as the level loads -, so every file goes where its name says in Data (levels\bo\BO.scn.xbx,
    // sounds\pcm\..., scripts\...), the archive's copy over a loose one that differs (the loose scripts' debug prints,
    // two of Training's textures); the archive folders go. A name two archives hold differently stays in its folder.
    // With no archive file and no folder, both loaders load nothing
    // (archive.cpp, packs.cpp) and the game reads the files loose.
    if (!check) {
        const made = new Set(record.made.map((m) => m.toLowerCase()));
        const placed = new Map<string, Buffer>(); // (by name in Data, lower case)
        for (const folder of record.folders) {
            if (/.(fr|de)$/i.test(folder)) continue;
            for (const path of walk(join(root, folder))) {
                const inData = relative(join(root, folder), path);
                const key = inData.toLowerCase();
                const data = readFileSync(path);
                const before = placed.get(key);
                if (before) {
                    if (before.equals(data)) rmSync(path);
                    continue;
                }
                placed.set(key, data);
                const target = join(root, "Data", inData);
                if (!existsSync(target)) make(target, data);
                else if (made.has(rel(target).toLowerCase())) writeFileSync(target, data);
                else if (!readFileSync(target).equals(data)) {
                    move(target);
                    make(target, data);
                }
                rmSync(path);
            }
            save();
        }
        // (the folders left empty)
        const prune = (path: string): boolean => {
            const empty = readdirSync(path).every((n) => statSync(join(path, n)).isDirectory() && prune(join(path, n)));
            if (empty) rmSync(path, { recursive: true });
            return empty;
        };
        for (const folder of record.folders) if (existsSync(join(root, folder))) prune(join(root, folder));
        save();
    }
    writeFileSync(
        join(root, BACKUP, "README.txt"),
        "The game's archives and .qb scripts (and the loose files the archives' copies replaced), moved here by\r\n" +
            "tools/qbjs/unpack_game.ts: the game runs from loose files and .ts scripts (ts_scripts=1).\r\n" +
            "To put everything back: node tools/qbjs/unpack_game.ts --game <this game folder> --restore\r\n",
    );
    return { record, failed };
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    const args = process.argv.slice(2);
    const at = args.indexOf("--game");
    const game = at >= 0 ? args[at + 1] : undefined;
    if (!game)
        throw Error(
            "Usage: node tools/qbjs/unpack_game.ts --game <folder> [--refresh | --restore] [--check] " +
                "[--hd-textures pbrify|seedvr2|none [--hd-levels a,b] [--hd-scope levels|all]]",
        );
    const hd = hdBase.parseBaseArgs(args);
    if (args.includes("--restore") || args.includes("--refresh")) {
        const put = restore(game);
        console.log(`put back: ${put.moved} files moved back, ${put.removed} made files / folders removed`);
    }
    if (!args.includes("--restore") && !args.includes("--refresh") && hd.model && readRecord(gameRoot(game))) {
        // (already unpacked: only the textures)
        hdBase.installAndReport(gameRoot(game), hd);
    } else if (!args.includes("--restore")) {
        const { record, failed } = unpack(game, args.includes("--check"));
        for (const name of failed) console.log(`ERROR - doesn't translate (the game won't load it): ${name}`);
        console.log(
            `unpacked: ${record.moved.length} files moved to ${BACKUP}, ${record.made.length} files made (the .ts and the ` +
                `archives' files, loose)`,
        );
        if (failed.length) process.exitCode = 1;
        hdBase.installAndReport(gameRoot(game), hd);
    }
}
