// NOT ORIGINAL - renames the level editor's saved layouts of the THPS3 levels (<game>\thug2-decomp-layouts\<scene
// checksum>.txt, src/game/front/level_editor_layout.cpp) from the levels' old names (thps3_<folder>) to the names
// thps3_levels.ts gives them now (THPS3's own: foun, rio... - thps3_levels.levelName): the file renamed after the new
// scene's checksum and its scene line changed. Nothing moves: the frame is THPS3's as before, and the sectors, rails
// and objects a layout names keep their checksums (THPS3's node names; the made objects' and materials' checksums
// still use the old name - thps3_levels.ts `tag`).
//
//   node tools/thps3_levels/migrate_layouts.ts --game C:\Games\THUG2-1 [--thps3 C:\Games\THPS3] [--dry-run] [levels...]
//
// The old file is kept as <name>.txt.before-thps3-names; a level whose new file already exists is skipped.
// (The THAW twin, which also moves positions: tools/thaw_levels/migrate_layouts.ts.)

import * as fs from "node:fs";
import * as path from "node:path";
import { parseArgs } from "node:util";
import * as qb from "../level_port/qb.ts";
import { hex8, isMain, print, pyExit, pyJoin, runMain } from "../lib/py.ts";
import { findPath } from "../thaw_levels/thaw_levels.ts";
import { LEVELS, legacyName, levelNames } from "./thps3_levels.ts";

const FOLDER = "thug2-decomp-layouts";
const KEEP = ".before-thps3-names";

// The layout's text with its scene line naming the new scene (the rest byte for byte). Returns [text, scene lines].
export function renameScene(text: string, oldScene: number, newScene: number, newName: string): [string, number] {
    let changed = 0;
    const out = text.replace(
        /^(\s*scene\s+)([0-9A-Fa-f]{1,8})([^\r\n]*)/gm,
        (_m, head: string, hex: string, rest: string) => {
            if (Number.parseInt(hex, 16) >>> 0 !== oldScene)
                throw new RangeError(`scene ${hex}, not ${hex8(oldScene)}`);
            changed += 1;
            const comment = rest.includes("#") ? `${rest.slice(0, rest.indexOf("#") + 1)} ${newName}` : rest;
            return `${head}${hex8(newScene)}${comment}`;
        },
    );
    return [out, changed];
}

const USAGE = "usage: migrate_layouts.ts [-h] --game GAME [--thps3 THPS3] [--dry-run] [levels ...]";

const OPTIONS = {
    game: { type: "string" },
    thps3: { type: "string" },
    "dry-run": { type: "boolean" },
    help: { type: "boolean", short: "h" },
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
    if (v.help) {
        print(USAGE);
        return 0;
    }
    if (v.game === undefined) {
        process.stderr.write(`${USAGE}\nmigrate_layouts.ts: error: the following arguments are required: --game\n`);
        pyExit(2);
    }
    const folder = findPath(v.game, FOLDER) || findPath(v.game, "Game", FOLDER);
    if (!folder) {
        print(`no ${FOLDER} in ${v.game}: nothing to migrate`);
        return 0;
    }
    const thug2 = fs.existsSync(pyJoin(v.game, "Data")) ? v.game : pyJoin(v.game, "Game");
    const names = levelNames(v.thps3 ?? "C:\\Games\\THPS3", thug2);
    const wanted = new Set(parsed.positionals.map((x) => x.toLowerCase()));
    for (const [base] of LEVELS) {
        if (wanted.size && !wanted.has(base.toLowerCase())) continue;
        const oldName = legacyName(base);
        const newName = names.get(base.toLowerCase())!;
        if (oldName === newName) continue;
        const oldScene = qb.checksum(oldName);
        const newScene = qb.checksum(newName);
        const oldPath = pyJoin(folder, `${hex8(oldScene)}.txt`);
        const newPath = pyJoin(folder, `${hex8(newScene)}.txt`);
        if (!fs.existsSync(oldPath)) continue;
        if (fs.existsSync(newPath)) {
            print(`${base}: ${newName} already has a layout - ${path.win32.basename(oldPath)} left as it is`);
            continue;
        }
        const [text, scenes] = renameScene(fs.readFileSync(oldPath, "utf-8"), oldScene, newScene, newName);
        print(`${base}: ${oldName} -> ${newName} (${scenes} scene line)${v["dry-run"] ? " (dry run)" : ""}`);
        if (!v["dry-run"]) {
            fs.writeFileSync(newPath, text, "utf-8");
            fs.renameSync(oldPath, oldPath + KEEP);
        }
    }
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
