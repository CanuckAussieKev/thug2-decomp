import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";
import { ensureWorkspaceMuted, isWorkspace, setIniValue } from "./workspace_ini.ts";

const CASES: [string, string][] = [
    // the key there: replaced
    [
        "[features]\r\nautomation=1\r\nmute_audio=0\r\n[x]\r\nmute_audio=5\r\n",
        "[features]\r\nautomation=1\r\nmute_audio=1\r\n[x]\r\nmute_audio=5\r\n",
    ],
    // the section without it: added under the header (case and spaces don't matter)
    [
        "[Graphics]\nmsaa=4\n[ Features ]\nautomation=1\n",
        "[Graphics]\nmsaa=4\n[ Features ]\nmute_audio=1\nautomation=1\n",
    ],
    // a key of the same name in another section only: the section added
    ["[x]\r\nmute_audio=0\r\n", "[x]\r\nmute_audio=0\r\n[features]\r\nmute_audio=1\r\n"],
    // no ini
    ["", "[features]\nmute_audio=1\n"],
    // MUTE_AUDIO = 0 spelled differently: replaced
    ["[features]\nMUTE_AUDIO = 0", "[features]\nmute_audio=1"],
];

test("setIniValue", () => {
    for (const [before, after] of CASES) assert.equal(setIniValue(before, "features", "mute_audio", "1"), after);
});

test("isWorkspace: only C:\\Games\\THUG2-<n>", () => {
    for (const yes of [
        "C:\\Games\\THUG2-1",
        "C:\\Games\\THUG2-4\\Game",
        "c:\\games\\thug2-2\\",
        "C:/Games/THUG2-3/Game",
    ])
        assert.ok(isWorkspace(yes), yes);
    for (const no of ["C:\\Games\\THUG2", "C:\\Games\\THUG2\\Game", "C:\\Games\\THUG2-BACKUP", "D:\\THUG2-1"])
        assert.ok(!isWorkspace(no), no);
    assert.equal(ensureWorkspaceMuted("C:\\Games\\THUG2\\Game"), false); // (never writes there)
});

// The PowerShell twin (game_exe.ps1, used by game_lock.ps1 and run_game.ps1) gives the same text.
test("Set-IniValue (game_exe.ps1) agrees", { skip: process.platform !== "win32" }, () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "workspace-ini-"));
    try {
        const script = path.join(path.dirname(fileURLToPath(import.meta.url)), "game_exe.ps1");
        CASES.forEach(([before], i) => {
            fs.writeFileSync(path.join(dir, `${i}.ini`), before, "utf8");
        });
        const ps =
            `. '${script}'; foreach ($i in 0..${CASES.length - 1}) { $f = Join-Path '${dir}' "$i.ini"; ` +
            `[IO.File]::WriteAllText("$f.out", (Set-IniValue ([IO.File]::ReadAllText($f)) 'features' 'mute_audio' '1')) }; ` +
            `if (Test-Workspace 'C:\\Games\\THUG2\\Game') { exit 1 }; if (-not (Test-Workspace 'C:\\Games\\THUG2-1\\Game')) { exit 1 }`;
        execFileSync("powershell.exe", ["-NoProfile", "-NonInteractive", "-Command", ps]);
        CASES.forEach(([, after], i) => {
            assert.equal(fs.readFileSync(path.join(dir, `${i}.ini.out`), "utf8"), after, `case ${i}`);
        });
    } finally {
        fs.rmSync(dir, { recursive: true, force: true });
    }
});
