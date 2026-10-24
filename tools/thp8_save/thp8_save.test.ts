// NOT ORIGINAL - tests of tools/thp8_save on synthetic saves (no game data, no user save): the structure format's
// types and name tables, the header's checksums, the writer's integer forms, the flag words, the tree diff.

import assert from "node:assert/strict";
import { test } from "node:test";
import { checksum } from "../level_port/qb.ts";
import { getFlag, MAX_CHANGES, setFlag } from "./max.ts";
import {
    crc32,
    FILE_SIZE,
    integerType,
    type NameTables,
    PAD_BYTE,
    readSave,
    type SaveFile,
    T_ARRAY,
    T_FLOAT,
    T_INT,
    T_NAME,
    T_REF,
    T_STRING,
    T_STRUCT,
    T_U8,
    T_U16,
    T_WIDE,
    T_ZERO_INT,
    type Value,
    verifyChecksums,
    writeSave,
} from "./save.ts";
import { diffSaves, formatStruct } from "./thp8_save.ts";

const TABLES: NameTables = { short: [checksum("cas_body"), checksum("nodeflags")], medium: [] };

// A save's bytes, laid out by hand as THP8 writes them.
function handMade(): Buffer {
    const le = (n: number): number[] => [n & 0xff, (n >>> 8) & 0xff, (n >>> 16) & 0xff, (n >>> 24) & 0xff];
    const name = (s: string): number[] => le(checksum(s));
    const wide = (s: string): number[] => [...[...s].flatMap((c) => [0, c.charCodeAt(0)]), 0, 0];
    const f32 = (x: number): number[] => {
        const b = Buffer.alloc(4);
        b.writeFloatLE(x);
        return [...b];
    };
    const summary = [
        T_U8,
        ...name("careermissionscomplete"),
        5,
        T_STRING,
        ...name("filename"),
        ...Buffer.from("Skater"),
        0,
        0,
    ];
    const main = [
        T_STRUCT,
        ...name("career"),
        // (0x80: the name is entry 1 of the 8-bit table - nodeflags; an array of 2 names)
        0x80 | T_ARRAY,
        1,
        T_NAME,
        2,
        0,
        ...name("a"),
        ...name("b"),
        T_ZERO_INT,
        ...name("prosbeaten"),
        T_U16,
        ...name("goalpoints"),
        0x34,
        0x12,
        T_INT,
        ...name("big"),
        ...le(0x12345678),
        T_FLOAT,
        ...name("mainvolume"),
        ...f32(1),
        T_WIDE,
        ...name("ui_string"),
        ...wide("No Way"),
        T_REF,
        0,
        0,
        0,
        0,
        ...name("DefaultGrabs"),
        0,
        0,
    ];
    const used = 0x14 + summary.length + main.length;
    const out = Buffer.alloc(FILE_SIZE, PAD_BYTE);
    out.fill(0, 0, used);
    out.writeUInt32BE(summary.length, 8);
    out.writeUInt32BE(used, 12);
    out.writeUInt32BE(0x47, 16);
    Buffer.from(summary).copy(out, 0x14);
    Buffer.from(main).copy(out, 0x14 + summary.length);
    out.writeUInt32BE(crc32(out.subarray(0x14, 0x14 + summary.length)), 4);
    out.writeUInt32BE(crc32(out.subarray(0, used)), 0);
    return out;
}

test("crc32 is Neversoft's checksum (no final inversion)", () => {
    assert.equal(crc32(Buffer.from("skater")), checksum("skater"));
});

test("a hand-made save reads, checks and writes back byte for byte", () => {
    const data = handMade();
    assert.deepEqual(
        verifyChecksums(data).map(([, ok]) => ok),
        [true, true, true],
    );
    const save = readSave(data, TABLES);
    assert.equal(save.version, 0x47);
    const career = save.main.components[0]!;
    assert.equal(career.name, checksum("career"));
    assert.equal(career.value.kind, "struct");
    const [flags, pros, points, big, volume, text, ref] = (
        career.value as { value: { components: { name: number; value: Value; index?: number }[] } }
    ).value.components;
    assert.equal(flags!.name, checksum("nodeflags"));
    assert.equal(flags!.index, 1);
    assert.deepEqual(flags!.value, {
        kind: "array",
        elementType: T_NAME,
        value: [
            { kind: "name", value: checksum("a") },
            { kind: "name", value: checksum("b") },
        ],
    });
    assert.deepEqual(pros!.value, { kind: "int", value: 0 });
    assert.deepEqual(points!.value, { kind: "int", value: 0x1234 });
    assert.deepEqual(big!.value, { kind: "int", value: 0x12345678 });
    assert.deepEqual(volume!.value, { kind: "float", value: 1 });
    assert.deepEqual(text!.value, { kind: "wide", value: "No Way" });
    assert.equal(ref!.name, 0);
    assert.deepEqual(ref!.value, { kind: "ref", value: checksum("DefaultGrabs") });
    assert.ok(writeSave(save).equals(data));
});

test("a changed value is written in the form its new value needs, checksums recomputed", () => {
    const save = readSave(handMade(), TABLES);
    const career = save.main.components[0]!.value as { value: { components: { type: number; value: Value }[] } };
    const pros = career.value.components[1]!;
    pros.value = { kind: "int", value: 11 };
    const out = writeSave(save);
    assert.deepEqual(
        verifyChecksums(out).map(([, ok]) => ok),
        [true, true, true],
    );
    const back = readSave(out, TABLES);
    const backPros = (back.main.components[0]!.value as { value: { components: { value: Value }[] } }).value
        .components[1]!;
    assert.deepEqual(backPros.value, { kind: "int", value: 11 });
    assert.equal(out.readUInt32BE(12), handMade().readUInt32BE(12) + 1); // (0x12 zero -> 0x10 + a byte)
});

test("integers take the writer's smallest form", () => {
    assert.equal(integerType(0), T_ZERO_INT);
    assert.equal(integerType(200), T_U8);
    assert.equal(integerType(9999), T_U16);
    assert.equal(integerType(-5), 0x0e);
    assert.equal(integerType(-300), 0x0f);
    assert.equal(integerType(100000), T_INT);
});

test("global flag n is bit n & 31 of word n >> 5", () => {
    const words: Value[] = Array.from({ length: 32 }, () => ({ kind: "name", value: 0 }) as Value);
    assert.equal(setFlag(words, 115), true);
    assert.equal(setFlag(words, 115), false);
    assert.equal(setFlag(words, 127), true);
    assert.deepEqual(words[3], { kind: "name", value: 0x80080000 });
    assert.equal(getFlag(words, 115), true);
    assert.equal(getFlag(words, 116), false);
    assert.throws(() => setFlag(words, 1024));
});

test("the tree diff names what changed; max's allowlist takes only its own fields", () => {
    const a = readSave(handMade(), TABLES);
    const b: SaveFile = readSave(handMade(), TABLES);
    const career = b.main.components[0]!.value as { value: { components: { value: Value }[] } };
    career.value.components[2]!.value = { kind: "int", value: 2393 };
    const names = new Map([
        [checksum("career"), "career"],
        [checksum("goalpoints"), "goalpoints"],
    ]);
    const lines = diffSaves(a, b, names);
    assert.deepEqual(lines, ["main.career.goalpoints: 4660 -> 2393"]);
    assert.ok(MAX_CHANGES.some((re) => re.test("main.globalinfo.career.goalpoints: 53 -> 2393")));
    assert.ok(!MAX_CHANGES.some((re) => re.test("main.globalinfo.options.screenbrightness: 5 -> 10")));
    assert.ok(formatStruct(a.summary, new Map([[checksum("filename"), "filename"]])).includes('filename = "Skater"'));
});
