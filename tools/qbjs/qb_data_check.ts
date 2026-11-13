// NOT ORIGINAL - proves the data files (qb_data.ts, emit_data.ts; docs/qb-ts.md "Data") against every global of a
// game's .qb files: each global's value three ways must be the same -
//   1. built from the module (emit_data.ts globalValue: what the data file is written from),
//   2. its data file written and read back (writeQbData -> readQbData),
//   3. made by the module's TypeScript with its globals in it (emit_ts.ts, compile_ts.ts, the runtime's qb_ts.js and
//      values.js in the game's mode) - what the game took over from the TypeScript before the split;
// and, with the text tables made as unpack_game.ts makes them (localize.ts, English), each localised global read back
// with English's table the same as the file's own text.
//   node tools/qbjs/qb_data_check.ts --game <folder>      (every version of every .qb: game_scripts.ts qbVersions)
import { readFileSync } from "node:fs";
import { pathToFileURL } from "node:url";
import { runInNewContext } from "node:vm";
import { compileTS } from "./compile_ts.ts";
import { moduleGlobals } from "./emit_data.ts";
import { emitTS } from "./emit_ts.ts";
import { gameRoot, qbVersions, scriptBytes } from "./game_scripts.ts";
import { helperSource } from "./helper-sources.ts";
import type { ModuleIR } from "./ir.ts";
import { Localizer, notLocalised } from "./localize.ts";
import { type QbDataValue, readQbData, writeQbData } from "./qb_data.ts";
import { readIR } from "./reader.ts";

// A value as comparable text: a script value by its place among the global's script values (the data names it by
// key, the runtime by a made-up number).
// (keys: a script value by its key - the data's own, compared as written)
function show(v: QbDataValue, scripts: { n: number; keys?: true } = { n: 0 }): string {
    switch (v.type) {
        case 7:
            return scripts.keys ? `script ${v.key}` : `script#${++scripts.n}`;
        case 10:
            return `{${v.members.map(([n, m]) => `${n}=${show(m, scripts)}`).join(",")}}`;
        case 12:
            return `[${v.elementType}:${v.elements.map((e) => show(e, scripts)).join(",")}]`;
        case 3:
        case 4:
            return `${v.type}:${v.bytes.join(".")}`;
        case 5:
        case 6:
            return `${v.type}:${v.coordinates.join(",")}`;
        default:
            return `${v.type}:${v.bits >>> 0}`;
    }
}
export const valueText = (v: QbDataValue): string => show(v);

// The module's globals as its TypeScript (globals in it) defines them in the game's mode: name -> value, in order.
// (text: the translated texts tr reads - qbText, as the game sets it)
export function typescriptGlobals(module: ModuleIR, text: Record<string, string> = {}): [number, QbDataValue][] {
    const defined: [number, QbDataValue][] = [];
    const game = {
        symbol: () => undefined,
        symbolKind: () => undefined,
        defineGlobal: (n: number, value: QbDataValue) => defined.push([n, value]),
        defineScript: () => undefined,
        registerNames: () => undefined,
        frame: () => 0,
    };
    const code =
        helperSource("random") +
        helperSource("values") +
        readFileSync(new URL("../../scripting/runtime.js", import.meta.url), "utf8") +
        readFileSync(new URL("../../scripting/qb_ts.js", import.meta.url), "utf8") +
        compileTS(emitTS(module), module.source);
    runInNewContext(code, { __qbGame: game, qbText: text }, { timeout: 120000 });
    return defined;
}

export type CheckResult = { files: number; globals: number; different: string[]; localised: number };

// Checks one module (its own text, not localised) - the differences as "<source> <name>: <what>".
// (table: the translated texts, for a module whose strings have tokens)
export function checkModule(
    module: ModuleIR,
    viaTypeScript = true,
    table: Record<string, string> = {},
): { globals: number; different: string[] } {
    const different: string[] = [];
    const built = moduleGlobals(module);
    const json = writeQbData({ source: module.source, from: "0", globals: built }, (n) => module.names?.[n]);
    const read = readQbData(json, (token) => table[token]).globals;
    const last = new Map(built); // (the data file keeps a name's last definition)
    const hex = (n: number) => n.toString(16).padStart(8, "0");
    if (read.length !== last.size) different.push(`${module.source}: ${read.length} globals read, ${last.size} built`);
    for (const [n, v] of read) {
        const want = last.get(n);
        if (!want || show(want, { n: 0, keys: true }) !== show(v, { n: 0, keys: true }))
            different.push(`${module.source} ${hex(n)}: read back differs`);
    }
    if (viaTypeScript) {
        const ts = new Map(typescriptGlobals(module, table));
        if (ts.size !== last.size) different.push(`${module.source}: ${ts.size} globals from TypeScript, ${last.size}`);
        for (const [n, v] of last)
            if (!ts.has(n) || show(ts.get(n)!) !== show(v))
                different.push(`${module.source} ${hex(n)}: the TypeScript's value differs`);
    }
    return { globals: last.size, different };
}
export function checkGame(game: string, viaTypeScript = true, log = (_: string) => {}): CheckResult {
    const root = gameRoot(game);
    const result: CheckResult = { files: 0, globals: 0, different: [], localised: 0 };
    const localizer = new Localizer();
    const english: { source: string; plain: ModuleIR; localised: ModuleIR }[] = [];
    for (const v of qbVersions(root)) {
        const source = `${v.origin}\\${v.name}`;
        let module: ModuleIR;
        try {
            module = readIR(scriptBytes(v.name, v.bytes), source);
        } catch {
            continue; // (doesn't translate: unpack_game.ts lists it)
        }
        if (!module.definitions.some((d) => d.kind === "global")) continue;
        result.files++;
        const checked = checkModule(module, viaTypeScript);
        result.globals += checked.globals;
        result.different.push(...checked.different);
        if (v.origin === "Data" && !notLocalised(v.name)) {
            const localised = readIR(scriptBytes(v.name, v.bytes), source);
            localizer.localize(source, localised, {});
            english.push({ source, plain: module, localised });
        }
        log(
            `${source}: ${checked.globals} globals${checked.different.length ? `, ${checked.different.length} differ` : ""}`,
        );
    }
    // Localised: each global with a token read back with English's table is the file's own text.
    const table = localizer.tables.en ?? {};
    for (const { source, plain, localised } of english) {
        const want = new Map(moduleGlobals(plain));
        const json = writeQbData({ source, from: "0", globals: moduleGlobals(localised) });
        if (!json.includes('"$tr"')) continue;
        result.localised++;
        for (const [n, v] of readQbData(json, (token) => table[token]).globals)
            if (show(v) !== show(want.get(n)!))
                result.different.push(`${source} ${n.toString(16)}: localised, read with English's text, differs`);
    }
    return result;
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    const args = process.argv.slice(2);
    const at = args.indexOf("--game");
    const game = at >= 0 ? args[at + 1] : undefined;
    if (!game) throw Error("Usage: node tools/qbjs/qb_data_check.ts --game <folder> [--no-typescript] [--verbose]");
    const result = checkGame(
        game,
        !args.includes("--no-typescript"),
        args.includes("--verbose") ? console.log : undefined,
    );
    for (const d of result.different.slice(0, 50)) console.log(`DIFFERENT ${d}`);
    console.log(
        `${result.files} files, ${result.globals} globals: ${result.different.length} different; ` +
            `${result.localised} files with localised text read back with English's`,
    );
    if (result.different.length) process.exitCode = 1;
}
