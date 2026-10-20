// NOT ORIGINAL - Python's `struct` module for the tools ported from Python (docs/typescript.md): pack / unpack with
// the same format strings ("<3f", "IHHHBBI", "<%dH" made with `${n}H`, ">I", "<8sIIII"), the same sizes and the same
// errors where JavaScript would silently wrap (a value out of a field's range, a float where an integer goes, a
// float32 overflow) - so a ported tool fails where its Python twin failed instead of writing wrapped bytes.
//
// Byte orders: "<" (little), ">" / "!" (big), "=" (little: this machine's, without alignment). No prefix or "@" is
// native with alignment in Python: refused here (every tool prefixes "<", or reads through binio's Reader / Writer,
// which do). Codes: x (pad), b B ? h H i I l L q Q (q / Q only within 2^53), e (half), f, d, s (bytes), c.

export class StructError extends Error {
    override name = "StructError";
}

export type StructValue = number | boolean | Buffer;

interface Field {
    code: string;
    count: number; // repeat count, or the byte length for "s"
}

const SIZES: Record<string, number> = {
    x: 1,
    c: 1,
    b: 1,
    B: 1,
    "?": 1,
    h: 2,
    H: 2,
    e: 2,
    i: 4,
    I: 4,
    l: 4,
    L: 4,
    f: 4,
    q: 8,
    Q: 8,
    d: 8,
    s: 1,
};

const parsed = new Map<string, { little: boolean; fields: Field[]; size: number }>();

function parse(fmt: string): { little: boolean; fields: Field[]; size: number } {
    const cached = parsed.get(fmt);
    if (cached) return cached;
    let i = 0;
    let little = true;
    const first = fmt[0];
    if (first === "<" || first === "=") {
        i = 1;
    } else if (first === ">" || first === "!") {
        little = false;
        i = 1;
    } else {
        throw new StructError(`struct format ${JSON.stringify(fmt)}: give a byte order ("<" or ">")`);
    }
    const fields: Field[] = [];
    let size = 0;
    while (i < fmt.length) {
        const ch = fmt[i]!;
        if (ch === " " || ch === "\t" || ch === "\n" || ch === "\r") {
            i++;
            continue;
        }
        let count = 1;
        let digits = "";
        while (i < fmt.length && fmt[i]! >= "0" && fmt[i]! <= "9") digits += fmt[i++];
        if (digits) count = Number.parseInt(digits, 10);
        const code = fmt[i++];
        if (code === undefined || !(code in SIZES)) {
            throw new StructError(`bad char in struct format ${JSON.stringify(fmt)}`);
        }
        fields.push({ code, count });
        size += code === "s" ? count : SIZES[code]! * count;
    }
    const result = { little, fields, size };
    parsed.set(fmt, result);
    return result;
}

export function calcsize(fmt: string): number {
    return parse(fmt).size;
}

// An empty array already holding doubles. A NaN's payload bits (THPS3's uninitialised uvs: 0x7FC0817D) survive
// float32 -> number -> float32, as in Python - but V8 sometimes canonicalises a NaN (to 0x7FC00000) when it is the
// first value stored into an array still holding small integers (the array's change of element kind; it depends on
// the code's optimisation tier, so it isn't even the same from run to run). Arrays started as doubles keep the bits,
// and so do their copies (slice, spread, concat, map).
export function doubleArray<T = number>(): T[] {
    const out = [0.5];
    out.pop();
    return out as T[];
}

// The values of `fmt` at `offset` of `data` (struct.unpack_from). Raises StructError when the data is too short.
export function unpackFrom(fmt: string, data: Uint8Array, offset = 0): StructValue[] {
    const { little, fields, size } = parse(fmt);
    if (offset < 0 || offset + size > data.length) {
        throw new StructError(`unpack_from requires a buffer of at least ${size + offset} bytes (${data.length})`);
    }
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
    const out = doubleArray<StructValue>();
    let at = offset;
    for (const { code, count } of fields) {
        if (code === "s") {
            out.push(Buffer.from(data.subarray(at, at + count)));
            at += count;
            continue;
        }
        for (let k = 0; k < count; k++) {
            switch (code) {
                case "x":
                    break;
                case "c":
                    out.push(Buffer.from(data.subarray(at, at + 1)));
                    break;
                case "b":
                    out.push(view.getInt8(at));
                    break;
                case "B":
                    out.push(view.getUint8(at));
                    break;
                case "?":
                    out.push(view.getUint8(at) !== 0);
                    break;
                case "h":
                    out.push(view.getInt16(at, little));
                    break;
                case "H":
                    out.push(view.getUint16(at, little));
                    break;
                case "e":
                    out.push(halfToNumber(view.getUint16(at, little)));
                    break;
                case "i":
                case "l":
                    out.push(view.getInt32(at, little));
                    break;
                case "I":
                case "L":
                    out.push(view.getUint32(at, little));
                    break;
                case "f":
                    out.push(view.getFloat32(at, little));
                    break;
                case "d":
                    out.push(view.getFloat64(at, little));
                    break;
                case "q":
                case "Q": {
                    const big = code === "q" ? view.getBigInt64(at, little) : view.getBigUint64(at, little);
                    if (big > BigInt(Number.MAX_SAFE_INTEGER) || big < BigInt(Number.MIN_SAFE_INTEGER)) {
                        throw new StructError(`${code} value ${big} beyond 2^53: read it as a BigInt`);
                    }
                    out.push(Number(big));
                    break;
                }
            }
            at += SIZES[code]!;
        }
    }
    return out;
}

// unpackFrom of a format with only numeric codes, typed as numbers.
export function unpackNumbers(fmt: string, data: Uint8Array, offset = 0): number[] {
    const values = unpackFrom(fmt, data, offset);
    for (const v of values) {
        if (typeof v !== "number") throw new StructError(`unpackNumbers: ${fmt} has a non-numeric field`);
    }
    return values as number[];
}

// struct.unpack: the data must be exactly the format's size.
export function unpack(fmt: string, data: Uint8Array): StructValue[] {
    if (data.length !== calcsize(fmt)) {
        throw new StructError(`unpack requires a buffer of ${calcsize(fmt)} bytes (${data.length})`);
    }
    return unpackFrom(fmt, data, 0);
}

const RANGES: Record<string, [number, number]> = {
    b: [-0x80, 0x7f],
    B: [0, 0xff],
    h: [-0x8000, 0x7fff],
    H: [0, 0xffff],
    i: [-0x80000000, 0x7fffffff],
    l: [-0x80000000, 0x7fffffff],
    I: [0, 0xffffffff],
    L: [0, 0xffffffff],
    q: [Number.MIN_SAFE_INTEGER, Number.MAX_SAFE_INTEGER],
    Q: [0, Number.MAX_SAFE_INTEGER],
};

function integer(code: string, value: StructValue): number {
    if (typeof value === "boolean") return value ? 1 : 0; // (Python's bool is an int)
    if (typeof value !== "number" || !Number.isInteger(value)) {
        throw new StructError(`required argument is not an integer (${code}: ${String(value)})`);
    }
    const [lo, hi] = RANGES[code]!;
    if (value < lo || value > hi) {
        throw new StructError(`'${code}' format requires ${lo} <= number <= ${hi} (${value})`);
    }
    return value;
}

function real(code: string, value: StructValue): number {
    if (typeof value === "boolean") return value ? 1 : 0;
    if (typeof value !== "number") throw new StructError(`required argument is not a float (${code})`);
    return value;
}

// Writes `values` into `target` at `offset` (struct.pack_into).
export function packInto(fmt: string, target: Uint8Array, offset: number, ...values: StructValue[]): void {
    const { little, fields, size } = parse(fmt);
    if (offset < 0 || offset + size > target.length) {
        throw new StructError(`pack_into requires a buffer of at least ${size + offset} bytes`);
    }
    const view = new DataView(target.buffer, target.byteOffset, target.byteLength);
    let at = offset;
    let n = 0;
    const next = (): StructValue => {
        if (n >= values.length) throw new StructError(`pack expected more items for ${fmt} (got ${values.length})`);
        return values[n++]!;
    };
    for (const { code, count } of fields) {
        if (code === "s") {
            const value = next();
            if (!(value instanceof Uint8Array)) throw new StructError("argument for 's' must be a bytes object");
            const bytes = value.subarray(0, count);
            target.set(bytes, at);
            target.fill(0, at + bytes.length, at + count);
            at += count;
            continue;
        }
        for (let k = 0; k < count; k++) {
            switch (code) {
                case "x":
                    view.setUint8(at, 0);
                    break;
                case "c": {
                    const value = next();
                    if (!(value instanceof Uint8Array) || value.length !== 1) {
                        throw new StructError("char format requires a bytes object of length 1");
                    }
                    view.setUint8(at, value[0]!);
                    break;
                }
                case "?":
                    view.setUint8(at, next() ? 1 : 0);
                    break;
                case "b":
                    view.setInt8(at, integer(code, next()));
                    break;
                case "B":
                    view.setUint8(at, integer(code, next()));
                    break;
                case "h":
                    view.setInt16(at, integer(code, next()), little);
                    break;
                case "H":
                    view.setUint16(at, integer(code, next()), little);
                    break;
                case "i":
                case "l":
                    view.setInt32(at, integer(code, next()), little);
                    break;
                case "I":
                case "L":
                    view.setUint32(at, integer(code, next()), little);
                    break;
                case "q":
                    view.setBigInt64(at, BigInt(integer(code, next())), little);
                    break;
                case "Q":
                    view.setBigUint64(at, BigInt(integer(code, next())), little);
                    break;
                case "e":
                    view.setUint16(at, numberToHalf(real(code, next())), little);
                    break;
                case "f": {
                    const value = real(code, next());
                    const single = Math.fround(value);
                    // (Python: "float too large to pack with f format" - DataView would store an infinity)
                    if (!Number.isFinite(single) && Number.isFinite(value)) {
                        throw new StructError("float too large to pack with f format");
                    }
                    view.setFloat32(at, value, little);
                    break;
                }
                case "d":
                    view.setFloat64(at, real(code, next()), little);
                    break;
            }
            at += SIZES[code]!;
        }
    }
    if (n !== values.length) throw new StructError(`pack expected ${n} items for packing (got ${values.length})`);
}

// struct.pack: the bytes of `values` in `fmt`.
export function pack(fmt: string, ...values: StructValue[]): Buffer {
    const out = Buffer.alloc(calcsize(fmt));
    packInto(fmt, out, 0, ...values);
    return out;
}

// The float32 a number is stored as (struct.unpack("<f", struct.pack("<f", x))[0]); raises on overflow as Python.
export function f32(value: number): number {
    const single = Math.fround(value);
    if (!Number.isFinite(single) && Number.isFinite(value)) {
        throw new StructError("float too large to pack with f format");
    }
    return single;
}

function halfToNumber(h: number): number {
    const sign = h & 0x8000 ? -1 : 1;
    const exp = (h >> 10) & 0x1f;
    const frac = h & 0x3ff;
    if (exp === 0) return sign * frac * 2 ** -24;
    if (exp === 0x1f) return frac ? Number.NaN : sign * Number.POSITIVE_INFINITY;
    return sign * (1 + frac / 1024) * 2 ** (exp - 15);
}

function numberToHalf(value: number): number {
    // Round to nearest even, as Python's PyFloat_Pack2; overflow raises.
    if (Number.isNaN(value)) return 0x7e00;
    const sign = value < 0 || Object.is(value, -0) ? 0x8000 : 0;
    let x = Math.abs(value);
    if (x === Number.POSITIVE_INFINITY) return sign | 0x7c00;
    if (x === 0) return sign;
    let exp = Math.floor(Math.log2(x));
    if (2 ** exp > x) exp--;
    if (2 ** (exp + 1) <= x) exp++;
    let bits: number;
    if (exp < -14) {
        bits = roundEven(x / 2 ** -24);
    } else {
        const frac = roundEven((x / 2 ** exp - 1) * 1024);
        bits = ((exp + 15) << 10) + frac;
    }
    if (bits >= 0x7c00) throw new StructError("float too large to pack with e format");
    x = bits;
    return sign | x;
}

function roundEven(x: number): number {
    const f = Math.floor(x);
    const d = x - f;
    if (d > 0.5) return f + 1;
    if (d < 0.5) return f;
    return f % 2 === 0 ? f : f + 1;
}
