import assert from "node:assert/strict";
import { test } from "node:test";
import { contentHash } from "./pbr_file.ts";
import { HEADER_SIZE, type ReliefFile, readRelief, writeRelief } from "./relief_file.ts";

// The same sample as src/tests/relief_check.cpp (its kSampleFileHash is this file's hash).
export const SAMPLE: ReliefFile = {
    detector: 1,
    sources: 0x1122334455667788n,
    entries: [
        {
            checksum: 0x1234abcd,
            contentHash: 0x0102030405060708n,
            facade: {
                repeatU: 3,
                repeatV: 2,
                rects: [
                    { kind: "window", u0: 0.25, v0: 0.3, u1: 0.75, v1: 0.7, height: -0.05 },
                    { kind: "sill", u0: 0.2, v0: 0.7, u1: 0.8, v1: 0.75, height: 0.01 },
                ],
            },
        },
    ],
};
export const SAMPLE_HASH = 0x82402fc39a786d45n;

test("relief file: written and read back", () => {
    const bytes = writeRelief(SAMPLE);
    const read = readRelief(bytes);
    assert.equal(read.detector, 1);
    assert.equal(read.sources, SAMPLE.sources);
    assert.equal(read.entries.length, 1);
    const [entry] = read.entries;
    assert.equal(entry!.checksum, 0x1234abcd);
    assert.equal(entry!.contentHash, 0x0102030405060708n);
    assert.equal(entry!.facade.repeatU, 3);
    assert.equal(entry!.facade.rects[1]!.kind, "sill");
    assert.ok(Math.abs(entry!.facade.rects[0]!.height + 0.05) < 1e-7);
});

test("relief file: the game's sample hash", () => {
    assert.equal(contentHash(writeRelief(SAMPLE)), SAMPLE_HASH);
});

test("relief file: damaged files refused", () => {
    const bytes = writeRelief(SAMPLE);
    assert.throws(() => readRelief(bytes.subarray(0, bytes.length - 1)));
    const wrong = Buffer.from(bytes);
    wrong[0] = 0;
    assert.throws(() => readRelief(wrong));
    const kind = Buffer.from(bytes);
    kind[HEADER_SIZE + 24] = 9;
    assert.throws(() => readRelief(kind));
});
