// NOT ORIGINAL - tools/level_port's (and tools/compat's) parity cases on the user's game data (tools/parity/parity.ts):
//
//   node tools/parity/parity.ts tools/parity/level_port/cases.ts [--only qb] [--jobs 4]
//
// Cases whose game folders aren't there are left out. Nothing is written into the game folders (read only).

import * as fs from "node:fs";
import * as path from "node:path";
import type { Case } from "../parity.ts";

const THUG2 = "C:\\Games\\THUG2\\Game";
const THUG1 = "C:\\Games\\THUG1\\Game";
const THPS4 = "C:\\Games\\THPS4\\Game";
const THPS3 = "C:\\Games\\THPS3";
const WORKSPACES = ["C:\\Games\\THUG2-1", "C:\\Games\\THUG2-2", "C:\\Games\\THUG2-3", "C:\\Games\\THUG2-4"];
const STAGINGS = ["thug2-decomp-thps4", "thug2-decomp-thug1", "thug2-decomp-thps3", "thug2-decomp-thaw"];

const PY = "tools/parity/level_port/driver.py";
const TS = "tools/parity/level_port/driver.ts";

const exists = (p: string): boolean => fs.existsSync(p);

function list(dir: string, test: (name: string) => boolean): string[] {
    if (!exists(dir)) return [];
    return fs
        .readdirSync(dir)
        .filter(test)
        .sort()
        .map((f) => path.join(dir, f));
}

function walk(dir: string, test: (name: string) => boolean): string[] {
    const out: string[] = [];
    if (!exists(dir)) return out;
    for (const e of fs.readdirSync(dir, { withFileTypes: true }).sort((a, b) => (a.name < b.name ? -1 : 1))) {
        const p = path.join(dir, e.name);
        if (e.isDirectory()) out.push(...walk(p, test));
        else if (test(e.name)) out.push(p);
    }
    return out;
}

function chunks<T>(items: T[], size: number): T[][] {
    const out: T[][] = [];
    for (let i = 0; i < items.length; i += size) out.push(items.slice(i, i + size));
    return out;
}

const low = (s: string): string => s.toLowerCase();
const ends =
    (...suffixes: string[]) =>
    (name: string): boolean =>
        suffixes.some((s) => low(name).endsWith(s));

function driver(name: string, command: string, args: string[], extra: Partial<Case> = {}): Case {
    return { name, python: [PY, command, "{out}", ...args], ts: [TS, command, "{out}", ...args], ...extra };
}

export default function cases(): Case[] {
    const out: Case[] = [];
    const each = (lists: string[][], make: (c: string[], k: number) => Case): void => {
        lists.forEach((c, k) => {
            out.push(make(c, k));
        });
    };
    const pre = path.join(THUG2, "Data", "pre");
    const thug2Archives = list(pre, ends(".prx"));
    const thug1Archives = list(path.join(THUG1, "Data", "pre"), ends(".pre"));
    const stagingFiles = WORKSPACES.flatMap((w) =>
        STAGINGS.flatMap((s) => walk(path.join(w, s, "levels"), () => true)),
    );

    // prx.ts: every archive's files (LZSS), find()
    each(chunks(thug2Archives, 12), (c, k) => driver(`archives thug2 ${k}`, "archives", c));
    each(chunks(thug1Archives, 25), (c, k) => driver(`archives thug1 ${k}`, "archives", c));

    // thug2_scene / thug2_col / tex: read -> write round trips (+ col_check, depalettise)
    const sceneArchives = thug2Archives.filter((p) => /(scn|col)\.prx$/i.test(p) || /skaterparts|mainmenu/i.test(p));
    each(chunks(sceneArchives, 8), (c, k) => driver(`files thug2 archives ${k}`, "files", c));
    const looseLevels = walk(path.join(THUG2, "Data", "levels"), ends(".xbx"));
    each(chunks(looseLevels, 60), (c, k) => driver(`files thug2 loose ${k}`, "files", c));
    const models = walk(path.join(THUG2, "Data", "models"), ends(".mdl.xbx", ".skin.xbx", ".tex.xbx"));
    each(chunks(models, 150), (c, k) => driver(`files thug2 models ${k}`, "files", c));
    const thug1Col = thug1Archives.filter((p) => /col\.pre$/i.test(p));
    each(chunks(thug1Col, 20), (c, k) => driver(`files thug1 col ${k}`, "files", c));
    const thps4Tex = walk(path.join(THPS4, "data", "levels"), ends("tex.dat"));
    if (thps4Tex.length) out.push(driver("files thps4 tex", "files", thps4Tex));
    each(
        chunks(
            stagingFiles.filter((p) => /\.(scn|col|tex)\.xbx$/i.test(p)),
            40,
        ),
        (c, k) => driver(`files converted ${k}`, "files", c),
    );

    // qb.ts (+ script_port.Source / statements): THUG2, THUG1 (weights), THPS4, THPS3 (no weights), converted levels
    if (exists(pre)) out.push(driver("qb thug2 qb_scripts", "qb", ["1", path.join(pre, "qb_scripts.prx")]));
    const thug2Qb = [
        ...walk(path.join(THUG2, "Data", "scripts"), ends(".qb")),
        ...walk(path.join(THUG2, "Data", "levels"), ends(".qb")),
    ];
    each(chunks(thug2Qb, 60), (c, k) => driver(`qb thug2 loose ${k}`, "qb", ["1", ...c]));
    each(chunks(thug1Archives, 30), (c, k) => driver(`qb thug1 ${k}`, "qb", ["1", ...c]));
    const thps4Qb = [
        ...walk(path.join(THPS4, "data", "scripts"), ends(".qb")),
        ...walk(path.join(THPS4, "data", "levels"), ends(".qb")),
    ];
    each(chunks(thps4Qb, 80), (c, k) => driver(`qb thps4 ${k}`, "qb", ["0", ...c]));
    const thps3Qb = [
        ...walk(path.join(THPS3, "Data", "Scripts"), ends(".qb")),
        ...walk(path.join(THPS3, "Data", "Levels"), ends(".qb")),
    ];
    each(chunks(thps3Qb, 80), (c, k) => driver(`qb thps3 ${k}`, "qb", ["0", ...c]));
    each(
        chunks(
            stagingFiles.filter((p) => low(p).endsWith(".qb")),
            60,
        ),
        (c, k) => driver(`qb converted ${k}`, "qb", ["1", ...c]),
    );

    // script_port.Thug2: THUG2's commands
    if (exists(THUG2)) out.push(driver("thug2 commands", "thug2", [THUG2]));

    // nodes / script_port.Translator / peds_cars / common.nsCompile per level, configured as each converter
    const thps3Scripts = walk(path.join(THPS3, "Data", "Scripts"), ends(".qb"));
    for (const level of list(path.join(THPS3, "Data", "Levels"), (n) => !/_sky$/i.test(n))) {
        const qb = list(level, ends(".qb"))[0];
        if (qb)
            out.push(
                driver(`translate thps3 ${path.basename(level)}`, "translate", [THUG2, "thps3", qb, ...thps3Scripts]),
            );
    }
    const thps4Scripts = walk(path.join(THPS4, "data", "scripts"), ends(".qb"));
    for (const level of list(path.join(THPS4, "data", "levels"), (n) => !/_sky$|_shell$/i.test(n))) {
        const qb = list(level, (n) => low(n) === `${low(path.basename(level))}.qb`)[0];
        if (qb) {
            out.push(
                driver(`translate thps4 ${path.basename(level)}`, "translate", [THUG2, "thps4", qb, ...thps4Scripts]),
            );
        }
    }
    const qbPre = path.join(THUG1, "Data", "pre", "qb.pre");
    for (const level of ["NJ", "NY", "FL", "SD", "HI", "VC", "SJ", "RU", "SE", "DJ", "PH", "SC", "HN"]) {
        const archive = path.join(THUG1, "Data", "pre", `${level}.pre`);
        if (exists(archive)) {
            out.push(
                driver(`translate thug1 ${level}`, "translate", [
                    THUG2,
                    "thug1",
                    `${archive}|Levels\\${level}\\${level}.qb`,
                    qbPre,
                ]),
            );
        }
    }

    // nodes.groundRestarts / netGameNodes on THUG2's own levels against their collision
    const thug2Levels = ["AP", "AU", "BA", "BE", "BO", "CA", "DJ", "LA", "NO", "PH", "SC", "SE", "SE2", "ST", "TR"];
    each(
        chunks(
            thug2Levels.filter((l) => exists(path.join(THUG2, "Data", "levels", l, `${l}.qb`))),
            4,
        ),
        (c, k) => driver(`ground thug2 ${k}`, "ground", [THUG2, ...c]),
    );

    // common.lighting on every structure; ported_levels on the workspaces' staging folders (read only)
    out.push(driver("lighting thug2", "lighting", ["1", path.join(pre, "qb_scripts.prx"), ...thug2Qb]));
    out.push(driver("lighting thug1", "lighting", ["1", qbPre]));
    out.push(driver("lighting thps4 thps3", "lighting", ["0", ...thps4Qb, ...thps3Qb]));
    out.push(driver("manifest", "manifest", [...WORKSPACES, THUG2].filter(exists)));

    // geometry: THPS4 / THUG1 scenes and collision through convertScene / convertCol (inputs: driver.ts prepare - the
    // THPS4 / THUG1 readers are TypeScript only since stage 2)
    for (const which of ["thps4", "thug1"]) {
        out.push(
            driver(`geometry ${which}`, "geometry", ["{in}"], { setup: [TS, "prepare", "{in}", which, THPS4, THUG1] }),
        );
    }
    each(
        chunks(
            thug2Archives.filter((p) => /col\.prx$/i.test(p)),
            6,
        ),
        (c, k) => driver(`colgeometry thug2 ${k}`, "colgeometry", c),
    );

    out.push(driver("compat", "compat", []));
    out.push(driver("common", "common", [THUG2, "TRscn.prx", "TRcol.prx", "Bits.prx"]));

    // the CLIs, as they are
    const cols = [
        ...thug2Archives.filter((p) => /col\.prx$/i.test(p)),
        ...stagingFiles.filter((p) => low(p).endsWith(".col.xbx")),
    ];
    each(chunks(cols, 30), (c, k) => ({
        name: `cli col_check ${k}`,
        python: ["tools/level_port/col_check.py", ...c],
        ts: ["tools/level_port/col_check.ts", ...c],
        nonzeroOk: true,
    }));
    out.push({
        name: "cli col_check missing file",
        python: ["tools/level_port/col_check.py", "{out}\\nothing.col.xbx"],
        ts: ["tools/level_port/col_check.ts", "{out}\\nothing.col.xbx"],
        stdout: "ignore",
        nonzeroOk: true,
    });
    for (const staging of STAGINGS) {
        const main = path.join(THUG2, staging);
        if (!exists(path.join(main, "levels.json"))) continue;
        // (the main install's staging folder against a copy changed script by script: every path of script_diff;
        // all examples shown and compared sorted - which N of them Python shows depends on its set order, which
        // changes from run to run)
        for (const show of ["0", "100000"]) {
            out.push({
                name: `cli script_diff changed ${staging} show ${show}`,
                setup: ["tools/parity/level_port/mutate_staging.py", "{in}", main],
                python: ["tools/level_port/script_diff.py", "{in}\\a", "{in}\\b", show],
                ts: ["tools/level_port/script_diff.ts", "{in}\\a", "{in}\\b", show],
                stdout: show === "0" ? "exact" : "sorted",
            });
        }
        if (exists(path.join(WORKSPACES[0]!, staging))) {
            out.push({
                name: `cli script_diff main THUG2-1 ${staging}`,
                python: ["tools/level_port/script_diff.py", main, path.join(WORKSPACES[0]!, staging), "1000"],
                ts: ["tools/level_port/script_diff.ts", main, path.join(WORKSPACES[0]!, staging), "1000"],
                stdout: "sorted",
            });
        }
    }
    for (const staging of STAGINGS) {
        const a = path.join(WORKSPACES[0]!, staging);
        for (const b of WORKSPACES.slice(1).map((w) => path.join(w, staging))) {
            if (!exists(a) || !exists(b)) continue;
            const label = `${path.basename(path.dirname(b))} ${staging}`;
            out.push({
                name: `cli script_diff ${label}`,
                python: ["tools/level_port/script_diff.py", a, b],
                ts: ["tools/level_port/script_diff.ts", a, b],
            });
            out.push({
                name: `cli script_diff show ${label}`,
                python: ["tools/level_port/script_diff.py", b, a, "40"],
                ts: ["tools/level_port/script_diff.ts", b, a, "40"],
                stdout: "sorted",
            });
        }
    }
    return out;
}
