// NOT ORIGINAL - tests of tools/pipeline/game/ground_pieces.ts: the port of the DLL's generator (the same numbers as
// src/hook/selftests_editor_ux9.cpp's), the record as the DLL writes it, the strip, the vertex layouts.

import assert from "node:assert/strict";
import { test } from "node:test";
import {
    build,
    FLAG_DECK,
    FLAG_VERT,
    FLAG_WALL,
    type GroundSpec,
    layoutFor,
    MINUS_X,
    MINUS_Z,
    PLUS_X,
    quarterProfile,
    rampLength,
    readGroundRecord,
    strip,
    stripTriangles,
    writeVertices,
} from "./ground_pieces.ts";

function box(height: number): GroundSpec {
    return {
        name: "ed_ground01",
        lower: false,
        x0: 0,
        z0: 0,
        x1: 400,
        z1: 300,
        base: 10,
        height,
        ramps: ["-", "-", "-", "-"],
        length: 0,
        material: 0x12345678,
        uv: 256,
        colour: 0xff808080,
        terrain: 0,
        hides: [],
    };
}
const near = (a: number, b: number, e = 1e-3): boolean => Math.abs(a - b) <= e;
const count = (faces: { flags: number }[], flags: number): number => faces.filter((f) => f.flags === flags).length;

test("ground: a platform, a quarter pipe, kickers, a pit - as the DLL builds them", () => {
    const plain = build(box(200));
    assert.equal(plain.faces.length, 10);
    assert.equal(plain.vertices.length, 30);
    assert.equal(count(plain.faces, FLAG_DECK), 2);
    assert.equal(count(plain.faces, FLAG_WALL), 8);
    assert.ok(near(plain.min.y, 6) && near(plain.max.y, 210));
    const qp = box(200);
    qp.ramps[PLUS_X] = "q";
    const q = build(qp);
    assert.equal(q.faces.length, 40);
    assert.equal(count(q.faces, FLAG_VERT), 8);
    assert.ok(near(q.max.x, 600));
    const k = box(100);
    k.ramps = ["k", "k", "-", "-"];
    assert.ok(near(build(k).min.x, -200) && near(build(k).max.x, 600));
    k.length = 50;
    assert.equal(rampLength(k, PLUS_X), 50);
    assert.equal(rampLength(k, MINUS_Z), 0);
    const pit = box(150);
    pit.lower = true;
    const p = build(pit);
    assert.equal(p.faces.length, 10);
    assert.ok(near(p.min.y, -140) && near(p.max.y, 10));
    pit.ramps[MINUS_X] = pit.ramps[PLUS_X] = "q";
    pit.height = 300;
    assert.equal(rampLength(pit, MINUS_X), 190);
    assert.equal(build(pit).faces.length, 42);
    const full = quarterProfile(200, 200, 8);
    assert.equal(full.length, 9);
    assert.deepEqual(full[8], [0, 200]);
    assert.ok(near(full[0]![0], 200));
    assert.equal(quarterProfile(100, 250, 8).length, 10);
});

test("ground: faces wound out of the solid", () => {
    const m = build(box(200));
    for (const f of m.faces) {
        const [a, b, c] = [m.vertices[f.a]!.pos, m.vertices[f.b]!.pos, m.vertices[f.c]!.pos];
        const n = {
            x: (b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y),
            y: (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z),
            z: (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x),
        };
        const centre = {
            x: (a.x + b.x + c.x) / 3 - 200,
            y: (a.y + b.y + c.y) / 3 - 110,
            z: (a.z + b.z + c.z) / 3 - 150,
        };
        assert.ok(n.x * centre.x + n.y * centre.y + n.z * centre.z > 0);
    }
});

test("ground: the strip draws every face both ways", () => {
    const s = box(200);
    s.ramps[MINUS_Z] = "q";
    const m = build(s);
    const key = (t: number[]): string => {
        const i = t.indexOf(Math.min(...t));
        return [...t.slice(i), ...t.slice(0, i)].join(",");
    };
    const want = m.faces.flatMap((f) => [key([f.a, f.b, f.c]), key([f.a, f.c, f.b])]).sort();
    const got = stripTriangles(strip(m.faces, true)).map(key).sort();
    assert.deepEqual(got, want);
});

test("ground: vertex layouts and vertices", () => {
    assert.deepEqual(layoutFor(0x152, 36), { normal: 12, size: -1, diffuse: 24, specular: -1, tex: 28, texCount: 1 });
    assert.deepEqual(layoutFor(0x1c2, 28), { normal: -1, size: -1, diffuse: 12, specular: 16, tex: 20, texCount: 1 });
    assert.equal(layoutFor(0x152, 40), null);
    assert.equal(layoutFor(2, 12), null);
    assert.equal(layoutFor(0x146, 32), null);
    const m = build(box(100));
    const data = writeVertices(m, layoutFor(0x152, 36)!, 36, 0xff7f6e5d);
    assert.equal(data.length, 30 * 36);
    assert.equal(data.readUInt32LE(5 * 36 + 24), 0xff7f6e5d);
    assert.ok(near(data.readFloatLE(5 * 36), m.vertices[5]!.pos.x));
});

test("ground: the record as the DLL writes it", () => {
    const s = readGroundRecord(
        "ed_ground01 lower rect=0,0,400,300 base=10 height=120 ramps=q,-,k,- length=75.5 material=0x12345678 uv=128 " +
            "color=80706050 terrain=7 hides=0xAABBCCDD,0x01020304",
    )!;
    assert.equal(s.lower, true);
    assert.deepEqual(s.ramps, ["q", "-", "k", "-"]);
    assert.equal(s.length, 75.5);
    assert.equal(s.material, 0x12345678);
    assert.equal(s.colour, 0x80706050);
    assert.equal(s.terrain, 7);
    assert.deepEqual(s.hides, [0xaabbccdd, 0x01020304]);
    assert.equal(readGroundRecord("ed_ground01 raise rect=0,0,1,1 base=0 height=10"), null, "no material");
    assert.equal(
        readGroundRecord("ed_ground01 raise rect=0,0,100,100 base=0 base=1 height=10 material=0x00000001"),
        null,
    );
    assert.equal(
        readGroundRecord("ed_ground01 raise rect=0,0,100,100 base=0 height=10 ramps=q,-,x,- material=0x00000001"),
        null,
    );
    assert.equal(readGroundRecord("ed_ground01 sideways rect=0,0,100,100 base=0 height=10 material=0x00000001"), null);
    assert.ok(readGroundRecord("ed_ground01 raise rect=0,0,100,100 base=0 height=10 material=0x00000001 later=1"));
    // (the rectangle put in order and grown to the minimum, as Normalise)
    const n = readGroundRecord("ed_ground02 raise rect=400,0,0,10 base=0 height=5000 material=0x00000001")!;
    assert.deepEqual([n.x0, n.x1, n.z1, n.height], [0, 400, 50, 2000]);
});
