// NOT ORIGINAL - the CMake library check (tools/cmake_layers.ts): its reading of CMake files, its rules on a small map,
// and the repository's CMake files against tools/layers.json.

import assert from "node:assert/strict";
import { test } from "node:test";
import { cppFilesOf, parseCMake, problems, projectTargets, type Target, targetsOf } from "./cmake_layers.ts";
import { type LayerMap, loadMap, readBaseline } from "./layers.ts";

test("CMake commands are read with comments, quotes and nested parentheses", () => {
    const commands = parseCMake(
        '# a comment (with parentheses)\nadd_library(x STATIC a.cpp # b.cpp\n  "c d.cpp")\nif((A) AND B)\nendif()\n',
    );
    assert.deepEqual(commands, [
        { name: "add_library", args: ["x", "STATIC", "a.cpp", "c d.cpp"] },
        { name: "if", args: ["(A)", "AND", "B"] },
        { name: "endif", args: [] },
    ]);
});

test("targets take their sources from their folder and collect their links", () => {
    const [target] = targetsOf(
        "add_library(thug2_engine STATIC\n    gfx/a.cpp\n)\ntarget_link_libraries(thug2_engine PUBLIC thug2_gpu PRIVATE imgui)\n",
        "src/engine/CMakeLists.txt",
    );
    assert.deepEqual(target, {
        name: "thug2_engine",
        kind: "library",
        file: "src/engine/CMakeLists.txt",
        sources: ["src/engine/gfx/a.cpp"],
        links: ["thug2_gpu", "imgui"],
    });
});

// A small map: base <- engine <- game, and two siblings above the game.
const MAP: LayerMap = {
    libraries: {
        base: { folders: ["src/base/"], allowed: [] },
        engine: { folders: ["src/engine/"], allowed: ["base"] },
        game: { folders: ["src/game/"], allowed: ["base", "engine"] },
        editor: { folders: ["src/editor/"], allowed: ["base", "engine", "game"] },
        frontend: { folders: ["src/frontend/"], allowed: ["base", "engine", "game"] },
        app: { folders: ["src/app/"], allowed: ["base", "engine", "game", "editor", "frontend"] },
    },
};

function library(name: string, links: string[]): Target {
    const folder = name.replace("thug2_", "");
    return { name, kind: "library", file: `src/${folder}/CMakeLists.txt`, sources: [`src/${folder}/a.cpp`], links };
}

function project(overrides: Record<string, string[]> = {}): Target[] {
    const links: Record<string, string[]> = {
        thug2_base: [],
        thug2_engine: ["thug2_base"],
        thug2_game: ["thug2_engine"],
        thug2_editor: ["thug2_game"],
        thug2_frontend: ["thug2_game"],
        ...overrides,
    };
    const targets = Object.entries(links).map(([name, to]) => library(name, to));
    targets.push({
        name: "thug2",
        kind: "executable",
        file: "src/app/CMakeLists.txt",
        sources: ["src/app/a.cpp"],
        links: ["thug2_editor", "thug2_frontend"],
    });
    return targets;
}

const CPP = [
    "src/app/a.cpp",
    "src/base/a.cpp",
    "src/editor/a.cpp",
    "src/engine/a.cpp",
    "src/frontend/a.cpp",
    "src/game/a.cpp",
];

test("links that mirror the map pass", () => {
    assert.deepEqual(problems(MAP, project(), CPP, []), []);
});

test("an upward link and a library not reached fail", () => {
    assert.deepEqual(problems(MAP, project({ thug2_engine: ["thug2_base", "thug2_game"] }), CPP, []), [
        "thug2_engine links thug2_game: engine may not include game",
        "thug2_engine reaches thug2_game, which engine may not include",
    ]);
    // (and so do the libraries above it, which reach the engine through the game)
    assert.deepEqual(problems(MAP, project({ thug2_game: ["thug2_base"] }), CPP, []), [
        "thug2_game doesn't reach thug2_engine, which game may include",
        "thug2_editor doesn't reach thug2_engine, which editor may include",
        "thug2_frontend doesn't reach thug2_engine, which frontend may include",
    ]);
});

test("sibling links follow the include baseline both ways", () => {
    const baseline = ["src/editor/a.cpp -> src/frontend/b.h"];
    // a baseline pair must be linked
    assert.deepEqual(problems(MAP, project(), CPP, baseline), [
        "thug2_editor must link thug2_frontend: tools/layers_baseline.txt has editor -> frontend lines",
    ]);
    assert.deepEqual(problems(MAP, project({ thug2_editor: ["thug2_game", "thug2_frontend"] }), CPP, baseline), []);
    // and a sibling link with no baseline line fails
    assert.deepEqual(problems(MAP, project({ thug2_frontend: ["thug2_game", "thug2_editor"] }), CPP, []), [
        "thug2_frontend links thug2_editor: frontend may not include editor",
        "thug2_frontend reaches thug2_editor, which frontend may not include",
    ]);
});

test("every source is in its target's library and every .cpp is compiled once", () => {
    const targets = project();
    (targets[1] as Target).sources.push("src/game/a.cpp");
    assert.deepEqual(problems(MAP, targets, [...CPP, "src/game/b.cpp"], []), [
        "thug2_engine: src/game/a.cpp is not in engine's folders",
        "src/game/a.cpp: compiled by thug2_engine, thug2_game",
        "src/game/b.cpp: compiled by no target",
    ]);
});

test("the repository's CMake targets mirror tools/layers.json", () => {
    assert.deepEqual(problems(loadMap(), projectTargets(), cppFilesOf(), readBaseline()), []);
});
