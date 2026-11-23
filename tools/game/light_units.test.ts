import assert from "node:assert/strict";
import { test } from "node:test";
import { fogBlocks, fogMedium, type Lamp, lampReachMetres, median } from "./light_units.ts";

test("the fog: Koschmieder's extinction for the game's distances", () => {
    // the time of day's night: SetFogDistance 8000 -> end 32 000 units (610 m): 3.912 / 610 m
    const night = fogMedium(8000);
    assert.ok(night);
    assert.ok(Math.abs(night.visibilityMetres - 24000 / 39.37) < 1e-6);
    assert.ok(Math.abs(night.extinctionPerMetre - 3.912 / (24000 / 39.37)) < 1e-9);
    assert.ok(Math.abs(night.overClearAir - 40000 / (24000 / 39.37)) < 1e-6);
    assert.equal(fogMedium(0), null);
});

test("a lamp's reach: where its light falls to a thousandth of a lux", () => {
    const lamp: Lamp = {
        position: [0, 0, 0],
        light: [1, 1, 1],
        core: 10,
        reach: 200,
        shape: 0,
        halfLength: 0,
        candela: 1000,
        lumens: 4000 * Math.PI,
        kelvin: 3000,
        kind: 2,
        source: 6,
        luminance: 20000,
        gameLight: 0,
    };
    assert.ok(Math.abs(lampReachMetres(lamp) - 1000) < 1e-6); // sqrt(1000 cd / 1e-3 lx)
    // a red sign: its brightest channel's intensity, not its luma's
    const red = { ...lamp, light: [1, 0, 0] as [number, number, number] };
    assert.ok(Math.abs(lampReachMetres(red) - Math.sqrt(1000 / 0.2126 / 1e-3)) < 1e-6);
});

test("the fog blocks of a decompiled script", () => {
    const text =
        "tod_x = {\n    fog_red=5 \n    fog_green=16 \n    fog_blue=22 \n    fog_alpha=63 \n    fog_dist=8000 \n} \n";
    const blocks = fogBlocks(text);
    assert.deepEqual(blocks.get("tod_x"), { red: 5, green: 16, blue: 22, alpha: 63, dist: 8000 });
    assert.equal(median([3, 1, 2]), 2);
});
