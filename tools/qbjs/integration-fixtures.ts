// NOT ORIGINAL - public synthetic QuickJS suites, no original game data.
import { readFileSync, writeFileSync } from "node:fs";
import { pathToFileURL } from "node:url";
import { executionBundle } from "./emitter-fixtures.ts";
import { nativeRuntimeBundle, runtimeAssertions } from "./runtime-fixtures.ts";

export function integrationBundle(): string {
    const values = readFileSync(new URL("../../scripting/values.js", import.meta.url), "utf8");
    const tests = readFileSync(new URL("../../scripting/tests/values.test.js", import.meta.url), "utf8");
    return (
        values +
        tests +
        '\nif (!(globalThis.qbValuesTestResult?.assertions >= 175)) throw Error("Values assertions did not complete");\n' +
        executionBundle(values) +
        nativeRuntimeBundle() +
        runtimeAssertions() +
        "\nglobalThis.qbIntegrationTestResult = {completed:true,values:qbValuesTestResult.assertions};\n"
    );
}
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    const output = process.argv[2];
    if (!output || process.argv.length !== 3) throw Error("Usage: node tools/qbjs/integration-fixtures.ts OUTPUT.js");
    writeFileSync(output, integrationBundle());
}
