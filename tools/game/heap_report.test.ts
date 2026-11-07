// NOT ORIGINAL - tests of tools/game/heap_report.ts. The lines are as the game writes them
// (src/engine/memory/memory_report.cpp): the format both sides agree on.

import assert from "node:assert/strict";
import { test } from "node:test";
import { compare, firstLoads, readHeaps, table, tight } from "./heap_report.ts";

const X86 = [
    "12:00:01 memory heaps in foun (32-bit build): 3 heaps",
    "12:00:01 memory heap in foun: region=94371824 used=4023296 peak=10400768 blocks=812 peak_blocks=1900 name=Top Down",
    "12:00:01 memory heap in foun: region=110496 used=40000 peak=52000 blocks=120 peak_blocks=130 name=skater",
    "12:00:01 memory heap in foun: region=15360 used=14000 peak=14500 blocks=3 peak_blocks=3 name=theme",
    "12:00:09 memory heaps in foun (32-bit build): 3 heaps",
    "12:00:09 memory heap in foun: region=94371824 used=1 peak=1 blocks=1 peak_blocks=1 name=Top Down",
].join("\n");
const X64 = [
    "memory heaps in foun (64-bit build): 3 heaps",
    "memory heap in foun: region=188743648 used=6000000 peak=15000000 blocks=812 peak_blocks=1900 name=Top Down",
    "memory heap in foun: region=220992 used=70000 peak=90000 blocks=120 peak_blocks=130 name=skater",
    "memory heap in foun: region=30720 used=28000 peak=29000 blocks=3 peak_blocks=3 name=theme",
].join("\n");

test("reads the heap lines and the build's width", () => {
    const report = readHeaps(X86);
    assert.equal(report.bits.get("in foun"), 32);
    assert.equal(report.heaps.length, 4);
    assert.deepEqual(report.heaps[0], {
        when: "in foun",
        load: 0,
        name: "Top Down",
        region: 94371824,
        used: 4023296,
        peak: 10400768,
        blocks: 812,
        peakBlocks: 1900,
    });
});

test("keeps the first load of a level", () => {
    const loads = firstLoads(readHeaps(X86));
    assert.deepEqual([...loads.keys()], ["in foun"]);
    assert.equal(loads.get("in foun")!.length, 3);
    assert.equal(loads.get("in foun")![0]!.peak, 10400768);
});

test("marks a peak within 10% of its region", () => {
    const heaps = readHeaps(X86).heaps;
    assert.equal(tight(heaps[1]!), false);
    assert.equal(tight(heaps[2]!), true);
    assert.match(table(heaps.slice(0, 3))[3]!, /^theme .* TIGHT$/);
});

test("compares two builds heap by heap", () => {
    const rows = compare(firstLoads(readHeaps(X86)).get("in foun")!, firstLoads(readHeaps(X64)).get("in foun")!);
    assert.equal(rows.length, 4);
    assert.match(rows[2]!, /^skater\s+108\s+216\s+51\s+88\s+x1\.73\s+128 KB$/);
    assert.match(rows[3]!, /^theme .* TIGHT$/);
});
