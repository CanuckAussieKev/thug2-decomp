// NOT ORIGINAL - the layering rule (tools/layering.ts): no file outside the boundary has more named views of in-memory
// layouts than its budget (tools/layering_budget.json, which may only fall).

import assert from "node:assert/strict";
import { test } from "node:test";
import { countViews, isBoundary, overBudget, readBudget, viewCounts } from "./layering.ts";

test("the views are counted and the boundary is told apart", () => {
    assert.equal(countViews("raw::as<int>(p); cell::of(q); memory::bits_as<float>(x); craw::as"), 3);
    assert.ok(isBoundary("src/formats/pre_format.cpp"));
    assert.ok(isBoundary("src/engine/script/token_reader.cpp"));
    assert.ok(!isBoundary("src/engine/gfx/geom.cpp"));
});

test("no file outside the boundary is over its named-view budget", () => {
    assert.deepEqual(overBudget(viewCounts(), readBudget()), []);
});
