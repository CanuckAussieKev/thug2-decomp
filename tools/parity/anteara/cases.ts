// NOT ORIGINAL - tools/anteara's parity cases (tools/parity/parity.ts): anteara.py and anteara.ts run through
// tools/parity/menu_level/wrap.ts (a game folder of junctions to a workspace's files in each side's scratch folder;
// the .qb files compared with their name entries sorted). install writes the mod's script set; extract remakes mod\
// from the user's merged mod folder (a copy of the tool in the scratch folder: it writes next to itself); check
// compares an installed set with the merged mod (stdout compared sorted: Python writes its diff lines straight to
// the byte stream, ahead of its buffered text). The installs with the overlay's edits are make_menu_level's cases.
//
//   node tools/parity/parity.ts tools/parity/anteara/cases.ts [--only <text>]
//
// Cases whose folders aren't there are left out.

import * as fs from "node:fs";
import type { Case } from "../parity.ts";

const WS1 = "C:\\Games\\THUG2-1";
const UNIFIED = "C:\\Games\\THUG2_PM_BAK\\Backup\\scripts_unified";
const NS = "C:\\Dev\\thug2-decomp\\out\\neverscript\\ns.exe";
const PY = "tools/anteara/anteara.py";
const TS = "tools/anteara/anteara.ts";
const WRAP = "tools/parity/menu_level/wrap.ts";
const PYWRAP = `import subprocess, sys; sys.exit(subprocess.call(["node", "${WRAP}", *sys.argv[1:]]))`;

function run(name: string, layout: string, args: string[], extra: Partial<Case> = {}): Case {
    return {
        name,
        python: ["-c", PYWRAP, "{out}", layout, PY, ...args],
        ts: [WRAP, "{out}", layout, TS, ...args],
        ...extra,
    };
}

export default function cases(): Case[] {
    // (the Python twin was deleted once these all passed: the cases stay as the record - see docs/typescript.md)
    if (!fs.existsSync(PY) || !fs.existsSync(WS1) || !fs.existsSync(NS)) return [];
    const out = [
        // the mod's set alone (no overlay edits), into --overlay
        run("install", `ws=${WS1};stagings=none`, [
            "install",
            "--game",
            "{game}",
            "--overlay",
            "{out}\\overlay",
            "--ns",
            NS,
        ]),
        // the default overlay folder, the game in a Game subfolder
        run("install sub", `ws=${WS1};stagings=none;sub=1`, ["install", "--game", "{game}", "--ns", NS]),
        // no loose scripts: None
        run("install no scripts", `ws=${WS1};stagings=none;scripts=0`, [
            "install",
            "--game",
            "{game}",
            "--overlay",
            "{out}\\overlay",
            "--ns",
            NS,
        ]),
    ];
    if (fs.existsSync(UNIFIED)) {
        out.push(
            run("extract", `ws=${WS1};stagings=none;tools=1`, [
                "extract",
                "--unified",
                UNIFIED,
                "--game",
                "{game}",
                "--ns",
                NS,
            ]),
            run(
                "check",
                `ws=${WS1};stagings=none`,
                ["check", "--unified", UNIFIED, "--overlay", `${WS1}\\thug2-decomp-data`, "--ns", NS],
                { stdout: "sorted" },
            ),
        );
    }
    return out;
}
