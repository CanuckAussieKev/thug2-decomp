// NOT ORIGINAL - builds the FFmpeg DLLs the FFmpeg movie codec loads (src/media_ffmpeg): FFmpeg 7.1.5, LGPL 2.1 or
// later only (no --enable-gpl, no --enable-nonfree), with nothing in it but what THUG2's Bink files need - the Bink
// demuxer and the Bink video and audio decoders, and swscale for the frames' colours - as avformat-61.dll,
// avcodec-61.dll, avutil-59.dll and swscale-8.dll.
//
//   node tools/ffmpeg/build_ffmpeg.ts [--arch x86|x64] [--out <folder>] [--jobs N] [--distro <WSL distribution>]
//
// No trustworthy prebuilt 32-bit LGPL FFmpeg DLLs exist (the usual Windows builds are 64-bit, and most are GPL), so
// they are built from FFmpeg's signed release tarball with FFmpeg's own cross-compiling path: its configure and make
// with the mingw-w64 GCC cross compiler, on Linux - on Windows inside WSL (`wsl.exe -d <distro>`). The toolchain:
// Ubuntu's gcc-mingw-w64-i686-win32 (x86) / gcc-mingw-w64-x86-64-win32 (x64) and make
// (`sudo apt-get install gcc-mingw-w64-i686-win32 gcc-mingw-w64-x86-64-win32 gcc make xz-utils`; the host gcc builds configure's helper programs). The "win32" compilers
// use Windows' own threads (FFmpeg's w32threads): the DLLs need nothing but Windows' system DLLs (libgcc is linked in
// statically). The build is repeatable: the same DLLs, byte for byte, wherever and whenever they are built (a fixed
// prefix and build folder, the release's date as the link time stamp).
//
// The tarball is checked against its SHA-256 (the release's signature, by FFmpeg's release key
// FCF986EA15E6E293A5644F10B4322F04D67658D8, was checked when the hash was pinned). The output folder
// (default out/ffmpeg/<arch>) gets bin/ (the DLLs), include/ (their headers), lib/ (import libraries) and
// build.json (the version, the hash, configure's flags, the compiler and the licence configure reports). The build
// copies the DLLs next to the exe (src/media_ffmpeg/CMakeLists.txt); the codec loads them by name at run time, so
// any LGPL build of the same FFmpeg major versions can replace them.

import { spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import * as fs from "node:fs";
import * as path from "node:path";
import { exitWith, isMain, parseCli, print, runMain } from "thug2-level-converter-core/lib/cli.ts";

export const FFMPEG_VERSION = "7.1.5";
export const FFMPEG_URL = `https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.xz`;
export const FFMPEG_SHA256 = "de668509caf9e35e3cd162473441fdb29538c6d96ed080292b3cf9e6fc5d558f";
// The DLL names of FFmpeg 7.1's major versions (what src/media_ffmpeg loads).
export const FFMPEG_DLLS = ["avutil-59.dll", "avcodec-61.dll", "avformat-61.dll", "swscale-8.dll"];

const ROOT = path.join(import.meta.dirname, "..", "..");
const INSTALL_PREFIX = "/thug2-ffmpeg";
const RELEASE_EPOCH = 1781922373; // 2026-06-20 02:26:13 UTC, the tarball's date

type Arch = "x86" | "x64";
const CROSS: Record<Arch, { arch: string; prefix: string }> = {
    x86: { arch: "x86", prefix: "i686-w64-mingw32-" },
    x64: { arch: "x86_64", prefix: "x86_64-w64-mingw32-" },
};

// configure's flags: everything off, then the Bink demuxer and decoders back on.
export function configureFlags(arch: Arch, prefix: string): string[] {
    const cross = CROSS[arch];
    return [
        `--prefix=${prefix}`,
        "--enable-cross-compile",
        "--target-os=mingw32",
        `--arch=${cross.arch}`,
        `--cross-prefix=${cross.prefix}`,
        `--cc=${cross.prefix}gcc-win32`,
        "--enable-shared",
        "--disable-static",
        "--disable-everything",
        "--disable-autodetect",
        "--disable-programs",
        "--disable-doc",
        "--disable-network",
        "--disable-avdevice",
        "--disable-avfilter",
        "--disable-swresample",
        "--disable-x86asm",
        "--disable-debug",
        "--enable-w32threads",
        "--enable-demuxer=bink",
        "--enable-decoder=bink,binkaudio_dct,binkaudio_rdft",
        "--extra-ldflags=-static-libgcc",
    ];
}

// The tarball, downloaded once into out/ffmpeg/download and checked.
async function tarball(): Promise<string> {
    const file = path.join(ROOT, "out", "ffmpeg", "download", `ffmpeg-${FFMPEG_VERSION}.tar.xz`);
    const sha256 = (data: Buffer): string => createHash("sha256").update(data).digest("hex");
    if (fs.existsSync(file) && sha256(fs.readFileSync(file)) === FFMPEG_SHA256) return file;
    fs.mkdirSync(path.dirname(file), { recursive: true });
    for (let attempt = 1; attempt <= 5; ++attempt) {
        try {
            const response = await fetch(FFMPEG_URL);
            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            const data = Buffer.from(await response.arrayBuffer());
            const got = sha256(data);
            if (got !== FFMPEG_SHA256) exitWith(`${FFMPEG_URL}: SHA-256 ${got}, expected ${FFMPEG_SHA256}`);
            fs.writeFileSync(file, data);
            return file;
        } catch (e) {
            print(`download attempt ${attempt}: ${e instanceof Error ? e.message : String(e)}`);
        }
    }
    return exitWith(`could not download ${FFMPEG_URL}`);
}

// A path as the Linux side sees it (WSL's /mnt/<drive>/...).
function linuxPath(windowsPath: string): string {
    if (process.platform !== "win32") return windowsPath;
    const full = path.resolve(windowsPath);
    return `/mnt/${full[0]!.toLowerCase()}${full.slice(2).replaceAll("\\", "/")}`;
}

// Runs a bash script on the Linux side; its output goes to ours.
function bash(script: string, distro: string): number {
    const scriptFile = path.join(ROOT, "out", "ffmpeg", "build.sh");
    fs.writeFileSync(scriptFile, script.replaceAll("\r\n", "\n"));
    const command =
        process.platform === "win32"
            ? spawnSync("wsl.exe", ["-d", distro, "--", "bash", linuxPath(scriptFile)], { stdio: "inherit" })
            : spawnSync("bash", [scriptFile], { stdio: "inherit" });
    return command.status ?? 1;
}

function quote(text: string): string {
    return `'${text.replaceAll("'", `'\\''`)}'`;
}

async function main(): Promise<number> {
    const { values } = parseCli(
        "usage: build_ffmpeg.ts [--arch x86|x64] [--out <folder>] [--jobs N] [--distro <WSL distribution>]",
        {
            options: {
                arch: { type: "string", default: "x86" },
                out: { type: "string" },
                jobs: { type: "string", default: "3" },
                distro: { type: "string", default: "Ubuntu" },
            },
        },
    );
    const arch = values.arch;
    if (arch !== "x86" && arch !== "x64") return exitWith(`--arch ${arch}: x86 or x64`);
    const out = path.resolve(values.out ?? path.join(ROOT, "out", "ffmpeg", arch));
    const source = await tarball();
    const installed = linuxPath(out);
    // (configure's prefix is a fixed name: it and the build folder end up in the DLLs, which stay the same wherever
    // they are built and installed)
    const flags = configureFlags(arch, INSTALL_PREFIX);
    const compiler = `${CROSS[arch].prefix}gcc-win32`;
    // Built in a Linux temporary folder (FFmpeg's build is slow over WSL's Windows drives), installed into `out`.
    const work = `/tmp/thug2-ffmpeg-${arch}`;
    const script = [
        "set -euo pipefail",
        // (the linker's time stamp in the DLLs: the release's date)
        `export SOURCE_DATE_EPOCH=${RELEASE_EPOCH}`,
        `work=${work}`,
        `rm -rf "$work" && mkdir -p "$work"`,
        `trap 'rm -rf "$work"' EXIT`,
        `tar -xJf ${quote(linuxPath(source))} -C "$work"`,
        `cd "$work/ffmpeg-${FFMPEG_VERSION}"`,
        `./configure ${flags.map(quote).join(" ")} > "$work/configure.log" || { tail -40 "$work/configure.log"; cat ffbuild/config.log | tail -40; exit 1; }`,
        `grep -E "^(License|External libraries|Enabled (decoders|demuxers))" -A2 "$work/configure.log" || true`,
        `grep -q 'define FFMPEG_LICENSE "LGPL version 2.1 or later"' config.h || { echo "not an LGPL configuration"; exit 1; }`,
        `make -j${Number(values.jobs)} > "$work/make.log" 2>&1 || { tail -40 "$work/make.log"; exit 1; }`,
        `make install DESTDIR="$work/staged" > "$work/install.log" 2>&1 || { tail -40 "$work/install.log"; exit 1; }`,
        `rm -rf ${quote(installed)} && mkdir -p ${quote(installed)}`,
        `cp -r "$work/staged${INSTALL_PREFIX}/." ${quote(installed)}`,
        `${compiler} --version | head -1 > ${quote(`${installed}/compiler.txt`)}`,
    ].join("\n");
    const status = bash(`${script}\n`, values.distro);
    if (status !== 0) return exitWith(`the FFmpeg build failed (exit ${status})`);
    for (const dll of FFMPEG_DLLS) {
        if (!fs.existsSync(path.join(out, "bin", dll))) return exitWith(`${dll} was not built`);
    }
    const compilerText = fs.readFileSync(path.join(out, "compiler.txt"), "utf8").trim();
    fs.rmSync(path.join(out, "compiler.txt"));
    const record = {
        version: FFMPEG_VERSION,
        url: FFMPEG_URL,
        sha256: FFMPEG_SHA256,
        licence: "LGPL version 2.1 or later",
        arch,
        compiler: compilerText,
        configure: configureFlags(arch, INSTALL_PREFIX),
        dlls: Object.fromEntries(
            FFMPEG_DLLS.map((dll) => [
                dll,
                createHash("sha256")
                    .update(fs.readFileSync(path.join(out, "bin", dll)))
                    .digest("hex"),
            ]),
        ),
    };
    fs.writeFileSync(path.join(out, "build.json"), `${JSON.stringify(record, null, 4)}\n`);
    print(`FFmpeg ${FFMPEG_VERSION} (${arch}) in ${out}`);
    for (const [dll, hash] of Object.entries(record.dlls)) print(`  ${dll} ${hash}`);
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
