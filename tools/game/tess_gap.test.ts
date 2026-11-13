import assert from "node:assert/strict";
import { test } from "node:test";
import { fan, probeResults, slopeGaps, summarise } from "./tess_gap.ts";

const LOG = [
    "automation: colprobe 0.0000 60.0000 0.0000 0.0000 -0.5000 0.8660 1500 -> hit 100.5 point 0 10 87 normal 0 0.7071 -0.7071 face 3 object 0",
    "automation: rtprobe 0.0000 60.0000 0.0000 0.0000 -0.5000 0.8660 1500 -> hit 101.25 instance 4 mesh 9 material 2 primitive 7 point 0 9 88",
    // a floor: left out
    "automation: colprobe 1.0000 60.0000 0.0000 0.0000 -1.0000 0.0000 1500 -> hit 60 point 1 0 0 normal 0 1 0 face 1 object 0",
    "automation: rtprobe 1.0000 60.0000 0.0000 0.0000 -1.0000 0.0000 1500 -> hit 60 instance 1 mesh 1 material 1 primitive 1 point 1 0 0",
    // a different surface behind: left out
    "automation: colprobe 2.0000 60.0000 0.0000 0.0000 -0.5000 0.8660 1500 -> hit 100 point 0 0 0 normal 0 0.5 0.866 face 1 object 0",
    "automation: rtprobe 2.0000 60.0000 0.0000 0.0000 -0.5000 0.8660 1500 -> hit 300 instance 1 mesh 1 material 1 primitive 1 point 0 0 0",
    "automation: rtprobe 3.0000 60.0000 0.0000 0.0000 -0.5000 0.8660 1500 -> miss",
].join("\n");

test("the probes' results paired by ray; the gap on slopes only, the same surface only", () => {
    const { col, rt } = probeResults(LOG);
    assert.equal(col.size, 3);
    assert.equal(rt.size, 3);
    const gaps = slopeGaps(LOG);
    assert.equal(gaps.size, 1);
    // (0.75 along the ray, which meets the slope's normal at cos = |(0, -0.5, 0.866) . (0, 0.7071, -0.7071)|)
    assert.ok(Math.abs([...gaps.values()][0]! - 0.75 * (0.5 + 0.866) * Math.SQRT1_2) < 1e-4);
});

test("the summary: count, mean and largest |gap|, each way", () => {
    const s = summarise([0.5, -1, 0.25]);
    assert.equal(s.count, 3);
    assert.equal(s.maxAbs, 1);
    assert.equal(s.mostBehind, 0.5);
    assert.equal(s.mostInFront, -1);
    assert.ok(Math.abs(s.meanAbs - 0.5833333) < 1e-6);
    assert.equal(summarise([]).count, 0);
});

test("the fan: every 10 degrees round, four pitches down, unit directions from above the point", () => {
    const rays = fan(10, 20, 30);
    assert.equal(rays.length, 36 * 4);
    for (const r of rays) {
        assert.deepEqual(r.origin, [10, 170, 30]);
        assert.ok(Math.abs(Math.hypot(...r.direction) - 1) < 1e-9);
        assert.ok(r.direction[1] < 0);
    }
});
