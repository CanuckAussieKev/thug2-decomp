// NOT ORIGINAL - tests of tools/pipeline/game/triggers.ts (the game's trigger records and scripts, ported) and the qb_write
// helpers the bake uses for them (scripts added / replaced, a level structure's entries set). The script bytes are the
// game's (Boston's compiled gap scripts); with the game's files at
// C:\Games\THUG2\Game they're also compared with Boston's own (skipped without them - nothing is written there).

import assert from "node:assert/strict";
import * as fs from "node:fs";
import { test } from "node:test";
import { checksum, Name, QbFile, Struct, tokens } from "../level_port/qb.ts";
import { addNames, encodeValue, putScripts, scriptSpans, setStructEntries } from "../level_port/qb_write.ts";
import { readCol, writeCol } from "../level_port/thug2_col.ts";
import { pack } from "../lib/struct.ts";
import {
    BOX_FACES,
    body,
    boxCorners,
    checksumOf,
    clampSize,
    cleanText,
    FACE_FLAGS,
    partner,
    problem,
    qbFile,
    readTriggerRecord,
    scriptNames,
    type TriggerSpec,
    triggerCollision,
    triggerNode,
} from "./triggers.ts";

const hex = (text: string): Buffer => Buffer.from(text.replace(/\s+/g, ""), "hex");

function spec(over: Partial<TriggerSpec>): TriggerSpec {
    return {
        name: "ed_trigger01",
        kind: "kill",
        pos: { x: 0, y: 0, z: 0 },
        yaw: 0,
        size: { x: 400, y: 200, z: 400 },
        water: false,
        target: "",
        script: "",
        gap: "",
        flags: [],
        text: "",
        score: 100,
        replaces: 0,
        exit: false,
        strength: 900,
        sound: "",
        seconds: 3,
        ...over,
    };
}

const START = spec({ kind: "gapstart", gap: "StateHouseSpine1", flags: ["CANCEL_GROUND"] });
const END = spec({
    name: "ed_trigger02",
    kind: "gapend",
    gap: "StateHouseSpine2",
    score: 250,
    text: "Statehouse Stair Spine",
});

test("triggers: the compiled scripts are the game's bytes (Boston's gap scripts)", () => {
    assert.equal(checksum("SK3_KillSkater"), 0xdea3057b);
    assert.equal(checksum("StartGap"), 0x65b9ec39);
    assert.deepEqual(
        body(START),
        hex("16 39 ec b9 65 16 26 2e 44 3b 07 16 2b 6a 6d 00 16 45 be fa f4 07 05 16 4d e7 27 9a 06 01"),
    );
    assert.deepEqual(
        body(END),
        hex(
            "16 b2 9f 39 e5 16 26 2e 44 3b 07 16 91 3b 64 99 16 ae c8 66 cd 07 17 fa 00 00 00 16 38 58 74 " +
                "c4 07 1b 17 00 00 00 53 74 61 74 65 68 6f 75 73 65 20 53 74 61 69 72 20 53 70 69 6e 65 00 01",
        ),
    );
    const slap = { ...START, gap: "HighSlap", flags: ["CANCEL_RAIL", "CANCEL_MANUAL", "CANCEL_GROUND"] };
    assert.deepEqual(
        body(slap),
        hex(
            "16 39 ec b9 65 16 26 2e 44 3b 07 16 2c 87 ea 2f 16 45 be fa f4 07 05 16 ee ec 4d cd 09 16 e1 " +
                "da 03 2d 09 16 4d e7 27 9a 06 01",
        ),
    );
    assert.deepEqual(body({ ...START, flags: [] }), hex("16 39 ec b9 65 16 26 2e 44 3b 07 16 2b 6a 6d 00 01"));
    const kill = spec({ target: "TRG_Restart01" });
    assert.deepEqual(
        body(kill),
        Buffer.concat([hex("16 7b 05 a3 de 16 78 ba 92 9f 07 16"), pack("<I", checksum("TRG_Restart01")), hex("01")]),
    );
    assert.equal(body({ ...kill, water: true })[1], checksum("SK3_KillSkater_Water") & 0xff);
    const tp = body({ ...kill, kind: "teleport" });
    assert.equal(tp.readUInt32LE(1), checksum("TeleportSkaterToNode"));
    assert.deepEqual(body(spec({ kind: "script", script: "0x1234ABCD" })), hex("16 cd ab 34 12 01"));
    assert.equal(checksumOf("0x1234ABCD"), 0x1234abcd);
    assert.equal(checksumOf(""), 0);
});

test("triggers: what lacks something makes an empty script (Problem)", () => {
    for (const s of [
        spec({}),
        spec({ kind: "teleport" }),
        spec({ kind: "script" }),
        spec({ kind: "gapstart" }),
        { ...END, text: "" },
        { ...END, score: 0 },
    ]) {
        assert.ok(problem(s));
        assert.equal(body(s).length, 0);
    }
    assert.equal(problem(START), null);
    assert.equal(problem(END), null);
});

test("triggers: the game's .qb image - scripts, names, the end", () => {
    const missing = spec({ name: "ed_trigger04" });
    const file = qbFile([START, END, spec({ name: "ed_trigger03", target: "TRG_Restart01" }), missing]);
    assert.deepEqual(
        file.subarray(0, 7),
        Buffer.concat([hex("23 16"), pack("<I", checksum("ed_trigger01Script")), hex("01")]),
    );
    const startBody = body(START);
    assert.deepEqual(file.subarray(7, 7 + startBody.length), startBody);
    assert.deepEqual(file.subarray(7 + startBody.length, 9 + startBody.length), hex("24 01"));
    const toks = [...tokens(file, true)];
    assert.equal(toks.filter(([, t]) => t === 0x23).length, 4);
    assert.equal(toks[toks.length - 1]![1], 0x00);
    assert.equal(toks[toks.length - 1]![0], file.length - 1);
    const q = new QbFile(file, "triggers.qb", true);
    assert.equal(q.names.get(checksum("StateHouseSpine1")), "StateHouseSpine1");
    assert.equal(q.names.get(checksum("ed_trigger03Script")), "ed_trigger03Script");
    assert.equal(q.names.get(checksum("ed_trigger04Script")), "ed_trigger04Script");
    assert.equal(q.names.has(checksum("CANCEL_GROUND")), false, "the game's image: no flag names");
    assert.equal(scriptNames(START, true).get(checksum("CANCEL_GROUND")), "CANCEL_GROUND");
});

test("triggers: records read as the game reads them", () => {
    const s = readTriggerRecord(
        'ed_trigger07 gapend pos=1.5,-2,3e2 yaw=1.57079637 size=310.5,64,1200.25 gap=ed_gap02 flags=PURE_AIR,CANCEL_GROUND score=250 replaces=8E1A2B3C text="My #gap"',
    );
    assert.ok(s);
    assert.deepEqual(s.pos, { x: 1.5, y: -2, z: 300 });
    assert.equal(s.yaw, Math.fround(1.57079637));
    assert.deepEqual(s.flags, ["PURE_AIR", "CANCEL_GROUND"]);
    assert.equal(s.score, 250);
    assert.equal(s.replaces, 0x8e1a2b3c);
    assert.equal(s.text, "My gap");
    const d = readTriggerRecord("ed_t kill pos=0,0,0 later=7");
    assert.ok(d, "a later version's key is skipped");
    assert.deepEqual(d.size, { x: 400, y: 200, z: 400 }, "the default size");
    assert.equal(d.score, 100);
    assert.deepEqual(readTriggerRecord("ed_t kill pos=0,0,0 size=1,99999,20")!.size, { x: 10, y: 50000, z: 20 });
    for (const bad of [
        "ed_t Kill pos=0,0,0",
        "ed_t other pos=0,0,0",
        "bad-name kill pos=0,0,0",
        "ed_t kill pos=0,0",
        "ed_t kill pos=0,0,0 yaw=x",
        "ed_t kill pos=0,0,0 target=a.b",
        "ed_t kill pos=0,0,0 flags=",
        "ed_t kill pos=0,0,0 flags=A,",
        "ed_t kill pos=0,0,0 score=1000001",
        "ed_t kill pos=0,0,0 replaces=123456789",
        "ed_t kill pos=0,0,0 =1",
        "ed_t kill pos=0,0,0 yaw=0 yaw=1",
        'ed_t gapend pos=0,0,0 text="open',
        "ed_t kill",
    ]) {
        assert.equal(readTriggerRecord(bad), null, bad);
    }
    assert.equal(cleanText(` a"b#c\x01 ${"x".repeat(80)}`).length, 59, "60 kept, then the leading space trimmed");
    assert.equal(cleanText(' a"b#c '), "a'bc");
    assert.deepEqual(clampSize({ x: Number.NaN, y: 5, z: 60000 }), { x: 10, y: 10, z: 50000 });
    assert.equal(partner([START, END], 0), -1, "different GapIDs");
    assert.equal(partner([START, { ...END, gap: "statehousespine1" }], 0), 1, "case doesn't matter");
});

test("triggers: the node and the box", () => {
    const s = spec({ name: "ed_trigger09", target: "TRG_Restart01", pos: { x: 100, y: 50, z: -20 }, yaw: Math.PI / 2 });
    const node = triggerNode(s);
    assert.deepEqual(
        node.entries.map(([k]) => k!.value),
        ["Pos", "Angles", "Name", "Class", "Type", "TriggerScript"].map(checksum),
    );
    assert.equal((node.get("Class") as Name).value, checksum("GenericNode"));
    assert.equal((node.get("Type") as Name).value, checksum("ed_trigger"));
    assert.equal((node.get("TriggerScript") as Name).value, checksum("ed_trigger09Script"));
    const o = triggerCollision(s);
    assert.equal(o.checksum, checksum("ed_trigger09"));
    assert.equal(o.verts.length, 8);
    assert.deepEqual(
        o.faces.map((f) => f.slice(2)),
        BOX_FACES.map((f) => [...f]),
    );
    assert.ok(o.faces.every((f) => f[0] === FACE_FLAGS && (f[0] & 0x50) === 0x50 && f[1] === 0));
    // turned a quarter: x and z extents swap
    assert.deepEqual(
        o.bbox.map((v) => Math.round(v)),
        [-100, -50, -220, 1, 300, 150, 180, 1],
    );
    // through the writer and reader: fixed-point corners (as THUG2's own boxes) within a sixteenth
    const back = readCol(writeCol({ objects: [o] }), "col").objects[0]!;
    assert.equal(back.fixedVerts, 1);
    back.verts.forEach((v, i) => {
        for (let k = 0; k < 3; k++) assert.ok(Math.abs(v[k]! - o.verts[i]![k]!) <= 1 / 32);
    });
    assert.deepEqual(back.faces, o.faces);
    const big = triggerCollision({ ...s, size: { x: 50000, y: 10, z: 10 } });
    assert.equal(big.fixedVerts, 0, "too big for sixteenths: float corners");
    assert.equal(readCol(writeCol({ objects: [big] }), "col").objects[0]!.verts[0]![0], big.verts[0]![0]);
});

// The static line test's triangle test (src/game/nx/coll_tests.cpp RayHitsTriangle, 0x0040B420): a determinant below
// 0.03 - the line crossing the face from behind - is no hit. In doubles here (only the sides matter).
function lineHits(start: number[], end: number[], v0: number[], v1: number[], v2: number[]): boolean {
    const d = [end[0]! - start[0]!, end[1]! - start[1]!, end[2]! - start[2]!];
    const e1 = [v1[0]! - v0[0]!, v1[1]! - v0[1]!, v1[2]! - v0[2]!];
    const e2 = [v2[0]! - v0[0]!, v2[1]! - v0[1]!, v2[2]! - v0[2]!];
    const p = [e2[2]! * d[1]! - e2[1]! * d[2]!, e2[0]! * d[2]! - e2[2]! * d[0]!, e2[1]! * d[0]! - e2[0]! * d[1]!];
    const det = e1[2]! * p[2]! + p[0]! * e1[0]! + p[1]! * e1[1]!;
    if (det < 0.03) return false;
    const o = [start[0]! - v0[0]!, start[1]! - v0[1]!, start[2]! - v0[2]!];
    const u = o[2]! * p[2]! + o[1]! * p[1]! + o[0]! * p[0]!;
    if (u < 0 || u > det) return false;
    const q = [o[1]! * e1[2]! - o[2]! * e1[1]!, o[2]! * e1[0]! - o[0]! * e1[2]!, o[0]! * e1[1]! - o[1]! * e1[0]!];
    const v = q[2]! * d[2]! + q[0]! * d[0]! + q[1]! * d[1]!;
    if (v < 0 || u + v > det) return false;
    const along = (q[2]! * e2[2]! + q[1]! * e2[1]! + q[0]! * e2[0]!) / det;
    return along >= 0 && along <= 1;
}

test("triggers: a baked box fires on the way in; with exit=1 on the way out too", () => {
    assert.equal(readTriggerRecord("ed_t kill pos=0,0,0 exit=1")?.exit, true);
    assert.equal(readTriggerRecord("ed_t kill pos=0,0,0 exit=0")?.exit, false);
    assert.equal(readTriggerRecord("ed_t kill pos=0,0,0")?.exit, false);
    assert.equal(readTriggerRecord("ed_t kill pos=0,0,0 exit=yes"), null);
    assert.equal(readTriggerRecord("ed_t kill pos=0,0,0 exit=0 exit=1"), null);
    const s = spec({ name: "ed_t", target: "TRG_Restart01", pos: { x: 0, y: 100, z: 0 }, yaw: 0.3 });
    const hits = (o: ReturnType<typeof triggerCollision>, from: number[], to: number[]): number =>
        o.faces.filter((f) => lineHits(from, to, o.verts[f[2]]!, o.verts[f[3]]!, o.verts[f[4]]!)).length;
    // (off the quads' diagonals: a line through a diagonal meets both its triangles)
    const inside = [11, 103, -17];
    const lines = [
        [7, 90, 1000],
        [1000, 110, 13],
        [-23, 1000, 5],
        [31, -1000, -9],
        [-700, 120, -650],
    ];
    const plain = triggerCollision(s);
    const exit = triggerCollision({ ...s, exit: true });
    assert.equal(exit.faces.length, 24);
    assert.deepEqual(exit.faces.slice(0, 12), plain.faces, "the box's own faces first, as without exit");
    for (const [i, [a, b, c]] of BOX_FACES.entries()) assert.deepEqual(exit.faces[12 + i]!.slice(2), [a, c, b]);
    for (const outside of lines) {
        assert.equal(hits(plain, outside, inside), 1, `in from ${outside}: one face`);
        assert.equal(hits(plain, inside, outside), 0, `out to ${outside}: nothing (one-sided faces)`);
        assert.equal(hits(exit, outside, inside), 1, `exit box, in from ${outside}: one face`);
        assert.equal(hits(exit, inside, outside), 1, `exit box, out to ${outside}: one (a face facing in)`);
    }
    const back = readCol(writeCol({ objects: [exit] }), "col").objects[0]!;
    assert.deepEqual(back.faces, exit.faces);
});

// --- qb_write: scripts and structures ---------------------------------------------------------------------------------

function file(parts: Buffer[], names: string[]): Buffer {
    return Buffer.concat([
        ...parts,
        ...names.map((n) => Buffer.concat([hex("2b"), pack("<I", checksum(n)), Buffer.from(`${n}\0`, "latin1")])),
        hex("00"),
    ]);
}
const script = (name: string, b: Buffer): Buffer =>
    Buffer.concat([hex("01 23 16"), pack("<I", checksum(name)), hex("01"), b, hex("24 01")]);
const global = (name: string, value: Struct): Buffer =>
    Buffer.concat([hex("16"), pack("<I", checksum(name)), hex("07"), encodeValue(value), hex("01")]);

test("qb_write: scripts added before the name table, replaced in place; other bytes kept", () => {
    const one = script("Level_Script", hex("16 11 22 33 44 01"));
    const data = file([one], ["Level_Script"]);
    assert.deepEqual(putScripts(data, []), data, "nothing to add: the same bytes");
    const out = putScripts(data, [[checksum("ed_trigger01Script"), body(START)]], scriptNames(START, true));
    assert.deepEqual(out.subarray(0, one.length), one, "the file's own script keeps its bytes");
    const q = new QbFile(out, "s.qb", true);
    assert.equal(q.scripts.size, 2);
    assert.equal(q.names.get(checksum("ed_trigger01Script")), "ed_trigger01Script");
    assert.equal(q.names.get(checksum("Level_Script")), "Level_Script");
    const span = scriptSpans(out).get(checksum("ed_trigger01Script"))!;
    assert.deepEqual(
        out.subarray(span[0], span[1]),
        Buffer.concat([hex("23 16"), pack("<I", checksum("ed_trigger01Script")), hex("01"), body(START), hex("24")]),
    );
    // baked again with another body: replaced, not added twice; the rest as it was
    const again = putScripts(out, [[checksum("ed_trigger01Script"), body({ ...START, flags: [] })]]);
    assert.equal(new QbFile(again, "s.qb", true).scripts.size, 2);
    assert.deepEqual(again.subarray(0, span[0]), out.subarray(0, span[0]));
    assert.deepEqual(again.subarray(again.length - (out.length - span[1])), out.subarray(span[1]));
    // a file without a name table: before its closing 0x00
    const bare = putScripts(hex("00"), [[checksum("x"), hex("01")]]);
    assert.equal(new QbFile(bare, "s.qb", true).scripts.size, 1);
    assert.equal(bare[bare.length - 1], 0x00);
    assert.deepEqual(addNames(data, new Map([[checksum("Level_Script"), "Level_Script"]])), data);
});

test("qb_write: a level structure's entries set (the last of a repeated key), other bytes kept", () => {
    const before = global("Level_aa", new Struct([[new Name(checksum("level")), "aa"]]));
    const level = new Struct([
        [new Name(checksum("name")), "Old"],
        [new Name(checksum("ambient_red")), 1],
        [null, new Name(checksum("skip_teammate_pro"))],
        [new Name(checksum("ambient_red")), 2],
        [new Name(checksum("sprites")), ["a", "b"]],
    ]);
    const after = global("Level_cc", new Struct([[new Name(checksum("level")), "cc"]]));
    const data = file([before, global("Level_bb", level), after], ["Level_bb", "name"]);
    const out = setStructEntries(
        data,
        checksum("Level_bb"),
        [
            [checksum("name"), "New name"],
            [checksum("ambient_red"), 70],
            [checksum("tod_state"), new Name(checksum("night"))],
        ],
        new Map([
            [checksum("tod_state"), "tod_state"],
            [checksum("night"), "night"],
            [checksum("name"), "name"],
        ]),
    );
    assert.deepEqual(out.subarray(0, before.length), before);
    const q = new QbFile(out, "Levels.qb", true);
    const s = q.globals.find(([k]) => k.value === checksum("Level_bb"))![1] as Struct;
    assert.equal(s.get(checksum("name")), "New name");
    const reds = s.entries.filter(([k]) => k?.value === checksum("ambient_red")).map(([, v]) => v);
    assert.deepEqual(reds, [1, 70], "the last one (the one the game finds) set in place");
    assert.equal((s.entries[s.entries.length - 1]![1] as Name).value, checksum("night"), "a new key at the end");
    assert.deepEqual(s.get(checksum("sprites")), ["a", "b"]);
    assert.equal(q.names.get(checksum("night")), "night");
    const cc = q.globals.find(([k]) => k.value === checksum("Level_cc"))![1] as Struct;
    assert.equal(cc.get(checksum("level")), "cc");
    assert.throws(() => setStructEntries(data, checksum("Level_zz"), [[checksum("name"), "x"]]));
});

// --- with the game's files (read only; skipped without them) -----------------------------------------------------------

const BO = "C:\\Games\\THUG2\\Game\\Data\\levels\\BO";

test("triggers: as Boston's own compiled gap script and trigger box", { skip: !fs.existsSync(BO) }, () => {
    const scripts = fs.readFileSync(`${BO}\\BO_Scripts.qb`);
    const q = new QbFile(scripts, "BO_Scripts.qb", true);
    // BO_Gap_StateHouseSpine1: StartGap (StateHouseSpine1, CANCEL_GROUND), then EndGap (StateHouseSpine2, 250, ...)
    const span = scriptSpans(scripts).get(checksum("BO_Gap_StateHouseSpine1"));
    assert.ok(span && q.scripts.has(checksum("BO_Gap_StateHouseSpine1")));
    const own = scripts.subarray(span[0] + 7, span[1] - 1); // (after 23 16 <name> 01, before 24)
    assert.deepEqual(own, Buffer.concat([body(START), body(END)]));
    // TRGP_ShoppingCart's box: the same corners in the same order, the same faces and flags
    const col = readCol(fs.readFileSync(`${BO}\\BO.col.xbx`), "BO.col.xbx");
    const cart = col.objects.find((o) => o.checksum === checksum("TRGP_ShoppingCart"))!;
    const [x0, y0, z0, , x1, y1, z1] = cart.bbox as number[];
    const corners = boxCorners({
        pos: { x: (x0! + x1!) / 2, y: (y0! + y1!) / 2, z: (z0! + z1!) / 2 },
        yaw: 0,
        size: { x: x1! - x0!, y: y1! - y0!, z: z1! - z0! },
    });
    assert.deepEqual(
        corners.map((c) => [c.x, c.y, c.z]),
        cart.verts,
    );
    assert.deepEqual(
        cart.faces,
        BOX_FACES.map(([a, b, c]) => [FACE_FLAGS, 0, a, b, c]),
    );
});
