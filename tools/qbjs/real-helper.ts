// NOT ORIGINAL - optional local-data scenario. Never commits game QB/IR/JS.
import { createHash } from "node:crypto";
import { readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { pathToFileURL } from "node:url";
import { emitIR } from "./emitter.ts";
import { readIR, readQBFile } from "./reader.ts";
import { checksum } from "./translate.ts";

export const helperModule = "scripts/game/misc/level_flags.qb";
export const helperName = "ResetLevelFlags";
export function prepareHelper(bytes: Uint8Array) {
    const ir = readIR(bytes, helperModule);
    const helper = ir.definitions.findLast((d) => d.name === checksum(helperName));
    if (helper?.kind !== "script" || helper.body.length === 0) throw Error("Missing nonempty ResetLevelFlags script");
    const calls = new Set<number>(),
        globals = new Set<number>();
    const visit = (x: unknown): void => {
        if (!x || typeof x !== "object") return;
        if (Array.isArray(x)) {
            x.forEach(visit);
            return;
        }
        const record = x as Record<string, unknown>;
        if (record.kind === "call") calls.add(record.name as number);
        if (record.kind === "global") globals.add(record.name as number);
        Object.values(record).forEach(visit);
    };
    visit(helper.body);
    visit(helper.defaults);
    if (calls.size !== 1 || !calls.has(checksum("UnSetFlag")) || globals.size)
        throw Error("Helper dependencies differ from supported UnSetFlag-only scenario");
    const metadata = {
        module: helperModule,
        script: helperName,
        crc: helper.name,
        offset: helper.offset,
        sha256: createHash("sha256").update(bytes).digest("hex"),
        bytes: bytes.length,
        definitions: ir.definitions.length,
        hosts: ["UnSetFlag"],
        globalReads: [],
        requiresRandom: false,
    };
    const values = readFileSync(new URL("../../scripting/values.js", import.meta.url), "utf8");
    return {
        metadata,
        javascript:
            "// Private output from local game data; do not publish.\n" +
            'if (globalThis.qb?.runtimeVersion !== 2) throw Error("QB runtime version 2 required");\n' +
            values +
            "\n" +
            emitIR(ir),
    };
}
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    const [, , data, output] = process.argv;
    if (!data || !output || process.argv.length !== 4)
        throw Error("Usage: node tools/qbjs/real-helper.ts DATA_DIRECTORY OUTPUT.js");
    const result = prepareHelper(readQBFile(join(data, helperModule), output));
    writeFileSync(output, result.javascript);
    process.stdout.write(JSON.stringify(result.metadata, null, 2) + "\n");
}
