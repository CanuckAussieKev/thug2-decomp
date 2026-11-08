// NOT ORIGINAL - tests of deploy_build.ps1 with game_lock.ps1 and game_exe.ps1's Get-PeArch: in a scratch game folder (no
// real game: the files are PE headers and nothing else) a 64-bit build is deployed in place of a 32-bit one under the lock,
// a build of the wrong machine is refused, and release gives the 32-bit files back.

import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const windows = process.platform === "win32";

// A file that is only a PE header: "MZ", the PE signature at 0x40, the machine, and a marker text after it.
function peFile(machine: number, marker: string): Buffer {
    const buf = Buffer.alloc(0x100);
    buf.write("MZ", 0, "latin1");
    buf.writeUInt32LE(0x40, 0x3c);
    buf.writeUInt32LE(0x4550, 0x40);
    buf.writeUInt16LE(machine, 0x44);
    buf.write(marker, 0x80, "latin1");
    return buf;
}

function powershell(script: string, args: string[]): { code: number | null; out: string } {
    const r = spawnSync(
        "powershell.exe",
        ["-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", path.join(HERE, script), ...args],
        { encoding: "utf-8" },
    );
    return { code: r.status, out: `${r.stdout}${r.stderr}` };
}

const X86 = 0x14c;
const X64 = 0x8664;
const DLLS = ["d3dx9_43.dll", "SDL3.dll"];

function makeBuild(dir: string, machine: number, marker: string): void {
    fs.mkdirSync(dir, { recursive: true });
    fs.writeFileSync(path.join(dir, "thug2-decomp.exe"), peFile(machine, marker));
    fs.writeFileSync(path.join(dir, "thug2-decomp.pdb"), marker);
    for (const dll of DLLS) fs.writeFileSync(path.join(dir, dll), peFile(machine, marker));
    fs.writeFileSync(path.join(dir, "gamecontrollerdb.txt"), marker);
}

const text = (file: string): string => fs.readFileSync(file, "latin1").replaceAll("\0", "");

test("deploy_build.ps1: a 64-bit build in place of a 32-bit one, under the lock; release puts it back", {
    skip: !windows,
}, () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), "thug2-deploy-"));
    try {
        const game = path.join(root, "game");
        makeBuild(game, X86, "x86-original");
        fs.writeFileSync(path.join(game, "THUG2.exe"), "marker");
        fs.writeFileSync(path.join(game, "thug2-decomp.ini"), "[features]\r\nmute_audio=1\r\n");
        const x86 = path.join(root, "build-x86");
        const x64 = path.join(root, "build-x64");
        makeBuild(x86, X86, "x86-new");
        makeBuild(x64, X64, "x64-build");
        // (the 64-bit build has the level editor plugin and its launcher; the folder had neither)
        const plugin = path.join("plugins", "thug2-level-editor.dll");
        const launcher = "thug2-editor.exe";
        fs.mkdirSync(path.join(x64, "plugins"));
        fs.writeFileSync(path.join(x64, plugin), peFile(X64, "x64-build"));
        fs.writeFileSync(path.join(x64, launcher), peFile(X64, "x64-build"));
        // (the editor's tools folder: an older one in the game folder, a new one made already - stand-ins with their
        // bundle.json)
        const toolsFolder = (at: string, key: string): void => {
            fs.mkdirSync(path.join(at, "pipeline"), { recursive: true });
            fs.writeFileSync(path.join(at, "bundle.json"), `{"key":"${key}"}\n`);
            fs.writeFileSync(path.join(at, "pipeline", "run.ts"), key);
        };
        toolsFolder(path.join(game, "editor-tools"), "old");
        const tools = path.join(root, "editor-tools");
        toolsFolder(tools, "new");
        const toolsText = (): string =>
            fs.readFileSync(path.join(game, "editor-tools", "pipeline", "run.ts"), "latin1");

        // no lock: refused
        const noLock = powershell("deploy_build.ps1", ["-Game", game, "-Arch", "x64", "-BuildDir", x64]);
        assert.equal(noLock.code, 2, noLock.out);
        assert.match(noLock.out, /THE LOCK IS NOT HELD/);

        assert.equal(powershell("game_lock.ps1", ["-Action", "acquire", "-Owner", "t", "-Game", game]).code, 0);

        // the wrong machine: refused, nothing copied
        const wrong = powershell("deploy_build.ps1", ["-Game", game, "-Arch", "x64", "-BuildDir", x86]);
        assert.equal(wrong.code, 2, wrong.out);
        assert.match(wrong.out, /is x86, not x64/);
        assert.match(text(path.join(game, "thug2-decomp.exe")), /x86-original/);

        // the 64-bit build goes in: its exe and DLLs, the FFmpeg ones missing from the build warned about
        const deploy = ["-Game", game, "-Arch", "x64", "-BuildDir", x64, "-EditorTools", tools];
        const done = powershell("deploy_build.ps1", deploy);
        assert.equal(done.code, 0, done.out);
        assert.match(done.out, /DEPLOYED the x64 build/);
        assert.match(done.out, /FFmpeg's DLLs missing/);
        for (const f of ["thug2-decomp.exe", ...DLLS, plugin, launcher])
            assert.match(text(path.join(game, f)), /x64-build/, f);
        // (the tools folder replaced whole; the same again isn't copied)
        assert.equal(toolsText(), "new");
        assert.match(powershell("deploy_build.ps1", deploy).out, /editor-tools\\ up to date/);

        // run_game.ps1 -Arch x86 refuses the 64-bit exe before it starts anything
        const refused = powershell("run_game.ps1", ["-Game", game, "-Arch", "x86", "-Seconds", "1"]);
        assert.equal(refused.code, 2, refused.out);
        assert.match(refused.out, /is x64, not x86/);

        // release: the 32-bit files are back as they were, the ini too
        const released = powershell("game_lock.ps1", ["-Action", "release", "-Owner", "t", "-Game", game]);
        assert.equal(released.code, 0, released.out);
        for (const f of ["thug2-decomp.exe", ...DLLS]) assert.match(text(path.join(game, f)), /x86-original/, f);
        assert.equal(fs.readFileSync(path.join(game, "thug2-decomp.ini"), "latin1"), "[features]\r\nmute_audio=1\r\n");
        assert.equal(fs.existsSync(path.join(game, ".game-lock")), false);
        // (the plugin and the launcher it didn't have removed, with the plugins folder it left empty)
        assert.equal(fs.existsSync(path.join(game, "plugins")), false);
        assert.equal(fs.existsSync(path.join(game, launcher)), false);
        // (the tools folder as it was)
        assert.equal(toolsText(), "old");
    } finally {
        fs.rmSync(root, { recursive: true, force: true });
    }
});
