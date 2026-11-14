// NOT ORIGINAL - the detail-material tools' tests: the library file (the same sample as src/tests/format_check_surface.cpp
// "detail-material library": the same bytes), the kinds' decisions, the detail texels, the PNG reader.
import assert from "node:assert/strict";
import { test } from "node:test";
import { deflateSync } from "node:zlib";
import { contentHash } from "../pbr_maps/pbr_file.ts";
import { encodePng } from "../pbr_maps/png.ts";
import { ASSIGNMENT_SIZE, HEADER_SIZE, KIND_SIZE, readLibrary, writeLibrary } from "./detail_file.ts";
import { materials } from "./detail_materials.ts";
import { boxBlur, detailTexels } from "./detail_texels.ts";
import { decideKind, EVIDENCE_CLASS, EVIDENCE_TERRAIN, KIND, kindOfTerrain, type TerrainEvidence } from "./kinds.ts";
import type { FloatImage } from "./png_read.ts";
import { readPng } from "./png_read.ts";
import { metresPerUv, terrainName } from "./texture_terrain.ts";

// (format_check_surface.cpp's kDetailSampleHash: the C++ writer's bytes of the same sample)
const SAMPLE_HASH = 0xda1c9691dbb60c72n;

function sampleLibrary(): Buffer {
    const texels = new Uint8Array(4 * 4 * 4).map((_, i) => i);
    return writeLibrary(
        [
            {
                kind: KIND.brick,
                side: 4,
                tileMetres: 2,
                roughness: 0.85,
                roughnessSwing: 0.08,
                f0: 0.04,
                metalness: 0,
                roughnessWeight: 0.5,
                texels,
            },
        ],
        [
            {
                checksum: 0x1234abcd,
                contentHash: 0x0102030405060708n,
                kind: KIND.brick,
                evidence: EVIDENCE_TERRAIN,
                repeats: 2.5,
                meanLuma: 0.25,
            },
        ],
    );
}

test("the library: layout, read back, the C++ writer's bytes", () => {
    const file = sampleLibrary();
    assert.equal(file.length, HEADER_SIZE + KIND_SIZE + ASSIGNMENT_SIZE + 64);
    assert.equal(file.readUInt32LE(0), 0x4d443254);
    assert.equal(file.readUInt32LE(HEADER_SIZE + 28), HEADER_SIZE + KIND_SIZE + ASSIGNMENT_SIZE); // (the texels' offset)
    const back = readLibrary(file);
    assert.equal(back.kinds.length, 1);
    assert.equal(back.kinds[0]!.kind, KIND.brick);
    assert.equal(back.kinds[0]!.texels[63], 63);
    assert.equal(back.assignments[0]!.contentHash, 0x0102030405060708n);
    assert.equal(back.assignments[0]!.repeats, 2.5);
    assert.equal(contentHash(file), SAMPLE_HASH, `hash ${contentHash(file).toString(16)}`);
});

const evidence = (areas: [string, number][], level = 0, total = 100): TerrainEvidence => ({
    areas: new Map(areas),
    unmatched: 0,
    level,
    total,
});

test("kinds: terrain names, the decision's rules", () => {
    assert.equal(kindOfTerrain("CONCROUGH"), "concrete_rough");
    assert.equal(kindOfTerrain("SIDEWALK"), "concrete_smooth");
    assert.equal(kindOfTerrain("METALGRATING"), "metal");
    assert.equal(kindOfTerrain("WOODPIER"), "wood");
    assert.equal(kindOfTerrain("DEFAULT"), null);
    assert.equal(terrainName(1), "CONCSMOOTH");
    assert.equal(terrainName(37), "WETCONC");
    // the terrain decides past half the matched area
    assert.deepEqual(
        decideKind(
            "concrete",
            evidence([
                ["ASPHALT", 60],
                ["CONCSMOOTH", 40],
            ]),
        ),
        {
            kind: "asphalt",
            evidence: EVIDENCE_TERRAIN,
        },
    );
    // no majority: the class
    assert.deepEqual(
        decideKind(
            "stone",
            evidence([
                ["ASPHALT", 40],
                ["DEFAULT", 35],
                ["BRICK", 25],
            ]),
        ),
        {
            kind: "stone",
            evidence: EVIDENCE_CLASS,
        },
    );
    // painted, over DEFAULT faces, upright: plaster; lying level: not
    assert.equal(decideKind("default", evidence([["DEFAULT", 100]], 10)).kind, "plaster");
    assert.equal(decideKind("default", evidence([["DEFAULT", 100]], 80)).kind, "none");
    // glass and water never; no evidence: the class
    assert.equal(decideKind("glass", evidence([["CONCSMOOTH", 100]])).kind, "none");
    assert.equal(decideKind("foliage", undefined).kind, "grass");
});

test("metres per texture unit: the area-weighted median", () => {
    const density = new Map([
        [0, 10], // 1 m: 10
        [8, 30], // 2 m: 30
        [16, 5], // 4 m: 5
    ]);
    assert.equal(metresPerUv({ terrain: evidence([]), area: 45, density }), 2);
});

const flat = (value: number[], side = 32): FloatImage => {
    const channels = value.length;
    const data = new Float32Array(side * side * channels);
    for (let i = 0; i < side * side; i++) for (let c = 0; c < channels; c++) data[i * channels + c] = value[c]!;
    return { width: side, height: side, channels, data };
};

test("detail texels: a flat scan is neutral, a bump's slope is kept, the normal's y turned to +v", () => {
    const neutral = detailTexels(flat([0.5, 0.4, 0.3]), flat([0.5, 0.5, 1]), flat([0.6]), 16);
    for (let i = 0; i < 16 * 16; i++) {
        assert.equal(neutral[i * 4], 128);
        assert.equal(neutral[i * 4 + 1], 128);
        assert.equal(neutral[i * 4 + 2], 128);
        assert.equal(neutral[i * 4 + 3], 128);
    }
    // one texel brighter, its normal leaning up the picture (+y in OpenGL's maps): A above 128, G below (-v)
    const colour = flat([0.5, 0.5, 0.5]);
    const normal = flat([0.5, 0.5, 1]);
    const at = 8 * 32 + 8;
    colour.data.fill(0.8, at * 3, at * 3 + 3);
    normal.data[at * 3 + 1] = 0.9;
    const texels = detailTexels(colour, normal, flat([0.5]), 32);
    assert.ok(texels[at * 4 + 3]! > 160, `albedo ${texels[at * 4 + 3]}`);
    assert.ok(texels[at * 4 + 1]! < 100, `normal y ${texels[at * 4 + 1]}`);
    // the blur wraps round
    const ring = new Float32Array(16).fill(0);
    ring[0] = 16;
    assert.ok(Math.abs(boxBlur(ring, 4, 1)[15]! - 16 / 9) < 1e-6);
});

test("PNG: 8-bit read as encoded, 16-bit big-endian", () => {
    const rgba = new Uint8Array([255, 0, 128, 255, 10, 20, 30, 40]);
    const image = readPng(encodePng(2, 1, rgba));
    assert.equal(image.channels, 4);
    assert.ok(Math.abs(image.data[2]! - 128 / 255) < 1e-6);
    // a 1 x 1 16-bit grey PNG of 0x8000
    const chunk = (type: string, data: Buffer): Buffer => {
        const head = Buffer.alloc(8);
        head.writeUInt32BE(data.length, 0);
        head.write(type, 4, "ascii");
        return Buffer.concat([head, data, Buffer.alloc(4)]);
    };
    const ihdr = Buffer.alloc(13);
    ihdr.writeUInt32BE(1, 0);
    ihdr.writeUInt32BE(1, 4);
    ihdr[8] = 16;
    ihdr[9] = 0;
    const png = Buffer.concat([
        Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
        chunk("IHDR", ihdr),
        chunk("IDAT", deflateSync(Buffer.from([0, 0x80, 0x00]))),
        chunk("IEND", Buffer.alloc(0)),
    ]);
    const grey = readPng(png);
    assert.equal(grey.channels, 1);
    assert.ok(Math.abs(grey.data[0]! - 0x8000 / 65535) < 1e-6);
});

test("materials.json: one CC0 scan per kind, every file with an MD5", () => {
    const list = materials();
    const kinds = new Set(list.map((m) => m.kind));
    assert.equal(kinds.size, list.length, "a kind listed once");
    for (const m of list) {
        assert.match(m.licence, /^CC0/);
        for (const file of Object.values(m.files)) {
            assert.match(file.md5, /^[0-9a-f]{32}$/);
            assert.match(file.url, /^https:\/\//);
        }
    }
});
