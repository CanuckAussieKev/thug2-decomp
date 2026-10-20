// NOT ORIGINAL - unit tests of the THPS3 converter's THUGPRO compatibility pieces (no game data needed).

import assert from "node:assert/strict";
import { test } from "node:test";
import * as qb from "../level_port/qb.ts";
import { Name, type QbValue, Struct, Vector } from "../level_port/qb.ts";
import { renameScene } from "./migrate_layouts.ts";
import { ctfNodes, legacyName, levelName } from "./thps3_levels.ts";

const n = (text: string): Name => new Name(qb.checksum(text));
const vec = (...values: number[]): Vector => new Vector("vector", values);

test("level names: THPS3's own for its network levels, thps3_ for the rest and for THUG2's own names", () => {
    const regular = new Struct([
        [n("load_script"), n("Load_Foun")],
        [null, n("regular_level")],
    ]);
    const debug = new Struct([
        [n("load_script"), n("Load_SkateShop")],
        [null, n("debug_level")],
    ]);
    const thug2 = new Set([qb.checksum("Load_AP"), qb.checksum("Level_AP")]);
    assert.equal(levelName("Foun", regular, thug2), "foun");
    assert.equal(levelName("Ap", regular, thug2), "thps3_ap"); // (THUG2's own Load_AP: checksums are case-blind)
    assert.equal(levelName("SkateShop", debug, thug2), "thps3_skateshop");
    assert.equal(levelName("Sk3Ed_Bch", null, thug2), "thps3_sk3ed_bch");
    assert.equal(levelName("Foun", regular, thug2, true), "thps3_foun");
    assert.equal(legacyName("Rio_Sky"), "thps3_rio_sky");
});

test("capture the flag: THPS3's flag bases and team restarts as THUG2's game-mode nodes", () => {
    const names = new Map<number, string>();
    const blue = qb.checksum("CTF_1");
    const array: QbValue[] = [
        new Struct([
            [n("Position"), vec(10, 0, 20)],
            [n("Angles"), vec(0, 1, 0)],
            [n("Name"), n("TRG_CTF_TEAM1")],
            [n("Class"), n("GenericNode")],
            [n("Type"), n("CTF_1")],
        ]),
        new Struct([
            [n("Position"), vec(500, 0, 20)],
            [n("Name"), n("TRG_CTF1_Restart_1P")],
            [n("Class"), n("Restart")],
            [n("restart_types"), [n("MultiPlayer"), n("CTF_1")]],
        ]),
        new Struct([
            [n("Position"), vec(40, 0, 20)],
            [n("Name"), n("TRG_CTF1_Restart_2P")],
            [n("Class"), n("Restart")],
            [n("restart_types"), [n("MultiPlayer"), n("CTF_1")]],
        ]),
        new Struct([
            [n("Position"), vec(11, 0, 20)],
            [n("Name"), n("TRG_Restart_Other")],
            [n("Class"), n("Restart")],
            [n("restart_types"), [n("MultiPlayer")]],
        ]),
    ];
    for (const t of ["GenericNode", "Restart", "TRG_CTF_TEAM1", "TRG_CTF1_Restart_2P"]) names.set(qb.checksum(t), t);
    const [made, scripts, report] = ctfNodes(array, names, [
        { colour: "Blue", type: blue },
        { colour: "Red", type: qb.checksum("CTF_2") }, // (no base in this array: nothing made)
    ]);
    const byName = new Map(made.map((s) => [(s.get("Name") as Name).value, s]));
    assert.deepEqual(
        [...byName.keys()],
        ["TRG_CTF_Blue", "TRG_CTF_Blue_Base", "TRG_CTF_Restart_Blue"].map(qb.checksum),
    );
    const flag = byName.get(qb.checksum("TRG_CTF_Blue"))!;
    assert.deepEqual((flag.get("Position") as Vector).values, [10, 0, 20]);
    assert.equal(flag.get("Model"), "gameobjects\\flags\\flag_blue\\flag_blue.mdl");
    assert.equal((flag.get("TriggerScript") as Name).value, qb.checksum("TRG_CTF_BlueScript"));
    assert.ok(flag.hasFlag("NeverSuspend") && !flag.hasFlag("CreatedAtStart"));
    // the team's restart: its own type's nearest to the base (not the closer restart of another type)
    const restart = byName.get(qb.checksum("TRG_CTF_Restart_Blue"))!;
    assert.deepEqual((restart.get("Position") as Vector).values, [40, 0, 20]);
    assert.deepEqual(
        (restart.get("restart_types") as Name[]).map((t) => t.value),
        ["CTF", "MultiPlayer", "Horse"].map(qb.checksum),
    );
    assert.equal(scripts, "script TRG_CTF_BlueScript {\n    Team_Flag blue \n}\n");
    assert.match(report, /Blue \(base TRG_CTF_TEAM1, restart at TRG_CTF1_Restart_2P\)/);
});

test("capture the flag with the compatibility data (TP-10 / TP-11): places from it, the form and restarts ours", () => {
    const names = new Map<number, string>();
    const array: QbValue[] = [
        new Struct([
            [n("Position"), vec(10, 0, 20)],
            [n("Name"), n("TRG_CTF_TEAM1")],
            [n("Class"), n("GenericNode")],
            [n("Type"), n("CTF_1")],
        ]),
        new Struct([
            [n("Position"), vec(100, 0, -200)], // (THUG2's world: z 200)
            [n("Name"), n("TRG_MP_1")],
            [n("Class"), n("Restart")],
            [n("restart_types"), [n("MultiPlayer"), n("CTF_1")]],
        ]),
        new Struct([
            [n("Position"), vec(900, 0, 0)],
            [n("Name"), n("TRG_MP_2")],
            [n("Class"), n("Restart")],
            [n("restart_types"), [n("Multiplayer")]],
        ]),
    ];
    for (const t of ["GenericNode", "Restart"]) names.set(qb.checksum(t), t);
    const p = (pos: number[]) => ({ pos, angles: [0, 0, 0] });
    const [made, scripts] = ctfNodes(array, names, [{ colour: "Blue", type: qb.checksum("CTF_1") }], {
        flags: { TRG_CTF_Blue: p([90, 0, 210]), TRG_CTF_Blue_Base: p([91, 0, 210]) },
        extra: { TRG_CTF_Green: p([880, 0, 0]), TRG_Flag_Red: p([1, 2, 3]), TRG_Team_Restart: p([4, 5, 6]) },
    });
    const byName = new Map(made.map((s) => [(s.get("Name") as Name).value, s]));
    const posOf = (nm: string): readonly number[] => (byName.get(qb.checksum(nm))!.get("Pos") as Vector).values;
    assert.deepEqual(posOf("TRG_CTF_Blue"), [90, 0, 210]);
    assert.deepEqual(posOf("TRG_CTF_Blue_Base"), [91, 0, 210]);
    assert.deepEqual(posOf("TRG_CTF_Green"), [880, 0, 0]);
    assert.deepEqual(posOf("TRG_Team_Restart"), [4, 5, 6]);
    // restarts: THPS3's (Position), nearest to the flag in THUG2's world
    const restartOf = (c: string) =>
        (byName.get(qb.checksum(`TRG_CTF_Restart_${c}`))!.get("Position") as Vector).values;
    assert.deepEqual(restartOf("Blue"), [100, 0, -200]);
    assert.deepEqual(restartOf("Green"), [900, 0, 0]);
    assert.equal(byName.get(qb.checksum("TRG_Flag_Red"))!.get("Model"), "gameobjects\\flags\\flag_red\\flag_red.mdl");
    assert.match(scripts, /script TRG_Flag_RedScript \{\n {4}Team_Flag red \n\}/);
    assert.match(scripts, /script TRG_CTF_GreenScript \{\n {4}Team_Flag green \n\}/);
});

test("layouts: the scene line renamed, the rest kept byte for byte", () => {
    const old = qb.checksum("thps3_foun");
    const now = qb.checksum("foun");
    const hex = (v: number): string => v.toString(16).toUpperCase().padStart(8, "0");
    const text = `# layout\r\nscene ${hex(old)}  # thps3_foun\r\nsector 12345678 1 0.5 0 0  # x\r\n`;
    const [out, lines] = renameScene(text, old, now, "foun");
    assert.equal(lines, 1);
    assert.equal(out, `# layout\r\nscene ${hex(now)}  # foun\r\nsector 12345678 1 0.5 0 0  # x\r\n`);
    assert.throws(() => renameScene(text, now, old, "x"));
});
