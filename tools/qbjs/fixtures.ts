// NOT ORIGINAL - synthetic QB fixtures. No game scripts or extracted bytes.
import { writeFileSync } from "node:fs";
import { pathToFileURL } from "node:url";
import { checksum, translate } from "./translate.ts";
export const word = (n: number): number[] => {
    const b = Buffer.alloc(4);
    b.writeUInt32LE(n >>> 0);
    return [...b];
};
export const name = (s: string): number[] => [0x16, ...word(checksum(s))];
export const integer = (n: number): number[] => [0x17, ...word(n)];
export const floating = (bits: number): number[] => [0x1a, ...word(bits)];
export const param = (s: string): number[] => [0x2d, ...name(s)];
export const field = (s: string, value: number[]): number[] => [...name(s), 7, ...value];
export const command = (s: string, args: number[] = []): number[] => [...name(s), ...args, 1];
export const script = (s: string, body: number[], defaults: number[] = []): number[] => [
    0x23,
    ...name(s),
    ...defaults,
    1,
    ...body,
    0x24,
    1,
];
export function conditional(condition: number[], yes: number[], no: number[]): number[] {
    const b = [0x47, 0, 0, ...condition, 1, ...yes, 0x48, 0, 0];
    const target = b.length,
        endField = b.length - 2;
    b.push(1, ...no);
    const end = b.length;
    b.push(0x28, 1);
    b[1] = (target - 1) & 255;
    b[2] = (target - 1) >>> 8;
    b[endField] = (end - endField) & 255;
    b[endField + 1] = (end - endField) >>> 8;
    return b;
}
export function fixture(): Uint8Array {
    const returned = (s: string, v: number[]) => [0x29, ...field(s, v), 1];
    return Uint8Array.from([
        ...script(
            "main",
            [
                ...command("Echo", [
                    ...field("value", param("x")),
                    ...field("raw", floating(0x80000000)),
                    ...field("name", name("opaque")),
                ]),
                ...command("child", field("x", param("x"))),
                ...conditional(
                    name("Check"),
                    [...field("checked", integer(1)), 1],
                    [...field("checked", integer(0)), 1],
                ),
                ...returned("done", integer(1)),
            ],
            [...field("x", integer(11)), ...field("marker", integer(55))],
        ),
        ...script(
            "child",
            [
                ...field("private", integer(99)),
                1,
                ...command("Wait", [...integer(1), ...name("gameframes")]),
                ...returned("result", param("x")),
            ],
            field("x", integer(9)),
        ),
        ...script("zero", [...command("Wait", [...integer(0), ...name("frames")]), ...returned("done", integer(1))]),
        ...script("two", [...command("Wait", [...integer(2), ...name("frames")]), ...returned("done", integer(1))]),
        ...script("unknown", command("NoSuchHost")),
        ...script("throws", command("Throws")),
        ...script("recursive", command("recursive")),
        ...script("badwait", command("Wait", integer(1))),
        ...script("negative", command("Wait", [...integer(-1), ...name("gameframes")])),
        ...script("float", returned("result", param("x")), field("x", floating(0x80000000))),
        ...script(
            "expression",
            conditional([0x0e, ...param("x"), 0x0f], returned("truth", integer(1)), returned("truth", integer(0))),
        ),
        ...script(
            "callcondition",
            conditional(name("zero"), returned("truth", integer(1)), returned("truth", integer(0))),
        ),
        ...script("spread", [...command("echo_child", [0x2c]), ...returned("done", integer(1))]),
        ...script("echo_child", returned("forwarded", param("x"))),
        ...script("floatwait", command("Wait", [...integer(1), ...floating(0x3f800000), ...name("frames")])),
        ...script("mixedwait", command("Wait", [...integer(1), ...name("frames"), ...name("seconds")])),
        0,
    ]);
}
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    const output = process.argv[2];
    if (!output) throw Error("Expected output JS path");
    writeFileSync(output, translate(fixture(), "synthetic.qb"));
}
