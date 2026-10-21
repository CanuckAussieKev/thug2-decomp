// NOT ORIGINAL - tests of the THPS2 readers (pkr.ts, thps2.ts) and converter (thps2_levels.ts) on synthetic data: no
// THPS2 file is needed (nothing of the game is in the repo).

import assert from "node:assert/strict";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { test } from "node:test";
import * as thug2Col from "../level_port/thug2_col.ts";
import { Pkr, readPkrDirectory, writePkr } from "./pkr.ts";
import * as thps2 from "./thps2.ts";
import * as conv from "./thps2_levels.ts";

// --- synthetic files -----------------------------------------------------------------------------------------------

const u16 = (...v: number[]): Buffer => Buffer.from(new Uint16Array(v).buffer);
const i16 = (...v: number[]): Buffer => Buffer.from(new Int16Array(v).buffer);
const u32 = (...v: number[]): Buffer => Buffer.from(new Uint32Array(v.map((x) => x >>> 0)).buffer);
const i32 = (...v: number[]): Buffer => Buffer.from(new Int32Array(v).buffer);

const MAGENTA = thps2.PS1_MAGENTA;

// A world of one object (at 100, -200, 300) whose model is a textured gouraud quad on the floor (y = 0, facing up:
// THPS2's y is down) and an invisible flat-shaded trigger triangle standing on it; textures named 0x11111111 (a 4 x 2
// 16-colour texture: colour 1 red, 2 magenta, 0 transparent) in a library at the end of the same file.
function worldPsx(): Buffer {
    // model
    const vertices = [
        [0, 0, 0],
        [64, 0, 0],
        [0, 0, 64],
        [64, 0, 64],
        [0, -64, 0],
    ];
    // THPS2's plane normal is -(v1 - v0) x (v2 - v0): (64,0,0) x (0,0,64) = (0,-4096,0) -> minus: (0, 4096, 0)?
    // The floor faces up: up is -y in THPS2, so its normal is (0, -4096, 0) and its corners turn the other way.
    const quad = Buffer.concat([
        u16(thps2.FACE_TEXTURED | thps2.FACE_GOURAUD | 0x1000, 28),
        Buffer.from([0, 2, 1, 3]), // vertex[4]: 0 1 / 2 3 = (0,0) (0,64) / (64,0) (64,64)
        Buffer.from([0, 1, 1, 0]), // shade: palette indices
        u16(0, thps2.SURFACE_VERT | (3 << 9)), // plane 0, vert, terrain 3
        u32(0), // texture index 0 into the names
        Buffer.from([0, 0, 0, 1, 3, 0, 3, 1]), // uvs
    ]);
    const tri = Buffer.concat([
        u16(thps2.FACE_TRIANGLE | thps2.FACE_INVISIBLE, 16),
        Buffer.from([0, 1, 4, 0]),
        Buffer.from([10, 20, 30, 0]),
        u16(1, thps2.SURFACE_TRIGGER),
    ]);
    const model = Buffer.concat([
        u16(0x8, vertices.length, 2, 2),
        u32(64 * 4096),
        i16(64, 0, 0, -64, 64, 0),
        u32(0),
        ...vertices.map((v) => i16(v[0]!, v[1]!, v[2]!, 0)),
        i16(0, -4096, 0, 0), // plane 0: the floor, up (THPS2's -y)
        i16(0, 0, 4096, 0), // plane 1
        quad,
        tri,
    ]);
    const objectsAt = 12;
    const object = Buffer.concat([
        u32(0),
        i32(100 * 4096, -200 * 4096, 300 * 4096),
        u32(0),
        u16(0, 0),
        i16(0, 0),
        u32(0, 0),
    ]);
    const modelTableAt = objectsAt + 36;
    const modelAt = modelTableAt + 8;
    const chunksAt = modelAt + model.length;
    const rgbs = Buffer.from([128, 128, 128, 0, 64, 32, 16, 0]);
    const chunks = Buffer.concat([u32(thps2.TAG_RGBS, rgbs.length), rgbs, u32(0xffffffff)]);
    const names = Buffer.concat([u32(0xabcdef01), u32(1, 0x11111111)]);
    // texture library: no 16-colour... one 16-colour palette (key 7), no 256-colour ones, one texture
    const palette = new Array<number>(16).fill(0x7fff);
    palette[0] = 0;
    palette[1] = 0x001f; // red
    palette[2] = MAGENTA;
    const texels = Buffer.from([0x21, 0x01, 0x10, 0x11]); // row 0: 1 2 1 0, row 1: 0 1 1 1 (4-bit, low first)
    const library = Buffer.concat([
        u32(1, 7),
        u16(...palette),
        u32(0),
        u32(1),
        u32(0),
        u32(0, 16, 7, 0),
        u16(4, 2),
        texels,
    ]);
    return Buffer.concat([
        u32(thps2.PSX_VERSION_THPS2, chunksAt, 1),
        object,
        u32(1, modelAt),
        model,
        chunks,
        names,
        library,
    ]);
}

// Nodes: a restart "Start" (links 1, 2) and two rail points, the first linked to the second.
function trgFile(): Buffer {
    const nodesData: Buffer[] = [];
    const start = Buffer.concat([
        u16(thps2.NODE_RESTART, 2, 1, 2),
        Buffer.alloc(0),
        i32(-900, -2250, 450),
        i16(0, 1024, 0),
        Buffer.from("Start\0", "latin1"),
    ]);
    nodesData.push(start);
    nodesData.push(Buffer.concat([u16(thps2.NODE_RAIL_POINT, 1, 2), i32(0, -90, 0), u16(1)]));
    nodesData.push(Buffer.concat([u16(thps2.NODE_RAIL_POINT, 0), i32(225, -90, 0), u16(1)]));
    nodesData.push(u16(thps2.NODE_END));
    // (each node's 32-bit data aligned to 4 in the file: nodes placed at offsets that keep these aligned)
    const header = 12 + nodesData.length * 4;
    const offsets: number[] = [];
    const parts: Buffer[] = [];
    let at = header;
    for (const [k, n] of nodesData.entries()) {
        // the restart's data starts 8 bytes in, the 1-link rail's 6, the 0-link rail's 4
        const dataAt = k === 0 ? 8 : k === 1 ? 6 : 4;
        while ((at + dataAt) % 4) {
            parts.push(Buffer.alloc(1));
            at++;
        }
        offsets.push(at);
        parts.push(n);
        at += n.length;
    }
    return Buffer.concat([Buffer.from("_TRG"), u32(2, nodesData.length), u32(...offsets), ...parts]);
}

// --- pkr -----------------------------------------------------------------------------------------------------------

test("pkr: the directory and the files of an archive round trip", () => {
    const files = [
        { folder: "data/", name: "SKTEST.PSX", data: Buffer.from("world") },
        { folder: "newtex/Test/", name: "11111111.BMP", data: Buffer.from("bitmap!") },
        { folder: "data/", name: "SKTEST_T.TRG", data: Buffer.from("nodes") },
    ];
    const archive = writePkr(files);
    const dir = readPkrDirectory(archive);
    assert.deepEqual(
        dir.map((e) => e.path),
        ["data/sktest.psx", "data/sktest_t.trg", "newtex/test/11111111.bmp"],
    );
    const top = fs.mkdtempSync(path.join(os.tmpdir(), "pkr-"));
    try {
        const file = path.join(top, "ALL.PKR");
        fs.writeFileSync(file, archive);
        const pkr = new Pkr(file);
        assert.equal(pkr.read("DATA\\SKTEST_T.TRG").toString(), "nodes");
        assert.equal(pkr.list("newtex/test/").length, 1);
        assert.ok(!pkr.has("data/nothing.psx"));
        pkr.close();
    } finally {
        fs.rmSync(top, { recursive: true });
    }
    assert.throws(() => readPkrDirectory(Buffer.from("PKR1............")), thps2.FormatError);
});

// --- psx -----------------------------------------------------------------------------------------------------------

test("psx: objects, models, faces, palette and texture library", () => {
    const psx = thps2.readPsx(worldPsx(), "test");
    assert.equal(psx.objects.length, 1);
    assert.deepEqual(psx.objects[0]!.position, [100, -200, 300]);
    const model = psx.models[0]!;
    assert.equal(model.vertices.length, 5);
    assert.equal(model.faces.length, 2);
    const [quad, tri] = model.faces as [thps2.PsxFace, thps2.PsxFace];
    assert.deepEqual(quad.vertices, [0, 2, 1, 3]);
    assert.equal(quad.texture, 0);
    assert.deepEqual(quad.uvs, [0, 0, 0, 1, 3, 0, 3, 1]);
    assert.deepEqual(tri.vertices, [0, 1, 4]);
    assert.deepEqual(psx.rgbs, [
        [128, 128, 128],
        [64, 32, 16],
    ]);
    assert.deepEqual(psx.modelNames, [0xabcdef01]);
    assert.deepEqual(psx.textureNames, [0x11111111]);
    const t = psx.textures[0]!;
    assert.equal(t.width, 4);
    assert.equal(t.height, 2);
    const alpha = [...Array(8).keys()].map((i) => t.rgba[i * 4 + 3]);
    // row 0: red, magenta (transparent), red, 0 (transparent); row 1: 0, red, red, red
    assert.deepEqual(alpha, [255, 0, 255, 0, 0, 255, 255, 255]);
    assert.deepEqual([...t.rgba.subarray(0, 4)], [255, 0, 0, 255]);
});

test("ps1Color: 0x0000 and magenta are transparent, the semi-transparency bit is ignored", () => {
    assert.deepEqual(thps2.ps1Color(0), [0, 0, 0, 0]);
    assert.deepEqual(thps2.ps1Color(MAGENTA), [0, 0, 0, 0]);
    assert.deepEqual(thps2.ps1Color(MAGENTA | 0x8000), [0, 0, 0, 0]);
    assert.deepEqual(thps2.ps1Color(0x8000), [0, 0, 0, 255]);
    assert.deepEqual(thps2.ps1Color(0x7fff), [255, 255, 255, 255]);
});

test("psx: a wrong version or a cut file is a FormatError", () => {
    const data = worldPsx();
    const bad = Buffer.from(data);
    bad.writeUInt32LE(0x00020003, 0);
    assert.throws(() => thps2.readPsx(bad), thps2.FormatError);
    assert.throws(() => thps2.readPsx(data.subarray(0, 80)), thps2.FormatError);
});

// --- trg -----------------------------------------------------------------------------------------------------------

test("trg: restarts and rail points, their data aligned to 4 in the file", () => {
    const nodes = thps2.readTrg(trgFile(), "test");
    assert.deepEqual(
        nodes.map((n) => n.type),
        [thps2.NODE_RESTART, thps2.NODE_RAIL_POINT, thps2.NODE_RAIL_POINT, thps2.NODE_END],
    );
    const r = thps2.restartOf(nodes[0]!);
    assert.deepEqual(r, { position: [-900, -2250, 450], angles: [0, 1024, 0], name: "Start" });
    assert.deepEqual(nodes[0]!.links, [1, 2]);
    assert.deepEqual(thps2.railPointOf(nodes[1]!), { position: [0, -90, 0], flags: 1 });
    assert.deepEqual(nodes[1]!.links, [2]);
    assert.deepEqual(nodes[2]!.links, []);
});

// --- the converter -------------------------------------------------------------------------------------------------

test("space: THPS2's (x, y down, z) as THUG2's geometry and node positions", () => {
    assert.deepEqual(conv.toThug([225, -450, 675]), [100, 200, -300]);
    assert.deepEqual(conv.toNodePosition([225, -450, 675]), [100, 200, 300]);
    assert.deepEqual(conv.toThugNormal([0, -4096, 0]), [0, 1, 0]);
});

test("faces: quads split keeping the turning order; outward normals as THUG2's collision takes them", () => {
    const psx = thps2.readPsx(worldPsx(), "test");
    const model = psx.models[0]!;
    const quad = model.faces[0]!;
    assert.deepEqual(conv.faceTriangles(quad), [
        [0, 2, 1],
        [1, 2, 3],
    ]);
    // each triangle's (b - a) x (c - a) in THUG2's space points along the face's plane normal there
    const normal = conv.toThugNormal(model.normals[quad.plane]!);
    for (const tri of conv.faceTriangles(quad)) {
        const [a, b, c] = tri.map((k) => conv.toThug(model.vertices[quad.vertices[k]!]!));
        const u = [0, 1, 2].map((k) => b![k]! - a![k]!);
        const v = [0, 1, 2].map((k) => c![k]! - a![k]!);
        const cross = [u[1]! * v[2]! - u[2]! * v[1]!, u[2]! * v[0]! - u[0]! * v[2]!, u[0]! * v[1]! - u[1]! * v[0]!];
        assert.ok(cross[0]! * normal[0] + cross[1]! * normal[1] + cross[2]! * normal[2] > 0);
    }
});

test("faces: uvs reach the far side of the last texel; colours from the palette or the face", () => {
    const psx = thps2.readPsx(worldPsx(), "test");
    const [quad, tri] = psx.models[0]!.faces as [thps2.PsxFace, thps2.PsxFace];
    assert.deepEqual(conv.faceUvs(quad, 4, 2), [
        [0, 0],
        [0, 1],
        [1, 0],
        [1, 1],
    ]);
    assert.deepEqual(conv.faceColors(quad, psx.rgbs), [
        [128, 128, 128],
        [64, 32, 16],
        [64, 32, 16],
        [128, 128, 128],
    ]);
    assert.deepEqual(conv.faceColors(tri, psx.rgbs), [
        [10, 20, 30],
        [10, 20, 30],
        [10, 20, 30],
    ]);
});

test("collision flags: vert, wallride on steep faces, triggers and vert walls left out", () => {
    const face = (flags: number, surface: number): thps2.PsxFace => ({
        flags,
        vertices: [0, 1, 2],
        shade: [0, 0, 0, 0],
        plane: 0,
        surface,
        texture: -1,
        uvs: [],
        extra: Buffer.alloc(0),
    });
    assert.equal(conv.collisionFlags(face(0, 0), 1), 0);
    assert.equal(conv.collisionFlags(face(0, thps2.SURFACE_VERT), 0.2), thug2Col.FACE_VERT);
    assert.equal(conv.collisionFlags(face(0, 0), 0.1), thug2Col.FACE_WALL_RIDABLE);
    assert.equal(conv.collisionFlags(face(thps2.FACE_INVISIBLE, thps2.SURFACE_TRIGGER), 0), null);
    assert.equal(conv.collisionFlags(face(thps2.FACE_INVISIBLE, thps2.SURFACE_VERT_WALL), 0), null);
    assert.equal(
        conv.collisionFlags(face(thps2.FACE_INVISIBLE, 0), 0),
        thug2Col.FACE_CAMERA_PASSES | thug2Col.FACE_WALL_RIDABLE,
    );
    // (a visible face with the trigger bit still collides)
    assert.equal(conv.collisionFlags(face(0, thps2.SURFACE_TRIGGER), 1), 0);
});

test("convertWorld: a sector and a collision object per object; invisible faces collide but aren't drawn", () => {
    const psx = thps2.readPsx(worldPsx(), "test");
    const textures: conv.LevelTextures = {
        byName: new Map(psx.textures.map((t) => [psx.textureNames[t.index]!, t])),
        bmps: new Map(),
    };
    const done = conv.convertWorld(psx, "thps2_test", textures, false);
    assert.equal(done.scene.sectors.length, 1);
    assert.equal(done.scene.materials.length, 1);
    assert.equal(done.textures.textures.length, 1);
    assert.equal(done.textures.textures[0]!.checksum, 0x11111111);
    const mesh = done.scene.sectors[0]!.meshes[0]!;
    assert.equal(mesh.vertexCount, 4);
    assert.equal(done.col.objects.length, 1);
    const o = done.col.objects[0]!;
    // the quad's two triangles (vert); the trigger triangle left out
    assert.equal(o.faces.length, 2);
    for (const f of o.faces) assert.equal(f[0], thug2Col.FACE_VERT);
    assert.deepEqual(done.counts, { objects: 1, faces: 2, hidden: 1, noCollision: 1, bmps: 0 });
    // placed: the object's position, THUG2's space
    // (z: the model's 0..64 at 300, turned: -364 .. -300)
    assert.deepEqual(
        o.bbox.slice(0, 3),
        [100 / 2.25, 200 / 2.25, -364 / 2.25].map((v) => Math.floor(v * 16) / 16),
    );
});

test("textures: the BMP's colours with the PlayStation texture's alpha, transparent texels bled", () => {
    const psx = thps2.readPsx(worldPsx(), "test");
    const t = psx.textures[0]!;
    // an 8 x 4 BMP (twice the size), all blue
    const width = 8;
    const height = 4;
    const row = (width * 3 + 3) & ~3;
    const bmpData = Buffer.alloc(54 + row * height);
    bmpData.write("BM", 0, "latin1");
    bmpData.writeUInt32LE(54, 10);
    bmpData.writeUInt32LE(40, 14);
    bmpData.writeInt32LE(width, 18);
    bmpData.writeInt32LE(height, 22);
    bmpData.writeUInt16LE(1, 26);
    bmpData.writeUInt16LE(24, 28);
    for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) bmpData[54 + y * row + x * 3] = 200; // B
    const bmp = conv.readBmp(bmpData, "test.bmp");
    assert.equal(bmp.width, 8);
    const [w, h, level] = conv.textureLevel(t, bmp);
    assert.equal(w, 8);
    assert.equal(h, 4);
    // texel (2, 0) of the BMP is the PlayStation's (1, 0): magenta -> alpha 0
    assert.equal(level[(0 * 8 + 2) * 4 + 3], 0);
    assert.equal(level[(0 * 8 + 0) * 4 + 3], 255);
    assert.equal(level[(0 * 8 + 0) * 4], 200); // B
    // without the BMP: the PlayStation texture, transparent texels given a neighbour's colour (red)
    const [, , own] = conv.textureLevel(t, null);
    assert.equal(own[1 * 4 + 3], 0);
    assert.equal(own[1 * 4 + 2], 255); // R from the red neighbours
    assert.ok(conv.hasTransparency(t));
    assert.throws(() => conv.readBmp(Buffer.from("XX"), "bad"), thps2.FormatError);
});

test("restarts: the start first as Player1, 2P / horse / generic by name, headings in radians", () => {
    const r = (name: string): thps2.Restart => ({ position: [0, 0, 0], angles: [0, 0, 0], name });
    assert.deepEqual(conv.restartKinds([r("Waterfall"), r("Re_Start_2P"), r("Re_2P"), r("Ho_Bowl"), r("Half Pipe")]), [
        "Player1",
        "Player2",
        "MultiPlayer",
        "Horse",
        "Generic",
    ]);
    assert.deepEqual(conv.restartKinds([r("Re_The_Pit"), r("Re_Start")]), ["Generic", "Player1"]);
    assert.equal(conv.restartTitle("Ho_Start_Bike_Racks"), "Bike Racks");
    assert.equal(conv.restartTitle("Re_Start_Skate"), "Skate");
    assert.equal(conv.restartTitle("Control Room"), "Control Room");
    // (THPS2's 0 faces THUG2's +z; a quarter turn more faces -x)
    assert.ok(Math.abs(conv.heading(0)) < 1e-9);
    assert.ok(Math.abs(conv.heading(2048) - Math.PI) < 1e-9);
    assert.ok(Math.abs(conv.heading(1024) + Math.PI / 2) < 1e-9);
});

test("nodeArrayText: restarts then rails, links renumbered to the rail points", () => {
    const nodes = thps2.readTrg(trgFile(), "test");
    const out = conv.nodeArrayText("thps2_test", nodes);
    assert.equal(out.restarts, 1);
    assert.equal(out.rails, 2);
    assert.match(out.text, /^thps2_test_NodeArray = \[/);
    assert.match(out.text, /Position=\(-400\.0, 1000\.0, 200\.0\)/);
    assert.match(out.text, /Type=Player1/);
    assert.match(out.text, /RestartName="Start"/);
    assert.match(out.text, /Name=TRG_Rail_1 [\s\S]*Links=\[2\]/);
    assert.match(out.text, /script LoadTerrain/);
});

test("levels: ten, each its own name and level select number", () => {
    assert.equal(conv.LEVELS.length, 10);
    assert.equal(new Set(conv.LEVELS.map((l) => conv.levelName(l))).size, 10);
    assert.equal(new Set(conv.LEVELS.map((l) => l.levelNum)).size, 10);
    assert.equal(conv.findLevel("skhan")?.name, "hangar");
    assert.equal(conv.findLevel("thps2_hangar")?.base, "skhan");
});
