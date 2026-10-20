// NOT ORIGINAL - tools/thaw_levels' parity cases (tools/parity/parity.ts): the THAW converter's convert (all 15 levels
// with each option), selfcheck, thugpro_compare and migrate_layouts, Python against TypeScript on the user's game data.
//
//   node tools/parity/parity.ts tools/parity/thaw_levels/cases.ts [--only convert] [--jobs 4]
//
// Each side runs through run.py / run.ts: the compiled .qb files compared as qbnorm.ts's text (ns.exe writes its
// checksum names in a different order every run), the side's own output folder written "{out}" in its stdout. Reads
// THAW (C:\Games\THAW), the workspace C:\Games\THUG2-4 (its THUG2 data: never written) and THUGPRO's install (read
// only); writes only into the harness's scratch folders. Without THAW, or once the Python twins are gone (they were
// deleted when the port was proven: check out the commit before that to run these again), there are no cases.

import * as fs from "node:fs";
import * as path from "node:path";
import type { Case } from "../parity.ts";

const THAW = "C:\\Games\\THAW";
const WORKSPACE = "C:\\Games\\THUG2-4"; // (read only: its Data, for THUG2's commands, models and particles)
const MAIN = "C:\\Games\\THUG2"; // (read only: the stock install's Game subfolder step-in)
const THUGPRO = path.join(process.env.LOCALAPPDATA ?? "", "THUG Pro");
const HERE = path.dirname(import.meta.filename);
const REPO = path.dirname(path.dirname(path.dirname(HERE)));
// ns.exe: this checkout's, else the main checkout's (worktrees don't build their own)
const NS = [path.join(REPO, "out", "neverscript", "ns.exe"), "C:\\Dev\\thug2-decomp\\out\\neverscript\\ns.exe"].find(
    (p) => fs.existsSync(p),
);

const PY_RUN = "tools/parity/thaw_levels/run.py";
const TS_RUN = "tools/parity/thaw_levels/run.ts";

// A tool run on both sides: `tool` without its extension (tools/thaw_levels/thaw_levels).
function tool(name: string, file: string, args: string[], extra: Partial<Case> = {}, copy: string[] = []): Case {
    return {
        name,
        python: [PY_RUN, "{out}", ...copy, `${file}.py`, ...args],
        ts: [TS_RUN, "{out}", ...copy, `${file}.ts`, ...args],
        ...extra,
    };
}

export default function cases(): Case[] {
    if (!fs.existsSync(THAW) || !fs.existsSync(path.join(REPO, "tools", "thaw_levels", "thaw_levels.py")) || !NS) {
        return [];
    }
    const out: Case[] = [];
    const levels = "tools/thaw_levels/thaw_levels";
    const convert = (name: string, args: string[], extra: Partial<Case> = {}, thug2 = WORKSPACE): Case =>
        tool(name, levels, ["convert", ...args, "--thug2", thug2, "--out", "{out}", "--ns", NS], extra);

    // convert: all 15 levels with each option, subsets, the Game subfolder step-in, errors
    out.push(convert("convert default", []));
    out.push(convert("convert compat none", ["--compat", "none"]));
    out.push(convert("convert frame recentre", ["--frame", "recentre"]));
    out.push(convert("convert legacy-names", ["--legacy-names"]));
    out.push(convert("convert static", ["--static"]));
    out.push(
        convert("convert subset recentre 5000", ["Z_MA", "SV2", "z_oi", "--frame", "recentre", "--recentre", "5000"]),
    );
    out.push(
        convert("convert subset static legacy none", ["CH", "Z_SV", "--static", "--legacy-names", "--compat=none"]),
    );
    out.push(
        convert("convert subset compat none recentre", ["EL", "SM", "SV", "--compat", "none", "--frame=recentre"]),
    );
    if (fs.existsSync(path.join(MAIN, "Game", "Data")))
        out.push(convert("convert game step-in", ["JA", "DN"], {}, MAIN));
    out.push(convert("convert unknown level", ["Z_MA", "Z_XX"], { nonzeroOk: true }));
    out.push(
        tool("convert no ns", levels, ["convert", "--thug2", WORKSPACE, "--out", "{out}", "--ns", "{out}\\no-ns.exe"], {
            nonzeroOk: true,
        }),
    );
    out.push(
        tool("convert bad frame", levels, ["convert", "--frame", "other"], {
            nonzeroOk: true,
            stdout: "ignore",
        }),
    );

    // selfcheck
    out.push(tool("selfcheck", levels, ["selfcheck", "--thug2", WORKSPACE]));
    if (fs.existsSync(path.join(MAIN, "Game", "Data")))
        out.push(tool("selfcheck step-in", levels, ["selfcheck", "--thug2", MAIN]));

    // thugpro_compare: our levels (converted once, by the Python twin, into the shared input) against THUGPRO's
    const compare = "tools/thaw_levels/thugpro_compare";
    const staging = [
        "tools/thaw_levels/thaw_levels.py",
        "convert",
        "--thug2",
        WORKSPACE,
        "--out",
        "{in}\\staging",
        "--ns",
        NS,
    ];
    if (fs.existsSync(THUGPRO)) {
        out.push(
            tool(
                "thugpro_compare all",
                compare,
                ["--staging", "{in}\\staging\\levels", "--thugpro", THUGPRO, "--json", "{out}\\compare.json"],
                { setup: staging, nonzeroOk: true },
            ),
        );
        out.push(
            tool(
                "thugpro_compare subset",
                compare,
                ["CH", "z_ma", "OI", "sv", "--staging", "{in}\\staging\\levels", "--json", "{out}\\compare.json"],
                { setup: staging, nonzeroOk: true },
            ),
        );
    }
    out.push(
        tool(
            "thugpro_compare not converted",
            compare,
            ["--thug2", "{in}", "--thugpro", THUGPRO, "--json", "{out}\\compare.json"],
            { nonzeroOk: true },
        ),
    );

    // migrate_layouts on made-up layouts (make_layouts.py), each side its own copy
    const migrate = "tools/thaw_levels/migrate_layouts";
    const layouts = ["tools/parity/thaw_levels/make_layouts.py", "{in}"];
    const copy = ["--copy", "{in}\\game", "{out}\\game"];
    out.push(tool("migrate dry-run", migrate, ["--game", "{out}\\game", "--dry-run"], { setup: layouts }, copy));
    out.push(tool("migrate", migrate, ["--game", "{out}\\game"], { setup: layouts }, copy));
    out.push(tool("migrate subset", migrate, ["MA", "z_oi", "--game", "{out}\\game"], { setup: layouts }, copy));
    out.push(
        tool(
            "migrate recentre 5000",
            migrate,
            ["--game", "{out}\\game", "--recentre", "5000"],
            { setup: layouts },
            copy,
        ),
    );
    out.push(
        tool(
            "migrate bad scene",
            migrate,
            ["--game", "{out}\\game"],
            { setup: [...layouts, "bad"], nonzeroOk: true },
            copy,
        ),
    );
    out.push(tool("migrate no layouts", migrate, ["--game", "{out}\\nothing"]));
    return out;
}
