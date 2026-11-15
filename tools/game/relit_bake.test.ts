import assert from "node:assert/strict";
import { test } from "node:test";
import { chooseLevels, parseBakeLog, reportLine } from "./relit_bake.ts";

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
