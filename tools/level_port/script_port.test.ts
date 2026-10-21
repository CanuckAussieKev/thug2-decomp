// NOT ORIGINAL - tests of the script translator's knowledge of THUG2 (tools/level_port/script_port.ts): the member
// table (thug2_members.ts), TriggerType's translation, and which commands a THUG2 script needs of its object.

import assert from "node:assert/strict";
import { test } from "node:test";
import { pack } from "../lib/struct.ts";
import { checksum } from "./qb.ts";
import * as sp from "./script_port.ts";

test("member table: the members THUG2 handles by name, and its tables", () => {
    const [members, table] = sp.Thug2.memberTable();
    const who = (name: string): readonly string[] | undefined => members.get(checksum(name));
    assert.deepEqual(who("TriggerType"), ["trigger"]);
    for (const name of ["StartGap", "EndGap", "DuplicateTrigger", "StartGapTrick", "CheckGapTricks", "ClearGapTricks"])
        assert.deepEqual(who(name), ["skatergap"], name);
    assert.deepEqual(who("Obj_LockToObject"), ["lockobj"]);
    assert.deepEqual(who("Obj_AdjustSound"), ["sound"]);
    assert.deepEqual(who("Obj_StopSound"), ["sound"]);
    assert.deepEqual(who("Obj_ObjectInRadius"), [sp.BASE]);
    assert.deepEqual(who("Obj_AllowSkitching"), ["skitch"]);
    assert.deepEqual(who("SparksOff"), ["skater"]);
    assert.deepEqual([...(who("Obj_MoveToNode") ?? [])].sort(), ["motion", "skatercorephysics"]);
    assert.ok(who("Die")?.includes("object"));
    // (a table member: Obj_PlaySound, the sound component's)
    assert.deepEqual(who("Obj_PlaySound"), ["sound"]);
    assert.ok(table.has(checksum("Obj_PlaySound")) && !table.has(checksum("TriggerType")));
    assert.equal(table.size, 483);
    assert.equal(members.get(checksum("AnimLoaded"))?.length, 0); // (registered; no converted object's)
});

test("contexts: the skater has its whole set, every name a THUG2 component's", () => {
    const known = new Set([...sp.COMPONENT_NAMES, "skater"]);
    for (const [context, set] of Object.entries(sp.SAFE_COMPONENTS))
        for (const c of set) assert.ok(known.has(c), `${context}: ${c}`);
    for (const c of ["trigger", "skatergap", "skaterscore", "trick", "sound", "lockobj", "animblender", "skater"])
        assert.ok(sp.SAFE_COMPONENTS[sp.SKATER].has(c), c);
    for (const c of ["lockobj", sp.BASE]) assert.ok(sp.SAFE_COMPONENTS[sp.OBJECT].has(c), c);
});

const tok = (t: number, p: Buffer = Buffer.alloc(0)): sp.Tok => new sp.Tok(t, p, 0);
const name = (n: string): sp.Tok => sp.nameTok(checksum(n), 0);
const ints = (toks: readonly sp.Tok[]): (number | string)[] =>
    toks.slice(1).map((t) => (t.t === 0x17 ? t.p.readInt32LE(0) : t.t === 0x05 ? "[" : t.t === 0x06 ? "]" : "?"));

test("triggerType: the TRIGGER_* names, THAW's flags, a parameter", () => {
    // THPS3 / THPS4 / THUG1
    const one = sp.triggerType([name("TriggerType"), name("TRIGGER_LIP_ON")]);
    assert.ok(Array.isArray(one));
    assert.deepEqual(ints(one), ["[", "[", 7, 3, "]", "]"]);
    const list = sp.triggerType([
        name("TriggerType"),
        tok(0x05),
        name("TRIGGER_JUMP_OFF"),
        name("TRIGGER_SKATE_OFF_EDGE"),
        tok(0x06),
    ]);
    assert.ok(Array.isArray(list));
    assert.deepEqual(ints(list), ["[", "[", 2, 4, "]", "[", 5, "]", "]"]);
    // THAW: GetTriggerArray's order and three numbers
    const flags = sp.triggerType([name("TriggerType"), name("land"), name("onto")]);
    assert.ok(Array.isArray(flags));
    assert.deepEqual(ints(flags), ["[", 3, 8, -1, "]"]);
    assert.deepEqual(sp.triggerFlags(["Lip", "On"]), [3, 7, -1]);
    assert.deepEqual(sp.triggerFlags(["through"]), [-1, -1, -1]);
    assert.deepEqual(sp.triggerFlags(["walk", "skate", "jump", "onto"]), [14, 1, 2]);
    assert.equal(
        sp.triggerType([name("TriggerType"), name("Smack"), name("onto")]),
        "TriggerType: no THUG2 trigger event: Smack",
    );
    // <trigger_array>: kept
    const param = [name("TriggerType"), tok(0x2d), name("trigger_array")];
    assert.deepEqual(sp.triggerType(param), param);
    // anything else (THUG2's TriggerType reads through a null without an array)
    assert.equal(sp.triggerType([name("TriggerType")]), "TriggerType: form");
    assert.equal(sp.triggerType([name("TriggerType"), name("Something")]), "TriggerType: form");
});

// A THUG2 script body as [token, payload] pairs.
const t2 = (...items: (string | number)[]): [number, Buffer][] =>
    items.map((x) =>
        typeof x === "string"
            ? [0x16, pack("<I", checksum(x))]
            : x === 0x47 || x === 0x48
              ? [x, pack("<H", 0)]
              : [x, Buffer.alloc(0)],
    );
const EOL = 0x01;

test("scriptNeeds: commands only, GotParam branches only when passed", () => {
    const thug2 = Object.create(sp.Thug2.prototype) as sp.Thug2;
    const [members] = sp.Thug2.memberTable();
    Object.assign(thug2, {
        members,
        scripts: new Set([checksum("Breaks"), checksum("Helper")]),
        uses: new Map(),
        needs: new Map(),
    });
    // script Breaks { Kill name=<name>  skate=1  if GotParam Vibrate  Vibrate ... endif  if NOT GotParam Quiet
    //   Obj_PlaySound glass  endif  Helper <...> }
    thug2.uses.set(
        checksum("Breaks"),
        sp.Thug2.commandsOf(
            t2(
                ...["Kill", "name", 0x07, 0x2d, "name", EOL],
                ...["skate", 0x07, 0x17, EOL],
                ...[0x47, "GotParam", "Vibrate", EOL, "Vibrate", "actuator", EOL, 0x28, EOL],
                ...[0x47, 0x39, "GotParam", "Quiet", EOL, "Obj_PlaySound", "glass", EOL, 0x28, EOL],
                ...["Helper", 0x2c, EOL],
            ),
        ),
    );
    // script Helper { if GotParam Lock  Obj_LockToObject ... endif  Skater:SparksOff }
    thug2.uses.set(
        checksum("Helper"),
        sp.Thug2.commandsOf(
            t2(
                ...[0x47, "GotParam", "Lock", EOL, "Obj_LockToObject", EOL, 0x28, EOL],
                ...["Skater", 0x42, "SparksOff", EOL],
            ),
        ),
    );
    const needs = (passed: string[] | null): string[] =>
        [...thug2.scriptNeeds(checksum("Breaks"), passed === null ? null : passed.map(checksum))].sort();
    // (`skate` is a parameter, Kill a C function, SparksOff another object's; vibration only with Vibrate)
    assert.deepEqual(needs([]), ["sound"]);
    assert.deepEqual(needs(["Vibrate"]), ["sound", "vibration"]);
    // (<...> hands the caller's parameters on: Helper's Lock branch with Lock)
    assert.deepEqual(needs(["Lock"]), ["lockobj", "sound"]);
    // (not known: every branch counts)
    assert.deepEqual(needs(null), ["lockobj", "sound", "vibration"]);
});

test("hidesShown: Invisible / Kill prefix= of a set kept shown, not name= or other sets", () => {
    const names = new Map<number, string>([
        [checksum("prefix"), "prefix"],
        [checksum("name"), "name"],
    ]);
    const tr = Object.create(sp.Translator.prototype) as sp.Translator;
    Object.assign(tr, { names, shownPrefixes: ["skware_light"], leftOut: new Map() });
    const str = (s: string): sp.Tok => new sp.Tok(0x1b, Buffer.from(s + "\0", "latin1"), 0);
    const prefix = (s: string): sp.Arg[] => [[checksum("prefix"), str(s)]];
    assert.equal(tr.hidesShown(prefix("SkWare_Light")), true);
    assert.equal(tr.hidesShown(prefix("SkWare_Light_Wall")), true); // (a part of the set)
    assert.equal(tr.hidesShown(prefix("SkWare")), true); // (covers the set)
    assert.equal(tr.hidesShown(prefix("SkWare_Dark")), false);
    const pane = new sp.Tok(0x16, Buffer.from(pack("<I", checksum("SkWare_Light_Glass01"))), 0);
    assert.equal(tr.hidesShown([[checksum("name"), pane]]), false); // (a glass pane's Kill before its Shatter)
    assert.equal(tr.leftOut.get("hides sectors kept shown: skware_light"), 3);
    Object.assign(tr, { shownPrefixes: [] });
    assert.equal(tr.hidesShown(prefix("SkWare_Light")), false);
});
