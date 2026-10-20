// NOT ORIGINAL - what the tools ported from Python need to behave exactly as their Python twins did
// (docs/typescript.md, "Python -> JavaScript traps"): Python's float formatting and rounding, its compensated sum()
// and math.dist, json.dump's text, text-mode files (CRLF on Windows), sys.exit, and a few str methods whose
// JavaScript namesakes differ.

import * as fs from "node:fs";

// --- floats that must stay floats -------------------------------------------------------------------------------

// A Python float where JavaScript's number can't tell it from an int: a qb value (1.0 is written "1.0", 1 is "1"),
// a json.dump'd 1.0. Everything else uses plain numbers.
export class Float {
    readonly value: number;
    constructor(value: number) {
        this.value = value;
    }
}

// --- rounding -------------------------------------------------------------------------------------------------------

// Python's round(x) of a float: to the nearest integer, halves to the even one (round(0.5) = 0, round(2.5) = 2) -
// not Math.round (halves up). Raises on inf / nan as Python.
export function roundHalfEven(x: number): number {
    if (!Number.isFinite(x)) throw new RangeError(`cannot round ${x} to an integer`);
    const f = Math.floor(x);
    const d = x - f; // (exact for every double)
    let r: number;
    if (d > 0.5) r = f + 1;
    else if (d < 0.5) r = f;
    else r = f % 2 === 0 ? f : f + 1;
    return r === 0 ? 0 : r; // (an int has no -0)
}

// Python's round(x, ndigits) of a float: the double nearest the exact value rounded to ndigits decimals, halves to
// even on the exact binary value (round(2.675, 2) = 2.67, round(0.125, 2) = 0.12).
export function pyRound(x: number, ndigits: number): number {
    if (!Number.isFinite(x)) return x;
    return Number.parseFloat(formatFixed(x, ndigits));
}

// --- float text -----------------------------------------------------------------------------------------------------

function decompose(x: number): { negative: boolean; mantissa: bigint; exponent: number } {
    // |x| = mantissa * 2^exponent exactly (x finite)
    const view = new DataView(new ArrayBuffer(8));
    view.setFloat64(0, x);
    const hi = view.getUint32(0);
    const lo = view.getUint32(4);
    const negative = (hi & 0x80000000) !== 0;
    const biased = (hi >>> 20) & 0x7ff;
    let mantissa = (BigInt(hi & 0xfffff) << 32n) | BigInt(lo);
    let exponent: number;
    if (biased === 0) {
        exponent = -1074;
    } else {
        mantissa |= 1n << 52n;
        exponent = biased - 1075;
    }
    return { negative, mantissa, exponent };
}

// Python's "%.<digits>f" % x: the exact binary value correctly rounded (halves to even), the sign kept on a negative
// value that rounds to zero ("-0.000"), "inf" / "-inf" / "nan". (Number.toFixed rounds halves up and goes
// exponential from 1e21.)
export function formatFixed(x: number, digits: number): string {
    if (Number.isNaN(x)) return "nan";
    if (x === Number.POSITIVE_INFINITY) return "inf";
    if (x === Number.NEGATIVE_INFINITY) return "-inf";
    const { negative, mantissa, exponent } = decompose(x);
    const scale = 10n ** BigInt(digits);
    let q: bigint;
    if (exponent >= 0) {
        q = (mantissa << BigInt(exponent)) * scale;
    } else {
        const num = mantissa * scale;
        const den = 1n << BigInt(-exponent);
        q = num / den;
        const twice = (num % den) * 2n;
        if (twice > den || (twice === den && q % 2n === 1n)) q += 1n;
    }
    let text = q.toString();
    if (digits > 0) {
        text = text.padStart(digits + 1, "0");
        text = `${text.slice(0, text.length - digits)}.${text.slice(text.length - digits)}`;
    }
    return (negative ? "-" : "") + text;
}

// Python's "%g" % x / "%.<precision>g" % x: `precision` significant digits (the exact binary value correctly
// rounded, halves to even: "%g" % 100.0625 == "100.062" - toPrecision rounds that half up), fixed notation for
// exponents -4 ... precision - 1, else d.ddde+XX; trailing zeros and a trailing point dropped; "-0", "inf", "nan".
export function formatG(x: number, precision = 6): string {
    if (Number.isNaN(x)) return "nan";
    if (x === Number.POSITIVE_INFINITY) return "inf";
    if (x === Number.NEGATIVE_INFINITY) return "-inf";
    const p = Math.max(precision, 1);
    const sign = x < 0 || Object.is(x, -0) ? "-" : "";
    if (x === 0) return `${sign}0`;
    const { mantissa, exponent } = decompose(x);
    // |x| = num / den exactly
    let num = exponent >= 0 ? mantissa << BigInt(exponent) : mantissa;
    let den = exponent >= 0 ? 1n : 1n << BigInt(-exponent);
    // the decimal exponent X: 10^X <= |x| < 10^(X + 1)
    let X = Math.floor(Math.log10(Math.abs(x)));
    const pow10 = (n: number): bigint => 10n ** BigInt(n);
    const atLeast = (e: number): boolean => (e >= 0 ? num >= pow10(e) * den : num * pow10(-e) >= den);
    while (!atLeast(X)) X -= 1;
    while (atLeast(X + 1)) X += 1;
    // digits = |x| / 10^(X - p + 1), rounded halves to even
    const shift = X - p + 1;
    if (shift >= 0) den *= pow10(shift);
    else num *= pow10(-shift);
    let digits = num / den;
    const twice = (num % den) * 2n;
    if (twice > den || (twice === den && digits % 2n === 1n)) digits += 1n;
    if (digits === pow10(p)) {
        digits /= 10n;
        X += 1;
    }
    let text = digits.toString();
    const strip = (t: string): string => (t.includes(".") ? t.replace(/0+$/, "").replace(/\.$/, "") : t);
    if (X < -4 || X >= p) {
        const body = strip(text.length > 1 ? `${text[0]}.${text.slice(1)}` : text);
        return `${sign}${body}e${X < 0 ? "-" : "+"}${Math.abs(X).toString().padStart(2, "0")}`;
    }
    const decimals = p - 1 - X;
    if (decimals > 0) {
        text = text.padStart(decimals + 1, "0");
        text = `${text.slice(0, text.length - decimals)}.${text.slice(text.length - decimals)}`;
    }
    return sign + strip(text);
}

// The shortest digits that read back as x, and the decimal exponent: x = 0.d1d2d3... * 10^point.
function shortestDigits(x: number): { digits: string; point: number } {
    const text = String(Math.abs(x)); // (ECMAScript's Number::toString: the shortest round-trip digits)
    const e = text.indexOf("e");
    let mantissa = text;
    let exp = 0;
    if (e >= 0) {
        mantissa = text.slice(0, e);
        exp = Number.parseInt(text.slice(e + 1), 10);
    }
    const dot = mantissa.indexOf(".");
    let whole = mantissa;
    let fraction = "";
    if (dot >= 0) {
        whole = mantissa.slice(0, dot);
        fraction = mantissa.slice(dot + 1);
    }
    let digits = whole + fraction;
    let point = whole.length + exp;
    const lead = digits.length - digits.replace(/^0+/, "").length;
    digits = digits.slice(lead);
    point -= lead;
    digits = digits.replace(/0+$/, "");
    if (!digits) return { digits: "0", point: 1 };
    return { digits, point };
}

// Python's repr(float) / str(float): the shortest round-trip digits, fixed notation for exponents -4 ... 15
// ("0.0001", "1e-05", "1e+16", "100.0"), "inf", "nan", "-0.0". (JavaScript's String(number) switches at 1e-7 / 1e21
// and drops ".0".)
export function pyFloatRepr(x: number): string {
    if (Number.isNaN(x)) return "nan";
    if (x === Number.POSITIVE_INFINITY) return "inf";
    if (x === Number.NEGATIVE_INFINITY) return "-inf";
    const sign = x < 0 || Object.is(x, -0) ? "-" : "";
    if (x === 0) return `${sign}0.0`;
    const { digits, point } = shortestDigits(x);
    const exp = point - 1; // x = d.ddd * 10^exp
    if (exp < -4 || exp >= 16) {
        const mantissa = digits.length > 1 ? `${digits[0]}.${digits.slice(1)}` : digits;
        const e = Math.abs(exp).toString().padStart(2, "0");
        return `${sign}${mantissa}e${exp < 0 ? "-" : "+"}${e}`;
    }
    if (point <= 0) return `${sign}0.${"0".repeat(-point)}${digits}`;
    if (point >= digits.length) return `${sign}${digits}${"0".repeat(point - digits.length)}.0`;
    return `${sign}${digits.slice(0, point)}.${digits.slice(point)}`;
}

// Python's float(text): strict (no "", no hex, no trailing junk), whitespace and underscores between digits allowed,
// inf / nan / infinity in any case. Raises a ValueError-like RangeError otherwise.
export function pyParseFloat(text: string): number {
    const t = text.trim();
    const special = t.toLowerCase().replace(/^[+-]/, "");
    if (special === "inf" || special === "infinity") {
        return t.startsWith("-") ? Number.NEGATIVE_INFINITY : Number.POSITIVE_INFINITY;
    }
    if (special === "nan") return Number.NaN;
    const number = /^[+-]?(?:\d(?:_?\d)*(?:\.(?:\d(?:_?\d)*)?)?|\.\d(?:_?\d)*)(?:[eE][+-]?\d(?:_?\d)*)?$/;
    if (!number.test(t)) throw new RangeError(`could not convert string to float: ${pyStrRepr(text)}`);
    return Number(t.replaceAll("_", ""));
}

// --- sums and distances ---------------------------------------------------------------------------------------------

// Python's sum() of floats (3.12+): Neumaier's compensated summation, not plain left-to-right addition - a sum of
// squares can differ in the last bit. (Python's sum of ints is exact: plain addition here.) An empty sum is 0.
export function pySum(values: Iterable<number>): number {
    let total = 0;
    let c = 0;
    let first = true;
    for (const x of values) {
        if (first) {
            total = 0 + x;
            first = false;
            continue;
        }
        const t = total + x;
        if (Math.abs(total) >= Math.abs(x)) c += total - t + x;
        else c += x - t + total;
        total = t;
    }
    if (c !== 0 && Number.isFinite(c)) total += c;
    return total;
}

// Exact x * y as hi + lo (Dekker / Veltkamp; Python's dl_mul uses fma - the same exact result without overflow).
function twoProduct(x: number, y: number): [number, number] {
    const p = x * y;
    const SPLIT = 134217729; // 2^27 + 1
    let t = SPLIT * x;
    const xh = t - (t - x);
    const xl = x - xh;
    t = SPLIT * y;
    const yh = t - (t - y);
    const yl = y - yh;
    const lo = xh * yh - p + xh * yl + xl * yh + xl * yl;
    return [p, lo];
}

function fastSum(a: number, b: number): [number, number] {
    const x = a + b;
    const z = x - a;
    return [x, b - z];
}

const DBL_MIN = 2.2250738585072014e-308;

function frexpExponent(x: number): number {
    // e with x = m * 2^e, 0.5 <= m < 1 (x > 0, finite)
    const view = new DataView(new ArrayBuffer(8));
    view.setFloat64(0, x);
    const biased = (view.getUint16(0) >>> 4) & 0x7ff;
    if (biased === 0) return frexpExponent(x * 2 ** 64) - 64;
    return biased - 1022;
}

// CPython's vector_norm (math.hypot / math.dist): scaled by the largest, squares and sums kept to double-double
// precision, one differential correction - correctly rounded in nearly all cases, so it can differ from
// Math.sqrt(sum of squares) and from Math.hypot in the last bit.
function vectorNorm(vec: number[], max: number, foundNan: boolean): number {
    if (max === Number.POSITIVE_INFINITY) return max;
    if (foundNan) return Number.NaN;
    if (max === 0 || vec.length <= 1) return max;
    const maxE = frexpExponent(max);
    if (maxE < -1023) {
        return (
            DBL_MIN *
            vectorNorm(
                vec.map((v) => v / DBL_MIN),
                max / DBL_MIN,
                foundNan,
            )
        );
    }
    const scale = 2 ** -maxE;
    let csum = 1.0;
    let frac1 = 0.0;
    let frac2 = 0.0;
    for (const v of vec) {
        const x = v * scale;
        const [hi, lo] = twoProduct(x, x);
        const [sHi, sLo] = fastSum(csum, hi);
        csum = sHi;
        frac1 += lo;
        frac2 += sLo;
    }
    let h = Math.sqrt(csum - 1.0 + (frac1 + frac2));
    const [hi, lo] = twoProduct(-h, h);
    const [sHi, sLo] = fastSum(csum, hi);
    csum = sHi;
    frac1 += lo;
    frac2 += sLo;
    const x = csum - 1.0 + (frac1 + frac2);
    h += x / (2.0 * h);
    return h / scale;
}

// Python's math.dist(p, q).
export function pyDist(p: readonly number[], q: readonly number[]): number {
    if (p.length !== q.length) throw new RangeError("both points must have the same number of dimensions");
    const diffs: number[] = [];
    let max = 0.0;
    let foundNan = false;
    for (let i = 0; i < p.length; i++) {
        const x = Math.abs(p[i]! - q[i]!);
        diffs.push(x);
        if (Number.isNaN(x)) foundNan = true;
        if (x > max) max = x;
    }
    return vectorNorm(diffs, max, foundNan);
}

// Python's math.hypot(*values).
export function pyHypot(...values: number[]): number {
    const vec = values.map(Math.abs);
    let max = 0.0;
    let foundNan = false;
    for (const x of vec) {
        if (Number.isNaN(x)) foundNan = true;
        if (x > max) max = x;
    }
    return vectorNorm(vec, max, foundNan);
}

// Python's floor division and modulo (the result takes the divisor's sign: -7 // 2 = -4, -7 % 2 = 1).
export function floorDiv(a: number, b: number): number {
    if (b === 0) throw new RangeError("integer division or modulo by zero");
    return Math.floor(a / b);
}

export function pyMod(a: number, b: number): number {
    if (b === 0) throw new RangeError("integer division or modulo by zero");
    const r = a % b;
    return r !== 0 && r < 0 !== b < 0 ? r + b : r;
}

// int.from_bytes(data, "little", signed=...) of up to 6 bytes (0 for none).
export function intFromBytesLE(data: Uint8Array, signed = false): number {
    if (data.length > 6) throw new RangeError("intFromBytesLE: more than 6 bytes - use BigInt");
    let value = 0;
    for (let i = data.length - 1; i >= 0; i--) value = value * 256 + data[i]!;
    if (signed && data.length && data[data.length - 1]! & 0x80) value -= 2 ** (8 * data.length);
    return value;
}

// Python's int(text, 16): hex digits (underscores between them, an optional 0x, a sign, surrounding whitespace);
// raises otherwise (parseInt would stop quietly at the first bad character).
export function pyIntHex(text: string): number {
    const m = /^\s*([+-]?)(?:0[xX]_?)?([0-9A-Fa-f](?:_?[0-9A-Fa-f])*)\s*$/.exec(text);
    if (!m) throw new RangeError(`invalid literal for int() with base 16: ${pyStrRepr(text)}`);
    const value = Number.parseInt(m[2]!.replaceAll("_", ""), 16);
    return m[1] === "-" ? -value : value;
}

// --- str ------------------------------------------------------------------------------------------------------------

// "%08X" (u32 checksums: give `value >>> 0` if it can be negative)
export function hex8(value: number): string {
    return (value >>> 0).toString(16).toUpperCase().padStart(8, "0");
}

// Python's str.isalnum / str.isdigit / str.isalpha of one character (Unicode-aware, as Python: "é".isalnum() is
// true, "²".isdigit() is true - JavaScript's \w and \d are ASCII only).
export function isAlnumChar(ch: string): boolean {
    return /^[\p{L}\p{N}]$/u.test(ch);
}

export function isDigitChar(ch: string): boolean {
    return /^\p{Nd}$/u.test(ch) || ch === "\u00b2" || ch === "\u00b3" || ch === "\u00b9";
}

export function isAlphaChar(ch: string): boolean {
    return /^\p{L}$/u.test(ch);
}

const PY_WHITESPACE =
    " \t\n\r\x0b\x0c\x1c\x1d\x1e\x1f\x85\xa0\u1680\u2000\u2001\u2002\u2003\u2004\u2005\u2006\u2007\u2008\u2009\u200a\u2028\u2029\u202f\u205f\u3000";

// Python's str.strip() (no arguments): Python's whitespace, which differs from String.trim's (trim takes \ufeff,
// strip takes \x1c-\x1f and \x85).
export function pyStrip(text: string): string {
    let a = 0;
    let b = text.length;
    while (a < b && PY_WHITESPACE.includes(text[a]!)) a++;
    while (b > a && PY_WHITESPACE.includes(text[b - 1]!)) b--;
    return text.slice(a, b);
}

// Python's str.splitlines(): splits on \n, \r\n, \r, \v, \f, \x1c-\x1e, \x85, \u2028, \u2029 (not only \n).
// keepends: str.splitlines(keepends=True), each line with its line end.
export function pySplitlines(text: string, keepends = false): string[] {
    const out: string[] = [];
    const re = /\r\n|[\n\r\x0b\x0c\x1c\x1d\x1e\x85\u2028\u2029]/g;
    let last = 0;
    for (let m = re.exec(text); m; m = re.exec(text)) {
        out.push(text.slice(last, keepends ? m.index + m[0].length : m.index));
        last = m.index + m[0].length;
    }
    if (last < text.length) out.push(text.slice(last));
    return out;
}

// Python's str.split() (no arguments): runs of Python's whitespace separate, none at the ends.
export function pySplit(text: string): string[] {
    const out: string[] = [];
    let word = "";
    for (const ch of text) {
        if (PY_WHITESPACE.includes(ch)) {
            if (word) out.push(word);
            word = "";
        } else word += ch;
    }
    if (word) out.push(word);
    return out;
}

// Python's str.title() for cased letters: each run of letters capitalised, the rest lower case ("EAST LOS ANGELES"
// -> "East Los Angeles", "it's" -> "It'S" as Python).
export function pyTitle(text: string): string {
    let out = "";
    let previousCased = false;
    for (const ch of text) {
        const cased = ch.toLowerCase() !== ch.toUpperCase();
        out += cased ? (previousCased ? ch.toLowerCase() : ch.toUpperCase()) : ch;
        previousCased = cased;
    }
    return out;
}

// Python's repr(str): single quotes unless the text has ' and no ", backslash escapes, \x.. / \u.... for what
// isn't printable.
export function pyStrRepr(text: string): string {
    const quote = text.includes("'") && !text.includes('"') ? '"' : "'";
    let out = quote;
    for (const ch of text) {
        const c = ch.codePointAt(0)!;
        if (ch === quote || ch === "\\") out += `\\${ch}`;
        else if (ch === "\n") out += "\\n";
        else if (ch === "\r") out += "\\r";
        else if (ch === "\t") out += "\\t";
        else if (ch !== " " && /^[\p{Cc}\p{Cf}\p{Cs}\p{Co}\p{Cn}\p{Zl}\p{Zp}\p{Zs}]$/u.test(ch)) {
            // (not str.isprintable(): escaped)
            if (c <= 0xff) out += `\\x${c.toString(16).padStart(2, "0")}`;
            else if (c <= 0xffff) out += `\\u${c.toString(16).padStart(4, "0")}`;
            else out += `\\U${c.toString(16).padStart(8, "0")}`;
        } else out += ch;
    }
    return out + quote;
}

// Python's repr(bytes): b'...' (b"..." when it has ' and no "), \\ \t \n \r escaped, other bytes outside space ... "~"
// as \xhh.
export function pyBytesRepr(data: Uint8Array): string {
    const quote = data.includes(0x27) && !data.includes(0x22) ? '"' : "'";
    let out = `b${quote}`;
    for (const b of data) {
        const ch = String.fromCharCode(b);
        if (ch === quote || ch === "\\") out += `\\${ch}`;
        else if (ch === "\t") out += "\\t";
        else if (ch === "\n") out += "\\n";
        else if (ch === "\r") out += "\\r";
        else if (b < 0x20 || b >= 0x7f) out += `\\x${b.toString(16).padStart(2, "0")}`;
        else out += ch;
    }
    return out + quote;
}

// repr() of plain values as Python prints them (print(a_dict)): str, int, float (Float), bool, None, list, dict
// (a Map or a plain object), tuple (a readonly array tagged with pyTuple).
export function pyRepr(value: unknown): string {
    if (value === null || value === undefined) return "None";
    if (value === true) return "True";
    if (value === false) return "False";
    if (typeof value === "string") return pyStrRepr(value);
    if (typeof value === "number") return Number.isInteger(value) ? String(value) : pyFloatRepr(value);
    if (value instanceof Float) return pyFloatRepr(value.value);
    if (Array.isArray(value)) {
        const items = value.map(pyRepr);
        if (TUPLES.has(value)) return items.length === 1 ? `(${items[0]},)` : `(${items.join(", ")})`;
        return `[${items.join(", ")}]`;
    }
    const entries = value instanceof Map ? [...value.entries()] : Object.entries(value as object);
    return `{${entries.map(([k, v]) => `${pyRepr(k)}: ${pyRepr(v)}`).join(", ")}}`;
}

const TUPLES = new WeakSet<object>();

// Marks an array as a Python tuple for pyRepr ("(1, 2)").
export function pyTuple<T extends unknown[]>(items: T): T {
    TUPLES.add(items);
    return items;
}

// --- bytes <-> str --------------------------------------------------------------------------------------------------

// Python's bytes(list_of_ints): raises on a value outside 0 ... 255 (Buffer.from(array) keeps its low byte).
export function pyBytes(values: Iterable<number>): Buffer {
    const list = [...values];
    for (const v of list) {
        if (!Number.isInteger(v) || v < 0 || v > 255) throw new RangeError(`bytes must be in range(0, 256): ${v}`);
    }
    return Buffer.from(list);
}

// bytes.decode("latin-1") / str.encode("latin-1") (raises past U+00FF, as Python - Buffer.from(text, "latin1") would
// keep the low byte).
export function decodeLatin1(data: Uint8Array): string {
    return Buffer.from(data.buffer, data.byteOffset, data.byteLength).toString("latin1");
}

export function encodeLatin1(text: string): Buffer {
    for (let i = 0; i < text.length; i++) {
        if (text.charCodeAt(i) > 0xff) {
            throw new RangeError(`'latin-1' codec can't encode character at position ${i}`);
        }
    }
    return Buffer.from(text, "latin1");
}

// str.encode("ascii") (raises past U+007F).
export function encodeAscii(text: string): Buffer {
    for (let i = 0; i < text.length; i++) {
        if (text.charCodeAt(i) > 0x7f) throw new RangeError(`'ascii' codec can't encode character at position ${i}`);
    }
    return Buffer.from(text, "latin1");
}

// bytes.decode("ascii", "replace"): U+FFFD for each byte past 0x7F (Buffer's "ascii" decoding drops the top bit
// instead: 0xE9 -> "i").
export function decodeAsciiReplace(data: Uint8Array): string {
    let out = "";
    for (const b of data) out += b < 0x80 ? String.fromCharCode(b) : "\ufffd";
    return out;
}

// cp1252 (Windows' ANSI code page: what Python's subprocess text=True, print to a pipe and open() without an encoding
// use on this machine). Its five undefined bytes raise, as Python's codec.
const CP1252_HIGH = "€\u0081‚ƒ„…†‡ˆ‰Š‹Œ\u008dŽ\u008f" + "\u0090‘’“”•–—˜™š›œ\u009džŸ";

export function decodeCp1252(data: Uint8Array): string {
    let out = "";
    for (const b of data) {
        if (b >= 0x80 && b < 0xa0) {
            if (b === 0x81 || b === 0x8d || b === 0x8f || b === 0x90 || b === 0x9d) {
                throw new RangeError(`'charmap' codec can't decode byte 0x${b.toString(16)}`);
            }
            out += CP1252_HIGH[b - 0x80];
        } else {
            out += String.fromCharCode(b);
        }
    }
    return out;
}

const UTF8 = new TextDecoder("utf-8", { fatal: true, ignoreBOM: true });
const UTF8_REPLACE = new TextDecoder("utf-8", { fatal: false, ignoreBOM: true });

// bytes.decode("utf-8") (strict; a BOM is kept as U+FEFF, as Python's "utf-8" - TextDecoder drops it by default).
export function decodeUtf8(data: Uint8Array, errors: "strict" | "replace" = "strict"): string {
    return (errors === "strict" ? UTF8 : UTF8_REPLACE).decode(data);
}

// --- text files as Python's open(..., "w") / open(...) ---------------------------------------------------------------

// Python's text mode on Windows writes each "\n" as "\r\n" (open(path, "w", encoding="utf-8")). Use this where the
// Python tool wrote a text file, so the bytes are the same (levels.json, NeverScript sources, reports).
export function writeTextPy(path: string, text: string): void {
    fs.writeFileSync(path, process.platform === "win32" ? text.replaceAll("\n", "\r\n") : text, "utf-8");
}

// Python's text mode reading (open(path, encoding=...).read()): "\r\n" and "\r" read as "\n" (universal newlines).
export function readTextPy(path: string, errors: "strict" | "replace" = "strict"): string {
    return decodeUtf8(fs.readFileSync(path), errors).replace(/\r\n?/g, "\n");
}

// --- json -----------------------------------------------------------------------------------------------------------

function jsonString(text: string): string {
    // json.dumps' ensure_ascii form: everything outside space ... "~" escaped (\u00e9, \u007f, \ud83d\ude00)
    let out = '"';
    for (let i = 0; i < text.length; i++) {
        const ch = text[i]!;
        const c = text.charCodeAt(i);
        if (ch === '"') out += '\\"';
        else if (ch === "\\") out += "\\\\";
        else if (ch === "\n") out += "\\n";
        else if (ch === "\r") out += "\\r";
        else if (ch === "\t") out += "\\t";
        else if (ch === "\b") out += "\\b";
        else if (ch === "\f") out += "\\f";
        else if (c < 0x20 || c > 0x7e) out += `\\u${c.toString(16).padStart(4, "0")}`;
        else out += ch;
    }
    return `${out}"`;
}

function jsonFloat(x: number): string {
    if (Number.isNaN(x)) return "NaN";
    if (x === Number.POSITIVE_INFINITY) return "Infinity";
    if (x === Number.NEGATIVE_INFINITY) return "-Infinity";
    return pyFloatRepr(x);
}

// json.dumps(value, indent=indent) as Python writes it: ", " / ": " separators (",\n" with an indent), ASCII only,
// floats (Float, or non-integral numbers) as repr ("1.0", "1e-05"), integral numbers as ints. Maps and plain objects
// are dicts (keys must be strings; numbers are written as Python writes int keys: "1").
export function pyJsonDumps(value: unknown, indent?: number): string {
    const write = (v: unknown, level: number): string => {
        if (v === null || v === undefined) return "null";
        if (v === true) return "true";
        if (v === false) return "false";
        if (typeof v === "string") return jsonString(v);
        if (typeof v === "number") return Number.isInteger(v) ? String(v) : jsonFloat(v);
        if (v instanceof Float) return jsonFloat(v.value);
        const pad = indent === undefined ? "" : `\n${" ".repeat(indent * (level + 1))}`;
        const end = indent === undefined ? "" : `\n${" ".repeat(indent * level)}`;
        const sep = indent === undefined ? ", " : ",";
        if (Array.isArray(v)) {
            if (v.length === 0) return "[]";
            return `[${v.map((x) => pad + write(x, level + 1)).join(sep)}${end}]`;
        }
        const entries = v instanceof Map ? [...v.entries()] : Object.entries(v as object);
        if (entries.length === 0) return "{}";
        return `{${entries.map(([k, x]) => `${pad}${jsonString(String(k))}: ${write(x, level + 1)}`).join(sep)}${end}}`;
    };
    return write(value, 0);
}

type JsonReviverContext = { source?: string };

const SPECIAL: Record<string, number> = {
    "\u0000NaN": Number.NaN,
    "\u0000Infinity": Number.POSITIVE_INFINITY,
    "\u0000-Infinity": Number.NEGATIVE_INFINITY,
};

// json.loads: Python's json also reads NaN, Infinity and -Infinity (json.dump writes them for nan / inf); JSON.parse
// refuses them. floats "Float": numbers written with a "." or an exponent (and NaN / Infinity) come back as Float -
// json.loads gives a float for "1.0" - the others as numbers; "number": all as numbers. Plain objects for dicts.
export function pyJsonLoads(text: string, floats: "Float" | "number" = "Float"): unknown {
    // (NaN / Infinity outside strings made strings JSON.parse reads, turned back by the reviver)
    let prepared = "";
    let last = 0;
    let inString = false;
    for (let i = 0; i < text.length; i++) {
        const ch = text[i];
        if (inString) {
            if (ch === "\\") i++;
            else if (ch === '"') inString = false;
            continue;
        }
        if (ch === '"') {
            inString = true;
            continue;
        }
        for (const word of ["-Infinity", "Infinity", "NaN"]) {
            if (text.startsWith(word, i)) {
                prepared += `${text.slice(last, i)}"\\u0000${word}"`;
                i += word.length - 1;
                last = i + 1;
                break;
            }
        }
    }
    prepared += text.slice(last);
    const reviver = (_key: string, value: unknown, context?: JsonReviverContext): unknown => {
        if (typeof value === "string" && value in SPECIAL) {
            return floats === "Float" ? new Float(SPECIAL[value]!) : SPECIAL[value]!;
        }
        if (
            floats === "Float" &&
            typeof value === "number" &&
            context?.source !== undefined &&
            /[.eE]/.test(context.source)
        ) {
            return new Float(value);
        }
        return value;
    };
    return JSON.parse(prepared, reviver as (key: string, value: unknown) => unknown);
}

// Python's int(text) (base 10): strict, surrounding whitespace, a sign and underscores between digits allowed.
export function pyParseInt(text: string): number {
    const m = /^\s*([+-]?\d(?:_?\d)*)\s*$/.exec(text);
    if (!m) throw new RangeError(`invalid literal for int() with base 10: ${pyStrRepr(text)}`);
    return Number.parseInt(m[1]!.replaceAll("_", ""), 10);
}

// Python's print(*args): the texts joined with spaces, a line end. (console.log reads "%s" / "%d" in its first
// argument when more follow; this doesn't.) Python on Windows writes "\r\n" - see docs/typescript.md.
export function print(...args: unknown[]): void {
    process.stdout.write(`${args.map((a) => (typeof a === "string" ? a : pyRepr(a))).join(" ")}\n`);
}

// --- sys.exit -------------------------------------------------------------------------------------------------------

// Python's SystemExit: sys.exit("message") prints the message to stderr and exits with 1; sys.exit(n) exits with n.
// Thrown (not process.exit) so output is flushed; runMain turns it into the exit code. Note: a ported
// `except Exception:` must not swallow it (Python's SystemExit isn't an Exception) - rethrow it.
export class SystemExit extends Error {
    override name = "SystemExit";
    readonly code: number;
    readonly text: string | undefined;
    constructor(codeOrMessage: number | string = 0) {
        super(typeof codeOrMessage === "string" ? codeOrMessage : `exit ${codeOrMessage}`);
        this.code = typeof codeOrMessage === "string" ? 1 : codeOrMessage;
        this.text = typeof codeOrMessage === "string" ? codeOrMessage : undefined;
    }
}

export function pyExit(codeOrMessage: number | string = 0): never {
    throw new SystemExit(codeOrMessage);
}

// Runs a CLI's main: its return value (a number) or SystemExit is the exit code, as Python's
// `sys.exit(main())`.
export async function runMain(main: () => number | undefined | Promise<number | undefined>): Promise<void> {
    try {
        const code = await main();
        if (typeof code === "number") process.exitCode = code;
    } catch (e) {
        if (e instanceof SystemExit) {
            if (e.text !== undefined) process.stderr.write(`${e.text}\n`);
            process.exitCode = e.code;
            return;
        }
        throw e;
    }
}

// Whether this module is the one Node was started with (Python's `if __name__ == "__main__":`).
export function isMain(importMetaUrl: string): boolean {
    const entry = process.argv[1];
    if (!entry) return false;
    const url = new URL(importMetaUrl);
    const self = decodeURIComponent(url.pathname).replace(/^\/([A-Za-z]:)/, "$1");
    const norm = (p: string): string => p.replaceAll("\\", "/").toLowerCase();
    return norm(self) === norm(fs.realpathSync(entry));
}

// --- dates ----------------------------------------------------------------------------------------------------------

// datetime.date.today().isoformat(): the local date, "2026-10-20".
export function today(): string {
    const d = new Date();
    const two = (n: number): string => String(n).padStart(2, "0");
    return `${d.getFullYear()}-${two(d.getMonth() + 1)}-${two(d.getDate())}`;
}

// --- paths ----------------------------------------------------------------------------------------------------------

// Python's os.path.join on Windows (ntpath.join): joins with "\" but keeps the parts' own slashes (node:path's join
// normalises "C:/x" to "C:\x" - matters only where a path is printed or compared as text). An absolute part
// (a drive, or a leading slash) starts again, as ntpath.
export function pyJoin(first: string, ...rest: string[]): string {
    let out = first;
    for (const part of rest) {
        if (/^[A-Za-z]:/.test(part)) {
            out = part;
        } else if (part.startsWith("\\") || part.startsWith("/")) {
            const drive = /^[A-Za-z]:/.exec(out);
            out = (drive ? drive[0] : "") + part;
        } else if (out === "" || out.endsWith("\\") || out.endsWith("/") || /^[A-Za-z]:$/.test(out)) {
            out += part;
        } else {
            out += `\\${part}`;
        }
    }
    return out;
}

// --- truth ----------------------------------------------------------------------------------------------------------

// Python's truth of a value: None, False, 0, 0.0 (a Float too), "", and empty lists / dicts / sets / Maps / bytes are
// false; other objects true. (JavaScript's: [] and {} are true, NaN false - Python's nan is true.)
export function pyTruthy(value: unknown): boolean {
    if (value === null || value === undefined || value === false) return false;
    if (typeof value === "number") return value !== 0;
    if (typeof value === "string") return value.length > 0;
    if (value instanceof Float) return value.value !== 0;
    if (Array.isArray(value) || value instanceof Uint8Array) return value.length > 0;
    if (value instanceof Map || value instanceof Set) return value.size > 0;
    if (Object.getPrototypeOf(value) === Object.prototype) return Object.keys(value).length > 0;
    return true;
}

// --- min / max ------------------------------------------------------------------------------------------------------

// Python's max() / min() of numbers: the first of the greatest / least (a NaN is kept only when first - Math.max
// gives NaN for any NaN), raising on an empty sequence; no argument-count limit (Math.max(...a) overflows the stack
// for big arrays).
export function pyMax(values: Iterable<number>): number {
    let best: number | undefined;
    for (const v of values) if (best === undefined || v > best) best = v;
    if (best === undefined) throw new RangeError("max() arg is an empty sequence");
    return best;
}

export function pyMin(values: Iterable<number>): number {
    let best: number | undefined;
    for (const v of values) if (best === undefined || v < best) best = v;
    if (best === undefined) throw new RangeError("min() arg is an empty sequence");
    return best;
}

// Python's min(items, key=key) / max(items, key=key): the first item with the least / greatest key.
export function pyMinBy<T>(items: Iterable<T>, key: (item: T) => number): T {
    let best: T | undefined;
    let bestKey = 0;
    let found = false;
    for (const item of items) {
        const k = key(item);
        if (!found || k < bestKey) {
            best = item;
            bestKey = k;
            found = true;
        }
    }
    if (!found) throw new RangeError("min() arg is an empty sequence");
    return best as T;
}

// --- sorting --------------------------------------------------------------------------------------------------------

// Python's sorted() of numbers (Array.sort without a comparator sorts as text: [10, 9] stays [10, 9]).
export function sortedNumbers(values: Iterable<number>): number[] {
    return [...values].sort((a, b) => a - b);
}

// Python's comparison of str (by code point; JavaScript's < compares UTF-16 units - the same below U+10000).
export function compareStr(a: string, b: string): number {
    return a < b ? -1 : a > b ? 1 : 0;
}
