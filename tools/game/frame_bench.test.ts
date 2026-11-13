import assert from "node:assert/strict";
import { test } from "node:test";
import { measured, parseFrames, parseSamples, percentile, scenario, summarise } from "./frame_bench.ts";

// A run: two loading loops, then four measured loops (skip 1) - drawn, drawn, skipped, drawn - and the closing five.
const loop = (f: number, d: number, l: number, t: number, w: number, p: number[]): string =>
    JSON.stringify({ f, d, l, t, w, p });
const FRAMES = [
    JSON.stringify({ phases: ["pace", "logic"] }),
    loop(1, 1, 1, 100, 100, [0, 50]),
    loop(2, 1, 1, 100, 100, [0, 50]),
    loop(3, 1, 0, 16, 10, [6, 2]), // (skipped: the settle loop)
    JSON.stringify({ phases: ["pace", "logic", "render"] }),
    loop(4, 1, 0, 20, 18, [2, 3, 12]),
    loop(5, 1, 0, 16, 12, [4, 2, 8]),
    loop(6, 0, 0, 14, 4, [10, 2, 0]),
    loop(7, 1, 0, 40, 38, [2, 6, 30]),
    ...[8, 9, 10, 11, 12].map((f) => loop(f, 1, 0, 16, 10, [6, 2, 2])),
    '{"f":13,"d"', // (cut off when the game closed)
].join("\n");

test("frames.jsonl parsed: the phases' names (the last list), each loop", () => {
    const frames = parseFrames(FRAMES);
    assert.deepEqual(frames.phases, ["pace", "logic", "render"]);
    assert.equal(frames.loops.length, 12);
    assert.equal(frames.loops[0]!.loading, true);
    assert.equal(frames.loops[5]!.drawn, false);
});

test("the loops measured: after the last loading loop and the skip, without the closing five", () => {
    const loops = measured(parseFrames(FRAMES), 1);
    assert.deepEqual(
        loops.map((l) => l.frame),
        [4, 5, 6, 7],
    );
});

test("the summary: drawn frames' spacing (a skipped loop's time added to the next), work, hitches, phases", () => {
    const s = summarise(parseFrames(FRAMES), 1);
    assert.equal(s.loops, 4);
    assert.equal(s.drawn, 3);
    // spacing: frame 5 16 ms after 4, frame 7 14 + 40 after 5
    assert.equal(s.interval, (16 + 54) / 2);
    assert.equal(s.worst, 54);
    assert.equal(s.hitches, 1);
    assert.equal(s.work, (18 + 12 + 38) / 3);
    assert.equal(s.drawnPhases.render, (12 + 8 + 30) / 3);
    assert.equal(s.skippedPhases.pace, 10);
    assert.ok(Math.abs(s.fps - 3 / 0.09) < 1e-9);
});

test("percentile: the value at p of the sorted list", () => {
    assert.equal(percentile([5, 1, 4, 2, 3], 0.5), 3);
    assert.equal(percentile([5, 1, 4, 2, 3], 0.99), 4);
    assert.equal(percentile([], 0.99), 0);
});

test("samples.txt parsed: the total, each phase / function's counts (the exe's own column optional)", () => {
    const text = [
        "# 120 samples of the main thread; phase\tmodule\trva\town\tinclusive\texe own",
        "logic\tthug2-decomp.exe\t780E80\t10\t40\t12",
        "hitch logic\tntdll.dll\t1623C0\t3\t3",
    ].join("\n");
    const { total, samples } = parseSamples(text);
    assert.equal(total, 120);
    assert.equal(samples.length, 2);
    assert.deepEqual(samples[0], {
        phase: "logic",
        module: "thug2-decomp.exe",
        rva: 0x780e80,
        own: 10,
        inclusive: 40,
        exeOwn: 12,
    });
    assert.equal(samples[1]!.exeOwn, 0);
});

test("the scenario: the level waited for, a five-second step per five seconds asked, then quit", () => {
    const commands = scenario("BO", 10);
    assert.equal(commands[0], "waitfor level BO 300");
    assert.equal(commands.filter((c) => c.startsWith("hold up")).length, 2);
    assert.equal(commands.at(-1), "quit");
});
