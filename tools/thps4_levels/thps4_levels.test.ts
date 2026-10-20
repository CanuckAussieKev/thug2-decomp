// NOT ORIGINAL - tests of the THPS4 converter's level names (THUGPRO compatibility), its restart-script helpers and the
// layout migration (no game data).

import assert from "node:assert/strict";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { test } from "node:test";
import * as qb from "../level_port/qb.ts";
import { Tok } from "../level_port/script_port.ts";
import { hex8 } from "../lib/py.ts";
import { pack } from "../lib/struct.ts";
import { KEEP, main as migrate, renames } from "./migrate_layouts.ts";
import {
    addNetNodes,
    COMPAT,
    createdInPlay,
    levelName,
    loadName,
    PORT,
    pedsCarsWanted,
    portChange,
    setCompat,
    setPortChanges,
    skyName,
} from "./thps4_levels.ts";
import { GAME_MODE_NAMES, isEmptyScript, makeSkaterGoto, setupName } from "./thps4_scripts.ts";

test("levels take THPS4's own names; the skate shop stays prefixed, its load script the profile's", () => {
    setCompat("thugpro");
    assert.equal(levelName("Alc"), "alc");
    assert.equal(levelName("SF2"), "sf2");
    assert.equal(levelName("SkateShop"), "thps4_skateshop");
    assert.equal(levelName("Zoo", true), "thps4_zoo");
    assert.equal(loadName("Alc"), "alc");
    assert.equal(loadName("SkateShop"), "practice");
    assert.equal(loadName("SkateShop", true), "thps4_skateshop");
    assert.equal(skyName("Alc_Sky", "Alc"), "alc_sky");
    assert.equal(skyName("Skateshop_Sky", "SkateShop"), "thps4_skateshop_sky");
    assert.equal(setupName("alc"), "alc_ported_setup");
    setCompat("none");
    assert.equal(COMPAT.loadNames.size, 0);
    assert.equal(loadName("SkateShop"), "thps4_skateshop");
    setCompat("thugpro");
});

test("only THUG2's game-mode flag names are kept", () => {
    for (const ok of ["TRG_CTF_Red", "TRG_CTF_Yellow_Base", "TRG_Flag_Blue", "trg_flag_green"]) {
        assert.ok(GAME_MODE_NAMES.test(ok), ok);
    }
    for (const no of ["TRG_Flag_Restart", "TRG_CTF_Restart_Red", "TRG_CTF_Red_Base2", "Flag_Red"]) {
        assert.ok(!GAME_MODE_NAMES.test(no), no);
    }
});

const name = (text: string): Tok => new Tok(0x16, pack("<I", qb.checksum(text)), 0);

test("MakeSkaterGoto keeps DropIn / StartSkating1 only", () => {
    const dropIn = [name("MakeSkaterGoto"), name("DropIn")];
    assert.equal(makeSkaterGoto(dropIn), dropIn);
    assert.ok(makeSkaterGoto([name("MakeSkaterGoto"), name("StartSkating1")]));
    assert.equal(makeSkaterGoto([name("MakeSkaterGoto"), name("FreezeSkater")]), null);
    assert.equal(makeSkaterGoto([name("MakeSkaterGoto"), name("DropIn"), name("x")]), null);
});

test("isEmptyScript: a script with only its name", () => {
    const header = Buffer.concat([Buffer.from([0x23, 0x16]), pack("<I", 1), Buffer.from([0x01])]);
    assert.ok(isEmptyScript(Buffer.concat([header, Buffer.from([0x01, 0x24])])));
    const call = Buffer.concat([Buffer.from([0x16]), pack("<I", 2), Buffer.from([0x01])]);
    assert.ok(!isEmptyScript(Buffer.concat([header, call, Buffer.from([0x24])])));
});

test("migrate_layouts renames a thps4_<lvl> layout to the level's new scene", () => {
    assert.deepEqual(renames(["Alc", "SkateShop"]), [["thps4_alc", "alc"]]);
    const game = fs.mkdtempSync(path.join(os.tmpdir(), "thps4-layouts-"));
    try {
        const folder = path.join(game, "thug2-decomp-layouts");
        fs.mkdirSync(folder);
        const oldScene = qb.checksum("thps4_zoo");
        const newScene = qb.checksum("zoo");
        const oldFile = path.join(folder, `${hex8(oldScene)}.txt`);
        fs.writeFileSync(oldFile, `scene ${hex8(oldScene)}  # thps4_zoo\nobject 3 1.5 2 3\n`);
        const write = process.stdout.write;
        process.stdout.write = () => true;
        try {
            assert.equal(migrate(["--game", game]), 0);
        } finally {
            process.stdout.write = write;
        }
        assert.equal(
            fs.readFileSync(path.join(folder, `${hex8(newScene)}.txt`), "utf-8"),
            `scene ${hex8(newScene)}  # zoo\nobject 3 1.5 2 3\n`,
        );
        assert.ok(fs.existsSync(oldFile + KEEP));
    } finally {
        fs.rmSync(game, { recursive: true });
    }
});

test("TP-21: the profile's crown and spawns added in our form; --compat none adds nothing", () => {
    const names = new Map<number, string>([
        [qb.checksum("Class"), "Class"],
        [qb.checksum("Restart"), "Restart"],
    ]);
    const restart = (): qb.Struct =>
        new qb.Struct([
            [new qb.Name(qb.checksum("Name")), new qb.Name(qb.checksum("TRG_SS_SkaterDropIn"))],
            [new qb.Name(qb.checksum("Class")), new qb.Name(qb.checksum("Restart"))],
            [new qb.Name(qb.checksum("restart_types")), [new qb.Name(qb.checksum("Generic"))]],
        ]);
    setCompat("none");
    const plain = [restart()];
    assert.equal(addNetNodes("SkateShop", "thps4_skateshop", plain, names, []), "");
    assert.equal(plain.length, 1);
    setCompat("thugpro");
    const net = COMPAT.netNodes.get("skateshop")!;
    const converted = [restart()];
    const script = addNetNodes("SkateShop", "thps4_skateshop", converted, names, []);
    assert.match(script, /^script thps4_skateshop_net_dropin \{\n {4}MakeSkaterGoto DropIn/);
    assert.equal(converted.length, 1 + net.restarts.length + net.crowns.length);
    const types = converted[0]!.get("restart_types") as qb.Name[];
    assert.deepEqual(
        types.map((t) => t.value),
        ["Generic", "Multiplayer", "Horse"].map(qb.checksum),
    );
    assert.equal(addNetNodes("Alc", "alc", [], names, []), "");
});

test("port changes: every entry has id / op / target / why, read whatever the profile; --no-port-changes none", () => {
    setCompat("none");
    setPortChanges(true);
    const ids = PORT.changes.map((e) => e.id);
    assert.deepEqual(
        ids,
        Array.from({ length: ids.length }, (_, i) => `PC-THPS4-${i + 1}`),
    );
    for (const e of PORT.changes) assert.ok(["add", "set", "remove"].includes(e.op) && e.why && e.rule, e.id);
    assert.ok(portChange("open_walls", "SkateShop") && portChange("open_walls", "skateshop"));
    assert.ok(!portChange("open_walls", "Alc"));
    assert.ok(portChange("zero_angles", "Alc")); // (levels "*")
    assert.ok(!pedsCarsWanted("Sch") && pedsCarsWanted("Alc"));
    setPortChanges(false);
    try {
        // (--no-port-changes: only the required ones - the crash guards)
        assert.ok(PORT.changes.length > 0 && PORT.changes.every((e) => e.required === true));
        assert.ok(!pedsCarsWanted("Sch"));
        assert.ok(!portChange("open_walls", "SkateShop"));
        const text = "{Name=SSO_Fence05 Class=LevelGeometry}";
        assert.equal(createdInPlay(text, "SkateShop", []), text);
    } finally {
        setPortChanges(true);
        setCompat("thugpro");
    }
    assert.match(createdInPlay("{Name=SSO_Fence05 Class=LevelGeometry}", "SkateShop", []), /CreatedAtStart/);
    // (a "set" on a node the level hasn't: an error, not an add)
    assert.throws(() => createdInPlay("{Name=Other Class=LevelGeometry}", "SkateShop", []), /PC-THPS4-3 sets SSO_Fence05/);
});
