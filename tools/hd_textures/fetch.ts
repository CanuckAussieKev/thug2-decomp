// NOT ORIGINAL - fetches the optional AI texture upscaler the converters' `--hd-textures` pass runs
// (tools/level_port/hd_textures.ts): Real-ESRGAN's standalone realesrgan-ncnn-vulkan (BSD-3-Clause, Xintao Wang;
// Vulkan - no CUDA or PyTorch) - one pinned release zip, its SHA-256 checked, unpacked into
// out\hd_textures\<release>\ (out\ is git-ignored: nothing of it is committed).
//
//     node tools/hd_textures/fetch.ts [--force]
//
// --force downloads and unpacks again. The zip ships the models realesrgan-x4plus, realesrgan-x4plus-anime and
// realesr-animevideov3 (x2 / x3 / x4): README.md.

import { spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import * as fs from "node:fs";
import * as path from "node:path";
import { REPO } from "../level_port/common.ts";
import { DEFAULT_TOOL, TOOL_RELEASE } from "../level_port/hd_textures.ts";
import { isMain, print, pyExit, runMain } from "../lib/py.ts";

// Real-ESRGAN v0.2.5.0 (2022-04-24), the Windows build
export const RELEASE_URL = `https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/${TOOL_RELEASE}-windows.zip`;
export const SHA256 = "abc02804e17982a3be33675e4d471e91ea374e65b70167abc09e31acb412802d";

export async function main(argv: readonly string[] = process.argv.slice(2)): Promise<number> {
    const force = argv.includes("--force");
    const unknown = argv.filter((a) => a !== "--force");
    if (unknown.length) pyExit(`usage: fetch.ts [--force] (unknown: ${unknown.join(" ")})`);
    const target = path.dirname(DEFAULT_TOOL);
    if (fs.existsSync(DEFAULT_TOOL) && !force) {
        print(`already there: ${DEFAULT_TOOL}`);
        return 0;
    }
    const base = path.join(REPO, "out", "hd_textures");
    fs.mkdirSync(base, { recursive: true });
    const zip = path.join(base, `${TOOL_RELEASE}-windows.zip`);
    print(`downloading ${RELEASE_URL}`);
    const response = await fetch(RELEASE_URL);
    if (!response.ok) pyExit(`download failed: HTTP ${response.status}`);
    const data = Buffer.from(await response.arrayBuffer());
    const hash = createHash("sha256").update(data).digest("hex");
    if (hash !== SHA256) pyExit(`SHA-256 mismatch: got ${hash}, expected ${SHA256} - not unpacked`);
    fs.writeFileSync(zip, data);
    // (a fresh, empty folder for the archive's files; Windows' tar.exe reads zips)
    fs.rmSync(target, { recursive: true, force: true });
    fs.mkdirSync(target, { recursive: true });
    // (Windows' own bsdtar by its path: a Git Bash GNU tar first on PATH takes "C:" for a remote host)
    const windowsTar = path.join(process.env.SystemRoot ?? "C:\\Windows", "System32", "tar.exe");
    const tar = spawnSync(fs.existsSync(windowsTar) ? windowsTar : "tar", ["-xf", zip, "-C", target], {
        encoding: "utf8",
    });
    if (tar.status !== 0) pyExit(`unpacking failed: ${tar.stderr}`);
    fs.rmSync(zip);
    if (!fs.existsSync(DEFAULT_TOOL)) pyExit(`${DEFAULT_TOOL} not in the archive`);
    print(`unpacked into ${target} (${data.length} bytes, SHA-256 ok)`);
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
