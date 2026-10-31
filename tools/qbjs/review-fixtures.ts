// NOT ORIGINAL - upstream-review regressions, synthetic QB only.
import { readFileSync } from "node:fs";
import { emitIR } from "./emitter.ts";
import { field, name, script, word } from "./fixtures.ts";
import { readIR } from "./reader.ts";
export function reviewBundle(): string {
    const vector = [0x1e, ...word(0), ...word(0x80000000), ...word(0)];
    const tokens = Uint8Array.from([
        ...script("reviewAngles", [], [...field("angles", vector), ...field("kept", vector)]),
        ...script("reviewExpressionAngles", [], field("angles", [14, ...vector, 15])),
        ...script("reviewAssignmentAngles", [...field("angles", vector), 1]),
        ...script("reviewScriptValue", [...field("callback", [14, ...name("reviewAngles"), 15]), 1]),
        0,
    ]);
    return (
        emitIR(readIR(tokens, "review.qb")) +
        readFileSync(new URL("../../scripting/tests/review.test.js", import.meta.url), "utf8")
    );
}
