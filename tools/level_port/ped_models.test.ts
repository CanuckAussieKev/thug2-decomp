// NOT ORIGINAL - tests of the skinned-mesh writer (thug2_scene.ts), the ped profile additions (ped_models.ts) and a
// port's own rigs (ped_rigs.ts). The game-data one is skipped when THUG2 isn't installed.

import assert from "node:assert/strict";
import * as fs from "node:fs";
import * as path from "node:path";
import { test } from "node:test";
import * as pedModels from "./ped_models.ts";
import * as pedRigs from "./ped_rigs.ts";
import { checksum, Name, Struct } from "./qb.ts";
import * as thug2Scene from "./thug2_scene.ts";

test("skin vertex packing: weights 11/11/10 bits of 1023, normals signed 11/11/10 bits, both exact both ways", () => {
    for (const packed of [0x000003ff, 0x000ff9ff, 0xffffffff, 0x40100401, 0]) {
        assert.equal(thug2Scene.packWeights(thug2Scene.unpackWeights(packed)), packed >>> 0);
        assert.equal(thug2Scene.packNormal(thug2Scene.unpackNormal(packed)), packed >>> 0);
    }
    assert.deepEqual(thug2Scene.unpackWeights(0x000003ff), [1, 0, 0]);
    assert.equal(thug2Scene.packWeights([0.5, 0.5, 0]), 0x00100200); // (511.5 rounds to 512)
    // (+1 on each axis: 1023, 1023, 511)
    assert.equal(thug2Scene.packNormal([1, 1, 1]), (1023 | (1023 << 11) | (511 << 22)) >>> 0);
    assert.deepEqual(thug2Scene.unpackNormal(thug2Scene.packNormal([0, -1, 0])), [0, -1, 0]);
});

test("skinnedMesh: the shipped files' layout; read back the same; the vertex shader is the weights used", () => {
    const vertex = (w: number[], bones: number[]): thug2Scene.SkinVertex => ({
        position: [0, 65.4375, 5.125],
        weights: w,
        bones,
        normal: [0, 0, 1],
        color: [128, 128, 128, 128],
        uvs: [0.5, 0.25],
    });
    const vertices = [
        vertex([1, 0, 0], [30, 0, 0, 0]),
        vertex([0.5, 0.5, 0], [2, 3, 0, 0]),
        vertex([1, 0, 0], [4, 0, 0, 0]),
    ];
    const mesh = thug2Scene.skinnedMesh({
        sphere: [0, 65, 5, 1],
        bbox: [0, 65, 5, 0, 66, 6],
        flags: 0,
        material: 0x1234,
        vertices,
        uvSets: 1,
        strip: [0, 1, 2],
        pixelShader: 3,
    });
    assert.equal(mesh.stride, 40);
    assert.deepEqual(mesh.offsets, [24, 28, 32]);
    assert.deepEqual(mesh.vertexShader, [2, 0]);
    assert.deepEqual(mesh.lods, [[0, 1, 2]]);
    const data = mesh.buffers[0]![1];
    assert.equal(data.length, 120);
    assert.equal(data.readUInt16LE(16), 90); // (bone 30 as the shader's row 90)
    const back = thug2Scene.readSkinVertices(mesh);
    assert.deepEqual(
        back.map((v) => v.bones),
        vertices.map((v) => v.bones),
    );
    const scene: thug2Scene.Scene = {
        versions: [1, 1, 1],
        materials: [],
        sectors: [
            {
                checksum: 1,
                bone: -1,
                flags: 0x80402057,
                bbox: [0, 0, 0, 1, 1, 1],
                sphere: [0, 0, 0, 1],
                meshes: [mesh],
            },
        ],
        hierarchyCount: 0,
        tail: Buffer.alloc(0),
    };
    const written = thug2Scene.writeScene(scene);
    assert.ok(thug2Scene.writeScene(thug2Scene.rewriteSkin(thug2Scene.readScene(written))).equals(written));
});

test("THUG2's own skin written again from what the reader makes of it: byte-identical", (t) => {
    const file = path.join("C:\\Games\\THUG2\\Game\\Data\\models\\Ped_Male\\Australia", "Ped_M_AU_Head01.skin.xbx");
    if (!fs.existsSync(file)) return t.skip("THUG2 not installed");
    const data = fs.readFileSync(file);
    assert.ok(thug2Scene.writeScene(thug2Scene.rewriteSkin(thug2Scene.readScene(data))).equals(data));
});

test("ped additions: entries at the end of their list, profiles before the name list", () => {
    const cas =
        "ped_m_head = [\n    {\n        desc_id=A \n    } \n] \nped_body = [\n    {\n        desc_id=B \n    } \n] \n";
    const additions: pedModels.Additions = {
        cas: { ped_body: ["{\n    desc_id=THUG1_X\n}"], ped_f_head: ["{ desc_id=THUG1_F }"] },
        profiles: ["THUG1_P = {\n    ped_body={desc_id=THUG1_X}\n}"],
    };
    assert.equal(
        pedModels.casEdit(additions, "cas_ped_m")(cas),
        "ped_m_head = [\n    {\n        desc_id=A \n    } \n] \nped_body = [\n    {\n        desc_id=B \n    } \n" +
            "    {\n        desc_id=THUG1_X\n    }\n] \n",
    );
    assert.throws(() => pedModels.casEdit(additions, "cas_ped_f")(cas), /ped_f_head/);
    assert.equal(
        pedModels.profileEdit(additions)("A = 1 \n__register_checksums__ A\n"),
        "A = 1 \nTHUG1_P = {\n    ped_body={desc_id=THUG1_X}\n}\n__register_checksums__ A\n",
    );
    assert.equal(pedModels.fileOf("ped_F_legs"), "cas_ped_f");
    assert.equal(pedModels.fileOf("ped_m_accessories"), "cas_ped_m");
});

test("fixPedRigs: a port's own profile gets its own rig, ReferenceChecksum with it", () => {
    const names = new Map<number, string>();
    const k = (t: string): Name => {
        names.set(checksum(t), t);
        return new Name(checksum(t));
    };
    const ped = (profile: string, skeleton: string): Struct =>
        new Struct([
            [k("Class"), k("Pedestrian")],
            [k("Type"), k("Ped_From_Profile")],
            [k("SkeletonName"), k(skeleton)],
            [k("AnimName"), k("animload_THPS6_human")],
            [k("profile"), k(profile)],
        ]);
    const array = [
        ped("THUG1_Ped_Bum_01", "THPS6_human"),
        ped("THUG1_random_female_profile_2", "Ped_Female"),
        ped("x", "THPS5_human"),
    ];
    const own = (v: number, node: Struct): pedRigs.Rig | undefined => {
        const text = names.get(v) ?? "";
        if (!text.startsWith("THUG1_")) return undefined;
        const s = node.get("SkeletonName");
        return s instanceof Name && s.value === checksum("Ped_Female") ? pedRigs.THUG1_FEMALE : pedRigs.THUG1_MALE;
    };
    const fix = pedRigs.fixPedRigs(array, names, () => false, own);
    assert.equal(fix.human, 3);
    assert.equal(fix.female, 0); // (THUG2's Ped_Female set isn't what the THUG1 woman needs)
    const text = (s: Struct, key: string): string | undefined => {
        const v = s.get(key);
        return v instanceof Name ? names.get(v.value) : undefined;
    };
    assert.deepEqual(
        array.map((s) => [text(s, "SkeletonName"), text(s, "AnimName"), text(s, "ReferenceChecksum"), text(s, "Type")]),
        [
            ["THPS5_human", "animload_THUG1_THPS5_human", undefined, "Ped_From_Profile"],
            ["Ped_Female", "animload_THUG1_Ped_Female", "THUG1_Ped_Female", "Ped_F_From_Profile"],
            ["THPS6_human", "animload_THPS6_human", undefined, "Ped_From_Profile"],
        ],
    );
    // (and nothing more to change the second time)
    assert.equal(pedRigs.fixPedRigs(array, names, () => false, own).changed.length, 0);
    assert.deepEqual(pedRigs.otherAnims(array, names), []);
});
