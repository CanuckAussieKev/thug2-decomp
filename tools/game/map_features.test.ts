// NOT ORIGINAL - tests of tools/pipeline/game/map_features.ts (the DLL's rail speed-up and proximity scripts, ported) and of the
// feature map's layout records in tools/pipeline/game/layout_file.ts. The expected bytes are the DLL self-test's
// (src/hook/selftests_editor_map1.cpp): NeverScript's output for the same statements.

import assert from "node:assert/strict";
import { test } from "node:test";
import { checksum, tokens } from "../level_port/qb.ts";
import { putScripts } from "../level_port/qb_write.ts";
import { LayoutError, readLayout, readProximRecord } from "./layout_file.ts";
import {
    proximBody,
    proximScriptName,
    RAIL_SPEED_BODY,
    RAIL_SPEED_LOOP_BODY,
    RAIL_SPEED_QB,
    speedBody,
    speedScriptName,
} from "./map_features.ts";
import { body, readTriggerRecord } from "./triggers.ts";

const hex = (text: string): Buffer => Buffer.from(text.replace(/\s+/g, ""), "hex");

test("rail speed-up scripts: the DLL's image, its bodies, a point's script", () => {
    assert.equal(RAIL_SPEED_QB.length, 0x10f);
    assert.equal(RAIL_SPEED_QB.readUInt32LE(2), checksum("ed_RailSpeed"));
    // (the bodies: after the header line, up to endscript - an if, and a while ... loop)
    assert.equal(RAIL_SPEED_BODY[0], 0x47);
    assert.deepEqual([...RAIL_SPEED_BODY.subarray(-2)], [0x28, 0x01]);
    assert.equal(RAIL_SPEED_QB[0x62], 0x23);
    assert.equal(RAIL_SPEED_QB.readUInt32LE(0x64), checksum("ed_RailSpeedLoop"));
    assert.equal(RAIL_SPEED_LOOP_BODY[0], 0x16);
    assert.deepEqual([...RAIL_SPEED_LOOP_BODY.subarray(-2)], [0x21, 0x01]);
    assert.deepEqual(
        speedBody(checksum("ed_orig"), 0.05),
        hex("16 eb29e2c1 2c 01 16 7d9ec1f6 16 c6c01320 07 1a cdcc4c3d 01"),
    );
    assert.deepEqual(speedBody(0, 0.05), hex("16 7d9ec1f6 16 c6c01320 07 1a cdcc4c3d 01"));
    assert.equal(speedScriptName(0xabcdef), "ed_railspeed_00ABCDEF");
    // (baked into a scripts file: it tokenises to its end)
    const file = putScripts(
        Buffer.from([0x00]),
        [
            [checksum("ed_RailSpeed"), RAIL_SPEED_BODY],
            [checksum("ed_RailSpeedLoop"), RAIL_SPEED_LOOP_BODY],
        ],
        new Map([[checksum("ed_RailSpeed"), "ed_RailSpeed"]]),
    );
    const all = [...tokens(file, true)];
    assert.equal(all.filter((t) => t[1] === 0x23).length, 2);
    assert.equal(all.at(-1)![1], 0x00);
});

test("proximity scripts: if Inside {enter} else {exit}, as NeverScript compiles it", () => {
    const enter = checksum("ed_enter");
    const exit = checksum("ed_exit");
    const own = checksum("ed_orig");
    assert.deepEqual(
        proximBody(enter, exit, own),
        hex("47 1100 16 4b0d0145 01 16 ee6ba5ff 01 48 0a00 01 16 870d7627 01 28 01"),
    );
    assert.deepEqual(
        proximBody(enter, 0, own),
        hex("47 1100 16 4b0d0145 01 16 ee6ba5ff 01 48 0b00 01 16 eb29e2c1 2c 01 28 01"),
    );
    assert.deepEqual(proximBody(enter, 0, 0), hex("47 1100 16 4b0d0145 01 16 ee6ba5ff 01 48 0400 01 28 01"));
    assert.deepEqual(
        proximBody(0, exit, own),
        hex("47 1200 16 4b0d0145 01 16 eb29e2c1 2c 01 48 0a00 01 16 870d7627 01 28 01"),
    );
    assert.equal(proximBody(0, 0, own).length, 0);
    assert.equal(proximScriptName(0xbeef), "ed_proxim_0000BEEF");
});

test("boost / break triggers: the DLL's bodies", () => {
    const boost = readTriggerRecord("ed_trigger01 boost pos=0,0,0 strength=900");
    assert.ok(boost);
    assert.deepEqual(body(boost), hex("16 f083bbb8 16 0901d9f0 07 17 84030000 01"));
    const glass = readTriggerRecord(
        "ed_trigger02 break pos=0,0,0 target=ed_sector sound=HitGlassPane2x replaces=12345678",
    );
    assert.ok(glass);
    assert.equal(glass.replaces, 0x12345678);
    assert.deepEqual(
        body(glass),
        hex(
            "16 b1b25151 16 c5a4a2b0 01 16 4841c2ee 16 f981dca1 07 16 ee605f74 16 97c26b28 07 17 88130000 16 d99d30bd 07 1a " +
                "0000803f 16 875f4a83 07 1a 0000c03f 16 01c132e6 07 17 03000000 16 a60bc37d 07 17 00000000 16 7bdab913 07 1a " +
                "cdcccc3e 16 6d908761 07 17 00000000 16 fba08016 07 17 3c000000 16 41f1898f 07 17 00000000 01 16 96a9d6f8 16 " +
                "f981dca1 07 16 ee605f74 01",
        ),
    );
    assert.equal(readTriggerRecord("ed_trigger03 boost pos=0,0,0 strength=50"), null);
    assert.equal(readTriggerRecord("ed_trigger03 boost pos=0,0,0 strength=600 strength=700"), null);
    assert.equal(readTriggerRecord("ed_trigger03 break pos=0,0,0 sound=a-b"), null);
    assert.equal(readTriggerRecord("ed_trigger03 boost pos=0,0,0")?.strength, 900);
});

// (map4) LevelEditor_PlayCamera camera=ed_camera01 time=2.5 - the DLL self-test's bytes (selftests_editor_map4.cpp).
test("camera triggers: the DLL's body, records", () => {
    const shot = readTriggerRecord("ed_trigger05 camera pos=0,0,0 target=ed_camera01 seconds=2.5");
    assert.ok(shot);
    assert.equal(shot.seconds, 2.5);
    assert.deepEqual(body(shot), hex("16 c7654bff 16 fa11e3c4 07 16 bda5ba50 16 ba676b90 07 1a 00002040 01"));
    assert.equal(readTriggerRecord("ed_trigger05 camera pos=0,0,0 target=ed_camera01")?.seconds, 3);
    assert.equal(body(readTriggerRecord("ed_trigger05 camera pos=0,0,0")!).length, 0, "no camera: an empty script");
    assert.equal(readTriggerRecord("ed_trigger05 camera pos=0,0,0 seconds=0.1"), null);
    assert.equal(readTriggerRecord("ed_trigger05 camera pos=0,0,0 seconds=61"), null);
    assert.equal(readTriggerRecord("ed_trigger05 camera pos=0,0,0 seconds=2 seconds=3"), null);
});

test("layout records: ledges, speed-ups, proximity nodes - read as the DLL reads them", () => {
    const layout = readLayout(
        [
            "thug2-decomp layout 2",
            "point ed_ledge01 ClimbingNode pos=1,2,3 facing=0 type=Ledge hang=right",
            "point ed_rail01 RailNode pos=1,2,3 speed=0.0625",
            "railspeed 00001234 0.25",
            "proxim 12345678 enter=BO_Library_On exit=BO_Library_Off shape=box size=400,200,300 offset=0,100,-5.5  # TRG_x",
            "proxim ABCDEF01 shape=sphere radius=350",
        ].join("\n"),
    );
    assert.equal(layout.points[0]!.type, "Ledge");
    assert.equal(layout.points[0]!.hang, 2);
    assert.equal(layout.points[1]!.speed, 0.0625);
    assert.deepEqual(layout.railSpeeds, [{ name: 0x1234, rate: 0.25 }]);
    assert.equal(layout.proxims.length, 2);
    assert.equal(layout.proxims[0]!.enter, "BO_Library_On");
    assert.deepEqual(layout.proxims[0]!.offset, { x: 0, y: 100, z: -5.5 });
    assert.equal(layout.proxims[1]!.radius, 350);
    assert.equal(layout.unknown.length, 0);
    const refused = (line: string): void =>
        assert.throws(() => readLayout(`thug2-decomp layout 2\n${line}`), LayoutError, line);
    refused("point ed_rail02 RailNode pos=0,0,0 speed=0.9");
    refused("point ed_ladder02 ClimbingNode pos=0,0,0 speed=0.05");
    refused("point ed_rail02 RailNode pos=0,0,0 type=Ledge");
    refused("point ed_ledge02 ClimbingNode pos=0,0,0 type=Ledge hang=up");
    refused("railspeed 00001234 0.001");
    refused("railspeed 00001234 0.05\nrailspeed 00001234 0.06");
    refused("proxim 12345678 shape=box");
    refused("proxim 12345678 shape=sphere radius=5");
    refused("proxim 12345678 enter=a enter=b");
    refused("proxim 1234 enter=a");
    assert.equal(readProximRecord(["12345678", "exit=x", "later=1"])?.exit, "x");
});
