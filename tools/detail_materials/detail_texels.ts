// NOT ORIGINAL - a material scan (colour, normal and roughness maps) made into a detail texture: only what is finer than
// an eighth... a sixteenth of the tile is kept, so the scan's own pattern (brick courses, slabs, planks) doesn't print
// over the game's - the game's texture keeps its layout, the detail adds the grain. Per texel (detail_file.ts):
//   R, G - the normal (the scan's OpenGL-style map: +y up the picture, turned to +v down it) less its local mean (a
//          box blur of the tile's sixteenth, wrapping round: the scans tile), as 0 - 255 for -1 - 1;
//   B    - the roughness less its local mean, about 0.5;
//   A    - the colour's luminance over its local mean (0.25 - 2) x 127.5: the albedo's grain, not its colour.
import type { FloatImage } from "./png_read.ts";

export const LUMA = [0.2126, 0.7152, 0.0722] as const;
const BLUR_SHARE = 1 / 16;
const RATIO_MIN = 0.25;
const RATIO_MAX = 2;

// One channel of an image (grey: 0; a missing channel: the first) resampled to side x side by area (box) averaging -
// the scans are 1 - 2K, the detail textures 256 - 2048.
export function channelAt(image: FloatImage, channel: number, side: number): Float32Array {
    const out = new Float32Array(side * side);
    const c = Math.min(channel, image.channels - 1);
    const sx = image.width / side;
    const sy = image.height / side;
    for (let y = 0; y < side; y++) {
        const y0 = Math.floor(y * sy);
        const y1 = Math.max(y0 + 1, Math.floor((y + 1) * sy));
        for (let x = 0; x < side; x++) {
            const x0 = Math.floor(x * sx);
            const x1 = Math.max(x0 + 1, Math.floor((x + 1) * sx));
            let sum = 0;
            let n = 0;
            for (let yy = y0; yy < y1 && yy < image.height; yy++) {
                for (let xx = x0; xx < x1 && xx < image.width; xx++) {
                    sum += image.data[(yy * image.width + xx) * image.channels + c]!;
                    n++;
                }
            }
            out[y * side + x] = n ? sum / n : 0;
        }
    }
    return out;
}

// A box blur of radius r (2r + 1 texels), wrapping round, rows then columns.
export function boxBlur(values: Float32Array, side: number, r: number): Float32Array {
    const pass = (from: Float32Array, horizontal: boolean): Float32Array => {
        const out = new Float32Array(side * side);
        const width = 2 * r + 1;
        for (let line = 0; line < side; line++) {
            const at = (i: number): number => {
                const k = ((i % side) + side) % side;
                return horizontal ? from[line * side + k]! : from[k * side + line]!;
            };
            let sum = 0;
            for (let i = -r; i <= r; i++) sum += at(i);
            for (let i = 0; i < side; i++) {
                if (horizontal) out[line * side + i] = sum / width;
                else out[i * side + line] = sum / width;
                sum += at(i + r + 1) - at(i - r);
            }
        }
        return out;
    };
    return pass(pass(values, true), false);
}

const byte = (value: number): number => Math.max(0, Math.min(255, Math.round(value)));

export function detailTexels(colour: FloatImage, normal: FloatImage, roughness: FloatImage, side: number): Uint8Array {
    const r = Math.max(1, Math.round(side * BLUR_SHARE));
    const red = channelAt(colour, 0, side);
    const green = channelAt(colour, 1, side);
    const blue = channelAt(colour, 2, side);
    const luma = new Float32Array(side * side);
    for (let i = 0; i < luma.length; i++) luma[i] = LUMA[0] * red[i]! + LUMA[1] * green[i]! + LUMA[2] * blue[i]!;
    const lumaMean = boxBlur(luma, side, r);
    const nx = channelAt(normal, 0, side).map((v) => v * 2 - 1);
    const ny = channelAt(normal, 1, side).map((v) => -(v * 2 - 1)); // (+y up the picture -> +v down it)
    const nxMean = boxBlur(nx, side, r);
    const nyMean = boxBlur(ny, side, r);
    const rough = channelAt(roughness, 0, side);
    const roughMean = boxBlur(rough, side, r);
    const out = new Uint8Array(side * side * 4);
    for (let i = 0; i < side * side; i++) {
        const ratio = Math.min(RATIO_MAX, Math.max(RATIO_MIN, luma[i]! / Math.max(lumaMean[i]!, 1e-3)));
        out[i * 4] = byte((nx[i]! - nxMean[i]!) * 127.5 + 127.5);
        out[i * 4 + 1] = byte((ny[i]! - nyMean[i]!) * 127.5 + 127.5);
        out[i * 4 + 2] = byte((rough[i]! - roughMean[i]! + 0.5) * 255);
        out[i * 4 + 3] = byte(ratio * 127.5);
    }
    return out;
}
