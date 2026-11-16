import assert from "node:assert/strict";
import { test } from "node:test";
import { stalls, stretches, verdict } from "./soak_check.ts";

const frame = (n: number, ms: number, over: number, uploads: number): string =>
    `vulkan: frame ${n}: 3 pipelines (0 refused), shaders 0 compiled in 0.0 s and 6 from the disk cache, pipelines made ` +
    `in 0.0 s; ${ms.toFixed(2)} ms a frame (0.17 ms of it waiting for the device and the swapchain), 4 draws and 1.0 ` +
    `passes a frame over the last ${over}; uploads ${uploads} (79.0 MB) in 0.04 s; 1 waits for the device`;
const scene = (builds: number, written: number): string =>
    `vulkan: ray tracing scene: 222 meshes (222 built, 0 compacted, 43 dynamic, 0 refused), BLAS 3.2 MB (3.2 MB before ` +
    `compaction), TLAS + instances 0.1 MB, scratch 0.9 MB; 222 instances in the last TLAS; ${builds} builds, 0 refits, ` +
    `1 TLAS builds (asked for: 222 new meshes, 0 new geometry, ${written} written)`;
const LOADING = "loading console: the loading screen was up 1.1 s";

// A level session of seven windows after a short front end, then the quit's load.
function session(lastMs: number, lastUploads: number, lastBuilds: number): string {
    const lines = [frame(100, 30, 100, 500), LOADING, scene(3000, 0)];
    let builds = 3000;
    for (let i = 1; i <= 7; i++) {
        const late = i >= 6;
        builds += late ? lastBuilds : 10;
        lines.push(frame(100 + i * 300, late ? lastMs : 10, 300, late ? lastUploads : 300), scene(builds, 0));
    }
    lines.push(LOADING, frame(2500, 40, 50, 100));
    return lines.join("\n");
}

test("the log's stretches between loading screens, each window with the scene's counts before it", () => {
    const all = stretches(session(10, 300, 10));
    assert.equal(all.length, 3);
    assert.equal(all[1]!.length, 7);
    assert.equal(all[1]![0]!.ms, 10);
    assert.equal(all[1]![0]!.uploads, 300);
    assert.equal(all[1]![1]!.builds, 3010);
});

test("a session as fast at its end as at its start is flat", () => {
    const result = verdict(session(10, 300, 10));
    assert.ok(result);
    assert.deepEqual(result.growing, []);
    assert.equal(result.first.ms, 10);
    assert.equal(result.first.uploads, 1);
});

test("frame time, uploads and BLAS builds that grow are each reported", () => {
    const result = verdict(session(340, 300 * 1946, 3000));
    assert.ok(result);
    assert.deepEqual(result.growing, ["frame time", "uploads a frame", "BLAS builds a frame"]);
});

test("stalls: each stall once, the device's waits, the longest", () => {
    const log = [
        frame(100, 10, 100, 1),
        "stall: the main loop's loop 101 has run 2.3 s (the game not responding) - in rt_begin; its stack: a.exe+0x10",
        "stall: the main loop's loop 101 has run 12.3 s (the game not responding) - in rt_begin; its stack: a.exe+0x10",
        "stall: over - the main loop ran on after 13.0 s",
        "vulkan: frame 102 has waited 4 s for the device (its work not finished) - still waiting",
        "stall: the main loop's loop 900 has run 2.0 s (the game not responding) - in logic; its stack: a.exe+0x20",
    ].join("\n");
    assert.deepEqual(stalls(log), { count: 2, longestSeconds: 12.3, deviceWaits: 1 });
    assert.deepEqual(stalls(session(10, 300, 10)), { count: 0, longestSeconds: 0, deviceWaits: 0 });
});

test("too few windows: nothing to judge", () => {
    assert.equal(verdict([frame(1, 10, 10, 1), frame(2, 10, 10, 1)].join("\n")), null);
});
