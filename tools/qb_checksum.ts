// NOT ORIGINAL - canonical byte checksum for offline QB tools.
// Mirrors src/game/core/crc.cpp: ASCII case fold and slash normalization.
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
