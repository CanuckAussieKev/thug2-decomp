// NOT ORIGINAL - the classical surface-map generator: height from the texture's luminance, normals from the height by
// a Sobel filter, roughness from the class's rule and the texture's local grain. No model, no download: the fallback
// for every texture a model does badly on, and the default (docs/design/vulkan.md "Per-pixel lighting and surface
// maps"). Textures tile, so every filter wraps round the edges.
//
//   luma      Rec. 709 of the linearised colour (sRGB's curve: 2.2)
//   height    0.5 + sum over scales s of w_s (luma - blur_s(luma)) / (4 sigma) - the band-passed luma (dark is low:
//             cracks, mortar, gaps), scales 1, 2, 4, 8 texels x size / 256 with weights 0.4, 0.3, 0.2, 0.1, sigma the
//             band's deviation, clamped to 0 - 1
//   normal    (-dh/du x k, -dh/dv x k, 1) normalised, the central differences of a Sobel kernel, k = 3 x the texture's
//             bump - its class's, weakened or raised by its level's terrains and its texel size (surfaces.ts) - (rows
//             grow downward: +v)
//   roughness the class's base + its half-range x tanh((g - mean g) / deviation of g) / 2, g the local grain (the
//             blurred |luma - blur_1(luma)|), clamped to the class's range

import { type ClassRule, clampRoughness } from "./classes.ts";

export interface Rgba {
    width: number;
    height: number;
    data: Uint8Array; // RGBA rows, top first
}

export function linear(byte: number): number {
    return (byte / 255) ** 2.2;
}

export function luminance(image: Rgba): Float32Array {
    const out = new Float32Array(image.width * image.height);
    for (let i = 0; i < out.length; i++) {
        const r = linear(image.data[i * 4]!);
        const g = linear(image.data[i * 4 + 1]!);
        const b = linear(image.data[i * 4 + 2]!);
        out[i] = (0.2126 * r + 0.7152 * g + 0.0722 * b) ** (1 / 2.2); // (back to a perceptual scale)
    }
    return out;
}

// A box blur of radius r along x then y, three times (close to a Gaussian), wrapping.
export function blurWrap(src: Float32Array, width: number, height: number, radius: number): Float32Array {
    let a = Float32Array.from(src);
    let b = new Float32Array(src.length);
    const r = Math.max(1, Math.round(radius));
    const span = 2 * r + 1;
    for (let pass = 0; pass < 3; pass++) {
        for (let y = 0; y < height; y++) {
            let sum = 0;
            for (let k = -r; k <= r; k++) sum += a[y * width + (((k % width) + width) % width)]!;
            for (let x = 0; x < width; x++) {
                b[y * width + x] = sum / span;
                const out = (((x - r) % width) + width) % width;
                const into = (x + r + 1) % width;
                sum += a[y * width + into]! - a[y * width + out]!;
            }
        }
        [a, b] = [b, a];
        for (let x = 0; x < width; x++) {
            let sum = 0;
            for (let k = -r; k <= r; k++) sum += a[(((k % height) + height) % height) * width + x]!;
            for (let y = 0; y < height; y++) {
                b[y * width + x] = sum / span;
                const out = (((y - r) % height) + height) % height;
                const into = (y + r + 1) % height;
                sum += a[into * width + x]! - a[out * width + x]!;
            }
        }
        [a, b] = [b, a];
    }
    return a;
}

function deviation(values: Float32Array): { mean: number; sd: number } {
    let sum = 0;
    for (const v of values) sum += v;
    const mean = sum / Math.max(1, values.length);
    let sq = 0;
    for (const v of values) sq += (v - mean) * (v - mean);
    return { mean, sd: Math.sqrt(sq / Math.max(1, values.length)) };
}

export function heightFrom(luma: Float32Array, width: number, height: number): Float32Array {
    const scale = Math.max(1, Math.min(width, height) / 256);
    const weights = [0.4, 0.3, 0.2, 0.1];
    const band = new Float32Array(luma.length);
    weights.forEach((weight, index) => {
        const blurred = blurWrap(luma, width, height, 2 ** index * scale);
        for (let i = 0; i < band.length; i++) band[i]! += weight * (luma[i]! - blurred[i]!);
    });
    const { mean, sd } = deviation(band);
    const out = new Float32Array(luma.length);
    for (let i = 0; i < out.length; i++) {
        out[i] = sd > 1e-6 ? Math.min(1, Math.max(0, 0.5 + (band[i]! - mean) / (4 * sd))) : 0.5;
    }
    return out;
}

// Tangent-space normals (x along +u, y along +v), three floats a texel.
export function normalsFrom(heights: Float32Array, width: number, height: number, bump: number): Float32Array {
    const out = new Float32Array(heights.length * 3);
    const at = (x: number, y: number) =>
        heights[(((y % height) + height) % height) * width + (((x % width) + width) % width)]!;
    const k = 3 * bump;
    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
            const du =
                (at(x + 1, y - 1) +
                    2 * at(x + 1, y) +
                    at(x + 1, y + 1) -
                    at(x - 1, y - 1) -
                    2 * at(x - 1, y) -
                    at(x - 1, y + 1)) /
                8;
            const dv =
                (at(x - 1, y + 1) +
                    2 * at(x, y + 1) +
                    at(x + 1, y + 1) -
                    at(x - 1, y - 1) -
                    2 * at(x, y - 1) -
                    at(x + 1, y - 1)) /
                8;
            const nx = -du * k;
            const ny = -dv * k;
            const length = Math.hypot(nx, ny, 1);
            const i = (y * width + x) * 3;
            out[i] = nx / length;
            out[i + 1] = ny / length;
            out[i + 2] = 1 / length;
        }
    }
    return out;
}

export function roughnessFrom(luma: Float32Array, width: number, height: number, rule: ClassRule): Float32Array {
    const fine = blurWrap(luma, width, height, 1);
    const grain = new Float32Array(luma.length);
    for (let i = 0; i < grain.length; i++) grain[i] = Math.abs(luma[i]! - fine[i]!);
    const local = blurWrap(grain, width, height, Math.max(1, Math.min(width, height) / 64));
    const { mean, sd } = deviation(local);
    const half = (rule.max - rule.min) / 2;
    const out = new Float32Array(luma.length);
    for (let i = 0; i < out.length; i++) {
        const z = sd > 1e-6 ? (local[i]! - mean) / sd : 0;
        out[i] = clampRoughness(rule.base + (half * Math.tanh(z)) / 2, rule);
    }
    return out;
}

export function encodeSigned(value: number): number {
    return Math.round(Math.min(255, Math.max(0, value * 127.5 + 127.5)));
}
export function encodeUnit(value: number): number {
    return Math.round(Math.min(1, Math.max(0, value)) * 255);
}

// The map's texels (surface_map_format.h): normal x, y, roughness, height.
export function packMaps(normals: Float32Array, roughness: Float32Array, heights: Float32Array): Uint8Array {
    const out = new Uint8Array(heights.length * 4);
    for (let i = 0; i < heights.length; i++) {
        out[i * 4] = encodeSigned(normals[i * 3]!);
        out[i * 4 + 1] = encodeSigned(normals[i * 3 + 1]!);
        out[i * 4 + 2] = encodeUnit(roughness[i]!);
        out[i * 4 + 3] = encodeUnit(heights[i]!);
    }
    return out;
}

// Packed maps (packMaps' layout) with their normals' tilt scaled by `factor` (x and y times it, renormalised): a model's
// maps brought to the texture's bump.
export function scaleNormals(maps: Uint8Array, factor: number): Uint8Array {
    if (factor === 1) return maps;
    const out = Uint8Array.from(maps);
    for (let i = 0; i < out.length; i += 4) {
        const x = (out[i]! - 127.5) / 127.5;
        const y = (out[i + 1]! - 127.5) / 127.5;
        const z = Math.sqrt(Math.max(0, 1 - x * x - y * y));
        const length = Math.hypot(x * factor, y * factor, z) || 1;
        out[i] = encodeSigned((x * factor) / length);
        out[i + 1] = encodeSigned((y * factor) / length);
    }
    return out;
}

// `bump`: the texture's own (surfaces.ts bumpFor: its terrains and texel size) - the class's when not given.
export function classicalMaps(image: Rgba, rule: ClassRule, bump = rule.bump): Uint8Array {
    const luma = luminance(image);
    const heights = heightFrom(luma, image.width, image.height);
    const normals = normalsFrom(heights, image.width, image.height, bump);
    const roughness = roughnessFrom(luma, image.width, image.height, rule);
    return packMaps(normals, roughness, heights);
}
