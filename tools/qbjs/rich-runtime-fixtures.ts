// NOT ORIGINAL - synthetic QB crossing the native rich bridge; no game data.
import { writeFileSync } from "node:fs";
import { pathToFileURL } from "node:url";
import { emitIR } from "./emitter.ts";
import { command, conditional, field, floating, integer, name, param, script, word } from "./fixtures.ts";
import { helperSource } from "./helper-sources.ts";
import { readIR } from "./reader.ts";

const returned = (fields: number[]) => [0x29, ...fields, 1];
export function richRuntimeBundle() {
    const returns = (condition: number) =>
        returned([
            ...field("reply", param("replyHost")),
            ...field("arrayResult", param("arrayHost")),
            ...field("byteResult", param("byteHost")),
            ...field("condition", integer(condition)),
        ]);
    const arguments_ = [
        ...field("data", param("payload")),
        ...field("raw", param("raw")),
        ...field("local", param("local")),
        ...field("pair", param("pair")),
        ...field("vector", param("vector")),
        ...field("dupe", integer(1)),
        ...field("dupe", name("opaque")),
        ...field("dupe", floating(0x80000000)),
        ...name("flagA"),
        ...name("flagB"),
        ...name("flagA"),
    ];
    const input = Uint8Array.from([
        ...script(
            "richMain",
            [
                ...command("richChild", [0x2c]),
                ...command("Wait", [...integer(1), ...name("gameframes")]),
                ...returned(field("done", integer(1))),
            ],
            field("number", integer(5)),
        ),
        ...script(
            "richChild",
            [
                ...field("private", integer(999)),
                1,
                ...conditional(
                    [...name("RichInspect"), ...arguments_],
                    [...command("RichVerify"), ...returns(1)],
                    [...command("RichVerify"), ...returns(0)],
                ),
            ],
            field("childDefault", [0x1c, ...word(3), 0x80, 0xff, 0]),
        ),
        ...script("richSingle", command("Switchable", field("data", param("payload")))),
        ...script("richMissing", command("NoSuchRichHost")),
        ...script("richSimple", returned(field("value", param("payload")))),
        ...script("scalarSimple", returned(field("value", integer(7)))),
        0,
    ]);
    return helperSource("values") + "\n" + emitIR(readIR(input, "rich-runtime.qb"));
}
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    if (process.argv.length !== 3) throw Error("Usage: node tools/qbjs/rich-runtime-fixtures.ts OUTPUT.js");
    writeFileSync(process.argv[2]!, richRuntimeBundle());
}
