import assert from "node:assert/strict";
import { test } from "node:test";
import { iniFor, readLog, SCENARIOS, verdict } from "./rt_contract.ts";

const PASSED = [
    "rt-check: the contract check is on ([debug] rt_contract_check=1) - no ray-tracing backend behind it",
    "rt-check: summary frames=600 violations=0 meshes=4210 materials=812 instances/frame=3900 draws/frame=1500 drawn-meshes/frame=1200 adds=4300",
].join("\r\n");

test("a clean log passes", () => {
    const result = readLog(PASSED);
    assert.equal(result.on, true);
    assert.equal(result.frames, 600);
    assert.equal(result.draws, 1500);
    assert.equal(result.violations, 0);
    assert.equal(verdict(result), "");
});

test("violations fail a run, counted from the summary or the lines", () => {
    const text = `${PASSED}\nrt-check: VIOLATION frame 12: instance of mesh 7: never added\nrt-check: VIOLATION remove_mesh 9: never added\n`;
    const result = readLog(text);
    assert.equal(result.violations, 2);
    assert.equal(result.violationLines.length, 2);
    assert.match(verdict(result), /2 violation/);
    const counted = readLog(PASSED.replace("violations=0", "violations=350"));
    assert.equal(counted.violations, 350);
});

test("no check, no frames, no draws or a crash fail", () => {
    assert.match(verdict(readLog("nothing here")), /never on/);
    assert.match(verdict(readLog(PASSED.split("\r\n")[0] ?? "")), /no frame/);
    assert.match(verdict(readLog(PASSED.replace("draws/frame=1500", "draws/frame=0"))), /no draw/);
    assert.match(verdict(readLog(`${PASSED}\nEXCEPTION 0xC0000005 at thug2-decomp.exe+0x1234\n`)), /failed/);
});

test("the ini: renderer, the check, the scenario's values", () => {
    const anteara = SCENARIOS.find((s) => s.name === "anteara");
    assert.ok(anteara);
    const text = iniFor("[Graphics]\r\nrenderer=vulkan\r\n[features]\r\nanteara_mod=0\r\n", "d3d9", anteara);
    assert.match(text, /renderer=d3d9/);
    assert.match(text, /\[debug\]\r\nrt_contract_check=1/);
    assert.match(text, /anteara_mod=1/);
    assert.doesNotMatch(text, /anteara_mod=0/);
});

test("every scenario ends the game and checks a level", () => {
    for (const s of SCENARIOS) {
        const commands = s.commands("vulkan");
        assert.equal(commands[commands.length - 1], "quit");
        assert.ok(commands.some((c) => c.startsWith("waitfor level")));
        assert.ok(commands.includes("console rt summary"));
    }
});
