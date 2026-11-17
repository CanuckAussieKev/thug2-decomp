import assert from "node:assert/strict";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { test } from "node:test";
import {
    archiveBake,
    archivedVersions,
    currentDataVersion,
    fileVersion,
    findArchived,
    inputsHash,
    pruneVersions,
    restoreVersion,
} from "./relit_archive.ts";

function relitBytes(version: number, tail = "x"): Buffer {
    const head = Buffer.alloc(8);
    head.writeUInt32LE(0x44523254, 0);
    head.writeUInt32LE(version, 4);
    return Buffer.concat([head, Buffer.from(tail)]);
}

function setup(): { game: string; folder: string; roots: string[] } {
    const game = fs.mkdtempSync(path.join(os.tmpdir(), "relit-archive-"));
    const folder = path.join(game, "thug2-decomp-relit");
    fs.mkdirSync(path.join(game, "Data", "levels", "BO"), { recursive: true });
    fs.writeFileSync(path.join(game, "Data", "levels", "BO", "BO.scn.xbx"), "scene");
    return { game, folder, roots: [path.join(game, "Data"), path.join(game, "thug2-decomp-data")] };
}

function bake(folder: string, roots: string[], version: number, level: string, scene: string): string {
    fs.mkdirSync(folder, { recursive: true });
    const live = path.join(folder, `${scene}.relit`);
    fs.writeFileSync(live, relitBytes(version, level));
    const inputs = inputsHash(roots, level);
    archiveBake(
        folder,
        version,
        { level, inputs, scene, line: `${level}\t${scene}\trelit`, state: `${level}\trow` },
        live,
        "log",
    );
    return inputs;
}

test("the inputs' hash follows the level's files, not their dates or other levels", () => {
    const { game, roots } = setup();
    const first = inputsHash(roots, "BO");
    assert.equal(inputsHash(roots, "bo"), first);
    fs.utimesSync(path.join(game, "Data", "levels", "BO", "BO.scn.xbx"), 1, 1);
    assert.equal(inputsHash(roots, "BO"), first);
    fs.mkdirSync(path.join(game, "Data", "levels", "TR"), { recursive: true });
    fs.writeFileSync(path.join(game, "Data", "levels", "TR", "TR.scn.xbx"), "other");
    assert.equal(inputsHash(roots, "BO"), first);
    fs.writeFileSync(path.join(game, "Data", "levels", "BO", "BO.scn.xbx"), "scene2");
    assert.notEqual(inputsHash(roots, "BO"), first);
    // an overlay copy of the level counts too
    const before = inputsHash(roots, "BO");
    fs.mkdirSync(path.join(game, "thug2-decomp-data", "levels", "BO"), { recursive: true });
    fs.writeFileSync(path.join(game, "thug2-decomp-data", "levels", "BO", "BO.col.xbx"), "col");
    assert.notEqual(inputsHash(roots, "BO"), before);
});

test("an archived bake is found by version and inputs, and restored as the live file", () => {
    const { folder, roots } = setup();
    const inputs = bake(folder, roots, 7, "BO", "1a2b3c4d");
    const found = findArchived(folder, 7, "BO", inputs);
    assert.equal(found?.scene, "1a2b3c4d");
    assert.equal(findArchived(folder, 6, "BO", inputs), null, "another version");
    assert.equal(findArchived(folder, 7, "BO", "0000000000000000"), null, "other inputs");
    fs.rmSync(path.join(folder, "1a2b3c4d.relit"));
    const out = restoreVersion(folder, 7, 7, "HEADER");
    assert.equal(out.refused, null);
    assert.equal(fileVersion(fs.readFileSync(path.join(folder, "1a2b3c4d.relit"))), 7);
    assert.equal(fs.readFileSync(path.join(folder, "bake-report.txt"), "utf8"), "BO\t1a2b3c4d\trelit\n");
    assert.equal(fs.readFileSync(path.join(folder, "state-table.txt"), "utf8"), "HEADER\nBO\trow\n");
    assert.equal(fs.readFileSync(path.join(folder, "logs", "BO.log"), "utf8"), "log");
});

test("a damaged archive file (other version) is not used", () => {
    const { folder, roots } = setup();
    const inputs = bake(folder, roots, 7, "BO", "1a2b3c4d");
    const file = path.join(folder, "archive", "v7", `bo-${inputs}.relit`);
    fs.writeFileSync(file, relitBytes(6));
    assert.equal(findArchived(folder, 7, "BO", inputs), null);
});

test("restore refuses a version the game would call out of date", () => {
    const { folder, roots } = setup();
    bake(folder, roots, 6, "BO", "1a2b3c4d");
    const out = restoreVersion(folder, 6, 7, "H");
    assert.match(out.refused ?? "", /reads relit data version 7/);
    assert.equal(fs.existsSync(path.join(folder, "bake-report.txt")), false);
    assert.match(restoreVersion(folder, 7, 7, "H").refused ?? "", /no archived data/);
    assert.match(restoreVersion(folder, 7, null, "H").refused ?? "", /unknown/);
});

test("restore takes a level's newest bake of the version", () => {
    const { game, folder, roots } = setup();
    bake(folder, roots, 7, "BO", "1a2b3c4d");
    fs.writeFileSync(path.join(game, "Data", "levels", "BO", "BO.scn.xbx"), "changed");
    bake(folder, roots, 7, "BO", "5e6f7a8b");
    fs.rmSync(path.join(folder, "1a2b3c4d.relit"));
    fs.rmSync(path.join(folder, "5e6f7a8b.relit"));
    const out = restoreVersion(folder, 7, 7, "H");
    assert.deepEqual(
        out.restored.map((e) => e.scene),
        ["5e6f7a8b"],
    );
});

test("prune keeps the newest versions and the game's own", () => {
    const { folder, roots } = setup();
    for (const v of [3, 4, 5, 6, 7]) bake(folder, roots, v, "BO", "1a2b3c4d");
    assert.deepEqual(pruneVersions(folder, 2, 7), [3, 4, 5]);
    assert.deepEqual(archivedVersions(folder), [6, 7]);
    assert.deepEqual(pruneVersions(folder, 1, 6), [], "the game's own version stays (6) with the newest (7)");
    assert.deepEqual(archivedVersions(folder), [6, 7]);
});

test("the data version is read from the source header", () => {
    const version = currentDataVersion();
    assert.ok(version !== null && version >= 7);
    assert.equal(currentDataVersion("C:\\no\\such\\header.h"), null);
});
