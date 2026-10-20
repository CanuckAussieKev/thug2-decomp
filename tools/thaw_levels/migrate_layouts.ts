// NOT ORIGINAL - moves the level editor's saved layouts of the THAW levels (<game>\thug2-decomp-layouts\<scene
// checksum>.txt, src/game/front/level_editor_layout.cpp) from the levels as this converter made them before THUGPRO's
// frame and names (thaw_<xx>, recentred by thaw_levels.recentreShift) to the levels as it makes them now
// (levelName, frameShift): the file renamed after the new scene's checksum, its scene line changed and every position
// moved by the difference between the two frames.
//
//   node tools/thaw_levels/migrate_layouts.ts --game C:\Games\THUG2-1 [--dry-run] [levels...]
//
// A layout's records (all world coordinates):
//   sector / rail / clone <checksum> <quarters> <offset x y z> ...: x' = Q x + offset (Q: quarter turns about y,
//       about the world origin) - in a frame moved by d: offset' = offset + d - Q d
//   object <id> <x y z> <rows...> / copy <name> <source> <x y z> <rows...>: the position moved by d
// The old file is kept as <name>.txt.before-thugpro-frame; a level whose new file already exists is skipped.

import * as fs from "node:fs";
import * as path from "node:path";
import { parseArgs } from "node:util";
import * as qb from "../level_port/qb.ts";
import {
    formatG,
    hex8,
    isMain,
    print,
    pyExit,
    pyIntHex,
    pyJoin,
    pyParseFloat,
    pyParseInt,
    pySplit,
    pySplitlines,
    readTextPy,
    runMain,
} from "../lib/py.ts";
import * as thaw from "./thaw.ts";
import {
    findPath,
    frameShift,
    LEVELS,
    levelName,
    need,
    recentreShift,
    zoneArray,
    zoneFiles,
    zonePak,
} from "./thaw_levels.ts";

const FOLDER = "thug2-decomp-layouts";
const KEEP = ".before-thugpro-frame";

// As the level editor's QuarterTurn (level_editor_scenery.cpp).
export function quarterTurn(v: readonly number[], quarters: number): readonly number[] {
    const [x, y, z] = v as [number, number, number];
    const q = quarters & 3;
    if (q === 1) return [z, y, -x];
    if (q === 2) return [-x, y, -z];
    if (q === 3) return [-z, y, x];
    return v;
}

const number = (v: number): string => formatG(v, 9);

// str.partition(sep)
function partition(text: string, sep: string): [string, string, string] {
    const at = text.indexOf(sep);
    return at < 0 ? [text, "", ""] : [text.slice(0, at), sep, text.slice(at + sep.length)];
}

// The layout's text with its scene and positions changed. Returns [text, records moved].
export function migrateText(
    text: string,
    oldScene: number,
    newScene: number,
    d: readonly number[],
    newName: string | null = null,
): [string, number] {
    const out: string[] = [];
    let moved = 0;
    for (const line of pySplitlines(text, true)) {
        let [body, hash, comment] = partition(line, "#");
        const ending = line.endsWith("\n") ? "\n" : "";
        const words = pySplit(body);
        if (!words.length) {
            out.push(line);
            continue;
        }
        const kind = words[0]!;
        if (kind === "scene") {
            if (pyIntHex(words[1]!) !== oldScene) throw new RangeError(`scene ${words[1]}, not ${hex8(oldScene)}`);
            words[1] = hex8(newScene);
            if (newName && hash) comment = ` ${newName}`;
        } else if (kind === "sector" || kind === "rail" || kind === "clone") {
            const q = pyParseInt(words[2]!);
            const offset = words.slice(3, 6).map(pyParseFloat);
            const turned = quarterTurn(d, q);
            words.splice(3, 3, ...[0, 1, 2].map((k) => number(offset[k]! + d[k]! - turned[k]!)));
            moved += 1;
        } else if (kind === "object" || kind === "copy") {
            const at = kind === "object" ? 2 : 3;
            words.splice(at, 3, ...[0, 1, 2].map((k) => number(pyParseFloat(words[at + k]!) + d[k]!)));
            moved += 1;
        }
        let rebuilt = words.join(" ");
        if (hash) rebuilt += `  #${comment.replace(/[\r\n]+$/, "")}`;
        out.push(rebuilt + ending);
    }
    return [out.join(""), moved];
}

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
    let recentre = 20000.0;
    if (v.recentre !== undefined) {
        try {
            recentre = pyParseFloat(v.recentre);
        } catch {
            process.stderr.write(
                `${USAGE}\nmigrate_layouts.ts: error: argument --recentre: invalid float value: '${v.recentre}'\n`,
            );
            pyExit(2);
        }
    }
    const thawFolder = v.thaw ?? "C:\\Games\\THAW";
    const folder = findPath(v.game, FOLDER) || findPath(v.game, "Game", FOLDER);
    if (!folder) {
        print(`no ${FOLDER} in ${v.game}: nothing to migrate`);
        return 0;
    }
    const names = thaw.loadNames(thawFolder);
    const wanted = new Set(parsed.positionals.map((x) => x.toLowerCase()));
    for (const [zone] of LEVELS) {
        if (wanted.size && !wanted.has(zone.toLowerCase()) && !wanted.has(zone.toLowerCase().slice(2))) continue;
        const oldName = levelName(zone, true);
        const newName = levelName(zone);
        const oldScene = qb.checksum(oldName);
        const newScene = qb.checksum(newName);
        const oldPath = pyJoin(folder, `${hex8(oldScene)}.txt`);
        const newPath = pyJoin(folder, `${hex8(newScene)}.txt`);
        if (!fs.existsSync(oldPath)) continue;
        if (fs.existsSync(newPath)) {
            print(`${zone}: ${newName} already has a layout - ${path.win32.basename(oldPath)} left as it is`);
            continue;
        }
        const pak = zonePak(thawFolder, zone);
        if (pak === null) throw new TypeError(`${zone}: no _net archive`); // (Python: open(None))
        const files = zoneFiles(pak);
        const old = recentreShift(thaw.readCol(need(files, ".col"), `${zone}.col`), recentre);
        const now = frameShift(zone, zoneArray(files, zone, names), names);
        const d = [0, 1, 2].map((k) => now[k]! - old[k]!);
        const text = readTextPy(oldPath);
        const [changed, moved] = migrateText(text, oldScene, newScene, d, newName);
        print(
            `${zone}: ${oldName} -> ${newName}, moved by (${formatG(d[0]!)}, ${formatG(d[1]!)}, ${formatG(d[2]!)}): ` +
                `${moved} records${v["dry-run"] ? " (dry run)" : ""}`,
        );
        if (!v["dry-run"]) {
            fs.writeFileSync(newPath, changed, "utf-8");
            fs.renameSync(oldPath, oldPath + KEEP);
        }
    }
    return 0;
}

const USAGE = "usage: migrate_layouts.ts [-h] --game GAME [--thaw THAW] [--recentre RECENTRE] [--dry-run] [levels ...]";

const OPTIONS = {
    game: { type: "string" }, // the game folder whose thug2-decomp-layouts to migrate
    thaw: { type: "string" },
    recentre: { type: "string" }, // the old frame's --recentre
    "dry-run": { type: "boolean" },
} as const;

if (isMain(import.meta.url)) await runMain(() => main());
