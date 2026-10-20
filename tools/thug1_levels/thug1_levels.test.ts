// NOT ORIGINAL - tests of the THUG1 converter's level naming (no game data): THUG1's own names unless THUG2 uses them,
// the compatibility profile's choices (TP-30), --legacy-names, the skies.

import assert from "node:assert/strict";
import { test } from "node:test";
import { checksum, Name, Struct } from "../level_port/qb.ts";
import { COMPAT, levelName, ownName, setCompat, skyName } from "./thug1_levels.ts";

const struct = (level: string, load: string): Struct =>
    new Struct([
        [new Name(checksum("level")), level],
        [new Name(checksum("load_script")), new Name(checksum(load))],
    ]);

test("ownName: the level= of a structure loaded by Load_<level>", () => {
    assert.equal(ownName(struct("NJ", "Load_NJ")), "nj");
    assert.equal(ownName(struct("hn", "Load_hn")), "hn");
    assert.equal(ownName(struct("mainmenu", "Load_SkateShop")), null); // (THUG1's front end shop)
    assert.equal(ownName(undefined), null);
});

test("levelName: THUG1's own name, thug1_ when THUG2 has it, the profile's choice, legacy", () => {
    const thug2 = new Set(["Load_SE", "Load_SC", "Load_TestLevel"].map((s) => checksum(s)));
    setCompat("thugpro");
    assert.equal(COMPAT.levelNames.get("se"), "hh");
    assert.equal(levelName("NJ", struct("NJ", "Load_NJ"), thug2), "nj");
    assert.equal(levelName("SC", struct("SC", "Load_SC"), thug2), "thug1_sc");
    assert.equal(levelName("SE", struct("SE", "Load_SE"), thug2), "hh");
    assert.equal(levelName("NJ_Skateshop", undefined, thug2), "thug1_nj_skateshop");
    assert.equal(levelName("NJ", struct("NJ", "Load_NJ"), thug2, true), "thug1_nj");
    setCompat("none");
    assert.equal(levelName("SE", struct("SE", "Load_SE"), thug2), "thug1_se");
    setCompat("thugpro");
});

test("skyName follows the level's name", () => {
    assert.equal(skyName("SE", "hh", "SE_Sky"), "hh_sky");
    assert.equal(skyName("NJ", "nj", "NJ_Sky"), "nj_sky");
    assert.equal(skyName("SC", "thug1_sc", "SC_Sky"), "thug1_sc_sky");
    assert.equal(skyName("NJ_Skateshop", "thug1_nj_skateshop", "NJ_Sky"), "thug1_nj_sky");
});
