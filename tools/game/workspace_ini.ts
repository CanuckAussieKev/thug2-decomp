// NOT ORIGINAL - the workspaces' ini rule for the TypeScript launchers (edit_level.ts, try_spot.ts): a workspace's
// games (C:\Games\THUG2-1, -2, ...) always run muted, [features] mute_audio=1 (src/game/sfx/mute_audio.h) - the user
// is at the machine. The main install C:\Games\THUG2 (and anything else) is never touched. The PowerShell twin is in
// game_exe.ps1 (Test-Workspace, Set-IniValue, Set-WorkspaceMuted: game_lock.ps1, run_game.ps1) - keep them the same.

import * as fs from "node:fs";
import * as path from "node:path";

// A workspace copy or its Game folder.
export function isWorkspace(folder: string): boolean {
    return /^C:\\Games\\THUG2-\d+(\\Game)?$/i.test(path.win32.normalize(folder).replace(/[\\/]+$/, ""));
}

// The ini's text with key=value in [section]: the key's line there replaced, else added under the section's header,
// else the section added at the end. Line ends kept (CRLF if the text has any).
export function setIniValue(text: string, section: string, key: string, value: string): string {
    const nl = text.includes("\r\n") ? "\r\n" : "\n";
    const lines = text.length ? text.split(/\r?\n/) : [];
    const want = section.toLowerCase();
    const keyLine = new RegExp(`^${key.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}\\s*=`, "i");
    let current = "";
    let header = -1;
    for (let i = 0; i < lines.length; i++) {
        const line = lines[i]!.trim();
        const h = /^\[(.+)\]$/.exec(line);
        if (h) {
            current = h[1]!.trim().toLowerCase();
            if (current === want) header = i;
            continue;
        }
        if (current === want && keyLine.test(line)) {
            lines[i] = `${key}=${value}`;
            return lines.join(nl);
        }
    }
    if (header >= 0) {
        lines.splice(header + 1, 0, `${key}=${value}`);
    } else {
        if (lines.length && lines[lines.length - 1] === "") lines.pop();
        lines.push(`[${section}]`, `${key}=${value}`, "");
    }
    return lines.join(nl);
}

// [features] mute_audio=1 in a workspace's thug2-decomp.ini (`folder`: the resolved game folder); true if it's a
// workspace (the ini then says so).
export function ensureWorkspaceMuted(folder: string): boolean {
    if (!isWorkspace(folder)) return false;
    const ini = path.join(folder, "thug2-decomp.ini");
    const text = fs.existsSync(ini) ? fs.readFileSync(ini, "utf8") : "";
    const updated = setIniValue(text, "features", "mute_audio", "1");
    if (updated !== text) fs.writeFileSync(ini, updated, "utf8");
    return true;
}
