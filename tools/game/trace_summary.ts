// NOT ORIGINAL - a skater trace (the automation channel's `trace <frames>`: thug2-decomp-trace.txt) in short: the
// runs of one state (ground / air / wall / lip / rail / wallplant) with where each started and ended, the highest
// point and speed of each, and the frames where the horizontal velocity turned round at once (a bounce off a wall:
// more than 90 degrees in a frame) or the speed dropped by more than half (stopped against something).
//
//   node tools/game/trace_summary.ts <trace.txt> [--every N]
//       --every N: also every Nth line as it is.

import * as fs from "node:fs";
import * as path from "node:path";

export interface TraceLine {
    frame: number;
    pos: [number, number, number];
    vel: [number, number, number];
    upY: number;
    state: number;
    physics: number;
}

const STATES = ["ground", "air", "wall", "lip", "rail", "wallplant"];

export function readTrace(text: string): TraceLine[] {
    const out: TraceLine[] = [];
    for (const line of text.split(/\r?\n/)) {
        if (!line.trim() || line.startsWith("#")) continue;
        const v = line.trim().split(/\s+/).map(Number);
        if (v.length < 10 || v.some((x) => Number.isNaN(x))) continue;
        out.push({
            frame: v[0]!,
            pos: [v[1]!, v[2]!, v[3]!],
            vel: [v[4]!, v[5]!, v[6]!],
            upY: v[7]!,
            state: v[8]!,
            physics: v[9]!,
        });
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
    kind: "bounce" | "stop";
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
    });
    return { segments, events };
}

function fmt(v: readonly number[]): string {
    return `(${v.map((x) => x.toFixed(0)).join(", ")})`;
}

function main(argv: string[]): number {
    const file = argv[0];
    if (!file) {
        console.error("usage: trace_summary.ts <trace.txt> [--every N]");
        return 2;
    }
    const lines = readTrace(fs.readFileSync(file, "utf8"));
    const { segments, events } = summarize(lines);
    console.log(`${path.basename(file)}: ${lines.length} frames`);
    for (const s of segments)
        console.log(
            `  ${String(s.from.frame).padStart(6)}-${String(s.to.frame).padEnd(6)} ${(STATES[s.state] ?? String(s.state)).padEnd(9)} ${fmt(s.from.pos)} -> ${fmt(s.to.pos)}  top y ${s.maxY.toFixed(0)}, speed up to ${s.maxSpeed.toFixed(0)}`,
        );
    for (const e of events)
        console.log(
            `  frame ${e.frame}: ${e.kind} at ${fmt(e.pos)} (speed ${e.before.toFixed(0)} -> ${e.after.toFixed(0)})`,
        );
    const every = argv.indexOf("--every");
    if (every >= 0) {
        const n = Number(argv[every + 1] ?? 10);
        lines.forEach((l, i) => {
            if (i % n === 0)
                console.log(
                    `  ${l.frame} ${fmt(l.pos)} v ${fmt(l.vel)} up_y ${l.upY.toFixed(2)} ${STATES[l.state] ?? l.state}`,
                );
        });
    }
    return 0;
}

if (process.argv[1] && path.resolve(process.argv[1]) === path.resolve(import.meta.filename))
    process.exit(main(process.argv.slice(2)));
