import assert from "node:assert/strict";
import { test } from "node:test";
import {
    contentHash,
    contentHashReference,
    ENTRY_SIZE,
    HEADER_SIZE,
    type MapEntry,
    readPbr,
    writePbr,
} from "./pbr_file.ts";

const bytes = (count: number, first: number) => Uint8Array.from({ length: count }, (_, i) => (first + i) & 0xff);

// The same sample as src/tests/format_check_surface.cpp (its kSampleFileHash is this file's hash).
export const SAMPLE: MapEntry[] = [
    {
        checksum: 0x1234abcd,
        contentHash: 0x0102030405060708n,
        width: 2,
        height: 3,
        materialClass: 2,
        generator: 0,
        f0: 0.04,
        texels: bytes(24, 1),
    },
    {
        checksum: 0xfeedf00d,
        contentHash: 0xa0b0c0d0e0f00010n,
        width: 1,
        height: 1,
        materialClass: 4,
        generator: 1,
        f0: 0.5,
        texels: bytes(4, 200),
    },
];
export const SAMPLE_FILE_HASH = 0x3d218c744addc00an;

test("content hash: FNV-1a 64's published values", () => {
    assert.equal(contentHash(new Uint8Array(0)), 0xcbf29ce484222325n);
    assert.equal(contentHash(Buffer.from("a")), 0xaf63dc4c8601ec8cn);
    assert.equal(contentHash(Buffer.from("foobar")), 0x85944171f73967e8n);
});

test("content hash: the fast form is the BigInt one on random bytes", () => {
    let seed = 12345;
    const random = Uint8Array.from({ length: 4096 }, () => {
        seed = (Math.imul(seed, 1103515245) + 12345) >>> 0;
        return seed >>> 24;
    });
    for (const length of [1, 7, 64, 4096]) {
        assert.equal(contentHash(random.subarray(0, length)), contentHashReference(random.subarray(0, length)));
    }
});

test("the file: the C++ check's sample bytes, read back", () => {
    const file = writePbr(SAMPLE);
    assert.equal(file.length, HEADER_SIZE + 2 * ENTRY_SIZE + 24 + 4);
    assert.equal(contentHash(file), SAMPLE_FILE_HASH);
    const back = readPbr(file);
    assert.equal(back.length, 2);
    assert.equal(back[0]!.checksum, 0x1234abcd);
    assert.equal(back[0]!.contentHash, 0x0102030405060708n);
    assert.equal(back[1]!.contentHash, 0xa0b0c0d0e0f00010n);
    assert.deepEqual([...back[1]!.texels], [200, 201, 202, 203]);
    assert.equal(back[1]!.generator, 1);
    assert.ok(Math.abs(back[0]!.f0 - 0.04) < 1e-7);
});

test("the file: broken ones refused", () => {
    const file = writePbr(SAMPLE);
    assert.throws(() => readPbr(file.subarray(0, file.length - 1)));
    const other = Buffer.from(file);
    other[0] = 0;
    assert.throws(() => readPbr(other));
    assert.throws(() => writePbr([{ ...SAMPLE[0]!, width: 3 }]));
});
