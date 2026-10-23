// NOT ORIGINAL - tests of ramp_collision.ts (steep faces continuing ramps made skatable, faces over ramps).

import assert from "node:assert/strict";
import { test } from "node:test";
import type { SourceColObject } from "./geometry.ts";
import { edgesJoin, fixRampObjects, type RampTri, rampFlags, rideRank } from "./ramp_collision.ts";
import * as thug2Col from "./thug2_col.ts";

type Vec = [number, number, number];

// A strip of quads (two triangles each) rising along -z from the floor at z = 0, x 0..width: each section a slope in
// degrees and a length along it. The triangles face up / towards +z (the approach). Returns the triangles and each
// section's first triangle index.
function quarterPipe(sections: readonly [number, number][], width = 100, flags: number[] = []): RampTri[] {
    const tris: RampTri[] = [];
    let y = 0;
    let z = 0;
    sections.forEach(([slope, len], i) => {
        const r = (slope * Math.PI) / 180;
        const y1 = y + Math.sin(r) * len;
        const z1 = z - Math.cos(r) * len;
        const a: Vec = [0, y, z];
        const b: Vec = [width, y, z];
        const c: Vec = [0, y1, z1];
        const d: Vec = [width, y1, z1];
        const f = flags[i] ?? 0;
        // (wound so the normal points up / towards +z)
        tris.push({ p: [a, b, d], flags: f }, { p: [a, d, c], flags: f });
        y = y1;
        z = z1;
    });
    return tris;
}

function up(t: RampTri): number {
    const u = [t.p[1][0] - t.p[0][0], t.p[1][1] - t.p[0][1], t.p[1][2] - t.p[0][2]];
    const v = [t.p[2][0] - t.p[0][0], t.p[2][1] - t.p[0][1], t.p[2][2] - t.p[0][2]];
    const n = [u[1]! * v[2]! - u[2]! * v[1]!, u[2]! * v[0]! - u[0]! * v[2]!, u[0]! * v[1]! - u[1]! * v[0]!];
    return n[1]! / Math.hypot(n[0]!, n[1]!, n[2]!);
}

test("the test quarter pipe faces up", () => {
    for (const t of quarterPipe([
        [0, 100],
        [30, 50],
        [70, 50],
    ]))
        assert.ok(up(t) > 0);
});

test("a steep unflagged face at the top of a transition becomes vert, and the 45-degree face under it", () => {
    // Skate Heaven's: 26.9 -> 45.7 -> 67.6 degrees
    const tris = quarterPipe([
        [0, 100],
        [26.9, 50],
        [45.7, 50],
        [67.6, 60],
    ]);
    assert.equal(rideRank(tris[6]!.flags, up(tris[6]!)), 1); // (a wall in THUG2 as it is)
    const r = rampFlags(tris);
    const V = thug2Col.FACE_VERT;
    assert.deepEqual(r.promoted.sort(), [6, 7]);
    assert.deepEqual(r.band.sort(), [4, 5]);
    // (the floor and the 26.9-degree face untouched)
    assert.deepEqual(r.flags, [0, 0, 0, 0, V, V, V, V]);
});

test("a wallridable steep face continuing a ramp loses the wall flag; chains go on upwards", () => {
    // Chopper Drop's: 14.5 -> 33.8 -> 57.8 -> 73.0 (flagged wallridable), and a 80 degree face above it
    const W = thug2Col.FACE_WALL_RIDABLE;
    const tris = quarterPipe(
        [
            [0, 100],
            [14.5, 40],
            [33.8, 40],
            [57.8, 40],
            [73, 40],
            [80, 40],
        ],
        100,
        [0, 0, 0, 0, W, W],
    );
    const r = rampFlags(tris);
    assert.deepEqual(
        r.promoted.sort((a, b) => a - b),
        [8, 9, 10, 11],
    );
    for (const i of [8, 9, 10, 11]) assert.equal(r.flags[i], thug2Col.FACE_VERT);
    // the vert band down to the 57.8-degree face; 33.8 is under kVertBandSlope
    assert.deepEqual(
        r.band.sort((a, b) => a - b),
        [6, 7],
    );
    assert.equal(r.flags[4], 0);
});

test("a wall on a floor, a bend over the limit, a face past 84 degrees stay; vert under 40 degrees is taken off", () => {
    const wall = rampFlags(
        quarterPipe([
            [0, 100],
            [70, 100],
        ]),
    );
    assert.deepEqual(wall.promoted, []);
    const bend = rampFlags(
        quarterPipe([
            [0, 100],
            [20, 50],
            [70, 50],
        ]),
    );
    assert.deepEqual(bend.promoted, []); // (50 degrees)
    const steep = rampFlags(
        quarterPipe([
            [0, 100],
            [30, 50],
            [60, 50],
            [87, 50],
        ]),
    );
    assert.deepEqual(steep.promoted, []);
    const V = thug2Col.FACE_VERT;
    const vert = rampFlags(
        quarterPipe(
            [
                [0, 100],
                [22.4, 50],
                [67.6, 50],
            ],
            100,
            [0, V, V],
        ),
    );
    assert.deepEqual(vert.promoted, []);
    // (NY's: the 22.4-degree vert transition no longer vert - an ollie there was vert air)
    assert.deepEqual(vert.lowVert.sort(), [2, 3]);
    assert.deepEqual(vert.flags, [0, 0, 0, 0, V, V]);
    // a trigger plane's (canRamp false) keeps its flags
    assert.deepEqual(
        rampFlags(
            quarterPipe(
                [
                    [0, 100],
                    [22.4, 50],
                ],
                100,
                [0, V],
            ),
            () => false,
        ).lowVert,
        [],
    );
});

test("a convex join (a steep face hanging off a deck's edge) is no ramp", () => {
    // deck at y 100, z 0..-100; a 70 degree face going down from its front edge towards +z
    const deck: RampTri[] = [
        {
            p: [
                [0, 100, 0],
                [100, 100, 0],
                [100, 100, -100],
            ],
            flags: 0,
        },
    ];
    const r70 = (70 * Math.PI) / 180;
    const down: RampTri = {
        p: [
            [0, 100, 0],
            [0, 100 - Math.sin(r70) * 50, Math.cos(r70) * 50],
            [100, 100, 0],
        ],
        flags: 0,
    };
    assert.ok(up(deck[0]!) > 0.99 && up(down) > 0.1 && up(down) < 0.42);
    assert.deepEqual(rampFlags([...deck, down]).promoted, []);
});

test("joins across objects and T-junctions count; canRamp keeps a face a wall", () => {
    // the transition one long quad, the steep face above it two narrower quads (their edges meet the long one's
    // edge in the middle)
    const lower = quarterPipe(
        [
            [0, 100],
            [45, 50],
        ],
        200,
    );
    const top = lower[3]!.p[1]; // [0, y1, z1] of the 45 section's far edge
    const r = (68 * Math.PI) / 180;
    const steep = (x0: number, x1: number): RampTri[] => {
        const a: Vec = [x0, top[1], top[2]];
        const b: Vec = [x1, top[1], top[2]];
        const c: Vec = [x0, top[1] + Math.sin(r) * 50, top[2] - Math.cos(r) * 50];
        const d: Vec = [x1, top[1] + Math.sin(r) * 50, top[2] - Math.cos(r) * 50];
        return [
            { p: [a, b, d], flags: 0 },
            { p: [a, d, c], flags: 0 },
        ];
    };
    const tris = [...lower, ...steep(0, 100), ...steep(100, 200)];
    assert.deepEqual(
        rampFlags(tris).promoted.sort((a, b) => a - b),
        [4, 5, 6, 7],
    );
    assert.deepEqual(
        rampFlags(tris, (i) => i < 6).promoted.sort((a, b) => a - b),
        [4, 5],
    );
    assert.ok(edgesJoin([0, 0, 0], [200, 0, 0], [100, 0.2, 0], [150, 0, 0]));
    assert.ok(!edgesJoin([0, 0, 0], [200, 0, 0], [100, 2, 0], [150, 0, 0]));
    assert.ok(!edgesJoin([0, 0, 0], [200, 0, 0], [200.5, 0, 0], [300, 0, 0]));
});

test("a face lying on a vert face takes its flags; one on a floor doesn't change", () => {
    const V = thug2Col.FACE_VERT;
    // NY's: a vert face at 67.6 degrees and a decal model 0.4 inch in front of it, unflagged
    const vert = quarterPipe(
        [
            [0, 100],
            [22.4, 50],
            [67.6, 100],
        ],
        200,
        [0, V, V],
    );
    const decal = quarterPipe(
        [
            [0, 100],
            [22.4, 50],
            [67.6, 100],
        ],
        100,
    )
        .slice(4)
        .map((t) => ({
            flags: 0,
            p: t.p.map((q) => [q[0] + 50, q[1] + 0.15, q[2] + 0.37]) as [Vec, Vec, Vec],
        }));
    const r = rampFlags([...vert, ...decal]);
    assert.deepEqual(r.overlays.sort(), [6, 7]);
    assert.equal(r.flags[6], V);
    assert.equal(r.flags[7], V);
    // a decal on the floor: nothing more rideable under it
    const floor = quarterPipe([[0, 100]], 200);
    const spot: RampTri = {
        p: [
            [50, 0.2, -10],
            [60, 0.2, -10],
            [60, 0.2, -20],
        ],
        flags: 0,
    };
    assert.ok(up(spot) > 0.99);
    assert.deepEqual(rampFlags([...floor, spot]).overlays, []);
});

test("fixRampObjects changes the objects' faces in place", () => {
    const tris = quarterPipe([
        [0, 100],
        [30, 50],
        [55, 50],
        [72, 50],
    ]);
    const verts: number[][] = [];
    const faces: thug2Col.Face[] = [];
    for (const t of tris) {
        const base = verts.length;
        verts.push(...t.p);
        faces.push([t.flags, 0, base, base + 1, base + 2]);
    }
    const object: SourceColObject = { checksum: 1, flags: 0, bbox: [], verts, intensities: [], faces };
    assert.deepEqual(fixRampObjects([object]), { promoted: 2, overlays: 0, lowVert: 0, band: 2 });
    // (72 degrees vert, the 55-degree face under it too; 30 not)
    assert.deepEqual(
        object.faces.map((f) => f[0]),
        [0, 0, 0, 0, thug2Col.FACE_VERT, thug2Col.FACE_VERT, thug2Col.FACE_VERT, thug2Col.FACE_VERT],
    );
    assert.deepEqual(
        fixRampObjects([object], () => false),
        { promoted: 0, overlays: 0, lowVert: 0, band: 0 },
    );
});
