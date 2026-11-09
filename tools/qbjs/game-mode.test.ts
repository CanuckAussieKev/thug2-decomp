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

function load() {
    const calls: string[] = [];
    const scriptsDefined: number[] = [];
    const globals = new Map<number, unknown>();
    const game = {
        symbol: (n: number) =>
            globals.get(n) ?? (scriptsDefined.includes(n) ? { kind: "callable", type: 7, name: n } : undefined),
        symbolKind: (n: number) => (n === checksum("IsReady") ? "function" : undefined),
        callFunction: (n: number) => {
            calls.push(`function ${n.toString(16)}`);
            return true;
        },
        defineScript: (n: number) => scriptsDefined.push(n),
        defineGlobal: (n: number, v: unknown) => globals.set(n, v),
        draw: () => 0,
    };
    const context = runInNewContext(
        `globalThis.__qbGame = game;\n${helperSource("random")}${helperSource("values")}` +
            readFileSync(new URL("../../scripting/runtime.js", import.meta.url), "utf8") +
            readFileSync(new URL("../../scripting/qb_ts.js", import.meta.url), "utf8") +
            compileTS(source, "game.ts") +
            "\nglobalThis",
        { game },
    ) as { qb: Record<string, (...a: unknown[]) => unknown> };
    return { qb: context.qb, calls, scriptsDefined, globals };
}

// (values made in the vm context have its prototypes: compared as data)
const same = (actual: unknown, expected: unknown, message?: string) =>
    assert.deepEqual(actual === undefined ? undefined : JSON.parse(JSON.stringify(actual)), expected, message);

test("game mode: requests out, results and the record's parameters in, the level's changes back", () => {
    const { qb, calls, scriptsDefined, globals } = load();
    const n = checksum;
    same(scriptsDefined, [n("Main")]);
    same(globals.get(n("Limit")), { type: 1, bits: 3 });

    qb.begin!(7, n("Main"), [[n("start"), { type: 1, bits: 2 }]], 99);
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
    const params = r.params as [number, unknown][];
    same(params.find(([k]) => k === n("seen"))?.[1], { type: 13, bits: n("hello") });
    same(params.find(([k]) => k === n("ready"))?.[1], { type: 1, bits: 1 });
    r = qb.resume!(7, params, true) as Record<string, unknown>;
    assert.equal(r.done, true);
    same(r.returned, [[n("result"), { type: 1, bits: 5 }]]);
    assert.throws(() => qb.resume!(7, params, true), /Unknown QB level/);
});
