// What the level converters (tools/thps4_levels, tools/thug1_levels) share besides the formats: compiling NeverScript,
// writing a converted level's files (each read back first), the level structure's lighting keys, and the self-check's
// THUG2 round trips. (TypeScript twin of common.py.)

import { spawnSync } from "node:child_process";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { decodeCp1252, Float, print, pyExit, pyRound, roundHalfEven, writeTextPy } from "../lib/py.ts";
import { FormatError } from "./binio.ts";
import * as prx from "./prx.ts";
import type { Struct } from "./qb.ts";
import * as tex from "./tex.ts";
import * as thug2Col from "./thug2_col.ts";
import * as thug2Scene from "./thug2_scene.ts";

export const HERE = path.dirname(fileURLToPath(import.meta.url));
export const REPO = path.dirname(path.dirname(HERE));
export const NS = path.join(REPO, "out", "neverscript", "ns.exe");

// A process's output as Python's subprocess.run(..., capture_output=True, text=True) gives it: the locale's code page
// (cp1252), "\r\n" read as "\n".
export function textOutput(data: Buffer | null | undefined): string {
    return data ? decodeCp1252(data).replace(/\r\n?/g, "\n") : "";
}

export function nsCompile(ns: string, text: string, target: string): void {
    const work = fs.mkdtempSync(path.join(os.tmpdir(), "tmp"));
    try {
        const source = path.join(work, "source.ns");
        // (written as Python's text mode did: CRLF line ends on Windows)
        writeTextPy(source, text);
        const result = spawnSync(ns, ["-c", source, "-o", target, "-targetGame", "thug2"]);
        if (result.error) throw result.error;
        if (result.status !== 0 || !fs.existsSync(target)) {
            pyExit(`ns failed on ${target}:\n${textOutput(result.stdout)}${textOutput(result.stderr)}`);
        }
    } finally {
        fs.rmSync(work, { recursive: true, force: true });
    }
}

export function writeLevel(
    outDir: string,
    outName: string,
    scene: thug2Scene.Scene,
    textures: tex.TexFile,
    col: thug2Col.ColFile | null,
): void {
    // <outDir>\<outName>.scn.xbx / .tex.xbx / .col.xbx (none when col is null: THUG1's skies have no collision,
    // like THUG2's), each read back by THUG2's readers first.
    fs.mkdirSync(outDir, { recursive: true });
    let data = thug2Scene.writeScene(scene);
    thug2Scene.readScene(data, outName);
    fs.writeFileSync(path.join(outDir, `${outName}.scn.xbx`), data);
    tex.checkLoadable(textures, outName);
    data = tex.writeTex(textures);
    tex.readTex(data, outName);
    fs.writeFileSync(path.join(outDir, `${outName}.tex.xbx`), data);
    if (col === null) return;
    data = thug2Col.writeCol(col);
    thug2Col.readCol(data, outName);
    fs.writeFileSync(path.join(outDir, `${outName}.col.xbx`), data);
}

export function reportLevel(
    report: string[],
    outName: string,
    scene: thug2Scene.Scene,
    textures: tex.TexFile,
    col: thug2Col.ColFile | null,
    warnings: readonly string[],
    converted = 0,
): void {
    const meshes = scene.sectors.reduce((n, s) => n + s.meshes.length, 0);
    report.push(
        `${outName}: ${scene.materials.length} materials, ${scene.sectors.length} sectors (${meshes} meshes), ` +
            `${textures.textures.length} textures (${converted} depalettised), ${col ? col.objects.length : 0} collision objects`,
    );
    for (const warning of warnings.slice(0, 10)) report.push(`  warning: ${warning}`);
    if (warnings.length > 10) report.push(`  (${warnings.length - 10} more warnings)`);
}

const isNumber = (value: unknown): value is number | Float => typeof value === "number" || value instanceof Float;
const numberOf = (value: number | Float): number => (value instanceof Float ? value.value : value);

// A level structure's lighting (ambient + two directional lights) in THUG2's keys (each mod factor as a lo / hi
// pair, as THUG2's own levels have them): ints for the colours and angles, Floats for the mod factors.
export function lighting(struct: Struct): Record<string, number | Float> {
    const out: Record<string, number | Float> = {};
    for (const key of [
        "ambient_red",
        "ambient_green",
        "ambient_blue",
        "heading_0",
        "pitch_0",
        "red_0",
        "green_0",
        "blue_0",
        "heading_1",
        "pitch_1",
        "red_1",
        "green_1",
        "blue_1",
    ]) {
        const value = struct.get(key);
        // (Python's round() of an int is the int; of a float, halves to even)
        if (isNumber(value)) out[key] = value instanceof Float ? roundHalfEven(value.value) : value;
    }
    for (const key of ["ambient_mod_factor", "mod_factor_0", "mod_factor_1"]) {
        const value = struct.get(key);
        if (isNumber(value)) {
            out[`${key}_lo`] = new Float(pyRound(numberOf(value), 4));
            out[`${key}_hi`] = new Float(pyRound(numberOf(value), 4));
        } else {
            for (const suffix of ["_lo", "_hi"]) {
                const v = struct.get(key + suffix);
                if (isNumber(v)) out[key + suffix] = new Float(pyRound(numberOf(v), 4));
            }
        }
    }
    return out;
}

export function thug2RoundTrips(pre: string, archives: readonly string[]): [number, number] {
    // THUG2's scene / collision / texture files in the archives read and written again: they must come out
    // byte-identical. Returns [files checked, failures].
    let checked = 0;
    let failures = 0;
    for (const archive of archives) {
        for (const [name, data] of prx.entries(path.join(pre, archive))) {
            const low = name.toLowerCase();
            let again: Buffer;
            try {
                if (low.endsWith(".scn.xbx")) again = thug2Scene.writeScene(thug2Scene.readScene(data, name));
                else if (low.endsWith(".col.xbx")) again = thug2Col.writeCol(thug2Col.readCol(data, name));
                else if (low.endsWith(".tex.xbx")) again = tex.writeTex(tex.readTex(data, name));
                else continue;
            } catch (e) {
                if (!(e instanceof FormatError)) throw e;
                print(`FAIL ${name}: ${e.message}`);
                failures += 1;
                continue;
            }
            checked += 1;
            const same = again.equals(data);
            print(`${same ? "ok  " : "FAIL"} ${name} (${data.length} bytes) read and written again`);
            failures += same ? 0 : 1;
        }
    }
    return [checked, failures];
}
