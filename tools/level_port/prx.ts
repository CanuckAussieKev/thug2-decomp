// The games' file archives: THUG2's .prx and THUG1's .pre are the same format (version 0xABCD0003).
//   i32 total size, u32 version, i32 file count, then per file {i32 size, i32 compressed size (0: stored),
//   i16 name size, u16 use count, u32 checksum}, its name, and its data (LZSS when compressed) padded to 4 bytes
//   (src/game/file/pip.cpp NextRawEntry; the LZSS: src/game/util/lzss.cpp). (TypeScript twin of prx.py.)

import * as fs from "node:fs";
import { decodeAsciiReplace } from "../lib/py.ts";
import { unpackNumbers } from "../lib/struct.ts";

export function lzssDecode(data: Uint8Array, size: number): Buffer {
    // A 4096-byte window filled with spaces, writing from 4096 - 18 (as tools/menu_level/make_menu_level.py's).
    const window = Buffer.alloc(4096, 0x20);
    let r = 4096 - 18;
    const out = Buffer.alloc(Math.max(0, size) + 18);
    let length = 0;
    let i = 0;
    let flags = 0;
    const n = data.length;
    while (length < size && i < n) {
        flags >>= 1;
        if (!(flags & 0x100)) {
            flags = data[i]! | 0xff00;
            i += 1;
        }
        if (flags & 1) {
            if (i >= n) break;
            const c = data[i]!;
            i += 1;
            out[length++] = c;
            window[r] = c;
            r = (r + 1) & 4095;
        } else {
            if (i + 1 >= n) break;
            const position = data[i]! | ((data[i + 1]! & 0xf0) << 4);
            const last = (data[i + 1]! & 0x0f) + 2;
            i += 2;
            for (let k = 0; k < last + 1; k++) {
                const c = window[(position + k) & 4095]!;
                out[length++] = c;
                window[r] = c;
                r = (r + 1) & 4095;
            }
        }
    }
    return Buffer.from(out.subarray(0, length));
}

// [name, data] for each file of the archive (or only those for which wanted(name) is true: the others aren't
// decompressed).
export function* entries(path: string, wanted?: (name: string) => boolean): Generator<[string, Buffer]> {
    const data = fs.readFileSync(path);
    const count = unpackNumbers("<iii", data, 0)[2]!;
    let offset = 12;
    for (let k = 0; k < count; k++) {
        const [size, compressed, nameSize] = unpackNumbers("<iihHI", data, offset) as [number, number, number];
        const stored = compressed || size;
        const body = offset + 16 + nameSize;
        const rawName = data.subarray(offset + 16, body);
        const nul = rawName.indexOf(0);
        const name = decodeAsciiReplace(nul >= 0 ? rawName.subarray(0, nul) : rawName);
        if (wanted === undefined || wanted(name)) {
            const raw = data.subarray(body, body + stored);
            yield [name, compressed ? lzssDecode(raw, size) : Buffer.from(raw)];
        }
        offset = body + ((stored + 3) & ~3);
    }
}

// One file of an archive by its name (any case, either slash), or null.
export function find(path: string, name: string): Buffer | null {
    const key = name.toLowerCase().replaceAll("/", "\\");
    for (const [, data] of entries(path, (n) => n.toLowerCase().replaceAll("/", "\\") === key)) return data;
    return null;
}
