// NOT ORIGINAL - the small tools' parity cases (tools/parity/parity.ts): each Python tool of stage 4 (tools/*.py,
// tools/pipeline/game/*.py) against its TypeScript twin on the repo's data, the exes and the game folders (read only), stdout,
// exit code and every file written compared.
//
//   node tools/parity/parity.ts tools/parity/small_tools/cases.ts [--only <tool>] [--jobs 4]
//
// Tools that write into the repo (docs/PROGRESS.md, the progress notes, src/game/front/automation_tables.inc,
// sources under mutation) run in sandbox.ts's scratch copies; setup.ts makes the synthetic inputs (BPS patches, a
// crash log, mutate's stand-in checker and build, pending records). The Python twins were deleted once these all
// agreed (43/43): with them gone the cases are left out - check out commit e70590c (the last with them) to run them
// again. Known difference: mutate.py left a mutated LF file with CRLF line ends (Python's text mode); mutate.ts puts
// the original bytes back (the checkout's sources are CRLF, so the same there).

import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { REFERENCE_EXE } from "../../pipeline/lib/decomp.ts";
import type { Case } from "../parity.ts";

const REPO = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..", "..");
const HERE = "tools/parity/small_tools";
const PM = REFERENCE_EXE; // (the reference build: local.json)
const STOCK = "C:\\Games\\THUG2\\Game\\THUG2.exe";
const BACKUP_EXE = "C:\\Games\\THUG2-BACKUP\\Game\\THUG2.exe";
const NS = "C:\\Dev\\thug2-decomp\\out\\neverscript\\ns.exe";
const FUNCS = "data/derived/functions.json";

// the same command for both: tools/<tool>.py and tools/<tool>.ts
function direct(name: string, tool: string, args: string[], extra: Partial<Case> = {}): Case {
    return { name, python: [`${tool}.py`, ...args], ts: [`${tool}.ts`, ...args], ...extra };
}

// in a sandbox (sandbox.ts): sandbox options, then the tool's arguments
function boxed(name: string, tool: string, box: string[], args: string[], extra: Partial<Case> = {}): Case {
    const run = (lang: string, ext: string): string[] => [
        `${HERE}/sandbox.ts`,
        "{out}",
        ...box,
        "--",
        lang,
        `${tool}.${ext}`,
        ...args,
    ];
    return {
        name,
        // (the sandbox is a Node script: Python only starts it, so both sides run the same way)
        python: [
            "-c",
            "import subprocess,sys; sys.exit(subprocess.call(sys.argv[1:]))",
            process.execPath,
            ...run("python", "py"),
        ],
        ts: run("node", "ts"),
        ...extra,
    };
}

const setup = (what: string): string[] => [`${HERE}/setup.ts`, "{in}", what];

export default function cases(): Case[] {
    if (!fs.existsSync(path.join(REPO, "tools", "progress_report.py"))) return [];
    const out: Case[] = [];

    // bps_dump
    for (const f of ["patch.bps", "empty.bps", "quote.bps"]) {
        out.push(direct(`bps_dump ${f}`, "tools/bps_dump", [`{in}\\${f}`], { setup: setup("bps") }));
    }
    out.push(
        direct("bps_dump not a patch", "tools/bps_dump", ["{in}\\notbps.bin"], {
            setup: setup("bps"),
            nonzeroOk: true,
        }),
    );

    // check_code_writes (the repo's sources; a write moved into a rewritten function; one write fewer)
    const codeBox = ["--copy", "src/game", "--copy", FUNCS];
    out.push(boxed("check_code_writes", "tools/check_code_writes", codeBox, []));
    out.push(
        boxed(
            "check_code_writes overlap",
            "tools/check_code_writes",
            [...codeBox, "--put", "{in}\\overlap.cpp=src/game/net/online_hosts.cpp"],
            [],
            { setup: setup("codewrites"), nonzeroOk: true },
        ),
    );
    out.push(
        boxed(
            "check_code_writes count",
            "tools/check_code_writes",
            [...codeBox, "--put", "{in}\\short.cpp=src/game/net/online_hosts.cpp"],
            [],
            { setup: setup("codewrites"), nonzeroOk: true },
        ),
    );

    // extract_cfuncs, find_missed_functions, map_units: both builds of the exe
    for (const [label, exe] of [
        ["pm", PM],
        ["stock", STOCK],
    ] as const) {
        out.push(direct(`extract_cfuncs ${label}`, "tools/extract_cfuncs", [exe, "{out}\\cfuncs.csv"]));
        out.push(
            boxed(
                `find_missed_functions ${label}`,
                "tools/find_missed_functions",
                ["--copy", FUNCS],
                [exe, FUNCS, "missed.txt"],
            ),
        );
        out.push(direct(`map_units ${label}`, "tools/map_units", [exe, FUNCS, "{out}\\units.csv"]));
    }

    // fold_pending: into a copy of notes/docs/decomp-progress.md (the private notes repo)
    const foldBox = ["--copy", "notes/docs/decomp-progress.md", "--put", "{in}\\front.md=docs/pending/front.md"];
    const fold = (name: string, box: string[], args: string[], extra: Partial<Case> = {}): Case =>
        boxed(name, "tools/fold_pending", box, args, { setup: setup("pending"), ...extra });
    out.push(fold("fold_pending", foldBox, ["docs/pending/front.md", "FE", "front-end agent", "--date", "2026-10-19"]));
    out.push(fold("fold_pending today", foldBox, ["docs\\pending\\front.md", "FE", "agent $1 \\1"]));
    out.push(
        fold(
            "fold_pending crlf",
            ["--copy", "notes/docs/decomp-progress.md", "--put", "{in}\\crlf.md=crlf.md"],
            ["crlf.md", "A.B", "dotted", "--progress", "notes/docs/decomp-progress.md", "--date", "x"],
        ),
    );
    out.push(fold("fold_pending no entry", foldBox, ["docs/pending/front.md", "ZZ", "nobody"], { nonzeroOk: true }));
    out.push(fold("fold_pending usage", foldBox, ["docs/pending/front.md"], { nonzeroOk: true }));

    // mutate: a stand-in checker and build in the sandbox (no real build)
    const mutateBox = [
        "--put",
        "{in}\\thug2_check.exe=out/build/verify/thug2_check.exe",
        "--put",
        "{in}\\build.cmd=build.cmd",
        "--put",
        "{in}\\victim.cpp=victim.cpp",
        "--put",
        "{in}\\victim2.cpp=victim2.cpp",
    ];
    const mutate = (name: string, spec: string, names: string[], extra: Partial<Case> = {}, tsSpec = "ts"): Case => {
        const c = boxed(name, "tools/mutate", mutateBox, [], { setup: setup("mutate"), ...extra });
        const specFor = (ext: string): string => path.join(REPO, HERE, `${spec}.${ext}`);
        c.python.push(specFor("py"), ...names);
        c.ts.push(specFor(tsSpec), ...names);
        return c;
    };
    out.push(mutate("mutate all", "mutate_spec", []));
    out.push(mutate("mutate some", "mutate_spec", ["caught", "build-fails", "nope"]));
    out.push(mutate("mutate twice", "mutate_twice", [], { nonzeroOk: true }));
    // (mutate.ts also takes the Python specs of mutate.py's days)
    out.push(mutate("mutate python spec", "mutate_spec", ["caught", "missed", "crash"], {}, "py"));

    // progress_report: docs/PROGRESS.md from the repo's data and sources (the date: today on both sides)
    out.push(
        boxed(
            "progress_report",
            "tools/progress_report",
            ["--copy", "src/game", "--copy", "data/subsystems.csv", "--copy", FUNCS, "--mkdir", "docs"],
            [],
        ),
    );

    // symbolize: offsets, a log's last report, a log without one, a bad offset; the default --pdb-dir
    const symBox = [
        "--put",
        "{in}\\pdb\\dinput8.dll=out/build/verify/dinput8.dll",
        "--put",
        "{in}\\pdb\\dinput8.pdb=out/build/verify/dinput8.pdb",
    ];
    const sym = (name: string, args: string[], extra: Partial<Case> = {}): Case =>
        boxed(name, "tools/symbolize", symBox, args, { setup: setup("symbols"), ...extra });
    out.push(
        sym("symbolize offsets", ["11234", "0x22468", "3369C", "77f6c", "891A0", "180000", "100000", "1000", "7FFFFF"]),
    );
    out.push(sym("symbolize log", ["{in}\\crash.log", "55B04"]));
    out.push(sym("symbolize pdb-dir", ["--pdb-dir", "{in}\\pdb", "{in}\\crash.log"]));
    out.push(sym("symbolize no report", ["{in}\\nocrash.log"], { nonzeroOk: true }));
    out.push(sym("symbolize bad offset", ["zz"], { nonzeroOk: true }));

    // boot_level: a copy of the main install's menu overlay script
    const qb = "thug2-decomp-data\\front_end\\levels\\mainmenu\\mainmenu_scripts.qb";
    out.push(
        boxed(
            "boot_level",
            "tools/pipeline/game/boot_level",
            [
                "--put",
                `C:\\Games\\THUG2\\Game\\${qb}=game/${qb}`,
                "--put",
                `${NS}=out/neverscript/ns.exe`,
                "--qbnorm",
                `game/${qb}`,
            ],
            ["thps3_foun", "game"],
        ),
    );

    // exe_audit: the alternate THUG2.exe build against the reference build both ways, one against itself, a
    // different layout (the backup install's exe)
    const auditBox = ["--copy", FUNCS, "--copy", "src/game"];
    out.push(boxed("exe_audit", "tools/pipeline/game/exe_audit", auditBox, []));
    out.push(boxed("exe_audit swapped", "tools/pipeline/game/exe_audit", auditBox, ["--a", STOCK, "--b", PM]));
    out.push(boxed("exe_audit same", "tools/pipeline/game/exe_audit", auditBox, ["--a", PM, "--b", PM]));
    out.push(
        boxed("exe_audit layouts", "tools/pipeline/game/exe_audit", auditBox, ["--b", BACKUP_EXE], { nonzeroOk: true }),
    );

    // install_manifest: make (the main install's Game folder, ~3 GB), check the installs against the repo's manifest
    const manifest = path.join(REPO, "notes", "data", "install_manifest.json");
    out.push(boxed("install_manifest make", "tools/pipeline/game/install_manifest", [], ["make", "--out", "m.json"]));
    out.push(
        boxed(
            "install_manifest make default",
            "tools/pipeline/game/install_manifest",
            [],
            ["make", "--game", "C:\\Games\\THUG2-BACKUP"],
        ),
    );
    for (const [label, game] of [
        ["main", "C:\\Games\\THUG2"],
        ["pm", path.dirname(PM)],
        ["original", "C:\\Games\\THUG2-BACKUP"],
    ] as const) {
        out.push(
            boxed(
                `install_manifest check ${label}`,
                "tools/pipeline/game/install_manifest",
                [],
                ["check", "--game", game, "--manifest", manifest],
                {
                    nonzeroOk: true,
                },
            ),
        );
    }
    out.push(
        boxed(
            "install_manifest check list",
            "tools/pipeline/game/install_manifest",
            [],
            ["check", "--game", "C:\\Games\\THUG2-BACKUP", "--manifest", manifest, "--list", "3"],
            {
                nonzeroOk: true,
            },
        ),
    );
    out.push(
        boxed(
            "install_manifest not a folder",
            "tools/pipeline/game/install_manifest",
            [],
            ["check", "--game", "C:\\Games\\nope", "--manifest", manifest],
            { nonzeroOk: true },
        ),
    );

    // make_automation_tables: the main install's scripts, the menu level's ids (make_menu_level.py)
    const tablesBox = [
        "--mkdir",
        "src/game/front",
        "--copy",
        "tools/pipeline/menu_level/make_menu_level.py",
        "--tiesort",
        "src/game/front/automation_tables.inc",
    ];
    out.push(
        boxed("make_automation_tables", "tools/pipeline/game/make_automation_tables", tablesBox, [
            "--game",
            "C:\\Games\\THUG2",
            "--ns",
            NS,
        ]),
    );
    out.push(boxed("make_automation_tables defaults", "tools/pipeline/game/make_automation_tables", tablesBox, []));
    out.push(
        boxed("make_automation_tables game data", "tools/pipeline/game/make_automation_tables", tablesBox, [
            "--game",
            "C:\\Games\\THUG2\\Game\\Data",
        ]),
    );
    return out;
}
