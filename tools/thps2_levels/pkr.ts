// NOT ORIGINAL - THPS2 (PC)'s package archive, ALL.PKR: every data file of the game in one file. The layout, worked
// out from the user's own copy:
//
//   header  "PKR2", u32 1, u32 folder count, u32 file count
//   folders folder count x { char name[32] ("data/levels/" - lower case, '/'-separated, ending in '/'; junk may
//                            follow its terminating zero), u32 offset of its first file entry, u32 file count }
//   files   file count x { char name[32] (upper case: "HANGAR.PSX"), u32 compression (0xFFFFFFFE = stored),
//                          u32 offset (from the start of the archive), u32 size, u32 stored size }
//
// (A folder's files are consecutive in the file table.) Entries
// whose compression isn't 0xFFFFFFFE are zlib streams (inflated here); ALL.PKR of the PC release stores everything.

import * as fs from "node:fs";
import * as zlib from "node:zlib";
import { FormatError } from "../level_port/binio.ts";

export const STORED = 0xfffffffe;

export interface PkrEntry {
    folder: string; // "data/levels/" (as in the archive)
    name: string; // "HANGAR.PSX"
    path: string; // folder + name, lower case: "data/levels/hangar.psx"
    compression: number;
    offset: number;
    size: number;
    storedSize: number;
}

export { FormatError };

function cstr(buf: Buffer, at: number, len: number): string {
    const end = buf.indexOf(0, at);
    return buf.toString("latin1", at, end < 0 || end > at + len ? at + len : end);
}

// The directory of an archive (only the header and tables are read).
export function readPkrDirectory(buf: Buffer): PkrEntry[] {
    if (buf.length < 16 || buf.toString("latin1", 0, 4) !== "PKR2") throw new FormatError("not a PKR2 archive");
    const folderCount = buf.readUInt32LE(8);
    const fileCount = buf.readUInt32LE(12);
    const filesAt = 16 + folderCount * 40;
    if (filesAt + fileCount * 48 > buf.length) throw new FormatError("PKR tables past the end of the file");
    const out: PkrEntry[] = [];
    for (let f = 0; f < folderCount; f++) {
        const at = 16 + f * 40;
        const folder = cstr(buf, at, 32);
        const firstAt = buf.readUInt32LE(at + 32);
        const count = buf.readUInt32LE(at + 36);
        const first = (firstAt - filesAt) / 48;
        if (!Number.isInteger(first) || first < 0 || first + count > fileCount)
            throw new FormatError(`folder ${folder}: files outside the table`);
        for (let i = 0; i < count; i++) {
            const e = firstAt + i * 48;
            const name = cstr(buf, e, 32);
            out.push({
                folder,
                name,
                path: (folder + name).toLowerCase(),
                compression: buf.readUInt32LE(e + 32),
                offset: buf.readUInt32LE(e + 36),
                size: buf.readUInt32LE(e + 40),
                storedSize: buf.readUInt32LE(e + 44),
            });
        }
    }
    return out;
}

export class Pkr {
    readonly file: string;
    readonly entries: PkrEntry[];
    private readonly byPath = new Map<string, PkrEntry>();
    private readonly fd: number;

    constructor(file: string) {
        this.file = file;
        this.fd = fs.openSync(file, "r");
        const head = Buffer.alloc(16);
        fs.readSync(this.fd, head, 0, 16, 0);
        if (head.toString("latin1", 0, 4) !== "PKR2") throw new FormatError(`${file}: not a PKR2 archive`);
        const tables = 16 + head.readUInt32LE(8) * 40 + head.readUInt32LE(12) * 48;
        const buf = Buffer.alloc(tables);
        fs.readSync(this.fd, buf, 0, tables, 0);
        this.entries = readPkrDirectory(buf);
        for (const e of this.entries) this.byPath.set(e.path, e);
    }

    close(): void {
        fs.closeSync(this.fd);
    }

    find(path: string): PkrEntry | undefined {
        return this.byPath.get(path.toLowerCase().replaceAll("\\", "/"));
    }

    has(path: string): boolean {
        return this.find(path) !== undefined;
    }

    read(path: string): Buffer {
        const e = this.find(path);
        if (e === undefined) throw new FormatError(`${this.file}: no ${path}`);
        return this.readEntry(e);
    }

    readEntry(e: PkrEntry): Buffer {
        const stored = Buffer.alloc(e.storedSize);
        fs.readSync(this.fd, stored, 0, e.storedSize, e.offset);
        if (e.compression === STORED) return stored;
        const data = zlib.inflateSync(stored);
        if (data.length !== e.size) throw new FormatError(`${e.path}: inflated to ${data.length}, expected ${e.size}`);
        return data;
    }

    // Entries of a folder ("data/levels/"), in archive order.
    list(folder: string): PkrEntry[] {
        const f = folder.toLowerCase();
        return this.entries.filter((e) => e.folder.toLowerCase() === f);
    }
}

// Builds an archive (tests: synthetic data).
export function writePkr(files: { folder: string; name: string; data: Buffer }[]): Buffer {
    const folders: string[] = [];
    for (const f of files) if (!folders.includes(f.folder)) folders.push(f.folder);
    const ordered = folders.flatMap((d) => files.filter((f) => f.folder === d));
    const tables = 16 + folders.length * 40 + ordered.length * 48;
    const head = Buffer.alloc(tables);
    head.write("PKR2", 0, "latin1");
    head.writeUInt32LE(1, 4);
    head.writeUInt32LE(folders.length, 8);
    head.writeUInt32LE(ordered.length, 12);
    let index = 0;
    folders.forEach((d, i) => {
        const at = 16 + i * 40;
        head.write(d, at, 32, "latin1");
        const count = ordered.filter((f) => f.folder === d).length;
        head.writeUInt32LE(16 + folders.length * 40 + index * 48, at + 32);
        head.writeUInt32LE(count, at + 36);
        index += count;
    });
    let offset = tables;
    ordered.forEach((f, i) => {
        const e = 16 + folders.length * 40 + i * 48;
        head.write(f.name, e, 32, "latin1");
        head.writeUInt32LE(STORED, e + 32);
        head.writeUInt32LE(offset, e + 36);
        head.writeUInt32LE(f.data.length, e + 40);
        head.writeUInt32LE(f.data.length, e + 44);
        offset += f.data.length;
    });
    return Buffer.concat([head, ...ordered.map((f) => f.data)]);
}
