// NOT ORIGINAL - unit tests of THPS3's rotation deceleration rewrite (thps3_rotation.ts; no game data needed).

import assert from "node:assert/strict";
import { test } from "node:test";
import { checksum } from "../level_port/qb.ts";
import { Tok } from "../level_port/script_port.ts";
import { pack } from "../lib/struct.ts";
import { ROTATION_REWRITES, thps3Rotation } from "./thps3_rotation.ts";

const name = (text: string): Tok => new Tok(0x16, pack("<I", checksum(text)), 0);
const eq = (): Tok => new Tok(0x07, Buffer.alloc(0), 0);
const int = (v: number): Tok => new Tok(0x17, pack("<i", v), 0);
const float = (v: number): Tok => new Tok(0x1a, pack("<f", v), 0);
const minus = (): Tok => new Tok(0x0a, Buffer.alloc(0), 0);

// The statement's parameters as {name: number} and its flags.
function read(toks: Tok[]): [Record<string, number>, string[]] {
    const keys = ["angle", "speed", "acceleration", "deceleration", "FLAG_MAX_COORDS"];
    const text = (t: Tok): string => keys.find((k) => checksum(k) === t.name()) ?? String(t.name());
    const values: Record<string, number> = {};
    const flags: string[] = [];
    for (let i = 1; i < toks.length; i++) {
        const t = toks[i]!;
        if (toks[i + 1]?.t === 0x07) {
            let sign = 1;
            let j = i + 2;
            if (toks[j]!.t === 0x0a) {
                sign = -1;
                j++;
            }
            const v = toks[j]!;
            values[text(t)] = sign * (v.t === 0x1a ? v.p.readFloatLE(0) : v.p.readInt32LE(0));
            i = j;
        } else flags.push(text(t));
    }
    return [values, flags];
}

test("Foundry's bucket: acceleration less the deceleration, no deceleration, the rest kept", () => {
    // Obj_RotX Angle=90 Speed=30 Acceleration=75 Deceleration=20 FLAG_MAX_COORDS
    const toks = [
        name("Obj_RotX"),
        name("Angle"),
        eq(),
        int(90),
        name("Speed"),
        eq(),
        int(30),
        name("Acceleration"),
        eq(),
        int(75),
        name("Deceleration"),
        eq(),
        int(20),
        name("FLAG_MAX_COORDS"),
    ];
    const [values, flags] = read(thps3Rotation(toks));
    assert.deepEqual(values, { angle: 90, speed: 30, acceleration: 55 });
    assert.deepEqual(flags, ["FLAG_MAX_COORDS"]);
    // the way back: a negative angle
    toks[3] = minus();
    toks.splice(4, 0, int(90));
    assert.deepEqual(read(thps3Rotation(toks))[0], { angle: -90, speed: 30, acceleration: 55 });
});

test("Tokyo's smasher: no acceleration - the angle (or where it stops) at the average speed", () => {
    // Obj_RotY angle=-20 speed=80 deceleration=200: stops after 0.4 s, 16 degrees on (short of 20)
    const smasher = [
        name("Obj_RotY"),
        name("angle"),
        eq(),
        minus(),
        int(20),
        name("speed"),
        eq(),
        int(80),
        name("deceleration"),
        eq(),
        int(200),
        name("FLAG_MAX_COORDS"),
    ];
    const [values, flags] = read(thps3Rotation(smasher));
    assert.ok(Math.abs(values.angle! + 16) < 1e-4);
    assert.ok(Math.abs(values.speed! - 40) < 1e-4); // 16 degrees in 0.4 s
    assert.equal(values.deceleration, undefined);
    assert.deepEqual(flags, ["FLAG_MAX_COORDS"]);
    // reaches the angle first: 10 degrees from 80 slowing at 200: t = (80 - sqrt(6400 - 4000)) / 200
    const near = [
        name("Obj_RotY"),
        name("angle"),
        eq(),
        float(10),
        name("speed"),
        eq(),
        int(80),
        name("deceleration"),
        eq(),
        int(200),
    ];
    const t = (80 - Math.sqrt(2400)) / 200;
    const [v2] = read(thps3Rotation(near));
    assert.ok(Math.abs(v2.angle! - 10) < 1e-4);
    assert.ok(Math.abs(v2.speed! - 10 / t) < 1e-3);
});

test("kept: no deceleration, a value that isn't a number, angle 0; stopped: acceleration not above deceleration", () => {
    const plain = [name("Obj_RotY"), name("speed"), eq(), minus(), int(150), name("FLAG_MAX_COORDS")];
    assert.equal(thps3Rotation(plain), plain);
    const param = [
        name("Obj_RotX"),
        name("angle"),
        eq(),
        new Tok(0x2d, Buffer.alloc(0), 0),
        name("a"),
        name("deceleration"),
        eq(),
        int(5),
    ];
    assert.equal(thps3Rotation(param), param);
    const zero = [name("Obj_RotX"), name("angle"), eq(), int(0), name("deceleration"), eq(), int(5)];
    assert.equal(thps3Rotation(zero), zero);
    const weak = [
        name("Obj_RotZ"),
        name("angle"),
        eq(),
        int(45),
        name("speed"),
        eq(),
        int(10),
        name("acceleration"),
        eq(),
        int(5),
        name("deceleration"),
        eq(),
        int(5),
    ];
    assert.deepEqual(read(thps3Rotation(weak)), [{ angle: 0 }, []]);
    assert.deepEqual([...ROTATION_REWRITES.keys()], ["Obj_RotX", "Obj_RotY", "Obj_RotZ"].map(checksum));
});
