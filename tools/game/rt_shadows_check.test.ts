import assert from "node:assert/strict";
import * as fs from "node:fs";
import * as path from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";
import type { Image } from "thug2-level-converter-core/lib/frame_diff.ts";
import { readPlan } from "./renderer_parity.ts";
import {
    anyShadow,
    configValues,
    darkestRatio,
    judgeSpot,
    LIMITS,
    type Limits,
    logProblems,
    noiseOf,
    shadowDifference,
} from "./rt_shadows_check.ts";

const SHADOWS: Limits = { ...LIMITS, expect: "shadows", strength: 0.5 };
const NONE: Limits = { ...LIMITS, expect: "none", strength: 0.5 };

function flat(width: number, height: number, value: number): Image {
    return { width, height, rgb: new Uint8Array(width * height * 3).fill(value) };
}

// The pixels of rows from `top` on multiplied by `factor` (a shadow over the picture's lower part).
function shaded(image: Image, top: number, factor: number): Image {
    const rgb = new Uint8Array(image.rgb);
    for (let p = top * image.width * 3; p < rgb.length; p++) rgb[p] = Math.round(rgb[p]! * factor);
    return { ...image, rgb };
}

test("a shadow over the lower quarter: darkened there, nothing brighter, the ratio the strength's", () => {
    const off = flat(40, 40, 200);
    const on = shaded(off, 30, 0.5);
    const d = shadowDifference(off, on, null, 8);
    assert.equal(d.pixels, 1600);
    assert.equal(d.darkened, 400);
    assert.equal(d.lowerHalf, 400);
    assert.equal(d.brighter, 0);
    assert.equal(d.medianRatio, 0.5);
    const verdict = judgeSpot("a", d, null, SHADOWS);
    assert.deepEqual(verdict.problems, []);
    assert.equal(anyShadow([verdict], SHADOWS), true);
});

test("brighter pixels, too much darkened or the wrong ratio fail; none darkened anywhere fails", () => {
    const off = flat(10, 10, 100);
    const brighter = shaded(off, 0, 1.5);
    assert.match(judgeSpot("b", shadowDifference(off, brighter, null, 8), null, SHADOWS).problems.join(), /brighter/);
    const all = shaded(off, 0, 0.5);
    assert.match(judgeSpot("c", shadowDifference(off, all, null, 8), null, SHADOWS).problems.join(), /darkened/);
    const faint = shaded(off, 9, 0.85);
    assert.match(judgeSpot("d", shadowDifference(off, faint, null, 8), null, SHADOWS).problems.join(), /ratio/);
    const same = judgeSpot("e", shadowDifference(off, off, null, 8), null, SHADOWS);
    assert.deepEqual(same.problems, []);
    assert.equal(anyShadow([same], SHADOWS), false);
});

test("expect none: any change fails; the baseline must match off", () => {
    const off = flat(20, 20, 120);
    assert.deepEqual(judgeSpot("a", shadowDifference(off, off, null, 8), null, NONE).problems, []);
    const changed = shaded(off, 10, 0.5);
    assert.match(judgeSpot("b", shadowDifference(off, changed, null, 8), null, NONE).problems.join(), /skipped/);
    const baseline = shadowDifference(changed, off, null, 8);
    assert.match(judgeSpot("c", shadowDifference(off, off, null, 8), baseline, NONE).problems.join(), /baseline/);
});

test("the noise mask leaves out what differs between a configuration's runs", () => {
    const a = flat(4, 4, 50);
    const b = { ...a, rgb: new Uint8Array(a.rgb) };
    b.rgb.fill(200, 0, 3); // pixel 0 differs between the runs
    const mask = noiseOf([[a, b], [a]], 8, 0);
    assert.equal(mask[0], 1);
    assert.equal(
        mask.reduce((s, m) => s + m, 0),
        1,
    );
    const on = shaded(a, 0, 0.5);
    assert.equal(shadowDifference(a, on, mask, 8).pixels, 15);
});

test("the logs: the pass drawn with shadows expected, skipped without; crashes always", () => {
    const drawn = "vulkan: rt_shadows: the first shadow pass (1280x720, per sample, ...)";
    const skipped = "vulkan: rt_shadows: no acceleration structure for the scene yet - the shadow pass skipped";
    assert.deepEqual(logProblems("on", drawn, "shadows"), []);
    assert.match(logProblems("on", skipped, "shadows").join(), /no shadow pass/);
    assert.deepEqual(logProblems("on", skipped, "none"), []);
    assert.match(logProblems("on", drawn, "none").join(), /skipped/);
    assert.deepEqual(logProblems("off", "", "shadows"), []);
    assert.match(logProblems("off", "EXCEPTION 0xC0000005", "shadows").join(), /EXCEPTION/);
});

test("the configurations' ini values, and the plan's level groups", () => {
    assert.deepEqual(configValues("on"), { "Graphics.ray_tracing": "1", "Graphics.rt_shadows": "1" });
    assert.deepEqual(configValues("off"), { "Graphics.ray_tracing": "1", "Graphics.rt_shadows": "0" });
    assert.deepEqual(configValues("baseline"), {});
    const here = path.dirname(fileURLToPath(import.meta.url));
    const plan = readPlan(fs.readFileSync(path.join(here, "rt_shadows.json"), "utf8"));
    assert.ok(plan.groups.length >= 5);
    for (const g of plan.groups) assert.ok(g.level && g.spots?.length, g.name);
});

const AO: Limits = { ...LIMITS, expect: "shadows", strength: 0.6, effect: "ao" };

test("ambient occlusion: partial darkening passes; brighter or darker than full occlusion fails", () => {
    const off = flat(40, 40, 200);
    const corners = shaded(off, 30, 0.7); // (1 - 0.6 x 0.5)
    const d = shadowDifference(off, corners, null, 8, darkestRatio(AO));
    assert.equal(d.tooDark, 0);
    assert.deepEqual(judgeSpot("a", d, null, AO).problems, []);
    assert.equal(anyShadow([judgeSpot("a", d, null, AO)], AO), true);
    const black = shaded(off, 30, 0); // (a NaN or a runaway value)
    const dark = shadowDifference(off, black, null, 8, darkestRatio(AO));
    assert.equal(dark.tooDark, 400);
    assert.match(judgeSpot("b", dark, null, AO).problems.join(), /darker than full occlusion/);
    const brighter = shaded(off, 30, 1.2);
    assert.match(
        judgeSpot("c", shadowDifference(off, brighter, null, 8, darkestRatio(AO)), null, AO).problems.join(),
        /brighter/,
    );
});

test("ambient occlusion: its ini values and log lines", () => {
    assert.deepEqual(configValues("on", "ao"), {
        "Graphics.ray_tracing": "1",
        "Graphics.rt_shadows": "0",
        "Graphics.rt_ao": "1",
    });
    assert.deepEqual(configValues("off", "ao"), {
        "Graphics.ray_tracing": "1",
        "Graphics.rt_shadows": "0",
        "Graphics.rt_ao": "0",
    });
    const drawn =
        "vulkan: rt_ao: the first ambient occlusion pass (1280x720, 4 rays, radius 48.0, strength 0.60, denoised)";
    const skipped = "vulkan: rt_ao: no acceleration structure for the scene yet - the ambient occlusion pass skipped";
    assert.deepEqual(logProblems("on", drawn, "shadows", "ao"), []);
    assert.match(logProblems("on", skipped, "shadows", "ao").join(), /no ambient occlusion pass/);
    assert.deepEqual(logProblems("on", skipped, "none", "ao"), []);
    assert.equal(darkestRatio(SHADOWS), 0);
});
