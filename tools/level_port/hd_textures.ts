// NOT ORIGINAL - an optional pass that upscales a converted level's textures with an AI upscaler (Real-ESRGAN's
// standalone realesrgan-ncnn-vulkan: Vulkan, no CUDA / PyTorch - tools/hd_textures/README.md; fetched by
// tools/hd_textures/fetch.ts into out\, never committed). Off by default; a converter turns it on with
// `--hd-textures [2|4]` (tools/thps3_levels).
//
// Per texture of a level's dictionary (tex.TexFile, before it's written):
// - the top level as BGRA texels (DXT ones decoded - tools/level_port/dxt.ts);
// - skipped: textures smaller than --hd-min (default 16) on their longer side, single-colour ones, and ones already
//   at the size cap (--hd-max, default 1024: the scale halves until the longer side fits);
// - the colour upscaled on its own, as RGB: the texels under alpha 0 first filled from their visible neighbours (what
//   lies under a cut-out's holes is often black: upscaled as is it bleeds a dark halo into the edges);
// - alpha on its own: all opaque stays opaque; a cut-out (every alpha 0 or 255, give or take 8) is scaled
//   bilinearly and thresholded at 128 - sharp edges, no grey fringe the alpha test would show; any other alpha
//   (soft shadows, glass, smoke) goes through the model as a grey image;
// - before upscaling the image is padded by 16 texels on every side - with the texture's own other edge where a
//   material wraps it (u / v addressing), its edge texels where it clamps - and the border cut off afterwards: the
//   model sees the neighbours a tiled texture has in the game, so tiling shows no seams;
// - the model always scales by 4 (realesrgan-x4plus / -anime are 4x models); scale 2 is the 4x result 2 x 2
//   averaged;
// - written back in the format it came in (32-bit stays 32-bit, DXT1 / DXT5 re-encoded) with a full mip chain: as many
//   levels as before plus the ones the larger size adds (a texture written with one level keeps one).
//
// Results are cached by the source texture's hash and every setting that changes the output (model, the scale used,
// addressing, the pass's version, the tool's release) in --hd-cache (default %LOCALAPPDATA%\thug2-decomp\hd-textures):
// a convert without --hd-textures never touches it, and switching it back on takes every unchanged texture from there.

import { spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import * as zlib from "node:zlib";
import { decodePng, encodePng, type Image } from "../game/frame_diff.ts";
import { REPO } from "./common.ts";
import * as dxt from "./dxt.ts";
import type { TexFile, Texture } from "./tex.ts";

// The pinned release (tools/hd_textures/fetch.ts): Real-ESRGAN v0.2.5.0's Windows build.
export const TOOL_RELEASE = "realesrgan-ncnn-vulkan-20220424";
export const DEFAULT_TOOL = path.join(REPO, "out", "hd_textures", TOOL_RELEASE, "realesrgan-ncnn-vulkan.exe");
export const MODELS = ["realesrgan-x4plus", "realesrgan-x4plus-anime"] as const;
// bumped whenever the pass makes different pixels from the same input (old cache entries are then not used)
export const PASS_VERSION = 1;
export const PAD = 16;
const MODEL_SCALE = 4;

export function defaultCacheDir(): string {
    const local = process.env.LOCALAPPDATA;
    return local ? path.join(local, "thug2-decomp", "hd-textures") : path.join(REPO, "out", "hd-cache");
}

export interface HdOptions {
    scale: number; // 2 or 4
    maxSize: number; // the longer side's cap
    minSize: number; // textures smaller than this (longer side) are left as they are
    model: string;
    tool: string;
    cacheDir: string;
    gpu: string | null; // realesrgan's -g (null: its own choice)
    match: boolean; // pulled back to the original's colours and contrast (matchOriginal; --hd-no-match: the model's own)
}

export function defaultOptions(scale = 4): HdOptions {
    return {
        scale,
        maxSize: 1024,
        minSize: 16,
        model: "realesrgan-x4plus",
        tool: DEFAULT_TOOL,
        cacheDir: defaultCacheDir(),
        gpu: null,
        match: true,
    };
}

export interface HdStats {
    textures: number;
    upscaled: number;
    cacheHits: number;
    generated: number;
    skipped: Map<string, number>;
    bytesBefore: number;
    bytesAfter: number;
    seconds: number;
    toolSeconds: number;
}

export function newStats(): HdStats {
    return {
        textures: 0,
        upscaled: 0,
        cacheHits: 0,
        generated: 0,
        skipped: new Map(),
        bytesBefore: 0,
        bytesAfter: 0,
        seconds: 0,
        toolSeconds: 0,
    };
}

// How a texture is addressed by the materials that use it (a texture both wrapped and clamped: wrapped).
export interface Addressing {
    wrapU: boolean;
    wrapV: boolean;
}

// Upscales each image by 4 (the model); keys are kept.
export type Upscaler = (images: Map<string, Image>, options: HdOptions) => Map<string, Image>;

export type AlphaKind = "opaque" | "binary" | "smooth";

export function alphaKind(bgra: Uint8Array): AlphaKind {
    let opaque = true;
    for (let i = 3; i < bgra.length; i += 4) {
        const a = bgra[i]!;
        if (a > 8 && a < 247) return "smooth";
        if (a !== 255) opaque = false;
    }
    return opaque ? "opaque" : "binary";
}

function uniform(bgra: Uint8Array): boolean {
    for (let i = 4; i < bgra.length; i++) if (bgra[i] !== bgra[i & 3]) return false;
    return true;
}

// The texels under alpha 0 (<= 8) given their visible neighbours' average colour, ring by ring (wrap-aware): the
// colour that bleeds into an upscaled cut-out's edges is then the edge's own.
export function fillHidden(bgra: Uint8Array, w: number, h: number, wrapU: boolean, wrapV: boolean): Buffer {
    const out = Buffer.from(bgra);
    let known = new Uint8Array(w * h);
    let left = 0;
    for (let i = 0; i < w * h; i++) {
        known[i] = out[i * 4 + 3]! > 8 ? 1 : 0;
        if (!known[i]) left += 1;
    }
    if (left === w * h) return out;
    while (left > 0) {
        const next = known.slice();
        let filled = 0;
        for (let y = 0; y < h; y++) {
            for (let x = 0; x < w; x++) {
                const i = y * w + x;
                if (known[i]) continue;
                let n = 0;
                const sum = [0, 0, 0];
                for (let dy = -1; dy <= 1; dy++) {
                    for (let dx = -1; dx <= 1; dx++) {
                        if (!dx && !dy) continue;
                        let sx = x + dx;
                        let sy = y + dy;
                        if (sx < 0 || sx >= w) {
                            if (!wrapU) continue;
                            sx = (sx + w) % w;
                        }
                        if (sy < 0 || sy >= h) {
                            if (!wrapV) continue;
                            sy = (sy + h) % h;
                        }
                        const j = sy * w + sx;
                        if (!known[j]) continue;
                        n += 1;
                        for (let k = 0; k < 3; k++) sum[k] = sum[k]! + out[j * 4 + k]!;
                    }
                }
                if (!n) continue;
                for (let k = 0; k < 3; k++) out[i * 4 + k] = Math.round(sum[k]! / n);
                next[i] = 1;
                filled += 1;
            }
        }
        if (!filled) break;
        left -= filled;
        known = next;
    }
    return out;
}

function wrapIndex(v: number, n: number, wrap: boolean): number {
    if (wrap) return ((v % n) + n) % n;
    return v < 0 ? 0 : v >= n ? n - 1 : v;
}

// One channel set of a BGRA texture as a padded RGB image: "rgb" its colour, "alpha" its alpha as grey.
export function padded(
    bgra: Uint8Array,
    w: number,
    h: number,
    pad: number,
    wrapU: boolean,
    wrapV: boolean,
    what: "rgb" | "alpha",
): Image {
    const pw = w + 2 * pad;
    const ph = h + 2 * pad;
    const rgb = new Uint8Array(pw * ph * 3);
    for (let y = 0; y < ph; y++) {
        const sy = wrapIndex(y - pad, h, wrapV);
        for (let x = 0; x < pw; x++) {
            const s = (sy * w + wrapIndex(x - pad, w, wrapU)) * 4;
            const o = (y * pw + x) * 3;
            if (what === "rgb") {
                rgb[o] = bgra[s + 2]!;
                rgb[o + 1] = bgra[s + 1]!;
                rgb[o + 2] = bgra[s]!;
            } else rgb.fill(bgra[s + 3]!, o, o + 3);
        }
    }
    return { width: pw, height: ph, rgb };
}

// The middle of an upscaled padded image (the border, pad x factor wide, cut off), shrunk from `factor` to `scale`
// times the original by averaging (factor / scale) squared blocks: RGB.
export function cropAndShrink(img: Image, w: number, h: number, pad: number, factor: number, scale: number): Image {
    if (img.width !== (w + 2 * pad) * factor || img.height !== (h + 2 * pad) * factor)
        throw new Error(`upscaled image is ${img.width}x${img.height}, expected ${(w + 2 * pad) * factor} wide`);
    const box = factor / scale;
    const ow = w * scale;
    const oh = h * scale;
    const out = new Uint8Array(ow * oh * 3);
    const n = box * box;
    for (let y = 0; y < oh; y++) {
        for (let x = 0; x < ow; x++) {
            const sum = [0, 0, 0];
            for (let by = 0; by < box; by++) {
                const row = (pad * factor + y * box + by) * img.width;
                for (let bx = 0; bx < box; bx++) {
                    const s = (row + pad * factor + x * box + bx) * 3;
                    sum[0] = sum[0]! + img.rgb[s]!;
                    sum[1] = sum[1]! + img.rgb[s + 1]!;
                    sum[2] = sum[2]! + img.rgb[s + 2]!;
                }
            }
            const o = (y * ow + x) * 3;
            out[o] = Math.round(sum[0]! / n);
            out[o + 1] = Math.round(sum[1]! / n);
            out[o + 2] = Math.round(sum[2]! / n);
        }
    }
    return { width: ow, height: oh, rgb: out };
}

// A cut-out's alpha at `scale` times: bilinear (wrap / clamp as addressed), then 0 or 255 at 128.
export function binaryAlpha(
    bgra: Uint8Array,
    w: number,
    h: number,
    scale: number,
    wrapU: boolean,
    wrapV: boolean,
): Uint8Array {
    const ow = w * scale;
    const oh = h * scale;
    const out = new Uint8Array(ow * oh);
    const a = (x: number, y: number): number => bgra[(wrapIndex(y, h, wrapV) * w + wrapIndex(x, w, wrapU)) * 4 + 3]!;
    for (let y = 0; y < oh; y++) {
        const fy = (y + 0.5) / scale - 0.5;
        const y0 = Math.floor(fy);
        const ty = fy - y0;
        for (let x = 0; x < ow; x++) {
            const fx = (x + 0.5) / scale - 0.5;
            const x0 = Math.floor(fx);
            const tx = fx - x0;
            const v =
                (a(x0, y0) * (1 - tx) + a(x0 + 1, y0) * tx) * (1 - ty) +
                (a(x0, y0 + 1) * (1 - tx) + a(x0 + 1, y0 + 1) * tx) * ty;
            out[y * ow + x] = v >= 128 ? 255 : 0;
        }
    }
    return out;
}

// The next mip level of BGRA texels: each 2 x 2 block averaged (as thps3_levels' mipDown).
export function mipDown(level: Uint8Array, w: number, h: number): Buffer {
    const nw = Math.max(1, Math.floor(w / 2));
    const nh = Math.max(1, Math.floor(h / 2));
    const sx = w > 1 ? 2 : 1;
    const sy = h > 1 ? 2 : 1;
    const out = Buffer.alloc(nw * nh * 4);
    for (let y = 0; y < nh; y++) {
        const r0 = y * sy * w * 4;
        const r1 = r0 + (sy === 2 ? w * 4 : 0);
        for (let x = 0; x < nw; x++) {
            const c0 = r0 + x * sx * 4;
            const c1 = c0 + (sx === 2 ? 4 : 0);
            const d0 = r1 + x * sx * 4;
            const d1 = d0 + (sx === 2 ? 4 : 0);
            const o = (y * nw + x) * 4;
            for (let k = 0; k < 4; k++)
                out[o + k] = (level[c0 + k]! + level[c1 + k]! + level[d0 + k]! + level[d1 + k]! + 2) >> 2;
        }
    }
    return out;
}

// The scale used for a texture (1: left as it is).
export function scaleFor(w: number, h: number, options: HdOptions): number {
    let scale = options.scale;
    while (scale > 1 && Math.max(w, h) * scale > options.maxSize) scale /= 2;
    return scale;
}

export function topLevel(t: Texture): Buffer {
    const first = t.levels[0];
    if (first === undefined) throw new Error("texture without levels");
    if (t.dxt === 0) {
        if (t.texelDepth !== 32 || t.palette.length) throw new Error(`${t.texelDepth}-bit textures not handled`);
        return first;
    }
    return dxt.decode(first, t.width, t.height, t.dxt);
}

interface Job {
    texture: Texture;
    key: string;
    scale: number;
    bgra: Buffer;
    kind: AlphaKind;
    wrapU: boolean;
    wrapV: boolean;
}

function cacheKey(job: Omit<Job, "key">, options: HdOptions): string {
    const t = job.texture;
    const h = createHash("sha256");
    h.update(
        JSON.stringify({
            pass: PASS_VERSION,
            tool: TOOL_RELEASE,
            model: options.model,
            scale: job.scale,
            pad: PAD,
            wrapU: job.wrapU,
            wrapV: job.wrapV,
            width: t.width,
            height: t.height,
            dxt: t.dxt,
        }),
    );
    h.update(job.bgra);
    return h.digest("hex");
}

function cachePath(options: HdOptions, key: string): string {
    return path.join(options.cacheDir, key.slice(0, 2), `${key}.bgra.gz`);
}

function readCache(options: HdOptions, key: string, w: number, h: number): Buffer | null {
    const file = cachePath(options, key);
    if (!fs.existsSync(file)) return null;
    try {
        const data = zlib.gunzipSync(fs.readFileSync(file));
        if (data.length !== 8 + w * h * 4 || data.readUInt32LE(0) !== w || data.readUInt32LE(4) !== h) return null;
        return data.subarray(8);
    } catch {
        return null;
    }
}

function writeCache(options: HdOptions, key: string, w: number, h: number, bgra: Uint8Array): void {
    const file = cachePath(options, key);
    fs.mkdirSync(path.dirname(file), { recursive: true });
    const head = Buffer.alloc(8);
    head.writeUInt32LE(w, 0);
    head.writeUInt32LE(h, 4);
    const temp = `${file}.${process.pid}.tmp`;
    fs.writeFileSync(temp, zlib.gzipSync(Buffer.concat([head, bgra]), { level: 6 }));
    fs.renameSync(temp, file);
}

// The upscaled texels pulled back to the original (back-projection): twice, the difference between the original and
// the upscaled one averaged down to the original's size is spread back bilinearly (wrap / clamp as addressed). The
// model's own look stays in the detail, but every scale x scale block averages to the original texel again - the
// overall colours and contrast (Real-ESRGAN darkens mid-greys and stretches contrast) and the grain it smooths away
// come back, and the mip level at the original size is the original. Channels B, G, R, and A too when `alpha`.
export function matchOriginal(
    upscaled: Uint8Array,
    original: Uint8Array,
    w: number,
    h: number,
    scale: number,
    wrapU: boolean,
    wrapV: boolean,
    alpha: boolean,
): Buffer {
    const ow = w * scale;
    const oh = h * scale;
    const out = Buffer.from(upscaled);
    const channels = alpha ? 4 : 3;
    const diff = new Float32Array(w * h * 4);
    for (let pass = 0; pass < 2; pass++) {
        diff.fill(0);
        for (let y = 0; y < oh; y++)
            for (let x = 0; x < ow; x++) {
                const d = (Math.floor(y / scale) * w + Math.floor(x / scale)) * 4;
                const o = (y * ow + x) * 4;
                for (let k = 0; k < channels; k++) diff[d + k]! += out[o + k]!;
            }
        const n = scale * scale;
        for (let i = 0; i < w * h; i++)
            for (let k = 0; k < channels; k++) diff[i * 4 + k] = original[i * 4 + k]! - diff[i * 4 + k]! / n;
        for (let y = 0; y < oh; y++) {
            const fy = (y + 0.5) / scale - 0.5;
            const y0 = Math.floor(fy);
            const ty = fy - y0;
            const ya = wrapIndex(y0, h, wrapV) * w;
            const yb = wrapIndex(y0 + 1, h, wrapV) * w;
            for (let x = 0; x < ow; x++) {
                const fx = (x + 0.5) / scale - 0.5;
                const x0 = Math.floor(fx);
                const tx = fx - x0;
                const xa = wrapIndex(x0, w, wrapU);
                const xb = wrapIndex(x0 + 1, w, wrapU);
                const o = (y * ow + x) * 4;
                for (let k = 0; k < channels; k++) {
                    const v =
                        (diff[(ya + xa) * 4 + k]! * (1 - tx) + diff[(ya + xb) * 4 + k]! * tx) * (1 - ty) +
                        (diff[(yb + xa) * 4 + k]! * (1 - tx) + diff[(yb + xb) * 4 + k]! * tx) * ty;
                    out[o + k] = Math.max(0, Math.min(255, Math.round(out[o + k]! + v)));
                }
            }
        }
    }
    return out;
}

// The images one texture sends to the upscaler.
export function jobInputs(
    job: { bgra: Uint8Array; kind: AlphaKind; wrapU: boolean; wrapV: boolean },
    w: number,
    h: number,
): Map<string, Image> {
    const inputs = new Map<string, Image>();
    const colour = job.kind === "opaque" ? job.bgra : fillHidden(job.bgra, w, h, job.wrapU, job.wrapV);
    inputs.set("rgb", padded(colour, w, h, PAD, job.wrapU, job.wrapV, "rgb"));
    if (job.kind === "smooth") inputs.set("alpha", padded(job.bgra, w, h, PAD, job.wrapU, job.wrapV, "alpha"));
    return inputs;
}

// The upscaled BGRA texels from the upscaler's outputs.
export function jobResult(
    job: { bgra: Uint8Array; kind: AlphaKind; wrapU: boolean; wrapV: boolean; scale: number },
    w: number,
    h: number,
    outputs: Map<string, Image>,
): Buffer {
    const rgbOut = outputs.get("rgb");
    if (!rgbOut) throw new Error("no upscaled colour");
    const colour = cropAndShrink(rgbOut, w, h, PAD, MODEL_SCALE, job.scale);
    const ow = w * job.scale;
    const oh = h * job.scale;
    let alpha: Uint8Array | null = null;
    if (job.kind === "binary") alpha = binaryAlpha(job.bgra, w, h, job.scale, job.wrapU, job.wrapV);
    else if (job.kind === "smooth") {
        const a = outputs.get("alpha");
        if (!a) throw new Error("no upscaled alpha");
        const grey = cropAndShrink(a, w, h, PAD, MODEL_SCALE, job.scale);
        alpha = new Uint8Array(ow * oh);
        for (let i = 0; i < alpha.length; i++) alpha[i] = grey.rgb[i * 3]!;
    }
    const out = Buffer.alloc(ow * oh * 4);
    for (let i = 0; i < ow * oh; i++) {
        out[i * 4] = colour.rgb[i * 3 + 2]!;
        out[i * 4 + 1] = colour.rgb[i * 3 + 1]!;
        out[i * 4 + 2] = colour.rgb[i * 3]!;
        out[i * 4 + 3] = alpha ? alpha[i]! : 255;
    }
    return out;
}

// The texture rewritten at its new size: format kept, mip chain regenerated.
export function rebuild(t: Texture, bgra: Buffer, scale: number): void {
    const w = t.width * scale;
    const h = t.height * scale;
    const count =
        t.levels.length <= 1
            ? 1
            : Math.min(t.levels.length + Math.log2(scale), Math.floor(Math.log2(Math.min(w, h))) + 1);
    const levels: Buffer[] = [];
    let level = bgra;
    let lw = w;
    let lh = h;
    for (let n = 0; n < count; n++) {
        if (n > 0) {
            level = mipDown(level, lw, lh);
            lw = Math.max(1, Math.floor(lw / 2));
            lh = Math.max(1, Math.floor(lh / 2));
        }
        levels.push(t.dxt === 0 ? level : dxt.encode(level, lw, lh, t.dxt));
    }
    t.width = w;
    t.height = h;
    t.levels = levels;
}

function texBytes(tex: TexFile): number {
    return tex.textures.reduce((n, t) => n + t.levels.reduce((m, l) => m + l.length, 0), 0);
}

function skip(stats: HdStats, why: string): void {
    stats.skipped.set(why, (stats.skipped.get(why) ?? 0) + 1);
}

// The pass over one dictionary. `addressing`: by texture checksum (absent: wrapped both ways).
export function upscaleTexFile(
    tex: TexFile,
    addressing: ReadonlyMap<number, Addressing>,
    options: HdOptions,
    stats: HdStats,
    upscaler: Upscaler = realEsrgan,
): void {
    const start = performance.now();
    stats.bytesBefore += texBytes(tex);
    const jobs: Job[] = [];
    for (const t of tex.textures) {
        stats.textures += 1;
        if (t.palette.length || ![0, 1, 2, 5].includes(t.dxt) || (t.dxt === 0 && t.texelDepth !== 32)) {
            skip(stats, "format not handled");
            continue;
        }
        if (Math.max(t.width, t.height) < options.minSize) {
            skip(stats, `smaller than ${options.minSize}`);
            continue;
        }
        const scale = scaleFor(t.width, t.height, options);
        if (scale <= 1) {
            skip(stats, `at the ${options.maxSize} cap`);
            continue;
        }
        const bgra = topLevel(t);
        if (uniform(bgra)) {
            skip(stats, "one colour");
            continue;
        }
        const a = addressing.get(t.checksum) ?? { wrapU: true, wrapV: true };
        const kind = alphaKind(bgra);
        const partial = { texture: t, scale, bgra, kind, wrapU: a.wrapU, wrapV: a.wrapV };
        jobs.push({ ...partial, key: cacheKey(partial, options) });
    }
    const results = new Map<Job, Buffer>();
    const missing: Job[] = [];
    for (const job of jobs) {
        const hit = readCache(options, job.key, job.texture.width * job.scale, job.texture.height * job.scale);
        if (hit) {
            results.set(job, hit);
            stats.cacheHits += 1;
        } else missing.push(job);
    }
    if (missing.length) {
        const inputs = new Map<string, Image>();
        missing.forEach((job, n) => {
            for (const [part, img] of jobInputs(job, job.texture.width, job.texture.height))
                inputs.set(`${n}_${part}`, img);
        });
        const toolStart = performance.now();
        const outputs = upscaler(inputs, options);
        stats.toolSeconds += (performance.now() - toolStart) / 1000;
        missing.forEach((job, n) => {
            const mine = new Map<string, Image>();
            for (const part of ["rgb", "alpha"]) {
                const img = outputs.get(`${n}_${part}`);
                if (img) mine.set(part, img);
            }
            const bgra = jobResult(job, job.texture.width, job.texture.height, mine);
            writeCache(options, job.key, job.texture.width * job.scale, job.texture.height * job.scale, bgra);
            results.set(job, bgra);
            stats.generated += 1;
        });
    }
    for (const job of jobs) {
        // (the cache holds the model's own result: --hd-no-match on or off reuses it)
        let bgra = results.get(job)!;
        if (options.match) {
            const w = job.texture.width;
            const h = job.texture.height;
            const original = job.kind === "opaque" ? job.bgra : fillHidden(job.bgra, w, h, job.wrapU, job.wrapV);
            bgra = matchOriginal(bgra, original, w, h, job.scale, job.wrapU, job.wrapV, job.kind === "smooth");
        }
        rebuild(job.texture, bgra, job.scale);
        stats.upscaled += 1;
    }
    stats.bytesAfter += texBytes(tex);
    stats.seconds += (performance.now() - start) / 1000;
}

// Real-ESRGAN's ncnn / Vulkan build over a folder of PNGs (one run: the model loads once).
export function realEsrgan(images: Map<string, Image>, options: HdOptions): Map<string, Image> {
    if (!fs.existsSync(options.tool))
        throw new Error(`${options.tool} not found: fetch it with node tools/hd_textures/fetch.ts`);
    fs.mkdirSync(path.join(REPO, "out", "hd_textures"), { recursive: true });
    const work = fs.mkdtempSync(path.join(REPO, "out", "hd_textures", "work-"));
    try {
        const inDir = path.join(work, "in");
        const outDir = path.join(work, "out");
        fs.mkdirSync(inDir);
        fs.mkdirSync(outDir);
        for (const [name, img] of images) fs.writeFileSync(path.join(inDir, `${name}.png`), encodePng(img));
        const args = ["-i", inDir, "-o", outDir, "-n", options.model, "-s", String(MODEL_SCALE), "-f", "png"];
        args.push("-m", path.join(path.dirname(options.tool), "models"));
        if (options.gpu !== null) args.push("-g", options.gpu);
        const result = spawnSync(options.tool, args, { maxBuffer: 64 * 1024 * 1024 });
        const out = new Map<string, Image>();
        for (const name of images.keys()) {
            const file = path.join(outDir, `${name}.png`);
            if (!fs.existsSync(file)) {
                const log = `${result.stdout?.toString() ?? ""}${result.stderr?.toString() ?? ""}`
                    .split(/\r?\n/)
                    .slice(-10)
                    .join(os.EOL);
                throw new Error(`realesrgan-ncnn-vulkan made no ${name}.png (exit ${result.status}):${os.EOL}${log}`);
            }
            out.set(name, decodePng(fs.readFileSync(file)));
        }
        return out;
    } finally {
        fs.rmSync(work, { recursive: true, force: true });
    }
}

export function report(stats: HdStats, options: HdOptions): string[] {
    const mb = (n: number): string => (n / 1048576).toFixed(1);
    const skipped = [...stats.skipped].map(([why, n]) => `${n} ${why}`).join(", ");
    return [
        `HD textures (${options.model}, x${options.scale}, cap ${options.maxSize}): ${stats.upscaled} of ${stats.textures} upscaled ` +
            `(${stats.cacheHits} from the cache, ${stats.generated} made)${skipped ? `; left as they were: ${skipped}` : ""}`,
        `  texture data ${mb(stats.bytesBefore)} MB -> ${mb(stats.bytesAfter)} MB; ${stats.seconds.toFixed(1)} s ` +
            `(upscaler ${stats.toolSeconds.toFixed(1)} s); cache ${options.cacheDir}`,
    ];
}
