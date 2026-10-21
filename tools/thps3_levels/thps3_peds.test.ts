// NOT ORIGINAL - tests of THPS3's people (thps3_skin.ts, thps3_peds.ts): the quaternion maths, THUG2's skeleton and
// animation writers read back, the root turn, a model's rig posed as THPS3 poses it. The game-data one is skipped when
// THPS3 isn't installed.

import assert from "node:assert/strict";
import * as fs from "node:fs";
import * as path from "node:path";
import { test } from "node:test";
import * as peds from "./thps3_peds.ts";
import * as skin from "./thps3_skin.ts";

const close = (a: readonly number[], b: readonly number[], eps = 1e-4): void => {
    assert.equal(a.length, b.length);
    for (const [i, x] of a.entries()) assert.ok(Math.abs(x - b[i]!) < eps, `${a} vs ${b}`);
};

test("quaternions: a turn about x, the root turn's z-up to y-up, a frame matrix's quaternion", () => {
    // THPS3's root frame: rows (1,0,0), (0,0,-1), (0,1,0) - z goes up (y), y goes back (-z)
    const q = skin.matrixQuat([1, 0, 0, 0, 0, -1, 0, 1, 0]);
    close(q, peds.ROOT);
    close(skin.rotate(peds.ROOT, [0, 0, 1]), [0, 1, 0]);
    close(skin.rotate(peds.ROOT, [0, -1, 0]), [0, 0, 1]); // (THPS3's people face -y: THUG2's +z)
    close(skin.qmul(q, skin.conj(q)), skin.IDENTITY);
});

test("worldPoses: a child's world pose is its parent's times its own", () => {
    const turn: skin.Quat = [0, 0, Math.SQRT1_2, Math.SQRT1_2]; // 90 degrees about z
    const w = peds.worldPoses(
        [-1, 0],
        [
            { q: turn, t: [1, 0, 0] },
            { q: skin.IDENTITY, t: [2, 0, 0] },
        ],
    );
    close(w[1]!.t, [1, 2, 0]);
    close(w[1]!.q, turn);
});

test("writeSkeleton / readSkeleton: ids, parents, mirrors, the rest pose in THUG2's stored form", () => {
    const bones: skin.SkeletonBone[] = [
        { name: "a", parent: -1, mirror: -1, q: peds.ROOT, t: [0, 37.5, 0] },
        { name: "b", parent: 0, mirror: 2, q: skin.IDENTITY, t: [-4, 0, -3] },
        { name: "c", parent: 0, mirror: 1, q: skin.IDENTITY, t: [4, 0, -3] },
    ];
    const data = skin.writeSkeleton(bones);
    assert.equal(data.length, 12 + 44 * 3); // (THUG2's: 44 bytes a bone)
    assert.equal(data.readUInt32LE(0), 2);
    const back = skin.readSkeleton(data);
    assert.deepEqual(back.parents, [-1, 0, 0]);
    close(back.rest[0]!.q, peds.ROOT);
    close(back.rest[1]!.t, [-4, 0, -3]);
    // (stored conjugated: the file's x is minus ours)
    close([data.readFloatLE(12 + 36)], [-peds.ROOT[0]]);
    // the mirrors: each other's ids
    assert.equal(data.readUInt32LE(12 + 24 + 4), data.readUInt32LE(12 + 8));
});

test("writeAnim / readAnim: the keys back within their rounding (1/16384, 1/32 unit, 1/60 s), w's sign kept", () => {
    const q1 = skin.normalize([0.1, -0.2, 0.3, -0.9]);
    const tracks: skin.Track[] = [
        {
            rotations: [
                { time: 0, q: q1 },
                { time: 0.5, q: skin.IDENTITY },
            ],
            translations: [
                { time: 0, t: [1.25, -3.5, 40] },
                { time: 0.5, t: [0, 0, 0] },
            ],
        },
        { rotations: [{ time: 0, q: skin.IDENTITY }], translations: [{ time: 0, t: [0, 0, 5] }] },
    ];
    const data = skin.writeAnim(0.5, tracks);
    assert.equal(data.readUInt32LE(4), skin.ANIM_FLAGS);
    assert.equal(data.readUInt32LE(12), 2); // bones
    assert.equal(data.length % 4, 0);
    const back = skin.readAnim(data);
    assert.equal(back.duration, 0.5);
    const r = back.tracks[0]!.rotations[0]!.q;
    // (q and -q are one turn: compare up to the sign)
    const sign = Math.sign(r[3]) === Math.sign(q1[3]) ? 1 : -1;
    close(
        r.map((x) => x * sign),
        q1,
        2e-4,
    );
    close(back.tracks[0]!.translations[0]!.t, [1.25, -3.5, 40], 1 / 32);
    assert.equal(back.tracks[0]!.rotations[1]!.time, 0.5);
    assert.throws(() => skin.writeAnim(1, [{ rotations: [], translations: [{ time: 0, t: [2000, 0, 0] }] }]));
});

test("mirrors: the bones mirrored in x pair up, the middle ones have none", () => {
    const bone = (x: number, parent: number): skin.SknBone => ({
        frame: 0,
        parent,
        position: [x, 0, 0],
        rotation: skin.IDENTITY,
    });
    assert.deepEqual(peds.mirrors([bone(0, -1), bone(-4.25, 0), bone(4.25, 0)]), [-1, 2, 1]);
});

test("joinTriangles: only degenerate triangles at the joins, every triangle kept with its winding", () => {
    const tris = [
        [0, 1, 2],
        [3, 4, 5],
        [6, 7, 8],
        [9, 10, 11],
    ];
    const strip = peds.joinTriangles(tris);
    const drawn: string[] = [];
    for (let i = 0; i + 2 < strip.length; i++) {
        const [a, b, c] = [strip[i]!, strip[i + 1]!, strip[i + 2]!];
        if (a === b || b === c || a === c) continue;
        drawn.push((i % 2 ? [a, c, b] : [a, b, c]).join(","));
    }
    assert.deepEqual(
        drawn,
        tris.map((t) => t.join(",")),
    );
});

test("outwardStrip: triangles turned to face along their normals; inwardTriangles counts the others", () => {
    // a square in z = 0 as a strip of two triangles, both wound clockwise seen from +z, the normals +z
    const positions = [
        [0, 0, 0],
        [0, 1, 0],
        [1, 0, 0],
        [1, 1, 0],
    ];
    const normals = positions.map(() => [0, 0, 1]);
    const strip = [0, 1, 2, 3];
    assert.equal(peds.inwardTriangles(strip, positions, normals), 2);
    const fixed = peds.outwardStrip(strip, positions, normals);
    assert.equal(peds.inwardTriangles(fixed, positions, normals), 0);
    assert.equal(peds.inwardTriangles([0, 2, 1, 3], positions, normals), 0);
});

test("rig: a set's skeleton isn't named as a profile (THPS3_<model>: the set's base model has the set's name)", () => {
    for (const set of Object.keys(peds.SETS)) {
        const rig = peds.Thps3Peds.rig(set);
        assert.notEqual(rig.skeleton.toLowerCase(), peds.Thps3Peds.ours(peds.SETS[set]!.base).toLowerCase());
        assert.ok(rig.skeleton.startsWith(peds.PREFIX));
    }
});

const THPS3 = "C:\\Games\\THPS3";
const model = path.join(THPS3, "Data", "Models", "ped_Constr_A", "ped_Constr_A.SKN");
const walk = path.join(THPS3, "Data", "Anims", "pedestrian_a", "pedestrian_a_Walk1.ska");
test("THPS3's construction worker walking: posed through the written skeleton and animation where THPS3 has him", {
    skip: !fs.existsSync(model) && "THPS3 not installed",
}, () => {
    const s = skin.readSkn(fs.readFileSync(model), model);
    assert.equal(s.bones.length, 14);
    const anim = skin.readSka(fs.readFileSync(walk), 14, walk);
    for (const time of [0, 0.3, 0.6]) assert.ok(peds.poseError(s, s, 37.85, anim, time) < 0.1);
});
