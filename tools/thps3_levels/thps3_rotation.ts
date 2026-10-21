// THPS3's Obj_RotX / Obj_RotY / Obj_RotZ `deceleration` in THUG2's terms (a statement rewrite for script_port's
// Translator, THPS3 only).
//
// Both games turn an object about one axis with the same parameters - angle (degrees, the target turned to from where
// it is now), speed, acceleration, deceleration (degrees per second / per second), FLAG_MAX_COORDS (X negated, Y and Z
// swapped) - and the same per-axis state (target, turned so far, speed wanted, speed now, acceleration, deceleration;
// THPS3.exe 0x0048C4F0 / its update 0x0048ED80, THUG2PM.exe 0x00424B90 RotX, 0x00424D50 RotY, 0x00424F50 RotZ /
// update 0x00425A80). They differ in deceleration only:
// - THPS3: a drag, read on its own. Every frame speed += acceleration (up to the speed wanted), then speed -=
//   deceleration; the turn ends at the angle, or when the speed comes down to 0.
// - THUG2: "slow the current spin down to `speed`". Given deceleration it doesn't read acceleration, and the speed now
//   isn't set from `speed` (an angle resets it to 0); every frame speed -= deceleration until it reaches `speed`. All
//   signs are first made the angle's, so from 0 the speed runs away from `speed` - the turn goes the wrong way, ever
//   faster, and never reaches its angle (Obj_WaitRotate never returns). THUG2's own scripts never pass deceleration
//   with an angle; THPS4's pass it only without one (`speed=0 deceleration=10`: spin down) - THUG2's meaning.
// Foundry's molten-metal bucket (CJR_Foun_Bucket_MoveOut: angle=90 speed=30 acceleration=75 deceleration=20) and its
// intro bucket, Foundry's soaker chain and Tokyo's smasher (JS_Mid_Smasher_Controller) pass it.
//
// So THPS3's statement is rewritten without deceleration, to what THPS3 does:
// - with acceleration a > deceleration d: speeds up at a - d to `speed` (THPS3's drag then holds it d * frame time
//   below `speed`: a third of a degree per second, ignored) -> acceleration = a - d;
// - with a <= d: THPS3 stops the turn on its first frame -> `angle=0` (THUG2's: stop turning about that axis);
// - without acceleration: starts at `speed` and slows at d to a stop -> the same angle (or the shorter one where it
//   stops: speed^2 / 2d) in the same time at a constant speed, its average.
// Statements without deceleration, or with a value that isn't a plain number, are kept as they are.

import { checksum } from "../level_port/qb.ts";
import { Tok } from "../level_port/script_port.ts";
import { pack } from "../lib/struct.ts";

export const ROTATIONS = ["Obj_RotX", "Obj_RotY", "Obj_RotZ"] as const;
const ROT_X = checksum("Obj_RotX");
const ANGLE = checksum("angle");
const SPEED = checksum("speed");
const ACCELERATION = checksum("acceleration");
const DECELERATION = checksum("deceleration");
const FLAG_MAX_COORDS = checksum("FLAG_MAX_COORDS");
const VALUES = new Set([ANGLE, SPEED, ACCELERATION, DECELERATION]);

const EOL = [0x01, 0x02];

// The number a value's tokens make (an int or float, maybe after a minus), and the index after it; null otherwise.
function numberAt(toks: readonly Tok[], i: number): [number, number] | null {
    let sign = 1;
    if (toks[i]?.t === 0x0a) {
        sign = -1;
        i += 1;
    }
    const t = toks[i];
    if (t === undefined) return null;
    if (t.t === 0x17 || t.t === 0x18) return [sign * t.p.readInt32LE(0), i + 1];
    if (t.t === 0x1a) return [sign * t.p.readFloatLE(0), i + 1];
    return null;
}

function param(key: number, value: number, i: number): Tok[] {
    return [new Tok(0x16, pack("<I", key), i), new Tok(0x07, Buffer.alloc(0), i), new Tok(0x1a, pack("<f", value), i)];
}

// THUG2's statement for THPS3's `Obj_RotX / Y / Z ...` (its tokens), or the statement itself.
export function thps3Rotation(toks: Tok[]): Tok[] {
    const values = new Map<number, number>();
    const rest: Tok[] = []; // (the flags and any other parameters, kept)
    let i = 1;
    while (i < toks.length) {
        const t = toks[i]!;
        if (t.t === 0x16 && toks[i + 1]?.t === 0x07 && VALUES.has(t.name()!)) {
            const read = numberAt(toks, i + 2);
            if (read === null) return toks;
            values.set(t.name()!, read[0]);
            i = read[1];
            continue;
        }
        if (!EOL.includes(t.t)) rest.push(t);
        i += 1;
    }
    const decel = values.get(DECELERATION);
    if (decel === undefined) return toks;
    const head = toks[0]!;
    const at = head.i;
    const d = Math.abs(decel);
    const speed = Math.abs(values.get(SPEED) ?? 0);
    const accel = values.get(ACCELERATION);
    const angle = values.get(ANGLE);
    const stop = [head, ...param(ANGLE, 0, at)];
    const out = (kept: Map<number, number>): Tok[] => [
        head,
        ...rest,
        ...[...kept].flatMap(([key, value]) => param(key, value, at)),
    ];
    const withoutDecel = new Map([...values].filter(([key]) => key !== DECELERATION));
    if (angle === 0) return toks; // (both games: stop turning about that axis)
    if (d === 0) return out(withoutDecel); // (THPS3's: no drag)
    if (accel !== undefined) {
        if (Math.abs(accel) <= d) return stop;
        const kept = withoutDecel;
        kept.set(ACCELERATION, Math.abs(accel) - d);
        if (angle === undefined) kept.set(SPEED, speed); // (THPS3's signs: the speeds' own made positive)
        return out(kept);
    }
    if (speed === 0) return stop;
    // From `speed`, slowing at d: stops after speed / d seconds, speed^2 / 2d degrees on.
    const stops = (speed * speed) / (2 * d);
    let turn: number;
    let time: number;
    if (angle !== undefined && Math.abs(angle) < stops) {
        turn = Math.abs(angle);
        time = (speed - Math.sqrt(speed * speed - 2 * d * turn)) / d;
    } else {
        turn = stops;
        time = speed / d;
    }
    // The direction: the angle's; without one THPS3 turns the positive way about its axis (FLAG_MAX_COORDS: X negated).
    const flagged = rest.some((t) => t.t === 0x16 && t.name() === FLAG_MAX_COORDS);
    const sign = angle !== undefined ? Math.sign(angle) : flagged && head.name() === ROT_X ? -1 : 1;
    return out(
        new Map([
            [ANGLE, sign * turn],
            [SPEED, turn / time],
        ]),
    );
}

export const ROTATION_REWRITES: ReadonlyMap<number, (toks: Tok[]) => Tok[] | null> = new Map(
    ROTATIONS.map((name) => [checksum(name), thps3Rotation] as const),
);
