// NOT ORIGINAL - what the translated scripts assume about the 60 Hz tick (docs/design/tick-rate.md): every Wait and its
// unit, the "wait a frame" idioms, loops that count frames, frame-named arguments and per-tick increments, each with its
// file:line and a class:
//   a  mechanical - a count of ticks meant as time: N ticks -> N/60 s
//   b  judgement  - a per-tick increment, a count from a variable, a consumer that decides the unit
//   c  fine       - already time (seconds, milliseconds, `frames` - the Wait command makes them 16.67 ms of game time),
//                   or truly per-tick bookkeeping (poll each tick, "go on next update", one script a frame)
//
//   node tools/qbjs/frame_audit.ts --game <folder> [--overlays] [--out report.md] [--json out.json] [--all]
//   node tools/qbjs/frame_audit.ts <file.ts | folder>... [--out report.md]
//
// --game scans the game's Data\**\*.ts (the scripts as unpack_game.ts made them); --overlays adds the data overlays
// (thug2-decomp-<x> folders). --all lists the class c sites too (counted only by default).
// Parsed with the TypeScript compiler's own scanner (typescript/unstable/ast/scanner - TypeScript 7 has no in-process
// parser: its program API runs through the native server); the scripts' shapes are few and fixed (emit_ts.ts), so the
// structure (scripts, loops, calls, object keys) is read from the tokens.
import { existsSync, readdirSync, readFileSync, statSync, writeFileSync } from "node:fs";
import { join, relative } from "node:path";
import { isMain, parseCli, runMain } from "thug2-level-converter-core/lib/cli.ts";
import { SyntaxKind } from "typescript/unstable/ast";
import { createScanner } from "typescript/unstable/ast/scanner";

export type FrameClass = "a" | "b" | "c";

export type Pattern =
    | "wait-seconds"
    | "wait-ms"
    | "wait-frames"
    | "wait-gameframes"
    | "wait-one-per-frame"
    | "wait-real-time"
    | "wait-other"
    | "wait-one-game-frame"
    | "frame-command"
    | "frame-loop"
    | "frame-counter"
    | "per-tick-increment"
    | "frame-argument";

export type Finding = {
    file: string;
    line: number;
    script: string;
    pattern: Pattern;
    cls: FrameClass;
    why: string;
    text: string;
};

type Token = { kind: SyntaxKind; text: string; start: number; end: number };

// Commands (scripts or C functions) whose own unit is ticks: what calling them means.
const FRAME_COMMANDS: Record<string, { cls: FrameClass; why: string }> = {
    waitonegameframe: { cls: "c", why: "one Update (skutils: Wait 1 gameframes)" },
    waitframeslateollie: { cls: "a", why: "counts its frames= down once a tick (tricks: late-ollie window)" },
    animfinished_or_waitedframes: { cls: "a", why: "repeat frames= ticks (bails)" },
    waitwalkingframe: { cls: "c", why: "one Update after the walking trick checks" },
    waitanimwalkingframe: { cls: "c", why: "one Update after the walking trick checks" },
};

// Object keys whose name says "frame": what the number means.
const FRAME_KEYS: Record<string, { cls: FrameClass; why: string }> = {
    frames: { cls: "b", why: "a frame count - its consumer decides (camera / goal / bail scripts)" },
    wait_frames: { cls: "a", why: "ticks to wait" },
    boardoffframe: { cls: "c", why: "an animation frame (the anim's own clock)" },
    intoairframe: { cls: "c", why: "an animation frame (the anim's own clock)" },
    bloodframe: { cls: "c", why: "an animation frame (the anim's own clock)" },
    hitbodyframe: { cls: "c", why: "an animation frame (the anim's own clock)" },
    holdframe: { cls: "c", why: "an animation frame (the anim's own clock)" },
    lock_framerate: { cls: "c", why: "a renderer setting" },
    framerate: { cls: "c", why: "a renderer setting" },
};
const NOT_TIME_KEY = /wireframe|frame_text|dialog_frame|_pcnt$/i;

const UNIT_SECONDS = new Set(["seconds", "second"]);
const UNIT_FRAMES = new Set(["frame", "frames"]);
const UNIT_GAME = new Set(["gameframe", "gameframes", "game"]);
const UNIT_ONE_PER_FRAME = "one_per_frame";

function tokenize(text: string): Token[] {
    const scanner = createScanner(true);
    scanner.setText(text);
    const tokens: Token[] = [];
    const braces: boolean[] = []; // true: a template's ${ - its } goes on with the template
    for (;;) {
        let kind = scanner.scan();
        if (kind === SyntaxKind.EndOfFile) break;
        if (kind === SyntaxKind.OpenBraceToken) braces.push(false);
        else if (kind === SyntaxKind.TemplateHead) braces.push(true);
        else if (kind === SyntaxKind.CloseBraceToken) {
            if (braces.pop()) {
                kind = scanner.reScanTemplateToken(false);
                if (kind === SyntaxKind.TemplateMiddle) braces.push(true);
            }
        }
        tokens.push({ kind, text: scanner.getTokenText(), start: scanner.getTokenStart(), end: scanner.getTokenEnd() });
    }
    return tokens;
}

const OPENERS = new Set([SyntaxKind.OpenParenToken, SyntaxKind.OpenBraceToken, SyntaxKind.OpenBracketToken]);
const CLOSERS = new Set([SyntaxKind.CloseParenToken, SyntaxKind.CloseBraceToken, SyntaxKind.CloseBracketToken]);

type Loop = {
    kind: "while" | "do" | "for";
    start: number; // body's {
    end: number; // body's }
    cond: [number, number]; // condition tokens (exclusive)
    line: number;
};

// A loop's wait that makes one pass a tick (or a frame of time).
type TickWait = { index: number; unit: "game" | "frames" | "call" };

export function analyzeSource(text: string, file = "input.ts"): Finding[] {
    const t = tokenize(text);
    const lineStarts: number[] = [0];
    for (let i = 0; i < text.length; i++) if (text.charCodeAt(i) === 10) lineStarts.push(i + 1);
    const lineOf = (pos: number) => {
        let lo = 0,
            hi = lineStarts.length - 1;
        while (lo < hi) {
            const mid = (lo + hi + 1) >> 1;
            if (lineStarts[mid]! <= pos) lo = mid;
            else hi = mid - 1;
        }
        return lo + 1;
    };
    const lineText = (pos: number) => {
        const line = lineOf(pos);
        const end = line < lineStarts.length ? lineStarts[line]! - 1 : text.length;
        return text
            .slice(lineStarts[line - 1]!, end)
            .trim()
            .slice(0, 160);
    };

    // Brackets.
    const match = new Int32Array(t.length).fill(-1);
    const stack: number[] = [];
    t.forEach((tok, i) => {
        if (OPENERS.has(tok.kind)) stack.push(i);
        else if (CLOSERS.has(tok.kind) && stack.length) {
            const open = stack.pop()!;
            match[open] = i;
            match[i] = open;
        }
    });
    const kindAt = (i: number) => t[i]?.kind;
    const textAt = (i: number) => t[i]?.text ?? "";
    const lower = (i: number) => textAt(i).toLowerCase();

    // Scripts: script("Name", function* (p) { ... }).
    const scripts: { name: string; start: number; end: number }[] = [];
    // Loops.
    const loops: Loop[] = [];
    for (let i = 0; i < t.length; i++) {
        const tok = t[i]!;
        if (
            tok.kind === SyntaxKind.Identifier &&
            tok.text === "script" &&
            kindAt(i + 1) === SyntaxKind.OpenParenToken &&
            kindAt(i + 2) === SyntaxKind.StringLiteral
        )
            scripts.push({ name: textAt(i + 2).slice(1, -1), start: i, end: match[i + 1]! });
        if (
            (tok.kind === SyntaxKind.WhileKeyword || tok.kind === SyntaxKind.ForKeyword) &&
            kindAt(i + 1) === SyntaxKind.OpenParenToken
        ) {
            const close = match[i + 1]!;
            // a do-while's tail is not a loop of its own
            if (tok.kind === SyntaxKind.WhileKeyword && kindAt(i - 1) === SyntaxKind.CloseBraceToken) {
                const open = match[i - 1]!;
                if (kindAt(open - 1) === SyntaxKind.DoKeyword) continue;
            }
            if (close > 0 && kindAt(close + 1) === SyntaxKind.OpenBraceToken)
                loops.push({
                    kind: tok.kind === SyntaxKind.WhileKeyword ? "while" : "for",
                    start: close + 1,
                    end: match[close + 1]!,
                    cond: [i + 2, close],
                    line: lineOf(tok.start),
                });
        }
        if (tok.kind === SyntaxKind.DoKeyword && kindAt(i + 1) === SyntaxKind.OpenBraceToken) {
            const end = match[i + 1]!;
            const condOpen = end + 2;
            loops.push({
                kind: "do",
                start: i + 1,
                end,
                cond: kindAt(end + 1) === SyntaxKind.WhileKeyword ? [condOpen + 1, match[condOpen]!] : [end, end],
                line: lineOf(tok.start),
            });
        }
    }
    const scriptAt = (i: number) => scripts.find((s) => s.start <= i && i <= s.end)?.name ?? "";
    const innermostLoop = (i: number): Loop | undefined => {
        let best: Loop | undefined;
        for (const loop of loops) if (loop.start < i && i < loop.end && (!best || loop.start > best.start)) best = loop;
        return best;
    };

    const findings: Finding[] = [];
    const add = (i: number, pattern: Pattern, cls: FrameClass, why: string) =>
        findings.push({
            file,
            line: lineOf(t[i]!.start),
            script: scriptAt(i),
            pattern,
            cls,
            why,
            text: lineText(t[i]!.start),
        });

    // Top-level arguments of a call: [start, end) token ranges.
    const argsOf = (open: number): [number, number][] => {
        const close = match[open]!;
        const out: [number, number][] = [];
        let from = open + 1;
        for (let i = open + 1; i < close; i++) {
            if (OPENERS.has(t[i]!.kind)) i = match[i]!;
            else if (t[i]!.kind === SyntaxKind.CommaToken) {
                out.push([from, i]);
                from = i + 1;
            }
        }
        if (from < close) out.push([from, close]);
        return out;
    };
    // N.<name> flags anywhere in a range (lower case).
    const flagsIn = ([from, to]: [number, number]): string[] => {
        const out: string[] = [];
        for (let i = from; i < to - 2; i++)
            if (textAt(i) === "N" && kindAt(i + 1) === SyntaxKind.DotToken && kindAt(i + 2) === SyntaxKind.Identifier)
                out.push(lower(i + 2));
        return out;
    };
    const numberIn = ([from, to]: [number, number]): number | undefined => {
        let sign = 1,
            a = from,
            b = to;
        if (kindAt(a) === SyntaxKind.MinusToken) {
            sign = -1;
            a++;
        }
        // f(2) - a float literal
        if (textAt(a) === "f" && kindAt(a + 1) === SyntaxKind.OpenParenToken && match[a + 1] === b - 1) {
            a += 2;
            b -= 1;
        }
        return b - a === 1 && kindAt(a) === SyntaxKind.NumericLiteral ? sign * Number(textAt(a)) : undefined;
    };
    // A call: identifier ( - the callee's name (the last part of a.b.c) and its (.
    const isCall = (i: number) =>
        kindAt(i) === SyntaxKind.Identifier &&
        kindAt(i + 1) === SyntaxKind.OpenParenToken &&
        kindAt(i - 1) !== SyntaxKind.FunctionKeyword;

    const tickWaits = new Map<Loop, TickWait[]>();
    const noteTickWait = (i: number, unit: TickWait["unit"]) => {
        const loop = innermostLoop(i);
        if (!loop) return;
        let list = tickWaits.get(loop);
        if (!list) {
            list = [];
            tickWaits.set(loop, list);
        }
        list.push({ index: i, unit });
    };

    for (let i = 0; i < t.length; i++) {
        if (!isCall(i)) continue;
        const name = lower(i);
        const open = i + 1;
        if (name === "wait" && kindAt(i - 1) !== SyntaxKind.DotToken) {
            const args = argsOf(open);
            const flags = args.slice(1).flatMap(flagsIn);
            const first = args[0] ? flagsIn(args[0]) : [];
            const count = args[0] ? numberIn(args[0]) : undefined;
            const countText = count === undefined ? "a value" : String(count);
            const realTime = flags.includes("real_time") || flags.includes("realtime");
            if (args.length === 1 && kindAt(args[0]![0]) === SyntaxKind.DotDotDotToken) {
                // Wait(...random(k, [[3, N.seconds], [5, N.frames]...])) - one of several waits
                const units = first;
                if (units.some((f) => UNIT_GAME.has(f)))
                    add(i, "wait-gameframes", "b", "a random choice of Update counts");
                else if (units.some((f) => UNIT_SECONDS.has(f)))
                    add(i, "wait-seconds", "c", "a random choice of times");
                else if (units.some((f) => UNIT_FRAMES.has(f)))
                    add(i, "wait-frames", "c", "a random choice of 16.67 ms frames of game time");
                else add(i, "wait-ms", "c", "a random choice of milliseconds");
            } else if (flags.some((f) => UNIT_SECONDS.has(f))) {
                add(i, "wait-seconds", "c", "seconds of game time");
                noteTickWait(i, "frames");
            } else if (flags.some((f) => UNIT_GAME.has(f))) {
                const inLoop = innermostLoop(i);
                if (count === 1 || count === 0) {
                    add(
                        i,
                        "wait-gameframes",
                        "c",
                        inLoop ? "one Update a pass: polls each tick" : "goes on next Update",
                    );
                } else if (count !== undefined) {
                    add(i, "wait-gameframes", "a", `${count} Updates (${(count / 60).toFixed(4)} s at 60 Hz)`);
                } else {
                    add(i, "wait-gameframes", "b", "a count of Updates from a value");
                }
                if (count === 1) noteTickWait(i, "game");
            } else if (flags.some((f) => UNIT_FRAMES.has(f))) {
                add(i, "wait-frames", "c", `${countText} x 16.67 ms of game time (the Wait command)`);
                if (count === 1) noteTickWait(i, "frames");
            } else if (flags.includes(UNIT_ONE_PER_FRAME)) {
                add(i, "wait-one-per-frame", "c", "one script a frame: a per-frame budget");
            } else if (realTime) {
                add(i, "wait-real-time", "c", "wall-clock milliseconds");
            } else if (args.length === 1 && first.length === 0) {
                add(i, "wait-ms", "c", "milliseconds of game time");
            } else {
                add(i, "wait-other", "b", `unit not recognised: ${flags.join(", ") || "none"}`);
            }
            continue;
        }
        const command = FRAME_COMMANDS[name];
        if (command) {
            if (name === "waitonegameframe") {
                add(
                    i,
                    "wait-one-game-frame",
                    "c",
                    innermostLoop(i) ? "one Update a pass: polls each tick" : command.why,
                );
                noteTickWait(i, "game");
            } else {
                add(i, "frame-command", command.cls, command.why);
                if (command.cls === "c") noteTickWait(i, "call");
            }
        }
    }

    // Frame-named keys (object literals) and flags (outside Wait).
    for (let i = 0; i < t.length; i++) {
        if (kindAt(i) !== SyntaxKind.Identifier || kindAt(i + 1) !== SyntaxKind.ColonToken) continue;
        const name = lower(i);
        if (!name.includes("frame") || NOT_TIME_KEY.test(name)) continue;
        const known = FRAME_KEYS[name];
        add(i, "frame-argument", known?.cls ?? "b", known?.why ?? "a frame-named value: its consumer decides");
    }

    // Loops with a wait of one tick (or one 16.67 ms frame).
    const counted = new Set<number>();
    for (const [loop, waits] of tickWaits) {
        const game = waits.some((w) => w.unit !== "frames");
        const [cFrom, cTo] = loop.cond;
        // repeat N: do { ... } while (loop.again(N))
        const again = (() => {
            for (let i = cFrom; i < cTo; i++)
                if (textAt(i) === "again" && kindAt(i + 1) === SyntaxKind.OpenParenToken) return i + 1;
            return -1;
        })();
        if (again >= 0) {
            const args = argsOf(again);
            const n = args[0] ? numberIn(args[0]) : undefined;
            const at = loop.start - 1;
            if (args.length === 0) {
                // repeat for ever: a polling loop, counted below with its counters
            } else if (!game) {
                add(at, "frame-loop", "c", `repeat ${n ?? "a value"} x a timed wait: already time`);
            } else if (n !== undefined) {
                add(
                    at,
                    "frame-loop",
                    "a",
                    `repeat ${n} with a one-tick wait: ${n} ticks (${(n / 60).toFixed(4)} s at 60 Hz)`,
                );
            } else {
                add(at, "frame-loop", "b", "repeat a value of times with a one-tick wait: a tick count");
            }
        }
        // counters and accumulations in the body: p.x = add(p.x, ...) / sub(...)
        for (let i = loop.start; i < loop.end; i++) {
            if (innermostLoop(i) !== loop) continue;
            if (
                !(
                    textAt(i) === "p" &&
                    kindAt(i + 1) === SyntaxKind.DotToken &&
                    kindAt(i + 2) === SyntaxKind.Identifier &&
                    kindAt(i + 3) === SyntaxKind.EqualsToken
                )
            )
                continue;
            const target = textAt(i + 2);
            const op = textAt(i + 4);
            if (!["add", "sub", "mul"].includes(op) || kindAt(i + 5) !== SyntaxKind.OpenParenToken) continue;
            const args = argsOf(i + 5);
            const self = args.some(([a, b]) => b - a === 3 && textAt(a) === "p" && textAt(a + 2) === target);
            if (!self) continue;
            const other = args.find(([a, b]) => !(b - a === 3 && textAt(a) === "p" && textAt(a + 2) === target));
            const step = other ? numberIn(other) : undefined;
            if (counted.has(i)) continue;
            counted.add(i);
            const integerStep = step !== undefined && Math.abs(step) === 1 && op !== "mul"; // a count, not a wrap (+-360) or a rate
            const compared = (() => {
                // compared somewhere in the loop or its condition
                for (let j = Math.min(cFrom, loop.start); j < Math.max(cTo, loop.end); j++)
                    if (
                        ["eq", "gt", "lt", "ge", "le", "ne"].includes(textAt(j)) &&
                        kindAt(j + 1) === SyntaxKind.OpenParenToken &&
                        text.slice(t[j]!.start, t[match[j + 1]!]!.end).includes(`p.${target}`)
                    )
                        return true;
                return false;
            })();
            if (integerStep && compared) {
                add(
                    i,
                    "frame-counter",
                    game ? "a" : "c",
                    game
                        ? `p.${target} counts ticks (${op} ${step}) and is compared: a timer in ticks`
                        : `p.${target} counts timed waits: already time`,
                );
            } else if (integerStep) {
                add(i, "frame-counter", game ? "b" : "c", `p.${target} ${op} ${step} each pass, not compared here`);
            } else {
                add(
                    i,
                    "per-tick-increment",
                    game ? "b" : "c",
                    game
                        ? `p.${target} = ${op}(..) each tick: a per-tick rate`
                        : `p.${target} = ${op}(..) each timed pass: rate fixed by the wait`,
                );
            }
        }
    }
    findings.sort((x, y) => x.line - y.line);
    return findings;
}

// Scripts under a folder: *.ts, without node_modules and the game's own made folders.
export function scriptFiles(path: string): string[] {
    if (!existsSync(path)) return [];
    if (!statSync(path).isDirectory()) return path.toLowerCase().endsWith(".ts") ? [path] : [];
    return readdirSync(path)
        .sort()
        .filter((name) => name !== "node_modules" && !name.startsWith("."))
        .flatMap((name) => scriptFiles(join(path, name)));
}

// The game folder's scan roots: Data, and with overlays the data overlays (not the made caches / backups).
export function gameScanRoots(folder: string, overlays: boolean): string[] {
    const root = existsSync(join(folder, "Data", "scripts")) ? folder : join(folder, "Game");
    if (!existsSync(join(root, "Data"))) throw Error(`${folder}: no Data - not a game folder`);
    const roots = [join(root, "Data")];
    if (overlays)
        for (const name of readdirSync(root).sort()) {
            const lowerName = name.toLowerCase();
            if (
                lowerName.startsWith("thug2-decomp-") &&
                !["thug2-decomp-ts", "thug2-decomp-backup", "thug2-decomp-previous"].includes(lowerName) &&
                statSync(join(root, name)).isDirectory()
            )
                roots.push(join(root, name));
        }
    return roots;
}

const PATTERNS: Pattern[] = [
    "wait-seconds",
    "wait-ms",
    "wait-frames",
    "wait-gameframes",
    "wait-one-per-frame",
    "wait-real-time",
    "wait-other",
    "wait-one-game-frame",
    "frame-command",
    "frame-loop",
    "frame-counter",
    "per-tick-increment",
    "frame-argument",
];

export function report(findings: Finding[], files: number, all: boolean): string {
    const lines: string[] = [];
    lines.push("# Script frame audit", "");
    lines.push(
        `Generated by \`node tools/qbjs/frame_audit.ts\` over ${files} script files. Classes: **a** mechanical (ticks meant as time:`,
        "N -> N/60 s), **b** needs judgement, **c** fine as is (already time, or per-tick bookkeeping).",
        "",
        "| pattern | a | b | c | total |",
        "|---|---:|---:|---:|---:|",
    );
    const totals = { a: 0, b: 0, c: 0 };
    for (const pattern of PATTERNS) {
        const of = findings.filter((f) => f.pattern === pattern);
        if (!of.length) continue;
        const n = (cls: FrameClass) => of.filter((f) => f.cls === cls).length;
        totals.a += n("a");
        totals.b += n("b");
        totals.c += n("c");
        lines.push(`| ${pattern} | ${n("a")} | ${n("b")} | ${n("c")} | ${of.length} |`);
    }
    lines.push(`| **all** | ${totals.a} | ${totals.b} | ${totals.c} | ${findings.length} |`, "");
    // Reasons (grouped)
    lines.push("## Why, by count", "", "| class | pattern | why | count |", "|---|---|---|---:|");
    const reasons = new Map<string, number>();
    for (const f of findings) {
        const key = `${f.cls}\u0000${f.pattern}\u0000${f.why.replace(/-?\d+(\.\d+)?/g, "#")}`;
        reasons.set(key, (reasons.get(key) ?? 0) + 1);
    }
    for (const [key, count] of [...reasons].sort((x, y) => (x[0] < y[0] ? -1 : 1))) {
        const [cls, pattern, why] = key.split("\u0000");
        lines.push(`| ${cls} | ${pattern} | ${why} | ${count} |`);
    }
    lines.push("");
    for (const cls of all ? (["a", "b", "c"] as const) : (["a", "b"] as const)) {
        const of = findings.filter((f) => f.cls === cls);
        lines.push(`## Class ${cls} (${of.length})`, "");
        let file = "";
        for (const f of of.sort((x, y) => (x.file === y.file ? x.line - y.line : x.file < y.file ? -1 : 1))) {
            if (f.file !== file) {
                file = f.file;
                lines.push("", `### ${file}`, "");
            }
            lines.push(`- ${f.line} \`${f.script}\` ${f.pattern}: ${f.why} - \`${f.text.replace(/`/g, "'")}\``);
        }
        lines.push("");
    }
    return `${lines.join("\n")}\n`;
}

const USAGE =
    "Usage: node tools/qbjs/frame_audit.ts (--game <folder> [--overlays] | <file.ts | folder>...) [--out report.md] [--json out.json] [--all]";

async function main(): Promise<number> {
    const { values, positionals } = parseCli(USAGE, {
        options: {
            game: { type: "string" },
            overlays: { type: "boolean" },
            out: { type: "string" },
            json: { type: "string" },
            all: { type: "boolean" },
        },
        allowPositionals: true,
    });
    const roots = values.game ? gameScanRoots(values.game, values.overlays ?? false) : positionals;
    if (!roots.length) throw Error(USAGE);
    const base = values.game ?? process.cwd();
    const findings: Finding[] = [];
    let files = 0;
    for (const root of roots)
        for (const file of scriptFiles(root)) {
            files++;
            findings.push(...analyzeSource(readFileSync(file, "utf8"), relative(base, file).replace(/\\/g, "/")));
        }
    const text = report(findings, files, values.all ?? false);
    if (values.out) writeFileSync(values.out, text);
    else process.stdout.write(text.split("\n## Why")[0]!);
    if (values.json) writeFileSync(values.json, `${JSON.stringify(findings, null, 1)}\n`);
    return 0;
}

if (isMain(import.meta.url)) await runMain(main);
