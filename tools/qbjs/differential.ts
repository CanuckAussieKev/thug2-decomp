// NOT ORIGINAL - runs every script of a QB module two ways - emitter.ts's JavaScript and emit_ts.ts's TypeScript
// (compiled by compile_ts.ts onto scripting/qb_ts.js) - against the same recording host, and compares what they do:
// each command call with its arguments, waits, how each task ends and its final parameters.
// CLI: node tools/qbjs/differential.ts <file.qb | folder> [--show N]
import { readdirSync, readFileSync, statSync } from "node:fs";
import { join } from "node:path";
import { pathToFileURL } from "node:url";
import { runInNewContext } from "node:vm";
import { compileTS } from "./compile_ts.ts";
import { emitTS } from "./emit_ts.ts";
import { emitIR } from "./emitter.ts";
import { helperSource } from "./helper-sources.ts";
import type { ModuleIR } from "./ir.ts";
import { readIR } from "./reader.ts";

const runtimeSource = () =>
    `if (globalThis.qb?.runtimeVersion !== 2) {\n${readFileSync(new URL("../../scripting/runtime.js", import.meta.url), "utf8")}\n}\n`;
const tsHelpersSource = () => readFileSync(new URL("../../scripting/qb_ts.js", import.meta.url), "utf8");

// The host: every command is answered from its checksum and how often it was called, so both runs see the same
// answers in the same order - true or false, now and then a one-frame wait.
const harness = (seed: number) => `
(() => {
const SEED = ${seed};
const events = [];
const counts = new Map();
const LIMIT = 3000;
const text = (x, depth = 0) => {
    if (depth > 40) return '...';
    if (Array.isArray(x)) return '[' + x.map(y => text(y, depth + 1)).join(',') + ']';
    if (x && typeof x === 'object') return '{' + Object.keys(x).map(k => k + ':' + text(x[k], depth + 1)).join(',') + '}';
    return String(x);
};
// Objects: every name but 0 and one in eleven (moved by the seed) exists.
globalThis.__qbFindObject = (id) => id !== 0 && (id + SEED) % 11 !== 0;
globalThis.__qbHost = (name, args, p, object, qualified) => {
    const n = (counts.get(name) ?? 0) + 1;
    counts.set(name, n);
    if (events.length >= LIMIT) throw Error('host call limit');
    events.push('call ' + name.toString(16) + (qualified ? ' on ' : ' in ') + object.toString(16) + ' ' + text(args));
    return { condition: ((name >>> 3) + n + SEED) % 3 !== 0, outputs: [], frames: n % 4 === 0 ? 1 : undefined };
};
return (scripts) => {
    for (const name of scripts) {
        counts.clear();
        events.push('start ' + name.toString(16));
        let id, r;
        try {
            id = qb.start(name, [], 1000000);
            for (let frame = 0; frame < 400; ++frame) {
                r = qb.update(id, globalThis.__frame = (globalThis.__frame ?? 0) + 1);
                if (r.status !== 'waiting' && r.status !== 'ready') break;
            }
            // A loop that never waits runs out of the step budget on both paths, at different passes (they count
            // steps differently: docs/qb-ts.md): one outcome; the calls it made before are still compared.
            const message = String(r.message).replace(/ at .*$/, '');
            events.push(/fuel exhausted/.test(message) ? 'end runaway' : 'end ' + r.status + ' ' + message + ' ' + text(r.parameters));
        } catch (e) {
            events.push('throw ' + String(e).replace(/ at .*$/, ''));
        } finally {
            if (id !== undefined) qb.cancel(id);
        }
    }
    return events;
};
})()`;

export type Outcome = { same: boolean; scripts: number; js: string[]; ts: string[]; error?: string };

function run(code: string, scripts: number[], withTs: boolean, seed: number): string[] {
    const bundle =
        helperSource("random") +
        helperSource("values") +
        runtimeSource() +
        (withTs ? tsHelpersSource() : "") +
        code +
        `\n;(${harness(seed)})`;
    const runner = runInNewContext(bundle, {}, { timeout: 20000 }) as (s: number[]) => string[];
    return runInNewContext("x => f(x)", { f: runner }, { timeout: 60000 })(scripts) as string[];
}

// Each seed answers the host's conditions differently, so the runs take different branches.
export function differential(module: ModuleIR, seeds = [0, 1, 2]): Outcome {
    const scripts = module.definitions.filter((d) => d.kind === "script").map((d) => d.name);
    const ts = compileTS(emitTS(module), module.source);
    const javascript = emitIR(module);
    const load = (code: string, withTs: boolean, seed: number) => {
        try {
            return run(code, scripts, withTs, seed);
        } catch (e) {
            // A module too big for the runtime's load budgets fails on both paths, at whichever bound trips
            // first (the paths build values in differently sized steps): one outcome.
            const message = String(e).split("\n")[0]!;
            return [/fuel exhausted|work limit/.test(message) ? "load over budget" : `load ${message}`];
        }
    };
    const js = seeds.flatMap((seed) => load(javascript, false, seed));
    const tsEvents = seeds.flatMap((seed) => load(ts, true, seed));
    return {
        same: js.length === tsEvents.length && js.every((e, i) => e === tsEvents[i]),
        scripts: scripts.length,
        js,
        ts: tsEvents,
    };
}

const walk = (path: string): string[] =>
    statSync(path).isDirectory()
        ? readdirSync(path).flatMap((n) => walk(join(path, n)))
        : path.toLowerCase().endsWith(".qb")
          ? [path]
          : [];

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    const [, , target, flag, count] = process.argv;
    if (!target) throw Error("Usage: node tools/qbjs/differential.ts <file.qb | folder> [--show N]");
    const show = flag === "--show" ? Number(count) : 3;
    let same = 0,
        differ = 0,
        unsupported = 0;
    for (const file of walk(target)) {
        let module: ModuleIR;
        try {
            module = readIR(readFileSync(file), file.replace(/\\/g, "/"));
        } catch {
            ++unsupported;
            continue;
        }
        let outcome: Outcome;
        try {
            outcome = differential(module);
        } catch (e) {
            ++differ;
            console.log(`ERROR ${file}: ${String(e).split("\n")[0]}`);
            continue;
        }
        if (outcome.same) {
            ++same;
            continue;
        }
        ++differ;
        const i = outcome.js.findIndex((e, n) => e !== outcome.ts[n]);
        console.log(`DIFFER ${file} (event ${i} of ${outcome.js.length} / ${outcome.ts.length})`);
        for (let n = Math.max(0, i - 1); n < i + show; ++n) {
            const [js, ts] = [outcome.js[n], outcome.ts[n]];
            if (js !== undefined) console.log(`  js ${js.slice(0, 300)}`);
            if (ts !== undefined) console.log(`  ts ${ts.slice(0, 300)}`);
        }
    }
    console.log(`same ${same}, differ ${differ}, not translated ${unsupported}`);
}
