// NOT ORIGINAL - tests of tools/game/clip_frames.ts's pure parts (no game, no clip).

import assert from "node:assert/strict";
import { test } from "node:test";
import {
    buildIndex,
    indexText,
    lineForFrame,
    parseCrop,
    parseFacts,
    parseNumberList,
    parseSidecarFrames,
    selectFrames,
} from "./clip_frames.ts";

const SIDECAR = [
    "THUG2 decomp clip thug2_x.mp4",
    "",
    "[clip]",
    "file:  thug2_x.mp4",
    "",
    "[frames]",
    "frame     vf    time_s     game  dt_ms   pos_x pos_y pos_z   dir_x dir_y dir_z",
    "    1      1     0.017      100  16.70       1.00      2.00      3.00   0.0000  0.0000  1.0000",
    "    2      3     0.041      101  24.00       1.50      2.00      3.50   0.0000  0.0000  1.0000",
    "    3      5     0.083      102  41.00          - - -        - - -",
    "",
].join("\n");

test("parseNumberList takes whole numbers or seconds and refuses junk and negatives", () => {
    assert.deepEqual(parseNumberList("1, 2,3", true), [1, 2, 3]);
    assert.deepEqual(parseNumberList("0.5,2", false), [0.5, 2]);
    assert.equal(parseNumberList("1.5", true), null);
    assert.equal(parseNumberList("1,,2", true), null);
    assert.equal(parseNumberList("-1", true), null);
    assert.equal(parseNumberList("a", false), null);
});

test("parseCrop wants four numbers with a size", () => {
    assert.deepEqual(parseCrop("10,20,300,200"), { x: 10, y: 20, w: 300, h: 200 });
    assert.equal(parseCrop("10,20,0,200"), null);
    assert.equal(parseCrop("1,2,3"), null);
});

test("parseSidecarFrames reads the lines under [frames], the camera's dashes as null", () => {
    const lines = parseSidecarFrames(SIDECAR);
    assert.equal(lines.length, 3);
    assert.deepEqual(lines[0]!.position, [1, 2, 3]);
    assert.deepEqual(lines[0]!.direction, [0, 0, 1]);
    assert.equal(lines[1]!.vf, 3);
    assert.equal(lines[2]!.position, null);
    assert.equal(lines[2]!.game, 102);
});

test("selectFrames: every N, listed frames, times; clamped, sorted, without repeats", () => {
    const facts = { fps: 60, frames: 300 };
    assert.deepEqual(selectFrames({ every: 100 }, facts), [0, 100, 200]);
    assert.deepEqual(selectFrames({}, facts), [0, 60, 120, 180, 240]);
    assert.deepEqual(selectFrames({ frames: [250, 10, 10, 999] }, facts), [10, 250, 299]);
    assert.deepEqual(selectFrames({ at: [0, 1.5, 4.999] }, facts), [0, 90, 299]);
});

test("lineForFrame is the last displayed frame that first shows at or before the video frame", () => {
    const lines = parseSidecarFrames(SIDECAR);
    assert.equal(lineForFrame(lines, 0), null);
    assert.equal(lineForFrame(lines, 1)?.frame, 1);
    assert.equal(lineForFrame(lines, 4)?.frame, 2);
    assert.equal(lineForFrame(lines, 90)?.frame, 3);
});

test("buildIndex names the files by video frame and indexText lists them with the pose", () => {
    const lines = parseSidecarFrames(SIDECAR);
    const entries = buildIndex([1, 4], 60, lines);
    assert.deepEqual(
        entries.map((e) => e.file),
        ["vf_000001.png", "vf_000004.png"],
    );
    const text = indexText(
        "C:/x/thug2_x.mp4",
        { codec: "h264", width: 1280, height: 720, fps: 60, frames: 6 },
        entries,
    );
    assert.match(text, /thug2_x\.mp4: 1280x720, 60 a second, 6 frames, h264/);
    assert.match(text, /vf_000004\.png .* 1\.50 2\.00 3\.50/);
});

test("parseFacts reads the exe's line or its error", () => {
    assert.deepEqual(parseFacts("clip: h264 1280x720 60.000 300\n"), {
        codec: "h264",
        width: 1280,
        height: 720,
        fps: 60,
        frames: 300,
    });
    assert.deepEqual(parseFacts("clip: error - no such file"), { error: "no such file" });
    assert.ok("error" in parseFacts(""));
});
