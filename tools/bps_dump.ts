// NOT ORIGINAL - dumps a BPS patch (beat's binary patch format: "BPS1", varint sizes and metadata, then
// SourceRead / TargetRead / SourceCopy / TargetCopy commands, three CRC32s at the end): each command with the target
// file offset it writes and, for TargetRead, the bytes. Used to read what PartyMod's patch changes in the exe.
//
//   node tools/bps_dump.ts <patch.bps>

import * as fs from "node:fs";
import { isMain, print, pyBytesRepr, runMain } from "./pipeline/lib/py.ts";

// Python's f"{n:0<width>x}" (a negative number: "-" then the zero-padded digits, the sign counted in the width)
const hex = (n: number, width: number): string =>
    n < 0 ? `-${(-n).toString(16).padStart(width - 1, "0")}` : n.toString(16).padStart(width, "0");
const hexSpaced = (data: Uint8Array): string => [...data].map((b) => hex(b, 2)).join(" ");

export function dumpBps(p: Buffer): void {
    if (p.subarray(0, 4).toString("latin1") !== "BPS1") throw new Error("AssertionError: not a BPS1 patch");
    let i = 4;
    const byte = (): number => {
        const x = p[i++];
        if (x === undefined) throw new RangeError("index out of range");
        return x;
    };
    // BPS's varint: 7 bits a byte, the top bit ends it, each continuation adds one (no two encodings of a number)
    const num = (): number => {
        let d = 0;
        let s = 1;
        for (;;) {
            const x = byte();
            d += (x & 0x7f) * s;
            if (x & 0x80) return d;
            s *= 128;
            d += s;
        }
    };
    const ss = num();
    const ts = num();
    const ms = num();
    const meta = p.subarray(i, i + ms);
    i += ms;
    const tail = p.subarray(p.length - 12);
    if (tail.length < 12) throw new RangeError("unpack requires a buffer of 12 bytes");
    const [srcCrc, tgtCrc] = [tail.readUInt32LE(0), tail.readUInt32LE(4)];
    print(`source size=${ss} target size=${ts} meta=${pyBytesRepr(meta)}`);
    print(`source crc32=${hex(srcCrc, 8)} target crc32=${hex(tgtCrc, 8)}`);
    let out = 0;
    let sr = 0;
    let tr = 0;
    const end = p.length - 12;
    while (i < end) {
        const d = num();
        const cmd = d % 4;
        const ln = Math.floor(d / 4) + 1;
        if (cmd === 0) out += ln;
        else if (cmd === 1) {
            const data = p.subarray(i, i + ln);
            i += ln;
            print(`  TargetRead  @ file 0x${hex(out, 6)} len ${ln}: ${hexSpaced(data)}`);
            out += ln;
        } else {
            const o = num();
            const offset = (o % 2 ? -1 : 1) * Math.floor(o / 2);
            if (cmd === 2) {
                sr += offset;
                print(`  SourceCopy  @ file 0x${hex(out, 6)} len ${ln} from src 0x${hex(sr, 6)}`);
                sr += ln;
            } else {
                tr += offset;
                print(`  TargetCopy  @ file 0x${hex(out, 6)} len ${ln} from tgt 0x${hex(tr, 6)}`);
                tr += ln;
            }
            out += ln;
        }
    }
}

function main(): number {
    const file = process.argv[2];
    if (file === undefined) throw new RangeError("usage: node tools/bps_dump.ts <patch.bps>");
    dumpBps(fs.readFileSync(file));
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
