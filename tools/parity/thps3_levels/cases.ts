// NOT ORIGINAL - tools/pipeline/thps3_levels' parity cases (tools/parity/parity.ts): thps3_levels.py against thps3_levels.ts
// on the user's THPS3 and THUG2, every file written compared (the .qb in wrap.py's stable form) and stdout.
//
//   node tools/parity/parity.ts tools/parity/thps3_levels/cases.ts [--only selfcheck] [--jobs 3]
//
// Nothing is written into the game folders (convert's --out is the case's scratch folder). The Python converter was
// deleted once these all agreed (docs/typescript.md): with it gone the cases are left out - check out commit c121a28
// (the last with it) to run them again.

import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import type { Case } from "../parity.ts";

const TOOLS = path.dirname(path.dirname(path.dirname(fileURLToPath(import.meta.url))));
const THUG2 = "C:\\Games\\THUG2\\Game";
const THPS3 = "C:\\Games\\THPS3";
const NS = "C:\\Dev\\thug2-decomp\\out\\neverscript\\ns.exe";

function wrapped(name: string, args: string[], extra: Partial<Case> = {}): Case {
    return {
        name,
        python: ["tools/parity/thps3_levels/wrap.py", "{out}", ...args],
        ts: ["tools/parity/thps3_levels/wrap.ts", "{out}", ...args],
        ...extra,
    };
}

export default function cases(): Case[] {
    if (!fs.existsSync(path.join(TOOLS, "thps3_levels", "thps3_levels.py"))) return [];
    if (!fs.existsSync(THPS3) || !fs.existsSync(THUG2)) return [];
    const convert = (...more: string[]): string[] => ["convert", "--out", "{out}\\staging", "--ns", NS, ...more];
    return [
        // every default level, all options as they are
        wrapped("convert default", convert()),
        // the park editor's (not converted by default), an unknown name, the THUG2 folder given without its Game
        wrapped("convert park editor", [
            ...convert("Sk3Ed_Bch", "Sk3Ed_Indr", "Sk3Ed_Schl", "Nope", "foun"),
            "--thug2",
            "C:\\Games\\THUG2",
        ]),
        // static levels: no scripts, objects, waypoints
        wrapped("convert no-scripts", convert("--no-scripts")),
        // no .dff objects, no mip levels
        wrapped("convert no-objects no-mips", convert("--no-objects", "--no-mips")),
        // no NeverScript: sys.exit's message, exit 1
        wrapped("convert without ns", ["convert", "--out", "{out}\\staging", "--ns", "{out}\\missing.exe"], {
            nonzeroOk: true,
        }),
        wrapped("selfcheck", ["selfcheck"]),
        wrapped("selfcheck extra", ["selfcheck", "--thug2", "C:\\Games\\THUG2", "--extra", "BOscn.prx", "BOcol.prx"]),
    ];
}
