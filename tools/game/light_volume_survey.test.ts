import assert from "node:assert/strict";
import { test } from "node:test";
import { isLightVolume, type VolumeLook } from "./light_volume_survey.ts";

// Training's fitting prism (FD86DE2E) as the rule reads it - the same cases as rt_scene_check's (rt_lights.h light_volume)
const prism: VolumeLook = {
    blend: 5,
    moving: false,
    brightest: 0x80,
    leastAlpha: 0,
    mostAlpha: 0x20,
    leastTexelLuma: 0.971,
};

test("a fitting's prism is a light volume", () => {
    assert.equal(isLightVolume(prism), true);
    assert.equal(isLightVolume({ ...prism, brightest: 0x80 }), true);
});

test("the near misses are not", () => {
    assert.equal(isLightVolume({ ...prism, leastTexelLuma: 0.001 }), false, "a shadow blob's dark texture");
    assert.equal(isLightVolume({ ...prism, brightest: 0x60 }), false, "under full brightness");
    assert.equal(isLightVolume({ ...prism, leastAlpha: 0x10 }), false, "never fades to nothing (glass, haze)");
    assert.equal(isLightVolume({ ...prism, mostAlpha: 0 }), false, "nothing drawn");
    assert.equal(isLightVolume({ ...prism, moving: true }), false, "water moves");
    assert.equal(isLightVolume({ ...prism, blend: 1 }), false, "an additive card is a light card's rule");
});
