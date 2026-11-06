import assert from "node:assert/strict";
import * as fs from "node:fs";
import * as path from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";
import type { Image } from "thug2-level-converter-core/lib/frame_diff.ts";
import {
    compareSpot,
    decodePpm,
    difference,
    frameTime,
    groupCommands,
    groupSpots,
    growMask,
    iniFor,
    noiseMask,
    readPlan,
    vulkanFrameLine,
} from "./renderer_parity.ts";

function flat(width: number, height: number, value: number): Image {
    return { width, height, rgb: new Uint8Array(width * height * 3).fill(value) };
}

function poke(image: Image, x: number, y: number, value: number): Image {
    const copy = { ...image, rgb: new Uint8Array(image.rgb) };
    copy.rgb.fill(value, (y * image.width + x) * 3, (y * image.width + x) * 3 + 3);
    return copy;
}

test("the plan's level groups capture each spot, then the commands, the timed wait and quit", () => {
    const group = {
        name: "foun",
        level: "foun",
        spots: [{ name: "a", commands: ["teleport 1 2 3", "wait 10"] }],
        commands: ["camera 1 2 3 0 0 1 1 0 0"],
        frametime: 900,
    };
    assert.deepEqual(groupCommands(group, "vulkan", 2), [
        "waitfor level foun 300",
        "teleport 1 2 3",
        "wait 10",
        "capture 1 parity-vulkan-2-a",
        "wait 2",
        "camera 1 2 3 0 0 1 1 0 0",
        "wait 900",
        "quit",
    ]);
    assert.deepEqual(groupSpots(group), ["a"]);
});

test("a group's own captures get the run's series folder; a window group isn't quit", () => {
    const group = { name: "fe", commands: ["waitfor ready", "capture 1 fe-main"], window: true };
    assert.deepEqual(groupCommands(group, "d3d9", 1), ["waitfor ready", "capture 1 parity-d3d9-1-fe-main"]);
    assert.deepEqual(groupSpots(group), ["fe-main"]);
});

test("the plan in the repo reads, with unique names", () => {
    const here = path.dirname(fileURLToPath(import.meta.url));
    const plan = readPlan(fs.readFileSync(path.join(here, "renderer_parity.json"), "utf8"));
    const names = plan.groups.flatMap(groupSpots);
    assert.equal(new Set(names).size, names.length);
    assert.throws(() => readPlan('{"groups":[{"name":"a b"}]}'));
});

test("the ini: values set in their sections, then the renderer", () => {
    const base = "[features]\nautomation=1\n[Graphics]\nmsaa=8\n";
    const text = iniFor(base, [{ "Graphics.msaa": "0" }, { "features.edit_level": "foun" }], "vulkan");
    assert.equal(text, "[features]\nedit_level=foun\nautomation=1\n[Graphics]\nrenderer=vulkan\nmsaa=0\n");
    assert.throws(() => iniFor(base, [{ msaa: "0" }], "d3d9"));
});

test("a PPM dump reads", () => {
    const data = Buffer.concat([Buffer.from("P6\n2 1\n255\n", "latin1"), Buffer.from([1, 2, 3, 4, 5, 6])]);
    assert.deepEqual(decodePpm(data), { width: 2, height: 1, rgb: new Uint8Array([1, 2, 3, 4, 5, 6]) });
    assert.throws(() => decodePpm(Buffer.from("P5\n1 1\n255\n\0", "latin1")));
});

test("the noise mask: pixels any two runs differ at by more than the threshold, grown", () => {
    const a = flat(5, 5, 100);
    const mask = noiseMask([a, poke(a, 2, 2, 120), poke(a, 0, 0, 105)], 8);
    assert.equal(
        mask.reduce((s, m) => s + m, 0),
        1,
    );
    assert.equal(mask[12], 1);
    const grown = growMask(mask, 5, 5, 1);
    assert.equal(
        grown.reduce((s, m) => s + m, 0),
        9,
    );
    assert.equal(grown[0], 0);
    assert.equal(grown[6], 1);
});

test("the difference outside the mask: mean, signed, largest, the region and the cells", () => {
    const a = flat(100, 100, 50);
    let b = poke(a, 10, 20, 80);
    b = poke(b, 90, 95, 40);
    const all = difference(a, b, null, 8);
    assert.equal(all.largest, 30);
    assert.equal(all.over, 2);
    assert.deepEqual(all.region, [10, 20, 81, 76]);
    assert.deepEqual(all.cells, [
        { x: 0, y: 0, count: 1 },
        { x: 80, y: 80, count: 1 },
    ]);
    assert.ok(Math.abs(all.signed - (30 - 10) / 10000) < 1e-12);
    const mask = new Uint8Array(100 * 100);
    mask[20 * 100 + 10] = 1;
    const outside = difference(a, b, mask, 8);
    assert.equal(outside.pixels, 9999);
    assert.equal(outside.over, 1);
    assert.deepEqual(outside.region, [90, 95, 1, 1]);
});

test("a spot: what moves between one renderer's runs isn't counted against the other", () => {
    const base = flat(10, 10, 60);
    const moving = poke(base, 3, 3, 200);
    const result = compareSpot("s", { d3d9: [base, moving], vulkan: [poke(base, 7, 7, 90), base] }, 8, 0);
    assert.equal(result.all.over, 1); // (run 1 against run 1: only the Vulkan pixel at 7,7)
    assert.equal(result.outside.over, 0); // (7,7 moves between Vulkan's own runs too)
    assert.equal(result.masked, 2 / 100);
});

test("frame times from the status samples taken during the timed wait", () => {
    const samples = [
        { ms: 0, frames: 10, current: "wait 30" },
        { ms: 100, frames: 20, current: "wait 900" },
        { ms: 1100, frames: 80, current: "wait 900" },
        { ms: 1200, frames: 86, current: "quit" },
    ];
    assert.deepEqual(frameTime(samples, 900), { ms: 1000 / 60, frames: 60 });
    assert.equal(frameTime(samples.slice(0, 2), 900), null);
});

test("the Vulkan log's frame line", () => {
    const log =
        "x\nvulkan: frame 197: 52 pipelines (0 refused), ...; 50.95 ms a frame (0.06 ms of it waiting for the device and the swapchain), 300 draws and 6.5 passes a frame over the last 197; uploads\n";
    assert.equal(vulkanFrameLine(log), "50.95 ms a frame (0.06 waiting), 300 draws, 6.5 passes");
    assert.equal(vulkanFrameLine("nothing"), null);
});
