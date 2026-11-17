// NOT ORIGINAL - round W2's rule (docs/design/vulkan.md "Round W2: grass and clutter lit by the one light"): the grass's
// blades and the clutter's items are lit by the light store, the sun's traced visibility and the one BRDF - never by the
// frame's colour under them. Their passes copy nothing of the frame (no copy or resolve of the colour target, no sampler
// of a copied-out colour) and their shaders hold no hand light model (the old root / tip shades, the ground's luminance
// over an albedo).

import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { test } from "node:test";

const ROOT = join(import.meta.dirname, "..");
const FILES = ["src/gpu_vulkan/vulkan_grass.cpp", "src/gpu_vulkan/vulkan_scatter.cpp"];

// what reading the frame's colour looks like in these passes
const SCREEN_READS: [RegExp, string][] = [
    [/vkCmdCopyImage\s*\(/, "a copy of an image (the frame's colour copied out)"],
    [/vkCmdResolveImage\s*\(/, "a resolve of the frame's colour"],
    [/\bu_source\b/, "a sampler of the frame's colour (u_source)"],
    [/\bcopy_colour\s*\(/, "copy_colour"],
    [/\bGROUND_ALBEDO\b|\bkClutterGroundAlbedo\b/, "the ground's luminance over an albedo"],
    [/\bROOT_SHADE\b|\bTIP_SHADE\b|\bkGrassRootShade\b|\bkGrassTipShade\b/, "a hand root / tip shade"],
];

test("one light: grass and clutter read nothing of the frame's colour", () => {
    const found: string[] = [];
    for (const file of FILES) {
        const text = readFileSync(join(ROOT, file), "utf8");
        for (const [pattern, what] of SCREEN_READS) {
            if (pattern.test(text)) found.push(`${file}: ${what}`);
        }
        if (!/sheet_radiance\(/.test(text) || !/window_probe\(/.test(text)) {
            found.push(`${file}: not lit through the light window and foliage_light.h's sheet_radiance`);
        }
    }
    assert.deepEqual(found, []);
});
