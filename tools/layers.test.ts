// NOT ORIGINAL - the layer checker (tools/layers.ts): the map's rules, the ratchet, and the repository against its
// baseline.

import assert from "node:assert/strict";
import { test } from "node:test";
import { compare, libraryOf, loadMap, mayInclude, readBaseline, strictViolations, violations } from "./layers.ts";

test("folders belong to the library of their longest prefix", () => {
    const map = loadMap();
    assert.equal(libraryOf(map, "src/base/core/str.cpp"), "base");
    assert.equal(libraryOf(map, "src/engine/gfx/engine.cpp"), "engine");
    assert.equal(libraryOf(map, "src/render_d3d9/geom.cpp"), "render_d3d9");
    assert.equal(libraryOf(map, "src/game/rider/rider.h"), "game");
    assert.equal(libraryOf(map, "src/app/main.cpp"), "app");
    assert.equal(libraryOf(map, "docs/x.h"), undefined);
});

test("a library may include itself and the ones below it only", () => {
    const map = loadMap();
    assert.ok(mayInclude(map, "engine", "base"));
    assert.ok(mayInclude(map, "game", "game"));
    assert.ok(!mayInclude(map, "engine", "game"));
    assert.ok(!mayInclude(map, "frontend", "editor"));
    assert.ok(!mayInclude(map, "formats", "platform"));
    assert.ok(mayInclude(map, "app", "editor"));
});

test("the ratchet reports new violations and baseline lines that are gone", () => {
    const result = compare(["a -> b", "c -> d"], ["c -> d", "e -> f"]);
    assert.deepEqual(result.added, ["a -> b"]);
    assert.deepEqual(result.stale, ["e -> f"]);
});

test("the strict libraries have no upward include, in the sources or the baseline", () => {
    const map = loadMap();
    assert.deepEqual(
        strictViolations(map, ["src/engine/x.cpp -> src/game/y.h", "src/frontend/a.cpp -> src/editor/b.h"]),
        ["src/engine/x.cpp -> src/game/y.h"],
    );
    assert.deepEqual(strictViolations(map, [...violations(map), ...readBaseline()]), []);
});

test("the repository has exactly the baseline's upward includes", () => {
    assert.deepEqual(compare(violations(), readBaseline()), { added: [], stale: [] });
});
