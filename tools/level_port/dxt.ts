// NOT ORIGINAL - DXT1 / DXT5 (S3TC, Direct3D's BC1 / BC3) decoding and a small encoder, for passes that rework a
// level's textures and must write them back in the format they came in (tools/level_port/hd_textures.ts). THUG2's
// .tex.xbx DXT values (tex.ts): 1 and 2 are DXT1 (2: with 1-bit alpha), 5 is DXT5. Texels are B, G, R, A bytes - the
// A8R8G8B8 layout the 32-bit textures use.
//
// The encoder is a plain range fit (each block's colour bounding box, inset by 1/16, its diagonal the line the
// indices project on; DXT5's alpha its own min / max with the 8-value ramp) - not as good as a cluster fit, but
// deterministic, fast and well under the upscaled textures' own error.

function expand565(c: number): [number, number, number] {
    const r = (c >> 11) & 31;
    const g = (c >> 5) & 63;
    const b = c & 31;
    return [(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)];
}

function pack565(r: number, g: number, b: number): number {
    return (
        ((Math.round((r * 31) / 255) & 31) << 11) |
        ((Math.round((g * 63) / 255) & 63) << 5) |
        (Math.round((b * 31) / 255) & 31)
    );
}

// The four (or three + transparent) colours of a DXT1 block as [r, g, b, a].
function palette(c0: number, c1: number, fourColours: boolean): number[][] {
    const a = expand565(c0);
    const b = expand565(c1);
    if (fourColours) {
        return [
            [...a, 255],
            [...b, 255],
            [0, 1, 2].map((k) => Math.floor((2 * a[k]! + b[k]!) / 3)).concat(255),
            [0, 1, 2].map((k) => Math.floor((a[k]! + 2 * b[k]!) / 3)).concat(255),
        ];
    }
    return [[...a, 255], [...b, 255], [0, 1, 2].map((k) => (a[k]! + b[k]!) >> 1).concat(255), [0, 0, 0, 0]];
}

function alphaRamp(a0: number, a1: number): number[] {
    const ramp = [a0, a1];
    if (a0 > a1) for (let k = 1; k < 7; k++) ramp.push(Math.floor(((7 - k) * a0 + k * a1) / 7));
    else {
        for (let k = 1; k < 5; k++) ramp.push(Math.floor(((5 - k) * a0 + k * a1) / 5));
        ramp.push(0, 255);
    }
    return ramp;
}

export function blockBytes(dxt: number): number {
    return dxt === 5 ? 16 : 8;
}

export function levelSize(width: number, height: number, dxt: number): number {
    return Math.max(1, Math.ceil(width / 4)) * Math.max(1, Math.ceil(height / 4)) * blockBytes(dxt);
}

// A DXT level as BGRA texels.
export function decode(data: Uint8Array, width: number, height: number, dxt: number): Buffer {
    const out = Buffer.alloc(width * height * 4);
    const bw = Math.max(1, Math.ceil(width / 4));
    const bh = Math.max(1, Math.ceil(height / 4));
    const size = blockBytes(dxt);
    if (data.length < bw * bh * size) throw new Error(`DXT${dxt} ${width}x${height}: ${data.length} bytes`);
    for (let by = 0; by < bh; by++) {
        for (let bx = 0; bx < bw; bx++) {
            const at = (by * bw + bx) * size;
            const colourAt = dxt === 5 ? at + 8 : at;
            const c0 = data[colourAt]! | (data[colourAt + 1]! << 8);
            const c1 = data[colourAt + 2]! | (data[colourAt + 3]! << 8);
            const pal = palette(c0, c1, dxt === 5 || c0 > c1);
            const bits =
                (data[colourAt + 4]! | (data[colourAt + 5]! << 8) | (data[colourAt + 6]! << 16)) +
                data[colourAt + 7]! * 0x1000000;
            let alphas: number[] | null = null;
            if (dxt === 5) {
                const ramp = alphaRamp(data[at]!, data[at + 1]!);
                alphas = [];
                // 16 x 3-bit indices, 48 bits little-endian
                let lo = data[at + 2]! | (data[at + 3]! << 8) | (data[at + 4]! << 16);
                let hi = data[at + 5]! | (data[at + 6]! << 8) | (data[at + 7]! << 16);
                for (let k = 0; k < 8; k++, lo >>= 3) alphas.push(ramp[lo & 7]!);
                for (let k = 0; k < 8; k++, hi >>= 3) alphas.push(ramp[hi & 7]!);
            }
            for (let k = 0; k < 16; k++) {
                const x = bx * 4 + (k & 3);
                const y = by * 4 + (k >> 2);
                if (x >= width || y >= height) continue;
                const c = pal[Math.floor(bits / 4 ** k) & 3]!;
                const o = (y * width + x) * 4;
                out[o] = c[2]!;
                out[o + 1] = c[1]!;
                out[o + 2] = c[0]!;
                out[o + 3] = alphas ? alphas[k]! : c[3]!;
            }
        }
    }
    return out;
}

function nearest(ramp: readonly number[], v: number): number {
    let best = 0;
    for (let i = 1; i < ramp.length; i++) if (Math.abs(ramp[i]! - v) < Math.abs(ramp[best]! - v)) best = i;
    return best;
}

// BGRA texels as a DXT level (1 or 2: DXT1 - texels with alpha below 128 transparent; 5: DXT5).
export function encode(bgra: Uint8Array, width: number, height: number, dxt: number): Buffer {
    const bw = Math.max(1, Math.ceil(width / 4));
    const bh = Math.max(1, Math.ceil(height / 4));
    const size = blockBytes(dxt);
    const out = Buffer.alloc(bw * bh * size);
    const block: number[][] = [];
    for (let by = 0; by < bh; by++) {
        for (let bx = 0; bx < bw; bx++) {
            block.length = 0;
            for (let k = 0; k < 16; k++) {
                // (a block past a small level's edge repeats its last texels)
                const x = Math.min(width - 1, bx * 4 + (k & 3));
                const y = Math.min(height - 1, by * 4 + (k >> 2));
                const o = (y * width + x) * 4;
                block.push([bgra[o + 2]!, bgra[o + 1]!, bgra[o]!, bgra[o + 3]!]);
            }
            const at = (by * bw + bx) * size;
            const transparent = dxt !== 5 ? block.map((c) => c[3]! < 128) : block.map(() => false);
            const opaque = block.filter((_, k) => !transparent[k]);
            const lo = [255, 255, 255];
            const hi = [0, 0, 0];
            for (const c of opaque) {
                for (let k = 0; k < 3; k++) {
                    lo[k] = Math.min(lo[k]!, c[k]!);
                    hi[k] = Math.max(hi[k]!, c[k]!);
                }
            }
            if (!opaque.length) {
                lo.fill(0);
                hi.fill(0);
            }
            for (let k = 0; k < 3; k++) {
                const inset = (hi[k]! - lo[k]!) / 16;
                lo[k] = lo[k]! + inset;
                hi[k] = hi[k]! - inset;
            }
            // the box's diagonal the colours lie along: a channel that falls as the widest one rises is flipped
            if (opaque.length) {
                const widest = [0, 1, 2].reduce((best, k) => (hi[k]! - lo[k]! > hi[best]! - lo[best]! ? k : best), 0);
                const mean = [0, 1, 2].map((k) => opaque.reduce((s, c) => s + c[k]!, 0) / opaque.length);
                for (let k = 0; k < 3; k++) {
                    if (k === widest) continue;
                    let cov = 0;
                    for (const c of opaque) cov += (c[k]! - mean[k]!) * (c[widest]! - mean[widest]!);
                    if (cov < 0) [lo[k], hi[k]] = [hi[k]!, lo[k]!];
                }
            }
            let c0 = pack565(hi[0]!, hi[1]!, hi[2]!);
            let c1 = pack565(lo[0]!, lo[1]!, lo[2]!);
            const threeColours = transparent.some(Boolean);
            // four colours need c0 > c1, three (+ transparent) c0 <= c1
            if (threeColours ? c0 > c1 : c0 < c1) [c0, c1] = [c1, c0];
            if (!threeColours && c0 === c1) {
                // (one colour: four-colour mode with index 0 everywhere needs c0 > c1)
                if (c1 > 0) c1 -= 1;
                else c0 += 1;
            }
            const pal = palette(c0, c1, !threeColours);
            let bits = 0;
            for (let k = 0; k < 16; k++) {
                let index = 3;
                if (!transparent[k]) {
                    const c = block[k]!;
                    let best = Infinity;
                    for (let i = 0; i < (threeColours ? 3 : 4); i++) {
                        const p = pal[i]!;
                        const d = (p[0]! - c[0]!) ** 2 + (p[1]! - c[1]!) ** 2 + (p[2]! - c[2]!) ** 2;
                        if (d < best) {
                            best = d;
                            index = i;
                        }
                    }
                }
                bits += index * 4 ** k;
            }
            const colourAt = dxt === 5 ? at + 8 : at;
            out.writeUInt16LE(c0, colourAt);
            out.writeUInt16LE(c1, colourAt + 2);
            out.writeUInt32LE(bits >>> 0, colourAt + 4);
            if (dxt === 5) {
                const as = block.map((c) => c[3]!);
                let a0 = Math.max(...as);
                let a1 = Math.min(...as);
                if (a0 === a1) {
                    if (a1 > 0) a1 -= 1;
                    else a0 += 1;
                }
                const ramp = alphaRamp(a0, a1);
                out[at] = a0;
                out[at + 1] = a1;
                let low = 0;
                let high = 0;
                for (let k = 0; k < 8; k++) low |= nearest(ramp, as[k]!) << (3 * k);
                for (let k = 0; k < 8; k++) high |= nearest(ramp, as[k + 8]!) << (3 * k);
                out[at + 2] = low & 0xff;
                out[at + 3] = (low >> 8) & 0xff;
                out[at + 4] = (low >> 16) & 0xff;
                out[at + 5] = high & 0xff;
                out[at + 6] = (high >> 8) & 0xff;
                out[at + 7] = (high >> 16) & 0xff;
            }
        }
    }
    return out;
}
