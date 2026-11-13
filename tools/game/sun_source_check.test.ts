import assert from "node:assert/strict";
import { test } from "node:test";
import { configValues } from "./rt_shadows_check.ts";
import {
    degreesApart,
    discoveryPlan,
    lastSunReport,
    parseSunLine,
    pictureStats,
    sideBySide,
    sunPlan,
    sunProblems,
} from "./sun_source_check.ts";

const LINE =
    "[12.3] rt: sun: the game's 0.0000 0.3420 -0.9397 (heading 180.0, elevation 20.0); game 0.0000 0.3420 -0.9397 " +
    "(heading 180.0, elevation 20.0); baked 0.5000 0.7000 0.5099 (heading 44.4, elevation 44.4); shadows 0.0000 0.3420 " +
    "-0.9397 (heading 180.0, elevation 20.0); fog 0.0000 0.3420 -0.9397 (heading 180.0, elevation 20.0); drawn sun none";

test("the console's `rt sun` line read back", () => {
    const report = parseSunLine(LINE)!;
    assert.equal(report.origin, "the game's");
    assert.deepEqual(report.scene, [0, 0.342, -0.9397]);
    assert.deepEqual(report.game, [0, 0.342, -0.9397]);
    assert.deepEqual(report.baked, [0.5, 0.7, 0.5099]);
    assert.deepEqual(
        report.users.map((u) => u.name),
        ["shadows", "fog", "drawn sun"],
    );
    assert.equal(report.users[2]!.toward, null);
    assert.equal(parseSunLine("rt: the baked sun: found"), null);
    assert.equal(lastSunReport(`x\n${LINE}\ny\n`)?.origin, "the game's");
    const none = parseSunLine("rt: sun: none (the bake shows no sun); game 0 1 0 (heading 0.0, elevation 90.0)")!;
    assert.equal(none.origin, "none (the bake shows no sun)");
    assert.equal(none.scene, null);
});

test("the effects against the scene's sun", () => {
    const report = parseSunLine(LINE)!;
    assert.deepEqual(sunProblems(report), ["drawn sun has no sun"]);
    report.users[2]!.toward = [0.005, 0.342, -0.9397];
    assert.deepEqual(sunProblems(report), []);
    report.users[1]!.toward = [0.5, 0.7, 0.5099];
    assert.equal(sunProblems(report).length, 1);
    // (no scene sun: the drawn sun may show the game's, nothing else may light by one)
    const none = parseSunLine(
        "rt: sun: none (the bake shows no sun); drawn sun 0 1 0 (heading 0.0, elevation 90.0); fog none",
    )!;
    assert.deepEqual(sunProblems(none), []);
    assert.ok(degreesApart([0, 1, 0], [1, 0, 0]) > 89.9);
});

test("the plans: discovery, then the suns' spots", () => {
    const sky = {
        groups: [
            {
                name: "BO",
                level: "BO",
                spots: [
                    {
                        name: "BO-ground",
                        commands: ["teleport 10.0 20.0 30.0 90.0", "camera 10 160 30 1 0.08 0", "wait 150"],
                    },
                    { name: "BO-up", commands: ["camera 10 1520 30 0.5 0.87 0"] },
                ],
            },
        ],
    };
    const discover = discoveryPlan(sky);
    assert.equal(discover.groups[0]!.spots!.length, 1);
    assert.deepEqual(discover.groups[0]!.commands, ["wait 120", "console rt sun"]);
    const plan = sunPlan(sky, { BO: parseSunLine(LINE)! });
    const names = plan.groups[0]!.spots!.map((s) => s.name);
    assert.deepEqual(names, ["BO-ground", "BO-above", "BO-sun-game", "BO-sun-baked"]);
    // (toward the game's sun: heading 180, pitched 14 under its 20 degrees)
    assert.match(plan.groups[0]!.spots![2]!.commands[0]!, /^camera 10\.0 320\.0 30\.0 0\.000 0\.105 -0\.995$/);
    assert.deepEqual(plan.groups[0]!.commands, ["console rt sun"]);
});

test("pictures: stats and side by side", () => {
    const grey = { width: 2, height: 1, rgb: new Uint8Array([100, 100, 100, 0, 0, 0]) };
    const stats = pictureStats(grey);
    assert.equal(stats.black, 0.5);
    assert.ok(Math.abs(stats.luma - 50) < 1e-9);
    const both = sideBySide(grey, grey);
    assert.equal(both.width, 2 + 8 + 2);
    assert.equal(both.rgb[(2 + 8) * 3], 100);
});

test("the configurations: level and game", () => {
    assert.equal(configValues("off", "sun-source")["Graphics.rt_sun_source"], "level");
    assert.equal(configValues("on", "sun-source")["Graphics.rt_sun_source"], "game");
});
