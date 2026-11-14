import assert from "node:assert/strict";
import { test } from "node:test";
import type { Stats, Usage } from "./classes.ts";
import type { Rgba } from "./classical.ts";
import {
    classifyCharacter,
    hintOf,
    isCharacterDictionary,
    SKIN_GARMENT,
    SKIN_TEXELS,
    skinShare,
    skinTexel,
    strandsAlongU,
} from "./models.ts";

const base: Usage = { base: true, overlay: false, environment: false, shiny: false, decal: false, transparent: false };
const stats = (over: Partial<Stats> = {}): Stats => ({
    width: 64,
    height: 64,
    cutout: 0,
    translucent: 0,
    meanLuma: 0.5,
    saturation: 0.3,
    hue: 20,
    detail: 0.02,
    colours: 200,
    streaks: 0.1,
    ...over,
});
const solid = (r: number, g: number, b: number, size = 16): Rgba => {
    const data = new Uint8Array(size * size * 4);
    for (let i = 0; i < size * size; i++) data.set([r, g, b, 255], i * 4);
    return { width: size, height: size, data };
};

test("the characters' dictionaries: skaters, peds, cutscenes, animals - not arrows, menus or shadows", () => {
    assert.ok(isCharacterDictionary("models/skater_male/hands.tex.xbx"));
    assert.ok(isCharacterDictionary("models/Skater_Pro/Skater_Hawk.tex.xbx"));
    assert.ok(isCharacterDictionary("models/peds/ped_jester/ped_jester.tex.xbx"));
    assert.ok(isCharacterDictionary("models/ped_male/boston/x.tex.xbx"));
    assert.ok(isCharacterDictionary("models/cutscenes/hawk_head.tex.xbx"));
    assert.ok(!isCharacterDictionary("models/arrow/arrow.tex.xbx"));
    assert.ok(!isCharacterDictionary("models/ped_shadow/ped_shadow.tex.xbx"));
    assert.ok(!isCharacterDictionary("levels/bo/bo.tex.xbx"));
    assert.ok(!isCharacterDictionary("models/skater_male/hands.skin.xbx"));
});

test("the CAS parts' names hint at what they are", () => {
    assert.equal(hintOf("models/skater_male/hair_m_long2.tex.xbx"), "hair");
    assert.equal(hintOf("models/skater_male/hair_m_buzzhat.tex.xbx"), "hair");
    assert.equal(hintOf("models/skater_male/hands.tex.xbx"), "skin");
    assert.equal(hintOf("models/skater_male/face_male_01.tex.xbx"), "skin");
    assert.equal(hintOf("models/skater_female/pant_leather.tex.xbx"), "leather");
    assert.equal(hintOf("models/skater_female/pant_baggy.tex.xbx"), "legwear");
    assert.equal(hintOf("models/skater_male/shirt_jacket_jean.tex.xbx"), "denim");
    assert.equal(hintOf("models/skater_female/shirt_tshirt.tex.xbx"), "cloth");
    assert.equal(hintOf("models/skater_male/extra_kneepad.tex.xbx"), "rubber");
    assert.equal(hintOf("models/skater_male/extra_watch_l_01.tex.xbx"), "metal");
    assert.equal(hintOf("models/skater_male/extra_ring02_l.tex.xbx"), "metal");
    assert.equal(hintOf("models/skater_male/board_default.tex.xbx"), "wood");
    assert.equal(hintOf("models/skater_male/extra_skeletonarms.tex.xbx"), "none");
    assert.equal(hintOf("models/animals/anl_skater/anl_sk8_goat.tex.xbx"), "fur");
    assert.equal(hintOf("models/skater_pro/skater_hawk.tex.xbx"), "none");
    assert.equal(hintOf("models/cutscenes/hawk_body.tex.xbx"), "none");
});

test("skin's texels: orange-red of any tone; not grey, blue, green or pure red", () => {
    for (const [r, g, b] of [
        [233, 190, 160], // pale
        [198, 134, 100], // medium
        [110, 70, 50], // dark
    ]) {
        assert.ok(skinTexel(r! / 255, g! / 255, b! / 255), `${r} ${g} ${b}`);
    }
    for (const [r, g, b] of [
        [128, 128, 128], // grey
        [60, 80, 160], // denim
        [60, 160, 60], // green
        [200, 20, 20], // pure red (hue 0)
        [20, 15, 10], // near black
    ]) {
        assert.ok(!skinTexel(r! / 255, g! / 255, b! / 255), `${r} ${g} ${b}`);
    }
    assert.equal(skinShare(solid(198, 134, 100)), 1);
    assert.equal(skinShare(solid(60, 80, 160)), 0);
});

test("classes: names first, the skin share deciding skin", () => {
    assert.equal(classifyCharacter(stats(), base, "hair", 1).name, "hair");
    assert.equal(classifyCharacter(stats(), base, "skin", 0.8).name, "skin");
    assert.equal(classifyCharacter(stats({ saturation: 0.05 }), base, "skin", 0).name, "default"); // (eyes)
    assert.equal(classifyCharacter(stats(), base, "none", 0.7).name, "skin");
    assert.equal(classifyCharacter(stats({ hue: 215, saturation: 0.3 }), base, "none", 0).name, "denim");
    assert.equal(classifyCharacter(stats({ hue: 215, saturation: 0.3 }), base, "legwear", 0).name, "denim");
    assert.equal(classifyCharacter(stats({ hue: 0, saturation: 0.05 }), base, "legwear", 0).name, "cloth");
    // a garment is never skin; its skin texels are, when they are part of it
    const brown = classifyCharacter(stats(), base, "cloth", 1);
    assert.equal(brown.name, "cloth");
    assert.ok(!brown.skinTexels);
    const arms = classifyCharacter(stats(), base, "cloth", (SKIN_TEXELS + SKIN_GARMENT) / 2);
    assert.equal(arms.name, "cloth");
    assert.ok(arms.skinTexels);
    assert.equal(classifyCharacter(stats(), { ...base, shiny: true }, "cloth", 0).name, "leather");
    assert.equal(classifyCharacter(stats({ saturation: 0.05 }), { ...base, shiny: true }, "none", 0).name, "metal");
    assert.equal(classifyCharacter(stats(), base, "fur", 1).name, "cloth");
});

test("classes: hair keeps its cut-out strands; other cut-outs are skipped", () => {
    const strands = stats({ cutout: 0.4, translucent: 0.5 });
    assert.equal(classifyCharacter(strands, base, "hair", 0).skip, undefined);
    assert.ok(classifyCharacter(strands, base, "cloth", 0).skip);
    assert.ok(classifyCharacter(stats({ width: 8 }), base, "hair", 0).skip);
    assert.ok(classifyCharacter(stats(), { ...base, base: false, overlay: true }, "skin", 1).skip);
});

test("hair's strands: along v when the luma changes along u, and the other way", () => {
    const size = 16;
    const stripes = (alongU: boolean): Rgba => {
        const data = new Uint8Array(size * size * 4);
        for (let y = 0; y < size; y++) {
            for (let x = 0; x < size; x++) {
                const v = ((alongU ? y : x) & 1) * 200 + 20;
                data.set([v, v, v, 255], (y * size + x) * 4);
            }
        }
        return { width: size, height: size, data };
    };
    assert.equal(strandsAlongU(stripes(true)), true);
    assert.equal(strandsAlongU(stripes(false)), false);
});
