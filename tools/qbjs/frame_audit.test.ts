// NOT ORIGINAL - frame_audit.ts: each pattern found and classed on small scripts shaped as emit_ts.ts writes them.
import assert from "node:assert/strict";

import { test } from "node:test";

import { analyzeSource, type Finding, report } from "./frame_audit.ts";

const SOURCE = `import { script, N, f, add, sub, eq, gt, test, repeat, random } from "qb";
const { wait, Wait, WaitOneGameFrame, WaitFramesLateOllie, PlayCam } = cmd;
script("Timed", function* (p) {
    yield* wait(2, N.seconds);
    yield* Wait(3, N.frames);
    yield* wait(250);
    yield* wait(1, N.gameframe);
    yield* wait(30, N.gameframes);
    yield* Wait(1, N.game, N.frame);
    yield* wait(p.n, N.gameframe);
    yield* Wait(...random(1, [[3, N.Seconds], [4, N.Seconds]]));
    yield* Wait(...random(2, [[5, N.gameframes], [9, N.gameframes]]));
    yield* wait(1, N.one_per_frame);
});
script("Poll", function* (p) {
    while (true) {
        if (test(eq(p.done, 1))) {
            break;
        }
        yield* WaitOneGameFrame();
    }
});
script("Counted", function* (p) {
    const loop = repeat();
    do {
        p.alpha = add(p.alpha, f(0.1));
        yield* wait(1, N.gameframe);
    } while (loop.again(20));
    p.timer = 0;
    while (true) {
        p.timer = add(p.timer, 1);
        if (test(gt(p.timer, 45))) {
            break;
        }
        yield* wait(1, N.gameframe);
    }
    const loop2 = repeat();
    do {
        yield* wait(1, N.frame);
    } while (loop2.again(10));
    yield* WaitFramesLateOllie({ frames: 6 });
    yield* PlayCam({ name: N.cam, frames: 300, BoardOffFrame: 12, wireframe_mode: 1 });
});
`;

const find = (all: Finding[], line: number, pattern: string) =>
    all.filter((x) => x.line === line && x.pattern === pattern);

test("waits: each unit and its class", () => {
    const all = analyzeSource(SOURCE, "x.ts");
    const one = (line: number, pattern: string) => {
        const hit = find(all, line, pattern);
        assert.equal(hit.length, 1, `line $line$pattern: $JSON.stringify(all.filter((x) => x.line === line))`);
        return hit[0]!;
    };
    assert.equal(one(4, "wait-seconds").cls, "c");
    assert.equal(one(5, "wait-frames").cls, "c");
    assert.equal(one(6, "wait-ms").cls, "c");
    assert.equal(one(7, "wait-gameframes").cls, "c");
    assert.equal(one(8, "wait-gameframes").cls, "a");
    assert.match(one(8, "wait-gameframes").why, /0.5000 s/);
    assert.equal(one(9, "wait-gameframes").cls, "c"); // Wait 1 game frame
    assert.equal(one(10, "wait-gameframes").cls, "b");
    assert.equal(one(11, "wait-seconds").cls, "c");
    assert.equal(one(12, "wait-gameframes").cls, "b");
    assert.equal(one(13, "wait-one-per-frame").cls, "c");
    assert.equal(one(4, "wait-seconds").script, "Timed");
});

test("loops: polls, repeat counts, counters, per-tick increments", () => {
    const all = analyzeSource(SOURCE, "x.ts");
    const poll = find(all, 20, "wait-one-game-frame");
    assert.equal(poll.length, 1);
    assert.equal(poll[0]!.cls, "c");
    assert.equal(poll[0]!.script, "Poll");
    // repeat 20 { alpha += 0.1; wait 1 gameframe }
    const repeat20 = find(all, 25, "frame-loop");
    assert.equal(repeat20.length, 1);
    assert.equal(repeat20[0]!.cls, "a");
    assert.equal(find(all, 26, "per-tick-increment")[0]?.cls, "b");
    // timer = timer + 1 ... compared with 45
    assert.equal(find(all, 31, "frame-counter")[0]?.cls, "a");
    // repeat 10 { wait 1 frame }: already time
    assert.equal(find(all, 38, "frame-loop")[0]?.cls, "c");
    assert.equal(find(all, 41, "frame-command")[0]?.cls, "a");
});

test("frame-named arguments", () => {
    const all = analyzeSource(SOURCE, "x.ts");
    assert.equal(find(all, 41, "frame-argument")[0]?.cls, "b"); // frames: 6
    const cam = find(all, 42, "frame-argument");
    assert.deepEqual(cam.map((x) => x.cls).sort(), ["b", "c"]); // frames: 300 (b), BoardOffFrame (c); wireframe not counted
});

test("report: counts per pattern and class", () => {
    const text = report(analyzeSource(SOURCE, "x.ts"), 1, false);
    assert.match(text, /| wait-gameframes | 1 | 2 | 2 | 5 |/);
    assert.match(text, /## Class a/);
    assert.doesNotMatch(text, /## Class c/);
});
