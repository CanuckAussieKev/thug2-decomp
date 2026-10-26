// NOT ORIGINAL - tests of tools/pipeline/game/bake_backup.ts in a temporary folder: installs backed up one after another, and
// restores undoing them newest first (files the install made deleted, files it replaced put back).

import assert from "node:assert/strict";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { test } from "node:test";
import { backUp, listBackups, restoreLatest } from "./bake_backup.ts";

test("bake backups: a stack of installs undone newest first", () => {
    const game = fs.mkdtempSync(path.join(os.tmpdir(), "bake-backup-"));
    try {
        const level = path.join(game, "thug2-decomp-data", "levels", "TR");
        const qb = path.join(level, "TR.qb");
        const scripts = path.join(level, "TR_scripts.qb");
        fs.mkdirSync(level, { recursive: true });
        fs.writeFileSync(qb, "original");

        // install 1 replaces TR.qb and makes TR_scripts.qb
        backUp(game, "TR", [qb, scripts], new Date(2026, 9, 21, 10, 0, 0, 0));
        fs.writeFileSync(qb, "bake 1");
        fs.writeFileSync(scripts, "scripts 1");
        // install 2 replaces both (the same instant: a -2 folder)
        backUp(game, "TR", [qb, scripts, qb], new Date(2026, 9, 21, 10, 0, 0, 0));
        fs.writeFileSync(qb, "bake 2");
        fs.writeFileSync(scripts, "scripts 2");
        assert.equal(listBackups(game, "tr").length, 2, "two backups, the level's name in any case");

        const first = restoreLatest(game, "TR");
        assert.ok(first);
        assert.equal(fs.readFileSync(qb, "utf8"), "bake 1");
        assert.equal(fs.readFileSync(scripts, "utf8"), "scripts 1");
        assert.equal(first.left, 1);
        assert.deepEqual(first.deleted, []);

        const second = restoreLatest(game, "TR");
        assert.ok(second);
        assert.equal(fs.readFileSync(qb, "utf8"), "original");
        assert.equal(fs.existsSync(scripts), false, "the file install 1 made is gone");
        assert.deepEqual(second.deleted, [path.resolve(scripts)]);
        assert.equal(second.left, 0);

        assert.equal(restoreLatest(game, "TR"), null, "nothing left to restore");
        assert.equal(fs.readFileSync(qb, "utf8"), "original");
    } finally {
        fs.rmSync(game, { recursive: true, force: true });
    }
});
