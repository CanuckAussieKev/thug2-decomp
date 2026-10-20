// NOT ORIGINAL - the TypeScript half of tools/level_port's parity driver (driver.py: the commands, the same output).
// prepare (TypeScript only: the geometry cases' inputs, read by both halves) uses tools/thps4_levels' and
// tools/thug1_levels' readers.
//
//   node tools/parity/level_port/driver.ts <command> <out> [inputs...]

import * as fs from "node:fs";
import * as path from "node:path";
import { FormatError } from "../../level_port/binio.ts";
import * as colCheck from "../../level_port/col_check.ts";
import * as common from "../../level_port/common.ts";
import * as geometry from "../../level_port/geometry.ts";
import * as nodes from "../../level_port/nodes.ts";
import * as pedsCars from "../../level_port/peds_cars.ts";
import * as portedLevels from "../../level_port/ported_levels.ts";
import * as prx from "../../level_port/prx.ts";
import * as qb from "../../level_port/qb.ts";
import * as scriptPort from "../../level_port/script_port.ts";
import * as tex from "../../level_port/tex.ts";
import * as thug2Col from "../../level_port/thug2_col.ts";
import * as thug2Scene from "../../level_port/thug2_scene.ts";
import {
    Float,
    hex8,
    isAlnumChar,
    print,
    pyFloatRepr,
    pyJsonDumps,
    pyJsonLoads,
    pyRepr,
    pyTuple,
    sortedNumbers,
} from "../../lib/py.ts";
import * as thps4 from "../../thps4_levels/thps4.ts";
import * as thug1 from "../../thug1_levels/thug1.ts";
import { blob, canon, digest, qcanon } from "../canon.ts";

const NS = "C:\\Dev\\thug2-decomp\\out\\neverscript\\ns.exe";

function write(out: string, name: string, text: string): void {
    const file = path.join(out, name);
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, text, "utf-8");
}

function errorLine(e: unknown): string {
    return e instanceof FormatError ? `error FormatError ${e.message}` : "error";
}

function* inputs(paths: readonly string[], suffixes: readonly string[]): Generator<[string, Buffer]> {
    const ends = (n: string): boolean => suffixes.some((s) => n.toLowerCase().endsWith(s));
    for (const p of paths) {
        const low = p.toLowerCase();
        if (low.endsWith(".prx") || low.endsWith(".pre")) {
            for (const [name, data] of prx.entries(p, ends)) yield [`${path.win32.basename(p)}|${name}`, data];
        } else if (ends(low)) {
            yield [path.win32.basename(p), fs.readFileSync(p)];
        }
    }
}

// A compiled .qb as text without NeverScript's own nondeterminism (driver.py qb_norm).
function qbNorm(data: Buffer): string {
    const toks = [...qb.tokens(data, true)];
    const first = toks.find(([, t]) => t === 0x2b)?.[0] ?? data.length;
    const names = toks
        .filter(([, t]) => t === 0x2b)
        .map(([, , p]) => p.toString("hex"))
        .sort();
    const rest = toks
        .filter(([off, t]) => off >= first && t !== 0x2b)
        .map(([, t, p]) => Buffer.from([t]).toString("hex") + p.toString("hex"));
    return `${data.subarray(0, first).toString("hex")}\n${names.join("\n")}\n${rest.join("")}\n`;
}

const safe = (label: string): string => [...label].map((c) => (isAlnumChar(c) || "._-".includes(c) ? c : "_")).join("");
const flag = (b: boolean): number => (b ? 1 : 0);
const sortedPairs = <T>(pairs: [string, T][]): [string, T][] =>
    pairs.sort((a, b) => (a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0)).map((p) => pyTuple(p));

// --- archives, scene / collision / texture files --------------------------------------------------------------------

function cmdArchives(out: string, paths: readonly string[]): void {
    const lines: string[] = [];
    for (const p of paths) {
        const files = [...prx.entries(p)];
        lines.push(`archive ${path.win32.basename(p)} ${files.length}`);
        for (const [name, data] of files) lines.push(`${name} ${blob(data)}`);
        const found = files.length
            ? prx.find(p, files[Math.floor(files.length / 2)]![0].toUpperCase().replaceAll("\\", "/"))
            : null;
        lines.push(`find ${found !== null ? blob(found) : "None"}`);
    }
    write(out, "archives.txt", `${lines.join("\n")}\n`);
}

function cmdFiles(out: string, paths: readonly string[]): void {
    const lines: string[] = [];
    for (const [label, data] of inputs(paths, [
        ".scn.xbx",
        ".mdl.xbx",
        ".skin.xbx",
        ".col.xbx",
        ".tex.xbx",
        "tex.dat",
    ])) {
        const low = label.toLowerCase();
        try {
            if (low.endsWith(".col.xbx")) {
                const col = thug2Col.readCol(data, label, [9, 10]);
                const again = thug2Col.writeCol(col);
                const [faces, missing, objects] = colCheck.check(col);
                lines.push(
                    `${label} col ${digest(canon(col))} ${blob(again)} same=${flag(again.equals(data))} unreachable=${faces}/${missing}/${objects}`,
                );
            } else if (low.endsWith("tex.xbx") || low.endsWith("tex.dat")) {
                const t = tex.readTex(data, label);
                const again = tex.writeTex(t);
                let line = `${label} tex ${digest(canon(t))} ${blob(again)} same=${flag(again.equals(data))}`;
                try {
                    const changed = tex.depalettise(t);
                    tex.checkLoadable(t, label);
                    line += ` depal=${changed} ${blob(tex.writeTex(t))}`;
                } catch (e) {
                    if (!(e instanceof FormatError)) throw e;
                    line += ` depal ${errorLine(e)}`;
                }
                lines.push(line);
            } else {
                const s = thug2Scene.readScene(data, label);
                const again = thug2Scene.writeScene(s);
                lines.push(`${label} scn ${digest(canon(s))} ${blob(again)} same=${flag(again.equals(data))}`);
            }
        } catch (e) {
            if (!(e instanceof FormatError)) throw e;
            lines.push(`${label} ${errorLine(e)}`);
        }
    }
    write(out, "files.txt", `${lines.join("\n")}\n`);
}

// --- qb ---------------------------------------------------------------------------------------------------------------

function cmdQb(out: string, weightsArg: string, paths: readonly string[]): void {
    const weights = weightsArg === "1";
    const lines: string[] = [];
    for (const [label, data] of inputs(paths, [".qb"])) {
        lines.push(`file ${label}`);
        try {
            const toks = [...qb.tokens(data, weights)];
            lines.push(`tokens ${toks.length} ${digest(toks.map(([o, t, p]) => [o, t, blob(p)]))}`);
        } catch {
            lines.push("tokens error");
        }
        let q: qb.QbFile | null = null;
        try {
            q = new qb.QbFile(data, label, weights);
        } catch (e) {
            lines.push(`qbfile ${errorLine(e)}`);
        }
        if (q !== null) {
            lines.push(`names ${q.names.size} ${digest(canon(q.names))}`);
            lines.push(`globals ${q.globals.length} ${digest(q.globals.map(([k, v]) => [qcanon(k), qcanon(v)]))}`);
            lines.push(
                `scripts ${q.scripts.size} ${digest(
                    new Map([...q.scripts].map(([k, v]) => [hex8(k), v.map((st) => st.map(([t, p]) => [t, blob(p)]))])),
                )}`,
            );
            const calls = new Map<string, unknown>();
            for (const k of q.scripts.keys())
                calls.set(
                    hex8(k),
                    q.calls(k).map(([c, line]) => [hex8(c), line.length]),
                );
            lines.push(`calls ${digest(calls)}`);
            const text: string[] = [];
            for (const [key, value] of q.globals) {
                try {
                    text.push(`${qb.nameText(key.value, q.names)} = ${qb.toNs(value, q.names)}`);
                } catch (e) {
                    if (e instanceof TypeError) text.push(`# ${hex8(key.value)}: no NeverScript form`);
                    else if (e instanceof RangeError && e.message.includes("'ascii' codec")) {
                        text.push(`# ${hex8(key.value)}: not ASCII`);
                    } else throw e;
                }
            }
            write(out, path.join("ns", `${safe(label)}.ns`), `${text.join("\n")}\n`);
            for (const name of ["NodeArray", "TriggerScripts", "Level_AP"]) {
                const v = q.get(name);
                lines.push(`get ${name} ${v !== undefined ? digest(qcanon(v)) : "None"}`);
            }
        }
        try {
            const s = new scriptPort.Source(data, label, weights);
            const dumped = new Map<string, unknown>();
            for (const [k, body] of s.scripts) {
                const stmts = scriptPort.statements(body);
                dumped.set(hex8(k), [
                    ...stmts.map((st) => st.map((t) => [t.t, t.i, t.targets, t.weights])),
                    ...stmts.map((st) => scriptPort.argsOf(st.slice(1)).map(([k2, a]) => [k2, a.t, a.i])),
                    body.filter((t) => scriptPort.RANDOMS.includes(t.t)).map((t) => scriptPort.randomEnd(t, body)),
                ]);
            }
            lines.push(
                `source ${s.scripts.size} ${digest(dumped)} globals ${s.globals.size} ${digest(
                    new Map([...s.globals].map(([k, v]) => [hex8(k), qcanon(v)])),
                )}`,
            );
        } catch (e) {
            lines.push(`source ${errorLine(e)}`);
        }
    }
    write(out, "qb.txt", `${lines.join("\n")}\n`);
}

// --- THUG2's commands --------------------------------------------------------------------------------------------------

function cmdThug2(out: string, game: string): void {
    const t = new scriptPort.Thug2(game);
    const members = sortedNumbers(t.members.keys());
    const lines = [
        `cfuncs ${t.cfuncs.size} ${digest(sortedNumbers(t.cfuncs))}`,
        `members ${t.members.size} ${digest(members.map((k) => [k, t.members.get(k)]))}`,
        `scripts ${t.scripts.size} ${digest(sortedNumbers(t.scripts))}`,
        `globals ${t.globals.size} ${digest(sortedNumbers(t.globals))}`,
        `anims ${t.anims.size} ${digest(sortedNumbers(t.anims))}`,
        `bodies ${digest(new Map([...t.bodies].map(([k, v]) => [hex8(k), sortedNumbers(v)])))}`,
    ];
    const needs = new Map<string, string[]>();
    for (const name of sortedNumbers(t.scripts)) needs.set(hex8(name), [...t.scriptNeeds(name)].sort());
    lines.push(`needs ${digest(needs)}`);
    lines.push(`component ${pyRepr(members.slice(0, 50).map((n) => t.component(n)))}`);
    write(out, "thug2.txt", `${lines.join("\n")}\n`);
}

// --- node arrays and the script translator -------------------------------------------------------------------------

function nodeArrayOf(q: qb.QbFile, level?: string): qb.QbValue[] {
    let array = q.get("NodeArray");
    if (array === undefined && level !== undefined) array = q.get(`${level}_NodeArray`);
    return Array.isArray(array) ? array : [];
}

function contextOf(cls: string, game: string): scriptPort.Context {
    if (cls === "levelgeometry" || cls === "railnode" || cls === "restart") return scriptPort.SKATER;
    if (cls === "gameobject" || cls === "levelobject") return scriptPort.OBJECT;
    if (cls === "pedestrian") return game === "thug1" ? scriptPort.PED : scriptPort.OBJECT;
    if (cls === "vehicle") return game === "thug1" ? scriptPort.VEHICLE : scriptPort.OBJECT;
    return scriptPort.NONE;
}

const nameOf = (n: qb.Struct): number | null => {
    const v = n.get("Name");
    return v instanceof qb.Name ? v.value : null;
};

function killPairs(kills: ReadonlyMap<number, string>): [string, string][] {
    return sortedPairs([...kills].map(([k, v]) => [hex8(k), v]));
}

function cmdTranslate(out: string, gameDir: string, game: string, level: string, shared: readonly string[]): void {
    const weights = game === "thug1";
    let levelLabel: string;
    let levelData: Buffer;
    if (level.includes("|")) {
        const bar = level.indexOf("|");
        levelLabel = level.slice(bar + 1);
        levelData = prx.find(level.slice(0, bar), levelLabel)!;
    } else {
        levelLabel = path.win32.basename(level);
        levelData = fs.readFileSync(level);
    }
    const q = new qb.QbFile(levelData, levelLabel, weights);
    const sharedSources: scriptPort.Source[] = [];
    const names = new Map<number, string>();
    for (const [label, data] of inputs(shared, [".qb"])) {
        try {
            const last = label.replaceAll("/", "\\").split("|").at(-1)!;
            sharedSources.push(new scriptPort.Source(data, path.win32.basename(last).toLowerCase(), weights));
        } catch {}
    }
    for (const s of sharedSources) for (const [k, v] of s.names) names.set(k, v);
    for (const [k, v] of q.names) names.set(k, v);
    let array = nodeArrayOf(q);
    const lines = [`array ${array.length}`];
    let kills: Map<number, string>;
    if (game === "thug1") {
        array = nodes.expandIncludes(array, new Map(q.globals.map(([k, v]) => [k.value, v])));
        const restarts = new Set<number>();
        for (const n of array) {
            if (n instanceof qb.Struct && nodes.classOf(n, names) === "restart" && nameOf(n) !== null)
                restarts.add(nameOf(n)!);
        }
        kills = nodes.killCommands([q], restarts, names);
    } else {
        kills = nodes.killScripts(q, null);
    }
    lines.push(`kills ${pyRepr(killPairs(kills))}`);
    const [converted, stats] = nodes.convertNodes(array, names, kills, game === "thug1" ? ["Pos"] : []);
    lines.push(
        `converted ${converted.length} ${digest(converted.map((n) => qcanon(n)))} ${pyRepr(sortedPairs([...stats]))}`,
    );
    kills = nodes.addKillRestarts(converted, kills, names);
    lines.push(`kill restarts ${pyRepr(killPairs(kills))}`);
    lines.push(`kill triggers ${pyRepr(sortedNumbers(nodes.killTriggerNames(converted, kills)))}`);
    // (typos in some rails' terrains: fixTerrains' nearest names)
    const rails = converted.filter((n) => nodes.classOf(n, names) === "railnode");
    const terrains = [...nodes.THUG2_TERRAINS].sort();
    rails
        .filter((_, i) => i % 7 === 0)
        .forEach((node, k) => {
            const t = terrains[k % terrains.length]!;
            const typo = [
                `TERRAIN_${t.slice(0, -1)}X`,
                `TERRAIN_${t.slice(1)}`,
                `terrain_${t.toLowerCase()}s`,
                `TERRAIN_ZZZ${t.slice(0, 3)}`,
            ][k % 4]!;
            const value = qb.checksum(typo);
            names.set(value, typo);
            node.entries = node.entries.map(([key, v]) => [
                key,
                key !== null && key.value === qb.checksum("TerrainType") ? new qb.Name(value) : v,
            ]);
        });
    const text = nodes.nodeArrayNs("Parity_NodeArray", converted, names, kills);
    write(out, "nodes.ns", text);
    const sectors = new Set<number>();
    for (const n of converted) {
        const name = nameOf(n);
        if (name !== null && nodes.classOf(n, names) === "levelgeometry" && name % 3) sectors.add(name);
    }
    const [stripped, cleared] = nodes.stripMissingTrickObjects(text, sectors);
    write(out, "stripped.ns", stripped);
    lines.push(`stripped ${cleared}`);
    const allNames = new Set<number>();
    for (const n of array) if (n instanceof qb.Struct && nameOf(n) !== null) allNames.add(nameOf(n)!);
    const nodeNames = new Set<number>();
    for (const n of converted) if (nameOf(n) !== null) nodeNames.add(nameOf(n)!);
    const levelSource = new scriptPort.Source(levelData, "level", weights);
    const sources = [...sharedSources, levelSource];
    const thug2 = new scriptPort.Thug2(gameDir);
    const roots: [number, scriptPort.Context][] = [];
    const objects = new Map<number, scriptPort.ObjectContext>();
    for (const n of array) {
        if (!(n instanceof qb.Struct)) continue;
        const cls = nodes.classOf(n, names);
        const script = n.get("TriggerScript");
        const name = n.get("Name");
        const context = contextOf(cls, game);
        if (name instanceof qb.Name && ["gameobject", "levelobject", "pedestrian", "vehicle"].includes(cls)) {
            objects.set(name.value, game === "thps4" ? [context, n.hasFlag("CreatedAtStart")] : context);
        }
        if (script instanceof qb.Name && !kills.has(script.value)) roots.push([script.value, context]);
    }
    const allowed = new Set([...scriptPort.DEFAULT_SOURCES, "level"]);
    let tr: scriptPort.Translator;
    if (game === "thps3") {
        const undrawn = [...nodeNames].filter((v) => v % 5 === 0);
        tr = new scriptPort.Translator(sources, thug2, (label) => allowed.has(label), nodeNames, allNames, names, {
            provided: kills.keys(),
            inheritParams: true,
            undrawn,
        });
    } else if (game === "thps4") {
        tr = new scriptPort.Translator(sources, thug2, (label) => !label.includes("goal"), nodeNames, allNames, names, {
            provided: kills.keys(),
            full: true,
            objects,
        });
    } else {
        tr = new scriptPort.Translator(sources, thug2, (label) => !label.includes("goal"), nodeNames, allNames, names, {
            provided: kills.keys(),
            objects,
            strictConditions: true,
            animCheck: true,
            keptIn: ["LoadSound"],
            leftOutPrefixes: ["GoalManager_"],
            components: { [scriptPort.PED]: ["vehicle"] },
            byName: { [scriptPort.VEHICLE]: ["Obj_Hover"] },
            renames: { Obj_Hover: "Obj_HoverX" },
        });
    }
    const scripts = tr.run(roots);
    for (const [name, code] of scripts)
        lines.push(`script ${hex8(name)} ${blob(code)} ${flag(scriptPort.doesSomething(code))}`);
    lines.push(`left out ${pyRepr([...tr.leftOut].map((p) => pyTuple(p)))}`);
    lines.push(`used globals ${pyRepr(sortedNumbers(tr.usedGlobals))}`);
    lines.push(
        `referenced ${pyRepr(sortedNumbers(tr.referenced))} created ${pyRepr(sortedNumbers(tr.created))} prefixes ${pyRepr(
            [...tr.createdPrefixes].sort(),
        )}`,
    );
    lines.push(`contexts ${pyRepr(sortedPairs([...tr.contexts].map(([k, v]) => [hex8(k), [...v].sort()])))}`);
    write(out, "globals.ns", tr.globalsNs());
    const compiled = path.join(out, "nodes.qb");
    common.nsCompile(NS, text, compiled);
    const data = fs.readFileSync(compiled);
    fs.rmSync(compiled);
    const full = scriptPort.appendScripts(data, scripts, names);
    write(out, "nodes.qb.txt", qbNorm(data));
    write(out, "full.qb.txt", qbNorm(full));
    const [net, changed] = nodes.netGameNodes(text, scripts);
    write(out, "net.ns", net);
    lines.push(`net ${changed}`);
    const [absent, marked] = nodes.absentInNetGames(text);
    lines.push(`absent ${marked} ${digest(absent)}`);
    if (game === "thps4") {
        const assets = new pedsCars.Thug2Assets(gameDir);
        const [made, madeScripts, madeStats] = pedsCars.convert(
            array,
            names,
            [q],
            (n) => assets.profileExists(n),
            (m) => assets.modelExists(m),
        );
        pedsCars.offsetLinks(made, converted.length);
        write(out, "peds.ns", nodes.nodeArrayNs("Parity_Peds", made, names) + pedsCars.scriptsNs(madeScripts));
        lines.push(`peds ${pyRepr(sortedPairs(Object.entries(madeStats)))}`);
    }
    write(out, "translate.txt", `${lines.join("\n")}\n`);
}

function cmdGround(out: string, gameDir: string, levels: readonly string[]): void {
    const lines: string[] = [];
    for (const level of levels) {
        const folder = path.join(gameDir, "Data", "levels", level);
        const q = new qb.QbFile(fs.readFileSync(path.join(folder, `${level}.qb`)), level, true);
        const names = new Map(q.names);
        const array = nodeArrayOf(q, level).filter((n): n is qb.Struct => n instanceof qb.Struct);
        // (some of THUG2's restarts moved under the world, and a kill script naming one: groundRestarts' work)
        array
            .filter((n) => nodes.classOf(n, names) === "restart")
            .forEach((node, k) => {
                if (k % 4 === 0) {
                    const pos = node.get("Pos");
                    if (pos instanceof qb.Vector) {
                        node.set(
                            "Pos",
                            new qb.Vector("vector", [pos.values[0]!, pos.values[1]! - 5000.0, pos.values[2]!]),
                        );
                    }
                }
            });
        let text = nodes.nodeArrayNs(`${level}_NodeArray`, array, names);
        const first = array.find((n) => nodes.classOf(n, names) === "restart" && nameOf(n) !== null);
        if (first !== undefined) {
            text += `script Parity_Kill {\n    SK3_KillSkater nodename=${qb.nameText(nameOf(first)!, names)}\n}\n`;
        }
        const [grounded, report] = nodes.groundRestartsAt(text, folder, level);
        write(out, `${level}.ns`, grounded);
        const [net, changed] = nodes.netGameNodes(grounded);
        write(out, `${level}_net.ns`, net);
        lines.push(`${level} ${changed} ${pyRepr(report)}`);
    }
    write(out, "ground.txt", `${lines.join("\n")}\n`);
}

// --- level structures, manifests ------------------------------------------------------------------------------------

function tagged(d: Record<string, number | Float>): unknown {
    return Object.entries(d).map(([k, v]) => [
        k,
        v instanceof Float ? new Map([["f", pyFloatRepr(v.value)]]) : new Map([["i", v]]),
    ]);
}

function cmdLighting(out: string, weights: string, paths: readonly string[]): void {
    const lines: string[] = [];
    for (const [label, data] of inputs(paths, [".qb"])) {
        let q: qb.QbFile;
        try {
            q = new qb.QbFile(data, label, weights === "1");
        } catch {
            continue;
        }
        for (const [key, value] of q.globals) {
            if (value instanceof qb.Struct) {
                const light = common.lighting(value);
                if (Object.keys(light).length) lines.push(`${label} ${hex8(key.value)} ${pyJsonDumps(tagged(light))}`);
            }
        }
    }
    write(out, "lighting.txt", `${lines.join("\n")}\n`);
}

function cmdManifest(out: string, games: readonly string[]): void {
    const lines: string[] = [];
    for (const game of games) {
        const tag = safe(game);
        for (const staging of portedLevels.STAGINGS) {
            const file = path.join(game, staging, portedLevels.MANIFEST);
            const manifest = portedLevels.readManifest(file);
            if (manifest === null) continue;
            const target = path.join(out, tag, `${staging}.json`);
            fs.mkdirSync(path.dirname(target), { recursive: true });
            portedLevels.writeManifest(target, manifest);
            lines.push(`${tag} ${staging} same=${flag(fs.readFileSync(target).equals(fs.readFileSync(file)))}`);
        }
        const manifest = portedLevels.loadManifest(game);
        if (manifest === null) {
            lines.push(`${tag} none`);
            continue;
        }
        write(out, path.join(tag, "levels.ns"), portedLevels.levelsQbText(manifest));
        write(out, path.join(tag, "signs.ns"), portedLevels.signsText(manifest, "        "));
        lines.push(`${tag} numbers ${pyRepr(portedLevels.signNumbers(manifest))}`);
        lines.push(`${tag} names ${pyRepr(manifest.levels.map((e) => portedLevels.structName(e)))}`);
        const base = "script Load_A {\n}\nscript Load_TestLevel {\n    load_level Level_TestLevel\n}\n";
        write(out, path.join(tag, "added.ns"), portedLevels.addToLevels(base, manifest));
    }
    // installFiles from a small made-up game folder
    const fake = path.join(out, "fake_game");
    for (const [staging, level] of [
        ["thug2-decomp-thps4", "thps4_alc"],
        ["thug2-decomp-thaw", "thaw_z_bh"],
    ] as const) {
        fs.mkdirSync(path.join(fake, staging, "levels", level, "sub"), { recursive: true });
        fs.writeFileSync(path.join(fake, staging, "levels", level, `${level}.qb`), `qb ${level}`);
        fs.writeFileSync(
            path.join(fake, staging, "levels", level, "sub", "x.bin"),
            Buffer.from([...Array(256).keys()]),
        );
    }
    fs.mkdirSync(path.join(out, "overlay", "levels", "thps4_alc"), { recursive: true });
    fs.writeFileSync(path.join(out, "overlay", "levels", "thps4_alc", "old.txt"), "old");
    lines.push(`installed ${pyRepr(portedLevels.installFiles(fake, path.join(out, "overlay")))}`);
    write(out, "manifest.txt", `${lines.join("\n")}\n`);
}

// --- geometry ---------------------------------------------------------------------------------------------------------

const camel = (key: string): string => key.replace(/_([a-z])/g, (_, c: string) => c.toUpperCase());

// driver.py's JSON (bytes as {"hex": ...}, snake_case keys) as the TypeScript structures.
function unjson(x: unknown): unknown {
    if (Array.isArray(x)) return x.map(unjson);
    if (x !== null && typeof x === "object") {
        const o = x as Record<string, unknown>;
        const keys = Object.keys(o);
        if (keys.length === 1 && keys[0] === "hex") return Buffer.from(o.hex as string, "hex");
        return Object.fromEntries(keys.map((k) => [camel(k), unjson(o[k])]));
    }
    return x;
}

const snake = (key: string): string => key.replace(/[A-Z]/g, (c) => `_${c.toLowerCase()}`);

// The structures as driver.py's JSON reads them (bytes as {"hex": ...}, snake_case keys; a collision object's
// per-vertex intensities as bytes, as THPS4's Python reader had them).
function jsonable(x: unknown, key = ""): unknown {
    if (Buffer.isBuffer(x)) return new Map([["hex", x.toString("hex")]]);
    if (Array.isArray(x)) {
        if (key === "intensities") return x.map((i: number[]) => jsonable(Buffer.from(i)));
        return x.map((v) => jsonable(v));
    }
    if (x !== null && typeof x === "object") {
        return new Map(Object.entries(x).map(([k, v]) => [snake(k), jsonable(v, k)]));
    }
    return x;
}

function cmdPrepare(out: string, which: string, thps4Dir: string, thug1Dir: string): void {
    // The geometry commands' inputs: THPS4's and THUG1's scenes / collision as their readers (thps4.ts, thug1.ts)
    // make them, as JSON both drivers read. (Was driver.py's, with the Python readers: they went with stage 2.)
    fs.mkdirSync(out, { recursive: true });
    const levels = path.join(thps4Dir, "data", "levels");
    for (const folder of which === "thps4" ? fs.readdirSync(levels).sort() : []) {
        const base = path.join(levels, folder, folder.toLowerCase());
        if (!fs.existsSync(`${base}scn.dat`)) continue;
        const scene = thps4.readScene(fs.readFileSync(`${base}scn.dat`), folder);
        const item: Record<string, unknown> = {
            name: folder,
            game: "thps4",
            sectors: scene.sectors,
            materials: scene.materials.map(thps4.toThug2Material),
        };
        if (fs.existsSync(`${base}col.dat`))
            item.col = thps4.readCol(fs.readFileSync(`${base}col.dat`), folder).objects;
        fs.writeFileSync(path.join(out, `thps4_${folder}.json`), pyJsonDumps(jsonable(item)));
    }
    const pre = path.join(thug1Dir, "Data", "pre");
    for (const archive of which === "thug1" ? fs.readdirSync(pre).sort() : []) {
        if (!archive.toLowerCase().endsWith("scn.pre")) continue;
        for (const [name, data] of prx.entries(path.join(pre, archive), (n) => n.toLowerCase().endsWith(".scn.xbx"))) {
            let scene: thug1.Scene;
            try {
                scene = thug1.readScene(data, name);
            } catch (e) {
                if (e instanceof FormatError) continue;
                throw e;
            }
            const item = { name, game: "thug1", sectors: scene.sectors, materials: scene.materials };
            fs.writeFileSync(path.join(out, `thug1_${safe(name)}.json`), pyJsonDumps(jsonable(item)));
        }
    }
}

function cmdGeometry(out: string, paths: readonly string[]): void {
    // paths: prepare's JSON files, or folders of them
    const lines: string[] = [];
    const expanded: string[] = [];
    for (const p of paths) {
        if (fs.statSync(p).isDirectory()) {
            expanded.push(
                ...fs
                    .readdirSync(p)
                    .filter((f) => f.endsWith(".json"))
                    .sort()
                    .map((f) => path.join(p, f)),
            );
        } else {
            expanded.push(p);
        }
    }
    for (const file of expanded) {
        const item = unjson(pyJsonLoads(fs.readFileSync(file, "utf-8"), "number")) as {
            name: string;
            game: string;
            sectors: geometry.SourceSector[];
            materials: thug2Scene.Material[];
            col?: geometry.SourceColObject[];
        };
        let warnings: string[] = [];
        const scene = geometry.convertScene({ sectors: item.sectors }, warnings, item.materials, item.game === "thps4");
        let data = thug2Scene.writeScene(scene);
        lines.push(`${item.name} scene ${digest(canon(scene))} ${blob(data)} ${pyRepr(warnings.slice(0, 20))}`);
        lines.push(`${item.name} shaders ${pyRepr(item.materials.map(geometry.pixelShader))}`);
        if (item.col !== undefined) {
            warnings = [];
            const col = geometry.convertCol(
                { objects: item.col },
                (c) => c % 2 === 0,
                warnings,
                (f) => (f ^ 0x80) & 0x15fc,
            );
            data = thug2Col.writeCol(col);
            const again = thug2Col.readCol(data, item.name);
            lines.push(
                `${item.name} col ${digest(canon(col))} ${blob(data)} ${pyRepr(pyTuple(colCheck.check(again)))} ${pyRepr(warnings)}`,
            );
        }
    }
    write(out, "geometry.txt", `${lines.join("\n")}\n`);
}

function cmdColgeometry(out: string, paths: readonly string[]): void {
    // THUG2's own collision objects made again (fixed-point grid, reaches, BSP trees) from their vertices and faces.
    const lines: string[] = [];
    for (const [label, data] of inputs(paths, [".col.xbx"])) {
        const col = thug2Col.readCol(data, label);
        const source = {
            objects: col.objects.map((o) => ({
                checksum: o.checksum,
                flags: o.flags,
                bbox: o.bbox,
                verts: o.verts,
                intensities: o.intensities,
                faces: o.faces,
            })),
        };
        const made = geometry.convertCol(
            source,
            (c) => c % 3 !== 0,
            [],
            (f) => f,
        );
        const written = thug2Col.writeCol(made);
        lines.push(
            `${label} ${digest(canon(made))} ${blob(written)} ${pyRepr(pyTuple(colCheck.check(thug2Col.readCol(written, label))))}`,
        );
        for (const o of made.objects.slice(0, 3)) lines.push(`  ${blob(o.value!)} ${blob(o.reach)}`);
    }
    write(out, "colgeometry.txt", `${lines.join("\n")}\n`);
}

// --- the rest -----------------------------------------------------------------------------------------------------------

function cmdCommon(out: string, gameDir: string, archives: readonly string[]): void {
    const pre = path.join(gameDir, "Data", "pre");
    const [checked, failures] = common.thug2RoundTrips(pre, archives);
    print(`checked ${checked} failures ${failures}`);
    const folder = path.join(gameDir, "Data", "levels", "TR");
    const scene = thug2Scene.readScene(fs.readFileSync(path.join(folder, "TR.scn.xbx")));
    const textures = tex.readTex(fs.readFileSync(path.join(folder, "TR.tex.xbx")));
    const col = thug2Col.readCol(fs.readFileSync(path.join(folder, "TR.col.xbx")));
    common.writeLevel(path.join(out, "level"), "tr", scene, textures, col);
    common.writeLevel(path.join(out, "sky"), "tr_sky", scene, textures, null);
    const report: string[] = [];
    common.reportLevel(
        report,
        "tr",
        scene,
        textures,
        col,
        [...Array(13).keys()].map((k) => `w${k}`),
        2,
    );
    common.reportLevel(report, "tr_sky", scene, textures, null, []);
    write(out, "report.txt", `${report.join("\n")}\n`);
}

function main(): void {
    const [command, out, ...rest] = process.argv.slice(2) as [string, string, ...string[]];
    fs.mkdirSync(out, { recursive: true });
    switch (command) {
        case "archives":
            cmdArchives(out, rest);
            return;
        case "files":
            cmdFiles(out, rest);
            return;
        case "qb":
            cmdQb(out, rest[0]!, rest.slice(1));
            return;
        case "thug2":
            cmdThug2(out, rest[0]!);
            return;
        case "translate":
            cmdTranslate(out, rest[0]!, rest[1]!, rest[2]!, rest.slice(3));
            return;
        case "ground":
            cmdGround(out, rest[0]!, rest.slice(1));
            return;
        case "lighting":
            cmdLighting(out, rest[0]!, rest.slice(1));
            return;
        case "manifest":
            cmdManifest(out, rest);
            return;
        case "prepare":
            cmdPrepare(out, rest[0]!, rest[1]!, rest[2]!);
            return;
        case "geometry":
            cmdGeometry(out, rest);
            return;
        case "colgeometry":
            cmdColgeometry(out, rest);
            return;
        case "common":
            cmdCommon(out, rest[0]!, rest.slice(1));
            return;
        default:
            throw new Error(`unknown command ${command}`);
    }
}

main();
