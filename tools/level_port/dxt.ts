// NOT ORIGINAL - DXT1 / DXT5 (S3TC, Direct3D's BC1 / BC3) decoding and a small encoder, for passes that rework a
// level's textures and must write them back in the format they came in (tools/level_port/hd_textures.ts). THUG2's
// .tex.xbx DXT values (tex.ts): 1 and 2 are DXT1 (2: with 1-bit alpha), 5 is DXT5. Texels are B, G, R, A bytes - the
// A8R8G8B8 layout the 32-bit textures use.
//
// The encoder is deterministic (no randomness, no timing): per block the principal axis of the colours (power
// iteration) gives the first endpoints, then up to four rounds of least squares refit them to the indices the
// palette assigns (kept only while the error falls); DXT5 alpha tries the 8-value ramp and the 6-value one with 0 / 255.

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

// 4-colour mode: index 0..3 sit at t = 0, 1, 1/3, 2/3 between c0 and c1; 3-colour mode (+ transparent): 0, 1, 1/2.
const T4 = [0, 1, 1 / 3, 2 / 3];
const T3 = [0, 1, 0.5];

// the block's palette as RGB triples (what decode() gives) for endpoints c0 / c1
function fillPalette(out: Int32Array, c0: number, c1: number, four: boolean): void {
    const a = expand565(c0);
    const b = expand565(c1);
    for (let k = 0; k < 3; k++) {
        out[k] = a[k]!;
        out[3 + k] = b[k]!;
        out[6 + k] = four ? Math.floor((2 * a[k]! + b[k]!) / 3) : (a[k]! + b[k]!) >> 1;
        if (four) out[9 + k] = Math.floor((a[k]! + 2 * b[k]!) / 3);
    }
}

// Nearest palette entry for each of the `n` colours (rgb, interleaved): fills `index`, returns the squared error.
function assign(pal: Int32Array, entries: number, rgb: Int32Array, n: number, index: Uint8Array): number {
    let total = 0;
    for (let i = 0; i < n; i++) {
        const r = rgb[i * 3]!;
        const g = rgb[i * 3 + 1]!;
        const b = rgb[i * 3 + 2]!;
        let best = 1 << 30;
        let which = 0;
        for (let e = 0; e < entries; e++) {
            const dr = pal[e * 3]! - r;
            const dg = pal[e * 3 + 1]! - g;
            const db = pal[e * 3 + 2]! - b;
            const d = dr * dr + dg * dg + db * db;
            if (d < best) {
                best = d;
                which = e;
            }
        }
        index[i] = which;
        total += best;
    }
    return total;
}

const clamp255 = (v: number): number => (v < 0 ? 0 : v > 255 ? 255 : v);

// c0 / c1 in the order the mode needs (four colours: c0 > c1; three: c0 <= c1); a one-colour pair is made two-valued
function ordered(c0: number, c1: number, four: boolean): [number, number] {
    if (four) {
        if (c0 < c1) return [c1, c0];
        if (c0 === c1) return c1 > 0 ? [c0, c1 - 1] : [c0 + 1, c1];
        return [c0, c1];
    }
    return c0 > c1 ? [c1, c0] : [c0, c1];
}

const palA = new Int32Array(12);
const palB = new Int32Array(12);
const idxTrial = new Uint8Array(16);

// The two endpoints for `n` opaque colours (rgb, interleaved); `index` gets each colour's palette slot.
function fitColours(rgb: Int32Array, n: number, four: boolean, index: Uint8Array): { c0: number; c1: number } {
    const entries = four ? 4 : 3;
    const t = four ? T4 : T3;
    // the principal axis of the colours (power iteration on their covariance)
    const mean = [0, 0, 0];
    for (let i = 0; i < n; i++) for (let k = 0; k < 3; k++) mean[k] = mean[k]! + rgb[i * 3 + k]!;
    for (let k = 0; k < 3; k++) mean[k] = mean[k]! / n;
    const cov = [0, 0, 0, 0, 0, 0]; // rr gg bb rg rb gb
    for (let i = 0; i < n; i++) {
        const r = rgb[i * 3]! - mean[0]!;
        const g = rgb[i * 3 + 1]! - mean[1]!;
        const b = rgb[i * 3 + 2]! - mean[2]!;
        cov[0] = cov[0]! + r * r;
        cov[1] = cov[1]! + g * g;
        cov[2] = cov[2]! + b * b;
        cov[3] = cov[3]! + r * g;
        cov[4] = cov[4]! + r * b;
        cov[5] = cov[5]! + g * b;
    }
    // start from the covariance column of the widest channel
    let ax = cov[0]!;
    let ay = cov[3]!;
    let az = cov[4]!;
    if (cov[1]! > cov[0]! && cov[1]! >= cov[2]!) [ax, ay, az] = [cov[3]!, cov[1]!, cov[5]!];
    else if (cov[2]! > cov[0]! && cov[2]! > cov[1]!) [ax, ay, az] = [cov[4]!, cov[5]!, cov[2]!];
    if (Math.abs(ax) + Math.abs(ay) + Math.abs(az) < 1e-9) [ax, ay, az] = [1, 1, 1];
    for (let it = 0; it < 8; it++) {
        const x = cov[0]! * ax + cov[3]! * ay + cov[4]! * az;
        const y = cov[3]! * ax + cov[1]! * ay + cov[5]! * az;
        const z = cov[4]! * ax + cov[5]! * ay + cov[2]! * az;
        const len = Math.max(Math.abs(x), Math.abs(y), Math.abs(z));
        if (len < 1e-9) break;
        ax = x / len;
        ay = y / len;
        az = z / len;
    }
    let lo = Infinity;
    let hi = -Infinity;
    for (let i = 0; i < n; i++) {
        const p = (rgb[i * 3]! - mean[0]!) * ax + (rgb[i * 3 + 1]! - mean[1]!) * ay + (rgb[i * 3 + 2]! - mean[2]!) * az;
        if (p < lo) lo = p;
        if (p > hi) hi = p;
    }
    const norm = ax * ax + ay * ay + az * az;
    const end = (p: number): number =>
        pack565(
            clamp255(mean[0]! + (ax * p) / norm),
            clamp255(mean[1]! + (ay * p) / norm),
            clamp255(mean[2]! + (az * p) / norm),
        );
    let [c0, c1] = ordered(end(hi), end(lo), four);
    fillPalette(palA, c0, c1, four);
    let err = assign(palA, entries, rgb, n, index);
    for (let round = 0; round < 4 && err > 0; round++) {
        // least squares endpoints for the current indices: colour = A (1 - t) + B t
        let aa = 0;
        let ab = 0;
        let bb = 0;
        const ca = [0, 0, 0];
        const cb = [0, 0, 0];
        for (let i = 0; i < n; i++) {
            const tt = t[index[i]!]!;
            const wa = 1 - tt;
            aa += wa * wa;
            ab += wa * tt;
            bb += tt * tt;
            for (let k = 0; k < 3; k++) {
                ca[k] = ca[k]! + wa * rgb[i * 3 + k]!;
                cb[k] = cb[k]! + tt * rgb[i * 3 + k]!;
            }
        }
        const det = aa * bb - ab * ab;
        if (det < 1e-6) break;
        const A = [0, 1, 2].map((k) => clamp255((ca[k]! * bb - cb[k]! * ab) / det));
        const B = [0, 1, 2].map((k) => clamp255((cb[k]! * aa - ca[k]! * ab) / det));
        const [n0, n1] = ordered(pack565(A[0]!, A[1]!, A[2]!), pack565(B[0]!, B[1]!, B[2]!), four);
        if (n0 === c0 && n1 === c1) break;
        fillPalette(palB, n0, n1, four);
        const e = assign(palB, entries, rgb, n, idxTrial);
        if (e >= err) break;
        err = e;
        c0 = n0;
        c1 = n1;
        index.set(idxTrial);
    }
    return { c0, c1 };
}

const flat = new Int32Array(48);
const idx = new Uint8Array(16);

// BGRA texels as a DXT level (1 or 2: DXT1 - texels with alpha below 128 transparent; 5: DXT5).
export function encode(bgra: Uint8Array, width: number, height: number, dxt: number): Buffer {
    const bw = Math.max(1, Math.ceil(width / 4));
    const bh = Math.max(1, Math.ceil(height / 4));
    const size = blockBytes(dxt);
    const out = Buffer.alloc(bw * bh * size);
    const alphas = new Uint8Array(16);
    const slot = new Int8Array(16);
    for (let by = 0; by < bh; by++) {
        for (let bx = 0; bx < bw; bx++) {
            const at = (by * bw + bx) * size;
            let n = 0;
            let hasTransparent = false;
            for (let k = 0; k < 16; k++) {
                // (a block past a small level's edge repeats its last texels)
                const x = Math.min(width - 1, bx * 4 + (k & 3));
                const y = Math.min(height - 1, by * 4 + (k >> 2));
                const o = (y * width + x) * 4;
                alphas[k] = bgra[o + 3]!;
                if (dxt !== 5 && alphas[k]! < 128) {
                    slot[k] = -1;
                    hasTransparent = true;
                } else {
                    slot[k] = n;
                    flat[n * 3] = bgra[o + 2]!;
                    flat[n * 3 + 1] = bgra[o + 1]!;
                    flat[n * 3 + 2] = bgra[o]!;
                    n += 1;
                }
            }
            let c0 = 0;
            let c1 = 0;
            if (n > 0) ({ c0, c1 } = fitColours(flat, n, !hasTransparent, idx));
            let bits = 0;
            for (let k = 0; k < 16; k++) bits += (slot[k]! < 0 ? 3 : idx[slot[k]!]!) * 4 ** k;
            const colourAt = dxt === 5 ? at + 8 : at;
            out.writeUInt16LE(c0, colourAt);
            out.writeUInt16LE(c1, colourAt + 2);
            out.writeUInt32LE(bits >>> 0, colourAt + 4);
            if (dxt === 5) encodeAlpha(out, at, alphas);
        }
    }
    return out;
}

function rampError(ramp: readonly number[], as: Uint8Array): number {
    let e = 0;
    for (let k = 0; k < 16; k++) e += (ramp[nearest(ramp, as[k]!)]! - as[k]!) ** 2;
    return e;
}

// DXT5's alpha block at `at`: the 8-value ramp between the block's max and min or - when the block holds 0 and / or
// 255 - the 6-value ramp with those two built in, whichever errs less.
function encodeAlpha(out: Buffer, at: number, as: Uint8Array): void {
    let mn = 255;
    let mx = 0;
    let imn = 255;
    let imx = 0;
    for (let k = 0; k < 16; k++) {
        const a = as[k]!;
        mn = Math.min(mn, a);
        mx = Math.max(mx, a);
        if (a !== 0 && a !== 255) {
            imn = Math.min(imn, a);
            imx = Math.max(imx, a);
        }
    }
    let a0 = mx;
    let a1 = mn;
    if (a0 === a1) {
        if (a1 > 0) a1 -= 1;
        else a0 += 1;
    }
    let ramp = alphaRamp(a0, a1);
    if (mn === 0 || mx === 255) {
        // 6-value mode (a0 <= a1): the ramp between the inner extremes, 0 and 255 appended
        const lo = imx >= imn ? imn : 0;
        const hi = imx >= imn ? imx : 0;
        const six = alphaRamp(lo, hi);
        if (rampError(six, as) < rampError(ramp, as)) {
            ramp = six;
            a0 = lo;
            a1 = hi;
        }
    }
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
