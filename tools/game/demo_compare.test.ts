// NOT ORIGINAL - tests of tools/game/demo_compare.ts's pure parts (no game, no pictures on disk).

import assert from "node:assert/strict";
import { test } from "node:test";
import { compareTracks, compose, groupSets, parseSet, parseTrack, pictureDifference } from "./demo_compare.ts";

test("parseSet reads SET:SECTION.KEY=VALUE", () => {
    assert.deepEqual(parseSet("A:Graphics.ray_tracing=1"), {
        set: "A",
        section: "Graphics",
        key: "ray_tracing",
        value: "1",
    });
    assert.deepEqual(parseSet("before:features.new_frontend="), {
        set: "before",
        section: "features",
        key: "new_frontend",
        value: "",
    });
    for (const bad of ["", "A", "A:Graphics", "A:Graphics=1", "Graphics.msaa=4", ":Graphics.msaa=4"])
        assert.equal(parseSet(bad), null, bad);
});

test("groupSets keeps the sets in the order first named", () => {
    const sets = groupSets(
        ["B:Graphics.msaa=4", "A:Graphics.msaa=0", "B:Graphics.renderer=d3d9"].map((s) => parseSet(s)!),
    );
    assert.deepEqual([...sets.keys()], ["B", "A"]);
    assert.equal(sets.get("B")!.length, 2);
});

const HEADER =
    "# frame flags x y z vx vy vz at_x at_y at_z up_x up_y up_z state cam_x cam_y cam_z cam_dx cam_dy cam_dz";
function line(frame: number, x: number, camX = 0, state = 0): string {
    return `${frame} 3 ${x} 1 2 0 0 0 0 0 1 0 1 0 ${state} ${camX} 5 6 0 0 1`;
}

test("parseTrack and compareTracks: identical, then a drift", () => {
    const a = parseTrack([HEADER, line(5, 0), line(6, 1), line(7, 2)].join("\n"));
    assert.equal(a.size, 3);
    assert.deepEqual(a.get(6)!.position, [1, 1, 2]);
    const same = compareTracks(a, a);
    assert.equal(same.firstFrame, null);
    assert.equal(same.compared, 3);
    const b = parseTrack([HEADER, line(5, 0), line(6, 1.5, 0.25), line(7, 4), line(8, 4)].join("\n"));
    const drift = compareTracks(a, b);
    assert.equal(drift.firstFrame, 6);
    assert.equal(drift.firstSkater, 0.5);
    assert.equal(drift.firstCamera, 0.25);
    assert.equal(drift.maxSkater, 2);
    assert.equal(drift.missing, 1);
    const state = parseTrack([HEADER, line(5, 0), line(6, 1, 0, 4), line(7, 2)].join("\n"));
    assert.equal(compareTracks(a, state).firstFrame, 6);
});

test("compareTracks: a frame one run didn't draw (no camera: flags 1) isn't a difference", () => {
    const a = parseTrack([HEADER, line(5, 0, 3), line(6, 1, 3)].join("\n"));
    const undrawn = `6 1 1 1 2 0 0 0 0 0 1 0 1 0 0 0 0 0 0 0 0`;
    const b = parseTrack([HEADER, line(5, 0, 3), undrawn].join("\n"));
    const drift = compareTracks(a, b);
    assert.equal(drift.firstFrame, null);
    assert.equal(drift.maxCamera, 0);
    const moved = parseTrack([HEADER, line(5, 0, 3), `6 1 1.5 1 2 0 0 0 0 0 1 0 1 0 0 0 0 0 0 0 0`].join("\n"));
    assert.equal(compareTracks(a, moved).firstFrame, 6);
});

function solid(width: number, height: number, value: number) {
    return { width, height, rgb: new Uint8Array(width * height * 3).fill(value) };
}

test("compose lays the pictures out", () => {
    const a = solid(8, 2, 10);
    const b = solid(8, 2, 200);
    const side = compose([a, b], "side", 0);
    assert.equal(side.width, 8 * 2 + 4);
    assert.equal(side.rgb[0], 10);
    assert.equal(side.rgb[(side.width - 1) * 3], 200);
    const split = compose([a, b], "split", 0);
    assert.equal(split.width, 8);
    assert.equal(split.rgb[0], 10);
    assert.equal(split.rgb[7 * 3], 200);
    const wipeStart = compose([a, b], "wipe", 0);
    assert.equal(wipeStart.rgb[7 * 3], 200);
    const wipeEnd = compose([a, b], "wipe", 1);
    assert.equal(wipeEnd.rgb[0], 10);
    const diff = compose([a, b], "diff", 0);
    assert.equal(diff.rgb[0], 255);
});

test("pictureDifference: none for the same picture, all for opposite ones", () => {
    const a = solid(4, 4, 0);
    assert.deepEqual(pictureDifference(a, a), { mean: 0, changed: 0 });
    const b = solid(4, 4, 255);
    assert.deepEqual(pictureDifference(a, b), { mean: 255, changed: 1 });
});
