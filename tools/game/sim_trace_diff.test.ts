// NOT ORIGINAL - sim_trace_diff.ts: entries compared without their callers, skipped ones left out.
import assert from "node:assert/strict";
import { test } from "node:test";
import { comparable, entries, firstDifference } from "./sim_trace_diff.ts";

test("callers are left out of the comparison; the first differing entry is found", () => {
    assert.equal(comparable("1 - rnd0 4 -> 3 at 368474 367384"), "1 - rnd0 4 -> 3");
    const a = entries("# header\n1 x@0/0 cmd 1 {}\n1 - rnd0 4 -> 3 at 11 22\n2 object 1 2 (0, 0, 0)\n");
    const b = entries("# header\n1 x@0/0 cmd 1 {}\n1 - rnd0 4 -> 3 at 33 44\n2 object 1 3 (0, 0, 0)\n");
    assert.deepEqual(firstDifference(a, b), { a: 2, b: 2, same: false });
    assert.deepEqual(firstDifference(a, a), { a: 3, b: 3, same: true });
});

test("skipped entries are left out of both traces", () => {
    const a = entries("1 a@0/0 cmd 1 {}\n2 flare@0/0 cmd 9 {}\n2 b@0/0 cmd 2 {}", /flare@/);
    const b = entries("1 a@0/0 cmd 1 {}\n2 b@0/0 cmd 2 {}\n3 flare@0/0 cmd 9 {}", /flare@/);
    assert.equal(firstDifference(a, b).same, true);
    // (a trace one entry shorter: they part where it ends)
    assert.deepEqual(firstDifference(a, [...b, "4 c@0/0 cmd 3 {}"]), { a: 2, b: 2, same: false });
});
