// NOT ORIGINAL - tests of tools/level_port against values its Python twin gave. The game-data ones are skipped when
// the game isn't installed (the full comparison: tools/parity/level_port/cases.ts).

import assert from "node:assert/strict";
import * as fs from "node:fs";
import * as path from "node:path";
import { test } from "node:test";
import { Float } from "../lib/py.ts";
import { fixProfiles } from "./nodes.ts";
import { lzssDecode } from "./prx.ts";
import { checksum, fmtFloat, Name, nameText, QbFile, Struct, toNs, Vector } from "./qb.ts";
import * as tex from "./tex.ts";
import * as thug2Col from "./thug2_col.ts";
import * as thug2Scene from "./thug2_scene.ts";

const THUG2_TR = "C:\\Games\\THUG2\\Game\\Data\\levels\\TR";

test("checksums are unsigned CRC-32s of the lower case name", () => {
    assert.deepEqual(
        ["Wait", "NodeArray", "TerrainType", "Links", ""].map((n) =>
            checksum(n).toString(16).toUpperCase().padStart(8, "0"),
        ),
        ["82117C1A", "C472ECC5", "54CF8532", "2E7D5EE7", "FFFFFFFF"],
    );
    assert.equal(checksum("WAIT"), checksum("wait"));
    assert.throws(() => checksum("caf\u00e9")); // (Python's encode("ascii") raises)
});

test("fmtFloat writes the float32's repr, fixed-point for exponents", () => {
    const expected = [
        "0.10000000149011612",
        "1.0",
        "-0.0",
        "0.000010000",
        "300000000000000012135895401846682943488.000000000",
        "10000000000000000.000000000",
        "123456.7890625",
        "inf.0",
        "0.000000250",
    ];
    assert.deepEqual(
        [0.1, 1.0, -0, 1e-5, 3e38, 1e16, 123456.789, Number.POSITIVE_INFINITY, 2.5e-7].map(fmtFloat),
        expected,
    );
});

test("toNs: ints, Floats, vectors, flags, names", () => {
    const names = new Map([
        [checksum("Pos"), "Pos"],
        [checksum("TrickObject"), "TrickObject"],
        [checksum("Links"), "Links"],
    ]);
    const s = new Struct([
        [new Name(checksum("Pos")), new Vector("vector", [1.0, 2.5, -3.0])],
        [null, new Name(checksum("TrickObject"))],
        [new Name(checksum("Links")), [1, 2]],
        [new Name(checksum("x")), new Float(1.0)],
        [new Name(checksum("s")), 'a"b'],
    ]);
    assert.equal(
        toNs(s, names),
        '{\n    Pos=(1.0, 2.5, -3.0)\n    TrickObject\n    Links=[1, 2]\n    #7323E97C=1.0\n    #E4F130F4="a\\"b"\n}',
    );
    // (a keyword as a name is written as its checksum)
    assert.equal(nameText(checksum("script"), new Map([[checksum("script"), "script"]])), "#E37E78C5");
});

test("QbFile reads globals, negative numbers and floats", () => {
    // NodeArray = [ { x=-3 y=-1.5 } ] then the end
    const name = (n: string): Buffer => {
        const b = Buffer.alloc(5);
        b[0] = 0x16;
        b.writeUInt32LE(checksum(n), 1);
        return b;
    };
    const int = (v: number): Buffer => {
        const b = Buffer.alloc(5);
        b[0] = 0x17;
        b.writeInt32LE(v, 1);
        return b;
    };
    const float = (v: number): Buffer => {
        const b = Buffer.alloc(5);
        b[0] = 0x1a;
        b.writeFloatLE(v, 1);
        return b;
    };
    const data = Buffer.concat([
        name("NodeArray"),
        Buffer.from([0x07, 0x05, 0x03]),
        name("x"),
        Buffer.from([0x07, 0x0a]),
        int(3),
        name("y"),
        Buffer.from([0x07, 0x0a]),
        float(1.5),
        Buffer.from([0x04, 0x06, 0x01, 0x00]),
    ]);
    const q = new QbFile(data);
    const array = q.get("NodeArray") as Struct[];
    assert.equal(array[0]!.get("x"), -3);
    const y = array[0]!.get("y");
    assert.ok(y instanceof Float && y.value === -1.5);
});

test("LZSS: a literal run and a back reference", () => {
    // flags 0b01 -> literal "a", then a reference to window position 4078 (the "a") of 4 bytes: 5 bytes out, as
    // Python's (the last reference isn't cut at the size)
    const out = lzssDecode(Buffer.from([0x01, 0x61, 0xee, 0xf1]), 4);
    assert.equal(out.toString("latin1"), "aaaaa");
});

test("THUG2's own TR level read and written again is byte-identical", { skip: !fs.existsSync(THUG2_TR) }, () => {
    const scn = fs.readFileSync(path.join(THUG2_TR, "TR.scn.xbx"));
    assert.ok(thug2Scene.writeScene(thug2Scene.readScene(scn)).equals(scn));
    const col = fs.readFileSync(path.join(THUG2_TR, "TR.col.xbx"));
    assert.ok(thug2Col.writeCol(thug2Col.readCol(col)).equals(col));
    const t = fs.readFileSync(path.join(THUG2_TR, "TR.tex.xbx"));
    assert.ok(tex.writeTex(tex.readTex(t)).equals(t));
});

test("fixProfiles: a ped profile THUG2 hasn't becomes THUG2's random one (a woman's female)", () => {
    const names = new Map<number, string>();
    const k = (t: string): Name => {
        names.set(checksum(t), t);
        return new Name(checksum(t));
    };
    const ped = (name: string, type: string, profile: string): Struct =>
        new Struct([
            [k("Name"), k(name)],
            [k("Type"), k(type)],
            [k("profile"), k(profile)],
        ]);
    const array = [
        ped("A", "Ped_From_Profile", "random_sk3_male_profile"),
        ped("B", "Ped_F_From_Profile", "unknown_profile"),
        ped("C", "Ped_From_Profile", "PedPro_Muska"),
        new Struct([[k("Name"), k("D")]]),
    ];
    const known = new Set([checksum("PedPro_Muska")]);
    const changed = fixProfiles(array, names, (v) => known.has(v));
    assert.equal(changed.length, 2);
    assert.equal((array[0]!.get("profile") as Name).value, checksum("random_male_profile"));
    assert.equal((array[1]!.get("profile") as Name).value, checksum("random_female_profile"));
    assert.equal((array[2]!.get("profile") as Name).value, checksum("PedPro_Muska"));
    assert.equal(array[3]!.get("profile"), undefined);
    assert.ok(changed[0]!.startsWith("A: random_sk3_male_profile -> random_male_profile"));
});
