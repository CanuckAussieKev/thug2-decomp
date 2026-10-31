// NOT ORIGINAL - sources for isolated JS test contexts. The C++ Runtime embeds
// these once; generated production modules contain no compatibility helpers.
import { readFileSync } from "node:fs";
export function helperSource(name: "numeric" | "values" | "random"): string {
    const global = name === "numeric" ? "qbNumeric" : name === "values" ? "qbValues" : "qbRandom";
    const dependency = name === "numeric" ? "" : helperSource("numeric");
    return (
        dependency +
        `\nif (globalThis.${global} === undefined) {\n` +
        readFileSync(new URL(`../../scripting/${name}.js`, import.meta.url), "utf8") +
        "\n}\n"
    );
}
