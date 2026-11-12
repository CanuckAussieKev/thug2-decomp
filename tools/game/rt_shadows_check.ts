// NOT ORIGINAL - ray-traced sun shadows checked in the game ([Graphics] rt_shadows, src/gpu_vulkan/vulkan_rt_shadows.h):
// the same spots captured with the shadows off and on (and, optionally, with another build - main's - as a baseline),
// and the pictures compared outside each configuration's run-to-run noise.
//
//   node tools/game/rt_shadows_check.ts run --game C:\Games\THUG2-<n> --out <folder> --build <folder>
//        [--baseline-build <folder>] [--plan <plan.json>] [--groups a,b] [--runs 2] [--owner <name>]
//        [--set Section.key=value]...
//   node tools/game/rt_shadows_check.ts compare <folder> [--expect shadows|drawn|none|brighter] [--strength 0.5] [--threshold 8]
//        [--grow 1]
//   --effect ao (both commands): ray-traced ambient occlusion ([Graphics] rt_ao, vulkan_rt_effects.h) instead - the
//   configurations' rt_ao 0 / 1 (rt_shadows 0 in both), its own log lines and its own checks (below). `--expect drawn`
//   is the same as `--expect shadows`: the effect is drawn.
//   --effect reflections (both commands): ray-traced reflections ([Graphics] rt_reflections, vulkan_rt_reflections.h) -
//   rt_reflections 0 / 1 (rt_shadows and rt_ao 0 in both), with its own checks (below).
//   --effect hdr (both commands): HDR rendering and tone mapping ([Graphics] rt_hdr, vulkan_hdr.h) - rt_hdr 0 / 1, the
//   rest of the ini as the workspace has it (ray tracing too, so the ray-traced effects can be checked on float
//   targets with --set); --set Graphics.rt_exposure=... reaches both configurations but only `on` uses it.
//   `--expect drawn` (the default): with the defaults the picture matches; `--expect brighter`: rt_exposure raised.
//   --effect fog (both commands): volumetric fog and light shafts ([Graphics] rt_fog, vulkan_rt_fog.h) - rt_fog 0 / 1
//   (rt_shadows, rt_ao and rt_reflections 0 in both; rt_fog_mode etc. from --set), with its own checks (below).
//   --effect gi (both commands): one-bounce indirect light ([Graphics] rt_gi, vulkan_rt_effects.h) - rt_gi 0 / 1
//   (rt_shadows, rt_ao and rt_reflections 0 in both), with its own checks (below).
//   --effect sun (both commands): the sun's disc and glare, the screen-space rays, the bloom and the sky model
//   ([Graphics] rt_sun, rt_shafts, rt_bloom, rt_sky_model - vulkan_rt_sun.h, vulkan_rt_sky.h) - all four 0 / 1 together;
//   the other effects as the workspace's ini and --set have them (the user's look: --set Graphics.rt_shadows=1 ...).
//
// run: under the workspace's lock (game_lock.ps1, released at the end - which puts the folder's own build and ini back),
// the x64 build deployed (deploy_build.ps1 -Arch x64: the exe and every DLL beside it), then for each run and each
// configuration the ini written (the workspace's own + renderer=vulkan + the configuration's values + --set's) and each
// level group of the plan run (run_game.ps1 -Level, the automation channel's teleport / camera / capture 1 per spot),
// its pictures gathered into <out>/<config>/run<k>/<spot>.png and its log into <out>/<config>/run<k>/<group>.log:
//   off       ray_tracing=1, rt_shadows=0
//   on        ray_tracing=1, rt_shadows=1 (strength, softness... from --set)
//   baseline  --baseline-build's exe with the ini as the workspace has it (no ray tracing): only with --baseline-build
// Workspaces only; muted (workspace_ini.ts). The plan: rt_shadows.json beside this file (renderer_parity.ts's format; its
// level groups' spots are used, their commands and frame times aren't).
//
// compare (run does it at the end): per spot, the pixels outside every configuration's noise (any two of its runs
// differing by more than --threshold in a channel, grown by --grow), run 1 of each compared:
//   --expect shadows (after the TLAS's builder and the frame record are merged): on against off
//     - brighter: pixels where on is lighter than off by more than the threshold - at most 0.1% (the pass only darkens);
//     - darkened: pixels darker by more than the threshold - at most 80% of the picture (a spot can stand in the bake's
//       own shadow: z_ho's start), and at least 0.5% in one spot or more (shadows are drawn somewhere);
//     - the darkened pixels' median brightness ratio (on / off) from 1 - strength - 0.2 to 0.9, where 1% of the picture
//       or more is darkened (1 - strength is a full shadow; the sky test and the bake's agreement let in shares of one);
//     - the sun lines up with the bake: over every spot, the pixels the level lit darker in `off` (its luma under the
//       picture's Otsu threshold: baked-dark) darkened at least 1.5 times as often as the ones it lit brighter
//       (baked-bright) - a sun the bake wasn't made with darkens both alike;
//     - a level whose bake shows no sun (its `on` log: "rt: the baked sun: none" - indoors) changed in at most 0.1%;
//     - reported, not checked: the share of the darkened pixels in the picture's lower half (the ground, mostly);
//     - the logs: "rt_shadows: the first shadow pass" in every `on` group's (or the baked sun's "none"), no "skipped" /
//       "couldn't" line of the pass, no crash line.
//   --expect none (before them: no scene to trace - the pass is skipped): on against off differ in at most 0.1% of the
//     pixels, and the `on` logs say the pass was skipped.
//   baseline (when run): off against baseline differ in at most 0.1% of the pixels.
//   --effect ao --expect drawn (with the TLAS's builder): on against off
//     - brighter: at most 0.1% (the pass only darkens);
//     - darkened: at most 60% of the picture, and at least 0.5% in one spot or more (occlusion is drawn somewhere);
//     - too dark: pixels whose ratio (on / off) is below 1 - strength - 0.05 (what full occlusion leaves; a NaN or a
//       runaway value shows as black) - at most 0.1%;
//     - the darkened pixels' median ratio no lower than 1 - strength - 0.2 (occlusion is mostly partial);
//     - reported: the darkened share in the lower half (corners, the ground under ledges - look at the pictures);
//     - the logs: "rt_ao: the first ambient occlusion pass" in every `on` group's, no skipped line, no crash line.
//   --effect reflections --expect drawn: on against off, per spot by what its group's `on` log says
//     - the group drew reflections ("rt_reflections: the first reflection pass"): changed (lighter or darker) at most
//       60% of the picture; turned black (every channel 4 or less where off isn't: a NaN, a missing texture) at most
//       0.1%;
//     - the group had nothing reflective ("nothing reflective in the scene"): changed at most 0.1%, as when skipped;
//     - at least one group drew them, and some spot changed by 0.1% or more; no "couldn't" / "can't" line, no crash.
//   --effect hdr --expect drawn (the defaults: the curve is the identity below its knee, ~0.91): on against off
//     - at most 0.1% of the pixels with a channel changed by more than 12 (the shoulder takes the original's white to
//       ~247; float blending's rounding a step or two; a material pass above 1 no longer clipped before the next
//       multiplies it can do more, in a few pixels), the mean at most 1.0 - the largest and the mean reported per spot;
//   --effect hdr --expect brighter (rt_exposure above 0): on against off
//     - darkened: at most 0.1% (more light never darkens: the curve rises everywhere); turned black at most 0.1%;
//     - the mean change upward somewhere (at least one spot +2 or more);
//     - reported: the share of pure-white pixels (all channels 255) in each - a clipped look shows as a jump there;
//       checked: on's share no more than off's + 10 points;
//   both: the `on` logs show "rt_hdr: the first tone-mapping pass" and no "couldn't" line, `off` logs no pass; the
//     frame times ("ms a frame" in the Vulkan lines of each configuration's logs) averaged and reported.
//   --effect fog --expect drawn: on against off
//     - bounded changes: the picture's mean luma change at most 40 (of 255) - the share of pixels changed is reported,
//       not limited (the haze looking toward the sun touches nearly all of a picture, a little);
//     - turned black (a NaN, garbage): at most 0.1%; turned white (every channel 250 or more where off isn't: the sky or
//       the fog blown out) at most 0.5%;
//     - the logs: "rt_fog: the first fog pass" (or "the level has no fog") in every `on` group's, at least one group
//       with a pass, no "couldn't" / "can't" line, no crash; the device time of the passes ("rt_fog: the passes take")
//       reported.
//   --effect gi --expect drawn: on against off
//     - darkened: at most 0.1% (the pass only adds light); turned black at most 0.1% (a NaN, garbage);
//     - brighter: at most 80% of the picture, and at least 0.5% in one spot or more (bounce is drawn somewhere);
//     - bounded: pixels lighter by more than 112 in luma at most 0.1% (a runaway value);
//     - a level whose bake shows no sun ("rt_gi: the level's bake shows no sun" in its `on` log): changed at most 0.1%
//       (the bounce adds only light beyond the sky's - with no sun there is none);
//     - more in the shade: over every spot, the pixels the level lit darker in `off` (baked-dark, as for the shadows)
//       brightened more often than the ones it lit brighter;
//     - the logs: "rt_gi: the first indirect light pass" in every `on` group's, no skipped line, no crash line.
//   --effect sun --expect drawn: on against off
//     - bounded: the picture's mean luma change at most 45; turned black (a NaN, garbage) at most 0.1%; turned white
//       (every channel 250 or more where off isn't: the disc and its glare, a blow-out) at most 4%;
//     - drawn: some spot brightened by 0.5% or more;
//     - the logs: "rt_sun: the first pass" (or "no sun in this level") in every `on` group's, no "couldn't" / "can't"
//       line of rt_sun / rt_sky, no crash; where the disc was drawn, render_pc's "sun: the original's sun and lens flare
//       left out" (the original's halo sprite and lens flare gone); in `off` logs neither; the device time of the parts
//       ("rt_sun: the passes take") reported.
// report.txt / report.json in the folder; exit 1 when a check fails.

import { spawn } from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { exitWith, isMain, parseCli, print, runMain } from "thug2-level-converter-core/lib/cli.ts";
import type { Image } from "thug2-level-converter-core/lib/frame_diff.ts";
import { clearAutomationFiles } from "./edit_level.ts";
import {
    groupCommands,
    groupSpots,
    growMask,
    iniFor,
    noiseMask,
    type PlanGroup,
    readPicture,
    readPlan,
    seriesName,
} from "./renderer_parity.ts";
import { ensureWorkspaceMuted, isWorkspace } from "./workspace_ini.ts";

const USAGE = `usage: rt_shadows_check.ts run --game <workspace> --out <folder> --build <folder> [--baseline-build <folder>]
                           [--effect shadows|ao|reflections|hdr|fog|gi|sun] [--plan <plan.json>] [--groups a,b] [--runs N] [--owner name] [--set S.key=v]...
       rt_shadows_check.ts compare <folder> [--expect shadows|drawn|none|brighter] [--strength 0.5] [--threshold 8] [--grow 1]

Ray-traced sun shadows in the game: the same spots with rt_shadows off and on, compared (tools/game/README.md).`;

const HERE = path.dirname(fileURLToPath(import.meta.url));
export const CONFIGS = ["off", "on", "baseline"] as const;
export type Config = (typeof CONFIGS)[number];
// R4's sun shadows, R5's ambient occlusion and reflections, HDR, the fog, the one-bounce indirect light
export type Effect = "shadows" | "ao" | "reflections" | "hdr" | "fog" | "gi" | "sun";
export const EFFECTS: readonly Effect[] = ["shadows", "ao", "reflections", "hdr", "fog", "gi", "sun"];

// The ini values a configuration adds.
export function configValues(config: Config, effect: Effect = "shadows"): Record<string, string> {
    if (effect === "sun") {
        // (the sun's parts together; the other effects as the workspace's ini and --set have them - the pictures are
        // taken with everything else on)
        if (config === "baseline") return {};
        const on = config === "on" ? "1" : "0";
        return {
            "Graphics.ray_tracing": "1",
            "Graphics.rt_sun": on,
            "Graphics.rt_bloom": on,
            "Graphics.rt_shafts": on,
            "Graphics.rt_sky_model": on,
        };
    }
    if (effect === "hdr") {
        if (config === "baseline") return {};
        return { "Graphics.rt_hdr": config === "on" ? "1" : "0" };
    }
    if (effect === "gi") {
        if (config === "baseline") return {};
        return {
            "Graphics.ray_tracing": "1",
            "Graphics.rt_shadows": "0",
            "Graphics.rt_ao": "0",
            "Graphics.rt_reflections": "0",
            "Graphics.rt_gi": config === "on" ? "1" : "0",
        };
    }
    if (effect === "fog") {
        if (config === "baseline") return {};
        return {
            "Graphics.ray_tracing": "1",
            "Graphics.rt_shadows": "0",
            "Graphics.rt_ao": "0",
            "Graphics.rt_reflections": "0",
            "Graphics.rt_fog": config === "on" ? "1" : "0",
        };
    }
    if (effect === "reflections") {
        if (config === "baseline") return {};
        return {
            "Graphics.ray_tracing": "1",
            "Graphics.rt_shadows": "0",
            "Graphics.rt_ao": "0",
            "Graphics.rt_reflections": config === "on" ? "1" : "0",
        };
    }
    if (effect === "ao") {
        if (config === "baseline") return {};
        return {
            "Graphics.ray_tracing": "1",
            "Graphics.rt_shadows": "0",
            "Graphics.rt_ao": config === "on" ? "1" : "0",
        };
    }
    if (config === "off") return { "Graphics.ray_tracing": "1", "Graphics.rt_shadows": "0" };
    if (config === "on") return { "Graphics.ray_tracing": "1", "Graphics.rt_shadows": "1" };
    return {};
}

// --- comparing ---------------------------------------------------------------------------------------------------------

const luma = (rgb: Uint8Array, p: number): number =>
    0.299 * rgb[p * 3]! + 0.587 * rgb[p * 3 + 1]! + 0.114 * rgb[p * 3 + 2]!;

export interface ShadowDifference {
    pixels: number; // compared (outside the mask)
    brighter: number; // pixels where `on` is lighter by more than the threshold in a channel
    darkened: number; // darker by more than the threshold in a channel (and no channel lighter)
    lowerHalf: number; // of the darkened, those in the picture's lower half
    medianRatio: number | null; // the darkened pixels' median luma ratio on / off
    tooDark: number; // of the darkened, those whose ratio is below `floor`
    blackened: number; // pixels black in `on` (every channel BLACK or less) and not in `off`
    whitened: number; // pixels white in `on` (every channel WHITE or more) and not in `off`
    meanLumaChange: number; // the mean |luma on - luma off| over the compared pixels (0..255)
    // The `off` picture's pixels split into baked-bright and baked-dark at its luma's Otsu threshold (the level's own
    // lighting: sunlit against shaded); how many of each the pass darkened. A sun that matches the bake darkens mostly
    // what is already dark - its shadows fall on the baked ones.
    split: number; // the threshold (luma, 0..255)
    bright: number;
    brightDarkened: number;
    dark: number;
    darkDarkened: number;
    maxDelta: number; // the largest channel difference (either way)
    overDelta: number; // pixels with a channel changed by more than HDR_MAX_DELTA
    meanDelta: number; // the mean of the channels' absolute differences
    meanSigned: number; // the mean of the channels' differences, on - off
    whiteOff: number; // pixels pure white (every channel 255) in `off`
    whiteOn: number; // and in `on`
    // (the indirect light, which only adds) how many of each the pass brightened, and those brightened by more than
    // RUNAWAY_GAIN in luma
    brightBrightened: number;
    darkBrightened: number;
    runaway: number;
}

// A pixel lighter than this in luma with the indirect light on: more than the bounce can add (a runaway value).
export const RUNAWAY_GAIN = 112;

// The luma threshold that best splits a histogram in two (Otsu's: the largest between-class variance).
export function otsuThreshold(histogram: readonly number[]): number {
    let total = 0;
    let sum = 0;
    for (let i = 0; i < histogram.length; i++) {
        total += histogram[i]!;
        sum += i * histogram[i]!;
    }
    let below = 0;
    let belowSum = 0;
    let best = 0;
    let bestVariance = -1;
    for (let t = 0; t < histogram.length; t++) {
        below += histogram[t]!;
        if (below === 0) continue;
        const above = total - below;
        if (above === 0) break;
        belowSum += t * histogram[t]!;
        const meanBelow = belowSum / below;
        const meanAbove = (sum - belowSum) / above;
        const variance = below * above * (meanBelow - meanAbove) ** 2;
        if (variance > bestVariance) {
            bestVariance = variance;
            best = t;
        }
    }
    return best;
}

const BLACK = 4;
const WHITE = 250;

// `on` against `off` outside the mask (`floor`: the lowest ratio the pass can give; 0 counts none too dark).
export function shadowDifference(
    off: Image,
    on: Image,
    mask: Uint8Array | null,
    threshold: number,
    floor = 0,
): ShadowDifference {
    if (off.width !== on.width || off.height !== on.height)
        throw new Error(`sizes differ: ${off.width}x${off.height} / ${on.width}x${on.height}`);
    const { width, height } = off;
    let pixels = 0;
    let brighter = 0;
    let darkened = 0;
    let lowerHalf = 0;
    let tooDark = 0;
    let blackened = 0;
    let maxDelta = 0;
    let overDelta = 0;
    let sumDelta = 0;
    let sumSigned = 0;
    let whiteOff = 0;
    let whiteOn = 0;
    let whitened = 0;
    let lumaChange = 0;
    const ratios: number[] = [];
    const histogram = new Array<number>(256).fill(0);
    for (let p = 0; p < width * height; p++) if (!mask?.[p]) histogram[Math.min(255, Math.round(luma(off.rgb, p)))]!++;
    const split = otsuThreshold(histogram);
    let bright = 0;
    let brightDarkened = 0;
    let dark = 0;
    let darkDarkened = 0;
    let brightBrightened = 0;
    let darkBrightened = 0;
    let runaway = 0;
    for (let y = 0; y < height; y++)
        for (let x = 0; x < width; x++) {
            const p = y * width + x;
            if (mask?.[p]) continue;
            pixels++;
            const black = (rgb: Uint8Array): boolean =>
                rgb[p * 3]! <= BLACK && rgb[p * 3 + 1]! <= BLACK && rgb[p * 3 + 2]! <= BLACK;
            if (black(on.rgb) && !black(off.rgb)) blackened++;
            const white = (rgb: Uint8Array): boolean =>
                rgb[p * 3] === 255 && rgb[p * 3 + 1] === 255 && rgb[p * 3 + 2] === 255;
            if (white(off.rgb)) whiteOff++;
            if (white(on.rgb)) whiteOn++;
            const nearWhite = (rgb: Uint8Array): boolean =>
                rgb[p * 3]! >= WHITE && rgb[p * 3 + 1]! >= WHITE && rgb[p * 3 + 2]! >= WHITE;
            if (nearWhite(on.rgb) && !nearWhite(off.rgb)) whitened++;
            lumaChange += Math.abs(luma(on.rgb, p) - luma(off.rgb, p));
            let up = 0;
            let down = 0;
            for (let c = 0; c < 3; c++) {
                const delta = on.rgb[p * 3 + c]! - off.rgb[p * 3 + c]!;
                up = Math.max(up, delta);
                down = Math.max(down, -delta);
                sumDelta += Math.abs(delta);
                sumSigned += delta;
            }
            maxDelta = Math.max(maxDelta, up, down);
            if (Math.max(up, down) > HDR_MAX_DELTA) overDelta++;
            const isBright = Math.round(luma(off.rgb, p)) > split;
            if (isBright) bright++;
            else dark++;
            if (up > threshold) {
                brighter++;
                if (isBright) brightBrightened++;
                else darkBrightened++;
                if (luma(on.rgb, p) - luma(off.rgb, p) > RUNAWAY_GAIN) runaway++;
            } else if (down > threshold) {
                darkened++;
                if (isBright) brightDarkened++;
                else darkDarkened++;
                if (y >= height / 2) lowerHalf++;
                const before = luma(off.rgb, p);
                if (before > 0) {
                    const ratio = luma(on.rgb, p) / before;
                    ratios.push(ratio);
                    if (ratio < floor) tooDark++;
                }
            }
        }
    ratios.sort((a, b) => a - b);
    return {
        pixels,
        brighter,
        darkened,
        lowerHalf,
        medianRatio: ratios.length ? ratios[Math.floor(ratios.length / 2)]! : null,
        tooDark,
        blackened,
        whitened,
        meanLumaChange: pixels ? lumaChange / pixels : 0,
        split,
        bright,
        brightDarkened,
        dark,
        darkDarkened,
        maxDelta,
        overDelta,
        meanDelta: sumDelta / Math.max(1, pixels * 3),
        meanSigned: sumSigned / Math.max(1, pixels * 3),
        whiteOff,
        whiteOn,
        brightBrightened,
        darkBrightened,
        runaway,
    };
}

// The union of the configurations' noise masks, grown.
export function noiseOf(pictures: readonly Image[][], threshold: number, grow: number): Uint8Array {
    const first = pictures[0]![0]!;
    const union = new Uint8Array(first.width * first.height);
    for (const images of pictures) {
        if (images.length < 2) continue;
        const mask = noiseMask(images, threshold);
        for (let p = 0; p < mask.length; p++) if (mask[p]) union[p] = 1;
    }
    return growMask(union, first.width, first.height, grow);
}

export interface Limits {
    expect: "shadows" | "none" | "brighter"; // (brighter: HDR's raised exposure)
    strength: number;
    maxBrighter: number; // shares of the compared pixels
    maxDarkened: number;
    minDarkenedSomewhere: number;
    ratioSlack: number;
    maxRatio: number; // the median ratio at most this ...
    minRatioShare: number; // ... both checked where at least this share is darkened
    minDarkOverBright: number; // over every spot: baked-dark darkened / baked-bright darkened, at least
    maxChanged: number; // --expect none and the baseline
    effect?: Effect; // (shadows when not given)
}

// Below this ratio a pixel is darker than the effect can make it - ambient occlusion: full occlusion's 1 - strength,
// give or take 0.05; 0 (not checked) for the shadows.
export const TOO_DARK_SLACK = 0.05;
export function darkestRatio(limits: Limits): number {
    return limits.effect === "ao" ? Math.max(0, 1 - limits.strength - TOO_DARK_SLACK) : 0;
}

// HDR with its defaults against the 8-bit picture: the shoulder's largest change (white 255 -> ~247, and a step or two of
// float blending's rounding) and the mean change; with the exposure raised, the most pure white may grow (a share of the
// picture) and the least the brightest spot's mean must rise.
export const HDR_MAX_DELTA = 12;
export const HDR_MAX_MEAN = 1.0;
export const HDR_MORE_WHITE = 0.1;
export const HDR_LEAST_BRIGHTER = 2;

export const LIMITS: Omit<Limits, "expect" | "strength"> = {
    maxBrighter: 0.001,
    maxDarkened: 0.8,
    minDarkenedSomewhere: 0.005,
    ratioSlack: 0.2,
    maxRatio: 0.9,
    minRatioShare: 0.01,
    minDarkOverBright: 1.5,
    maxChanged: 0.001,
};

export interface SpotVerdict {
    name: string;
    difference: ShadowDifference;
    baseline: ShadowDifference | null; // off against the baseline (brighter + darkened = changed)
    problems: string[];
}

const share = (count: number, d: ShadowDifference): number => count / Math.max(1, d.pixels);
const pct = (v: number): string => `${(100 * v).toFixed(2)}%`;

// One spot's verdict (the "somewhere" check is the folder's: anyShadow).
export function judgeSpot(
    name: string,
    difference: ShadowDifference,
    baseline: ShadowDifference | null,
    limits: Limits,
    sunless = false, // the level's bake shows no sun: no pass there
): SpotVerdict {
    const problems: string[] = [];
    if (limits.effect === "hdr") {
        if (limits.expect === "brighter") {
            if (share(difference.darkened, difference) > limits.maxBrighter)
                problems.push(`darker with the exposure raised: ${pct(share(difference.darkened, difference))}`);
            if (share(difference.blackened, difference) > limits.maxBrighter)
                problems.push(`turned black: ${pct(share(difference.blackened, difference))}`);
            const moreWhite = share(difference.whiteOn - difference.whiteOff, difference);
            if (moreWhite > HDR_MORE_WHITE) problems.push(`pure white grew by ${pct(moreWhite)} of the picture`);
        } else {
            // (a few pixels may differ more: a pass of a material that went above 1 is no longer clipped before the
            // next pass multiplies it - the float target's own behaviour, not the curve's)
            if (share(difference.overDelta, difference) > limits.maxChanged)
                problems.push(
                    `${pct(share(difference.overDelta, difference))} of the pixels changed by more than ${HDR_MAX_DELTA} (largest ${difference.maxDelta})`,
                );
            if (difference.meanDelta > HDR_MAX_MEAN)
                problems.push(`the mean change ${difference.meanDelta.toFixed(2)} (at most ${HDR_MAX_MEAN})`);
        }
    } else if (limits.expect === "shadows" && limits.effect === "sun") {
        // (the sun, its glare and the bloom: a bright patch where the sun is, a little light everywhere - bounded)
        if (difference.meanLumaChange > MAX_SUN_LUMA_CHANGE)
            problems.push(`the sun changed the picture's luma by ${difference.meanLumaChange.toFixed(1)} on average`);
        if (share(difference.blackened, difference) > limits.maxBrighter)
            problems.push(`turned black with the sun on: ${pct(share(difference.blackened, difference))}`);
        if (share(difference.whitened, difference) > MAX_SUN_WHITENED)
            problems.push(`turned white with the sun on: ${pct(share(difference.whitened, difference))}`);
    } else if (limits.expect === "shadows" && limits.effect === "fog") {
        // (the fog may touch the whole picture - the haze looking toward the sun -, but not by much on average)
        if (difference.meanLumaChange > MAX_FOG_LUMA_CHANGE)
            problems.push(`the fog changed the picture's luma by ${difference.meanLumaChange.toFixed(1)} on average`);
        if (share(difference.blackened, difference) > limits.maxBrighter)
            problems.push(`turned black with the fog on: ${pct(share(difference.blackened, difference))}`);
        if (share(difference.whitened, difference) > MAX_WHITENED)
            problems.push(`turned white with the fog on: ${pct(share(difference.whitened, difference))}`);
    } else if (limits.expect === "shadows" && limits.effect === "reflections") {
        const changed = share(difference.brighter + difference.darkened, difference);
        if (changed > limits.maxDarkened)
            problems.push(`changed with the reflections on: ${pct(changed)} of the picture`);
        if (share(difference.blackened, difference) > limits.maxBrighter)
            problems.push(`turned black with the reflections on: ${pct(share(difference.blackened, difference))}`);
    } else if (limits.expect === "shadows" && limits.effect === "ao") {
        if (share(difference.brighter, difference) > limits.maxBrighter)
            problems.push(`brighter with the occlusion on: ${pct(share(difference.brighter, difference))}`);
        if (share(difference.darkened, difference) > limits.maxDarkened)
            problems.push(`darkened: ${pct(share(difference.darkened, difference))} of the picture`);
        if (share(difference.tooDark, difference) > limits.maxBrighter)
            problems.push(`darker than full occlusion leaves: ${pct(share(difference.tooDark, difference))}`);
        const lowest = 1 - limits.strength - limits.ratioSlack;
        if (difference.medianRatio !== null && difference.medianRatio < lowest)
            problems.push(
                `the occlusion's median ratio ${difference.medianRatio.toFixed(3)}, at least ${lowest.toFixed(3)} wanted`,
            );
    } else if (limits.expect === "shadows" && limits.effect === "gi" && !sunless) {
        if (share(difference.darkened, difference) > limits.maxBrighter)
            problems.push(`darker with the indirect light on: ${pct(share(difference.darkened, difference))}`);
        if (share(difference.blackened, difference) > limits.maxBrighter)
            problems.push(`turned black with the indirect light on: ${pct(share(difference.blackened, difference))}`);
        if (share(difference.brighter, difference) > limits.maxDarkened)
            problems.push(`brightened: ${pct(share(difference.brighter, difference))} of the picture`);
        if (share(difference.runaway, difference) > limits.maxBrighter)
            problems.push(
                `lighter by more than ${RUNAWAY_GAIN} in luma: ${pct(share(difference.runaway, difference))}`,
            );
    } else if (limits.expect === "shadows" && sunless) {
        const changed = share(difference.brighter + difference.darkened, difference);
        if (changed > limits.maxChanged) problems.push(`changed in a level with no baked sun: ${pct(changed)}`);
    } else if (limits.expect === "shadows") {
        if (share(difference.brighter, difference) > limits.maxBrighter)
            problems.push(`brighter with the shadows on: ${pct(share(difference.brighter, difference))}`);
        if (share(difference.darkened, difference) > limits.maxDarkened)
            problems.push(`darkened: ${pct(share(difference.darkened, difference))} of the picture`);
        const full = 1 - limits.strength;
        const ratio = difference.medianRatio;
        if (
            ratio !== null &&
            share(difference.darkened, difference) >= limits.minRatioShare &&
            (ratio < full - limits.ratioSlack || ratio > limits.maxRatio)
        )
            problems.push(
                `the shadows' median ratio ${ratio.toFixed(3)}, ${(full - limits.ratioSlack).toFixed(3)} - ${limits.maxRatio} wanted`,
            );
    } else {
        const changed = share(difference.brighter + difference.darkened, difference);
        if (changed > limits.maxChanged) problems.push(`changed with the pass skipped: ${pct(changed)}`);
    }
    if (baseline) {
        const changed = share(baseline.brighter + baseline.darkened, baseline);
        if (changed > limits.maxChanged) problems.push(`off differs from the baseline: ${pct(changed)}`);
    }
    return { name, difference, baseline, problems };
}

// Over every spot: how many times as often the baked-dark pixels were darkened as the baked-bright ones (null: none
// darkened).
export function darkOverBright(verdicts: readonly SpotVerdict[]): number | null {
    let bright = 0;
    let brightDarkened = 0;
    let dark = 0;
    let darkDarkened = 0;
    for (const { difference: d } of verdicts) {
        bright += d.bright;
        brightDarkened += d.brightDarkened;
        dark += d.dark;
        darkDarkened += d.darkDarkened;
    }
    if (!brightDarkened && !darkDarkened) return null;
    return darkDarkened / Math.max(1, dark) / Math.max(1e-9, brightDarkened / Math.max(1, bright));
}

// The indirect light: over every spot, how many times as often the baked-dark pixels were brightened as the
// baked-bright ones (null: none brightened). More than 1: the bounce lands mostly in the shade.
export function darkOverBrightBrightened(verdicts: readonly SpotVerdict[]): number | null {
    let bright = 0;
    let brightBrightened = 0;
    let dark = 0;
    let darkBrightened = 0;
    for (const { difference: d } of verdicts) {
        bright += d.bright;
        brightBrightened += d.brightBrightened;
        dark += d.dark;
        darkBrightened += d.darkBrightened;
    }
    if (!brightBrightened && !darkBrightened) return null;
    return darkBrightened / Math.max(1, dark) / Math.max(1e-9, brightBrightened / Math.max(1, bright));
}
export const GI_MIN_DARK_OVER_BRIGHT = 1.0;

export function anyShadow(verdicts: readonly SpotVerdict[], limits: Limits): boolean {
    if (limits.effect === "hdr")
        return limits.expect !== "brighter" || verdicts.some((v) => v.difference.meanSigned >= HDR_LEAST_BRIGHTER);
    if (limits.effect === "gi" || limits.effect === "sun")
        return verdicts.some((v) => share(v.difference.brighter, v.difference) >= limits.minDarkenedSomewhere);
    if (limits.effect === "reflections")
        return verdicts.some(
            (v) => share(v.difference.brighter + v.difference.darkened, v.difference) >= MIN_REFLECTED_SOMEWHERE,
        );
    return verdicts.some((v) => share(v.difference.darkened, v.difference) >= limits.minDarkenedSomewhere);
}
// (reflective surfaces are a small part of most pictures)
export const MIN_REFLECTED_SOMEWHERE = 0.001;
// (the fog: the share of a picture that may turn white - the sun's glow against a white sky)
export const MAX_WHITENED = 0.005;
// (the fog: the mean luma change a picture may get, 0..255)
export const MAX_FOG_LUMA_CHANGE = 40;
// (the sun: the disc and the glare round it are white - a few percent of a picture facing the sun; the haze and the
// bloom change the rest a little)
export const MAX_SUN_WHITENED = 0.04;
export const MAX_SUN_LUMA_CHANGE = 45;

// What a configuration's logs must (not) say.
const FIRST_PASS = "rt_shadows: the first shadow pass";
// a level whose bake shows no sun (indoors: src/engine/gfx/rt_sun.h) has no shadow pass - by design
const NO_BAKED_SUN = "rt: the baked sun: none";
const PASS_SKIPPED = /rt_shadows: .*(skipped|couldn't)/;
const FIRST_AO_PASS = "rt_ao: the first ambient occlusion pass";
const AO_SKIPPED = /rt_ao: .*(skipped|couldn't)/;
export const FIRST_GI_PASS = "rt_gi: the first indirect light pass";
const GI_SKIPPED = /rt_gi: .*(skipped|couldn't|isn't enabled)/;
export const FIRST_REFLECTION_PASS = "rt_reflections: the first reflection pass";
export const NOTHING_REFLECTIVE = "rt_reflections: nothing reflective in the scene";
const REFLECTIONS_FAILED = /rt_reflections: .*(couldn't|can't|isn't enabled)/;
export const FIRST_TONE_MAP_PASS = "rt_hdr: the first tone-mapping pass";
const HDR_FAILED = /rt_hdr: .*(couldn't|can't)/;
export const FIRST_FOG_PASS = "rt_fog: the first fog pass";
export const NO_LEVEL_FOG = "rt_fog: the level has no fog";
const FOG_FAILED = /rt_fog: .*(couldn't|can't|isn't enabled)/;
export const FOG_TIMING = /rt_fog: the passes take ([0-9.]+) ms on the device \(([0-9]+)x([0-9]+), ([0-9]+) steps/g;
export const FIRST_SUN_PASS = "rt_sun: the first pass";
export const NO_SUN = "rt_sun: no sun in this level";
// (render_pc: the original's sun sprite, corona and lens flare left out while the backend draws the sun)
export const GLARE_REPLACED = "sun: the original's sun and lens flare left out";
const SUN_DISC_DRAWN = "the sun's disc and glare";
const SUN_FAILED = /rt_(sun|sky): .*(couldn't|can't)/;
export const SUN_TIMING =
    /rt_sun: the passes take ([0-9.]+) ms on the device - rays ([0-9.]+), source ([0-9.]+), bloom ([0-9.]+), composite ([0-9.]+) \(([0-9]+)x([0-9]+)/g;
const BAD = /^(EXCEPTION|RUN-TIME CHECK|DEBUG CRT|PURE VIRTUAL|INVALID PARAMETER|ABORT|BUFFER OVERRUN)/m;

export function logProblems(
    config: Config,
    log: string,
    expect: Limits["expect"],
    effect: Effect = "shadows",
): string[] {
    const problems: string[] = [];
    const bad = BAD.exec(log);
    if (bad) problems.push(`log: ${bad[0]}`);
    if (effect === "hdr") {
        if (config === "on") {
            const failed = HDR_FAILED.exec(log);
            if (failed) problems.push(`log: ${failed[0]}`);
            if (!log.includes(FIRST_TONE_MAP_PASS)) problems.push("no tone-mapping pass in the log");
        } else if (log.includes(FIRST_TONE_MAP_PASS)) {
            problems.push(`a tone-mapping pass in the ${config} log`);
        }
        return problems;
    }
    if (effect === "sun") {
        if (config !== "on") {
            if (log.includes(FIRST_SUN_PASS)) problems.push(`a sun pass in the ${config} log`);
            if (log.includes(GLARE_REPLACED)) problems.push(`the original's sun left out in the ${config} log`);
            return problems;
        }
        const failed = SUN_FAILED.exec(log);
        if (failed) problems.push(`log: ${failed[0]}`);
        if (expect === "shadows" && !log.includes(FIRST_SUN_PASS) && !log.includes(NO_SUN))
            problems.push("no sun pass in the log, nor no sun in the level");
        // (the disc drawn: the original's sun and flare must have been left out)
        if (log.includes(SUN_DISC_DRAWN) && !log.includes(GLARE_REPLACED))
            problems.push("the sun's disc drawn but the original's sun not left out");
        if (expect === "none" && log.includes(FIRST_SUN_PASS)) problems.push("the log shows a sun pass");
        return problems;
    }
    if (config !== "on") return problems;
    if (effect === "fog") {
        const failed = FOG_FAILED.exec(log);
        if (failed) problems.push(`log: ${failed[0]}`);
        if (expect === "shadows" && !log.includes(FIRST_FOG_PASS) && !log.includes(NO_LEVEL_FOG))
            problems.push("no fog pass in the log, nor no fog in the level");
        if (expect === "none" && log.includes(FIRST_FOG_PASS)) problems.push("the log shows a fog pass");
        return problems;
    }
    if (effect === "reflections") {
        // (a level with nothing reflective draws none: its spots are judged unchanged - compareFolder)
        const failed = REFLECTIONS_FAILED.exec(log);
        if (failed) problems.push(`log: ${failed[0]}`);
        if (expect === "shadows" && !log.includes(FIRST_REFLECTION_PASS) && !log.includes(NOTHING_REFLECTIVE))
            problems.push("no reflection pass in the log, nor nothing reflective");
        if (expect === "none" && log.includes(FIRST_REFLECTION_PASS)) problems.push("the log shows a reflection pass");
        return problems;
    }
    if (effect === "gi") {
        const skipped = GI_SKIPPED.exec(log);
        if (expect === "shadows") {
            if (!log.includes(FIRST_GI_PASS)) problems.push("no indirect light pass in the log");
            if (skipped) problems.push(`log: ${skipped[0]}`);
        } else if (log.includes(FIRST_GI_PASS)) {
            problems.push("the log shows an indirect light pass");
        }
        return problems;
    }
    const ao = effect === "ao";
    const skipped = (ao ? AO_SKIPPED : PASS_SKIPPED).exec(log);
    if (expect === "shadows") {
        if (!log.includes(ao ? FIRST_AO_PASS : FIRST_PASS) && (ao || !log.includes(NO_BAKED_SUN)))
            problems.push(ao ? "no ambient occlusion pass in the log" : "no shadow pass in the log");
        if (skipped) problems.push(`log: ${skipped[0]}`);
    } else if (!skipped) {
        problems.push("the log doesn't say the pass was skipped");
    }
    return problems;
}

// The Vulkan frame lines' "<x> ms a frame (<y> ms of it waiting ...)" averaged over a log: x and x - y.
const FRAME_LINE = /([\d.]+) ms a frame \(([\d.]+) ms of it waiting/g;
export function frameTimes(log: string): { frame: number; working: number; lines: number } {
    let frame = 0;
    let working = 0;
    let lines = 0;
    for (const match of log.matchAll(FRAME_LINE)) {
        const total = Number(match[1]);
        frame += total;
        working += total - Number(match[2]);
        lines++;
    }
    return lines ? { frame: frame / lines, working: working / lines, lines } : { frame: 0, working: 0, lines };
}

function runFolders(out: string, config: Config): string[] {
    const root = path.join(out, config);
    if (!fs.existsSync(root)) return [];
    return fs
        .readdirSync(root)
        .filter((d) => /^run\d+$/.test(d))
        .sort((p, q) => Number(p.slice(3)) - Number(q.slice(3)))
        .map((d) => path.join(root, d));
}

function compareFolder(out: string, limits: Limits, threshold: number, grow: number): number {
    const runs = Object.fromEntries(CONFIGS.map((c) => [c, runFolders(out, c)])) as Record<Config, string[]>;
    if (!runs.off.length || !runs.on.length) exitWith(`${out} needs off/run<k> and on/run<k> folders`);
    const names = fs
        .readdirSync(runs.off[0]!)
        .filter((f) => f.endsWith(".png") && fs.existsSync(path.join(runs.on[0]!, f)))
        .sort();
    const lines: string[] = [
        `expect ${limits.expect}, strength ${limits.strength}, threshold ${threshold}, mask grown by ${grow}`,
    ];
    const verdicts: SpotVerdict[] = [];
    // (reflections: a spot's group drew them when its `on` log says so; its spots are judged unchanged otherwise)
    const drewReflections = (spot: string): boolean =>
        fs
            .readdirSync(runs.on[0]!)
            .filter((f) => f.endsWith(".log") && spot.startsWith(`${f.slice(0, -4)}-`))
            .some((f) => fs.readFileSync(path.join(runs.on[0]!, f), "utf8").includes(FIRST_REFLECTION_PASS));
    let anyReflections = false;
    // (the fog: the groups whose `on` log shows a pass, and the device time the logs give)
    let anyFog = false;
    const fogTimes: string[] = [];
    if (limits.effect === "fog")
        for (const f of fs.readdirSync(runs.on[0]!).filter((n) => n.endsWith(".log"))) {
            const text = fs.readFileSync(path.join(runs.on[0]!, f), "utf8");
            anyFog ||= text.includes(FIRST_FOG_PASS);
            for (const m of text.matchAll(FOG_TIMING))
                fogTimes.push(`${f.slice(0, -4)}: ${m[1]} ms (${m[2]}x${m[3]}, ${m[4]} steps)`);
        }
    // (the sun: the device time of its parts the logs give)
    const sunTimes: string[] = [];
    if (limits.effect === "sun")
        for (const f of fs.readdirSync(runs.on[0]!).filter((n) => n.endsWith(".log"))) {
            const text = fs.readFileSync(path.join(runs.on[0]!, f), "utf8");
            for (const m of text.matchAll(SUN_TIMING))
                sunTimes.push(
                    `${f.slice(0, -4)}: ${m[1]} ms - rays ${m[2]}, source ${m[3]}, bloom ${m[4]}, composite ${m[5]} (${m[6]}x${m[7]})`,
                );
        }
    // (a spot's group: its name up to the first "-"; the group's `on` log says whether the bake showed a sun)
    const sunless = (spot: string): boolean => {
        const log = path.join(runs.on[0]!, `${spot.split("-")[0]}.log`);
        return fs.existsSync(log) && fs.readFileSync(log, "utf8").includes(NO_BAKED_SUN);
    };
    for (const name of names) {
        const load = (config: Config): Image[] =>
            runs[config]
                .map((f) => path.join(f, name))
                .filter((f) => fs.existsSync(f))
                .map(readPicture);
        const off = load("off");
        const on = load("on");
        const baseline = load("baseline");
        const mask = noiseOf(baseline.length ? [off, on, baseline] : [off, on], threshold, grow);
        const spot = name.replace(/\.png$/i, "");
        const reflected = limits.effect === "reflections" && limits.expect === "shadows" && drewReflections(spot);
        anyReflections = anyReflections || reflected;
        const spotLimits: Limits =
            limits.effect === "reflections" && !reflected ? { ...limits, expect: "none" } : limits;
        const verdict = judgeSpot(
            spot,
            shadowDifference(off[0]!, on[0]!, mask, threshold, darkestRatio(limits)),
            baseline.length ? shadowDifference(baseline[0]!, off[0]!, mask, threshold) : null,
            spotLimits,
            ((limits.effect ?? "shadows") === "shadows" || limits.effect === "gi") && sunless(name),
        );
        verdicts.push(verdict);
        const d = verdict.difference;
        lines.push(
            `${verdict.name}: compared ${d.pixels} pixels; darkened ${pct(share(d.darkened, d))} (lower half ${pct(d.darkened ? d.lowerHalf / d.darkened : 0)}), brighter ${pct(share(d.brighter, d))}, median ratio ${d.medianRatio?.toFixed(3) ?? "-"}; baked-bright darkened ${pct(d.brightDarkened / Math.max(1, d.bright))}, baked-dark ${pct(d.darkDarkened / Math.max(1, d.dark))} (split at luma ${d.split})` +
                (limits.effect === "ao" ? `, too dark ${pct(share(d.tooDark, d))}` : "") +
                (limits.effect === "hdr"
                    ? `, max channel difference ${d.maxDelta} (${pct(share(d.overDelta, d))} over ${HDR_MAX_DELTA}), mean ${d.meanDelta.toFixed(3)} (signed ${d.meanSigned.toFixed(3)}), pure white ${pct(share(d.whiteOff, d))} -> ${pct(share(d.whiteOn, d))}`
                    : "") +
                (limits.effect === "fog" || limits.effect === "sun"
                    ? `, turned black ${pct(share(d.blackened, d))}, turned white ${pct(share(d.whitened, d))}, mean luma change ${d.meanLumaChange.toFixed(1)}`
                    : "") +
                (limits.effect === "gi"
                    ? `; baked-bright brightened ${pct(d.brightBrightened / Math.max(1, d.bright))}, baked-dark ${pct(d.darkBrightened / Math.max(1, d.dark))}, runaway ${pct(share(d.runaway, d))}, turned black ${pct(share(d.blackened, d))}${sunless(spot) ? " (no baked sun)" : ""}`
                    : "") +
                (limits.effect === "reflections"
                    ? `, turned black ${pct(share(d.blackened, d))}${reflected ? "" : " (nothing reflective drawn)"}`
                    : "") +
                (verdict.baseline
                    ? `; off vs baseline changed ${pct(share(verdict.baseline.brighter + verdict.baseline.darkened, verdict.baseline))}`
                    : "") +
                (verdict.problems.length ? `\n  FAIL ${verdict.problems.join("; ")}` : ""),
        );
    }
    let failed = verdicts.some((v) => v.problems.length > 0) || names.length === 0;
    if (names.length === 0) lines.push("FAIL no spot pictured in both configurations");
    if (limits.effect === "fog") {
        lines.push(
            `the fog's passes on the device: ${fogTimes.length ? fogTimes.join("; ") : "no timing in the logs"}`,
        );
        if (limits.expect === "shadows" && !anyFog) {
            failed = true;
            lines.push("FAIL no group drew the fog (no level with fog chosen?)");
        }
    } else if (limits.effect === "sun") {
        lines.push(
            `the sun's passes on the device: ${sunTimes.length ? sunTimes.join("; ") : "no timing in the logs"}`,
        );
        if (limits.expect === "shadows" && !anyShadow(verdicts, limits)) {
            failed = true;
            lines.push(`FAIL no spot brightened by ${pct(limits.minDarkenedSomewhere)} or more: no sun drawn`);
        }
    } else if (limits.effect === "reflections" && limits.expect === "shadows" && !anyReflections) {
        failed = true;
        lines.push("FAIL no group drew reflections (nothing reflective in any level chosen?)");
    } else if (limits.effect !== "hdr" && limits.expect === "shadows" && !anyShadow(verdicts, limits)) {
        failed = true;
        const drawn = {
            shadows: "shadows",
            ao: "occlusion",
            reflections: "reflections",
            hdr: "tone mapping",
            fog: "fog",
            gi: "indirect light",
            sun: "sun",
        }[limits.effect ?? "shadows"];
        const how = limits.effect === "gi" ? "brightened" : "darkened";
        lines.push(`FAIL no spot ${how} by ${pct(limits.minDarkenedSomewhere)} or more: no ${drawn} drawn`);
    }
    if (limits.expect === "shadows" && limits.effect === "gi") {
        const shade = darkOverBrightBrightened(verdicts);
        const ok = shade === null || shade > GI_MIN_DARK_OVER_BRIGHT;
        lines.push(
            `${ok ? "" : "FAIL "}more in the shade: baked-dark pixels brightened ${shade === null ? "-" : shade.toFixed(2)} times as often as baked-bright ones (more than ${GI_MIN_DARK_OVER_BRIGHT} wanted)`,
        );
        failed ||= !ok;
    }
    if (limits.expect === "shadows" && (limits.effect ?? "shadows") === "shadows") {
        const lined = darkOverBright(verdicts);
        const ok = lined === null || lined >= limits.minDarkOverBright;
        lines.push(
            `${ok ? "" : "FAIL "}the sun against the bake: baked-dark pixels darkened ${lined === null ? "-" : lined.toFixed(2)} times as often as baked-bright ones (${limits.minDarkOverBright} wanted)`,
        );
        failed ||= !ok;
    }
    for (const config of CONFIGS)
        for (const folder of runs[config])
            for (const log of fs.readdirSync(folder).filter((f) => f.endsWith(".log"))) {
                const text = fs.readFileSync(path.join(folder, log), "utf8");
                const problems = logProblems(config, text, limits.expect, limits.effect ?? "shadows");
                if (problems.length) {
                    failed = true;
                    lines.push(`FAIL ${config}/${path.basename(folder)}/${log}: ${problems.join("; ")}`);
                }
            }
    if (limits.effect === "hdr") {
        if (!anyShadow(verdicts, limits)) {
            failed = true;
            lines.push(`FAIL no spot brighter by ${HDR_LEAST_BRIGHTER} or more on average with the exposure raised`);
        }
        for (const config of CONFIGS) {
            const logs = runs[config].flatMap((folder) =>
                fs
                    .readdirSync(folder)
                    .filter((f) => f.endsWith(".log"))
                    .map((f) => fs.readFileSync(path.join(folder, f), "utf8")),
            );
            const times = frameTimes(logs.join("\n"));
            if (times.lines)
                lines.push(
                    `${config}: ${times.frame.toFixed(2)} ms a frame, ${times.working.toFixed(2)} ms not waiting (${times.lines} Vulkan frame lines)`,
                );
        }
    }
    const title = {
        shadows: "rt_shadows check",
        ao: "rt_ao check",
        reflections: "rt_reflections check",
        hdr: "rt_hdr check",
        fog: "rt_fog check",
        gi: "rt_gi check",
        sun: "rt_sun check",
    }[limits.effect ?? "shadows"];
    lines.push(failed ? `${title}: FAILED` : `${title}: passed`);
    fs.writeFileSync(path.join(out, "report.txt"), `${lines.join("\n")}\n`);
    fs.writeFileSync(path.join(out, "report.json"), `${JSON.stringify({ limits, verdicts }, null, 1)}\n`);
    for (const l of lines) print(l);
    return failed ? 1 : 0;
}

// --- running -----------------------------------------------------------------------------------------------------------

function powershell(script: string, args: string[]): Promise<{ code: number; out: string }> {
    return new Promise((resolve) => {
        const child = spawn(
            "powershell.exe",
            ["-NoProfile", "-NonInteractive", "-File", path.join(HERE, script), ...args],
            {
                stdio: ["ignore", "pipe", "pipe"],
            },
        );
        let out = "";
        child.stdout.on("data", (d: Buffer) => {
            out += d.toString("utf8");
        });
        child.stderr.on("data", (d: Buffer) => {
            out += d.toString("utf8");
        });
        child.on("close", (code) => resolve({ code: code ?? -1, out }));
    });
}

const GAME_EXE = "thug2-decomp.exe";
const DEPLOY_ATTEMPTS = 5;
const DEPLOY_WAIT_MS = 3000;

// Whether two files have the same bytes.
export function sameFile(one: string, other: string): boolean {
    if (!fs.existsSync(one) || !fs.existsSync(other)) return false;
    return fs.readFileSync(one).equals(fs.readFileSync(other));
}

async function runGroup(game: string, group: PlanGroup, config: Config, run: number, folder: string): Promise<string> {
    // (the group's spots only: its own commands and frame time are renderer_parity's)
    const spots: PlanGroup = {
        name: group.name,
        ...(group.level ? { level: group.level } : {}),
        spots: group.spots ?? [],
    };
    const commands = groupCommands(spots, config, run);
    for (const s of groupSpots(spots))
        fs.rmSync(path.join(game, "Screenshots", seriesName(config, run, s)), {
            recursive: true,
            force: true,
        });
    clearAutomationFiles(game);
    const log = path.join(game, "thug2-decomp.log");
    fs.rmSync(log, { force: true });
    const args = ["-Game", game, "-Arch", "x64", "-Seconds", String(group.seconds ?? 240), "-HangSeconds", "120"];
    args.push("-Shot", group.name, "-ShotDir", path.join(folder, "window"));
    if (group.level) args.push("-Level", group.level);
    args.push("-Commands", commands.join(";"));
    const result = await powershell("run_game.ps1", args);
    fs.writeFileSync(path.join(folder, `${group.name}.run.txt`), result.out);
    fs.writeFileSync(path.join(folder, `${group.name}.log`), fs.existsSync(log) ? fs.readFileSync(log, "utf8") : "");
    const notes = [`exit ${result.code}`];
    for (const s of groupSpots(spots)) {
        const series = path.join(game, "Screenshots", seriesName(config, run, s));
        const frame = path.join(series, "frame_0001.png");
        if (fs.existsSync(frame)) fs.copyFileSync(frame, path.join(folder, `${s}.png`));
        else notes.push(`no picture ${s}`);
        fs.rmSync(series, { recursive: true, force: true });
    }
    return notes.join("; ");
}

interface RunOptions {
    game: string;
    out: string;
    build: string;
    baselineBuild: string | null;
    plan: string;
    groups: string[] | null;
    runs: number;
    owner: string;
    set: Record<string, string>;
    effect: Effect;
}

async function runAll(options: RunOptions): Promise<number> {
    const { game, out } = options;
    if (!isWorkspace(game)) exitWith(`${game} isn't a workspace (C:\\Games\\THUG2-<n>): this tool runs only there`);
    const plan = readPlan(fs.readFileSync(options.plan, "utf8"));
    const groups = plan.groups.filter(
        (g) => g.level && g.spots?.length && (!options.groups || options.groups.includes(g.name)),
    );
    if (!groups.length) exitWith("no level group of the plan chosen");
    const configs: Config[] = options.baselineBuild ? ["baseline", "off", "on"] : ["off", "on"];
    fs.mkdirSync(out, { recursive: true });
    const journal = path.join(out, "run.txt");
    const note = (line: string): void => {
        print(line);
        fs.appendFileSync(journal, `${line}\n`);
    };
    const lock = await powershell("game_lock.ps1", ["-Action", "acquire", "-Owner", options.owner, "-Game", game]);
    note(lock.out.trim());
    if (lock.code !== 0) return 1;
    try {
        const ini = path.join(game, "thug2-decomp.ini");
        ensureWorkspaceMuted(game);
        const base = fs.readFileSync(ini, "utf8");
        let deployed: string | null = null;
        for (let run = 1; run <= options.runs; run++) {
            for (const config of configs) {
                const build = config === "baseline" ? options.baselineBuild! : options.build;
                if (build !== deployed) {
                    // (the game just closed may hold its exe a moment: deploy_build's copy then fails without an exit
                    // code - tried again until the folder's exe is the build's)
                    let placed = false;
                    for (let attempt = 1; attempt <= DEPLOY_ATTEMPTS && !placed; attempt++) {
                        const deploy = await powershell("deploy_build.ps1", [
                            "-Game",
                            game,
                            "-Arch",
                            "x64",
                            "-BuildDir",
                            build,
                            "-NoEditorTools",
                        ]);
                        note(deploy.out.trim());
                        if (deploy.code !== 0) return 1;
                        placed = sameFile(path.join(build, GAME_EXE), path.join(game, GAME_EXE));
                        if (!placed) await new Promise((resolve) => setTimeout(resolve, DEPLOY_WAIT_MS));
                    }
                    if (!placed) {
                        note(`the build's ${GAME_EXE} couldn't be put into ${game}`);
                        return 1;
                    }
                    deployed = build;
                }
                const values = [
                    plan.ini ?? {},
                    configValues(config, options.effect),
                    config === "baseline" ? {} : options.set,
                ];
                fs.writeFileSync(ini, iniFor(base, values, "vulkan"), "utf8");
                ensureWorkspaceMuted(game);
                const folder = path.join(out, config, `run${run}`);
                fs.mkdirSync(path.join(folder, "window"), { recursive: true });
                for (const group of groups) {
                    note(`${config} run ${run} ${group.name}: ${await runGroup(game, group, config, run, folder)}`);
                }
            }
        }
    } finally {
        const release = await powershell("game_lock.ps1", [
            "-Action",
            "release",
            "-Owner",
            options.owner,
            "-Game",
            game,
        ]);
        note(release.out.trim());
    }
    return 0;
}

async function main(): Promise<number> {
    const { values, positionals } = parseCli(USAGE, {
        options: {
            game: { type: "string" },
            out: { type: "string" },
            build: { type: "string" },
            "baseline-build": { type: "string" },
            plan: { type: "string", default: path.join(HERE, "rt_shadows.json") },
            groups: { type: "string" },
            runs: { type: "string", default: "2" },
            owner: { type: "string", default: "rt-shadows-check" },
            set: { type: "string", multiple: true },
            expect: { type: "string", default: "shadows" },
            effect: { type: "string", default: "shadows" },
            strength: { type: "string" },
            threshold: { type: "string", default: "8" },
            grow: { type: "string", default: "1" },
        },
        allowPositionals: true,
    });
    const [command, target] = positionals;
    const expect = values.expect === "drawn" ? "shadows" : values.expect;
    if (expect !== "shadows" && expect !== "none" && expect !== "brighter")
        exitWith("--expect shadows | drawn | none | brighter");
    if (!(EFFECTS as readonly string[]).includes(values.effect))
        exitWith("--effect shadows | ao | reflections | hdr | fog | gi | sun");
    const effect = values.effect as Effect;
    const set: Record<string, string> = {};
    for (const s of values.set ?? []) {
        const eq = s.indexOf("=");
        if (eq <= 0) exitWith(`--set Section.key=value (${s})`);
        set[s.slice(0, eq)] = s.slice(eq + 1);
    }
    // (the ini's defaults when not given: rt_shadow_strength 0.5, rt_ao_strength 0.6, rt_reflection_strength 0.5 -
    // gpu_vulkan.h)
    const strengthKey = {
        shadows: "Graphics.rt_shadow_strength",
        ao: "Graphics.rt_ao_strength",
        reflections: "Graphics.rt_reflection_strength",
        hdr: "Graphics.rt_exposure",
        fog: "Graphics.rt_fog_density",
        gi: "Graphics.rt_gi_strength",
        sun: "Graphics.rt_bloom_strength",
    }[effect];
    const strength = Number(
        set[strengthKey] ??
            values.strength ??
            { shadows: "0.5", ao: "0.6", reflections: "0.5", hdr: "0.5", fog: "0.5", gi: "1", sun: "0.05" }[effect],
    );
    const limits: Limits = { ...LIMITS, expect, strength, effect };
    const threshold = Number(values.threshold);
    const grow = Number(values.grow);
    if (command === "compare" && target && positionals.length === 2)
        return compareFolder(path.resolve(target), limits, threshold, grow);
    if (command === "run" && positionals.length === 1) {
        if (!values.game || !values.out || !values.build) exitWith("run needs --game, --out and --build");
        const out = path.resolve(values.out);
        const code = await runAll({
            game: path.resolve(values.game),
            out,
            build: path.resolve(values.build),
            baselineBuild: values["baseline-build"] ? path.resolve(values["baseline-build"]) : null,
            plan: path.resolve(values.plan),
            groups: values.groups ? values.groups.split(",") : null,
            runs: Number(values.runs),
            owner: values.owner,
            set,
            effect,
        });
        return code !== 0 ? code : compareFolder(out, limits, threshold, grow);
    }
    print(USAGE);
    return 2;
}

if (isMain(import.meta.url)) await runMain(() => main());
