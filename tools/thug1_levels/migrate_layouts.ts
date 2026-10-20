// NOT ORIGINAL - renames the level editor's saved layouts of the THUG1 levels (<game>\thug2-decomp-layouts\<scene
// checksum>.txt, src/game/front/level_editor_layout.cpp) from the names this converter gave them before (thug1_nj...)
// to the names it gives them now (thug1_levels.levelName: THUG1's own, nj...; Hotter Than Hell hh - TP-30): the file
// renamed after the new scene's checksum and its scene line changed. The frame is the same (THUG1's own world
// coordinates, as before), so no record moves (tools/thaw_levels/migrate_layouts.ts's migrateText with no shift).
//
//   node tools/thug1_levels/migrate_layouts.ts --game C:\Games\THUG2-3 [--thug1 C:\Games\THUG1] [--dry-run] [levels...]
//
// The old file is kept as <name>.txt.before-thugpro-names; a level whose new file already exists is skipped.

import * as fs from "node:fs";
import * as path from "node:path";
import { parseArgs } from "node:util";
import * as qb from "../level_port/qb.ts";
import { hex8, isMain, print, pyExit, pyJoin, readTextPy, runMain } from "../lib/py.ts";
import { migrateText } from "../thaw_levels/migrate_layouts.ts";
import { LEVELS, levelNames } from "./thug1_levels.ts";

const FOLDER = "thug2-decomp-layouts";
const KEEP = ".before-thugpro-names";

const findDir = (first: string, ...rest: string[]): string | null => {
    const p = pyJoin(first, ...rest);
    return fs.existsSync(p) && fs.statSync(p).isDirectory() ? p : null;
};

const USAGE = "usage: migrate_layouts.ts [-h] --game GAME [--thug1 THUG1] [--dry-run] [levels ...]";

const OPTIONS = {
    game: { type: "string" }, // the game folder whose thug2-decomp-layouts to migrate
    thug1: { type: "string" },
    "dry-run": { type: "boolean" },
} as const;

export function main(argv: readonly string[] = process.argv.slice(2)): number {
    let parsed: ReturnType<typeof parseArgs<{ options: typeof OPTIONS; allowPositionals: true }>>;
    try {
        parsed = parseArgs({ args: [...argv], options: OPTIONS, allowPositionals: true, strict: true });
    } catch (e) {
        process.stderr.write(`${USAGE}\nmigrate_layouts.ts: error: ${(e as Error).message}\n`);
        pyExit(2);
    }
    const v = parsed.values;
    if (v.game === undefined) {
        process.stderr.write(`${USAGE}\nmigrate_layouts.ts: error: the following arguments are required: --game\n`);
        pyExit(2);
    }
    const folder = findDir(v.game, FOLDER) ?? findDir(v.game, "Game", FOLDER);
    if (!folder) {
        print(`no ${FOLDER} in ${v.game}: nothing to migrate`);
        return 0;
    }
    const thug2 = findDir(v.game, "Game", "Data") ? pyJoin(v.game, "Game") : v.game;
    const now = levelNames(v.thug1 ?? "C:\\Games\\THUG1", thug2);
    const wanted = new Set(parsed.positionals.map((x) => x.toLowerCase()));
    for (const [code] of LEVELS) {
        if (wanted.size && !wanted.has(code.toLowerCase())) continue;
        const oldName = `thug1_${code.toLowerCase()}`;
        const newName = now.get(code.toLowerCase())!;
        if (newName === oldName) continue;
        const oldScene = qb.checksum(oldName);
        const newScene = qb.checksum(newName);
        const oldPath = pyJoin(folder, `${hex8(oldScene)}.txt`);
        const newPath = pyJoin(folder, `${hex8(newScene)}.txt`);
        if (!fs.existsSync(oldPath)) continue;
        if (fs.existsSync(newPath)) {
            print(`${code}: ${newName} already has a layout - ${path.win32.basename(oldPath)} left as it is`);
            continue;
        }
        const [changed, records] = migrateText(readTextPy(oldPath), oldScene, newScene, [0.0, 0.0, 0.0], newName);
        print(`${code}: ${oldName} -> ${newName}: ${records} records${v["dry-run"] ? " (dry run)" : ""}`);
        if (!v["dry-run"]) {
            fs.writeFileSync(newPath, changed, "utf-8");
            fs.renameSync(oldPath, oldPath + KEEP);
        }
    }
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
