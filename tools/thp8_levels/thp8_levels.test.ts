// NOT ORIGINAL - tests of the THP8 converter's readers on synthetic data (no game data): the big-endian pak, the
// big-endian qb item format, the scene's strips and vertex layouts, the Xenos tiling, DXT, Havok primitives, the
// node transforms.

import assert from "node:assert/strict";
import { test } from "node:test";
import { checksum, Name, Struct } from "../level_port/qb.ts";
import * as thaw from "../thaw_levels/thaw.ts";
import * as dxt from "./dxt.ts";
import * as geom from "./geom.ts";
import { readPak, readToc, readTocList } from "./thp8.ts";
import { place, rotate, SCALE, thug2Terrain, toThug } from "./thp8_levels.ts";
import * as xenos from "./xenos.ts";

const be = (...words: number[]): Buffer => {
    const b = Buffer.alloc(4 * words.length);
    words.forEach((w, i) => {
        b.writeUInt32BE(w >>> 0, 4 * i);
    });
    return b;
};

test("pak: big-endian 32-byte entries, offsets from the entry, ends at 'last'", () => {
    const head = Buffer.concat([
        be(checksum(".geom"), 0x40, 4, 0x11, 0x22, 0x33, 0x44, 0),
        be(checksum("last"), 0, 4, 0, 0, 0, 0, 0),
    ]);
    const data = Buffer.concat([head, Buffer.from([1, 2, 3, 4])]);
    const pak = readPak(data, "test");
    assert.equal(pak.entries.length, 1);
    const e = pak.entries[0]!;
    assert.equal(e.extension, ".geom");
    assert.equal(e.offset, 0x40);
    assert.equal(e.size, 4);
    assert.equal(e.name, 0x33);
    assert.equal(e.own, 0x44);
    assert.deepEqual([...pak.data.subarray(e.offset, e.offset + e.size)], [1, 2, 3, 4]);
});

test("toc: TOC1, then {checksum, size, packed size, 0}; the list's lines", () => {
    const data = Buffer.concat([
        Buffer.from("TOC1"),
        be(0x8ff, 0x20, 0xe90020, 0, 0, 0, 0),
        be(0xb25a5, 0x41000, 0x6eb2, 0),
    ]);
    assert.deepEqual(readToc(data), [{ checksum: 0xb25a5, size: 0x41000, packed: 0x6eb2 }]);
    const list = readTocList(
        "Name: images\\a.img.xen 28338 b25a5\r\nName: zones\\z_houses.pak.xen 8736614 5c9f86bb\r\n",
    );
    assert.deepEqual(list.get("zones\\z_houses.pak.xen"), [8736614, 0x5c9f86bb]);
});

test("qb items, big-endian: an int, and a structure with a flag and an include (unnamed type 0x1A)", () => {
    // header: u32 0, u32 size, 20 fixed bytes; item {00 20 type 00, name, file, value, 0}
    const header = Buffer.concat([be(0, 0), Buffer.from("1c0802041004080c0c0802041402040c10100c00", "hex")]);
    const intItem = Buffer.concat([Buffer.from([0, 0x20, 1, 0]), be(checksum("Answer"), 0, 42, 0)]);
    // structure item at 0x1C + 20 + 20: its data {u32 0x100, u32 first entry} then entries {00 80|type 00 00, key,
    // value, next}
    const structAt = 0x1c + 40;
    const entries = structAt + 8;
    const structItem = Buffer.concat([Buffer.from([0, 0x20, 0x0a, 0]), be(checksum("Node"), 0, structAt, 0)]);
    const body = Buffer.concat([
        be(0x100, entries),
        Buffer.from([0, 0x8d, 0, 0]),
        be(checksum("Class"), checksum("Restart"), entries + 16),
        Buffer.from([0, 0x9a, 0, 0]),
        be(0, checksum("Some_compressed_node"), 0),
    ]);
    const data = Buffer.concat([header, intItem, structItem, body]);
    data.writeUInt32BE(data.length, 4);
    const q = new thaw.QbItems(data, "test", new Map(), ">");
    assert.equal(q.get("Answer"), 42);
    const node = q.get("Node");
    assert.ok(node instanceof Struct);
    assert.ok((node.get("Class") as Name).equals(new Name(checksum("Restart"))));
    const [key, value] = node.entries[1]!;
    assert.equal(key, null);
    assert.ok(value instanceof Name && value.value === checksum("Some_compressed_node"));
});

test("scene: strips split at 0x7FFF, alternate windings, degenerate triangles dropped", () => {
    const indices = Buffer.alloc(2 * 9);
    [0, 1, 2, 3, 0x7fff, 4, 5, 5, 6].forEach((v, i) => {
        indices.writeUInt16BE(v, 2 * i);
    });
    const mesh = { indexCount: 9, vertexCount: 7, indices } as unknown as geom.GeomMesh;
    assert.deepEqual(geom.triangles(mesh), [
        [0, 1, 2],
        [2, 1, 3],
    ]);
});

test("scene: uv fields from the layout word, half floats", () => {
    assert.deepEqual(geom.uvSlots(0x0a020878), [0, 4, 5]);
    assert.deepEqual(geom.uvSlots(0x2aaa0078), [0, 1, 2, 3, 4, 5, 6]);
    assert.equal(geom.half(0x3c00), 1);
    assert.equal(geom.half(0xc000), -2);
    assert.equal(geom.half(0x3800), 0.5);
});

test("xenos tiling: a 32 x 32-block surface maps one to one", () => {
    for (const bpb of [4, 8, 16]) {
        const seen = new Set<number>();
        for (let y = 0; y < 32; y++) for (let x = 0; x < 32; x++) seen.add(xenos.tiledOffset(x, y, 32, bpb));
        assert.equal(seen.size, 1024);
        assert.equal(Math.max(...seen), 1023);
    }
    assert.equal(xenos.tiledOffset(0, 0, 32, 8), 0);
});

test("xenos: an untiled DXT1 texture comes back from its tiled, 16-bit-swapped form", () => {
    // 8 x 8 texels = 2 x 2 blocks, in a 32 x 32-block tiled surface
    const blocks = [0, 1, 2, 3].map((i) => Buffer.from([i, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77]));
    const surface = Buffer.alloc(32 * 32 * 8);
    blocks.forEach((b, i) => {
        const at = xenos.tiledOffset(i % 2, i >> 1, 32, 8) * 8;
        b.copy(surface, at);
    });
    surface.swap16();
    const t: xenos.XenosTexture = {
        checksum: 1,
        width: 8,
        height: 8,
        format: xenos.FMT_DXT1,
        endian: 1,
        tiled: true,
        pitch: 128,
        data: surface,
    };
    assert.equal(xenos.tiledSize(t), surface.length);
    assert.deepEqual(xenos.baseLevel(t), Buffer.concat(blocks));
});

test("dxt: a flat colour and its alpha survive encode / decode; mips halve", () => {
    const w = 8;
    const h = 8;
    const src = Buffer.alloc(w * h * 4);
    for (let i = 0; i < w * h; i++) src.set([40, 120, 200, i % 2 ? 255 : 0], 4 * i);
    const five = dxt.decode(dxt.encode(src, w, h, 5), w, h, 5);
    for (let i = 0; i < w * h; i++) {
        assert.ok(Math.abs(five[4 * i]! - 40) <= 8 && Math.abs(five[4 * i + 2]! - 200) <= 8);
        assert.equal(five[4 * i + 3], i % 2 ? 255 : 0);
    }
    const one = dxt.encode(src, w, h, 1);
    assert.ok(dxt.hasAlpha1(one));
    assert.equal(dxt.halve(src, w, h).length, 4 * 4 * 4);
});

test("nodes: Angles turn about y as THP8's rails show (a fence piece's long side along its rail)", () => {
    // a fence piece long in x, turned by -0.76 rad: its rail runs with dx and dz of the same sign
    const r = rotate([1, 0, 0], [0, -0.76, 0]);
    assert.ok(r[0] > 0 && r[2] > 0);
    const p = place([1, 0, 0], { pos: [10, 0, 0], angles: [0, Math.PI / 2, 0], scale: [2, 1, 1] });
    assert.ok(Math.abs(p[0] - 10) < 1e-9 && Math.abs(p[2] + 2) < 1e-9);
    assert.deepEqual(toThug([1, 2, 3], [0.5, 0, 0]), [SCALE + 0.5, 2 * SCALE, 3 * SCALE]);
});

test("terrains: THP8's names as THUG2's terrain numbers", () => {
    const table = ["DEFAULT", "ASPHALT", "BRICK", "CONCSMOOTH", "GRASS_HF", "NOT_A_TERRAIN"];
    assert.equal(thug2Terrain(table, 0), 0); // DEFAULT
    assert.equal(thug2Terrain(table, 3), 1); // CONCSMOOTH
    assert.equal(thug2Terrain(table, 4), 20); // GRASS
    assert.equal(thug2Terrain(table, 5), 0);
    assert.equal(thug2Terrain(table, 99), 0);
});
