// NOT ORIGINAL - tests of gap_table.ts on synthetic executables (no THPS2 file is needed): an encrypted SafeDisc-like
// one is recognised and left alone, a readable one's gap table is found whatever its record layout.

import assert from "node:assert/strict";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { test } from "node:test";
import * as gt from "./gap_table.ts";

const IMAGE_BASE = 0x400000;

// A PE with a .text, a .data and an .rdata section (each 0x1000 at raw 0x400 / 0x1400 / 0x2400, va 0x1000 / 0x2000 /
// 0x3000); `fill` writes into the file.
function pe(fill: (buf: Buffer) => void, sections = [".text", ".data", ".rdata"]): Buffer {
    const buf = Buffer.alloc(0x400 + sections.length * 0x1000);
    buf.write("MZ", 0, "latin1");
    buf.writeUInt32LE(0x80, 0x3c);
    buf.write("PE\0\0", 0x80, "latin1");
    buf.writeUInt16LE(0x14c, 0x84);
    buf.writeUInt16LE(sections.length, 0x86);
    buf.writeUInt16LE(0xe0, 0x94); // optional header size
    buf.writeUInt32LE(IMAGE_BASE, 0x98 + 28);
    sections.forEach((name, i) => {
        const o = 0x98 + 0xe0 + i * 40;
        buf.write(name, o, "latin1");
        buf.writeUInt32LE(0x1000, o + 8);
        buf.writeUInt32LE(0x1000 * (i + 1), o + 12);
        buf.writeUInt32LE(0x1000, o + 16);
        buf.writeUInt32LE(0x400 + 0x1000 * i, o + 20);
    });
    fill(buf);
    return buf;
}

// a deterministic pseudo-random byte stream (the "encrypted" sections)
function noise(buf: Buffer, from: number, to: number): void {
    let x = 0x12345678;
    for (let i = from; i < to; i++) {
        x ^= x << 13;
        x ^= x >>> 17;
        x ^= x << 5;
        buf[i] = x & 0xff;
    }
}

const GAPS: [number, string, number][] = [
    [2700, "KICKER GAP", 100],
    [2701, "OVER THE PIPE", 250],
    [2702, "HANGTIME", 500],
    [2703, "RAIL 2 RAIL", 150],
    [2704, "DOWN THE DROP", 75],
    [2706, "SECRET ROOM", 1000],
];
const USED = [2700, 2701, 2702, 2703, 2704, 2706, 2710];

// .data: records {u16 score, u16 gap, char *name} (8 bytes) at 0x1500; the names in .rdata (0x2400 on, va 0x403000)
function pointerTable(buf: Buffer): void {
    let text = 0x2400;
    GAPS.forEach(([gap, name, score], i) => {
        const rec = 0x1500 + i * 8;
        buf.writeUInt16LE(score, rec);
        buf.writeUInt16LE(gap, rec + 2);
        buf.writeUInt32LE(IMAGE_BASE + 0x3000 + (text - 0x2400), rec + 4);
        buf.write(`${name}\0`, text, "latin1");
        text += name.length + 1;
    });
}

test("probeExe: a SafeDisc-wrapped executable with encrypted data is recognised", () => {
    const wrapped = pe(
        (buf) => {
            noise(buf, 0x400, buf.length);
            buf.write("BoG_ *90.0&!!  Yy>", 0x100, "latin1");
            buf.fill(0, 0x112, 0x120);
            buf.writeUInt32LE(2, 0x120);
            buf.writeUInt32LE(5, 0x124);
            buf.writeUInt32LE(30, 0x128);
        },
        [".text", ".data", ".rdata", "stxt774"],
    );
    const p = gt.probeExe(wrapped, "THawk2.exe");
    assert.equal(p.pe, true);
    assert.equal(p.safeDisc, "2.05.030");
    assert.equal(p.encrypted, true);
    assert.match(gt.describeProbe(p), /SafeDisc 2\.05\.030-wrapped, data encrypted/);
    // and its noise holds no table
    assert.equal(gt.findGapTable(wrapped, USED).gaps.size, 0);
});

test("probeExe: a readable executable", () => {
    const p = gt.probeExe(pe(pointerTable), "plain.exe");
    assert.deepEqual([p.pe, p.safeDisc, p.encrypted], [true, null, false]);
    assert.equal(gt.probeExe(Buffer.from("not an exe"), "x").pe, false);
});

test("findGapTable: records of a number, a score and a name pointer", () => {
    const t = gt.findGapTable(pe(pointerTable), USED);
    assert.equal(t.gaps.size, 6);
    assert.deepEqual(t.gaps.get(2701), { name: "OVER THE PIPE", score: 250 });
    assert.deepEqual(t.gaps.get(2706), { name: "SECRET ROOM", score: 1000 });
    assert.equal(t.gaps.has(2710), false); // not in the table: the converter's stand-in
    assert.equal(t.layouts.length, 1);
    assert.deepEqual(
        { ...t.layouts[0]!, at: t.layouts[0]!.at.toString(16) },
        {
            at: "1500",
            stride: 8,
            numberAt: 2,
            numberWidth: 2,
            nameAt: 4,
            nameInline: false,
            scoreAt: 0,
            scoreWidth: 2,
            records: 6,
        },
    );
});

test("findGapTable: records with the name in them (u32 number, char[24] name, u32 score)", () => {
    const image = pe((buf) => {
        GAPS.forEach(([gap, name, score], i) => {
            const rec = 0x1800 + i * 32;
            buf.writeUInt32LE(gap, rec);
            buf.write(name, rec + 4, "latin1");
            buf.writeUInt32LE(score, rec + 28);
        });
    });
    const t = gt.findGapTable(image, USED);
    assert.equal(t.gaps.size, 6);
    assert.deepEqual(t.gaps.get(2700), { name: "KICKER GAP", score: 100 });
    assert.equal(t.layouts[0]!.nameInline, true);
    assert.equal(t.layouts[0]!.numberWidth, 4);
});

test("findGapTable: unrelated data - numbers without names, too few - is no table", () => {
    const image = pe((buf) => {
        // the gap numbers in a plain list (no names) and three named records
        USED.forEach((g, i) => {
            buf.writeUInt16LE(g, 0x1500 + i * 2);
        });
        GAPS.slice(0, 3).forEach(([gap, , score], i) => {
            buf.writeUInt16LE(gap, 0x1900 + i * 8);
            buf.writeUInt16LE(score, 0x1902 + i * 8);
            buf.writeUInt32LE(IMAGE_BASE + 0x3000, 0x1904 + i * 8);
        });
        buf.write("ONLY THREE\0", 0x2400, "latin1");
    });
    assert.equal(gt.findGapTable(image, USED).gaps.size, 0);
});

test("loadGapTable: the first readable executable's table; none - the stand-ins, with a report", () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "thps2gaps-"));
    try {
        const wrapped = path.join(dir, "THawk2.exe");
        const plain = path.join(dir, "plain.exe");
        fs.writeFileSync(
            wrapped,
            pe((buf) => noise(buf, 0x400, buf.length)),
        );
        fs.writeFileSync(plain, pe(pointerTable));
        const none = gt.loadGapTable([wrapped, path.join(dir, "missing.exe")], USED);
        assert.equal(none.gaps.size, 0);
        assert.match(none.report.join("\n"), /THawk2\.exe: .*data encrypted[\s\S]*stand-in names/);
        const found = gt.loadGapTable([wrapped, plain], USED);
        assert.equal(found.gaps.get(2702)?.name, "HANGTIME");
        assert.match(found.report.join("\n"), /gap table at 0x1500: 6 records of 8 bytes[\s\S]*6 of 7 gaps named/);
        assert.match(gt.loadGapTable([path.join(dir, "missing.exe")], USED).report[0]!, /no THPS2 executable found/);
    } finally {
        fs.rmSync(dir, { recursive: true, force: true });
    }
});
