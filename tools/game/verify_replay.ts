// NOT ORIGINAL - a replay checked before it is taken as proof of a run (docs/demos.md "Replays as proof"): who recorded
// it, when, with which build, in which level; the recorded skater's profile hash; whether its seal holds (the file as
// it was recorded, or changed after - and where); and, with --game, the run simulated again from its input in a
// workspace and compared with its recorded check points.
//
//   node tools/game/verify_replay.ts <demo file> [--game <workspace>] [--timeout 300] [--json]
//
// The seal (src/formats/demo_format.h, SEAL) is SHA-256 over the file's chunks and a chain over every frame's input
// and check points. Anyone with the source can seal an edited file again, so an intact seal shows a file not changed
// by hand - not that a person played it live. What does show the run is real for this build is the simulation: the
// game is deterministic, so the recorded input played again must give the recorded check points (--game). Even that
// can't tell a run played live from input made by a tool (a "tool-assisted" run): only watching the player can.
//
// --game plays it in a workspace (thug2-decomp.exe -demo <name> -demo-quit; muted): take the workspace's lock first
// (tools/game/game_lock.ps1). Workspaces only - never the main install. Exit code: 0 intact (and, with --game, the
// simulation as recorded), 1 changed or parted, 2 no seal (recorded before seals).

import { spawn } from "node:child_process";
import { createHash } from "node:crypto";
import * as fs from "node:fs";
import * as path from "node:path";
import { exitWith, isMain, parseCli, print, runMain } from "thug2-level-converter-core/lib/cli.ts";
import { clearAutomationFiles, launchExeOrError, resolveGameFolder, runningGames } from "./edit_level.ts";
import { ensureWorkspaceMuted, isWorkspace } from "./workspace_ini.ts";

const USAGE = "usage: node tools/game/verify_replay.ts <demo file> [--game <workspace>] [--timeout 300] [--json]";

const MAGIC = "THUG2DEM";
const NEWEST_VERSION = 2;
const PORT_BYTES = 32;
const CHECK_POINT_BYTES = 84;
const DIGEST_BYTES = 32;
const SEAL_LABEL = "THUG2DEM seal 1";
const SEAL_VERSION = 1;
const MAX_LINKS = Math.floor((60 * 60 * 60) / 600) + 1; // (an hour of frames, a link every 600: the game's bound)

// A chunk's tag as the u32 it is read as.
function tagOf(text: string): number {
    return Buffer.from(text, "latin1").readUInt32LE(0);
}
const TAG = {
    info: tagOf("INFO"),
    profile: tagOf("PROF"),
    details: tagOf("DETL"),
    input: tagOf("INPT"),
    checks: tagOf("CHEK"),
    seal: tagOf("SEAL"),
};

export interface Chunk {
    id: number;
    body: Buffer;
}

export interface ParsedDemo {
    version: number;
    chunks: Chunk[];
    level: string;
    skater: string;
    created: string;
    build: string;
    ports: number;
    frames: number;
    inputs: Buffer[]; // frame f's port p at f * ports + p
    checks: { frame: number; bytes: Buffer }[];
    profile: Buffer;
    details: Buffer;
}

class Reader {
    at = 0;
    readonly bytes: Buffer;
    constructor(bytes: Buffer) {
        this.bytes = bytes;
    }
    u32(): number {
        if (this.at + 4 > this.bytes.length) throw new Error("the file ends early");
        const value = this.bytes.readUInt32LE(this.at);
        this.at += 4;
        return value;
    }
    take(count: number): Buffer {
        if (count > this.bytes.length - this.at) throw new Error("the file ends early");
        const out = this.bytes.subarray(this.at, this.at + count);
        this.at += count;
        return out;
    }
    text(): string {
        return this.take(this.u32()).toString("latin1");
    }
}

// A demo file read: its chunks, its identity, its input frames and check points. Throws on a file that isn't one.
export function parseDemo(bytes: Buffer): ParsedDemo {
    if (bytes.length < 12 || bytes.subarray(0, 8).toString("latin1") !== MAGIC) throw new Error("not a demo file");
    const reader = new Reader(bytes);
    reader.take(8);
    const version = reader.u32();
    if (version === 0 || version > NEWEST_VERSION) throw new Error(`version ${version} isn't one this tool reads`);
    const demo: ParsedDemo = {
        version,
        chunks: [],
        level: "",
        skater: "",
        created: "",
        build: "",
        ports: 1,
        frames: 0,
        inputs: [],
        checks: [],
        profile: Buffer.alloc(0),
        details: Buffer.alloc(0),
    };
    let hasInfo = false;
    let hasInput = false;
    while (reader.at < bytes.length) {
        const id = reader.u32();
        const body = reader.take(reader.u32());
        demo.chunks.push({ id, body });
        const chunk = new Reader(body);
        if (id === TAG.info) {
            demo.level = chunk.text();
            demo.skater = chunk.text();
            demo.created = chunk.text();
            demo.build = chunk.text();
            hasInfo = true;
        } else if (id === TAG.profile) {
            demo.profile = body;
        } else if (id === TAG.details) {
            demo.details = body;
        } else if (id === TAG.input) {
            const ports = chunk.u32();
            const frames = chunk.u32();
            if (ports === 0 || ports > 2) throw new Error("bad input chunk");
            demo.ports = ports;
            demo.frames = frames;
            demo.inputs = new Array<Buffer>(frames * ports);
            for (let port = 0; port < ports; port++) {
                let frame = 0;
                while (frame < frames) {
                    const count = chunk.u32();
                    const data = chunk.take(PORT_BYTES);
                    if (count === 0 || count > frames - frame) throw new Error("bad input run");
                    for (let i = 0; i < count; i++, frame++) demo.inputs[frame * ports + port] = data;
                }
            }
            hasInput = true;
        } else if (id === TAG.checks) {
            const count = chunk.u32();
            for (let i = 0; i < count; i++) {
                const point = chunk.take(CHECK_POINT_BYTES);
                demo.checks.push({ frame: point.readUInt32LE(0), bytes: point });
            }
        }
    }
    if (!hasInfo) throw new Error("no info chunk");
    if (!hasInput) throw new Error("no input chunk");
    return demo;
}

function sha256(...parts: (Buffer | number)[]): Buffer {
    const hash = createHash("sha256");
    for (const part of parts) {
        if (typeof part === "number") {
            const word = Buffer.alloc(4);
            word.writeUInt32LE(part >>> 0);
            hash.update(word);
        } else {
            hash.update(part);
        }
    }
    return hash.digest();
}

// The recorded skater's identity (demo_format.cpp profile_hash).
export function profileHash(demo: ParsedDemo): Buffer {
    return sha256(TAG.profile, demo.profile.length, demo.profile, TAG.details, demo.details.length, demo.details);
}

export interface SealReport {
    state: "intact" | "changed" | "none";
    profileHash: string; // as the seal holds it ("" without one)
    headerChanged: boolean;
    profileChanged: boolean;
    changedFrom: number; // -1: none
    changedTo: number;
    endChanged: boolean;
    text: string;
}

function chain(start: Buffer, demo: ParsedDemo, stride: number): { links: Buffer[]; end: Buffer } {
    const links: Buffer[] = [];
    let link = start;
    let next = 0;
    for (let frame = 0; frame < demo.frames; frame++) {
        const parts: (Buffer | number)[] = [link, frame];
        for (let port = 0; port < demo.ports; port++) parts.push(demo.inputs[frame * demo.ports + port]!);
        while (next < demo.checks.length && demo.checks[next]!.frame === frame) parts.push(demo.checks[next++]!.bytes);
        link = sha256(...parts);
        if ((frame + 1) % stride === 0 || frame + 1 === demo.frames) links.push(link);
    }
    const rest = demo.checks.slice(next).map((c) => c.bytes);
    return { links, end: sha256(link, demo.checks.length - next, ...rest) };
}

// The seal checked (demo_format.cpp check_seal, the same words as the game's log: seal_text).
export function checkSeal(demo: ParsedDemo): SealReport {
    const out: SealReport = {
        state: "none",
        profileHash: "",
        headerChanged: false,
        profileChanged: false,
        changedFrom: -1,
        changedTo: -1,
        endChanged: false,
        text: "none (recorded before replays were sealed)",
    };
    const sealAt = demo.chunks.findIndex((c) => c.id === TAG.seal);
    if (sealAt < 0) return out;
    out.state = "changed";
    const startParts: (Buffer | number)[] = [Buffer.from(SEAL_LABEL, "latin1"), demo.version];
    for (const c of demo.chunks.slice(0, sealAt)) {
        if (c.id === TAG.input || c.id === TAG.checks) continue;
        startParts.push(c.id, c.body.length, c.body);
    }
    const start = sha256(...startParts);
    try {
        const seal = new Reader(demo.chunks[sealAt]!.body);
        const version = seal.u32();
        const profile = seal.take(DIGEST_BYTES);
        const storedStart = seal.take(DIGEST_BYTES);
        const stride = seal.u32();
        const count = seal.u32();
        if (version !== SEAL_VERSION || stride === 0 || count > MAX_LINKS) throw new Error("bad seal");
        const links: Buffer[] = [];
        for (let i = 0; i < count; i++) links.push(seal.take(DIGEST_BYTES));
        const storedEnd = seal.take(DIGEST_BYTES);
        out.profileHash = profile.toString("hex");
        out.profileChanged = !profile.equals(profileHash(demo));
        out.headerChanged = !storedStart.equals(start);
        const made = chain(storedStart, demo, stride);
        if (made.links.length !== links.length) {
            out.changedFrom = 0;
            out.changedTo = demo.frames;
        } else {
            const i = links.findIndex((l, n) => !l.equals(made.links[n]!));
            if (i >= 0) {
                out.changedFrom = i * stride;
                out.changedTo = Math.min((i + 1) * stride, demo.frames);
            }
        }
        out.endChanged = out.changedFrom < 0 && !made.end.equals(storedEnd);
    } catch {
        out.endChanged = true;
    }
    if (!out.headerChanged && !out.profileChanged && out.changedFrom < 0 && !out.endChanged) {
        out.state = "intact";
        out.text = "intact";
        return out;
    }
    const what: string[] = [];
    if (out.headerChanged) what.push("its header (level, skater, date, build, settings)");
    if (out.profileChanged) what.push("the skater");
    if (out.changedFrom >= 0)
        what.push(`the input or check points in frames ${out.changedFrom} - ${out.changedTo - 1}`);
    if (out.endChanged) what.push("the seal itself or the last check points");
    out.text = `changed after recording: ${what.join(", ")}`;
    return out;
}

export interface Resimulated {
    ran: boolean;
    compared: number;
    firstDivergence: number; // -1: none
    maxSkater: number;
    note: string;
}

// The demo played again in a workspace from its input; its playback's summary (<name>.play.run.json) read.
async function resimulate(file: string, game: string, timeout: number): Promise<Resimulated> {
    const out: Resimulated = { ran: false, compared: 0, firstDivergence: -1, maxSkater: 0, note: "" };
    if (!isWorkspace(game))
        exitWith(`${game} isn't a workspace (C:\\Games\\THUG2-n): replays are re-simulated there only`);
    const found = launchExeOrError(game, game);
    if ("error" in found) exitWith(found.error);
    const here = path.resolve(game).toLowerCase();
    if (runningGames().some((p) => p === null || path.dirname(path.resolve(p)).toLowerCase() === here))
        exitWith(`THUG2 IS ALREADY RUNNING FROM ${game} - not started`);
    const name = path.basename(file).replace(/\.demo$/i, "");
    const folder = path.join(game, "thug2-decomp-demos");
    fs.mkdirSync(folder, { recursive: true });
    const placed = path.join(folder, `${name}.demo`);
    if (path.resolve(placed).toLowerCase() !== path.resolve(file).toLowerCase()) fs.copyFileSync(file, placed);
    const summary = path.join(folder, `${name}.play.run.json`);
    fs.rmSync(summary, { force: true });
    ensureWorkspaceMuted(game);
    clearAutomationFiles(game);
    const child = spawn(found.exe, ["-demo", name, "-demo-quit"], { cwd: game, stdio: "ignore" });
    await new Promise<void>((resolve) => {
        const timer = setTimeout(() => {
            out.note = `still running after ${timeout} s - closed`;
            child.kill();
        }, timeout * 1000);
        child.on("exit", () => {
            clearTimeout(timer);
            resolve();
        });
    });
    if (!fs.existsSync(summary)) {
        out.note ||= "the game wrote no summary (see its thug2-decomp.log)";
        return out;
    }
    const run = JSON.parse(fs.readFileSync(summary, "utf8")) as {
        compared: number;
        first_divergence_frame: number;
        max_skater_distance: number;
    };
    out.ran = true;
    out.compared = run.compared;
    out.firstDivergence = run.first_divergence_frame;
    out.maxSkater = run.max_skater_distance;
    return out;
}

export function identityLines(demo: ParsedDemo, seal: SealReport): string[] {
    const actual = profileHash(demo).toString("hex");
    return [
        `skater:        ${demo.skater || "(none)"}`,
        `profile hash:  ${seal.profileHash || actual}${seal.profileHash && seal.profileHash !== actual ? ` (the file's skater: ${actual})` : ""}`,
        `recorded:      ${demo.created}`,
        `build:         ${demo.build}`,
        `level:         ${demo.level}`,
        `length:        ${demo.frames} frames (${(demo.frames / 60).toFixed(1)} s), ${demo.checks.length} check points`,
        `seal:          ${seal.text}`,
    ];
}

async function main(): Promise<number> {
    const { values, positionals } = parseCli(USAGE, {
        options: {
            game: { type: "string" },
            timeout: { type: "string", default: "300" },
            json: { type: "boolean", default: false },
        },
        allowPositionals: true,
    });
    const file = positionals[0];
    if (!file) exitWith(USAGE);
    if (!fs.existsSync(file)) exitWith(`no ${file}`);
    let demo: ParsedDemo;
    try {
        demo = parseDemo(fs.readFileSync(file));
    } catch (error) {
        exitWith(`${file}: ${(error as Error).message}`);
    }
    const seal = checkSeal(demo);
    const resim = values.game ? await resimulate(file, resolveGameFolder(values.game), Number(values.timeout)) : null;
    if (values.json) {
        print(
            JSON.stringify(
                {
                    file,
                    skater: demo.skater,
                    profileHash: profileHash(demo).toString("hex"),
                    created: demo.created,
                    build: demo.build,
                    level: demo.level,
                    frames: demo.frames,
                    seal,
                    resimulated: resim,
                },
                null,
                2,
            ),
        );
    } else {
        print(`replay ${file}`);
        for (const line of identityLines(demo, seal)) print(`  ${line}`);
        if (resim) {
            let text: string;
            if (!resim.ran) text = `not run - ${resim.note}`;
            else if (resim.compared === 0) text = "no check points compared";
            else if (resim.firstDivergence < 0) text = `the same as recorded (${resim.compared} check points)`;
            else
                text = `parts from the recording at frame ${resim.firstDivergence} (largest skater drift ${resim.maxSkater.toFixed(3)})`;
            print(`  simulated:     ${text}`);
        }
    }
    if (seal.state === "changed") return 1;
    if (resim && (!resim.ran || resim.compared === 0 || resim.firstDivergence >= 0)) return 1;
    return seal.state === "none" ? 2 : 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
