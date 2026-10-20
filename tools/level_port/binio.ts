// Little-endian binary reading and writing for the level file formats.
//
// Formats are Python struct strings without the byte order ("8I", "IHHHBBI", `${n}H`): tools/lib/struct.ts, "<"
// added here.

import { calcsize, packInto, type StructValue, unpackFrom, unpackNumbers } from "../lib/struct.ts";

export class FormatError extends Error {
    override name = "FormatError";
}

const hex = (n: number): string => n.toString(16).toUpperCase();

export class Reader {
    readonly data: Buffer;
    offset: number;
    readonly name: string;

    constructor(data: Uint8Array, offset = 0, name = "?") {
        this.data = Buffer.isBuffer(data) ? data : Buffer.from(data.buffer, data.byteOffset, data.byteLength);
        this.offset = offset;
        this.name = name;
    }

    private check(size: number): void {
        if (this.offset + size > this.data.length) {
            throw new FormatError(
                `${this.name}: read of ${size} bytes at 0x${hex(this.offset)} past the end (0x${hex(this.data.length)})`,
            );
        }
    }

    // The numbers of `fmt` (numeric codes only).
    read(fmt: string): number[] {
        const full = `<${fmt}`;
        this.check(calcsize(full));
        const values = unpackNumbers(full, this.data, this.offset);
        this.offset += calcsize(full);
        return values;
    }

    // read() of a format with bytes ("s") or bools ("?").
    readAny(fmt: string): StructValue[] {
        const full = `<${fmt}`;
        this.check(calcsize(full));
        const values = unpackFrom(full, this.data, this.offset);
        this.offset += calcsize(full);
        return values;
    }

    one(fmt: string): number {
        return this.read(fmt)[0]!;
    }

    u8(): number {
        return this.one("B");
    }

    u16(): number {
        return this.one("H");
    }

    i16(): number {
        return this.one("h");
    }

    u32(): number {
        return this.one("I");
    }

    i32(): number {
        return this.one("i");
    }

    f32(): number {
        return this.one("f");
    }

    bytes(count: number): Buffer {
        if (count < 0 || this.offset + count > this.data.length) {
            throw new FormatError(`${this.name}: ${count} bytes at 0x${hex(this.offset)} past the end`);
        }
        // (a copy, as Python's slice: the caller may keep it after the data goes)
        const value = Buffer.from(this.data.subarray(this.offset, this.offset + count));
        this.offset += count;
        return value;
    }

    atEnd(): boolean {
        return this.offset === this.data.length;
    }

    expectEnd(): void {
        if (!this.atEnd()) {
            throw new FormatError(`${this.name}: stopped at 0x${hex(this.offset)} of 0x${hex(this.data.length)}`);
        }
    }
}

export class Writer {
    private buffer = Buffer.alloc(256);
    private length = 0;

    private grow(size: number): void {
        if (this.length + size <= this.buffer.length) return;
        let capacity = this.buffer.length * 2;
        while (capacity < this.length + size) capacity *= 2;
        const bigger = Buffer.alloc(capacity);
        this.buffer.copy(bigger, 0, 0, this.length);
        this.buffer = bigger;
    }

    write(fmt: string, ...values: StructValue[]): void {
        const full = `<${fmt}`;
        const size = calcsize(full);
        this.grow(size);
        packInto(full, this.buffer, this.length, ...values);
        this.length += size;
    }

    u8(v: number): void {
        this.write("B", v);
    }

    u16(v: number): void {
        this.write("H", v);
    }

    u32(v: number): void {
        this.write("I", v);
    }

    i32(v: number): void {
        this.write("i", v);
    }

    f32(v: number): void {
        this.write("f", v);
    }

    raw(data: Uint8Array): void {
        this.grow(data.length);
        this.buffer.set(data, this.length);
        this.length += data.length;
    }

    padTo(alignment: number): void {
        while (this.length % alignment) {
            this.grow(1);
            this.buffer[this.length++] = 0;
        }
    }

    tell(): number {
        return this.length;
    }

    patch(offset: number, fmt: string, ...values: StructValue[]): void {
        packInto(`<${fmt}`, this.buffer.subarray(0, this.length), offset, ...values);
    }

    getvalue(): Buffer {
        return Buffer.from(this.buffer.subarray(0, this.length));
    }
}
