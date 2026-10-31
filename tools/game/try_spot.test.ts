// NOT ORIGINAL - tests of tools/pipeline/game/try_spot.ts's pure parts (no game data).

import assert from "node:assert/strict";
import { test } from "node:test";
import { formatList, formatSpot, parseSpot, readList, withSpotGuard } from "./try_spot.ts";

test("parseSpot takes what the game's ParseSpot takes", () => {
    assert.deepEqual(parseSpot("120,-40.5,3000"), { x: 120, y: -40.5, z: 3000 });
    assert.deepEqual(parseSpot(" 1, 2 ,3 , -90 "), { x: 1, y: 2, z: 3, heading: -90 });
    assert.deepEqual(parseSpot("1 2 3 45"), { x: 1, y: 2, z: 3, heading: 45 });
    assert.deepEqual(parseSpot("(10.5, 20.0, -30.25)"), { x: 10.5, y: 20, z: -30.25 });
    assert.deepEqual(parseSpot("1e2,0,0"), { x: 100, y: 0, z: 0 });
    for (const bad of ["", "1,2", "1,2,3,4,5", "1,,2,3", "1,2,3,", "1,2,x", "1x,2,3", "1,2,inf", "nan,2,3", ",1,2,3"])
        assert.equal(parseSpot(bad), null, bad);
});

test("formatSpot gives -at's form, without spaces", () => {
    assert.equal(formatSpot({ x: -1584, y: 25.8, z: -723.9, heading: -90 }), "-1584,25.8,-723.9,-90");
    assert.equal(formatSpot({ x: 1, y: 2, z: 3 }), "1,2,3");
});

test("withSpotGuard keeps a negative spot from being read as an option", () => {
    assert.deepEqual(withSpotGuard(["sub", "-1584,25.8,-723.9,-90"]), ["sub", "--", "-1584,25.8,-723.9,-90"]);
    assert.deepEqual(withSpotGuard(["--game", "C:\\Games\\THUG2-1", "foun", "1,2,3"]), [
        "--game",
        "C:\\Games\\THUG2-1",
        "foun",
        "1,2,3",
    ]);
    assert.deepEqual(withSpotGuard(["--", "-1,2,3"]), ["--", "-1,2,3"]);
});

test("the collfix list: every spot parses, and each has a command", () => {
    const spots = readList("collfix");
    assert.equal(spots.length, 8);
    for (const s of spots) assert.ok(parseSpot(s.at.join(",")), `${s.name}: ${s.at.join(",")}`);
    const lines = formatList("collfix", spots);
    assert.equal(lines.length, spots.length * 3);
    assert.match(lines[1]!, /^ {3}node tools\/pipeline\/game\/try_spot\.ts sub -?\d/);
    assert.equal(lines[2], "   (or: node tools/pipeline/game/try_spot.ts --list collfix 1)");
});
