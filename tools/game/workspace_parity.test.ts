// NOT ORIGINAL - tests of tools/game/workspace_parity.ts's pure parts (no game folders).

import assert from "node:assert/strict";
import { test } from "node:test";
import {
    capabilities,
    diffListings,
    getIniValue,
    type Listing,
    parseSidecarSettings,
    placeOf,
    removeIniValue,
    settingsDiff,
    settingsFor,
} from "./workspace_parity.ts";

const listing = (entries: [string, number, number][]): Listing =>
    new Map(entries.map(([k, size, mtime]) => [k, { size, mtime }]));

test("diffListings: missing, different (size or time over 2 s), extra - paths case-insensitive", () => {
    const from = listing([
        ["Data\\a.pre", 10, 1000],
        ["Data\\b.pre", 10, 1000],
        ["thug2-decomp-relit\\34e098ec.relit", 5, 1000],
        ["SDL3.dll", 7, 1000],
    ]);
    const to = listing([
        ["data\\A.pre", 10, 2500],
        ["Data\\b.pre", 11, 1000],
        ["SDL3.dll", 7, 4000],
        ["thug2-decomp-relit\\old.relit", 1, 0],
    ]);
    const d = diffListings(from, to);
    assert.deepEqual(d.missing, ["thug2-decomp-relit\\34e098ec.relit"]);
    assert.deepEqual(d.different, ["Data\\b.pre", "SDL3.dll"]);
    assert.deepEqual(d.extra, ["thug2-decomp-relit\\old.relit"]);
    assert.equal(placeOf("Data\\levels\\BO\\BO.relief"), "Data");
    assert.equal(placeOf("SDL3.dll"), "SDL3.dll");
});

test("capabilities reads what the game's start-up line finds", () => {
    const found = listing([
        ["Data\\levels\\BO\\BO.tex.Xbx.relief", 1, 0],
        ["thug2-decomp-data\\levels\\BO\\x.tex.Xbx.pbr", 1, 0],
        ["thug2-decomp-materials.t2dm", 1, 0],
        ["thug2-decomp-textures\\seedvr2\\a.dds", 1, 0],
        ["thug2-decomp-textures\\pbrify\\a.dds", 1, 0],
    ]);
    assert.equal(
        capabilities(found),
        "surface maps found, material library found, texture sets pbrify seedvr2, building relief files found",
    );
    assert.equal(
        capabilities(listing([])),
        "surface maps none, material library none, texture sets none, building relief files none",
    );
});

test("parseSidecarSettings: values, defaults, (empty), arrows; not the running lines", () => {
    const text = [
        "running texture set:                             seedvr2",
        "Graphics texture_set:                            seedvr2",
        "Graphics rt_tonemap:                             2 (default)",
        "Graphics rt_relit:                               1 -> 1 (On - needed by Time of day)",
        "Graphics frame_generation_provider:              auto (default) -> not in effect (Needs Frame generation)",
        "Graphics rt_grade_lut:                           (empty) (default)",
        "Graphics tod_levels:                             (empty)",
        "features hd_ui:                                  1",
        "debug collision_events:                          1",
    ].join("\r\n");
    assert.deepEqual(parseSidecarSettings(text), [
        { section: "graphics", key: "texture_set", value: "seedvr2" },
        { section: "graphics", key: "rt_tonemap", value: null },
        { section: "graphics", key: "rt_relit", value: "1" },
        { section: "graphics", key: "frame_generation_provider", value: null },
        { section: "graphics", key: "rt_grade_lut", value: null },
        { section: "graphics", key: "tod_levels", value: "" },
        { section: "features", key: "hd_ui", value: "1" },
        { section: "debug", key: "collision_events", value: "1" },
    ]);
});

test("settingsFor: the main ini taken whole, the workspace's window and test keys kept", () => {
    const ws =
        "[features]\r\nautomation=1\r\nstart_level=sch\r\n[Graphics]\r\nResolutionX=1280\r\nResolutionY=720\r\nrt_tonemap=0\r\nWindowDisplay=2\r\n";
    const main =
        "[features]\r\nhd_ui=1\r\n[Graphics]\r\nResolutionX=2560\r\nResolutionY=1440\r\nBorderless=1\r\ntod=night\r\n";
    const out = settingsFor(ws, { ini: main });
    assert.equal(getIniValue(out, "Graphics", "ResolutionX"), "1280");
    assert.equal(getIniValue(out, "Graphics", "WindowDisplay"), "2");
    assert.equal(getIniValue(out, "Graphics", "Borderless"), null);
    assert.equal(getIniValue(out, "Graphics", "rt_tonemap"), null);
    assert.equal(getIniValue(out, "Graphics", "tod"), "night");
    assert.equal(getIniValue(out, "features", "start_level"), "sch");
    assert.equal(getIniValue(out, "features", "automation"), "1");
    assert.equal(getIniValue(out, "features", "mute_audio"), "1");
    assert.equal(getIniValue(out, "features", "hd_ui"), "1");
});

test("settingsFor: a sidecar's settings over the workspace's ini - defaults taken out", () => {
    const ws = "[Graphics]\r\nResolutionX=1280\r\nrt_tonemap=0\r\nclutter=1\r\n";
    const sidecar = parseSidecarSettings(
        ["Graphics ResolutionX:   2560", "Graphics rt_tonemap:   2 (default)", "Graphics clutter:   0"].join("\n"),
    );
    const out = settingsFor(ws, { sidecar });
    assert.equal(getIniValue(out, "Graphics", "ResolutionX"), "1280");
    assert.equal(getIniValue(out, "Graphics", "rt_tonemap"), null);
    assert.equal(getIniValue(out, "Graphics", "clutter"), "0");
    assert.equal(removeIniValue("[a]\nk=1\n[b]\nk=2", "b", "K"), "[a]\nk=1\n[b]");
});

test("settingsDiff names the keys that differ, not the workspace's own", () => {
    const a = "[Graphics]\nResolutionX=1280\nrt_fog=1\n[features]\nautomation=1\nmenu_zoom=79\n";
    const b = "[Graphics]\nResolutionX=2560\nrt_fog=0\n[features]\n";
    assert.deepEqual(settingsDiff(a, b), ["features menu_zoom: 79 -> -", "graphics rt_fog: 1 -> 0"]);
});
