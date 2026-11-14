// NOT ORIGINAL - tests of tools/game/tick_compare.ts's pure parts (no game).

import assert from "node:assert/strict";
import { test } from "node:test";
import { parseTrack } from "./demo_compare.ts";
import { compareRuns, findJumps, reportLines } from "./tick_compare.ts";

// A track line: frame flags x y z vx vy vz at(3) up(3) state cam(3) dir(3).
function line(frame: number, y: number, state: number, x = 0): string {
    return `${frame} 3 ${x} ${y} 0 1 0 0 1 0 0 0 1 0 ${state} 0 10 0 0 0 1`;
}

function track(points: [number, number, number][], dx = 0): string {
    return points.map(([frame, y, state]) => line(frame, y, state, dx)).join("\n");
}

test("findJumps finds the runs in the air with their apex", () => {
    const t = parseTrack(
        track([
            [0, 0, 0],
            [1, 5, 1],
            [2, 9, 1],
            [3, 4, 1],
            [4, 0, 0],
            [5, 3, 1],
        ]),
    );
    assert.deepEqual(findJumps(t), [
        { takeoff: 1, landing: 4, apex: 9 },
        { takeoff: 5, landing: -1, apex: 3 },
    ]);
});

test("compareRuns: identical runs differ nowhere", () => {
    const text = track([
        [0, 0, 0],
        [1, 5, 1],
        [2, 0, 0],
    ]);
    const c = compareRuns(parseTrack(text), parseTrack(text));
    assert.equal(c.compared, 3);
    assert.equal(c.maxPosition, 0);
    assert.equal(c.firstOver, -1);
    assert.equal(c.stateMismatches, 0);
    assert.equal(c.jumps.length, 1);
    assert.deepEqual(c.jumps[0]!.base, c.jumps[0]!.other);
});

test("compareRuns: a shifted run, a late landing", () => {
    const base = parseTrack(
        track([
            [0, 0, 0],
            [1, 5, 1],
            [2, 0, 0],
            [3, 0, 0],
        ]),
    );
    const other = parseTrack(
        track(
            [
                [0, 0, 0],
                [1, 5, 1],
                [2, 1, 1],
                [3, 0, 0],
            ],
            0.5,
        ),
    );
    const c = compareRuns(base, other);
    assert.equal(c.firstOver, 0);
    assert.ok(Math.abs(c.maxPosition - Math.hypot(0.5, 1)) < 1e-9);
    assert.equal(c.stateMismatches, 1);
    assert.equal(c.jumps[0]!.other!.landing, 3);
    const report = reportLines(
        "d",
        [60, 120],
        new Map([
            [60, compareRuns(base, base)],
            [120, c],
        ]),
    ).join("\n");
    assert.match(report, /\| 120 \| 4 \| 0 \|/);
    assert.match(report, /16\.7 ms/); // (the landing a frame later)
});
