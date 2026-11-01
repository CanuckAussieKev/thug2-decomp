// NOT ORIGINAL - tests of tools/pipeline/game/collision_events.ts. LINE is a line as the game writes it
// (src/game/skater/collision_events.h): the format both sides agree on.

import assert from "node:assert/strict";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { test } from "node:test";
import { checksum } from "../level_port/qb.ts";
import type * as col from "../level_port/thug2_col.ts";
import {
    describeFace,
    fileMismatches,
    findCol,
    groupSpots,
    parseDetailLine,
    parseEventLine,
    parseFace,
    playerTurn,
    readEvents,
    spotDetails,
    spotRows,
    table,
    verdict,
    zoneOf,
} from "./collision_events.ts";

const LINE =
    "collision-event: n=3 level=thp8_net_beavervalley frame=21080 reason=wall-ahead paths=wall-ahead,side-wall " +
    "frames=4 spot=-6560.0,10.5,3452.5 pos_before=-6605.2,7.0,3452.5 pos=-6590.0,7.0,3452.5 " +
    "vel_before=300.0,0.0,1.2 vel_after=-150.0,0.0,-0.6 heading_before=89.6 heading=-90.4 ground_n=0.00,1.00,0.00 " +
    "ground_min_y=0.98 " +
    "face=sector:Z_Houses_Road01;face:17;flags:0x1040;decoded:trigger|invisible;terrain:6;n:-0.71,0.71,0.00;" +
    "at:-6560.0,10.5,3452.5 " +
    "probe_down_hit=none " +
    "probe_down_hit_all=sector:Z_Houses_Road01;face:17;flags:0x1040;decoded:trigger|invisible;terrain:6;" +
    "n:-0.71,0.71,0.00;at:-6560.0,10.5,3452.5 " +
    "probe_down_beyond=object:0xABCDEF01;face:0;flags:0x0001;decoded:skatable;terrain:0;n:0.00,0.00,0.00;" +
    "at:0.0,0.0,0.0 " +
    "probe_down_beyond_all=none probe_ahead=none probe_ahead_all=none probe_dir=1.00,0.00,0.00 flipped=ground-check>off-ground@21080 repeats=2 " +
    "screenshot=requested";

// The game's detail line.
const DETAIL =
    "collision-event-detail: n=3 state=ground physics=skating speed=326.4 trick=0 balance=manual input=up " +
    "input_window=up+down+x stick=0.00,-1.00 ray=-6519.8,7.1,3453.1>-6519.8,-1193.0,3453.1 " +
    "corners=-6530.0,7.0,3440.0;-6513.0,7.0,3440.0;-6513.0,7.0,3460.0 " +
    "col_box=-6600.0,7.0,3300.0>-6513.0,7.0,3640.0 edge=1 edge_dist=6.8 open_edge=1 " +
    "col_file=4140376@2026-10-24T12:58:51 scn_file=- approach=-6599,7,3453,g;-6520,7,3453,g camera=-";

test("the game's line parses", () => {
    const e = parseEventLine(`[14:50:48.123] ${LINE}`)!;
    assert.equal(e.n, 3);
    assert.equal(e.level, "thp8_net_beavervalley");
    assert.equal(e.frame, 21080);
    assert.equal(e.reason, "wall-ahead");
    assert.deepEqual(e.paths, ["wall-ahead", "side-wall"]);
    assert.deepEqual(e.spot, [-6560, 10.5, 3452.5]);
    assert.equal(e.fields.vel_after, "-150.0,0.0,-0.6");
    assert.equal(e.fields.screenshot, "requested");
    assert.equal(e.fields.flipped, "ground-check>off-ground@21080");
    assert.deepEqual(e.face, {
        kind: "sector",
        name: "Z_Houses_Road01",
        face: 17,
        flags: 0x1040,
        decoded: "trigger|invisible",
        terrain: 6,
        n: [-0.71, 0.71, 0],
        at: [-6560, 10.5, 3452.5],
    });
    assert.equal(e.probes.probe_down_hit, null);
    assert.equal(e.probes.probe_down_beyond?.kind, "object");
    assert.equal(e.probes.probe_down_beyond?.name, "0xABCDEF01");
    assert.equal(parseEventLine("level editor: level picked"), null);
    assert.equal(parseEventLine("collision-event: logging on in thp8_net_beavervalley"), null);
    assert.equal(parseFace("none"), null);
});

test("the verdict reads the face and the probes", () => {
    const e = parseEventLine(LINE)!;
    assert.equal(verdict(e), "invisible face, trigger face, a wall that faces up (ramp-like)");
    const slope = parseEventLine(
        LINE.replace("reason=wall-ahead", "reason=slope").replace(
            "probe_down_hit=none",
            "probe_down_hit=sector:A;face:1;flags:0x0001;decoded:skatable;terrain:0;n:0,1,0;at:0,0,0",
        ),
    )!;
    assert.match(verdict(slope), /^invisible face, trigger face, ground at 45 deg/);
    const frig = parseEventLine(
        LINE.replace("reason=wall-ahead", "reason=uber-frig")
            .replace(/ face=\S+/, " face=none")
            .replace("flipped=ground-check>off-ground@21080", "flipped=update-rest>update-start@21080"),
    )!;
    assert.equal(frig.face, null);
    assert.equal(verdict(frig), "no collision under the skater's next step (uber frig)");
});

test("events group by level and spot", () => {
    const at = (x: number, level = "a"): string =>
        LINE.replace("spot=-6560.0,10.5,3452.5", `spot=${x},0,0`).replace(
            "level=thp8_net_beavervalley",
            `level=${level}`,
        );
    const events = readEvents([at(0), at(50), at(500), at(10, "b"), "noise"].join("\n"));
    const spots = groupSpots(events, 80);
    assert.deepEqual(
        spots.map((s) => [s.level, s.at[0], s.events.length]),
        [
            ["a", 0, 2],
            ["a", 500, 1],
            ["b", 10, 1],
        ],
    );
    const rows = spotRows(spots);
    assert.equal(rows[0]![3], "wall-ahead x2");
    assert.equal(rows[0]![2], "6"); // (2 events, each with 2 left out before it)
    assert.equal(rows[0]![5], "Z_Houses_Road01 #17");
    const lines = table(rows, ["level", "spot", "n", "reason", "state", "face", "flags", "probes"]);
    assert.equal(lines.length, 5);
    assert.ok(lines[0]!.startsWith("level  spot"));
});

test("the collision file names the object, the zone and the face", () => {
    assert.equal(zoneOf("Z_Houses_Road01"), "houses");
    assert.equal(zoneOf("thp8_net_beavervalley_GapFill_3"), "-");
    const object = {
        checksum: checksum("Z_Houses_Road01"),
        verts: [
            [-6600, 0, 3400],
            [-6500, 0, 3400],
            [-6600, 100, 3500],
        ],
        faces: Array.from({ length: 18 }, () => [0x1040, 6, 0, 2, 1]),
    } as unknown as col.ColObject;
    const lines = describeFace({ objects: [object] }, parseEventLine(LINE)!);
    assert.match(lines[0]!, /^col object #0 Z_Houses_Road01 \(zone houses\), 18 faces$/);
    assert.match(lines[1]!, /^face #17: file flags 0x1040 terrain 6, normal /);

    const root = fs.mkdtempSync(path.join(os.tmpdir(), "colev-"));
    try {
        const dir = path.join(root, "thug2-decomp-thp8", "levels", "lv");
        fs.mkdirSync(dir, { recursive: true });
        fs.writeFileSync(path.join(dir, "lv.col.xbx"), "");
        assert.equal(findCol("lv", [root]), path.join(dir, "lv.col.xbx"));
        assert.equal(findCol("other", [root]), null);
    } finally {
        fs.rmSync(root, { recursive: true, force: true });
    }
});

test("the detail line joins its event; open edges, player turns and file changes are told", () => {
    const [e] = readEvents(`${LINE}\n[x] ${DETAIL}\n`);
    assert.equal(e!.detail?.state, "ground");
    assert.equal(e!.detail?.open_edge, "1");
    assert.equal(e!.repeats, 2);
    assert.equal(parseDetailLine(LINE), null);
    assert.match(verdict(e!), /an open edge 6\.8 from the spot \(a dropped piece\?\)/);
    const details = spotDetails(groupSpots([e!])[0]!);
    assert.match(
        details[0]!,
        /^n=3 wall-ahead; probe_dir 1\.00,0\.00,0\.00; \+2 left out before it; ground\/manual speed 326\.4/,
    );
    assert.match(details[0]!, /ray -6519\.8,7\.1,3453\.1>-6519\.8,-1193\.0,3453\.1/);

    // A slope with the down button held in the window: perhaps the player's; flat ground: the player's.
    const slope = readEvents(`${LINE.replace("reason=wall-ahead", "reason=slope")}\n${DETAIL}`)[0]!;
    assert.equal(playerTurn(slope), "player turn? (down held)");
    const flat = parseEventLine(
        LINE.replace("reason=wall-ahead", "reason=slope").replace("ground_min_y=0.98", "ground_min_y=1.00"),
    )!;
    assert.equal(playerTurn(flat), "flat ground: the player's own turn");
    assert.equal(playerTurn(e!), null);

    const level = "thp8_net_beavervalley";
    assert.deepEqual(fileMismatches([e!], "4140376@2026-10-24T12:58:51", level), []);
    assert.equal(fileMismatches([e!], "4200000@2026-10-24T17:00:00", level).length, 1);
});
