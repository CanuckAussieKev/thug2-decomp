// NOT ORIGINAL - the level editor's tools folder, editor-tools\ beside thug2-decomp.exe: what its Import Level window
// runs (src/editor/level_editor_import.h) - the level converters and the content pipeline, with a Node runtime of
// their own, so the player needs no Node, npm or checkout:
//
//   node tools/editor_tools.ts [--out <folder>] [--force]
//
//   editor-tools\node.exe         Node.js (NODE_VERSION, the official Windows x64 build: its zip from nodejs.org,
//                                 checked against NODE_ZIP_SHA256 and nodejs.org's SHASUMS256.txt), LICENSE.node.txt
//                                 its licence
//   editor-tools\ns.exe           NeverScript's ns (the converters' QB compiler: thug2-level-converter-core's pinned
//                                 release, checked against its SHA-256 - lib/neverscript.ts); the editor names it in
//                                 NEVERSCRIPT for the tools it runs
//   editor-tools\pipeline\        the content pipeline (tools/pipeline's files without its tests) and, in its
//                                 node_modules, the converters it pins - `npm ci --omit=dev` from its package-lock.json
//   editor-tools\anteara\         Anteara Mod (tools/anteara), which make_menu_level installs into the overlay when it is
//                                 beside the pipeline
//   editor-tools\bundle.json      what it was made from (versions, the pipeline's commit) and a key over all of it
//
// Made in out\editor-tools (--out: elsewhere); downloads cached in out\node and out\editor-tools-cache. Nothing is made
// again while bundle.json's key matches (--force: always). tools/game/deploy_build.ps1 copies the folder into a game
// folder (game_exe.ps1's $DeployedFolders - the lock backs it up as the files).

import { spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import * as fs from "node:fs";
import * as path from "node:path";
import { exitWith, isMain, parseCli, print, runMain } from "thug2-level-converter-core/lib/cli.ts";

export const ROOT = path.resolve(import.meta.dirname, "..");
export const NODE_VERSION = "24.11.1";
export const NODE_ZIP = `node-v${NODE_VERSION}-win-x64.zip`;
export const NODE_ZIP_SHA256 = "5355ae6d7c49eddcfde7d34ac3486820600a831bf81dc3bdca5c8db6a9bb0e76";
const NODE_DIST = `https://nodejs.org/dist/v${NODE_VERSION}`;

const sha256 = (data: Uint8Array): string => createHash("sha256").update(data).digest("hex");

// The files of a submodule (tracked, and new ones not ignored) that go into the bundle: not its tests, lint and
// type-check settings or CI.
export function bundledFiles(listed: readonly string[]): string[] {
    const left = (f: string): boolean =>
        /\.test\.ts$/.test(f) ||
        /^\.github\//.test(f) ||
        /^(\.gitignore|\.gitattributes|tsconfig\.json|biome\.json|lint\.biome\.json)$/.test(f);
    return listed.filter((f) => f && !left(f)).sort();
}

function gitFiles(folder: string): string[] {
    const r = spawnSync("git", ["-C", folder, "ls-files", "--cached", "--others", "--exclude-standard"], {
        encoding: "utf-8",
    });
    if (r.status !== 0) exitWith(`git ls-files in ${folder} failed: ${r.stderr}`);
    return bundledFiles(r.stdout.split(/\r?\n/)).filter((f) => fs.existsSync(path.join(folder, f)));
}

function gitHead(folder: string): string {
    const r = spawnSync("git", ["-C", folder, "rev-parse", "HEAD"], { encoding: "utf-8" });
    return r.status === 0 ? r.stdout.trim() : "";
}

async function download(url: string): Promise<Uint8Array> {
    const response = await fetch(url);
    if (!response.ok) throw new Error(`${url}: ${response.status} ${response.statusText}`);
    return new Uint8Array(await response.arrayBuffer());
}

// The Node zip, from the cache or nodejs.org - its SHA-256 the pinned one, which nodejs.org's SHASUMS256.txt lists.
async function nodeZip(cache: string): Promise<string> {
    const file = path.join(cache, NODE_ZIP);
    if (fs.existsSync(file) && sha256(fs.readFileSync(file)) === NODE_ZIP_SHA256) return file;
    const sums = new TextDecoder().decode(await download(`${NODE_DIST}/SHASUMS256.txt`));
    const listed = sums.split(/\r?\n/).find((line) => line.trim().endsWith(`  ${NODE_ZIP}`));
    if (listed?.split(/\s+/)[0] !== NODE_ZIP_SHA256) {
        throw new Error(`${NODE_DIST}/SHASUMS256.txt doesn't give ${NODE_ZIP} the pinned SHA-256 ${NODE_ZIP_SHA256}`);
    }
    print(`downloading ${NODE_DIST}/${NODE_ZIP}`);
    const data = await download(`${NODE_DIST}/${NODE_ZIP}`);
    const got = sha256(data);
    if (got !== NODE_ZIP_SHA256)
        throw new Error(`${NODE_ZIP}: SHA-256 ${got}, expected ${NODE_ZIP_SHA256} - not saved`);
    fs.mkdirSync(cache, { recursive: true });
    fs.writeFileSync(file, data);
    return file;
}

// node.exe and LICENSE out of the zip (Windows' own tar reads zip files).
function unzipNode(zip: string, into: string): void {
    const top = `node-v${NODE_VERSION}-win-x64`;
    const tar = path.join(process.env.SystemRoot ?? "C:\\Windows", "System32", "tar.exe");
    const r = spawnSync(tar, ["-xf", zip, "-C", into, `${top}/node.exe`, `${top}/LICENSE`], { encoding: "utf-8" });
    if (r.status !== 0) exitWith(`${tar} -xf ${zip} failed: ${r.stdout}${r.stderr}`);
    fs.renameSync(path.join(into, top, "node.exe"), path.join(into, "node.exe"));
    fs.renameSync(path.join(into, top, "LICENSE"), path.join(into, "LICENSE.node.txt"));
    fs.rmSync(path.join(into, top), { recursive: true, force: true });
}

// NeverScript's release ns for Windows x64, fetched into the cache (thug2-level-converter-core's own check).
async function neverscript(cache: string): Promise<{ file: string; sha256: string }> {
    process.env.THUG2_CONVERTERS_CACHE = cache;
    const ns = await import("thug2-level-converter-core/lib/neverscript.ts");
    const release = ns.releaseFile("win32", "x64");
    if (release === null) exitWith("no NeverScript release for Windows x64");
    return { file: await ns.fetchNs(), sha256: release[1] };
}

function copyFiles(from: string, files: readonly string[], to: string): void {
    for (const f of files) {
        const target = path.join(to, ...f.split("/"));
        fs.mkdirSync(path.dirname(target), { recursive: true });
        fs.copyFileSync(path.join(from, ...f.split("/")), target);
    }
}

function folderSize(folder: string): number {
    let total = 0;
    for (const entry of fs.readdirSync(folder, { withFileTypes: true, recursive: true })) {
        if (entry.isFile()) total += fs.statSync(path.join(entry.parentPath, entry.name)).size;
    }
    return total;
}

// The key over everything the bundle is made from.
function bundleKey(
    parts: readonly (readonly [string, string])[],
    folders: readonly [string, readonly string[]][],
): string {
    const hash = createHash("sha256");
    for (const [name, value] of parts) hash.update(`${name}=${value}\n`);
    for (const [folder, files] of folders) {
        for (const f of files) {
            hash.update(`${folder}/${f}\n`);
            hash.update(fs.readFileSync(path.join(folder, ...f.split("/"))));
        }
    }
    return hash.digest("hex");
}

function readKey(out: string): string | null {
    try {
        return (JSON.parse(fs.readFileSync(path.join(out, "bundle.json"), "utf-8")) as { key?: string }).key ?? null;
    } catch {
        return null;
    }
}

const USAGE = "usage: editor_tools.ts [--out <folder>] [--force]";

async function main(): Promise<number> {
    if (process.platform !== "win32")
        exitWith("the editor's tools folder is made for Windows x64 (the editor's build)");
    const { values } = parseCli(USAGE, {
        options: { out: { type: "string" }, force: { type: "boolean", default: false } },
    });
    const out = path.resolve(values.out ?? path.join(ROOT, "out", "editor-tools"));
    const pipeline = path.join(ROOT, "tools", "pipeline");
    const anteara = path.join(ROOT, "tools", "anteara");
    if (!fs.existsSync(path.join(pipeline, "package-lock.json"))) {
        exitWith(`${pipeline} has no package-lock.json (git submodule update --init)`);
    }
    const pipelineFiles = gitFiles(pipeline);
    const antearaFiles = fs.existsSync(path.join(anteara, "anteara.ts")) ? gitFiles(anteara) : [];
    const ns = await neverscript(path.join(ROOT, "out", "editor-tools-cache"));
    const key = bundleKey(
        [
            ["node", NODE_ZIP_SHA256],
            ["ns", ns.sha256],
        ],
        [
            [pipeline, pipelineFiles],
            [anteara, antearaFiles],
        ],
    );
    if (!values.force && readKey(out) === key && fs.existsSync(path.join(out, "node.exe"))) {
        print(`${out} is up to date (${(folderSize(out) / 1e6).toFixed(1)} MB)`);
        return 0;
    }

    const work = `${out}.making`;
    fs.rmSync(work, { recursive: true, force: true });
    fs.mkdirSync(work, { recursive: true });
    unzipNode(await nodeZip(path.join(ROOT, "out", "node")), work);
    fs.copyFileSync(ns.file, path.join(work, "ns.exe"));
    copyFiles(pipeline, pipelineFiles, path.join(work, "pipeline"));
    if (antearaFiles.length) copyFiles(anteara, antearaFiles, path.join(work, "anteara"));
    // (the converters the pipeline pins, production only: built on install - their prepare - from the lock file)
    const npm = spawnSync("npm ci --omit=dev --no-audit --no-fund", {
        cwd: path.join(work, "pipeline"),
        stdio: "inherit",
        shell: true,
    });
    if (npm.status !== 0) exitWith(`npm ci in ${path.join(work, "pipeline")} failed`);
    const bundle = {
        note: "The level editor's tools (tools/editor_tools.ts in the THUG2 decompilation): Node.js, NeverScript's ns, the content pipeline and its level converters.",
        node: NODE_VERSION,
        nodeZipSha256: NODE_ZIP_SHA256,
        neverscriptSha256: ns.sha256,
        pipeline: gitHead(pipeline),
        anteara: antearaFiles.length ? gitHead(anteara) : null,
        key,
    };
    fs.writeFileSync(path.join(work, "bundle.json"), `${JSON.stringify(bundle, null, 2)}\n`);
    fs.rmSync(out, { recursive: true, force: true });
    fs.renameSync(work, out);
    const size = folderSize(out);
    print(
        `made ${out}: ${(size / 1e6).toFixed(1)} MB (node.exe ${(fs.statSync(path.join(out, "node.exe")).size / 1e6).toFixed(1)} MB, pipeline ${(folderSize(path.join(out, "pipeline")) / 1e6).toFixed(1)} MB)`,
    );
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
