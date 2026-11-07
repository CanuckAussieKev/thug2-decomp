// NOT ORIGINAL - which Bink files a game folder has, by the fields of their headers that decide whether FFmpeg's
// Bink demuxer and decoders can play them: the revision ("BIK" + a letter), the size, the frame rate, the video flags
// (alpha, grey) and each sound track's rate, channels and transform (Bink audio's DCT or RDFT). The movies are listed one
// by one; the music and sound streams (Data/streams/**/*.bik) and the Bink files inside the .wad archives (pcm.wad's
// entries) are counted by kind.
//
//   node tools/ffmpeg/bink_survey.ts <game folder>
//
// Read only. FFmpeg 7.1's Bink demuxer takes the Bink 1 revisions b, f, g, h, i and k (and Bink 2, which it can't decode).

import * as fs from "node:fs";
import * as path from "node:path";
import { exitWith, isMain, print, runMain } from "thug2-level-converter-core/lib/cli.ts";

export interface BinkHeader {
    revision: string;
    width: number;
    height: number;
    frames: number;
    rate: string;
    videoFlags: number;
    tracks: string[];
}

const AUDIO_DCT = 0x1000;
const AUDIO_STEREO = 0x2000;
const MAX_TRACKS = 8;
const MAX_SIDE = 4096;

// The header at `at` (null if there's none: not "BIK", or fields no Bink file has).
export function readBinkHeader(data: Buffer, at = 0): BinkHeader | null {
    if (at + 44 > data.length || data.toString("latin1", at, at + 3) !== "BIK") return null;
    const word = (offset: number): number => data.readUInt32LE(at + offset);
    const trackCount = word(40);
    const width = word(20);
    const height = word(24);
    if (trackCount > MAX_TRACKS || width > MAX_SIDE || height > MAX_SIDE || at + 44 + 12 * trackCount > data.length)
        return null;
    // (after the count: a 4-byte field per track, then each track's rate and flags, then each track's id)
    const tracks: string[] = [];
    for (let track = 0; track < trackCount; ++track) {
        const field = at + 44 + 4 * trackCount + 4 * track;
        const flags = data.readUInt16LE(field + 2);
        tracks.push(
            `${data.readUInt16LE(field)} Hz ${flags & AUDIO_STEREO ? "stereo" : "mono"} ${flags & AUDIO_DCT ? "DCT" : "RDFT"}`,
        );
    }
    return {
        revision: `BIK${String.fromCharCode(data[at + 3]!)}`,
        width,
        height,
        frames: word(8),
        rate: `${word(28)}/${word(32)}`,
        videoFlags: word(36),
        tracks,
    };
}

const describe = (header: BinkHeader): string =>
    `${header.revision} ${header.width}x${header.height} video flags ${header.videoFlags.toString(16)}, sound: ${
        header.tracks.join(", ") || "none"
    }`;

function filesUnder(folder: string, pattern: RegExp, out: string[] = []): string[] {
    if (!fs.existsSync(folder)) return out;
    for (const entry of fs.readdirSync(folder, { withFileTypes: true })) {
        const file = path.join(folder, entry.name);
        if (entry.isDirectory()) filesUnder(file, pattern, out);
        else if (pattern.test(entry.name)) out.push(file);
    }
    return out.sort();
}

function main(): number {
    const game = process.argv[2];
    if (!game) return exitWith("usage: bink_survey.ts <game folder>");
    const data = fs.existsSync(path.join(game, "Data")) ? path.join(game, "Data") : path.join(game, "Game", "Data");
    if (!fs.existsSync(data)) return exitWith(`${game}: no Data folder`);
    const kinds = new Map<string, number>();
    const count = (kind: string): void => {
        kinds.set(kind, (kinds.get(kind) ?? 0) + 1);
    };
    for (const file of filesUnder(data, /\.bik$/i)) {
        const relative = path.relative(data, file);
        const header = readBinkHeader(fs.readFileSync(file));
        if (!header) count(`not a Bink file: ${relative}`);
        else if (/^movies/i.test(relative))
            print(`${relative}: ${describe(header)}, ${header.frames} frames at ${header.rate}`);
        else count(`${path.dirname(relative)}: ${describe(header)}`);
    }
    for (const file of filesUnder(data, /\.wad$/i)) {
        const bytes = fs.readFileSync(file);
        for (let at = bytes.indexOf("BIK"); at >= 0; at = bytes.indexOf("BIK", at + 1)) {
            const header = readBinkHeader(bytes, at);
            if (header) count(`${path.relative(data, file)} entries: ${describe(header)}`);
        }
    }
    for (const [kind, n] of [...kinds].sort()) print(`${n} x ${kind}`);
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
