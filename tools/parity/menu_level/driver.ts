// NOT ORIGINAL - the TypeScript half of tools/menu_level's unit-level parity cases (cases.ts; driver.py the same in
// Python): dumps what career_complete / qb_walk, level_select and level_select_icons work out from a workspace's
// files (read only) into <out>, as text both languages write the same.
//
//   node tools/parity/menu_level/driver.ts career|map|sections|signs <workspace> <ns.exe> <out>

import { spawnSync } from "node:child_process";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import * as portedLevels from "../../level_port/ported_levels.ts";
import { compareStr, pyFloatRepr } from "../../lib/py.ts";
import * as careerComplete from "../../menu_level/career_complete.ts";
import * as levelSelect from "../../menu_level/level_select.ts";
import * as icons from "../../menu_level/level_select_icons.ts";
import * as makeMenuLevel from "../../menu_level/make_menu_level.ts";

function decompiler(ns: string, work: string): (data: Buffer, name: string) => string {
    return (data, name) => {
        const file = path.join(work, name);
        fs.writeFileSync(file, data);
        const result = spawnSync(ns, ["-d", file, "-o", `${file}.ns`]);
        if (result.status !== 0) throw new Error(`ns failed on ${name}`);
        return fs.readFileSync(`${file}.ns`, "utf-8").replace(/\r\n?/g, "\n");
    };
}

function write(out: string, name: string, text: string): void {
    fs.writeFileSync(path.join(out, name), text, "utf-8");
}

function splitextRoot(p: string): string {
    const dot = p.lastIndexOf(".");
    return dot > Math.max(p.lastIndexOf("\\"), p.lastIndexOf("/")) ? p.slice(0, dot) : p;
}

function withTemp<T>(f: (work: string) => T): T {
    const work = fs.mkdtempSync(path.join(os.tmpdir(), "tmp"));
    try {
        return f(work);
    } finally {
        fs.rmSync(work, { recursive: true, force: true });
    }
}

function career(ws: string, ns: string, out: string): void {
    const pre = path.join(ws, "Data", "pre");
    withTemp((work) => {
        const decompile = decompiler(ns, work);
        const qbScripts = path.join(pre, "qb_scripts.prx");
        const scriptFile = (name: string): string =>
            decompile(makeMenuLevel.prxFile(qbScripts, name), name.slice(name.lastIndexOf("\\") + 1));
        const levelScripts = (prefix: string, qb: string, preName: string): string =>
            decompile(
                makeMenuLevel.prxFile(path.join(pre, `${splitextRoot(preName)}.prx`), qb),
                `${prefix}_scripts.qb`,
            );
        const levelQbs = (preName: string): Map<string, Buffer> =>
            makeMenuLevel.prxFiles(path.join(pre, `${splitextRoot(preName)}.prx`));
        const levels = scriptFile("scripts\\game\\levels.qb");
        const data = careerComplete.careerData(
            levels,
            scriptFile("scripts\\game\\menu\\chapter_info.qb"),
            scriptFile("scripts\\game\\goal_utilities.qb"),
            scriptFile("scripts\\game\\menu\\stats.qb"),
            levelScripts,
            scriptFile("scripts\\game\\menu\\gamemenu_levelselect.qb"),
            makeMenuLevel.prxFiles(qbScripts),
            levelQbs,
        );
        const lines = [careerComplete.describe(data)];
        for (const level of data.levels) {
            lines.push(
                `level ${level.load} ${level.prefix} ${level.number} ${level.qb} ${level.pre} ${level.goalsRoot} total=${level.total} points=${level.points}`,
            );
            for (const [name, points] of level.goals) lines.push(`  goal ${name} ${points}`);
        }
        for (const flag of data.flags) lines.push(`flag ${flag}`);
        for (const [trick, combo] of data.tricks) lines.push(`trick ${trick} ${combo}`);
        lines.push(`final chapter ${data.finalChapter}, stats ${data.stats.join(" ")}`);
        let classicTotal = 0;
        for (const [prefix, number] of data.classicLevels) {
            lines.push(`classic ${prefix} ${number}: ${data.classic.get(number)!.join(" ")}`);
        }
        for (const names of data.classic.values()) classicTotal += names.length;
        lines.push(`classic goals: ${classicTotal}`);
        let gapTotal = 0;
        for (const [prefix, number] of data.gapLevels) {
            const gaps = data.gaps.get(number)!;
            lines.push(`gaps ${prefix} ${number}: ${gaps.length}`);
            for (const [name, score, pos] of gaps) {
                lines.push(`  ${name}|${score}|${pos ? pos.map(pyFloatRepr).join(",") : "None"}`);
            }
        }
        for (const gaps of data.gaps.values()) gapTotal += gaps.length;
        lines.push(`gaps: ${gapTotal} over ${data.gaps.size} levels`);
        write(out, "career.txt", `${lines.join("\n")}\n`);
        write(out, "apply.ns", careerComplete.applyScript(data));
        const options = decompile(
            makeMenuLevel.prxFile(path.join(pre, "mainmenu_scripts.prx"), makeMenuLevel.OPTIONS_MENU_QB),
            "mainmenu_options.qb",
        );
        write(out, "options.ns", careerComplete.optionsMenu(options, data));
    });
}

const reprPair = ([x, y]: readonly [number, number]): string => `(${pyFloatRepr(x)}, ${pyFloatRepr(y)})`;

function mapPositions(out: string): void {
    const lines: string[] = [];
    for (let i = -24; i < 25; i++) {
        for (let j = -16; j < 17; j++) {
            const lon = i * 7.5 + 0.13;
            const lat = j * 5.0 - 0.07;
            lines.push(`${pyFloatRepr(lon)} ${pyFloatRepr(lat)} -> ${reprPair(levelSelect.mapPosition(lon, lat))}`);
        }
    }
    for (const [name, [lon, lat]] of levelSelect.PLACES) {
        lines.push(`${name} -> ${reprPair(levelSelect.mapPosition(lon, lat))}`);
    }
    write(out, "map.txt", `${lines.join("\n")}\n`);
}

function sections(ws: string, ns: string, out: string): void {
    const manifest = portedLevels.loadManifest(ws);
    const byGame = new Map<string, portedLevels.Manifest>();
    for (const entry of manifest?.levels ?? []) {
        const staging = String(entry.staging);
        if (!byGame.has(staging)) byGame.set(staging, { levels: [] });
        byGame.get(staging)!.levels.push(entry);
    }
    const games = new Map([
        ["thps4", "thug2-decomp-thps4"],
        ["thug1", "thug2-decomp-thug1"],
        ["thps3", "thug2-decomp-thps3"],
        ["thaw", "thug2-decomp-thaw"],
    ]);
    const pre = path.join(ws, "Data", "pre");
    const [select, sprites] = withTemp((work) => {
        const decompile = decompiler(ns, work);
        const qbScripts = path.join(pre, "qb_scripts.prx");
        return [
            decompile(
                makeMenuLevel.prxFile(qbScripts, "scripts\\game\\menu\\gamemenu_levelselect.qb"),
                "levelselect.qb",
            ),
            decompile(makeMenuLevel.prxFile(qbScripts, "scripts\\game\\menu\\sprites.qb"), "sprites.qb"),
        ];
    });
    const subsets = [[], ["thps4"], ["thug1", "thaw"], ["thps3"], [...games.keys()]];
    subsets.forEach((subset, n) => {
        const chosen: Record<string, portedLevels.Manifest | undefined> = {};
        for (const g of subset) chosen[g] = byGame.get(games.get(g)!);
        const sectionList = levelSelect.sections(chosen);
        write(out, `describe_${n}.txt`, `${levelSelect.describe(sectionList)}\n`);
        const signs = [...levelSelect.newSigns(sectionList)].sort((a, b) => compareStr(a[0], b[0]));
        write(out, `signs_${n}.txt`, `${signs.map(([k, v]) => `${k} ${v}`).join("\n")}\n`);
        const edited = levelSelect.editLevelSelect(makeMenuLevel.addSkateshopSign(select), sectionList);
        write(out, `levelselect_${n}.ns`, edited);
        write(out, `sprites_${n}.ns`, levelSelect.editSprites(sprites, sectionList));
    });
}

function signs(ws: string, out: string): void {
    const frame = makeMenuLevel.prxFile(path.join(ws, "Data", "pre", "Levelselect.prx"), icons.FRAME_SOURCE);
    for (const [name, draw] of icons.PICTOGRAMS) {
        fs.writeFileSync(path.join(out, `sign_${name}.img.xbx`), icons.makeSign(frame, name));
        write(out, `ink_${name}.txt`, `${icons.coverage(draw()).map(pyFloatRepr).join(" ")}\n`);
    }
}

const [what, ws, ns, out] = process.argv.slice(2) as [string, string, string, string];
if (what === "career") career(ws, ns, out);
else if (what === "map") mapPositions(out);
else if (what === "sections") sections(ws, ns, out);
else signs(ws, out);
