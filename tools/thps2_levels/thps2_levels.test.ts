// NOT ORIGINAL - tests of the THPS2 readers (pkr.ts, thps2.ts) and converter (thps2_levels.ts) on synthetic data: no
// THPS2 file is needed (nothing of the game is in the repo).

import assert from "node:assert/strict";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { test } from "node:test";
import { checksum as qbChecksum } from "../level_port/qb.ts";
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

test("faces: uvs are texel numbers over the size (linear, they tile); colours from the palette or the face", () => {
    const psx = thps2.readPsx(worldPsx(), "test");
    const [quad, tri] = psx.models[0]!.faces as [thps2.PsxFace, thps2.PsxFace];
    assert.deepEqual(conv.faceUvs(quad, 4, 2), [
        [0, 0],
        [0, 0.5],
        [0.75, 0],
        [0.75, 0.5],
    ]);
    assert.equal(conv.faceTiles(quad, 4, 2), false);
    // a floor tiled three times over a 64-wide texture (190 = 3 x 64 - 2, the common span)
    const floor = { ...quad, uvs: [0, 0, 0, 62, 190, 0, 190, 62] };
    assert.deepEqual(
        conv.faceUvs(floor, 64, 64).map(([u, v]) => [u, v]),
        [
            [0, 0],
            [0, 62 / 64],
            [190 / 64, 0],
            [190 / 64, 62 / 64],
        ],
    );
    assert.equal(conv.faceTiles(floor, 64, 64), true);
    assert.equal(conv.makeMaterial(1, 2, false, true).passes[0]!.uAddress, 0);
    assert.equal(conv.makeMaterial(1, 2, false, false).passes[0]!.uAddress, 1);
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
    assert.deepEqual(done.counts, {
        objects: 1,
        faces: 2,
        hidden: 1,
        noCollision: 1,
        bmps: 0,
        triggers: 0,
        duplicates: 0,
        layers: 0,
    });
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
    // the start node's names decide; then another 2P start is only a multiplayer one (Hangar's Re_Start_2P_NY)
    assert.deepEqual(conv.restartKinds([r("Start"), r("Re_Start_2P_NY"), r("2P Start 1")], "Start", "2P Start 1"), [
        "Player1",
        "MultiPlayer",
        "Player2",
    ]);
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

// --- the command language ------------------------------------------------------------------------------------------

// A .trg whose nodes are built knowing their file offset (each pads its own 32-bit data to 4 in the file).
function trgOf(builders: ((at: number) => Buffer)[]): Buffer {
    const header = 12 + builders.length * 4;
    const offsets: number[] = [];
    const parts: Buffer[] = [];
    let at = header;
    for (const b of builders) {
        offsets.push(at);
        const n = b(at);
        parts.push(n);
        at += n.length;
    }
    return Buffer.concat([Buffer.from("_TRG"), u32(2, builders.length), u32(...offsets), ...parts]);
}
const pad4 = (at: number, size: number): Buffer => Buffer.alloc((4 - ((at + size) % 4)) % 4);
const cstr = (s: string): Buffer => {
    const b = Buffer.from(`${s}\0`, "latin1");
    return b.length & 1 ? Buffer.concat([b, Buffer.alloc(1)]) : b;
};
// a command point: links, [aligned] trigger, then the commands (built with the offset of where they start)
function commandPoint(links: number[], trigger: number, commands: (at: number) => Buffer): (at: number) => Buffer {
    return (at) => {
        const head = u16(thps2.NODE_COMMAND_POINT, links.length, ...links);
        const pad = pad4(at, head.length);
        const before = Buffer.concat([head, pad, u32(trigger)]);
        return Buffer.concat([before, commands(at + before.length)]);
    };
}
const C = thps2.CMD;
// GapPolyHit: op, [aligned] other trigger, gap
const gapHit = (at: number, other: number, gap: number): Buffer => {
    const op = u16(C.GapPolyHit);
    return Buffer.concat([op, pad4(at, 2), u32(other), u16(gap)]);
};

// Restarts 0 "Start" and 1 "Bowl"; 2 the level's start commands (SetRestart "Bowl", SetRestart2 "Start"); command
// points 3 (trigger 0xAAAA: gap 2700 after 0xBBBB), 4 (0xBBBB: gap 2700 after 0xAAAA, and a career-only pulse),
// 5 (0xCCCC: pulses 6), 6 (no trigger: KillBruce, pulses restart 1); 7 the end.
function commandTrg(): Buffer {
    const restart = (name: string, commands: Buffer) => (at: number) => {
        const head = u16(thps2.NODE_RESTART, 0);
        return Buffer.concat([head, pad4(at, head.length), i32(0, 0, 0), i16(0, 0, 0), cstr(name), commands]);
    };
    return trgOf([
        restart(
            "Start",
            Buffer.concat([
                u16(C.SetReverbType, 1, C.SetCheatRestarts),
                cstr("Start"),
                cstr("Bowl"),
                u16(0),
                u16(0xffff),
            ]),
        ),
        restart("Bowl", u16(0xffff)),
        () =>
            Buffer.concat([
                u16(thps2.NODE_START, C.SetRestart),
                cstr("Bowl"),
                u16(C.SetRestart2),
                cstr("Start"),
                u16(C.SpoolIn),
                cstr("SkTest_O"),
                u16(C.SetFadeColor, 1, 2, 0xffff),
            ]),
        commandPoint([], 0xaaaa, (at) =>
            Buffer.concat([gapHit(at, 0xbbbb, 2700), u16(C.SetInitialPulses, 1, C.SendPulse, 0xffff)]),
        ),
        commandPoint([5], 0xbbbb, (at) =>
            Buffer.concat([gapHit(at, 0xaaaa, 2700), u16(C.IfCareerFlag, 3, C.SendPulse, C.Endif, 0xffff)]),
        ),
        commandPoint([6], 0xcccc, () => u16(C.SetInitialPulses, 0xffff, C.SendPulse, 0xffff)),
        commandPoint([1], 0, () => u16(C.KillBruce, C.SendPulse, 0xffff)),
        () => u16(thps2.NODE_END),
    ]);
}

test("commands: lists of command points, restarts and the start node parse to their ends", () => {
    const trg = thps2.readTrg(commandTrg(), "test");
    assert.deepEqual(thps2.restartCommands(trg[0]!), [
        { op: C.SetReverbType, args: [1] },
        { op: C.SetCheatRestarts, args: [["Start", "Bowl"]] },
    ]);
    assert.deepEqual(thps2.startCommands(trg[2]!), [
        { op: C.SetRestart, args: ["Bowl"] },
        { op: C.SetRestart2, args: ["Start"] },
        { op: C.SpoolIn, args: ["SkTest_O"] },
        { op: C.SetFadeColor, args: [1, 2] },
    ]);
    const a = thps2.commandPointOf(trg[3]!);
    assert.equal(a.trigger, 0xaaaa);
    assert.deepEqual(a.commands[0], { op: C.GapPolyHit, args: [0xbbbb, 2700] });
    assert.equal(thps2.commandPointOf(trg[6]!).trigger, 0);
    // an unknown opcode is a FormatError
    const bad = thps2.readTrg(trgOf([commandPoint([], 1, () => u16(999, 0xffff))]), "bad");
    assert.throws(() => thps2.commandPointOf(bad[0]!), thps2.FormatError);
});

test("triggers: gaps both ways, kills to the restart they pulse, career-only pulses left out", () => {
    const trg = thps2.readTrg(commandTrg(), "test");
    const effects = conv.triggerEffects(trg);
    const a = effects.get(0xaaaa)!;
    const b = effects.get(0xbbbb)!;
    assert.deepEqual([a.gapEnds, a.gapStarts], [[2700], [2700]]);
    assert.deepEqual([b.gapEnds, b.gapStarts], [[2700], [2700]]);
    assert.equal(b.kill, false); // (its pulse of 5 is under IfCareerFlag)
    const k = effects.get(0xcccc)!;
    assert.equal(k.kill, true);
    assert.equal(k.killRestart, 1);
    assert.equal(effects.has(0), false);
    // the node array: the start command's restart is Player1, the triggers' nodes and scripts
    const placed = [...effects.values()].map((effect) => ({ effect, position: [0, 0, 0] as conv.Vec3 }));
    const out = conv.nodeArrayText("thps2_test", trg, placed);
    assert.deepEqual([out.triggers, out.gaps, out.kills], [3, 1, 1]);
    assert.match(
        out.text,
        /Name=TRG_Restart_01 [\s\S]*RestartName="Bowl" [\s\S]*Type=Player1|Type=Player1 [\s\S]*RestartName="Bowl"/,
    );
    assert.match(out.text, /Name=TRG_Trigger_0000aaaa [\s\S]*TriggerScript=thps2_test_Trigger_0000aaaa/);
    assert.match(
        out.text,
        /script thps2_test_Trigger_0000aaaa \{\n {4}EndGap GapID=thps2_test_Gap_2700 score=250 text="GAP 2700" \n {4}StartGap GapID=thps2_test_Gap_2700 flags=\[CANCEL_GROUND\] \n\}/,
    );
    // "Bowl" is restart 1, the first listed (Player1): TRG_Restart_01
    assert.match(out.text, /script thps2_test_Trigger_0000cccc \{\n {4}SK3_KillSkater nodename=TRG_Restart_01 \n\}/);
    assert.deepEqual(conv.gapNumbers(trg), [2700]);
    // with THPS2's gap table: its name (quotes made safe) and score
    const named = conv.nodeArrayText(
        "thps2_test",
        trg,
        placed,
        [],
        new Set(),
        new Map([[2700, { name: 'OVER THE "PIPE"', score: 500 }]]),
    );
    assert.equal(named.gapsNamed, 1);
    assert.match(named.text, /EndGap GapID=thps2_test_Gap_2700 score=500 text="OVER THE 'PIPE'" /);
    assert.equal(out.gapsNamed, 0);
});

test("convertWorld: a trigger's faces become a trigger object of its node's name, both windings", () => {
    const psx = thps2.readPsx(worldPsx(), "test");
    const textures: conv.LevelTextures = { byName: new Map(), bmps: new Map() };
    const name = 0x12345678;
    const out = conv.convertWorld(psx, "thps2_test", textures, false, new Map([[psx.modelNames[0]!, name]]));
    assert.equal(out.counts.triggers, 1);
    const t = out.col.objects.find((o) => o.checksum === name)!;
    assert.equal(t.faces.length, 2);
    for (const f of t.faces) assert.equal(f[0], conv.TRIGGER_FACE_FLAGS);
    assert.deepEqual(t.faces[0]!.slice(2), [t.faces[1]![2], t.faces[1]![4], t.faces[1]![3]]);
});

test("terrains: THPS2's numbers as THUG2's Terrain_Types indices; rails by their flags", () => {
    const psx = thps2.readPsx(worldPsx(), "test");
    const quad = psx.models[0]!.faces[0]!; // terrain 3: T_DIRT
    assert.equal(conv.faceTerrain(quad), conv.THUG2_TERRAIN_TYPES.indexOf("DIRT"));
    assert.equal(conv.THUG2_TERRAIN_TYPES.indexOf("DIRT"), 22);
    assert.equal(conv.THUG2_TERRAIN_TYPES.indexOf("CONCSMOOTH"), 1);
    const out = conv.convertWorld(psx, "thps2_test", { byName: new Map(), bmps: new Map() }, false);
    assert.equal(out.col.objects[0]!.faces[0]![1], 22);
    assert.deepEqual(conv.railTerrain(0x10), ["Concrete", "CONCSMOOTH"]);
    assert.deepEqual(conv.railTerrain(1), ["Metal", "METALSMOOTH"]);
    assert.deepEqual(conv.railTerrain(0x22), ["Wood", "WOOD"]);
    assert.match(conv.loadTerrainNs(), /terrain=TERRAIN_DIRT [\s\S]*terrain=TERRAIN_GENERIC2 /);
    const nodes = conv.nodeArrayText("thps2_test", thps2.readTrg(trgFile(), "test"));
    assert.match(
        nodes.text,
        /Name=TRG_Rail_1 \n {8}Class=RailNode \n {8}Type=Metal [\s\S]*TerrainType=TERRAIN_METALSMOOTH /,
    );
});

test("sky: BackgroundCreate's models, the _o file's model placed at the origin", () => {
    const bg = (at: number): Buffer => {
        const head = u16(thps2.NODE_START, C.BackgroundCreate);
        return Buffer.concat([head, pad4(at, head.length), u32(0xabcdef01), u16(0, 0, 0, 0xffff)]);
    };
    const trg = thps2.readTrg(trgOf([bg, () => u16(thps2.NODE_END)]), "test");
    assert.deepEqual(conv.backgroundModels(trg), [0xabcdef01]);
    const psx = thps2.readPsx(worldPsx(), "test"); // (its one model is named 0xabcdef01, at (100, -200, 300))
    const sky = conv.convertSky(psx, [0xabcdef01], "thps2_test_sky", { byName: new Map(), bmps: new Map() }, false)!;
    assert.equal(sky.scene.sectors.length, 1);
    // the quad's corner (0, 0, 0) of the model at the origin, not at the object's place
    assert.ok(Math.abs(sky.scene.sectors[0]!.bbox[0]!) < 1e-3);
    assert.equal(conv.convertSky(psx, [0x12], "x", { byName: new Map(), bmps: new Map() }, false), null);
});

// a type-1 object of the model 0xabcdef01 (worldPsx's), its script tokens after the model
function objectNode(kind: number, angles: number[], tokens: number[]): (at: number) => Buffer {
    return (at) => {
        const head = Buffer.concat([u16(1, kind, 0x1000, 0), Buffer.from([0, 2, 4, 0xff])]);
        const pos = Buffer.concat([head, pad4(at, head.length), i32(4096 * 9, -4096 * 18, 4096 * 27), i16(...angles)]);
        const model = u16(thps2.V_MODEL_CHECKSUM);
        const mAt = at + pos.length + 2;
        return Buffer.concat([pos, model, pad4(mAt, 0), u32(0xabcdef01), u16(...tokens)]);
    };
}

test("objects: static, bouncy, spinning, scripted and turned platforms as LevelObjects of their own sectors", () => {
    const trg = thps2.readTrg(
        trgOf([
            objectNode(thps2.OBJECT_PLATFORM, [0, 1024, 0], [0x2127, 0, 0, 128, thps2.C_DONE]),
            objectNode(thps2.OBJECT_PLATFORM, [0, 0, 0], [0x4105, 0x4309, 3, thps2.C_DONE]),
            objectNode(thps2.OBJECT_PLATFORM, [0, 0, 0], [0x4281, 0x4301, thps2.C_DONE]),
            objectNode(thps2.OBJECT_PLATFORM, [100, 0, 0], [thps2.C_DONE]),
            () => u16(thps2.NODE_END),
        ]),
        "test",
    );
    const spin = thps2.objectOf(trg[0]!);
    assert.deepEqual(
        [spin.kind, spin.created, spin.position, spin.angles],
        [402, [0, 2, 4], [36864, -73728, 110592], [0, 1024, 0]],
    );
    assert.equal(spin.model, 0xabcdef01);
    assert.equal(conv.objectBehaviour(spin), "spinning");
    // 128 units a frame about z, 30 frames a second: 337.5 degrees a second, turned by the half turn about x
    assert.deepEqual(conv.spinSpeeds(spin), [0, 0, -337.5]);
    assert.equal(conv.objectBehaviour(thps2.objectOf(trg[1]!)), "bouncy");
    assert.equal(conv.objectBehaviour(thps2.objectOf(trg[2]!)), "scripted");
    const world = thps2.readPsx(worldPsx(), "world");
    const objs = conv.levelObjects(world, world, trg);
    assert.deepEqual(objs.counts, { static: 1, bouncy: 1, spinning: 1, scripted: 1, turned: 1, noModel: 0 });
    assert.equal(objs.world.objects.length, world.objects.length + 4);
    assert.deepEqual(
        objs.placed.map((p) => [p.name, p.behaviour]),
        [
            ["TRG_Object_0", "spinning"],
            ["TRG_Object_1", "bouncy"],
            ["TRG_Object_2", "scripted"],
            ["TRG_Object_3", "static"],
        ],
    );
    // the model appended once (and a turned copy for the one turned about x), its texture renumbered after the
    // world's names
    assert.equal(objs.world.models.length, world.models.length + 2);
    assert.equal(objs.world.models[1]!.faces[0]!.texture, world.textureNames.length);
    // (100 / 4096 of a turn about x: the turned copy's vertices are the model's turned)
    const plain = objs.world.models[1]!.vertices;
    const turned = objs.world.models[2]!.vertices;
    const ax = (100 * 2 * Math.PI) / 4096;
    plain.forEach((v, k) => {
        const t = turned[k]!;
        assert.ok(Math.abs(t[0]! - v[0]!) < 1e-6);
        assert.ok(Math.abs(t[1]! - (v[1]! * Math.cos(ax) - v[2]! * Math.sin(ax))) < 1e-6);
        assert.ok(Math.abs(t[2]! - (v[1]! * Math.sin(ax) + v[2]! * Math.cos(ax))) < 1e-6);
    });
    const out = conv.convertWorld(
        objs.world,
        "thps2_test",
        { byName: new Map(), bmps: new Map() },
        false,
        new Map(),
        objs.named,
    );
    const spinName = qbChecksum("TRG_Object_0");
    assert.ok(out.scene.sectors.some((s) => s.checksum === spinName));
    assert.ok(out.col.objects.some((o) => o.checksum === spinName));
    assert.ok(!out.col.objects.some((o) => o.checksum === qbChecksum("TRG_Object_1"))); // (bouncy: no collision)
    const text = conv.nodeArrayText("thps2_test", trg, [], objs.placed).text;
    assert.match(
        text,
        /Name=TRG_Object_0 \n {8}Class=LevelObject \n {8}CreatedAtStart \n {8}CollisionMode=Geometry \n {8}TriggerScript=thps2_test_Spin_0 /,
    );
    assert.match(text, /Name=TRG_Object_1 \n {8}Class=LevelObject \n {8}CreatedAtStart \n {4}\}/);
    assert.match(text, /script thps2_test_Spin_0 \{\n {4}Obj_RotZ speed=-337\.5 \n\}/);
});

test("faces: quads whose corners go round split along 0-2; blend modes from flags 0x40 and bits 8-9", () => {
    const face = (flags: number): thps2.PsxFace => ({
        flags,
        vertices: [0, 1, 2, 3],
        shade: [0, 0, 0, 0],
        plane: 0,
        surface: 0,
        texture: -1,
        uvs: [],
        extra: Buffer.alloc(0),
    });
    // Hangar's thin strip: corners round (0 and 3 at one end, 1 and 2 at the other)
    const round = [
        [0, 16, 139],
        [8, 16, -137],
        [16, 0, -137],
        [24, 0, 139],
    ];
    const grid = [
        [0, 0, 0],
        [64, 0, 0],
        [0, 0, 64],
        [64, 0, 64],
    ];
    assert.equal(conv.quadGoesRound(face(0), round), true);
    assert.equal(conv.quadGoesRound(face(0), grid), false);
    assert.deepEqual(conv.faceTriangles(face(0), round), [
        [0, 2, 1],
        [0, 3, 2],
    ]);
    assert.deepEqual(conv.faceTriangles(face(0), grid), [
        [0, 2, 1],
        [1, 2, 3],
    ]);
    assert.equal(conv.faceBlend(face(0x0100)), "opaque");
    assert.equal(conv.faceBlend(face(0x0040)), "half");
    assert.equal(conv.faceBlend(face(0x0140)), "add");
    assert.equal(conv.makeMaterial(1, 2, "add").passes[0]!.blendMode, 1);
    assert.equal(conv.makeMaterial(1, 2, "half").passes[0]!.blendMode, 5);
    assert.equal(conv.makeMaterial(1, 2, "opaque").passes[0]!.blendMode, 0);
});

test("levels: ten, each its own name and level select number", () => {
    assert.equal(conv.LEVELS.length, 10);
    assert.equal(new Set(conv.LEVELS.map((l) => conv.levelName(l))).size, 10);
    assert.equal(new Set(conv.LEVELS.map((l) => l.levelNum)).size, 10);
    assert.equal(conv.findLevel("skhan")?.name, "hangar");
    assert.equal(conv.findLevel("thps2_hangar")?.base, "skhan");
});
