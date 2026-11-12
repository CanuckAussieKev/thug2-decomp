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
    darkOverBright,
    frameTimes,
    judgeSpot,
    LIMITS,
    type Limits,
    logProblems,
    noiseOf,
    otsuThreshold,
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
    // (a median ratio over 0.9: too faint; under 1 - strength - 0.2: too dark; a share of a shadow between is fine)
    const light = flat(10, 10, 200);
    const faint = shaded(light, 8, 0.95);
    assert.match(judgeSpot("d", shadowDifference(light, faint, null, 8), null, SHADOWS).problems.join(), /ratio/);
    const black = shaded(light, 8, 0.2);
    assert.match(judgeSpot("d2", shadowDifference(light, black, null, 8), null, SHADOWS).problems.join(), /ratio/);
    const part = shaded(light, 8, 0.75);
    assert.deepEqual(judgeSpot("d3", shadowDifference(light, part, null, 8), null, SHADOWS).problems, []);
    // a level with no baked sun: any change fails
    assert.match(
        judgeSpot("d4", shadowDifference(light, part, null, 8), null, SHADOWS, true).problems.join(),
        /no baked sun/,
    );
    assert.deepEqual(judgeSpot("d5", shadowDifference(light, light, null, 8), null, SHADOWS, true).problems, []);
    const same = judgeSpot("e", shadowDifference(off, off, null, 8), null, SHADOWS);
    assert.deepEqual(same.problems, []);
    assert.equal(anyShadow([same], SHADOWS), false);
});

test("the baked split: Otsu's threshold between two levels; darkened counted per side", () => {
    const histogram = new Array<number>(256).fill(0);
    histogram[40] = 100;
    histogram[200] = 300;
    const split = otsuThreshold(histogram);
    assert.ok(split >= 40 && split < 200);
    // the upper half bright (200), the lower dark (40); the shadow over the lower quarter: dark pixels only
    const off = flat(20, 20, 200);
    for (let p = 10 * 20 * 3; p < off.rgb.length; p++) off.rgb[p] = 40;
    const d = shadowDifference(off, shaded(off, 15, 0.5), null, 8);
    assert.equal(d.bright, 200);
    assert.equal(d.dark, 200);
    assert.equal(d.brightDarkened, 0);
    assert.equal(d.darkDarkened, 100);
});

test("the sun against the bake: baked-dark darkened as often as baked-bright gives 1", () => {
    const off = flat(20, 20, 200);
    for (let p = 10 * 20 * 3; p < off.rgb.length; p++) off.rgb[p] = 40;
    // the lower quarter (dark) and the second quarter (bright) both darkened: alike
    const both = shaded(off, 15, 0.5);
    for (let p = 5 * 20 * 3; p < 10 * 20 * 3; p++) both.rgb[p] = 100;
    const alike = judgeSpot("a", shadowDifference(off, both, null, 8), null, SHADOWS);
    assert.ok(Math.abs(darkOverBright([alike])! - 1) < 1e-9);
    const lined = judgeSpot("b", shadowDifference(off, shaded(off, 15, 0.5), null, 8), null, SHADOWS);
    assert.ok(darkOverBright([lined])! > 1e6);
    assert.equal(darkOverBright([judgeSpot("c", shadowDifference(off, off, null, 8), null, SHADOWS)]), null);
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

const REFLECTIONS: Limits = { ...LIMITS, expect: "shadows", strength: 0.5, effect: "reflections" };

test("reflections: lighter and darker changes pass; most of the picture changed or turned black fails", () => {
    const off = flat(40, 40, 120);
    const floor = { ...off, rgb: new Uint8Array(off.rgb) };
    floor.rgb.fill(200, 30 * 40 * 3, 35 * 40 * 3); // a band lighter
    floor.rgb.fill(60, 35 * 40 * 3); // and one darker
    const d = shadowDifference(off, floor, null, 8);
    assert.equal(d.brighter, 200);
    assert.equal(d.darkened, 200);
    assert.equal(d.blackened, 0);
    assert.deepEqual(judgeSpot("a", d, null, REFLECTIONS).problems, []);
    assert.equal(anyShadow([judgeSpot("a", d, null, REFLECTIONS)], REFLECTIONS), true);
    const black = shaded(off, 30, 0); // (a NaN or a missing texture)
    assert.match(
        judgeSpot("b", shadowDifference(off, black, null, 8), null, REFLECTIONS).problems.join(),
        /turned black/,
    );
    const everything = shaded(off, 0, 1.5);
    assert.match(
        judgeSpot("c", shadowDifference(off, everything, null, 8), null, REFLECTIONS).problems.join(),
        /changed with the reflections on/,
    );
    const same = judgeSpot("d", shadowDifference(off, off, null, 8), null, REFLECTIONS);
    assert.equal(anyShadow([same], REFLECTIONS), false);
});

test("reflections: its ini values and log lines", () => {
    assert.deepEqual(configValues("on", "reflections"), {
        "Graphics.ray_tracing": "1",
        "Graphics.rt_shadows": "0",
        "Graphics.rt_ao": "0",
        "Graphics.rt_reflections": "1",
    });
    assert.equal(configValues("off", "reflections")["Graphics.rt_reflections"], "0");
    assert.deepEqual(configValues("baseline", "reflections"), {});
    const drawn =
        "vulkan: rt_reflections: the first reflection pass (1280x720, 12 reflective instances of 900, strength 0.50)";
    const nothing =
        "vulkan: rt_reflections: nothing reflective in the scene (no environment-mapped material) - skipped";
    const failed = "vulkan: rt_reflections: the passes couldn't be made (a compile error) - no ray-traced reflections";
    assert.deepEqual(logProblems("on", drawn, "shadows", "reflections"), []);
    assert.deepEqual(logProblems("on", nothing, "shadows", "reflections"), []);
    assert.match(logProblems("on", failed, "shadows", "reflections").join(), /couldn't/);
    assert.match(logProblems("on", "", "shadows", "reflections").join(), /no reflection pass/);
    assert.match(logProblems("on", drawn, "none", "reflections").join(), /reflection pass/);
    assert.deepEqual(logProblems("off", "", "shadows", "reflections"), []);
});

const HDR: Limits = { ...LIMITS, expect: "shadows", strength: 0, effect: "hdr" };
const HDR_BRIGHTER: Limits = { ...HDR, expect: "brighter" };

test("HDR: the defaults' shoulder passes; a large change fails; its ini values and log lines", () => {
    const off = flat(40, 40, 120);
    off.rgb.fill(255, 0, 40 * 3); // a white row: the shoulder takes it to ~247
    const on = { ...off, rgb: new Uint8Array(off.rgb) };
    on.rgb.fill(247, 0, 40 * 3);
    const d = shadowDifference(off, on, null, 8);
    assert.equal(d.maxDelta, 8);
    assert.equal(d.whiteOff, 40);
    assert.equal(d.whiteOn, 0);
    assert.deepEqual(judgeSpot("a", d, null, HDR).problems, []);
    assert.equal(anyShadow([judgeSpot("a", d, null, HDR)], HDR), true);
    const wrong = shaded(off, 20, 0.5);
    assert.match(judgeSpot("b", shadowDifference(off, wrong, null, 8), null, HDR).problems.join(), /changed by/);
    assert.deepEqual(configValues("on", "hdr"), { "Graphics.rt_hdr": "1" });
    assert.deepEqual(configValues("off", "hdr"), { "Graphics.rt_hdr": "0" });
    const pass = "vulkan: rt_hdr: the first tone-mapping pass (1280x720, 1 sample; exposure +0.00, curve 0)";
    assert.deepEqual(logProblems("on", pass, "shadows", "hdr"), []);
    assert.match(logProblems("on", "nothing", "shadows", "hdr").join(), /no tone-mapping pass/);
    assert.match(logProblems("off", pass, "shadows", "hdr").join(), /in the off log/);
});

test("HDR: the exposure raised brightens; darkening or a white-out fails", () => {
    const off = flat(40, 40, 100);
    const on = flat(40, 40, 140);
    const d = shadowDifference(off, on, null, 8);
    assert.deepEqual(judgeSpot("a", d, null, HDR_BRIGHTER).problems, []);
    assert.equal(anyShadow([judgeSpot("a", d, null, HDR_BRIGHTER)], HDR_BRIGHTER), true);
    const white = flat(40, 40, 255);
    assert.match(
        judgeSpot("b", shadowDifference(off, white, null, 8), null, HDR_BRIGHTER).problems.join(),
        /pure white/,
    );
    const darker = flat(40, 40, 60);
    assert.match(judgeSpot("c", shadowDifference(off, darker, null, 8), null, HDR_BRIGHTER).problems.join(), /darker/);
    assert.equal(
        anyShadow([judgeSpot("d", shadowDifference(off, off, null, 8), null, HDR_BRIGHTER)], HDR_BRIGHTER),
        false,
    );
});

test("frame times: the Vulkan lines averaged", () => {
    const log =
        "vulkan: frame 600: 3 pipelines; 16.70 ms a frame (12.70 ms of it waiting for the device and the swapchain)\n" +
        "vulkan: frame 1200: 3 pipelines; 16.50 ms a frame (12.10 ms of it waiting for the device and the swapchain)\n";
    const times = frameTimes(log);
    assert.equal(times.lines, 2);
    assert.ok(Math.abs(times.frame - 16.6) < 1e-9);
    assert.ok(Math.abs(times.working - 4.2) < 1e-9);
});
