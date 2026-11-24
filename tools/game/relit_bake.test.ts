import assert from "node:assert/strict";
import { test } from "node:test";
import {
    bakeMade,
    chooseLevels,
    disagreements,
    parseBakeLog,
    reportLine,
    startedLevel,
    stateOf,
    stateRow,
} from "./relit_bake.ts";

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
    // (the front end's own world: no level started, the menu's level asked for - start_level.cpp)
    const menu = "start level: MainMenu is the front end's own level - up already, the menu stays (batch run)\n";
    assert.equal(startedLevel(menu.replace("MainMenu", "Load_MainMenu"), "MainMenu"), true);
    assert.equal(startedLevel(menu.replace("MainMenu", "Load_MainMenu"), "BO"), false);
    assert.equal(parseBakeLog(menu).failure, null);
    // (the front end's world is up from the first frames: baked before the menu's start-level step - the bake's line)
    const up =
        "relit bake: Load_MainMenu is up (0 s) - its world (scene a94aa09c) baked once it has held for 90 frames\n";
    assert.equal(startedLevel(up, "MainMenu"), true);
    assert.equal(startedLevel(up, "BO"), false);
});

test("the levels chosen: all installed and the front end's world, or by name with or without Load_", () => {
    const levels = [
        { load: "BO", name: "Boston", game: "THUG2", installed: true },
        { load: "TR", name: "Training", game: "THUG2", installed: true },
        { load: "Default", name: "Default", game: "THUG2", installed: false },
        { load: "nj", name: "New Jersey", game: "THPS3", installed: true },
    ];
    assert.deepEqual(
        chooseLevels(levels, "all").chosen.map((l) => l.load),
        ["BO", "TR", "nj", "MainMenu"],
    );
    const some = chooseLevels(levels, "load_bo,NJ,Default,xx,mainmenu");
    assert.deepEqual(
        some.chosen.map((l) => l.load),
        ["BO", "nj", "MainMenu"],
    );
    assert.deepEqual(some.unknown, ["Default", "xx"]);
});

// (lighting round A2) the level's lighting state from its own evidence, and where it disagrees with itself; (round A3)
// its lamps by the bake's light and their own daylight in place of the "dark level"
const EVIDENCE = (tod: string, sun: string, elevation: number, open: number, onAtOwn = 3, shining = 12): string =>
    `the level's own time of day ${tod}; the baked sun's confidence 0.45 (${sun}, ${elevation} degrees up); vertex ` +
    `light median 0.500, brightest 5% from 1.200 (x2.40); 10 glow, 0 self-lit and 40 textured triangles (5 glow ` +
    `meshes), 12 lamps - ${shining} shining, 10 lit in the bake, 2 always on (no sky where they light), ${onAtOwn} on ` +
    `at its own time (its open sky 0.9500); upward faces see ${open} of the sky (median); the bake's sky 0.100 0.120 ` +
    `0.150 in the global unit, its daylight 0.85 of the physical`;

test("the state table: a level's state from its evidence, its disagreements flagged", () => {
    const day = parseBakeLog(
        `relit bake: evidence - ${EVIDENCE("afternoon", "a sun", 40, 0.8)}\n` +
            "relit bake: the lamps' solve - 900 900 900 edges, 10 of 12 lamps told by the bake\n",
    );
    assert.deepEqual(day.state, {
        tod: "afternoon",
        sun: true,
        confidence: 0.45,
        elevation: 40,
        brightest: 2.4,
        textured: 40,
        glowMeshes: 5,
        lamps: 12,
        shining: 12,
        litInBake: 10,
        alwaysOn: 2,
        onAtOwn: 3,
        ownSky: 0.95,
        openSky: 0.8,
        sky: 0.12,
        daylight: 0.85,
    });
    assert.equal(day.lampsSolved, "10 of 12");
    assert.equal(day.lampPhysics, null);
    const physics = parseBakeLog(
        "relit bake: lamps by physics - 364 lamps: 30 solved, 200 by their fixture's model; lumens per shining lamp: " +
            "median 8000, 90th percentile 20000, most 40000; 2000000 lm in all\n",
    );
    assert.match(
        physics.lampPhysics ?? "",
        /^364 lamps: 30 solved, 200 by their fixture's model; lumens per shining lamp/,
    );
    assert.match(
        stateRow("BO", day),
        /^BO\tafternoon\tsun 40 deg \(0\.45\)\t.*12 lamps \(12 shining, 10 lit in the bake, 2 always on, 40 textured triangles, 5 light cards hidden\)\toutdoors/,
    );
    assert.match(stateRow("BO", day), /\tagrees$/);
    // an evening level with no sun agrees; a day level outdoors with no sun found (Skatopia's case), a night level with a
    // high sun, a sun indoors don't
    assert.deepEqual(disagreements(stateOf(EVIDENCE("evening", "no sun", 0, 0.7))!), []);
    assert.match(disagreements(stateOf(EVIDENCE("afternoon", "no sun", 0, 0.7))!).join(), /but no sun found/);
    assert.match(disagreements(stateOf(EVIDENCE("night", "a sun", 50, 0.7))!).join(), /a sun 50 degrees up/);
    assert.match(disagreements(stateOf(EVIDENCE("afternoon", "a sun", 40, 0.05))!).join(), /indoors/);
    // (round A3) a night with no lamp on and no sun is lit by nothing; an indoor level with no lamp shining
    assert.match(disagreements(stateOf(EVIDENCE("night", "no sun", 0, 0.7, 0))!).join(), /lit by nothing/);
    assert.match(disagreements(stateOf(EVIDENCE("afternoon", "no sun", 0, 0.05, 0, 0))!).join(), /no lamp shining/);
    assert.match(stateRow("foun", parseBakeLog("")), /no evidence line/);
});
