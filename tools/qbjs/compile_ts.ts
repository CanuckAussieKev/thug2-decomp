// NOT ORIGINAL - a readable TypeScript script module (emit_ts.ts, docs/qb-ts.md) -> the JavaScript the embedded
// runtime loads: types stripped, the "qb" import read from globalThis.qbTs (scripting/qb_ts.js).
import { readFileSync, writeFileSync } from "node:fs";
import { stripTypeScriptTypes } from "node:module";
import { pathToFileURL } from "node:url";

const IMPORT = /^import \{([^}]*)\} from "qb";$/m;

// extra: more of the manifest - a standalone module's (install_ts.ts --standalone): the .qb it was made from (`from`,
// its bytes' hash) and the file's checksum names (`names`: [checksum, name, ...], registered as the .qb loader does).
export function compileTS(code: string, source: string, extra: Record<string, unknown> = {}): string {
    const js = stripTypeScriptTypes(code, { mode: "strip" });
    const match = IMPORT.exec(js);
    const imports = match ? match[1]!.split(",").map((s) => s.trim()) : [];
    const random = imports.some((name) => name.startsWith("random"));
    const manifest = {
        format: 2,
        source,
        scripts: [],
        requires: { values: 2, ...(random ? { random: 1 } : {}) },
        ...extra,
    };
    // (an import renamed with `as` is a destructuring rename)
    const names = match ? match[1]!.replace(/(\w+)\s+as\s+(\w+)/g, "$1: $2") : "";
    const body = match ? js.replace(IMPORT, `const {${names}} = globalThis.qbTs;`) : js;
    return `(() => {\nqb.module(${JSON.stringify(manifest)});\nglobalThis.qbTs.beginModule(${JSON.stringify(source)});\n${body}\n})();\n`;
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    const [, , input, output] = process.argv;
    if (!input || !output || process.argv.length !== 4)
        throw Error("Usage: node tools/qbjs/compile_ts.ts INPUT.ts OUTPUT.js");
    writeFileSync(output, compileTS(readFileSync(input, "utf8"), input.replace(/\\/g, "/")));
}
