// NOT ORIGINAL - offline check of ported levels' human peds: each on the skeleton its THUG2 profile's model is
// rigged to, and the Ped_Female animations loaded by the level's load_level_anims when any ped needs them
// (tools/level_port/ped_rigs.ts has why: a woman on THPS6_human is drawn twisted).
//
//   node tools/level_port/ped_rig_check.ts [--thug2 <game folder>] <overlay or level folder | level .qb> ...
//
// e.g. node tools/level_port/ped_rig_check.ts C:\Games\THUG2-3\thug2-decomp-thaw C:\Games\THUG2-3\thug2-decomp-thps4
// Read only. Exit status 1 when a ped or level is wrong.

import * as fs from "node:fs";
import * as path from "node:path";
import { isMain, print, runMain } from "../lib/py.ts";
import * as pedRigs from "./ped_rigs.ts";
import { Thug2Assets } from "./peds_cars.ts";
import { checksum, Name, QbFile, Struct } from "./qb.ts";

export interface LevelRigs {
    wrong: string[]; // "<node>: <profile> on <skeleton>, needs <skeleton>"
    female: number;
    human: number;
    femaleAnimsLoaded: boolean;
}

export function checkLevel(file: QbFile, femaleRig: (profile: number) => boolean): LevelRigs {
    const text = (v: number): string => file.names.get(v) ?? `0x${v.toString(16).padStart(8, "0")}`;
    const peds: Struct[] = [];
    for (const [, value] of file.globals) {
        // (the node array's peds, and the shared parts structures nodes include - THUG1's <level>_inc_<n>)
        for (const s of Array.isArray(value) ? value : [value]) {
            const cls = s instanceof Struct ? s.get("Class") : undefined;
            if (cls instanceof Name && cls.value === checksum("Pedestrian")) peds.push(s as Struct);
        }
    }
    // (checked on copies: fixPedRigs says what it would change)
    const names = new Map(file.names);
    const fix: pedRigs.RigFix = { changed: [], female: 0, human: 0 };
    const wrong: string[] = [];
    for (const ped of peds) {
        const one = pedRigs.fixPedRigs([new Struct([...ped.entries])], names, femaleRig);
        fix.female += one.female;
        fix.human += one.human;
        const was = ped.get("skeletonName");
        for (const c of one.changed)
            wrong.push(`${c.replace(" -> ", " needs ")} (was ${was instanceof Name ? text(was.value) : "none"})`);
    }
    const anims = file.scripts.get(checksum("load_level_anims")) ?? [];
    const female = checksum(pedRigs.FEMALE.anims);
    const femaleAnimsLoaded = anims.some((line) => line.some(([t, p]) => t === 0x16 && p.readUInt32LE(0) === female));
    return { wrong, female: fix.female, human: fix.human, femaleAnimsLoaded };
}

function levelQbs(target: string): string[] {
    if (fs.statSync(target).isFile()) return [target];
    const out: string[] = [];
    const walk = (dir: string): void => {
        for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
            const p = path.join(dir, e.name);
            if (e.isDirectory()) walk(p);
            // (a level's own .qb: levels\<x>\<x>.qb)
            else if (e.name.toLowerCase() === `${path.basename(dir).toLowerCase()}.qb`) out.push(p);
        }
    };
    walk(target);
    return out.sort();
}

export function main(argv: readonly string[] = process.argv.slice(2)): number {
    let game = "C:\\Games\\THUG2\\Game";
    const targets: string[] = [];
    for (let i = 0; i < argv.length; i++) {
        if (argv[i] === "--thug2") game = argv[++i]!;
        else targets.push(argv[i]!);
    }
    if (fs.existsSync(path.join(game, "Game", "Data"))) game = path.join(game, "Game");
    const assets = new Thug2Assets(game);
    let bad = 0;
    for (const target of targets) {
        for (const file of levelQbs(target)) {
            const r = checkLevel(new QbFile(fs.readFileSync(file), file, true), (v) => assets.femaleRigs.has(v));
            if (!r.human) continue;
            const animsBad = r.female > 0 && !r.femaleAnimsLoaded;
            const ok = !r.wrong.length && !animsBad;
            print(
                `${path.relative(target, file) || file}: ${r.human} human peds, ${r.female} on Ped_Female` +
                    `${r.wrong.length ? `, ${r.wrong.length} on the wrong skeleton` : ""}` +
                    `${animsBad ? ", Ped_Female animations not loaded (load_level_anims)" : ""}${ok ? " - ok" : ""}`,
            );
            for (const w of r.wrong.slice(0, 5)) print(`    ${w}`);
            if (r.wrong.length > 5) print(`    ... ${r.wrong.length - 5} more`);
            if (!ok) bad += 1;
        }
    }
    return bad ? 1 : 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
