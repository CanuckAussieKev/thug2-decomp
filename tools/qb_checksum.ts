// NOT ORIGINAL - canonical byte checksum for offline QB tools.
// Mirrors src/base/core/checksums.cpp: ASCII case fold and slash normalization.
export function checksum(name: string): number {
    let crc = 0xffffffff;
    for (let byte of Buffer.from(name, "utf8")) {
        if (byte >= 65 && byte <= 90) byte += 32;
        if (byte === 47) byte = 92;
        crc ^= byte;
        for (let i = 0; i < 8; ++i) crc = (crc >>> 1) ^ (crc & 1 ? 0xedb88320 : 0);
    }
    return crc >>> 0;
}

// The checksum of `text` appended to a name whose checksum is `crc` (checksums.cpp fold_text_into_checksum: the
// checksum has no final step, so a name's checksum is where its text left the fold - e.g. EndGap's "<GapID>_Success").
export function foldChecksum(crc: number, text: string): number {
    let out = crc >>> 0;
    for (let byte of Buffer.from(text, "utf8")) {
        if (byte >= 65 && byte <= 90) byte += 32;
        if (byte === 47) byte = 92;
        out ^= byte;
        for (let i = 0; i < 8; ++i) out = (out >>> 1) ^ (out & 1 ? 0xedb88320 : 0);
    }
    return out >>> 0;
}
