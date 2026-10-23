// NOT ORIGINAL - tests of the THP8 converter's readers on synthetic data (no game data): the big-endian pak, the
// big-endian qb item format, the scene's strips and vertex layouts, the Xenos tiling, DXT, Havok primitives, the
// node transforms.

import assert from "node:assert/strict";
import { test } from "node:test";
import { checksum, Local, Name, type QbValue, Struct, Vector } from "../level_port/qb.ts";
import * as thaw from "../thaw_levels/thaw.ts";
import * as dxt from "./dxt.ts";
import * as geom from "./geom.ts";
import * as havok from "./havok.ts";
import { readPak, readToc, readTocList } from "./thp8.ts";
import {
    bouncyProps,
    boxTriangles,
    cloudFade,
    emptyScript,
    endlessLoop,
    faceFlags,
    GLOW_DEFAULT,
    glowMaterials,
    glowScale,
    horizontalReach,
    MAX_TRIANGLES,
    materialColour,
    materialKey,
    memberCalls,
    mergeNodeArrays,
    NET_LEVEL_NUM,
    netSpecs,
    place,
    readNetworkAreas,
    refineBudget,
    rotate,
    SCALE,
    soloRestarts,
    thug2Terrain,
    toThug,
    triggerVolumes,
} from "./thp8_levels.ts";
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

// A packfile made here: its sections, fixups and class descriptions as havok.ts's header comment has them.
interface TestSection {
    name: string;
    data: Buffer;
    local: [number, number][];
    global: [number, number, number][];
    virt: [number, number, number][];
}

function packfile(sections: TestSection[]): Buffer {
    const header = Buffer.alloc(0x40 + 0x30 * sections.length);
    header.writeUInt32BE(havok.MAGIC[0]!, 0);
    header.writeUInt32BE(havok.MAGIC[1]!, 4);
    header.writeUInt32BE(4, 0x0c);
    header[0x10] = 4; // pointer bytes
    header[0x11] = 0; // big-endian
    header.writeUInt32BE(sections.length, 0x14);
    header.write("Havok-4.0.0-r1", 0x28, "latin1");
    const parts: Buffer[] = [header];
    let at = header.length;
    sections.forEach((s, i) => {
        const fix = (rows: number[][]): Buffer => be(...rows.flat(), 0xffffffff, 0xffffffff, 0xffffffff);
        const local = fix(s.local); // (padded with 0xFFFFFFFF, as Havok pads them)
        const global = fix(s.global);
        const virt = fix(s.virt);
        const h = 0x40 + 0x30 * i;
        header.write(s.name, h, "latin1");
        const l = s.data.length;
        const g = l + local.length;
        const v = g + global.length;
        const end = v + virt.length;
        [at, l, g, v, end, end, end].forEach((w, k) => {
            header.writeUInt32BE(w, h + 20 + 4 * k);
        });
        parts.push(s.data, local, global, virt);
        at += end;
    });
    return Buffer.concat(parts);
}

test("havok: a packfile's classes read from its __types__, an object's members by name, fixups", () => {
    // __classnames__: {signature, 9, name}
    const nameAt = new Map<string, number>();
    const cn: Buffer[] = [];
    let off = 0;
    for (const n of ["hkClass", "Base", "Thing"]) {
        nameAt.set(n, off + 5);
        const b = Buffer.concat([be(0x12345678), Buffer.from([9]), Buffer.from(`${n}\0`, "latin1")]);
        cn.push(b);
        off += b.length;
    }
    const classnames: TestSection = {
        name: "__classnames__",
        data: Buffer.concat(cn),
        local: [],
        global: [],
        virt: [],
    };
    // __types__: hkClass Base (size 8; id INT32 at 0), Thing : Base (size 28; scale REAL at 8, items ARRAY of INT32
    // at 12, label CSTRING at 24); member records 0x14 bytes; the strings at the end
    const types = Buffer.alloc(0x200);
    const t: TestSection = { name: "__types__", data: types, local: [], global: [], virt: [] };
    let str = 0x180;
    const putString = (at: number, text: string): void => {
        types.write(`${text}\0`, str, "latin1");
        t.local.push([at, str]);
        str += text.length + 1;
    };
    const member = (at: number, name: string, type: number, subtype: number, offset: number): void => {
        putString(at, name);
        types[at + 12] = type;
        types[at + 13] = subtype;
        types.writeUInt16BE(offset, at + 18);
    };
    // Base at 0, its member at 0x60; Thing at 0x30, its members at 0x80
    putString(0x00, "Base");
    types.writeUInt32BE(8, 0x08);
    t.local.push([0x18, 0x60]);
    types.writeUInt32BE(1, 0x1c);
    member(0x60, "id", havok.T.INT32, 0, 0);
    putString(0x30, "Thing");
    t.local.push([0x34, 0x00]); // parent: Base
    types.writeUInt32BE(28, 0x38);
    t.local.push([0x48, 0x80]);
    types.writeUInt32BE(3, 0x4c);
    member(0x80, "scale", havok.T.REAL, 0, 8);
    member(0x94, "items", havok.T.ARRAY, havok.T.INT32, 12);
    member(0xa8, "label", havok.T.CSTRING, 0, 24);
    t.virt.push([0x00, 0, nameAt.get("hkClass")!], [0x30, 0, nameAt.get("hkClass")!]);
    // __data__: a Thing at 0 (id 7, scale 2.5, items -> 3 ints at 0x20, label -> "Thing" in __classnames__)
    const data = Buffer.alloc(0x2c);
    data.writeInt32BE(7, 0);
    data.writeFloatBE(2.5, 8);
    data.writeUInt32BE(3, 16);
    [10, 20, -30].forEach((v, i) => {
        data.writeInt32BE(v, 0x20 + 4 * i);
    });
    const d: TestSection = {
        name: "__data__",
        data,
        local: [[12, 0x20]],
        global: [[24, 0, nameAt.get("Thing")!]],
        virt: [[0, 0, nameAt.get("Thing")!]],
    };

    const pf = new havok.Packfile(packfile([classnames, t, d]), "test.hkc");
    assert.equal(pf.version, "Havok-4.0.0-r1");
    assert.deepEqual(
        pf.allMembers("Thing").map((m) => m.name),
        ["id", "scale", "items", "label"],
    );
    const [thing] = pf.objectsOf("Thing");
    assert.ok(thing);
    assert.equal(pf.classOf(thing), "Thing");
    assert.equal(pf.int(thing, "Thing", "id"), 7);
    assert.equal(pf.real(thing, "Thing", "scale"), 2.5);
    const [items, count] = pf.array(thing, "Thing", "items");
    assert.equal(count, 3);
    assert.deepEqual(
        [0, 1, 2].map((i) => pf.data.readInt32BE(pf.abs(pf.at(items!, 4 * i)))),
        [10, 20, -30],
    );
    assert.equal(pf.str(pf.pointer(thing, "Thing", "label")), "Thing");
    assert.throws(() => pf.member("Thing", "missing"), /no member missing/);
    const bad = packfile([classnames]);
    bad.writeUInt32BE(0, 0);
    assert.throws(() => new havok.Packfile(bad), /not a Havok packfile/);
});

test("sky: cloud layers fade out towards the horizon (none at or below it, all from 0.35 up)", () => {
    assert.equal(cloudFade([100, 0, 0]), 0);
    assert.equal(cloudFade([100, -20, 0]), 0);
    assert.equal(cloudFade([0, 100, 0]), 1);
    assert.equal(cloudFade([100, 40, 0]), 1);
    const low = cloudFade([100, 10, 0]);
    const high = cloudFade([100, 25, 0]);
    assert.ok(low > 0 && low < high && high < 1);
});

test("glows: a material is drawn adding only when every sector using it is a glow / light shaft", () => {
    const mesh = (material: number) => ({ material }) as geom.GeomMesh;
    const sector = (c: number, ...materials: number[]) =>
        ({ checksum: c, meshes: materials.map(mesh) }) as unknown as geom.GeomSector;
    const g = {
        materials: [],
        bbox: [],
        sectors: [sector(1, 10, 11), sector(2, 11, 12), sector(3, 13), sector(4, 14)],
    };
    const names = new Map([
        [1, "Z_Skateshop_Lamp_Glo01"],
        [2, "Z_Skateshop_g_counter02"],
        [3, "Z_Outschool_PH_Outschool_Lightshafts05"],
        [4, "Z_Shops_g_GlowingSign_frame"],
    ]);
    assert.deepEqual([...glowMaterials(g as geom.Geom, names)].sort(), [10, 13]);
});

test("material colour: the four floats before the first texture's checksum; glows scaled by it times its alpha", () => {
    const params = Buffer.alloc(0x80);
    params.writeUInt32BE(0x00000008, 0x24);
    for (const [i, f] of [0.5, 1, 1, 0.1].entries()) params.writeFloatBE(f, 0x50 + 4 * i);
    params.writeUInt32BE(0xe893f0d3, 0x60);
    const m = { textures: [0xe893f0d3], params } as unknown as geom.GeomMaterial;
    assert.deepEqual(
        materialColour(m)!.map((x) => +x.toFixed(3)),
        [0.5, 1, 1, 0.1],
    );
    assert.deepEqual(
        glowScale(m).map((x) => +x.toFixed(3)),
        [0.05, 0.1, 0.1],
    );
    // no texture checksum in the block, or floats out of range: no colour, the stand-in strength
    const none = { textures: [0x1234], params } as unknown as geom.GeomMaterial;
    assert.equal(materialColour(none), null);
    assert.deepEqual(glowScale(none), [GLOW_DEFAULT, GLOW_DEFAULT, GLOW_DEFAULT]);
    params.writeFloatBE(-3, 0x50);
    assert.equal(materialColour(m), null);
});

test("restarts: network spawns also single-player ones (Player1 first), others as they were", () => {
    const text =
        "x_NodeArray = [\n" +
        "    {Pos=(0,0,0) Name=a Class=Restart restart_types=[Multiplayer]}\n" +
        "    {Pos=(0,0,0) Name=b Class=Restart restart_types=[Player1]}\n" +
        "    {Pos=(0,0,0) Name=c Class=Restart restart_types=[Player1 Multiplayer]}\n" +
        "    {Pos=(0,0,0) Name=d Class=RailNode restart_types=[Multiplayer]}\n" +
        "]\n";
    const [out, changed] = soloRestarts(text);
    assert.equal(changed, 1);
    assert.match(out, /Name=a Class=Restart restart_types=\[Player1 Multiplayer\]/);
    assert.match(out, /Name=b Class=Restart restart_types=\[Player1\]/);
    assert.match(out, /Name=d Class=RailNode restart_types=\[Multiplayer\]/);
});

test("face flags: THP8's trigger volumes (0x20 on non-collidable faces) as THUG2's trigger 0x40", () => {
    assert.equal(faceFlags(0x0004_00b0), 0xd0); // (terrain bits dropped; 0x20 -> 0x40, non-collidable and camera kept)
    assert.equal(faceFlags(0x20), 0x20); // (collidable: a decal, as it was)
    assert.equal(faceFlags(0x48), 0x48);
});

test("trigger volumes: collision-only LevelGeometry nodes with a TriggerScript", () => {
    const names = new Map<number, string>([[checksum("LevelGeometry"), "LevelGeometry"]]);
    const node = (name: string, script: boolean): Struct => {
        const s = new Struct([
            [new Name(checksum("Name")), new Name(checksum(name))],
            [new Name(checksum("Class")), new Name(checksum("LevelGeometry"))],
            [new Name(checksum("Pos")), new Vector("vector", [1, 2, 3])],
        ]);
        if (script) s.entries.push([new Name(checksum("TriggerScript")), new Name(checksum(`${name}Script`))]);
        return s;
    };
    const array = [node("Kill", true), node("Drawn", true), node("Plain", false)];
    const v = triggerVolumes(array, names, new Set([checksum("Drawn")]));
    assert.deepEqual(
        v.map((x) => [x.name, x.pos]),
        [[checksum("Kill"), [1, 2, 3]]],
    );
});

test("member calls: `Skater:KillSkater node=x` read as a call of KillSkater", () => {
    const tok = (t: number, word?: number): [number, Buffer] => {
        const p = Buffer.alloc(word === undefined ? 0 : 4);
        if (word !== undefined) p.writeUInt32LE(word >>> 0);
        return [t, p];
    };
    const line = [tok(0x16, checksum("Skater")), tok(0x42), tok(0x16, checksum("KillSkater")), tok(0x16, 7)];
    const q = {
        scripts: new Map([[1, [line]]]),
        calls: () => [[checksum("Skater"), line]],
    } as unknown as thaw.QbItems;
    const calls = memberCalls(q).calls(1);
    assert.deepEqual(
        calls.map(([c, l]) => [c, l.length]),
        [
            [checksum("Skater"), 4],
            [checksum("KillSkater"), 2],
        ],
    );
});

test("endless loops: a while with no count and no wait that can run is found; the script emptied", () => {
    const names = new Map<number, string>([
        [checksum("wait"), "wait"],
        [checksum("GotParam"), "GotParam"],
    ]);
    const name = (text: string): Buffer => {
        const b = Buffer.alloc(5);
        b[0] = 0x16;
        b.writeUInt32LE(checksum(text), 1);
        return b;
    };
    const b = (...x: number[]): Buffer => Buffer.from(x);
    // script s { while { if GotParam thps3_port_never { wait } } }  (repeat with no count)
    const body = (inner: Buffer[], count: Buffer = b()) =>
        Buffer.concat([b(0x23), name("s"), b(0x01, 0x20, 0x01), ...inner, b(0x21), count, b(0x01, 0x24)]);
    const neverWait = [
        b(0x47, 0x10, 0x00),
        name("GotParam"),
        name("thps3_port_never"),
        b(0x01),
        name("wait"),
        b(0x01, 0x28, 0x01),
    ];
    assert.equal(endlessLoop(body(neverWait), names), true);
    assert.equal(endlessLoop(body([name("wait"), b(0x01)]), names), false); // (a wait that runs)
    assert.equal(endlessLoop(body(neverWait, b(0x17, 5, 0, 0, 0)), names), false); // (counted)
    // (the never-true if's else branch runs)
    const elseWait = [
        b(0x47, 0x10, 0x00),
        name("GotParam"),
        name("thps3_port_never"),
        b(0x01, 0x48, 0x02, 0x00, 0x01),
        name("wait"),
        b(0x01, 0x28, 0x01),
    ];
    assert.equal(endlessLoop(body(elseWait), names), false);
    assert.deepEqual([...emptyScript(body(neverWait))], [0x23, ...name("s"), 0x01, 0x24]);
});

test("props: GO_ game objects made at the start become bouncy LevelObjects with their model's box as contacts", () => {
    const names = new Map<number, string>([
        [checksum("GameObject"), "GameObject"],
        [checksum("GO_Cafe_Chair"), "GO_Cafe_Chair"],
        [checksum("Ghost"), "Ghost"],
    ]);
    const node = (name: string, type: string, made: boolean): Struct => {
        const s = new Struct([
            [new Name(checksum("Name")), new Name(checksum(name))],
            [new Name(checksum("Pos")), new Vector("vector", [1, 2, 3])],
            [new Name(checksum("Class")), new Name(checksum("GameObject"))],
            [new Name(checksum("Type")), new Name(checksum(type))],
        ]);
        if (made) s.entries.push([null, new Name(checksum("CreatedAtStart"))]);
        s.entries.push([null, new Name(checksum("Bouncy"))]);
        return s;
    };
    const chair = node("Chair1", "GO_Cafe_Chair", true);
    const later = node("Chair2", "GO_Cafe_Chair", false);
    const ghost = node("G", "Ghost", true);
    const models = new Map([[checksum("GO_Cafe_Chair"), { box: [-0.5, 0, -0.5, 0.5, 1, 0.5] } as never]]);
    const made = bouncyProps([chair, later, ghost], names, models);
    assert.deepEqual([...made], [[checksum("Chair1"), checksum("GO_Cafe_Chair")]]);
    assert.equal(names.get((chair.get("Class") as Name).value), "LevelObject");
    assert.ok(chair.hasFlag("bouncy") && chair.hasFlag("CreatedAtStart"));
    const contacts = chair.get("contacts") as Vector[];
    assert.equal(contacts.length, 8);
    assert.deepEqual(
        contacts[7]!.values.map((v) => +v.toFixed(3)),
        [0.5 * SCALE, SCALE, 0.5 * SCALE].map((v) => +v.toFixed(3)),
    );
    assert.equal(names.get((later.get("Class") as Name).value), "GameObject");
    // the box: twelve triangles owned by the node, covering the six sides
    const tris = boxTriangles([0, 0, 0, 1, 2, 3], 42);
    assert.equal(tris.length, 12);
    assert.ok(tris.every((t) => t.owner === 42));
});

// --- network levels / zones merged ----------------------------------------------------------------------------------

test("network levels: THP8's list read - text, id, spawn zones, barrier pak, its zone; only several zones become levels", () => {
    const names = new Map<number, string>();
    const n = (text: string): Name => {
        names.set(checksum(text), text);
        return new Name(checksum(text));
    };
    const level = (id: string, text: string, pak: string, zone: string, zones: string[]): Struct =>
        new Struct([
            [n("text"), new Local(text)],
            [n("area_pak"), new Local(pak)],
            [n("area_pak_zone"), new Local(zone)],
            [n("checksum"), n(id)],
            [n("spawn_in"), zones.map(n)],
            [null, n("multiplayer_level")],
            [n("ui_struct"), n(`ui_${id}`)],
        ]);
    const globals = new Map<number, QbValue>([
        [checksum("mp_park"), level("Skatepark", "Skate Park", "z_online_skatepark", "Z_Cretepark", ["z_cretepark"])],
        [
            checksum("mp_school"),
            level("School", "SCHOOL", "z_online_school", "Z_Outschool", ["z_inschool", "Z_Outschool"]),
        ],
        [checksum("mp_town"), level("BeaverValley", "BEAVER VALLEY", "", "", ["z_houses", "z_shops"])],
        [checksum("ui_School"), new Struct([[n("description"), new Local("School's not out")]])],
        [
            checksum("online_level_select_menu_level_info"),
            ["mp_town", "mp_park", "mp_school"].map(
                (x) =>
                    new Struct([
                        [null, n(x)],
                        [n("level"), n(`load_${x}`)],
                    ]),
            ),
        ],
    ]);
    const areas = readNetworkAreas(globals, names);
    assert.deepEqual(
        areas.map((a) => [a.id, a.text, a.zones, a.barriers, a.mainZone]),
        [
            ["BeaverValley", "BEAVER VALLEY", ["z_houses", "z_shops"], "", ""],
            ["Skatepark", "SKATE PARK", ["z_cretepark"], "z_online_skatepark", "z_cretepark"],
            ["School", "SCHOOL", ["z_inschool", "z_outschool"], "z_online_school", "z_outschool"],
        ],
    );
    assert.equal(areas[2]!.description, "School's not out");
    const specs = netSpecs(areas, new Set(["SCHOOL"]));
    assert.deepEqual(
        specs.map((s) => [s.level, s.folder, s.zones, s.barriers, s.sign, s.levelNum]),
        [
            ["thp8_net_beavervalley", "net_beavervalley", ["z_houses", "z_shops"], [], "BEAVER VALLEY", NET_LEVEL_NUM],
            // (the area's own zone first; " AREA" where a zone level has the sign already)
            [
                "thp8_net_school",
                "net_school",
                ["z_outschool", "z_inschool"],
                ["z_online_school"],
                "SCHOOL AREA",
                NET_LEVEL_NUM + 1,
            ],
        ],
    );
});

test("zones merged: node arrays one after the other, each one's Links moved up by the nodes before it", () => {
    const links = (...l: number[]): Struct => new Struct([[new Name(checksum("Links")), l]]);
    const a: QbValue[] = [links(1), new Struct([]), links(0)];
    const b: QbValue[] = [links(1, 2), links(0), new Struct([])];
    const merged = mergeNodeArrays([a, b]);
    assert.equal(merged.length, 6);
    assert.deepEqual(
        merged.map((x) => (x as Struct).get("Links") ?? null),
        [[1], null, [0], [4, 5], [3], null],
    );
});

test("zones merged: a material is one only when its definition agrees (light maps, parameters)", () => {
    const m = (textures: number[], p = 0): geom.GeomMaterial =>
        ({
            checksum: 7,
            flags: 0,
            template: 1,
            textures,
            terrain: 0,
            state: 0,
            params: Buffer.from([p]),
        }) as geom.GeomMaterial;
    assert.equal(materialKey(m([1, 2, 3])), materialKey(m([1, 2, 3])));
    assert.notEqual(materialKey(m([1, 2, 3])), materialKey(m([1, 2, 4]))); // (another zone's shadow map)
    assert.notEqual(materialKey(m([1, 2, 3])), materialKey(m([1, 2, 3], 1)));
});

test("zones merged: the light's triangle budget per zone; the reach from the middle for network positions", () => {
    const counts = { triangles: 100000, refined: 0, refinedFrom: 0 } as Parameters<typeof refineBudget>[0];
    assert.equal(refineBudget({ ...counts, triangles: 180000 }), MAX_TRIANGLES);
    assert.equal(refineBudget({ ...counts, triangles: 360000 }, 2), 2 * MAX_TRIANGLES);
    const tri = (x: number, z: number) => ({ a: [x, 0, z], b: [x, 0, z], c: [0, 0, 0], material: 0, owner: 0 });
    // (metres in inches, moved by the shift)
    assert.equal(
        Math.round(horizontalReach([{ index: 0, triangles: [tri(100, -300)] }], [0, 0, 1000])),
        Math.round(300 * SCALE - 1000),
    );
});
