// NOT ORIGINAL - renames the level editor's saved layouts of the THPS4 levels (<game>\thug2-decomp-layouts\<scene
// checksum>.txt, src/game/front/level_editor_layout.cpp) from the names this converter used before (thps4_<lvl>) to
// THPS4's own (levelName: alc, zoo...): the file renamed after the new scene's checksum and its scene line changed.
// The levels didn't move (the frame is THPS4's in both), so the records stay as they are. The THAW counterpart, which
// also moves them, is tools/thaw_levels/migrate_layouts.ts (its migrateText is used here with no move).
//
//   node tools/thps4_levels/migrate_layouts.ts --game C:\Games\THUG2-2 [--dry-run] [levels...]
//
// The old file is kept as <name>.txt.before-thps4-names; a level whose new file already exists is skipped.

import * as fs from "node:fs";
import * as path from "node:path";
import { parseArgs } from "node:util";
import * as qb from "../level_port/qb.ts";
import { hex8, isMain, print, pyExit, pyJoin, readTextPy, runMain } from "../lib/py.ts";
import { migrateText } from "../thaw_levels/migrate_layouts.ts";
import { findPath } from "../thaw_levels/thaw_levels.ts";
import { LEVELS, levelName } from "./thps4_levels.ts";

const FOLDER = "thug2-decomp-layouts";
export const KEEP = ".before-thps4-names";

// [old name, new name] of each level whose name changed.
export function renames(levels: readonly string[] = LEVELS): [string, string][] {
    return levels.map((base): [string, string] => [levelName(base, true), levelName(base)]).filter(([a, b]) => a !== b);
}

const USAGE = "usage: migrate_layouts.ts [-h] --game GAME [--dry-run] [levels ...]";

const OPTIONS = {
    game: { type: "string" }, // the game folder whose thug2-decomp-layouts to migrate
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
    const folder = findPath(v.game, FOLDER) || findPath(v.game, "Game", FOLDER);
    if (!folder) {
        print(`no ${FOLDER} in ${v.game}: nothing to migrate`);
        return 0;
    }
    const wanted = new Set(parsed.positionals.map((x) => x.toLowerCase()));
    const levels = LEVELS.filter((b) => !wanted.size || wanted.has(b.toLowerCase()));
    let done = 0;
    for (const [oldName, newName] of renames(levels)) {
        const oldScene = qb.checksum(oldName);
        const newScene = qb.checksum(newName);
        const oldPath = pyJoin(folder, `${hex8(oldScene)}.txt`);
        const newPath = pyJoin(folder, `${hex8(newScene)}.txt`);
        if (!fs.existsSync(oldPath)) continue;
        if (fs.existsSync(newPath)) {
            print(`${oldName}: ${newName} already has a layout - ${path.win32.basename(oldPath)} left as it is`);
            continue;
        }
        const [changed] = migrateText(readTextPy(oldPath), oldScene, newScene, [0, 0, 0], newName);
        print(`${oldName} -> ${newName}${v["dry-run"] ? " (dry run)" : ""}`);
        done += 1;
        if (!v["dry-run"]) {
            fs.writeFileSync(newPath, changed, "utf-8");
            fs.renameSync(oldPath, oldPath + KEEP);
        }
    }
    if (!done) print("no THPS4 layouts under the old names");
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
