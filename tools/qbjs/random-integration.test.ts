// NOT ORIGINAL - token/manifest/lifetime regressions for random value integration.
import assert from "node:assert/strict";
import { test } from "node:test";
import { runInNewContext } from "node:vm";
import { emitIR } from "./emitter.ts";
import { command, field, integer, name, param, script, word } from "./fixtures.ts";
import { randomAssertions, randomChoice, randomFixture, randomRange, randomSource } from "./random-fixtures.ts";
import { readIR } from "./reader.ts";
import { nativeRuntimeBundle } from "./runtime-fixtures.ts";
import { checksum } from "./translate.ts";

test("random values execute on shared product RNG with lazy branches and owner cleanup", () => {
    runInNewContext(
        randomSource() + nativeRuntimeBundle() + randomFixture().javascript + randomAssertions(),
        {},
        { timeout: 5000 },
    );
});
test("reader validates random targets, join jumps and every truncation", () => {
    const bytes = randomFixture().input;
    for (let i = 0; i < bytes.length; i++) assert.throws(() => readIR(bytes.subarray(0, i)), /QB offset/);
    const branch = randomChoice(0x2f, [integer(1), integer(2)]);
    const mod = (value: number[]) => Uint8Array.from([...script("test", [...field("x", value), 1]), 0]);
    for (const index of [9, 22]) {
        const bad = [...branch];
        bad[index] = (bad[index]! + 1) & 255;
        assert.throws(() => readIR(mod(bad)), /random/);
    }
    assert.throws(() => readIR(mod([0x2f, ...word(4097)])), /count/);
    assert.throws(() => readIR(mod(randomChoice(0x2f, [integer(1)], [0]))), /unsafe weighted/);
    assert.throws(() => readIR(mod([5, ...branch, 6])), /literal-array/);
    assert.throws(() => readIR(mod([14, ...randomRange(0, 0, true), 15])), /RandomRange2 inside/);
    assert.throws(
        () => readIR(Uint8Array.from([...field("g", randomChoice(0x40, [integer(1), integer(2)])), 1, 0])),
        /stored random/,
    );
});
test("requires.random is conditional and helper versions are validated before registration", () => {
    assert.match(randomFixture().javascript, /"random":1/);
    assert.doesNotMatch(emitIR(readIR(Uint8Array.of(0))), /"random":1/);
    const context = runInNewContext(nativeRuntimeBundle() + "globalThis");
    assert.throws(
        () => runInNewContext(nativeRuntimeBundle() + randomFixture().javascript),
        /QB random API 1 required/,
    );
    assert.throws(() => context.qb.module({ format: 2, requires: { values: 2, random: 2 } }), /requirements/);
});
test("global ranges draw at load time and task fuel failure precedes random draws", () => {
    const input = Uint8Array.from([
        ...field("randomGlobal", randomRange(0x3f800000, 0x41200000)),
        1,
        ...script("readRandomGlobal", [...field("result", [14, ...name("randomGlobal"), 15]), 1]),
        0,
    ]);
    const js = emitIR(readIR(input, "global-random.qb"));
    runInNewContext(
        randomSource() +
            nativeRuntimeBundle() +
            js +
            randomFixture().javascript +
            `{
        const rng=qb.random(),ref=qbRandom.create(0);ref.range(0x3f800000,0x41200000);
        if(JSON.stringify(rng.state())!==JSON.stringify(ref.state()))throw Error('global load draw');
        for(let i=0;i<2;i++){
            const id=qb.start(${checksum("readRandomGlobal")},[],1000),r=qb.update(id,0);qb.cancel(id);
            if(r.status!=='finished'||r.parameters.find(f=>f[0]===${checksum("result")})[1].bits!==10)throw Error('global read');
        }
        if(JSON.stringify(rng.state())!==JSON.stringify(ref.state()))throw Error('global redrawn by task');
        const before=JSON.stringify(rng.state()),id=qb.start(${checksum("randomStored")},[],1),r=qb.update(id,0);qb.cancel(id);
        if(r.status!=='fault'||!r.message.includes('fuel')||JSON.stringify(rng.state())!==before)throw Error('fuel before draw');
    }`,
        {},
        { timeout: 5000 },
    );
});
test("dynamic anonymous structures merge members while named/explicit-zero fields stay nested", () => {
    const structure = [3, ...field("member", integer(7)), 4];
    const choice = () => randomChoice(0x2f, [structure]);
    const ir = readIR(
        Uint8Array.from([
            ...script("anonymous", command("Inspect", choice())),
            ...script("named", command("Inspect", field("nested", choice()))),
            ...script("explicitZero", command("Inspect", [0x16, ...word(0), 7, ...choice()])),
            ...script("parameter", command("Inspect", param("p"))),
            0,
        ]),
    );
    const context = runInNewContext(randomSource() + nativeRuntimeBundle() + emitIR(ir) + "globalThis");
    const captured: unknown[] = [];
    context.__qbHost = (_n: unknown, args: unknown) => {
        captured.push(JSON.parse(JSON.stringify(args)));
        return { condition: true, outputs: [] };
    };
    for (const n of ["anonymous", "named", "explicitZero", "parameter"]) {
        const id = context.qb.start(
            checksum(n),
            [[checksum("p"), { type: 10, members: [[checksum("member"), { type: 1, bits: 7 }]] }]],
            10000,
        );
        assert.equal(context.qb.update(id, 0).status, "finished");
        context.qb.cancel(id);
    }
    const members = [[checksum("member"), { type: 1, bits: 7 }]],
        value = { type: 10, members };
    assert.deepEqual(captured, [members, [[checksum("nested"), value]], [[0, value]], members]);
});
