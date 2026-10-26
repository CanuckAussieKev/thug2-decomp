// NOT ORIGINAL - tests of tools/pipeline/game/edit_level.ts's pure parts (no game data).

import assert from "node:assert/strict";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { test } from "node:test";
import { findExe, findLevel, formatList, type Level, resolveGameFolder } from "./edit_level.ts";

const LEVELS: Level[] = [
    { load: "BO", name: "Boston", game: "THUG2", installed: true },
    { load: "Default", name: "Default", game: "THUG2", installed: false },
    { load: "foun", name: "Foundry", game: "THPS3", installed: true },
    { load: "z_ma", name: "The Mall", game: "THAW", installed: true },
    { load: "TestLevel", name: "TestLevel", game: "THUG2", installed: true },
];

test("findLevel takes a load name with or without Load_, any case", () => {
    assert.equal(findLevel(LEVELS, "foun")?.name, "Foundry");
    assert.equal(findLevel(LEVELS, "Load_BO")?.name, "Boston");
    assert.equal(findLevel(LEVELS, "load_z_MA")?.name, "The Mall");
    assert.equal(findLevel(LEVELS, "nowhere"), null);
    assert.equal(findLevel(LEVELS, "Load_"), null);
});

test("formatList groups by game, THUG2 first, and leaves uninstalled levels out", () => {
    assert.deepEqual(formatList(LEVELS), [
        "THUG2 (its own, and the overlay's) - 2:",
        "  BO         Boston",
        "  TestLevel  TestLevel",
        "THPS3 (ported) - 1:",
        "  foun  Foundry",
        "THAW (ported) - 1:",
        "  z_ma  The Mall",
        "(not installed - no scene file - left out: Default)",
    ]);
});

test("resolveGameFolder steps into a stock install's Game folder, and keeps a folder that isn't THUG2's", () => {
    const top = fs.mkdtempSync(path.join(os.tmpdir(), "edit-level-"));
    try {
        fs.mkdirSync(path.join(top, "Stock", "Game"), { recursive: true });
        fs.writeFileSync(path.join(top, "Stock", "Game", "THUG2.exe"), "");
        fs.mkdirSync(path.join(top, "Flat"));
        fs.writeFileSync(path.join(top, "Flat", "THUG2.exe"), "");
        assert.equal(resolveGameFolder(path.join(top, "Stock")), path.join(top, "Stock", "Game"));
        assert.equal(resolveGameFolder(`${path.join(top, "Stock")}\\`), path.join(top, "Stock", "Game"));
        assert.equal(resolveGameFolder(path.join(top, "Flat")), path.join(top, "Flat"));
        assert.equal(resolveGameFolder(`${path.join(top, "None")}\\`), path.join(top, "None"));
    } finally {
        fs.rmSync(top, { recursive: true });
    }
});

test("findExe: THUG2.exe", () => {
    const files = new Set([path.join("C:\\Games\\THUG2-1", "THUG2.exe")]);
    const exists = (p: string): boolean => files.has(p);
    assert.equal(findExe("C:\\Games\\THUG2-1", exists), path.join("C:\\Games\\THUG2-1", "THUG2.exe"));
    assert.equal(findExe("C:\\Games\\Nothing", exists), null);
});
