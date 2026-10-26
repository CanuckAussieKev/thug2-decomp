// NOT ORIGINAL - tools/pipeline/menu_level's parity cases (tools/parity/parity.ts): make_menu_level.py and make_menu_level.ts
// (and level_select_icons' preview) run on game folders wrap.ts makes in each side's scratch folder from a workspace's
// files (junctions: nothing is written into the workspace), each writing its own overlay; every file written is
// compared byte for byte (the .qb files with their name entries sorted: tools/parity/converters/qb_norm.ts), and
// stdout (the scratch folder's path as "<out>"). The unit cases (driver.py / driver.ts) dump CREATE 100% CAS's data
// (story goals, classic goal names, every gap with its position), the level select's map positions, sections and
// edits, and every pictogram's sign.
//
//   node tools/parity/parity.ts tools/parity/menu_level/cases.ts [--only <text>] [--jobs 3]
//
// Cases whose workspaces aren't there are left out.

import * as fs from "node:fs";
import type { Case } from "../parity.ts";

const WS1 = "C:\\Games\\THUG2-1";
const WS2 = "C:\\Games\\THUG2-2";
const NS = "C:\\Dev\\thug2-decomp\\out\\neverscript\\ns.exe";
const PY = "tools/pipeline/menu_level/make_menu_level.py";
const TS = "tools/pipeline/menu_level/make_menu_level.ts";
const WRAP = "tools/parity/menu_level/wrap.ts";
// (the Python side runs the same wrapper through node: python -I -c <this> <wrap.ts arguments>)
const PYWRAP = `import subprocess, sys; sys.exit(subprocess.call(["node", "${WRAP}", *sys.argv[1:]]))`;

// A run of make_menu_level in a game folder laid out as `layout` (wrap.ts) with `args` ("{game}": that folder).
function make(name: string, layout: string, args: string[], extra: Partial<Case> = {}): Case {
    return {
        name,
        python: ["-c", PYWRAP, "{out}", layout, PY, ...args],
        ts: [WRAP, "{out}", layout, TS, ...args],
        ...extra,
    };
}

const STD = ["--game", "{game}", "--ns", NS];

export default function cases(): Case[] {
    // (the Python twin was deleted once these all passed - commit 57d7b5e, the last with it: the cases stay as the record)
    if (!fs.existsSync(PY) || !fs.existsSync(WS1) || !fs.existsSync(NS)) return [];
    const out: Case[] = [
        // every converted game (THPS4's shop: the front end's backdrop), the loose scripts (the Anteara Mod), the
        // overlay at its default place <game>\thug2-decomp-data
        make("make all", `ws=${WS1};stagings=all`, STD),
        // the game in a Game subfolder (stepped into), --out
        make("make sub out", `ws=${WS1};stagings=all;sub=1`, [...STD, "--out", "{out}\\overlay"]),
        // no converted levels: THUG2's section only, no backdrop
        make("make no converted", `ws=${WS1};stagings=none`, STD),
        // THUG1's only (a section, no backdrop); THPS4's only (the backdrop); THPS3 and THAW
        make("make thug1 only", `ws=${WS1};stagings=thug1`, STD),
        make("make thps4 only", `ws=${WS1};stagings=thps4`, STD),
        make("make thps3 thaw", `ws=${WS1};stagings=thps3,thaw`, STD),
        // no Data\scripts: the Anteara Mod not installed
        make("make no loose scripts", `ws=${WS1};stagings=none;scripts=0`, STD),
        // Default's scene installed: its level select entry kept
        make("make default installed", `ws=${WS1};stagings=none;default=1`, STD),
        // an overlay of ours from before (emptied and made again)
        make("make existing ours", `ws=${WS1};stagings=none;existing=ours`, STD),
        // a folder with someone else's file: refused, exit 1
        make("make existing foreign", `ws=${WS1};stagings=none;existing=foreign`, STD, { nonzeroOk: true }),
        // no NeverScript: exit 1
        make("make no ns", `ws=${WS1};stagings=none`, ["--game", "{game}", "--ns", "C:\\nowhere\\ns.exe"], {
            nonzeroOk: true,
        }),
        // the level select's sign preview (every pictogram; a few)
        {
            name: "icons preview all",
            python: [
                "-c",
                PYWRAP,
                "{out}",
                `ws=${WS1};stagings=none`,
                "tools/pipeline/menu_level/level_select_icons.py",
                "{out}\\signs.png",
                "--game",
                "{game}",
            ],
            ts: [
                WRAP,
                "{out}",
                `ws=${WS1};stagings=none`,
                "tools/pipeline/menu_level/level_select_icons.ts",
                "{out}\\signs.png",
                "--game",
                "{game}",
            ],
        },
        {
            name: "icons preview some",
            python: [
                "-c",
                PYWRAP,
                "{out}",
                `ws=${WS1};stagings=none;sub=1`,
                "tools/pipeline/menu_level/level_select_icons.py",
                "{out}\\some.png",
                "palm",
                "ufo",
                "--game",
                "{game}",
            ],
            ts: [
                WRAP,
                "{out}",
                `ws=${WS1};stagings=none;sub=1`,
                "tools/pipeline/menu_level/level_select_icons.ts",
                "{out}\\some.png",
                "palm",
                "ufo",
                "--game",
                "{game}",
            ],
        },
        // the unit-level dumps (driver.py / driver.ts)
        ...["career", "map", "sections", "signs"].map(
            (what): Case => ({
                name: `unit ${what}`,
                python: ["tools/parity/menu_level/driver.py", what, WS1, NS, "{out}"],
                ts: ["tools/parity/menu_level/driver.ts", what, WS1, NS, "{out}"],
            }),
        ),
    ];
    if (fs.existsSync(WS2)) out.push(make("make all ws2", `ws=${WS2};stagings=all`, STD));
    return out;
}
