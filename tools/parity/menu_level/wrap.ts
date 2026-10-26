// NOT ORIGINAL - the wrapper of tools/pipeline/menu_level's and tools/anteara's parity cases (cases.ts): builds a game folder
// of its own under <out> from a workspace's files (junctions, nothing copied, nothing written into the workspace),
// runs the tool in it (python for a .py, node for a .ts), takes the junctions away again, passes the exit code on,
// prints the tool's stdout with <out> written as "<out>" and rewrites every .qb under <out> with its 0x2B name
// entries sorted (tools/parity/converters/qb_norm.ts: ns.exe writes them in map order) and every .png as its pixels
// (<name>.png.pixels: the deflate streams differ - pngPixels).
//
//   node tools/parity/menu_level/wrap.ts <out> <layout> <tool.py|tool.ts> [args...]
//
// <layout>: ";"-separated settings, e.g. "ws=C:\Games\THUG2-1;stagings=all;scripts=1;sub=0;default=0;existing=none":
//   ws        the workspace whose Data and converted levels are used (read only)
//   stagings  all / none / a ","-list of thps4, thug1, thps3, thaw: the converted games' staging folders present
//   scripts   0: no Data\scripts (no loose scripts: Anteara Mod isn't installed)
//   sub       1: the game in a Game subfolder (<out>\root\Game; "{game}" is <out>\root - the tool steps in)
//   default   1: Data\levels\Default with a Default.scn.xbx and Default.qb (empty files: only their presence counts)
//   existing  none / ours (an overlay with the marker and a stale file) / foreign (a folder with someone's file)
//   tools     1: a copy of tools\pipeline\lib, tools\pipeline\level_port and the tool's own file at <out>\tools (for anteara extract,
//             which writes next to itself: its mod\ is copied in too); removed after the run but for mod\
// "{game}" in the arguments is the game folder made, "{out}" the output folder.

import { spawnSync } from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import * as zlib from "node:zlib";
import { normalise } from "../converters/qb_norm.ts";

const TOOLS = path.dirname(path.dirname(path.dirname(fileURLToPath(import.meta.url))));
const STAGINGS = ["thps4", "thug1", "thps3", "thaw"];

function link(target: string, at: string): void {
    fs.symlinkSync(target, at, "junction");
}

// Every junction under `dir` taken away (the link only, never what it points to); then the folders left empty.
function unlinkAll(dir: string): void {
    if (!fs.existsSync(dir)) return;
    for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
        const p = path.join(dir, e.name);
        if (e.isSymbolicLink()) fs.unlinkSync(p);
        else if (e.isDirectory()) unlinkAll(p);
    }
}

function removeEmpty(dir: string): void {
    if (!fs.existsSync(dir) || !fs.statSync(dir).isDirectory()) return;
    for (const e of fs.readdirSync(dir)) removeEmpty(path.join(dir, e));
    if (fs.readdirSync(dir).length === 0) fs.rmdirSync(dir);
}

function qbFiles(dir: string): string[] {
    const out: string[] = [];
    if (!fs.existsSync(dir)) return out;
    for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
        const p = path.join(dir, e.name);
        if (e.isDirectory()) out.push(...qbFiles(p));
        else if (e.name.toLowerCase().endsWith(".qb")) out.push(p);
    }
    return out;
}

// A PNG as its chunks with the image data inflated: Python 3.14's zlib (zlib-ng) and Node's deflate the same pixels
// into different streams, so the level select preview is compared by its pixels (and its other chunks).
function pngPixels(data: Buffer): Buffer {
    const parts: Buffer[] = [data.subarray(0, 8)];
    const idat: Buffer[] = [];
    let at = 8;
    while (at + 8 <= data.length) {
        const size = data.readUInt32BE(at);
        const kind = data.subarray(at + 4, at + 8).toString("latin1");
        const body = data.subarray(at + 8, at + 8 + size);
        if (kind === "IDAT") idat.push(body);
        else parts.push(data.subarray(at, at + 12 + size));
        at += 12 + size;
    }
    parts.push(Buffer.from("IDAT (inflated)\n", "latin1"), zlib.inflateSync(Buffer.concat(idat)));
    return Buffer.concat(parts);
}

function pngFiles(dir: string): string[] {
    const out: string[] = [];
    if (!fs.existsSync(dir)) return out;
    for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
        const p = path.join(dir, e.name);
        if (e.isDirectory()) out.push(...pngFiles(p));
        else if (e.name.toLowerCase().endsWith(".png")) out.push(p);
    }
    return out;
}

function main(): number {
    const [out, layoutText, tool, ...args] = process.argv.slice(2);
    if (out === undefined || layoutText === undefined || tool === undefined) {
        process.stderr.write("usage: node tools/parity/menu_level/wrap.ts <out> <layout> <tool> [args...]\n");
        return 2;
    }
    const layout = new Map(
        layoutText
            .split(";")
            .filter((s) => s)
            .map((s): [string, string] => {
                const at = s.indexOf("=");
                return [s.slice(0, at), s.slice(at + 1)];
            }),
    );
    const ws = layout.get("ws") ?? "C:\\Games\\THUG2-1";
    const sub = layout.get("sub") === "1";
    const gameArg = sub ? path.join(out, "root") : path.join(out, "game");
    const game = sub ? path.join(gameArg, "Game") : gameArg;
    const data = path.join(game, "Data");
    fs.mkdirSync(data, { recursive: true });
    for (const e of fs.readdirSync(path.join(ws, "Data"), { withFileTypes: true })) {
        if (!e.isDirectory()) continue;
        const name = e.name.toLowerCase();
        if (name === "scripts" && layout.get("scripts") === "0") continue;
        if (name === "levels" && layout.get("default") === "1") {
            const levels = path.join(data, e.name);
            fs.mkdirSync(levels);
            for (const level of fs.readdirSync(path.join(ws, "Data", e.name))) {
                if (level.toLowerCase() === "default") continue;
                link(path.join(ws, "Data", e.name, level), path.join(levels, level));
            }
            fs.mkdirSync(path.join(levels, "Default"));
            fs.writeFileSync(path.join(levels, "Default", "Default.scn.xbx"), "");
            fs.writeFileSync(path.join(levels, "Default", "Default.qb"), "");
            continue;
        }
        link(path.join(ws, "Data", e.name), path.join(data, e.name));
    }
    const stagings = layout.get("stagings") ?? "all";
    const wanted = stagings === "all" ? STAGINGS : stagings === "none" ? [] : stagings.split(",");
    for (const g of wanted) {
        const from = path.join(ws, `thug2-decomp-${g}`);
        if (fs.existsSync(from)) link(from, path.join(game, `thug2-decomp-${g}`));
    }
    const existing = layout.get("existing") ?? "none";
    if (existing === "ours") {
        const overlay = path.join(game, "thug2-decomp-data");
        fs.mkdirSync(path.join(overlay, "levels", "stale"), { recursive: true });
        fs.writeFileSync(path.join(overlay, "made-by-make_menu_level.txt"), "marker\n");
        fs.writeFileSync(path.join(overlay, "levels", "stale", "stale.qb"), "");
        fs.writeFileSync(path.join(overlay, "stale.txt"), "stale\n");
    } else if (existing === "foreign") {
        const overlay = path.join(game, "thug2-decomp-data");
        fs.mkdirSync(overlay, { recursive: true });
        fs.writeFileSync(path.join(overlay, "someone_elses.txt"), "keep me\n");
    }
    let script = tool;
    if (layout.get("tools") === "1") {
        // (a copy of the tool and what it imports, so what it writes next to itself lands in <out>)
        const copy = path.join(out, "tools");
        for (const dir of ["lib", "level_port"]) {
            fs.cpSync(path.join(TOOLS, dir), path.join(copy, dir), {
                recursive: true,
                filter: (p) => !p.endsWith(".py") && !p.includes("__pycache__"),
            });
        }
        const own = path.join(copy, path.basename(path.dirname(tool)));
        fs.mkdirSync(own, { recursive: true });
        fs.copyFileSync(tool, path.join(own, path.basename(tool)));
        fs.cpSync(path.join(path.dirname(tool), "mod"), path.join(own, "mod"), { recursive: true });
        script = path.join(own, path.basename(tool));
    }
    const filled = args.map((a) => a.replaceAll("{game}", gameArg).replaceAll("{out}", out));
    const result = script.endsWith(".py")
        ? spawnSync("python", ["-I", script, ...filled], { maxBuffer: 1 << 30 })
        : spawnSync(process.execPath, [script, ...filled], { maxBuffer: 1 << 30 });
    unlinkAll(game);
    if (layout.get("tools") === "1") {
        const copy = path.join(out, "tools");
        const own = path.join(copy, path.basename(path.dirname(tool)));
        for (const dir of ["lib", "level_port"]) fs.rmSync(path.join(copy, dir), { recursive: true, force: true });
        fs.rmSync(path.join(own, path.basename(tool)), { force: true });
        fs.rmSync(path.join(own, "__pycache__"), { recursive: true, force: true });
    }
    // (the game folder's empty Data and the made layout's dummy files are the wrapper's, not the tool's)
    const levelsDefault = path.join(data, "levels", "Default");
    if (fs.existsSync(levelsDefault)) fs.rmSync(levelsDefault, { recursive: true, force: true });
    removeEmpty(gameArg);
    process.stdout.write(result.stdout.toString("utf-8").replaceAll(out, "<out>"));
    process.stderr.write(result.stderr.toString("utf-8").replaceAll(out, "<out>"));
    for (const file of qbFiles(out)) fs.writeFileSync(file, normalise(fs.readFileSync(file)));
    for (const file of pngFiles(out)) {
        fs.writeFileSync(`${file}.pixels`, pngPixels(fs.readFileSync(file)));
        fs.rmSync(file);
    }
    return result.status ?? 1;
}

process.exitCode = main();
