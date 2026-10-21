// NOT ORIGINAL - tests of the THUG1 converter's level naming (no game data): THUG1's own names unless THUG2 uses them,
// the compatibility profile's choices (TP-30), --legacy-names, the skies.

import assert from "node:assert/strict";
import { test } from "node:test";
import { checksum, Name, Struct, Vector } from "../level_port/qb.ts";
import { applyTextEdits, DRIVING_EDITS } from "./driving.ts";
import * as portChanges from "./port_changes.ts";
import { applySpawns, COMPAT, levelName, ownName, setCompat, skyName } from "./thug1_levels.ts";

const struct = (level: string, load: string): Struct =>
    new Struct([
        [new Name(checksum("level")), level],
        [new Name(checksum("load_script")), new Name(checksum(load))],
    ]);

test("ownName: the level= of a structure loaded by Load_<level>", () => {
    assert.equal(ownName(struct("NJ", "Load_NJ")), "nj");
    assert.equal(ownName(struct("hn", "Load_hn")), "hn");
    assert.equal(ownName(struct("mainmenu", "Load_SkateShop")), null); // (THUG1's front end shop)
    assert.equal(ownName(undefined), null);
});

test("levelName: THUG1's own name, thug1_ when THUG2 has it, the profile's choice, legacy", () => {
    const thug2 = new Set(["Load_SE", "Load_SC", "Load_TestLevel"].map((s) => checksum(s)));
    setCompat("thugpro");
    assert.equal(COMPAT.levelNames.get("se"), "hh");
    assert.equal(levelName("NJ", struct("NJ", "Load_NJ"), thug2), "nj");
    assert.equal(levelName("SC", struct("SC", "Load_SC"), thug2), "thug1_sc");
    assert.equal(levelName("SE", struct("SE", "Load_SE"), thug2), "hh");
    assert.equal(levelName("NJ_Skateshop", undefined, thug2), "thug1_nj_skateshop");
    assert.equal(levelName("NJ", struct("NJ", "Load_NJ"), thug2, true), "thug1_nj");
    setCompat("none");
    assert.equal(levelName("SE", struct("SE", "Load_SE"), thug2), "thug1_se");
    setCompat("thugpro");
});

test("skyName follows the level's name", () => {
    assert.equal(skyName("SE", "hh", "SE_Sky"), "hh_sky");
    assert.equal(skyName("NJ", "nj", "NJ_Sky"), "nj_sky");
    assert.equal(skyName("SC", "thug1_sc", "SC_Sky"), "thug1_sc_sky");
    assert.equal(skyName("NJ_Skateshop", "thug1_nj_skateshop", "NJ_Sky"), "thug1_nj_sky");
});

test("TP-31 applySpawns: moves, re-types, renames and reorders the spawns, Links following", () => {
    const names = new Map<number, string>();
    const nm = (t: string): Name => {
        names.set(checksum(t), t);
        return new Name(checksum(t));
    };
    const restart = (name: string, types: string[], x: number, links: number[] = []): Struct =>
        new Struct([
            [nm("Pos"), new Vector("vector", [x, 0, 0])],
            [nm("Name"), nm(name)],
            [nm("Class"), nm("Restart")],
            [nm("restart_types"), types.map(nm)],
            ...(links.length ? ([[nm("Links"), links]] as [Name, number[]][]) : []),
        ]);
    const nodes = [
        restart("A", ["Player1", "Multiplayer"], 0),
        new Struct([
            [nm("Name"), nm("Rail")],
            [nm("Class"), nm("RailNode")],
            [nm("Links"), [2]],
        ]),
        restart("B", ["Multiplayer"], 1),
        restart("C", ["Multiplayer", "Horse"], 2, [0]),
    ];
    const line = applySpawns(
        nodes,
        names,
        {
            moved: { B: { pos: [5, 6, 7], angles: [0, 1, 0] } },
            types: { C: ["Player1"] },
            renamed: { A: "_A" },
            order: ["B", "A"],
        },
        "T",
    );
    assert.match(line, /B moved, C \[Player1\], A -> _A, spawn order/);
    const at = (i: number): string => names.get((nodes[i]!.get("Name") as Name).value)!;
    assert.deepEqual([at(0), at(1), at(2), at(3)], ["B", "Rail", "_A", "C"]);
    assert.deepEqual((nodes[0]!.get("Pos") as Vector).values, [5, 6, 7]);
    assert.deepEqual(nodes[1]!.get("Links"), [0]); // (the rail linked to B, now at 0)
    assert.deepEqual(nodes[3]!.get("Links"), [2]); // (C linked to A, now at 2)
    assert.throws(() => applySpawns(nodes, names, { order: ["B"] }, "T"));
});

test("PC-THUG1-21 / 22: THUGPRO's VC <-> SJ arrows and the limo's emptied scripts taken back, the kill zone kept", () => {
    const targets = (code: string): string[] => (COMPAT.new.get(code)?.entries ?? []).map((e) => e.target);
    setCompat("thugpro");
    for (const name of ["TRG_SCJ_arrow_01", "TRG_Restart_SJVC", "transition_arrowScript", "trp_death_SJScript"])
        assert.ok(!targets("vc").includes(name), name);
    assert.ok(!targets("vc").includes("TRG_drive_veh_limoScript"));
    assert.ok(!targets("vc").includes("VC_startup"));
    assert.ok(targets("vc").includes("TRG_drive_veh_leafScript")); // (the leaf blower stays THUGPRO's: not counted)
    assert.ok(!targets("sj").some((t) => /arrow|VCSJ/i.test(t)));
    assert.ok(!targets("vc").some((t) => /transition_arrow/i.test(t)));
    assert.ok(!(COMPAT.edits.get("vc")?.created_at_start?.off ?? []).includes("trp_death_SJ"));
    assert.deepEqual(COMPAT.edits.get("vc")?.created_at_start?.on, ["TRG_drive_veh_leaf"]);
    assert.deepEqual(portChanges.wonGoals(COMPAT.portChanges).get("vc"), ["VC_Goal_Race4"]);
    assert.equal(portChanges.drivingFiles(COMPAT.portChanges).length, 2);
    // (--no-port-changes: THUGPRO's entries as they are, no driving)
    setCompat("thugpro", true, false);
    assert.ok(targets("vc").includes("TRG_SCJ_arrow_01"));
    assert.ok(targets("vc").includes("TRG_drive_veh_limoScript"));
    assert.ok((COMPAT.edits.get("vc")?.created_at_start?.off ?? []).includes("trp_death_SJ"));
    assert.equal(portChanges.wonGoals(COMPAT.portChanges).size, 0);
    assert.deepEqual(portChanges.drivingFiles(COMPAT.portChanges), []);
    // (--compat none: no THUGPRO entries to take back; the driving stays ours)
    setCompat("none");
    assert.equal(COMPAT.new.size, 0);
    assert.equal(portChanges.drivingFiles(COMPAT.portChanges).length, 2);
    setCompat("thugpro");
});

test("PC-THUG1-22 applyTextEdits: each THUG1 text replaced once, CRLF read as LF; missing or twice is an error", () => {
    assert.equal(applyTextEdits("a \r\nb \r\n", [["a \n", "x \n"]], "f"), "x \nb \n");
    assert.throws(() => applyTextEdits("a \n", [["b \n", "x \n"]], "f"));
    assert.throws(() => applyTextEdits("a \na \n", [["a \n", "x \n"]], "f"));
    // (the files PC-THUG1-22 installs have their THUG2 edits)
    for (const file of portChanges.drivingFiles(portChanges.load())) assert.ok(DRIVING_EDITS[file.toLowerCase()]);
});
