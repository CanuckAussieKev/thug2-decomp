import assert from "node:assert/strict";
import { test } from "node:test";
import { bundledFiles, NODE_VERSION, NODE_ZIP } from "./editor_tools.ts";

test("the editor's tools folder leaves out the submodules' tests and settings", () => {
    assert.deepEqual(
        bundledFiles([
            "run.ts",
            "menu_level/make_menu_level.ts",
            "menu_level/menu_level.test.ts",
            "deps.test.ts",
            "tsconfig.json",
            "lint.biome.json",
            ".github/workflows/ci.yml",
            ".gitignore",
            "package-lock.json",
            "",
        ]),
        ["menu_level/make_menu_level.ts", "package-lock.json", "run.ts"],
    );
});

test("Node is a pinned 24 release for Windows x64", () => {
    assert.match(NODE_VERSION, /^24\.\d+\.\d+$/);
    assert.equal(NODE_ZIP, `node-v${NODE_VERSION}-win-x64.zip`);
});
