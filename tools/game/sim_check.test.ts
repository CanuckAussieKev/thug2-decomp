// NOT ORIGINAL - tests of tools/game/sim_check.ts's pure parts (no game).

import assert from "node:assert/strict";
import * as fs from "node:fs";
import * as path from "node:path";
import { test } from "node:test";
import {
    compareStates,
    compareTod,
    iniValue,
    parseStates,
    parseTod,
    passed,
    REFERENCE_FOLDER,
    REFERENCE_SETTINGS,
    referenceDemos,
    reportLine,
    runIni,
    runName,
    scriptsMode,
    settingsOverridden,
    todLine,
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

test("compareStates: the animation's hash compared when the reference has it", () => {
    const old = compareStates(states(["5 A B C 83"]), states(["5 A B C 83 E"]));
    assert.ok(passed(old)); // (a reference from before the anim field: its four fields)
    const c = compareStates(states(["5 A B C 83 E"]), states(["5 A B C 83 F"]));
    assert.equal(c.firstWhat, "anim");
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

test("a run under load has its own folder and report line", () => {
    assert.equal(runName(144), "144");
    assert.equal(runName(0, 7), "0-load7");
    const ref = states(["5 A B C 83"]);
    const line = reportLine("d", 0, compareStates(ref, ref), 30, 7);
    assert.match(line, /^\| d \| 60 \(max_fps=0\), load 7 \| 1 \/ 1 \| 0 \| none \| 30 \| ok \|$/);
});

test("every reference demo has its state file", () => {
    const demos = referenceDemos();
    assert.ok(demos.length > 0);
    for (const demo of demos) assert.ok(fs.existsSync(path.join(REFERENCE_FOLDER, `${demo}.state.txt`)), demo);
});

test("the scripts a folder runs: its ini's ts_scripts and whether it is unpacked", () => {
    const ini = "[Graphics]\nts_scripts=1\n[features]\nmute_audio=1\nts_scripts = 1\n";
    assert.equal(iniValue(ini, "features", "ts_scripts"), "1");
    assert.equal(iniValue(ini, "Features", "missing"), "");
    assert.equal(scriptsMode(ini, true), "ts");
    assert.equal(scriptsMode(ini, false), "mismatch");
    assert.equal(scriptsMode("[features]\nts_scripts=0\n", false), "qb");
    assert.equal(scriptsMode("", false), "qb");
    assert.equal(scriptsMode("", true), "mismatch");
});

test("the time of day's lines: kept apart from the state, compared with the first run", () => {
    const lines = [
        HEADER,
        "5 A B C 83",
        "#tod 5 13.5 6a3f6d2d 0.1 0.9 0.2",
        "6 A B C 100",
        "#tod 6 13.51 6a3f6d2d 0.1 0.9 0.2",
    ];
    const text = lines.join("\n");
    assert.equal(parseStates(text).size, 2);
    const first = parseTod(text);
    assert.equal(first.get(6), "13.51 6a3f6d2d 0.1 0.9 0.2");
    const same = compareTod(first, parseTod(text));
    assert.equal(same.firstDiffers, -1);
    assert.match(todLine("d", 144, same, 7), /^\| d \| 144, load 7 \| 2 \/ 2 \| none \| ok \|$/);
    const other = compareTod(first, parseTod(text.replace("#tod 6 13.51", "#tod 6 13.52")));
    assert.equal(other.firstDiffers, 6);
    assert.equal(compareTod(first, new Map()).firstDiffers, 5);
});

test("every run is played with the reference's settings: THAW's parked BMX off, no start level", () => {
    const ini = [
        "[features]",
        "thaw_bmx=1",
        "start_level=sch",
        "start_at=1,2,3,4",
        "ts_scripts=1",
        "",
        "[Graphics]",
        "max_fps=144",
        "",
    ].join("\r\n");
    const run = runIni(ini, 0, 7);
    for (const [section, key, value] of REFERENCE_SETTINGS) assert.equal(iniValue(run, section, key), value);
    assert.equal(iniValue(run, "features", "ts_scripts"), "1"); // the workspace's own scripts mode kept
    assert.equal(iniValue(run, "Graphics", "max_fps"), "0");
    assert.equal(iniValue(run, "debug", "load_test"), "7");
    assert.ok(run.includes("\r\n"));
    assert.deepEqual(
        settingsOverridden(ini).map((s) => s.split("=")[0]),
        ["[features] thaw_bmx", "[features] start_level", "[features] start_at"],
    );
    assert.deepEqual(settingsOverridden(run), []);
});
