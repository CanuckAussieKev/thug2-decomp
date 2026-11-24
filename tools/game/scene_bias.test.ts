// NOT ORIGINAL - tests of tools/game/scene_bias.ts's pure parts (no game data).

import assert from "node:assert/strict";
import { test } from "node:test";
import { kSceneSpots, parseSaved, planCommands, ratios, shotName } from "./scene_bias.ts";

test("the plan sets the time of day before the levels and names each reference by tag, spot and time", () => {
    const p1 = kSceneSpots.find((s) => s.name === "p1")!;
    const tr = kSceneSpots.find((s) => s.name === "tr")!;
    const lines = planCommands([p1, tr], ["noon", "night"], 256, "base");
    assert.equal(lines[0], "waitfor ready 600");
    assert.equal(lines[1], "console rt tod noon");
    assert.ok(lines.includes("level bo"));
    assert.ok(lines.includes("level tr"));
    assert.ok(lines.includes("console pt ref 256 as scene_base_p1_noon"));
    assert.ok(lines.includes("console pt ref 256 as scene_base_tr_night"));
    assert.ok(lines.indexOf("console rt tod night") > lines.indexOf("console pt ref 256 as scene_base_tr_noon"));
    assert.equal(shotName("", "no", "night"), "scene_no_night");
});

test("parseSaved reads the path tracer's saved line", () => {
    const log =
        "pt: saved C:\\Games\\THUG2-3\\Screenshots\\scene_base_p1_night_reference / _realtime (.exr, .png)  - 853x480, 256 " +
        "samples a pixel, EV100 0.89; 107 emitting triangles listed, 3 lamps sampled analytically (1 by their surfaces), " +
        "sky grid, lights by power, light counted: all; 0 samples not finite; mean luma reference 1.95e-05, realtime " +
        "9.7e-06 (x 0.4974 of the reference)\n";
    assert.deepEqual(parseSaved(log), [
        {
            name: "scene_base_p1_night",
            reference: 1.95e-5,
            realtime: 9.7e-6,
            ratio: 0.4974,
        },
    ]);
});

test("ratios leave out pixels far over the reference's 99th percentile", () => {
    const reference = new Float64Array(1000).fill(1);
    const realtime = new Float64Array(1000).fill(0.5);
    realtime[3] = 1e6; // a firefly in the realtime picture
    const r = ratios(realtime, reference);
    assert.ok(r.whole > 100);
    assert.equal(r.trimmed, 0.5);
    assert.equal(r.kept, 0.999);
});
