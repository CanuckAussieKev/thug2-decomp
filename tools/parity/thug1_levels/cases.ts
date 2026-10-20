// NOT ORIGINAL - tools/thug1_levels' parity cases (tools/parity/parity.ts): thug1_levels.py and thug1_levels.ts run on
// the user's THUG1 and THUG2 (read only), each converting into its own scratch folder; every file written is compared
// byte for byte (the .qb files with their name entries sorted: tools/parity/converters/qb_norm.*), and stdout.
//
//   node tools/parity/parity.ts tools/parity/thug1_levels/cases.ts [--only convert] [--jobs 3]
//
// Cases whose game folders aren't there are left out.

import * as fs from "node:fs";
import type { Case } from "../parity.ts";

const THUG2 = "C:\\Games\\THUG2";
const THUG1 = "C:\\Games\\THUG1";
const NS = "C:\\Dev\\thug2-decomp\\out\\neverscript\\ns.exe";
const PY = "tools/thug1_levels/thug1_levels.py";
const TS = "tools/thug1_levels/thug1_levels.ts";

// A convert run (through qb_norm: the .qb files normalised, the out folder's path in stdout as "<out>").
function convert(name: string, args: string[], extra: Partial<Case> = {}): Case {
    const all = ["convert", "--thug1", THUG1, "--ns", NS, "--out", "{out}", ...args];
    return {
        name,
        python: ["tools/parity/converters/qb_norm.py", "{out}", PY, ...all],
        ts: ["tools/parity/converters/qb_norm.ts", "{out}", TS, ...all],
        ...extra,
    };
}

export default function cases(): Case[] {
    // (the Python twin was deleted once these all passed - commit 4255a5c: the cases stay as the record)
    if (!fs.existsSync(PY) || !fs.existsSync(THUG1) || !fs.existsSync(THUG2) || !fs.existsSync(NS)) return [];
    return [
        // the default levels, with their scripts, peds, vehicles, objects and particles
        convert("convert all", []),
        // only geometry, restarts, rails and kill zones
        convert("convert static", ["--static"]),
        // the levels not converted by default (the front end's shop, the bedroom, the test level); codes in any case;
        // --thug2 without the Game folder
        convert("convert others", ["SKATESHOP", "cas_bedroom", "TestLevel", "--thug2", THUG2]),
        // a code the tool doesn't know after one it does: sys.exit after New Jersey's files, exit 1
        convert("convert unknown", ["nj", "XX"], { nonzeroOk: true }),
        // no NeverScript: sys.exit's message, exit 1
        convert("convert no ns", ["--ns", "C:\\nowhere\\ns.exe"], { nonzeroOk: true }),
        { name: "selfcheck", python: [PY, "selfcheck", "--thug1", THUG1], ts: [TS, "selfcheck", "--thug1", THUG1] },
        {
            name: "selfcheck extra",
            python: [PY, "selfcheck", "--thug2", THUG2, "--extra", "SCscn.prx", "SCcol.prx"],
            ts: [TS, "selfcheck", "--thug2", THUG2, "--extra", "SCscn.prx", "SCcol.prx"],
        },
    ];
}
