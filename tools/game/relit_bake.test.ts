import assert from "node:assert/strict";
import { test } from "node:test";
import { bakeMade, chooseLevels, parseBakeLog, reportLine, startedLevel } from "./relit_bake.ts";

const LOG = [
    "rt: a new world's first frame: 2843 world meshes",
    "relit bake: level 1a2b3c4d (BO) - relit: 2843 meshes (12 not relightable), 131072 vertices; the baked sun found " +
        "(confidence 0.45, margin 1.50); 87 lamps; 25000000 rays traced (300000 for the lamps) in 9000 ms, the fit and " +
        "the tints 2000 ms",
    "relit bake: the model sun 0.444 0.430 0.401 sky 0.300 0.310 0.330 bounce 0.200 0.200 0.200 lamps 0.120 0.100 " +
        "0.050 scale 1.010 (margin 8.80, median error 0.0400, 100000 vertices fitted, 0 dead sun channels); the bake's " +
        "sun the rays missed at 3000 vertices, lamp light at 2000 (mean 0.050), residual mean 0.0100",
    "relit bake: wrote C:\\Games\\THUG2-1\\Game\\thug2-decomp-relit\\1a2b3c4d.relit (9000 KB, content 0123456789abcdef) - 12000 ms in all",
].join("\n");

test("a bake's lines: the level, its verdict, counts, model and file", () => {
    const result = parseBakeLog(LOG);
    assert.equal(result.scene, "1a2b3c4d");
    assert.equal(result.verdict, "relit");
    assert.equal(result.meshes, 2843);
    assert.equal(result.vertices, 131072);
    assert.equal(result.lamps, 87);
    assert.equal(result.sun, "0.444 0.430 0.401");
    assert.equal(result.lampTerm, "0.120 0.100 0.050");
    assert.equal(result.written, true);
    assert.match(result.file ?? "", /1a2b3c4d\.relit$/);
    assert.match(reportLine("BO", 0, result), /^BO\t1a2b3c4d\trelit\t2843 meshes/);
});

test("a level whose bake shows no sun, and a run with no bake line", () => {
    const none = parseBakeLog(
        "relit bake: level 00ff00ff (AP) - no sun in the bake: 900 meshes (0 not relightable), 50000 vertices; the baked " +
            "sun none (confidence 0.12, margin 0.40); 30 lamps; 0 rays traced (0 for the lamps) in 0 ms, the fit and the tints 0 ms\n" +
            "relit bake: wrote C:\\g\\thug2-decomp-relit\\00ff00ff.relit (1 KB, content 00) - 10 ms in all",
    );
    assert.equal(none.verdict, "no sun in the bake");
    assert.equal(none.written, true);
    const failed = parseBakeLog("rt: nothing\n");
    assert.equal(failed.written, false);
    assert.match(reportLine("ny", null, failed), /^ny\tFAILED\texit killed/);
});

test("a run that ended with a batch run's failure is FAILED, with its reason", () => {
    const never = parseBakeLog(
        "start level: Load_BO (free skate)\n" +
            "relit bake: FAILED - Load_BO never came up: load_mainmenu still running after 300 s (the log's " +
            '"start level:" line says why) - nothing written\n',
    );
    assert.equal(never.written, false);
    assert.match(never.failure ?? "", /^Load_BO never came up/);
    assert.match(reportLine("BO", 2, never), /^BO\tFAILED\texit 2\tLoad_BO never came up/);
    const refused = parseBakeLog("start level: Load_xx isn't a level's load script - the main menu stays\n");
    assert.match(reportLine("xx", 2, refused), /^xx\tFAILED\texit 2\tLoad_xx isn't a level's load script/);
    // (written, but the game didn't end with 0: not made)
    const crashed = parseBakeLog(LOG);
    assert.equal(bakeMade(0, crashed), true);
    assert.equal(bakeMade(5, crashed), false);
    assert.match(reportLine("BO", 3, parseBakeLog("")), /^BO\tFAILED\texit 3\tthe level came up with another scene/);
});

test("a log shows its level started - not the main menu's world baked under its name", () => {
    assert.equal(startedLevel("x\nstart level: Load_BO (free skate)\n", "BO"), true);
    assert.equal(startedLevel("start level: load_thps3_ap (free skate)\n", "thps3_ap"), true);
    assert.equal(startedLevel("relit bake: level a94aa09c (BO) - relit: 201 meshes\n", "BO"), false);
    assert.equal(startedLevel("start level: Load_BOX (free skate)\n", "BO"), false);
});

test("the levels chosen: all installed, or by name with or without Load_", () => {
    const levels = [
        { load: "BO", name: "Boston", game: "THUG2", installed: true },
        { load: "TR", name: "Training", game: "THUG2", installed: true },
        { load: "Default", name: "Default", game: "THUG2", installed: false },
        { load: "nj", name: "New Jersey", game: "THPS3", installed: true },
    ];
    assert.deepEqual(
        chooseLevels(levels, "all").chosen.map((l) => l.load),
        ["BO", "TR", "nj"],
    );
    const some = chooseLevels(levels, "load_bo,NJ,Default,xx");
    assert.deepEqual(
        some.chosen.map((l) => l.load),
        ["BO", "nj"],
    );
    assert.deepEqual(some.unknown, ["Default", "xx"]);
});
