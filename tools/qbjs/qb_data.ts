// NOT ORIGINAL - a script file's data: its globals as JSON (`<name>.json` beside its `<name>.ts`, docs/qb-ts.md
// "Data"). The values are the ones the game builds for the file's globals - the runtime's value objects
// (scripting/values.js) - so the file reads back to exactly what the game had, without running any code.
// Dependency-free on purpose: a tool in another repository (a converter, a gate) may import or copy this one file.
//
// The file:
//
//   { "format": "qb-data 1", "source": "Data\\levels\\bo\\bo.qb", "from": "<the .qb's hash>",
//     "globals": { "<name>": <value>, ... } }
//
// Values - plain JSON where it can only mean one thing, a one-key object with a $ tag elsewhere:
//
//   integer            5, -3                           (a JSON number with no fraction or exponent: an integer)
//   float              1.25, 1e-7                      (a number that isn't whole: a float - its shortest spelling
//                      {"$float": 2}, {"$float": -0}    that reads back to the same 32 bits; a whole one, -0, and an
//                      {"$float": "0x7fc00000"}         infinity or NaN - its bits - are tagged)
//   name               "Class", "_0x1a2b3c4d"          (a JSON string: a checksum, by its text where the file's names
//                                                       know it, else _0x and its 8 hex digits)
//   string             {"$string": "text"}             (bytes: one character each, \u00XX past ASCII)
//   localised string   {"$local": "text"}
//   translated text    {"$tr": "bo_scripts.boston"}   (a string's token: its text from Data\lang\<language>.json -
//                      {"$local": {"$tr": "..."}}       tools/qbjs/localize.ts; English's when the language has none)
//   pair / vector      {"$pair": [x, y]}, {"$vec": [x, y, z]}   (floats: any number - whole ones and -0 too -, or
//                                                                "0x..." bits for infinities and NaN)
//   structure          {"Pos": {"$vec": [...]}, "CreatedAtStart": true}   (members in order; `true`: an unnamed
//                      {"$struct": [["a", 1], [null, 2], ...]}           name - a flag; the list form when the members
//                                                                         don't fit an object: an unnamed member that
//                                                                         isn't a name, a key twice, a key that
//                                                                         starts with $ or reads as an index)
//   array              [1, 2, 3]                       (one type of element: the first's; ints among floats are floats)
//   script             {"$script": "Masks#1"}          (a structure's `script ... endscript`: its code stays in the
//                                                       .ts, scriptValue("Masks#1", code(...)); the key is the global's
//                                                       name and its scripts' count)
//
// Read in TypeScript with readQbData (value objects as values.js makes them, with the names' text and the strings'
// tokens beside), in C++ by src/script_qb/ts_data.cpp (the game: straight to the script heap's cells).

export const FORMAT = "qb-data 1";

// The runtime's value objects (scripting/values.js): integers, floats and names as 32 bits; strings as bytes (token:
// a translated text's - its bytes the language's text when read with a table); structures as [name, value] members
// in order (name 0: unnamed); script values by their key.
export type QbDataValue =
    | { type: 1 | 2 | 13; bits: number }
    | { type: 3 | 4; bytes: number[]; token?: string }
    | { type: 5 | 6; coordinates: number[] }
    | { type: 7; key: string }
    | { type: 10; members: [number, QbDataValue][] }
    | { type: 12; elementType: number; elements: QbDataValue[] };

export type QbData = {
    source: string;
    from: string;
    globals: [number, QbDataValue][]; // in the file's order
    names: Map<number, string>; // every name's text the file spells out (checksum -> text)
};

// The game's checksum of a name (src/base/core/checksums.cpp): CRC-32 of its bytes with no final inversion, ASCII
// folded to lower case and / counted as \; `_0x` and 8 hex digits is that checksum itself.
const CRC_TABLE = Array.from({ length: 256 }, (_, n) => {
    for (let i = 0; i < 8; ++i) n = n & 1 ? (n >>> 1) ^ 0xedb88320 : n >>> 1;
    return n >>> 0;
});
export function nameChecksum(text: string): number {
    const hex = /^_0x([0-9a-fA-F]{8})$/.exec(text);
    if (hex) return Number.parseInt(hex[1]!, 16) >>> 0;
    let crc = 0xffffffff;
    for (let i = 0; i < text.length; ++i) {
        let byte = text.charCodeAt(i) & 0xff;
        if (byte >= 65 && byte <= 90) byte += 32;
        if (byte === 47) byte = 92;
        crc = (crc >>> 8) ^ CRC_TABLE[(crc ^ byte) & 0xff]!;
    }
    return crc >>> 0;
}

const floatOf = (bits: number) => new Float32Array(new Uint32Array([bits]).buffer)[0]!;
const bitsOf = (x: number) => new Uint32Array(new Float32Array([x]).buffer)[0]!;

// --- reading ----------------------------------------------------------------------------------------------------------

// `text`: the translated texts (token -> text: Data\lang\<language>.json, English's under it); without, a $tr string's
// bytes are its token's.
export function readQbData(json: string, text?: (token: string) => string | undefined): QbData {
    const file = JSON.parse(json) as { format?: unknown; source?: unknown; from?: unknown; globals?: unknown };
    if (file.format !== FORMAT) throw Error(`not a ${FORMAT} file (format ${JSON.stringify(file.format)})`);
    const names = new Map<number, string>();
    const name = (t: string) => {
        const n = nameChecksum(t);
        if (!/^_0x[0-9a-fA-F]{8}$/.test(t)) names.set(n, t);
        return n;
    };
    const bytes = (s: string) =>
        Array.from(s, (c) => {
            const b = c.charCodeAt(0);
            if (b > 0xff) throw Error(`a string's character past \\u00ff: ${JSON.stringify(s)}`);
            return b;
        });
    const coordinate = (x: unknown) => {
        if (typeof x === "number") return bitsOf(x);
        if (typeof x === "string" && /^0x[0-9a-fA-F]{1,8}$/.test(x)) return Number.parseInt(x, 16) >>> 0;
        throw Error(`not a coordinate: ${JSON.stringify(x)}`);
    };
    const textValue = (type: 3 | 4, x: unknown): QbDataValue => {
        if (typeof x === "string") return { type, bytes: bytes(x) };
        const token = (x as { $tr?: unknown } | null)?.$tr;
        if (typeof token !== "string") throw Error(`not a string: ${JSON.stringify(x)}`);
        return { type, bytes: bytes(text?.(token) ?? token), token };
    };
    const value = (x: unknown): QbDataValue => {
        if (typeof x === "number")
            return Number.isInteger(x) && !Object.is(x, -0) ? { type: 1, bits: x >>> 0 } : { type: 2, bits: bitsOf(x) };
        if (typeof x === "string") return { type: 13, bits: name(x) };
        if (Array.isArray(x)) {
            const items = x.map(value);
            const floats = items.some((v) => v.type === 2) && items.every((v) => v.type === 1 || v.type === 2);
            const elementType = floats ? 2 : (items[0]?.type ?? 0);
            const elements = items.map((v) =>
                floats && v.type === 1 ? { type: 2 as const, bits: bitsOf(v.bits | 0) } : v,
            );
            if (elements.some((v) => v.type !== elementType)) throw Error("an array's elements of different types");
            return { type: 12, elementType, elements };
        }
        if (x === null || typeof x !== "object") throw Error(`not a value: ${JSON.stringify(x)}`);
        const keys = Object.keys(x);
        const tag = keys.length === 1 && keys[0]!.startsWith("$") ? keys[0]! : undefined;
        const inner = tag ? (x as Record<string, unknown>)[tag] : undefined;
        switch (tag) {
            case undefined:
                return {
                    type: 10,
                    members: Object.entries(x).map(([k, v]): [number, QbDataValue] =>
                        v === true ? [0, { type: 13, bits: name(k) }] : [name(k), value(v)],
                    ),
                };
            case "$float":
                if (typeof inner === "number") return { type: 2, bits: bitsOf(inner) };
                return { type: 2, bits: coordinate(inner) };
            case "$string":
            case "$tr":
                return textValue(3, tag === "$tr" ? x : inner);
            case "$local":
                return textValue(4, inner);
            case "$pair":
            case "$vec": {
                const n = tag === "$pair" ? 2 : 3;
                if (!Array.isArray(inner) || inner.length !== n) throw Error(`${tag} takes ${n} coordinates`);
                return { type: tag === "$pair" ? 5 : 6, coordinates: inner.map(coordinate) };
            }
            case "$struct":
                if (!Array.isArray(inner)) throw Error("$struct takes a list of [key, value]");
                return {
                    type: 10,
                    members: inner.map((m): [number, QbDataValue] => {
                        if (!Array.isArray(m) || m.length !== 2) throw Error("a $struct member is [key, value]");
                        return [m[0] === null ? 0 : name(String(m[0])), value(m[1])];
                    }),
                };
            case "$script":
                if (typeof inner !== "string") throw Error("$script takes its key");
                return { type: 7, key: inner };
            default:
                throw Error(`unknown tag ${tag}`);
        }
    };
    const globals = file.globals;
    if (globals === null || typeof globals !== "object" || Array.isArray(globals)) throw Error("no globals");
    return {
        source: String(file.source ?? ""),
        from: String(file.from ?? ""),
        globals: Object.entries(globals).map(([k, v]) => [name(k), value(v)]),
        names,
    };
}

// A structure's member by name (the last of that name, as the game finds one), and whether it has a flag.
export function member(structure: QbDataValue, key: string): QbDataValue | undefined {
    if (structure.type !== 10) return undefined;
    const n = nameChecksum(key);
    return structure.members.findLast(([name]) => name === n)?.[1];
}
export function hasFlag(structure: QbDataValue, flag: string): boolean {
    const n = nameChecksum(flag);
    return structure.type === 10 && structure.members.some(([k, v]) => k === 0 && v.type === 13 && v.bits === n);
}

// --- writing ----------------------------------------------------------------------------------------------------------

const WIDTH = 120;
const INDENT = "    ";

// A float's spelling: the shortest decimal that reads back to its 32 bits (through a double, as JavaScript's
// Math.fround(Number(text)) and C++'s (float)strtod read it), or null for infinities and NaN.
function floatDigits(bits: number): string | null {
    const f = floatOf(bits);
    if (!Number.isFinite(f)) return null;
    if (Object.is(f, -0)) return "-0";
    for (let digits = 1; digits <= 9; ++digits) {
        const text = String(Number(f.toPrecision(digits)));
        if (Math.fround(Number(text)) === f) return text;
    }
    throw Error(`float 0x${bits.toString(16)} has no spelling`);
}
const bitsText = (bits: number) => `"0x${(bits >>> 0).toString(16).padStart(8, "0")}"`;
function floatText(bits: number): string {
    const text = floatDigits(bits);
    if (text === null) return `{"$float": ${bitsText(bits)}}`;
    return Number.isInteger(Number(text)) ? `{"$float": ${text}}` : text;
}
const coordinateText = (bits: number) => floatDigits(bits) ?? bitsText(bits);

// A string's bytes as a JSON string: ASCII as it is, every other byte \u00XX.
function stringText(bytes: readonly number[]): string {
    let out = '"';
    for (const b of bytes) {
        if (b === 0x22) out += '\\"';
        else if (b === 0x5c) out += "\\\\";
        else if (b === 0x0a) out += "\\n";
        else if (b === 0x09) out += "\\t";
        else if (b === 0x0d) out += "\\r";
        else if (b >= 0x20 && b < 0x7f) out += String.fromCharCode(b);
        else out += `\\u00${b.toString(16).padStart(2, "0")}`;
    }
    return `${out}"`;
}

// A piece of the file: flat text, or a bracketed list that may break one item per line.
type Doc = string | { open: string; items: Doc[]; close: string; flat: string };
const list = (open: string, items: Doc[], close: string): Doc => ({
    open,
    items,
    close,
    flat: `${open}${items.map((i) => (typeof i === "string" ? i : i.flat)).join(", ")}${close}`,
});
function layout(d: Doc, indent: string, used: number, out: string[]): void {
    if (typeof d === "string") {
        out.push(d);
        return;
    }
    if (used + d.flat.length <= WIDTH || !d.items.length) {
        out.push(d.flat);
        return;
    }
    const inner = indent + INDENT;
    out.push(`${d.open.trimEnd()}\n`);
    d.items.forEach((item, i) => {
        out.push(inner);
        layout(item, inner, inner.length, out);
        out.push(i < d.items.length - 1 ? ",\n" : "\n");
    });
    out.push(`${indent}${d.close.trimStart()}`);
}

// `nameText`: a checksum's text where known (the writer checks it spells that checksum), else undefined.
export function writeQbData(
    data: { source: string; from: string; globals: [number, QbDataValue][] },
    nameText: (n: number) => string | undefined = () => undefined,
): string {
    const nameOf = (n: number) => {
        const t = nameText(n);
        return t !== undefined && /^[\x20-\x7e]+$/.test(t) && nameChecksum(t) === n
            ? t
            : `_0x${(n >>> 0).toString(16).padStart(8, "0")}`;
    };
    const keyText = (n: number) => JSON.stringify(nameOf(n));
    const value = (v: QbDataValue): Doc => {
        switch (v.type) {
            case 1:
                return String(v.bits | 0);
            case 2:
                return floatText(v.bits);
            case 13:
                return keyText(v.bits);
            case 3:
            case 4: {
                const text = v.token !== undefined ? `{"$tr": ${JSON.stringify(v.token)}}` : stringText(v.bytes);
                return v.type === 3 ? (v.token !== undefined ? text : `{"$string": ${text}}`) : `{"$local": ${text}}`;
            }
            case 5:
            case 6:
                return `{"${v.type === 5 ? "$pair" : "$vec"}": [${v.coordinates.map(coordinateText).join(", ")}]}`;
            case 7:
                return `{"$script": ${JSON.stringify(v.key)}}`;
            case 12:
                return list("[", v.elements.map(value), "]");
            case 10: {
                const keys = v.members.map(([n, m]) => (n === 0 ? (m.type === 13 ? nameOf(m.bits) : null) : nameOf(n)));
                const asObject =
                    keys.every((k) => k !== null && !k.startsWith("$") && !/^(0|[1-9]\d*)$/.test(k)) &&
                    new Set(keys).size === keys.length;
                if (asObject)
                    return list(
                        "{",
                        v.members.map(([n, m], i) => {
                            const key = `${JSON.stringify(keys[i])}: `;
                            if (n === 0) return `${key}true`;
                            return prefixed(key, value(m));
                        }),
                        "}",
                    );
                return list(
                    '{"$struct": [',
                    v.members.map(([n, m]) => list("[", [n === 0 ? "null" : keyText(n), value(m)], "]")),
                    "]}",
                );
            }
        }
    };
    const prefixed = (key: string, d: Doc): Doc =>
        typeof d === "string" ? key + d : { ...d, open: key + d.open, flat: key + d.flat };
    // (a name defined twice: the last definition - as the loader leaves it)
    const last = new Map<number, QbDataValue>();
    for (const [n, v] of data.globals) last.set(n, v);
    const top = list(
        "{",
        [
            `"format": ${JSON.stringify(FORMAT)}`,
            `"source": ${JSON.stringify(data.source)}`,
            `"from": ${JSON.stringify(data.from)}`,
            prefixed(
                '"globals": ',
                list(
                    "{",
                    [...last].map(([n, v]) => prefixed(`${keyText(n)}: `, value(v))),
                    "}",
                ),
            ),
        ],
        "}",
    );
    const out: string[] = [];
    layout(top, "", WIDTH + 1, out); // (begun past the width: the file's own object always broken over lines)
    return `${out.join("")}\n`;
}
