// NOT ORIGINAL - tests of tools/game/verify_replay.ts (no game): a demo the game's own writer made (its bytes as
// thug2_format_check writes them - src/tests/format_check_demo.cpp tiny(), the same hex there) read, its seal intact;
// a byte changed in the input, the header or the skater found.

import assert from "node:assert/strict";
import { test } from "node:test";
import { checkSeal, identityLines, parseDemo, profileHash } from "./verify_replay.ts";

// (format_check_demo.cpp kTinyHex: BO, Tony Hawk, 3 frames, one check point, OPTS, the seal)
const TINY_HEX =
    "544855473244454d01000000494e464f7100000002000000424f09000000546f6e79204861776b13000000323032362d3131" +
    "2d31352031323a30303a3030070000006162633132333478563412f0debc9aeeffc000000000000000000003000000010000" +
    "00010000000000000000000000000000000000000000000000000000000000000053455453040000000000000050524f4605" +
    "00000001020300ff4445544c03000000090807494e5054740000000100000003000000010000008080008080808080808080" +
    "8080808080808080808080808080808080808080800100000080800180808080808080808080808080808080808080808080" +
    "808080808080800100000080800280808080808080808080808080808080808080808080808080808080804348454b580000" +
    "000100000001000000030000000000803f00000040000080bf0000803f0000003f000080be00000000000000000000803f00" +
    "0000000000803f00000000010000000000803f0000ca42008095c300000000cdcc4cbe48e17a3f4f50545306000000010000" +
    "0000005345414c8c000000010000009f7cf6ce970dc05eb4a937646d61110f4b2f9e1c9173a7c95c7611c4ca8795cda891aa" +
    "9b06be9cf6e26651bc6ed9d2e9ac4f608570a62adeba3c54e2c0a8679e5802000001000000d7143dbbb96c17239f2dfc20fa" +
    "4dd7f8f1b837328307b2492e2e0945b75823b55fe40407233c8928610d6f047b75f757d447a0fa9b9172b90b28dfc96cc8e2" +
    "03";

function tiny(): Buffer {
    return Buffer.from(TINY_HEX, "hex");
}

// A chunk's body offset in the file (the first chunk tagged `tag`).
function bodyOf(bytes: Buffer, tag: string): number {
    let at = 12;
    while (at + 8 <= bytes.length) {
        const size = bytes.readUInt32LE(at + 4);
        if (bytes.subarray(at, at + 4).toString("latin1") === tag) return at + 8;
        at += 8 + size;
    }
    throw new Error(`no ${tag}`);
}

test("the game's tiny demo reads back with its identity", () => {
    const demo = parseDemo(tiny());
    assert.equal(demo.level, "BO");
    assert.equal(demo.skater, "Tony Hawk");
    assert.equal(demo.created, "2026-11-15 12:00:00");
    assert.equal(demo.build, "abc1234");
    assert.equal(demo.frames, 3);
    assert.equal(demo.checks.length, 1);
    assert.equal(profileHash(demo).length, 32);
});

test("its seal is intact, with the profile hash the file's skater has", () => {
    const demo = parseDemo(tiny());
    const seal = checkSeal(demo);
    assert.equal(seal.state, "intact");
    assert.equal(seal.text, "intact");
    assert.equal(seal.profileHash, profileHash(demo).toString("hex"));
    assert.ok(identityLines(demo, seal).some((l) => l.includes("Tony Hawk")));
});

test("a changed frame of input is found, and where", () => {
    const bytes = tiny();
    // (INPT: ports, frames, then the first run's count and its 32 bytes - byte 2 of frame 0's data)
    bytes[bodyOf(bytes, "INPT") + 8 + 4 + 2]! ^= 1;
    const seal = checkSeal(parseDemo(bytes));
    assert.equal(seal.state, "changed");
    assert.equal(seal.changedFrom, 0);
    assert.equal(seal.changedTo, 3);
    assert.equal(seal.headerChanged, false);
    assert.match(seal.text, /changed after recording: the input or check points in frames 0 - 2/);
});

test("a changed skater name in the header is found", () => {
    const bytes = tiny();
    // (INFO: "BO", then "Tony Hawk": its last letter)
    bytes[bodyOf(bytes, "INFO") + 4 + 2 + 4 + 8] = "l".charCodeAt(0);
    const demo = parseDemo(bytes);
    assert.equal(demo.skater, "Tony Hawl");
    const seal = checkSeal(demo);
    assert.equal(seal.state, "changed");
    assert.equal(seal.headerChanged, true);
});

test("a changed skater profile is found", () => {
    const bytes = tiny();
    bytes[bodyOf(bytes, "PROF")]! ^= 1;
    const seal = checkSeal(parseDemo(bytes));
    assert.equal(seal.state, "changed");
    assert.equal(seal.profileChanged, true);
});

test("a demo without a seal says so", () => {
    const bytes = tiny();
    const cut = bodyOf(bytes, "SEAL") - 8;
    const seal = checkSeal(parseDemo(bytes.subarray(0, cut)));
    assert.equal(seal.state, "none");
});

test("not a demo is refused", () => {
    assert.throws(() => parseDemo(Buffer.from("NOTADEMO\x01\x00\x00\x00", "latin1")), /not a demo file/);
});
