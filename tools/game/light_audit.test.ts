// NOT ORIGINAL - tests of tools/game/light_audit.ts's pure parts (no game data, no game run).

import assert from "node:assert/strict";
import { test } from "node:test";
import type { Image } from "thug2-level-converter-core/lib/frame_diff.ts";
import { censusBlocks, drawKind, heatCells, probesOf, regionRatios, restartsOf } from "./light_audit.ts";

const flat = (width: number, height: number, value: (x: number, y: number) => number): Image => {
    const rgb = new Uint8Array(width * height * 3);
    for (let y = 0; y < height; y++)
        for (let x = 0; x < width; x++) rgb.fill(value(x, y), (y * width + x) * 3, (y * width + x) * 3 + 3);
    return { width, height, rgb };
};

test("restartsOf reads the restart tester's lines in order", () => {
    const log =
        "x\nrestart tester: 1 / 12 P1: Restart (#C02EEE38) at (-2585, 124, -1275), types [Player1]\n" +
        "restart tester: 2 / 12 Tunnel at (2710.5, 60, 1043), types []\n";
    assert.deepEqual(restartsOf(log), [
        { name: "P1: Restart (#C02EEE38)", x: -2585, y: 124, z: -1275 },
        { name: "Tunnel", x: 2710.5, y: 60, z: 1043 },
    ]);
});

test("censusBlocks starts a block at each world line", () => {
    const log = [
        "automation: console rt census:",
        "rt census: world: relit relit; model ...",
        "rt census: world mesh relit: light stream | opaque: 3 meshes",
        "rt census: light paths a frame over 10 frames: ...",
        "rt census: world: relit off",
    ].join("\r\n");
    const blocks = censusBlocks(log);
    assert.equal(blocks.length, 2);
    assert.equal(blocks[0]!.length, 3);
    assert.equal(blocks[1]![0], "rt census: world: relit off");
});

test("probesOf takes each probe's place and its answer", () => {
    const log =
        "automation: console rt probe 0.15 0.25:\nrt probe 0.150 0.250: hit 10.0 at 1 2 3, mesh 7\n  material 0A0B0C0D\n" +
        "12:00 automation: console rt probe 0.4 0.55:\nrt probe 0.400 0.550: nothing\n";
    const probes = probesOf(log);
    assert.equal(probes.length, 2);
    assert.equal(probes[0]!.across, 0.15);
    assert.match(probes[0]!.text, /mesh 7/);
    assert.equal(probes[1]!.down, 0.55);
});

test("drawKind: the first pass decides; a later pass alone is an overlay", () => {
    assert.equal(drawKind([]), "unused");
    assert.equal(drawKind([{ pass: 0, blend: 0, cutoff: 0 }]), "solid");
    assert.equal(drawKind([{ pass: 0, blend: 0, cutoff: 128 }]), "cut-out");
    assert.equal(drawKind([{ pass: 0, blend: 5, cutoff: 0 }]), "blended");
    assert.equal(drawKind([{ pass: 0, blend: 1, cutoff: 0 }]), "additive");
    assert.equal(drawKind([{ pass: 1, blend: 5, cutoff: 0 }]), "later pass");
    assert.equal(
        drawKind([
            { pass: 0, blend: 0, cutoff: 0 },
            { pass: 2, blend: 5, cutoff: 0 },
        ]),
        "solid",
    );
    assert.equal(
        drawKind([
            { pass: 0, blend: 0, cutoff: 0 },
            { pass: 0, blend: 5, cutoff: 0 },
        ]),
        "mixed",
    );
});

test("regionRatios: the same picture is 1 everywhere; a darker shade shows in the shade", () => {
    const a = flat(64, 36, (x) => (x < 32 ? 200 : 40));
    for (const [, v] of Object.entries(regionRatios(a, a).ratio)) assert.ok(Math.abs(v - 1) < 1e-9);
    const b = flat(64, 36, (x) => (x < 32 ? 200 : 20)); // the shade's light counted twice
    const r = regionRatios(a, b);
    assert.ok(Math.abs(r.ratio.lit! - 1) < 1e-9);
    assert.ok(Math.abs(r.ratio.shade! - 0.5) < 1e-6);
    assert.ok(r.ratio.whole! < 1);
});

test("heatCells ranks the changed cells first", () => {
    const a = flat(160, 90, () => 100);
    const b = flat(160, 90, (x, y) => (x < 10 && y < 10 ? 50 : 100));
    const { cells, heat } = heatCells(a, b);
    assert.ok(cells[0]!.cx < 0.1 && cells[0]!.cy < 0.12);
    assert.ok(cells[0]!.ratio < 0.6);
    assert.equal(heat.width, 160);
});
