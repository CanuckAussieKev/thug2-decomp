// NOT ORIGINAL - tools/thps4_levels' parity cases (tools/parity/parity.ts): thps4_levels.py and thps4_levels.ts run on
// the user's THPS4 and THUG2 (read only), each converting into its own scratch folder; every file written is compared
// byte for byte (the .qb files with their name entries sorted: tools/parity/converters/qb_norm.*), and stdout.
//
//   node tools/parity/parity.ts tools/parity/thps4_levels/cases.ts [--only convert] [--jobs 3]
//
// Cases whose game folders aren't there are left out.

import * as fs from "node:fs";
import type { Case } from "../parity.ts";

const THUG2 = "C:\\Games\\THUG2";
const THPS4 = "C:\\Games\\THPS4";
const NS = "C:\\Dev\\thug2-decomp\\out\\neverscript\\ns.exe";
const PY = "tools/thps4_levels/thps4_levels.py";
const TS = "tools/thps4_levels/thps4_levels.ts";

// A convert run (through qb_norm: the .qb files normalised, the out folder's path in stdout as "<out>").
function convert(name: string, args: string[], extra: Partial<Case> = {}): Case {
    const all = ["convert", "--thps4", THPS4, "--ns", NS, "--out", "{out}", ...args];
    return {
        name,
        python: ["tools/parity/converters/qb_norm.py", "{out}", PY, ...all],
        ts: ["tools/parity/converters/qb_norm.ts", "{out}", TS, ...all],
        ...extra,
    };
}

export default function cases(): Case[] {
    // (the Python twin was deleted once these all passed - commit 4255a5c: the cases stay as the record)
    if (!fs.existsSync(PY) || !fs.existsSync(THPS4) || !fs.existsSync(THUG2) || !fs.existsSync(NS)) return [];
    return [
        // every level, scripts, peds, cars and vehicles' models
        convert("convert all", []),
        // the static levels (no scripts, game objects or vehicles)
        convert("convert no-scripts", ["--no-scripts"]),
        // some levels, in another order (the signs' numbers go by it), one not there; --thug2 without the Game folder
        convert("convert some", ["SkateShop", "zoo", "Foo2", "Sch", "--thug2", THUG2]),
        // the park editor's set (not a default level), then Foo: a node array without a scene (FormatError, exit 1)
        convert("convert park editor and foo", ["Sk4Ed", "Foo"], { nonzeroOk: true }),
        // no NeverScript: sys.exit's message, exit 1
        convert("convert no ns", ["--ns", "C:\\nowhere\\ns.exe"], { nonzeroOk: true }),
        { name: "selfcheck", python: [PY, "selfcheck", "--thps4", THPS4], ts: [TS, "selfcheck", "--thps4", THPS4] },
        {
            name: "selfcheck extra",
            python: [PY, "selfcheck", "--thug2", THUG2, "--extra", "SCscn.prx", "SCcol.prx"],
            ts: [TS, "selfcheck", "--thug2", THUG2, "--extra", "SCscn.prx", "SCcol.prx"],
        },
    ];
}
