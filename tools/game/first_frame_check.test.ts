import assert from "node:assert/strict";
import { test } from "node:test";
import { configValues, difference, graphicsOf, levelCommands, logTimes, SPOTS } from "./first_frame_check.ts";

test("graphicsOf keeps the [Graphics] keys but the window's", () => {
    const ini = "[Features]\nx=1\n[Graphics]\nrt_ao=1\nResolutionX=2560\nmsaa=8\nWindowed=1\n[Other]\nrt_ao=0\n";
    assert.deepEqual(graphicsOf(ini), { "Graphics.rt_ao": "1", "Graphics.msaa": "8" });
});

test("configValues: off turns ray tracing and every rt_ effect off", () => {
    const g = { "Graphics.rt_ao": "1", "Graphics.ray_tracing": "1", "Graphics.msaa": "8" };
    assert.deepEqual(configValues("on", g), g);
    assert.deepEqual(configValues("off", g), {
        "Graphics.rt_ao": "0",
        "Graphics.ray_tracing": "0",
        "Graphics.msaa": "8",
    });
});

test("levelCommands arm the capture before the level is up", () => {
    const commands = levelCommands(SPOTS[0]!, "on");
    assert.ok(commands[1]!.startsWith("capture 61 firstframe-on-BO afterload"));
    assert.ok(commands[2]!.startsWith("waitfor level BO"));
});

test("difference counts pixels over the threshold outside the mask", () => {
    const a = { width: 2, height: 1, rgb: new Uint8Array([0, 0, 0, 100, 100, 100]) };
    const b = { width: 2, height: 1, rgb: new Uint8Array([20, 0, 0, 100, 100, 100]) };
    assert.equal(difference(a, b, new Uint8Array([0, 0]), 12).changed, 1);
    assert.equal(difference(a, b, new Uint8Array([1, 0]), 12).changed, 0);
});

test("logTimes reads the level's lines before the series' first frame", () => {
    const log = [
        "rt: a new world's first frame: 1 world meshes, 2 instances - the structures built and compacted in 3 ms, the baked sun fitted, 500 ms in all before it is drawn",
        "rt: the new world's first frame took 2000 ms (500 ms of it its structures and sun)",
        "loading console: the loading screen was up 2.1 s",
        "rt: a new world's first frame: 9 world meshes, 9 instances - the structures built and compacted in 50 ms, the baked sun fitted, 417 ms in all before it is drawn",
        "screenshot: series - the first frame after the loading screen",
        "rt: the new world's first frame took 653 ms (417 ms of it its structures and sun)",
    ].join("\n");
    const t = logTimes(log);
    assert.equal(t.work, 417);
    assert.equal(t.firstFrame, 653);
    assert.equal(t.loading, 2.1);
    assert.equal(t.workBeforeFirst, true);
    assert.equal(t.crashed, false);
});
