import assert from "node:assert/strict";
import { test } from "node:test";
import type { Rgba } from "./classical.ts";
import { detectFacade, heightAt, repeats } from "./facades.ts";

type Rgb = [number, number, number];

// A texture filled with `wall` (a little noise on it: grain), then rectangles painted over it.
function painted(
    width: number,
    height: number,
    wall: Rgb,
    rects: [number, number, number, number, Rgb][],
    seed = 1,
): Rgba {
    const data = new Uint8Array(width * height * 4);
    let s = seed;
    const noise = () => {
        s = (s * 1103515245 + 12345) & 0x7fffffff;
        return ((s >> 8) % 9) - 4;
    };
    for (let i = 0; i < width * height; i++) {
        const n = noise();
        data[i * 4] = wall[0] + n;
        data[i * 4 + 1] = wall[1] + n;
        data[i * 4 + 2] = wall[2] + n;
        data[i * 4 + 3] = 255;
    }
    for (const [x0, y0, x1, y1, colour] of rects) {
        for (let y = y0; y < y1; y++) {
            for (let x = x0; x < x1; x++) {
                const i = ((y % height) * width + (x % width)) * 4;
                data[i] = colour[0];
                data[i + 1] = colour[1];
                data[i + 2] = colour[2];
            }
        }
    }
    return { width, height, data };
}

const WALL: Rgb = [176, 150, 128];
const GLASS: Rgb = [40, 52, 70];
const SILL: Rgb = [235, 230, 220];

// A grid of 4 x 4 windows (one a 32-texel cell), a sill under each.
function grid(): Rgba {
    const rects: [number, number, number, number, Rgb][] = [];
    for (let j = 0; j < 4; j++) {
        for (let i = 0; i < 4; i++) {
            rects.push([i * 32 + 9, j * 32 + 6, i * 32 + 23, j * 32 + 24, GLASS]);
            rects.push([i * 32 + 7, j * 32 + 24, i * 32 + 25, j * 32 + 27, SILL]);
        }
    }
    return painted(128, 128, WALL, rects);
}

test("facades: a grid's cell found", () => {
    const facade = detectFacade(grid()).facade;
    assert.ok(facade, "a facade");
    assert.equal(facade.repeatU, 4);
    assert.equal(facade.repeatV, 4);
    assert.equal(facade.windows, 16);
});

test("facades: windows recessed, sills out, the wall flat", () => {
    const facade = detectFacade(grid()).facade!;
    // (texel (16, 15) - a window's middle; (16, 25) - its sill; (2, 2) - the wall)
    const at = (x: number, y: number) => heightAt(facade, (x + 0.5) / 128, (y + 0.5) / 128);
    assert.ok(at(16, 15) < 0, "the window in");
    assert.ok(Math.abs(at(16, 15) + 0.35 * (14 / 128)) < 0.01, "in by 0.35 of its narrower side");
    assert.ok(at(16, 25) > 0, "the sill out");
    assert.equal(at(2, 2), 0, "the wall where it is");
    // (every cell alike)
    assert.equal(at(16 + 64, 15 + 96), at(16, 15));
    const kinds = new Set(facade.rects.map((r) => r.kind));
    assert.ok(kinds.has("window") && kinds.has("sill"));
});

test("facades: a whole building's front (one cell) - two windows", () => {
    const front = painted(64, 128, WALL, [
        [16, 20, 48, 52, GLASS],
        [16, 76, 48, 108, GLASS],
    ]);
    const found = detectFacade(front);
    assert.ok(found.facade, found.skip);
    assert.equal(found.facade.windows, 2);
});

test("facades: no structure, left flat", () => {
    // (plain grain)
    assert.ok(detectFacade(painted(64, 64, WALL, [])).skip);
    // (brick courses: light mortar lines across)
    const courses: [number, number, number, number, Rgb][] = [];
    for (let y = 0; y < 64; y += 8) courses.push([0, y, 64, y + 1, SILL]);
    assert.ok(detectFacade(painted(64, 64, [150, 60, 45], courses)).skip, "brick");
    // (one dark rectangle: a door, a sign)
    assert.ok(detectFacade(painted(64, 64, WALL, [[20, 10, 44, 60, GLASS]])).skip, "a door");
    // (a busy picture: random blocks of colour)
    const blocks: [number, number, number, number, Rgb][] = [];
    let s = 7;
    for (let i = 0; i < 40; i++) {
        s = (s * 1103515245 + 12345) & 0x7fffffff;
        const x = s % 60;
        const y = (s >> 8) % 60;
        blocks.push([
            x,
            y,
            x + 3 + (s % 9),
            y + 2 + ((s >> 4) % 11),
            [(s >> 3) & 255, (s >> 11) & 255, (s >> 17) & 255],
        ]);
    }
    assert.ok(detectFacade(painted(64, 64, WALL, blocks)).skip, "a busy picture");
});

test("facades: repeats - a period's multiples, not a smooth texture's", () => {
    const width = 64;
    const height = 16;
    const stripes = new Float32Array(width * height);
    for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) stripes[y * width + x] = x % 16 < 6 ? 1 : 0;
    assert.equal(repeats({ width, height, data: stripes }, true), 4);
    const ramp = new Float32Array(width * height);
    for (let y = 0; y < height; y++)
        for (let x = 0; x < width; x++) ramp[y * width + x] = Math.sin((x / width) * Math.PI * 2);
    assert.equal(repeats({ width, height, data: ramp }, true), 1);
});
