// NOT ORIGINAL - where a game's files are in a folder the user names (--thug2, --thps3, --thps4, --thug1, --thaw,
// --game): the install root with a Game subfolder (the stock installs: THUG2\Game, THPS4\Game, THUG1\Game,
// THAW\Game) or the game folder itself (Game's contents at the top - what's left when the launcher, docs and readme
// around it are deleted; PartyMod's THUG2PM installs, the workspaces, THPS3). Each game is told by marker files /
// folders of its data, matched without regard to case; the folder itself is tried first, then its Game folder.
// tools/game/game_exe.ps1's Resolve-Install is the PowerShell twin (the same markers).

import * as fs from "node:fs";
import { pyExit, pyJoin } from "./py.ts";

export type InstallGame = "thug2" | "thps3" | "thps4" | "thug1" | "thaw" | "thps2";

export interface InstallKind {
    title: string; // for messages ("THAW")
    // Any one of these identifies the folder; each is one or more paths ("\"-separated) that must all be under it.
    markers: readonly (readonly string[])[];
}

export const INSTALLS: Readonly<Record<InstallGame, InstallKind>> = {
    // the exe (PartyMod's THUG2PM.exe or a stock no-CD THUG2.exe), or the data alone (the converters only read it)
    thug2: { title: "THUG2", markers: [["THUG2PM.exe"], ["THUG2.exe"], ["Data\\pre\\qb_scripts.prx"]] },
    thps3: { title: "THPS3", markers: [["Data\\Levels"]] },
    // (skeletons: THPS3 has a Data\Levels too, but no skeletons)
    thps4: { title: "THPS4", markers: [["data\\levels", "data\\skeletons"]] },
    // (qb.pre: THUG1's scripts - THUG2's Data\pre has qb_scripts.prx instead)
    thug1: { title: "THUG1", markers: [["Data\\pre\\qb.pre"]] },
    thaw: { title: "THAW", markers: [["data\\worlds\\worldzones"], ["data\\pak\\qb.pak.wpc"]] },
    // (THPS2 PC: every data file in one archive)
    thps2: { title: "THPS2", markers: [["ALL.PKR"]] },
};

const SUBFOLDER = "Game";

function entries(dir: string): string[] | null {
    try {
        return fs.readdirSync(dir);
    } catch {
        return null;
    }
}

// A path under `folder`, each part matched without regard to case (the name as it is on disk); null when it isn't
// there.
export function findCaseless(folder: string, ...parts: string[]): string | null {
    let p = folder;
    for (const part of parts) {
        const match = entries(p)?.find((e) => e.toLowerCase() === part.toLowerCase());
        if (match === undefined) return null;
        p = pyJoin(p, match);
    }
    return p;
}

function trimSeparators(dir: string): string {
    const trimmed = dir.replace(/[\\/]+$/, "");
    // ("C:\" stays: "C:" alone is the drive's current folder)
    return trimmed === "" || /^[A-Za-z]:$/.test(trimmed) ? dir : trimmed;
}

function hasMarker(dir: string, game: InstallGame): boolean {
    return INSTALLS[game].markers.some((all) => all.every((m) => findCaseless(dir, ...m.split("\\")) !== null));
}

// The folder holding the game's files: `dir` itself or its Game folder; null when neither has the game's markers.
export function findInstall(dir: string, game: InstallGame): string | null {
    const folder = trimSeparators(dir);
    if (hasMarker(folder, game)) return folder;
    const sub = findCaseless(folder, SUBFOLDER);
    if (sub !== null && hasMarker(sub, game)) return sub;
    return null;
}

// "<dir> doesn't look like a THAW install: expected data\worlds\worldzones or data\pak\qb.pak.wpc in it or in its
// Game folder"
export function installError(dir: string, game: InstallGame): string {
    const kind = INSTALLS[game];
    const what = kind.markers.map((all) => all.join(" and ")).join(" or ");
    return `${dir} doesn't look like a ${kind.title} install: expected ${what} in it or in its ${SUBFOLDER} folder`;
}

const resolved = new Map<string, string>(); // (game + "|" + dir -> folder: the converters ask per file)

// findInstall, or exit with installError's message (SystemExit: a CLI's runMain prints it). Asking again with the
// folder it returned gives the same folder, so a function may resolve what it is handed whether or not its caller
// did.
export function resolveInstall(dir: string, game: InstallGame): string {
    const key = `${game}|${dir}`;
    const known = resolved.get(key);
    if (known !== undefined) return known;
    const folder = findInstall(dir, game);
    if (folder === null) pyExit(installError(dir, game));
    resolved.set(key, folder);
    return folder;
}
