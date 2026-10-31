// NOT ORIGINAL - which of the GameSpy SDK's functions in the reference build the game can ever reach: a conservative
// live set (anything doubtful counts as live), so every piece left out of the decompilation is provably never run.
//   node tools/gamespy_reach.ts [--exe <reference build>] [--verbose] [--why <address>]
// Writes data/gamespy_reach.csv and prints a summary (--verbose: the pieces only data words keep live; --why: the
// chain of first references from a root to a piece). Needs uv (capstone is fetched by uv run).
//
// Pieces: the SDK's range is cut at every Ghidra function start, so every byte belongs to some piece. Each piece is
// decoded from its start (capstone, tools/capstone_refs.py) by recursive descent within the piece, following
// branches and switch jump tables; what the descent never reaches after its last byte is split off - padding
// (CC / 90), the 5-byte filler, or a piece of its own (a function Ghidra never made, or a data table the code points
// at), decoded the same way. Holes inside a piece (unreached bytes before its end) are swept linearly.
// References into a piece make it live (where they may land: the closure's rules below):
//   roots, from outside the range - the decoded instructions of every Ghidra function in .text outside it and of the
//   gaps between them (branch targets, immediates, displacements), every rel32 E8 / E9 / 0F 8x and rel8 EB / 7x / E0-E3 at every byte offset
//   of .text outside it, every dword at every byte offset of .text outside it and of every other section, and the
//   PE entry point (the exe has no exports, no relocation table; its TLS directory is in .rdata, scanned);
//   from a live piece - its descent's branch / call targets, immediates and displacements, falling off its end
//   into the next piece, its jump tables' entries, its holes' linear sweep, and every dword at every byte offset
//   of the piece (belt and braces).
// "strong" references are the decoded ones and aligned data words; "weak" ones the byte-offset scans. The live set
// is the fixpoint over both; each piece's reason is the first reference that reached it (strong ones first).

import { spawnSync } from "node:child_process";
import * as fs from "node:fs";
import { Image } from "./code_coverage.ts";
import { REFERENCE_EXE } from "./pipeline/lib/decomp.ts";
import { isMain, print, runMain } from "./pipeline/lib/py.ts";

export const GAMESPY_RANGE: readonly [number, number] = [0x5f8160, 0x622e80];
const [LO, HI] = GAMESPY_RANGE;
const inRange = (a: number): boolean => a >= LO && a < HI;
const hex = (n: number): string => `0x${n.toString(16).padStart(8, "0")}`;
const isPad = (b: number | undefined): boolean => b === 0xcc || b === 0x90;

// The SDK's modules, by address (the linker kept each object file's functions together; boundaries read from the
// strings, imports and protocol keywords the functions use - approximate where a module has none).
const COMPONENTS: readonly (readonly [number, string])[] = [
    [0x5f8160, "gsi platform / common (sockets, registry, random)"],
    [0x5f8660, "availability check"],
    [0x5f8950, "cdkey / challenge hashing"],
    [0x5f8a90, "natneg (NAT negotiation)"],
    [0x5fa340, "qr2 (query and reporting 2)"],
    [0x5fc010, "natneg (NAT negotiation)"],
    [0x5fc880, "chat (peerchat IRC client)"],
    [0x5fefc0, "qr2 / queryreporting keys"],
    [0x5ffb90, "ghttp (motd)"],
    [0x5ffd90, "GP (presence) interface"],
    [0x600640, "cdkey / challenge hashing"],
    [0x600780, "gstats (gamestats)"],
    [0x6027e0, "md5 / common"],
    [0x604d00, "peer"],
    [0x610030, "chat socket"],
    [0x612040, "serverbrowsing"],
    [0x6142a0, "ghttp (HTTP)"],
    [0x616660, "GP (presence)"],
    [0x61d040, "GP file transfer"],
    [0x61eca0, "GP peer messaging"],
    [0x620120, "queryreporting (qr v1) / pinger"],
    [0x622a50, "GP (presence)"],
];
const componentOf = (a: number): string => {
    let name = COMPONENTS[0]![1];
    for (const [at, n] of COMPONENTS) if (a >= at) name = n;
    return name;
};

interface Fn {
    addr: number;
    end: number;
    name: string;
    callers: number[];
    callees: number[];
    ptr_from: number[];
}
interface Ref {
    to: number;
    from: number;
    kind: string;
    strong: boolean;
}
type Kind = "function" | "missed" | "data" | "padding" | "filler";
// The 5 bytes that follow many functions (linker filler, not code - as in code_coverage.ts).
const FILLER = [0x52, 0x4c, 0x44, 0x21, 0x00];
interface Unit {
    start: number;
    end: number;
    kind: Kind;
    name: string;
    refs: Ref[];
    notes: string[];
    live: boolean;
    via: "" | "strong" | "weak";
    why: Ref | undefined;
}

// capstone_refs.py's decode of every byte offset of the range.
interface Decoded {
    size: Uint8Array;
    flow: string[];
    target: Int32Array; // -1: none
    values: (number[] | undefined)[];
}
const UV = ["run", "--no-project", "--python", "3.13", "--with", "capstone", "python", "-I", "tools/capstone_refs.py"];
function runCapstone(args: string[], exe: string, input?: string): string {
    const r = spawnSync("uv", [...UV, ...args, exe], {
        encoding: "utf-8",
        maxBuffer: 1 << 30,
        ...(input !== undefined ? { input } : {}),
    });
    if (r.status !== 0) throw new Error(`capstone_refs.py failed: ${r.stderr}`);
    return r.stdout;
}
function decodeRange(exe: string): Decoded {
    const n = HI - LO;
    const d: Decoded = { size: new Uint8Array(n), flow: [], target: new Int32Array(n).fill(-1), values: [] };
    for (const line of runCapstone(["every", LO.toString(16), HI.toString(16)], exe).split("\n")) {
        const p = line.trim().split(" ");
        if (p.length < 5) continue;
        const i = parseInt(p[0]!, 16) - LO;
        d.size[i] = parseInt(p[1]!, 10);
        d.flow[i] = p[2]!;
        if (p[3] !== "-") d.target[i] = parseInt(p[3]!, 16);
        if (p[4] !== "-") d.values[i] = p[4]!.split(",").map((x) => parseInt(x, 16));
    }
    return d;
}

function main(): number {
    const args = process.argv.slice(2);
    const exeAt = args.indexOf("--exe");
    const exe = exeAt >= 0 ? args[exeAt + 1]! : REFERENCE_EXE;
    const verbose = args.includes("--verbose");
    if (!exe) throw new RangeError("no reference build: pass --exe or set referenceExe in local.json");
    const file = fs.readFileSync(exe);
    const img = new Image(file);
    const sections = img.sectionBytes();
    const text = sections.find((s) => s.name === ".text")!;
    const byte = (a: number): number | undefined => img.byte(a);
    const dword = (a: number): number => img.dword(a);
    const pe = file.readUInt32LE(0x3c);
    const entry = file.readUInt32LE(pe + 24 + 16) + file.readUInt32LE(pe + 24 + 28);

    const funcs = JSON.parse(fs.readFileSync("data/derived/functions.json", "utf-8")) as Fn[];
    const fnAt = new Map(funcs.map((f) => [f.addr, f]));
    const starts = [...new Set([LO, ...funcs.map((f) => f.addr).filter(inRange)])].sort((a, b) => a - b);
    const dec = decodeRange(exe);

    // --- Pieces ---
    const units: Unit[] = [];
    const doubts: string[] = [];
    const holes = new Uint8Array(HI - LO);
    const newUnit = (start: number, end: number, kind: Kind): Unit => {
        const u: Unit = {
            start,
            end,
            kind,
            name: fnAt.get(start)?.name ?? "",
            refs: [],
            notes: [],
            live: false,
            via: "",
            why: undefined,
        };
        units.push(u);
        return u;
    };
    const ref = (u: Unit, to: number, from: number, kind: string, strong: boolean): void => {
        if (inRange(to)) u.refs.push({ to, from, kind, strong });
    };
    // Every dword at every byte offset of [from, to): weak references.
    const dwordScan = (u: Unit, from: number, to: number): void => {
        for (let a = from; a + 4 <= to; a++) ref(u, dword(a), a, "dword", false);
    };
    // The references of the instruction decoded at a.
    const insnRefs = (u: Unit, a: number, strong: boolean): void => {
        const i = a - LO;
        const t = dec.target[i]!;
        const f = dec.flow[i];
        if (t >= 0) ref(u, t, a, f === "c" ? "call" : f === "j" ? "jmp" : "jcc", strong);
        for (const v of dec.values[i] ?? []) ref(u, v, a, "operand", strong);
    };

    // Decode the piece [s, e) from s; split off what the descent never reaches after its last byte.
    const processPiece = (s: number, e: number, kind: Kind): void => {
        const covered = new Uint8Array(e - s);
        const seen = new Uint8Array(e - s);
        const u = newUnit(s, e, kind); // (end fixed below)
        const pending: Ref[] = [];
        const stack = [s];
        const operandsInside: number[] = [];
        while (stack.length) {
            let a = stack.pop()!;
            while (a >= s && a < e && !seen[a - s]) {
                seen[a - s] = 1;
                const i = a - LO;
                const size = dec.size[i]!;
                if (size === 0) {
                    covered[a - s] = 1;
                    u.notes.push(`undecodable at ${hex(a)}`);
                    doubts.push(`${hex(s)}: undecodable byte at ${hex(a)}`);
                    break;
                }
                for (let k = 0; k < size && a + k < e; k++) covered[a + k - s] = 1;
                const flow = dec.flow[i]!;
                const t = dec.target[i]!;
                insnRefs(u, a, true);
                if (t >= s && t < e) stack.push(t);
                for (const v of dec.values[i] ?? []) if (v >= s && v < e) operandsInside.push(v);
                if (flow === "j" && t < 0) {
                    // jmp [table + reg*4]: the table's entries pointing into the piece are more code.
                    for (const v of dec.values[i] ?? []) {
                        if (!inRange(v)) continue;
                        for (let p = v; ; p += 4) {
                            const entryTarget = dword(p);
                            if (!(entryTarget >= s && entryTarget < e)) break;
                            stack.push(entryTarget);
                            pending.push({ to: entryTarget, from: p, kind: "jump table", strong: true });
                            for (let k = 0; k < 4; k++) if (p + k >= s && p + k < e) covered[p + k - s] = 1;
                        }
                    }
                }
                if (flow === "r" || flow === "j" || flow === "s") break;
                const next = a + size;
                if (next >= e) {
                    pending.push({ to: next, from: a, kind: "falls through", strong: true });
                    break;
                }
                a = next;
            }
        }
        let last = s;
        for (let a = s; a < e; a++) if (covered[a - s]) last = a + 1;
        u.end = Math.max(last, s + 1);
        u.refs.push(...pending);
        // Holes: swept linearly (weak).
        for (let a = s; a < u.end; ) {
            if (covered[a - s] || isPad(byte(a))) {
                a++;
                continue;
            }
            const size = dec.size[a - LO]!;
            if (size === 0) {
                a++;
                continue;
            }
            insnRefs(u, a, false);
            a += size;
        }
        for (let a = s; a < u.end; a++) if (!covered[a - s] && !isPad(byte(a))) holes[a - LO] = 1;
        dwordScan(u, u.start, u.end);
        // The rest: padding, or pieces of their own.
        let a = u.end;
        while (a < e) {
            let b = a;
            if (isPad(byte(a))) {
                while (b < e && isPad(byte(b))) b++;
                newUnit(a, b, "padding");
            } else if (a + 5 <= e && FILLER.every((x, i) => byte(a + i) === x)) {
                b = a + 5;
                newUnit(a, b, "filler").refs.length = 0;
            } else {
                while (b < e && !isPad(byte(b))) b++;
                const isData = operandsInside.some((v) => v >= a && v < b);
                processPiece(a, e, isData ? "data" : "missed");
                return;
            }
            a = b;
        }
    };
    for (let k = 0; k < starts.length; k++) {
        const s = starts[k]!;
        processPiece(s, starts[k + 1] ?? HI, fnAt.has(s) ? "function" : "missed");
    }
    units.sort((x, y) => x.start - y.start);
    for (const u of units) u.refs = u.refs.filter((r) => r.to < u.start || r.to >= u.end);
    const unitStarts = units.map((u) => u.start);
    const unitAt = (a: number): Unit | undefined => {
        let lo = 0;
        let hi = unitStarts.length;
        while (lo < hi) {
            const mid = (lo + hi) >> 1;
            if (unitStarts[mid]! <= a) lo = mid + 1;
            else hi = mid;
        }
        const u = units[lo - 1];
        return u && a < u.end ? u : undefined;
    };

    // --- Roots ---
    const roots: Ref[] = [];
    const root = (to: number, from: number, kind: string, strong: boolean): void => {
        if (inRange(to)) roots.push({ to, from, kind, strong });
    };
    root(entry, entry, "PE entry", true);
    // Decoded: every Ghidra function outside the range, swept linearly over its (clipped) extent, and every gap
    // between them that isn't all padding (code Ghidra made no function for - some of the game's own).
    const allStarts = funcs.map((f) => f.addr).sort((a, b) => a - b);
    const sweep: string[] = [];
    const inFunction = new Uint8Array(text.data.length);
    for (let k = 0; k < allStarts.length; k++) {
        const f = fnAt.get(allStarts[k]!)!;
        if (inRange(f.addr)) continue;
        let end = Math.min(f.end + 1, allStarts[k + 1] ?? f.end + 1);
        if (f.addr < LO) end = Math.min(end, LO);
        if (end <= f.addr) continue;
        sweep.push(`${f.addr.toString(16)} ${end.toString(16)}`);
        inFunction.fill(1, f.addr - text.va, end - text.va);
    }
    const textEnd = text.va + text.data.length;
    for (let a = text.va; a < textEnd; ) {
        if (inFunction[a - text.va] || inRange(a)) {
            a++;
            continue;
        }
        let b = a;
        let real = false;
        while (b < textEnd && !inFunction[b - text.va] && !inRange(b)) real = !isPad(byte(b++)) || real;
        if (real) sweep.push(`${a.toString(16)} ${b.toString(16)}`);
        a = b;
    }
    for (const line of runCapstone(["sweep", LO.toString(16), HI.toString(16)], exe, sweep.join("\n")).split("\n")) {
        const p = line.trim().split(" ");
        if (p.length !== 3) continue;
        const from = parseInt(p[0]!, 16);
        const where = inFunction[from - text.va] ? "code" : "code (no Ghidra function)";
        root(parseInt(p[2]!, 16), from, `${where} ${p[1]}`, true);
    }
    // Raw: every byte offset of .text outside the range, and every other section.
    const t = text.data;
    for (let off = 0; off + 1 < t.length; off++) {
        const a = text.va + off;
        if (inRange(a)) continue;
        const op = t[off]!;
        if (off + 4 <= t.length) root(t.readUInt32LE(off), a, "code bytes: dword", false);
        if ((op === 0xe8 || op === 0xe9) && off + 5 <= t.length)
            root((a + 5 + t.readInt32LE(off + 1)) >>> 0, a, `code bytes: rel32 ${op.toString(16)}`, false);
        if (op === 0x0f && t[off + 1]! >= 0x80 && t[off + 1]! <= 0x8f && off + 6 <= t.length)
            root((a + 6 + t.readInt32LE(off + 2)) >>> 0, a, "code bytes: rel32 jcc", false);
        if (op === 0xeb || (op >= 0x70 && op <= 0x7f) || (op >= 0xe0 && op <= 0xe3))
            root((a + 2 + ((t[off + 1]! << 24) >> 24)) >>> 0, a, "code bytes: rel8", false);
    }
    for (const s of sections) {
        if (s.name === ".text") continue;
        for (let off = 0; off + 4 <= s.data.length; off++) {
            const aligned = (s.va + off) % 4 === 0;
            root(s.data.readUInt32LE(off), s.va + off, `${s.name}${aligned ? "" : " unaligned"}`, aligned);
        }
    }
    // Falling into the range from the code before it.
    let before = LO - 1;
    while (isPad(byte(before))) before--;
    const boundaryNote = `the last non-padding byte before the range is ${hex(before)} (${byte(before)!.toString(16)})`;

    // --- Closure: strong references first, then the weak ones ---
    // Two rules. "any address": every reference into a piece counts, wherever it lands. "entry": branches (call /
    // jmp / jcc / falling through / jump-table entries, decoded or raw rel32 / rel8) count wherever they land, but a
    // pointer (a data word, an instruction's immediate / displacement, a byte-offset dword) only when it lands on a
    // piece's first byte or in a hole (bytes the piece's descent never reached): compiled C takes the address of a
    // function, never of a spot inside one, so a pointer into the middle of a function can only start running code
    // that is running already (an exception handler's scope table) - or it is a number that happens to look like one.
    const isPointer = (kind: string): boolean =>
        kind === "operand" || kind === "dword" || kind === "code bytes: dword" || kind.startsWith(".");
    const closure = (anyAddress: boolean, codeRootsOnly = false): Map<Unit, { why: Ref; weak: boolean }> => {
        const live = new Map<Unit, { why: Ref; weak: boolean }>();
        const pass = (useWeak: boolean): void => {
            const work: Ref[] = roots.filter(
                (r) => (r.strong || useWeak) && !(codeRootsOnly && r.kind.startsWith(".")),
            );
            for (const u of live.keys()) work.push(...u.refs.filter((r) => r.strong || useWeak));
            for (let k = 0; k < work.length; k++) {
                const r = work[k]!;
                const u = unitAt(r.to);
                if (!u || live.has(u)) continue;
                if (!anyAddress && isPointer(r.kind) && r.to !== u.start && !holes[r.to - LO]) continue;
                live.set(u, { why: r, weak: useWeak });
                for (const x of u.refs) if (x.strong || useWeak) work.push(x);
            }
        };
        pass(false);
        pass(true);
        return live;
    };
    const anyAddressLive = closure(true);
    const codeRootsLive = closure(false, true); // (for the summary: what only a data table keeps live)
    for (const [u, w] of closure(false)) {
        u.live = true;
        u.via = w.weak ? "weak" : "strong";
        u.why = w.why;
    }

    // --- The naive direct-call closure (Ghidra's callers / callees), for comparison ---
    const rangeFns = funcs.filter((f) => inRange(f.addr));
    const naive = new Set<number>();
    const nWork = rangeFns.filter((f) => f.callers.some((c) => !inRange(c))).map((f) => f.addr);
    const naiveRoots = nWork.length;
    while (nWork.length) {
        const a = nWork.pop()!;
        if (naive.has(a)) continue;
        naive.add(a);
        for (const c of fnAt.get(a)?.callees ?? []) if (inRange(c) && fnAt.has(c)) nWork.push(c);
    }

    // --- Output ---
    const rows = ["start,end,size,kind,component,name,live,via,why_kind,why_from,live_any_address,first_bytes,note"];
    for (const u of units) {
        const first: string[] = [];
        for (let a = u.start; a < Math.min(u.end, u.start + 8); a++) first.push(byte(a)!.toString(16).padStart(2, "0"));
        const notes = [...u.notes];
        if (!u.live && u.kind !== "padding" && u.kind !== "filler" && u.end - u.start < 5)
            notes.push("shorter than 5 bytes");
        rows.push(
            [
                hex(u.start),
                hex(u.end),
                u.end - u.start,
                u.kind,
                `"${componentOf(u.start)}"`,
                u.name,
                u.live ? "yes" : "no",
                u.via,
                u.why ? u.why.kind : u.kind === "padding" || u.kind === "filler" ? "" : "no reference",
                u.why ? hex(u.why.from) : "",
                anyAddressLive.has(u) ? "yes" : "no",
                u.live ? "" : first.join(" "),
                notes.join("; "),
            ].join(","),
        );
    }
    fs.writeFileSync("data/gamespy_reach.csv", `${rows.join("\n")}\n`);

    const code = units.filter((u) => u.kind !== "padding" && u.kind !== "filler");
    const sum = (us: Unit[]): number => us.reduce((n, u) => n + u.end - u.start, 0);
    const live = code.filter((u) => u.live);
    const dead = code.filter((u) => !u.live);
    const pads = units.filter((u) => u.kind === "padding" || u.kind === "filler");
    print(`GameSpy SDK ${hex(LO)}-${hex(HI)}: ${HI - LO} bytes in ${units.length} pieces`);
    print(
        `  live: ${live.length} pieces, ${sum(live)} bytes (${live.filter((u) => u.via === "weak").length} only via weak references)`,
    );
    print(`  dead: ${dead.length} pieces, ${sum(dead)} bytes`);
    const anyLive = code.filter((u) => anyAddressLive.has(u));
    print(`  (with pointers into the middle of a piece counted too: ${anyLive.length} live, ${sum(anyLive)} bytes)`);
    const dataOnly = live.filter((u) => !codeRootsLive.has(u));
    print(`  live only through the data sections' words: ${dataOnly.length} pieces, ${sum(dataOnly)} bytes`);
    if (verbose)
        for (const u of dataOnly) print(`    ${hex(u.start)} ${u.name}: ${u.why!.kind} at ${hex(u.why!.from)}`);
    print(
        `  padding / filler: ${pads.length} runs, ${sum(pads)} bytes (${pads.filter((u) => u.live).length} referenced)`,
    );
    for (const k of ["function", "missed", "data"] as const) {
        const us = code.filter((u) => u.kind === k);
        print(`  ${k}: ${us.length} (${us.filter((u) => u.live).length} live)`);
    }
    for (const u of code.filter((x) => x.kind === "missed"))
        print(
            `    missed function ${hex(u.start)}-${hex(u.end)} (${u.end - u.start} bytes) live=${u.live ? "yes" : "no"}`,
        );
    print("by component (pieces live / all, bytes live / all):");
    const comps = [...new Set(COMPONENTS.map((c) => c[1]))];
    for (const c of comps) {
        const us = code.filter((u) => componentOf(u.start) === c);
        print(
            `  ${c}: ${us.filter((u) => u.live).length} / ${us.length}, ${sum(us.filter((u) => u.live))} / ${sum(us)}`,
        );
    }
    const liveFns = new Set(code.filter((u) => u.live && u.kind === "function").map((u) => u.start));
    const deadFns = code.filter((u) => !u.live && u.kind === "function");
    print(`naive direct-call closure: ${naiveRoots} roots, ${naive.size} of ${rangeFns.length} functions reached`);
    const extra = [...liveFns].filter((a) => !naive.has(a));
    const byKind = new Map<string, number>();
    for (const a of extra) {
        const k = unitAt(a)!.why!.kind.replace(/^\.\w+( unaligned)?$/, "data$1");
        byKind.set(k, (byKind.get(k) ?? 0) + 1);
    }
    print(
        `  live here, not reached by it: ${extra.length} (first reference: ${[...byKind].map(([k, n]) => `${k} ${n}`).join(", ")})`,
    );
    const lost = [...naive].filter((a) => !liveFns.has(a));
    print(`  reached by it, dead here: ${lost.length}${lost.length ? ` (${lost.map(hex).join(", ")})` : ""}`);
    print(
        `dead functions: ${deadFns.length}; address-taken by Ghidra (ptr_from): ${deadFns.filter((u) => (fnAt.get(u.start)?.ptr_from.length ?? 0) > 0).length}`,
    );
    const short = dead.filter((u) => u.end - u.start < 5);
    print(
        `dead pieces shorter than 5 bytes: ${short.length}${short.length ? ` (${short.map((u) => hex(u.start)).join(", ")})` : ""}`,
    );
    print(boundaryNote);
    for (const d of doubts) print(`  doubt: ${d}`);
    if (verbose)
        for (const u of live.filter((x) => x.via === "weak"))
            print(`  weak: ${hex(u.start)} ${u.why!.kind} at ${hex(u.why!.from)}`);
    // --why <address>: the chain of first references from a root to the piece holding the address.
    const whyAt = args.indexOf("--why");
    if (whyAt >= 0) {
        let u = unitAt(parseInt(args[whyAt + 1] ?? "", 16));
        for (let n = 0; u?.why && n < 100; n++) {
            print(`  ${hex(u.start)} ${u.name}: ${u.why.kind} at ${hex(u.why.from)} (${u.via})`);
            u = inRange(u.why.from) ? unitAt(u.why.from) : undefined;
        }
    }
    print("wrote data/gamespy_reach.csv");
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
