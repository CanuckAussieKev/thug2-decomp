// NOT ORIGINAL - the runtime's game mode (scripting/runtime.js begin / resume, docs/qb-ts.md "In the game") driven
// the way src/script_ts drives it, with a stand-in for the game's natives: a level's command calls come out as
// requests, results go back in, the record's parameters are refreshed on each resume and the level's own changes
// come back with its answers.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { test } from "node:test";
import { runInNewContext } from "node:vm";
import { checksum } from "../qb_checksum.ts";
import { compileTS } from "./compile_ts.ts";
import { helperSource } from "./helper-sources.ts";

const source = `
import { script, global, cmd, N, G, fn, add, test as truth } from "qb";
const { Probe, Wait, Helper } = cmd;
global("Limit", 3);
script("Main", { start: 1 }, function* (p) {
    p.x = add(p.start, G.Limit);
    if (yield* Probe({ a: p.x })) {
        yield* Wait(1, N.gameframe);
    }
    p.seen = p.fromHost;
    if (truth(fn.IsReady())) p.ready = 1;
    yield* Helper();
    return { result: p.x };
});
`;

function load(recording = false) {
    const calls: string[] = [];
    let frame = 0;
    const scriptsDefined: number[] = [];
    const definitions = new Map<number, number>(); // a script's name -> its definition (what its body names)
    const globals = new Map<number, unknown>();
    const game = {
        symbol: (n: number) =>
            globals.get(n) ?? (scriptsDefined.includes(n) ? { kind: "callable", type: 7, name: n } : undefined),
        symbolKind: (n: number) => (n === checksum("IsReady") ? "function" : undefined),
        callFunction: (n: number) => {
            calls.push(`function ${n.toString(16)}`);
            return true;
        },
        defineScript: (n: number, id: number) => {
            scriptsDefined.push(n);
            definitions.set(n, id);
        },
        defineGlobal: (n: number, v: unknown) => globals.set(n, v),
        draw: () => 0,
        recording,
        frame: () => frame,
    };
    const context = runInNewContext(
        `globalThis.__qbGame = game;\n${helperSource("random")}${helperSource("values")}` +
            readFileSync(new URL("../../scripting/runtime.js", import.meta.url), "utf8") +
            readFileSync(new URL("../../scripting/qb_ts.js", import.meta.url), "utf8") +
            compileTS(source, "game.ts") +
            "\nglobalThis",
        { game },
    ) as { qb: Record<string, (...a: unknown[]) => unknown> };
    return {
        qb: context.qb,
        calls,
        scriptsDefined,
        definitions,
        globals,
        nextFrame: () => {
            ++frame;
        },
    };
}

// (values made in the vm context have its prototypes: compared as data)
const same = (actual: unknown, expected: unknown, message?: string) =>
    assert.deepEqual(actual === undefined ? undefined : JSON.parse(JSON.stringify(actual)), expected, message);

test("game mode: requests out, results and the record's parameters in, the level's changes back", () => {
    const { qb, calls, scriptsDefined, definitions, globals } = load();
    const n = checksum;
    same(scriptsDefined, [n("Main")]);
    same(globals.get(n("Limit")), { type: 1, bits: 3 });

    qb.begin!(7, definitions.get(n("Main")), [[n("start"), { type: 1, bits: 2 }]], 99);
    // first resume: defaults merged with the arguments, x assigned - the level's parameters come back changed
    let r = qb.resume!(7, null, undefined) as Record<string, unknown>;
    assert.equal(r.call, n("Probe"));
    same(r.args, [[n("a"), { type: 1, bits: 5 }]]);
    assert.equal(r.on, undefined);
    same(r.params, [
        [n("start"), { type: 1, bits: 2 }],
        [n("x"), { type: 1, bits: 5 }],
    ]);
    // the interpreter answers true; meanwhile a C function added fromHost to the record's parameters
    const record = [...(r.params as unknown[]), [n("fromHost"), { type: 13, bits: n("hello") }]];
    r = qb.resume!(7, record, true) as Record<string, unknown>;
    assert.equal(r.call, n("Wait"));
    assert.equal(r.params, null, "nothing changed by the level itself");
    r = qb.resume!(7, record, true) as Record<string, unknown>;
    // seen = <fromHost> (refreshed from the record), ready = 1 (IsReady called in the expression)
    assert.equal(r.call, n("Helper"));
    same(calls, [`function ${n("IsReady").toString(16)}`]);
    // after the first answer only the assignments come back
    assert.equal(r.params, undefined);
    same(r.assigned, [
        [n("seen"), { type: 13, bits: n("hello") }],
        [n("ready"), { type: 1, bits: 1 }],
    ]);
    const params = [...record, ...(r.assigned as unknown[])];
    r = qb.resume!(7, params, true) as Record<string, unknown>;
    assert.equal(r.done, true);
    same(r.returned, [[n("result"), { type: 1, bits: 5 }]]);
    assert.throws(() => qb.resume!(7, params, true), /Unknown QB level/);
});

test("game mode, [debug] ts_types=1: the types each site sees, and which calls waited", () => {
    const { qb, nextFrame, definitions } = load(true);
    const n = checksum;
    const hex = (x: number) => x.toString(16).padStart(8, "0");
    qb.begin!(7, definitions.get(n("Main")), [[n("start"), { type: 1, bits: 2 }]], 99);
    let r = qb.resume!(7, null, undefined) as Record<string, unknown>; // -> Probe
    r = qb.resume!(7, r.params, true) as Record<string, unknown>; // Probe answered in the same frame -> Wait
    const record = [...(r.params === null ? [] : (r.params as unknown[]))];
    nextFrame();
    qb.resume!(7, record, true); // Wait answered a frame later -> Helper
    const sites = JSON.parse(qb.recordedTypes!() as string).sites as Record<string, Record<string, number>>;
    same(sites[`wait ${hex(n("Probe"))}`], { "same frame": 1 });
    same(sites[`wait ${hex(n("Wait"))}`], { waited: 1 });
    same(sites[`arg ${hex(n("Probe"))} ${hex(n("a"))}`], { int: 1 });
    same(sites[`arg ${hex(n("Wait"))} #0`], { int: 1 });
    same(sites[`arg ${hex(n("Wait"))} #1`], { name: 1 });
    same(sites[`global ${hex(n("Limit"))}`], { int: 1 });
    same(sites[`op11 game.ts ${hex(n("Main"))} p.${hex(n("start"))} value`], { "int,int": 1 });
    // a later session's counts add to an earlier one's
    qb.loadTypes!(JSON.stringify({ version: 1, sites: { [`wait ${hex(n("Wait"))}`]: { waited: 2 } } }));
    same(JSON.parse(qb.recordedTypes!() as string).sites[`wait ${hex(n("Wait"))}`], { waited: 3 });
});
