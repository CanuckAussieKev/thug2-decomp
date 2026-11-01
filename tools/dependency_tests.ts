// NOT ORIGINAL - runs the tests of the installed level converters and thug2-level-converter-core (`npm test` runs it
// after tools/**/*.test.ts): what this repository's tests covered while the converters were part of tools/pipeline.
// npm built each package's .js beside its .ts (Node strips no types under node_modules - the core's README), its tests
// included; `node --test` skips node_modules in a glob, so the files are named one by one.
//
//   node tools/dependency_tests.ts

import { spawnSync } from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";

const ROOT = path.resolve(import.meta.dirname, "..");
const PACKAGES = /^thug2-(level-converter-core|[a-z0-9]+-level-converter|[a-z0-9]+-compat)$/;

function tests(dir: string): string[] {
    const out: string[] = [];
    for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
        const p = path.join(dir, e.name);
        if (e.isDirectory()) {
            if (e.name !== "node_modules") out.push(...tests(p));
        } else if (e.name.endsWith(".test.js")) {
            out.push(p);
        }
    }
    return out;
}

const modules = path.join(ROOT, "node_modules");
const files = fs
    .readdirSync(modules)
    .filter((n) => PACKAGES.test(n))
    .flatMap((n) => tests(path.join(modules, n)))
    .sort();
if (!files.length) {
    process.stderr.write("no converter packages installed: run npm install\n");
    process.exit(1);
}
const r = spawnSync(process.execPath, ["--test", ...files], { stdio: "inherit", cwd: ROOT });
process.exit(r.status ?? 1);
