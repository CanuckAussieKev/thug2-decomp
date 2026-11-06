// NOT ORIGINAL - the layering rule (tools/layering.ts): no file outside the boundary uses the named views of in-memory
// layouts.

import assert from "node:assert/strict";
import { test } from "node:test";
import { countViews, isBoundary, viewsOutside } from "./layering.ts";

test("the views are counted and the boundary is told apart", () => {
    assert.equal(countViews("raw::as<int>(p); cell::of(q); memory::bits_as<float>(x); craw::as"), 3);
    assert.equal(countViews("core::address(p); core::at_address<int>(n); core::at(list, 0)"), 2);
    assert.ok(isBoundary("src/formats/pre_format.cpp"));
    assert.ok(isBoundary("src/engine/script/token_reader.cpp"));
    assert.ok(!isBoundary("src/render_pc/geom.cpp"));
});

test("no file outside the boundary uses a named view", () => {
    assert.deepEqual(viewsOutside(), []);
});
