// NOT ORIGINAL - tests of tools/game/sim_check.ts's pure parts (no game).

import assert from "node:assert/strict";
import * as fs from "node:fs";
import * as path from "node:path";
import { test } from "node:test";
import {
    compareStates,
    parseStates,
    passed,
    REFERENCE_FOLDER,
    referenceDemos,
    reportLine,
    undrawnFrames,
} from "./sim_check.ts";

const HEADER = "# frame skater objects random clock_ms";

function states(lines: string[]): Map<number, string> {
    return parseStates([HEADER, ...lines].join("\n"));
}

test("parseStates keys the lines by frame, skipping comments", () => {
    const s = states(["5 AAAA BBBB CCCC 83", "", "6 AAAB BBBB CCCC 100"]);
    assert.equal(s.size, 2);
    assert.equal(s.get(5), "AAAA BBBB CCCC 83");
});

test("compareStates: identical runs pass", () => {
    const ref = states(["5 A B C 83", "6 A B C 100"]);
    const c = compareStates(ref, states(["5 A B C 83", "6 A B C 100"]));
    assert.equal(c.matched, 2);
    assert.ok(passed(c));
    assert.match(reportLine("d", 0, c), /\| ok \|$/);
});

test("compareStates: the first frame that differs and which hashes", () => {
    const ref = states(["5 A B C 83", "6 A B C 100", "7 A B C 116"]);
    const c = compareStates(ref, states(["5 A B C 83", "6 X B Y 100", "7 X B Y 116"]));
    assert.equal(c.firstDiffers, 6);
    assert.equal(c.firstWhat, "skater, random");
    assert.ok(!passed(c));
});

test("compareStates: a frame missing from the run fails it", () => {
    const c = compareStates(states(["5 A B C 83", "6 A B C 100"]), states(["5 A B C 83"]));
    assert.equal(c.missing, 1);
    assert.ok(!passed(c));
});

test("undrawnFrames reads the playback's late frames from the log", () => {
    assert.equal(undrawnFrames("demo: playback pace - 19.60 game s in 19.46 s, 22 of 1176 frames not drawn"), 22);
    assert.equal(undrawnFrames("nothing"), -1);
});

test("every reference demo has its state file", () => {
    const demos = referenceDemos();
    assert.ok(demos.length > 0);
    for (const demo of demos) assert.ok(fs.existsSync(path.join(REFERENCE_FOLDER, `${demo}.state.txt`)), demo);
});
