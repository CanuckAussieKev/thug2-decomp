// NOT ORIGINAL - a skater trace (the automation channel's `trace <frames>`: thug2-decomp-trace.txt) in short: the
// runs of one state (ground / air / wall / lip / rail / wallplant) with where each started and ended, the highest
// point and speed of each, and the frames where the horizontal velocity turned round at once (a bounce off a wall:
// more than 90 degrees in a frame) or the speed dropped by more than half (stopped against something); from a trace
// with the later columns also bails, combos landed or lost (the combo so far back to 0), air blips (in the air for
// 3 frames or less between ground) and the ground's collision object changing (a seam).
//
//   node tools/pipeline/game/trace_summary.ts <trace.txt> [--every N | --trials]
//       --every N: also every Nth line as it is. --trials: a trace of several rides (teleports between them), a line
//       a ride - bail or not, how its combo ended.

import * as fs from "node:fs";
import * as path from "node:path";
import { helpFromHeader } from "../lib/cli.ts";

export interface TraceLine {
    frame: number;
    pos: [number, number, number];
    vel: [number, number, number];
    upY: number;
    state: number;
    physics: number;
    bailing?: number;
    trick?: number;
    pot?: number; // the combo so far (before the multiplier)
    multiplier?: number;
    landed?: number; // the last combo landed
    groundFlags?: number;
    groundObject?: number;
    teleport?: string; // the first line after a teleport (the trace's "# teleport ..." line)
}

const STATES = ["ground", "air", "wall", "lip", "rail", "wallplant"];

export function readTrace(text: string): TraceLine[] {
    const out: TraceLine[] = [];
    let teleport: string | undefined;
    for (const line of text.split(/\r?\n/)) {
        if (line.startsWith("# teleport")) teleport = line.slice(2);
        if (!line.trim() || line.startsWith("#")) continue;
        const v = line.trim().split(/\s+/).map(Number);
        if (v.length < 10 || v.some((x) => Number.isNaN(x))) continue;
        const l: TraceLine = {
            frame: v[0]!,
            pos: [v[1]!, v[2]!, v[3]!],
            vel: [v[4]!, v[5]!, v[6]!],
            upY: v[7]!,
            state: v[8]!,
            physics: v[9]!,
        };
        if (v.length >= 17) {
            l.bailing = v[10]!;
            l.trick = v[11]!;
            l.pot = v[12]!;
            l.multiplier = v[13]!;
            l.landed = v[14]!;
            l.groundFlags = v[15]!;
            l.groundObject = v[16]!;
        }
        if (teleport !== undefined) l.teleport = teleport;
        teleport = undefined;
        out.push(l);
    }
    return out;
}

export interface Segment {
    state: number;
    from: TraceLine;
    to: TraceLine;
    maxY: number;
    maxSpeed: number;
}

export interface Event {
    frame: number;
    kind: "bounce" | "stop" | "bail" | "combo ended" | "combo lost" | "air blip" | "seam";
    pos: [number, number, number];
    before: number; // horizontal speed before
    after: number;
}

export function summarize(lines: readonly TraceLine[]): { segments: Segment[]; events: Event[] } {
    const segments: Segment[] = [];
    const events: Event[] = [];
    let current: Segment | null = null;
    lines.forEach((l, i) => {
        const speed = Math.hypot(...l.vel);
        if (!current || current.state !== l.state) {
            current = { state: l.state, from: l, to: l, maxY: l.pos[1], maxSpeed: speed };
            segments.push(current);
        }
        current.to = l;
        current.maxY = Math.max(current.maxY, l.pos[1]);
        current.maxSpeed = Math.max(current.maxSpeed, speed);
        const p = lines[i - 1];
        if (!p) return;
        const h0 = Math.hypot(p.vel[0], p.vel[2]);
        const h1 = Math.hypot(l.vel[0], l.vel[2]);
        if (h0 > 50 && h1 > 1) {
            const cos = (p.vel[0] * l.vel[0] + p.vel[2] * l.vel[2]) / (h0 * h1);
            if (cos < 0) events.push({ frame: l.frame, kind: "bounce", pos: l.pos, before: h0, after: h1 });
        }
        const s0 = Math.hypot(...p.vel);
        if (s0 > 100 && speed < s0 * 0.5)
            events.push({ frame: l.frame, kind: "stop", pos: l.pos, before: s0, after: speed });
        if (l.bailing === 1 && p.bailing === 0)
            events.push({ frame: l.frame, kind: "bail", pos: l.pos, before: s0, after: speed });
        // (the combo so far back to 0: lost in a bail, else landed - ended)
        if ((p.pot ?? 0) > 0 && l.pot === 0)
            events.push({
                frame: l.frame,
                kind: l.bailing ? "combo lost" : "combo ended",
                pos: l.pos,
                before: p.pot!,
                after: l.state,
            });
        if (l.state === 0 && p.state === 0 && l.groundObject !== p.groundObject && l.groundObject && p.groundObject)
            events.push({ frame: l.frame, kind: "seam", pos: l.pos, before: p.groundObject, after: l.groundObject });
    });
    // short air between two ground runs
    segments.forEach((s, i) => {
        if (s.state !== 1 || i === 0 || i === segments.length - 1) return;
        if (segments[i - 1]!.state === 0 && segments[i + 1]!.state === 0 && s.to.frame - s.from.frame < 3)
            events.push({
                frame: s.from.frame,
                kind: "air blip",
                pos: s.from.pos,
                before: s.to.frame - s.from.frame + 1,
                after: 0,
            });
    });
    events.sort((a, b) => a.frame - b.frame);
    return { segments, events };
}

// A trace of several rides (teleports between them: the trace's teleport lines, or without them a jump of more than
// 150 inches in a frame), a line each: the
// top height, air blips, seams, whether the skater bailed (and where: state, up y, height), the combo's end.
export function trials(lines: readonly TraceLine[]): string[] {
    const pieces: TraceLine[][] = [];
    const marked = lines.some((l) => l.teleport !== undefined);
    lines.forEach((l, i) => {
        const p = lines[i - 1];
        const jump = p && Math.hypot(l.pos[0] - p.pos[0], l.pos[1] - p.pos[1], l.pos[2] - p.pos[2]) > 150;
        if (marked ? l.teleport !== undefined : !p || jump) pieces.push([]);
        pieces[pieces.length - 1]?.push(l);
    });
    return pieces.map((piece, n) => {
        const { events } = summarize(piece);
        const first = piece[0]!;
        const top = Math.max(...piece.map((l) => l.pos[1]));
        const base = first.pos[1];
        const count = (kind: Event["kind"]): number => events.filter((e) => e.kind === kind).length;
        const bail = events.find((e) => e.kind === "bail");
        const at = (frame: number): TraceLine => piece.find((l) => l.frame === frame) ?? first;
        const bailText = bail
            ? `BAIL at +${bail.frame - first.frame} (${STATES[at(bail.frame - 1).state] ?? "?"} before, up y ${at(bail.frame - 1).upY.toFixed(2)}, height ${(bail.pos[1] - base).toFixed(0)})`
            : "no bail";
        const ends = events.filter((e) => e.kind === "combo ended" || e.kind === "combo lost");
        const maxPot = Math.max(0, ...piece.map((l) => l.pot ?? 0));
        const endText = ends
            .map(
                (e) =>
                    `${e.kind === "combo lost" ? "lost" : "ended"} at +${e.frame - first.frame} height ${(e.pos[1] - base).toFixed(0)} (${STATES[e.after] ?? e.after})`,
            )
            .join(", ");
        const comboText = maxPot ? `combo ${maxPot}: ${endText || "still going"}` : "no combo";
        let states = "";
        for (const l of piece) {
            const c = STATES[l.state]?.[0] ?? "?";
            if (!states.endsWith(c)) states += c;
        }
        const where = first.teleport ? ` [${first.teleport.replace(/ -> .*/, "")}]` : "";
        return `trial ${n}${where}: top +${(top - base).toFixed(0)}, ${count("air blip")} air blips, ${count("seam")} seams, ${bailText}; ${comboText}; states ${states}`;
    });
}

function fmt(v: readonly number[]): string {
    return `(${v.map((x) => x.toFixed(0)).join(", ")})`;
}

function main(argv: string[]): number {
    const file = argv[0];
    if (!file) {
        console.error("usage: trace_summary.ts <trace.txt> [--every N | --trials]");
        return 2;
    }
    const lines = readTrace(fs.readFileSync(file, "utf8"));
    const { segments, events } = summarize(lines);
    if (argv.includes("--trials")) {
        for (const t of trials(lines)) console.log(t);
        return 0;
    }
    console.log(`${path.basename(file)}: ${lines.length} frames`);
    for (const s of segments)
        console.log(
            `  ${String(s.from.frame).padStart(6)}-${String(s.to.frame).padEnd(6)} ${(STATES[s.state] ?? String(s.state)).padEnd(9)} ${fmt(s.from.pos)} -> ${fmt(s.to.pos)}  top y ${s.maxY.toFixed(0)}, speed up to ${s.maxSpeed.toFixed(0)}`,
        );
    const detail = (e: Event): string => {
        switch (e.kind) {
            case "bounce":
            case "stop":
            case "bail":
                return `speed ${e.before.toFixed(0)} -> ${e.after.toFixed(0)}`;
            case "combo ended":
            case "combo lost":
                return `${e.before} so far, ${STATES[e.after] ?? e.after}`;
            case "air blip":
                return `${e.before} frames`;
            case "seam":
                return `object 0x${e.before.toString(16)} -> 0x${e.after.toString(16)}`;
        }
    };
    for (const e of events) console.log(`  frame ${e.frame}: ${e.kind} at ${fmt(e.pos)} (${detail(e)})`);
    const every = argv.indexOf("--every");
    if (every >= 0) {
        const n = Number(argv[every + 1] ?? 10);
        lines.forEach((l, i) => {
            if (i % n === 0)
                console.log(
                    `  ${l.frame} ${fmt(l.pos)} v ${fmt(l.vel)} up_y ${l.upY.toFixed(2)} ${STATES[l.state] ?? l.state}` +
                        (l.pot === undefined
                            ? ""
                            : ` bail ${l.bailing} trick ${l.trick} pot ${l.pot}x${l.multiplier} landed ${l.landed} ground 0x${l.groundFlags!.toString(16)} 0x${l.groundObject!.toString(16)}`),
                );
        });
    }
    return 0;
}

if (process.argv[1] && path.resolve(process.argv[1]) === path.resolve(import.meta.filename)) {
    helpFromHeader(import.meta.url);
    process.exit(main(process.argv.slice(2)));
}
