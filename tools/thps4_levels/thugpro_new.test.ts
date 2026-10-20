// NOT ORIGINAL - tests of THUGPRO's own content by reference (thugpro_new.ts) with made-up code (no game data).

import assert from "node:assert/strict";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { test } from "node:test";
import { checksum, Struct } from "../level_port/qb.ts";
import { pack } from "../lib/struct.ts";
import {
    applyNew,
    HELPER_MAP,
    HELPER_SCRIPTS,
    hookStart,
    loadFiles,
    type NewEntry,
    namesIn,
    rawScripts,
    renamed,
    sha256,
} from "./thugpro_new.ts";

const name = (t: string): Buffer => Buffer.concat([Buffer.from([0x16]), pack("<I", checksum(t))]);
// script <s> { THUGPRO_SafeKill name=X }, then a global value - as a compiled .qb
const code = Buffer.concat([
    Buffer.from([0x23]),
    name("TP_Test"),
    Buffer.from([0x01]),
    name("THUGPRO_SafeKill"),
    name("name"),
    Buffer.from([0x07]),
    name("Node_X"),
    Buffer.from([0x01, 0x24]),
    Buffer.from([0x01]),
    Buffer.from([0x00]),
]);

test("rawScripts: a script's whole code by its name; renamed: THUGPRO's helpers made ours, same size", () => {
    const scripts = rawScripts(code);
    const s = scripts.get(checksum("TP_Test"))!;
    assert.equal(s[0], 0x23);
    assert.equal(s[s.length - 1], 0x24);
    const ours = renamed(s, HELPER_MAP);
    assert.equal(ours.length, s.length);
    assert.ok(namesIn(ours).has(checksum("thps4_tp_safe_kill")));
    assert.ok(!namesIn(ours).has(checksum("THUGPRO_SafeKill")));
    assert.ok(namesIn(ours).has(checksum("Node_X")));
    // (every helper our renaming points at is one of ours)
    const helpers = new Set(Object.keys(HELPER_SCRIPTS).map(checksum));
    for (const to of HELPER_MAP.values()) assert.ok(helpers.has(to));
});

test("hookStart: the start script spawned from the setup script, or a setup script made for it", () => {
    const [a, hasA] = hookStart("script x_ported_setup {\n    SpawnScript A \n}\n", "x_ported_setup", "x_new", true);
    assert.equal(a, "script x_ported_setup {\n    SpawnScript x_new \n    SpawnScript A \n}\n");
    assert.ok(hasA);
    const [b, hasB] = hookStart("", "x_ported_setup", "x_new", false);
    assert.equal(b, "script x_ported_setup {\n    SpawnScript x_new \n}\n");
    assert.ok(hasB);
});

test("missing install or a different file: a reason, never a failure; applyNew then leaves the level as it is", () => {
    const files = {
        "x/x": { prx: "data\\pre\\X_scripts.prx", entry: "levels\\x\\x.qb", size: 1, sha256: sha256(code) },
    };
    const empty = fs.mkdtempSync(path.join(os.tmpdir(), "tpnew-"));
    try {
        const r = loadFiles(empty, files, ["x/x"]);
        assert.equal(typeof r, "string");
        assert.match(String(loadFiles(path.join(empty, "nothing"), files, ["x/x"])), /no THUGPRO install/);
        const entry: NewEntry = {
            id: "TPN-THPS4-1",
            op: "add",
            rule: "script",
            target: { level: "X", script: "TP_Test" },
            why: "test",
            file: "x/x",
        };
        const converted = [new Struct([])];
        const brought = new Map<number, Buffer>();
        const report: string[] = [];
        const write = process.stderr.write;
        process.stderr.write = (() => true) as typeof process.stderr.write;
        let made: ReturnType<typeof applyNew>;
        try {
            made = applyNew([entry], files, {
                base: "X",
                level: "x",
                converted,
                names: new Map(),
                brought,
                text: "",
                report,
                thugpro: empty,
                banners: false,
                out: empty,
            });
        } finally {
            process.stderr.write = write;
        }
        assert.equal(made.text, "");
        assert.equal(made.start, null);
        assert.equal(brought.size, 0);
        assert.equal(converted.length, 1);
        assert.match(report.join("\n"), /left out/);
    } finally {
        fs.rmSync(empty, { recursive: true, force: true });
    }
});
