// NOT ORIGINAL - a script file's trigger data (trigger_data.ts): what the engine's walks over a TriggerScript's code
// found (nodes.cpp for_each_script_call, editor_camera.cpp's flag walk), made from synthetic QB and written to the data
// file's "triggers" (qb_data.ts). The game's own files against the game's walks: the script check's `declare` lines
// (docs/script-check.md).
import assert from "node:assert/strict";
import { readdirSync, readFileSync } from "node:fs";
import { test } from "node:test";
import { emitData } from "./emit_data.ts";
import { command, field, integer, name, param, script, word } from "./fixtures.ts";
import { member, nameChecksum, type QbDataValue, readQbData } from "./qb_data.ts";
import { randomChoice } from "./random-fixtures.ts";
import { readIR } from "./reader.ts";
import {
    ADDED_C_FUNCTIONS,
    CAMERA_FLAG,
    DECLARED_FUNCTIONS,
    type Source,
    TriggerData,
    triggerScripts,
    triggersValue,
} from "./trigger_data.ts";

const text = (s: string) => [0x1b, ...word(s.length + 1), ...Array.from(s, (c) => c.charCodeAt(0)), 0];
const source = (bytes: number[], file = "test.qb"): Source => {
    const b = Uint8Array.from([...bytes, 0]);
    return { bytes: b, ir: readIR(b, file) };
};
const n = nameChecksum;
// A declaration's calls to `fn`, each call's members as [name, value] (names by checksum).
const calls = (data: TriggerData, module: Source, scriptName: string, fn: string): QbDataValue[] =>
    data
        .declarations(module)
        .find(([s]) => s === n(scriptName))?.[1]
        .calls.get(fn) ?? [];
const int = (x: number): QbDataValue => ({ type: 1, bits: x >>> 0 });
const nm = (s: string): QbDataValue => ({ type: 13, bits: n(s) });
const str = (s: string): QbDataValue => ({ type: 3, bytes: Array.from(s, (c) => c.charCodeAt(0)) });
const struct = (...members: [string, QbDataValue][]): QbDataValue => ({
    type: 10,
    members: members.map(([k, v]) => [n(k), v]),
});

// The boot scripts the level's reach: the kill scripts as sk4_scripts has them.
const boot = source([
    ...script("SK3_KillSkater", [...command("SK3_Killskater_Finish", [0x2c])]),
    ...script("SK3_Killskater_Finish", [...command("KillSkater", [...field("node", param("nodename"))])]),
]);

test("a gap through a helper: the helper's arguments passed on, its EndGap's evaluated with them", () => {
    const module = source([
        ...script("Trg_Gap", [
            ...command("Gap_Helper", [...field("id", name("Gap1")), ...field("score", integer(250))]),
        ]),
        ...script("Gap_Helper", [
            ...command("EndGap", [
                ...field("GapID", param("id")),
                ...field("score", param("score")),
                ...field("text", text("Big Gap")),
            ]),
        ]),
    ]);
    const data = new TriggerData([boot], new Set([n("Trg_Gap")]));
    assert.deepEqual(calls(data, module, "Trg_Gap", "EndGap"), [
        struct(["GapID", nm("Gap1")], ["score", int(250)], ["text", str("Big Gap")]),
    ]);
    // (only the TriggerScripts are declared)
    assert.deepEqual(
        data.declarations(module).map(([s]) => s),
        [n("Trg_Gap")],
    );
});

test("calls in statement order, StartGap and EndGap apart; EndGap's gapscript followed for EndGap only", () => {
    const module = source([
        ...script("Trg", [
            ...command("StartGap", [...field("GapID", name("A"))]),
            ...command("EndGap", [...field("GapID", name("A")), ...field("gapscript", name("Gs"))]),
            ...command("EndGap", [...field("GapID", name("B"))]),
        ]),
        ...script("Gs", [
            ...command("EndGap", [...field("GapID", name("C"))]),
            ...command("StartGap", [...field("GapID", name("D"))]),
        ]),
    ]);
    const data = new TriggerData([], new Set([n("Trg")]));
    const ids = (fn: string) =>
        calls(data, module, "Trg", fn).map((c) => (member(c, "GapID") as { bits: number }).bits);
    assert.deepEqual(ids("EndGap"), [n("A"), n("C"), n("B")]);
    assert.deepEqual(ids("StartGap"), [n("A")]);
});

test("a kill through the boot scripts, and the editor camera kept out by it", () => {
    const module = source([
        ...script("Trg_Kill", [...command("SK3_KillSkater", [...field("nodename", name("Restart1"))])]),
        ...script("Trg_Plain", [...command("Wait", integer(1))]),
    ]);
    const data = new TriggerData([boot], new Set([n("Trg_Kill"), n("Trg_Plain")]));
    const declared = new Map(data.declarations(module));
    const kill = declared.get(n("Trg_Kill"))!;
    assert.deepEqual(kill.calls.get("SK3_KillSkater"), [struct(["nodename", nm("Restart1")])]);
    // (the <...> passed on: KillSkater's node= is the trigger's nodename)
    assert.deepEqual(kill.calls.get("KillSkater"), [struct(["node", nm("Restart1")])]);
    assert.equal(kill.keepsCameraOut, true);
    // (a script that declares nothing has no trigger data)
    assert.equal(declared.has(n("Trg_Plain")), false);
});

test("a Random among a called script's arguments: the declaration doesn't depend on it - and one that does is reported", () => {
    const module = source([
        ...script("Trg", [
            ...command("Helper", [
                ...field("id", name("Gap1")),
                ...field("pick", randomChoice(0x2f, [integer(1), integer(2)])),
            ]),
        ]),
        ...script("Helper", [...command("EndGap", [...field("GapID", param("id")), ...field("score", integer(100))])]),
        ...script("Trg_Random", [
            ...command("EndGap", [
                ...field("GapID", name("G")),
                ...field("score", randomChoice(0x2f, [integer(1), integer(2)])),
            ]),
        ]),
    ]);
    const data = new TriggerData([], new Set([n("Trg"), n("Trg_Random")]));
    assert.deepEqual(calls(data, module, "Trg", "EndGap"), [struct(["GapID", nm("Gap1")], ["score", int(100)])]);
    assert.deepEqual(data.randomDependent, ["test.qb: " + n("Trg_Random").toString(16).padStart(8, "0") + " (EndGap)"]);
});

test("SetEventHandler's scr= followed; a C function's arguments never read as calls, nor a C function at a statement's start found; ShatterAndDie's calls without arguments", () => {
    const module = source([
        ...script("Trg", [
            ...command("SetEventHandler", [...field("Ex", name("x")), ...field("scr", name("Handler"))]),
            ...command("SpawnScript", name("Other")),
            ...command("Shatter", [...field("area", integer(500))]),
            ...command("ShatterAndDie", [...field("area", integer(500))]),
        ]),
        ...script("Handler", [...command("TeleportSkaterToNode", [...field("nodename", name("Exit"))])]),
        ...script("Other", [...command("EndGap", [...field("GapID", name("Never"))])]),
    ]);
    const data = new TriggerData([], new Set([n("Trg")]));
    // (TeleportSkaterToNode isn't defined here: a name nobody knows - the walk passes over it)
    assert.deepEqual(calls(data, module, "Trg", "EndGap"), []);
    // (Shatter is a C function: the loader made the statement a direct call, which the walk compares with
    // SetEventHandler only - the original's walk never finds it there)
    assert.deepEqual(calls(data, module, "Trg", "Shatter"), []);
    const withShatterScript = source([
        ...script("Trg", [...command("ShatterAndDie", [...field("area", integer(500))])]),
        ...script("ShatterAndDie", [...command("Shatter", [0x2c])]),
    ]);
    assert.deepEqual(calls(new TriggerData([], new Set([n("Trg")])), withShatterScript, "Trg", "ShatterAndDie"), [
        struct(),
    ]);
    const withHandlerScript = source([
        ...script("Trg", [...command("SetEventHandler", [...field("scr", name("Handler"))])]),
        ...script("Handler", [...command("Teleport", [...field("nodename", name("Exit"))])]),
        ...script("Teleport", [...command("EndGap", [...field("GapID", param("nodename"))])]),
    ]);
    assert.deepEqual(calls(new TriggerData([], new Set([n("Trg")])), withHandlerScript, "Trg", "EndGap"), [
        struct(["GapID", nm("Exit")]),
    ]);
});

test("five levels deep, no further", () => {
    const chain = (i: number) => script(`S${i}`, [...command(i === 7 ? "EndGap" : `S${i + 1}`)]);
    const module = source([...[0, 1, 2, 3, 4, 5, 6, 7].flatMap(chain)]);
    const reach = (top: number) =>
        calls(new TriggerData([], new Set([n(`S${top}`)])), module, `S${top}`, "EndGap").length;
    assert.equal(reach(2), 1); // S2 -> ... -> S7 (depth 5) calls EndGap
    assert.equal(reach(1), 0); // S7 at depth 6: not walked
});

test("the TriggerScripts a module's node arrays name", () => {
    const module = source([
        ...name("Lvl_NodeArray"),
        7,
        5,
        3,
        ...field("Name", name("Trg1")),
        ...field("TriggerScript", name("Trg_Gap")),
        4,
        1,
        3,
        ...field("Name", name("Rail")),
        4,
        6,
        1,
    ]);
    assert.deepEqual([...triggerScripts(module.ir)], [n("Trg_Gap")]);
});

test("the data file's triggers: written, read back, and a module of trigger data alone has a data file", () => {
    const module = source([
        ...script("Trg", [...command("EndGap", [...field("GapID", name("A")), ...field("score", integer(5))])]),
        ...script("SK3_KillSkater", [...command("Wait", integer(1))]),
    ]);
    const data = new TriggerData([], new Set([n("Trg"), n("SK3_KillSkater")]));
    const triggers = triggersValue(data.declarations(module));
    const json = emitData(module.ir, "12345678", triggers, (x) => data.nameText(x));
    assert.ok(json, "a data file");
    assert.match(json, /"triggers": \{/);
    assert.match(json, /"calls": \{"EndGap": \[\{"_0x[0-9a-f]{8}": "_0x[0-9a-f]{8}", "_0x[0-9a-f]{8}": 5\}\]\}/);
    assert.match(json, new RegExp(`"${CAMERA_FLAG}": true`));
    const read = readQbData(json);
    assert.deepEqual(read.globals, []);
    assert.deepEqual(read.triggers, triggers);
});

test("the C functions this project adds: the list is the sources' registrations not in THUG2's table", async () => {
    const { CFUNC_NAMES } = await import("thug2-level-converter-core/level_port/thug2_cfuncs.ts");
    const thug2 = new Set(CFUNC_NAMES.map((x) => x.toLowerCase()));
    const src = new URL("../../src/", import.meta.url);
    const files = (readdirSync(src, { recursive: true }) as string[]).filter((f) => f.endsWith(".cpp"));
    const registered = new Set<string>();
    for (const f of files) {
        const textOf = readFileSync(new URL(f.replace(/\\/g, "/"), src), "utf8");
        for (const [, x] of textOf.matchAll(/\{\s*\.name = "([A-Za-z0-9_]+)",\s*\.function/g)) registered.add(x!);
    }
    const added = [...registered].filter((x) => !thug2.has(x.toLowerCase())).sort();
    assert.deepEqual(added, [...ADDED_C_FUNCTIONS].sort());
});

test("the declared functions are the engine's (declared_calls.cpp), in its order", () => {
    const cpp = readFileSync(new URL("../../src/engine/script/declared_calls.cpp", import.meta.url), "utf8");
    const listed = [...cpp.matchAll(/checksums::hash_from_text\("([A-Za-z0-9_]+)"\),/g)].map((m) => m[1]);
    assert.deepEqual(listed, [...DECLARED_FUNCTIONS]);
});
