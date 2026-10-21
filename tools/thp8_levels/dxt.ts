// DXT (S3TC) blocks: decoding to 32-bit B, G, R, A texels and a plain encoder (each block's colours between the two
// furthest apart along their main axis - the bounding-box fit) for the mip levels THP8's textures are given here.
// Little-endian blocks, as Direct3D on the PC (xenos.ts swaps the Xbox's back first).

const expand565 = (c: number): [number, number, number] => {
    const r = (c >> 11) & 31;
    const g = (c >> 5) & 63;
    const b = c & 31;
    return [(b << 3) | (b >> 2), (g << 2) | (g >> 4), (r << 3) | (r >> 2)];
};

function colorBlock(
    src: Buffer,
    at: number,
    out: Buffer,
    ox: number,
    oy: number,
    w: number,
    h: number,
    dxt1: boolean,
): void {
    const c0 = src.readUInt16LE(at);
    const c1 = src.readUInt16LE(at + 2);
    const p0 = expand565(c0);
    const p1 = expand565(c1);
    const pal: number[][] = [
        [...p0, 255],
        [...p1, 255],
    ];
    if (!dxt1 || c0 > c1) {
        pal.push([0, 1, 2].map((k) => Math.round((2 * p0[k]! + p1[k]!) / 3)).concat(255));
        pal.push([0, 1, 2].map((k) => Math.round((p0[k]! + 2 * p1[k]!) / 3)).concat(255));
    } else {
        pal.push([0, 1, 2].map((k) => Math.round((p0[k]! + p1[k]!) / 2)).concat(255));
        pal.push([0, 0, 0, 0]);
    }
    const bits = src.readUInt32LE(at + 4);
    for (let y = 0; y < 4; y++) {
        for (let x = 0; x < 4; x++) {
            const px = ox + x;
            const py = oy + y;
            if (px >= w || py >= h) continue;
            const c = pal[(bits >>> (2 * (4 * y + x))) & 3]!;
            const o = (py * w + px) * 4;
            out[o] = c[0]!;
            out[o + 1] = c[1]!;
            out[o + 2] = c[2]!;
            out[o + 3] = c[3]!;
        }
    }
}

// DXT1 (kind 1), DXT3 (3) or DXT5 (5) blocks -> w x h B, G, R, A texels.
export function decode(src: Buffer, w: number, h: number, kind: number): Buffer {
    const out = Buffer.alloc(w * h * 4);
    const bw = Math.ceil(w / 4);
    const bh = Math.ceil(h / 4);
    const size = kind === 1 ? 8 : 16;
    for (let by = 0; by < bh; by++) {
        for (let bx = 0; bx < bw; bx++) {
            const at = (by * bw + bx) * size;
            if (kind === 1) {
                colorBlock(src, at, out, bx * 4, by * 4, w, h, true);
                continue;
            }
            colorBlock(src, at + 8, out, bx * 4, by * 4, w, h, false);
            const alpha: number[] = [];
            if (kind === 3) {
                for (let k = 0; k < 16; k++) {
                    const v = (src[at + (k >> 1)]! >> (4 * (k & 1))) & 15;
                    alpha.push(v * 17);
                }
            } else {
                const a0 = src[at]!;
                const a1 = src[at + 1]!;
                const pal = [a0, a1];
                if (a0 > a1) for (let k = 1; k < 7; k++) pal.push(Math.round(((7 - k) * a0 + k * a1) / 7));
                else {
                    for (let k = 1; k < 5; k++) pal.push(Math.round(((5 - k) * a0 + k * a1) / 5));
                    pal.push(0, 255);
                }
                let bits = 0n;
                for (let k = 0; k < 6; k++) bits |= BigInt(src[at + 2 + k]!) << BigInt(8 * k);
                for (let k = 0; k < 16; k++) alpha.push(pal[Number((bits >> BigInt(3 * k)) & 7n)]!);
            }
            for (let k = 0; k < 16; k++) {
                const px = bx * 4 + (k & 3);
                const py = by * 4 + (k >> 2);
                if (px < w && py < h) out[(py * w + px) * 4 + 3] = alpha[k]!;
            }
        }
    }
    return out;
}

// Whether a DXT1 level has transparent texels (a block in 3-colour mode using index 3).
export function hasAlpha1(src: Buffer): boolean {
    for (let at = 0; at + 8 <= src.length; at += 8) {
        if (src.readUInt16LE(at) > src.readUInt16LE(at + 2)) continue;
        const bits = src.readUInt32LE(at + 4);
        for (let k = 0; k < 16; k++) if (((bits >>> (2 * k)) & 3) === 3) return true;
    }
    return false;
}

// The next mip level (each 2 x 2 averaged; a side of 1 stays).
export function halve(src: Buffer, w: number, h: number): Buffer {
    const nw = Math.max(1, w >> 1);
    const nh = Math.max(1, h >> 1);
    const sx = w > 1 ? 2 : 1;
    const sy = h > 1 ? 2 : 1;
    const out = Buffer.alloc(nw * nh * 4);
    for (let y = 0; y < nh; y++) {
        for (let x = 0; x < nw; x++) {
            const a = (y * sy * w + x * sx) * 4;
            const b = a + (sx === 2 ? 4 : 0);
            const c = a + (sy === 2 ? w * 4 : 0);
            const d = c + (sx === 2 ? 4 : 0);
            for (let k = 0; k < 4; k++)
                out[(y * nw + x) * 4 + k] = (src[a + k]! + src[b + k]! + src[c + k]! + src[d + k]! + 2) >> 2;
        }
    }
    return out;
}

const pack565 = (b: number, g: number, r: number): number =>
    ((Math.round((r * 31) / 255) << 11) | (Math.round((g * 63) / 255) << 5) | Math.round((b * 31) / 255)) >>> 0;

function encodeColor(texels: number[][], out: Buffer, at: number, punch: boolean): void {
    // the two texels furthest apart along the block's main (bounding box) diagonal
    const solid = texels.filter((t) => !punch || t[3]! >= 128);
    const lo = [0, 1, 2].map((k) => Math.min(...(solid.length ? solid : texels).map((t) => t[k]!)));
    const hi = [0, 1, 2].map((k) => Math.max(...(solid.length ? solid : texels).map((t) => t[k]!)));
    let c0 = pack565(hi[0]!, hi[1]!, hi[2]!);
    let c1 = pack565(lo[0]!, lo[1]!, lo[2]!);
    const transparent = punch && solid.length < texels.length;
    if (transparent ? c0 > c1 : c0 < c1) [c0, c1] = [c1, c0];
    if (!transparent && c0 === c1) {
        // (4-colour mode needs c0 > c1)
        if (c1 > 0) c1 -= 1;
        else c0 += 1;
    }
    const p0 = expand565(c0);
    const p1 = expand565(c1);
    const pal = transparent
        ? [p0, p1, [0, 1, 2].map((k) => (p0[k]! + p1[k]!) / 2)]
        : [p0, p1, [0, 1, 2].map((k) => (2 * p0[k]! + p1[k]!) / 3), [0, 1, 2].map((k) => (p0[k]! + 2 * p1[k]!) / 3)];
    let bits = 0;
    texels.forEach((t, k) => {
        let best = 0;
        if (transparent && t[3]! < 128) best = 3;
        else {
            let bd = Number.POSITIVE_INFINITY;
            pal.forEach((p, i) => {
                const dd = (p[0]! - t[0]!) ** 2 + (p[1]! - t[1]!) ** 2 + (p[2]! - t[2]!) ** 2;
                if (dd < bd) {
                    bd = dd;
                    best = i;
                }
            });
        }
        bits |= best << (2 * k);
    });
    out.writeUInt16LE(c0, at);
    out.writeUInt16LE(c1, at + 2);
    out.writeUInt32LE(bits >>> 0, at + 4);
}

function encodeAlpha(alpha: number[], out: Buffer, at: number): void {
    const a0 = Math.max(...alpha);
    const a1 = Math.min(...alpha);
    out[at] = a0;
    out[at + 1] = a1;
    const pal = [a0, a1];
    for (let k = 1; k < 7; k++) pal.push(Math.round(((7 - k) * a0 + k * a1) / 7));
    let bits = 0n;
    alpha.forEach((a, k) => {
        let best = 0;
        let bd = 999;
        pal.forEach((p, i) => {
            if (Math.abs(p - a) < bd) {
                bd = Math.abs(p - a);
                best = i;
            }
        });
        if (a0 === a1) best = 0;
        bits |= BigInt(best) << BigInt(3 * k);
    });
    for (let k = 0; k < 6; k++) out[at + 2 + k] = Number((bits >> BigInt(8 * k)) & 0xffn);
}

// w x h B, G, R, A texels -> DXT1 (kind 1: 1-bit alpha kept) or DXT5 (5) blocks.
export function encode(src: Buffer, w: number, h: number, kind: number): Buffer {
    const bw = Math.ceil(w / 4);
    const bh = Math.ceil(h / 4);
    const size = kind === 1 ? 8 : 16;
    const out = Buffer.alloc(bw * bh * size);
    for (let by = 0; by < bh; by++) {
        for (let bx = 0; bx < bw; bx++) {
            const texels: number[][] = [];
            for (let k = 0; k < 16; k++) {
                const px = Math.min(w - 1, bx * 4 + (k & 3));
                const py = Math.min(h - 1, by * 4 + (k >> 2));
                const o = (py * w + px) * 4;
                texels.push([src[o]!, src[o + 1]!, src[o + 2]!, src[o + 3]!]);
            }
            const at = (by * bw + bx) * size;
            if (kind === 1) encodeColor(texels, out, at, true);
            else {
                encodeAlpha(
                    texels.map((t) => t[3]!),
                    out,
                    at,
                );
                encodeColor(texels, out, at + 8, false);
            }
        }
    }
    return out;
}
