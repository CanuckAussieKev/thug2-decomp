// NOT ORIGINAL - reads a Windows minidump (.dmp: %LOCALAPPDATA%\CrashDumps, written by Windows Error Reporting when
// the game dies) without a debugger: the exception, where it happened (module + offset), the registers and the return
// addresses found on the faulting thread's stack - the same report the DLL's crash logger writes (src/hook/log.cpp),
// for crashes it couldn't log (a /GS fast-fail ends the process before any handler runs).
//
//     node tools/pipeline/game/read_dump.ts <file.dmp> [--threads] [--modules]
//
// --threads: every thread's EIP and stack scan, not only the faulting one. --modules: the module list. --image <file>:
// a module file with the dump's PE timestamp (the crashed build's dinput8.dll) - WER's dumps leave module code out, so
// the stack scan checks "follows a CALL" against the files (each module's own path is tried first); values it
// couldn't check are marked "?".
// Offsets in DINPUT8.dll (our DLL) go to `node tools/symbolize.ts --pdb-dir <build> <offsets>` - with the PDB of the
// build that crashed (the module's timestamp is printed: compare it with the build's dinput8.dll).
// A 32-bit (WOW64) process' dump written by the 64-bit WER: the exception stream's context is the x86 one when the
// dump says the processor is x86 (architecture 0); for an x64 dump of a WOW64 process the 32-bit registers aren't in
// it, and only the stack scan from the thread's recorded stack range is given.

import * as fs from "node:fs";
import { parseCli } from "../lib/cli.ts";
import { isMain, print, pyExit, runMain } from "../lib/py.ts";

const USAGE = `usage: read_dump.ts [-h] [--threads] [--modules] [--image FILE] dump

Read a minidump: exception, registers, return addresses on the stack, modules.

positional arguments:
  dump        the .dmp file

options:
  -h, --help  show this help message and exit
  --threads   every thread, not only the faulting one
  --modules   list the modules
  --image FILE  a module's file (same name and PE timestamp, e.g. the crashed build's dinput8.dll) whose code
              checks the stack scan; repeatable. Each module's own path is tried anyway`;

interface Module {
    base: number;
    size: number;
    time: number;
    name: string;
    path: string;
    // The module's file mapped as an image (sections at their virtual addresses), when a file with the same PE
    // timestamp was found: lets the stack scan check the code before a return address the dump doesn't hold.
    image?: Buffer;
}

interface Range {
    start: number;
    size: number;
    rva: number;
}

export class Minidump {
    readonly buf: Buffer;
    readonly streams = new Map<number, { size: number; rva: number }>();
    readonly modules: Module[] = [];
    readonly ranges: Range[] = [];
    architecture = -1;

    constructor(buf: Buffer) {
        this.buf = buf;
        if (buf.toString("latin1", 0, 4) !== "MDMP") throw new Error("not a minidump (no MDMP signature)");
        const count = buf.readUInt32LE(8);
        const dir = buf.readUInt32LE(12);
        for (let i = 0; i < count; ++i) {
            const e = dir + i * 12;
            this.streams.set(buf.readUInt32LE(e), { size: buf.readUInt32LE(e + 4), rva: buf.readUInt32LE(e + 8) });
        }
        const sys = this.streams.get(7);
        if (sys) this.architecture = buf.readUInt16LE(sys.rva);
        const mods = this.streams.get(4);
        if (mods) {
            const n = buf.readUInt32LE(mods.rva);
            for (let i = 0; i < n; ++i) {
                const m = mods.rva + 4 + i * 108;
                const nameRva = buf.readUInt32LE(m + 20);
                const len = buf.readUInt32LE(nameRva);
                const full = buf.toString("utf16le", nameRva + 4, nameRva + 4 + len);
                this.modules.push({
                    base: Number(buf.readBigUInt64LE(m)),
                    size: buf.readUInt32LE(m + 8),
                    time: buf.readUInt32LE(m + 16),
                    name: full.slice(full.lastIndexOf("\\") + 1),
                    path: full,
                });
            }
        }
        const list = this.streams.get(5);
        if (list) {
            const n = buf.readUInt32LE(list.rva);
            for (let i = 0; i < n; ++i) {
                const d = list.rva + 4 + i * 16;
                this.ranges.push({
                    start: Number(buf.readBigUInt64LE(d)),
                    size: buf.readUInt32LE(d + 8),
                    rva: buf.readUInt32LE(d + 12),
                });
            }
        }
        const list64 = this.streams.get(9);
        if (list64) {
            const n = Number(buf.readBigUInt64LE(list64.rva));
            let rva = Number(buf.readBigUInt64LE(list64.rva + 8));
            for (let i = 0; i < n; ++i) {
                const d = list64.rva + 16 + i * 16;
                const size = Number(buf.readBigUInt64LE(d + 8));
                this.ranges.push({ start: Number(buf.readBigUInt64LE(d)), size, rva });
                rva += size;
            }
        }
        this.ranges.sort((a, b) => a.start - b.start);
    }

    // The dump's bytes for [address, address + size), or null when it doesn't hold them all.
    read(address: number, size: number): Buffer | null {
        let lo = 0;
        let hi = this.ranges.length - 1;
        while (lo <= hi) {
            const mid = (lo + hi) >> 1;
            const r = this.ranges[mid]!;
            if (address < r.start) hi = mid - 1;
            else if (address >= r.start + r.size) lo = mid + 1;
            else {
                if (address + size > r.start + r.size || r.rva + (address - r.start) + size > this.buf.length)
                    return null;
                const at = r.rva + (address - r.start);
                return this.buf.subarray(at, at + size);
            }
        }
        return null;
    }

    moduleOf(address: number): Module | undefined {
        return this.modules.find((m) => address >= m.base && address < m.base + m.size);
    }

    where(address: number): string {
        const m = this.moduleOf(address);
        const hex = address.toString(16).toUpperCase().padStart(8, "0");
        return m ? `${m.name}+0x${(address - m.base).toString(16).toUpperCase()} (${hex})` : hex;
    }

    // Maps `file` as the image of the module it matches by PE timestamp (default: each module's own path, when the
    // file there still has the timestamp). Returns the module's name, or null when none matches.
    attachImage(file: string, module?: Module): string | null {
        let pe: Buffer;
        try {
            pe = fs.readFileSync(file);
        } catch {
            return null;
        }
        if (pe.length < 0x40 || pe.toString("latin1", 0, 2) !== "MZ") return null;
        const nt = pe.readUInt32LE(0x3c);
        if (nt + 24 > pe.length || pe.toString("latin1", nt, nt + 4) !== "PE\0\0") return null;
        const time = pe.readUInt32LE(nt + 8);
        const base = file.replace(/^.*[\\/]/, "").toLowerCase();
        const m =
            module ??
            this.modules.find((x) => x.time === time && x.image === undefined && x.name.toLowerCase() === base);
        if (!m || m.time !== time) return null;
        const sections = pe.readUInt16LE(nt + 6);
        const optSize = pe.readUInt16LE(nt + 20);
        const headers = pe.readUInt32LE(nt + 24 + 60);
        const image = Buffer.alloc(m.size);
        pe.copy(image, 0, 0, Math.min(headers, m.size, pe.length));
        for (let i = 0; i < sections; ++i) {
            const sh = nt + 24 + optSize + i * 40;
            const va = pe.readUInt32LE(sh + 12);
            const raw = pe.readUInt32LE(sh + 16);
            const ptr = pe.readUInt32LE(sh + 20);
            if (va >= m.size) continue;
            pe.copy(image, va, ptr, Math.min(ptr + raw, pe.length, ptr + (m.size - va)));
        }
        m.image = image;
        return m.name;
    }

    // Code bytes from the dump, else from the module's attached image (relocations not applied: fine for opcodes).
    code(address: number, size: number): Buffer | null {
        const b = this.read(address, size);
        if (b) return b;
        const m = this.moduleOf(address);
        if (!m?.image || address - m.base + size > m.size || address < m.base) return null;
        return m.image.subarray(address - m.base, address - m.base + size);
    }

    // Whether `address` follows a CALL (E8 rel32, FF /2) - as the DLL's crash logger checks. null when the dump
    // doesn't hold the code before it (WER's dumps mostly leave module code out).
    afterCall(address: number): boolean | null {
        const b = this.code(address - 7, 7);
        if (!b) return null;
        if (b[2] === 0xe8) return true;
        for (const length of [2, 3, 4, 6, 7]) {
            if (b[7 - length] === 0xff && ((b[8 - length]! >> 3) & 7) === 2) return true;
        }
        return false;
    }

    // Return addresses on the stack from `esp` up (a scan: some may be stale). A value inside a module whose code
    // the dump doesn't hold is listed with " ?" (not checked: it may be a stale return address or a pointer to data).
    scan(esp: number, limit = 0x4000, most = 32): string[] {
        const out: string[] = [];
        for (let slot = esp; slot < esp + limit && out.length < most; slot += 4) {
            const w = this.read(slot, 4);
            if (!w) break;
            const value = w.readUInt32LE(0);
            if (!this.moduleOf(value)) continue;
            const call = this.afterCall(value);
            if (call === false) continue;
            out.push(
                `    [esp+0x${(slot - esp).toString(16).toUpperCase()}] ${this.where(value)}${call === null ? " ?" : ""}`,
            );
        }
        return out;
    }
}

// x86 CONTEXT: Edi at 156 ... Esp at 196.
function x86Registers(ctx: Buffer): Record<string, number> {
    const names = ["EDI", "ESI", "EBX", "EDX", "ECX", "EAX", "EBP", "EIP", "CS", "EFLAGS", "ESP"];
    const r: Record<string, number> = {};
    names.forEach((n, i) => {
        r[n] = ctx.readUInt32LE(156 + i * 4);
    });
    return r;
}

const FAST_FAIL: Record<number, string> = {
    2: "FAST_FAIL_STACK_COOKIE_CHECK_FAILURE (/GS: a buffer overrun on the stack)",
    5: "FAST_FAIL_INVALID_ARG",
    7: "FAST_FAIL_FATAL_APP_EXIT (abort())",
    10: "FAST_FAIL_GUARD_ICALL_CHECK_FAILURE",
    14: "FAST_FAIL_INVALID_BUFFER_ACCESS",
    23: "FAST_FAIL_INVALID_SET_OF_CONTEXT",
};

function hex(v: number): string {
    return (v >>> 0).toString(16).toUpperCase().padStart(8, "0");
}

export function report(dump: Minidump, threads: boolean, modules: boolean): string[] {
    const out: string[] = [];
    const b = dump.buf;
    out.push(`processor architecture ${dump.architecture} (0 = x86, 9 = x64)`);
    const exe = dump.modules[0];
    if (exe) out.push(`process: ${exe.name}`);
    for (const m of dump.modules) {
        if (/^dinput8\.dll$/i.test(m.name) || m === exe)
            out.push(
                `module ${m.name} at ${hex(m.base)} size 0x${m.size.toString(16)} timestamp ${hex(m.time)} (${new Date(m.time * 1000).toISOString()})`,
            );
    }
    let faultThread = -1;
    const exc = dump.streams.get(6);
    if (exc) {
        faultThread = b.readUInt32LE(exc.rva);
        const e = exc.rva + 8;
        const code = b.readUInt32LE(e);
        const address = Number(b.readBigUInt64LE(e + 16));
        const nparams = b.readUInt32LE(e + 24);
        const params: number[] = [];
        for (let i = 0; i < Math.min(nparams, 15); ++i) params.push(Number(b.readBigUInt64LE(e + 32 + i * 8)));
        out.push(`EXCEPTION 0x${hex(code)} at ${dump.where(address)} in thread ${faultThread}`);
        if (params.length) out.push(`  parameters: ${params.map((p) => p.toString(16).toUpperCase()).join(" ")}`);
        if (code === 0xc0000409 && params.length)
            out.push(`  fast-fail code ${params[0]}: ${FAST_FAIL[params[0]!] ?? "?"}`);
        if (code === 0xc0000005 && params.length >= 2)
            out.push(
                `  ${params[0] ? (params[0] === 8 ? "executing" : "writing") : "reading"} address ${hex(params[1]!)}`,
            );
        const ctxSize = b.readUInt32LE(e + 152);
        const ctxRva = b.readUInt32LE(e + 156);
        if (dump.architecture === 0 && ctxSize >= 200) {
            const r = x86Registers(b.subarray(ctxRva, ctxRva + ctxSize));
            out.push(
                `  EAX ${hex(r.EAX!)} EBX ${hex(r.EBX!)} ECX ${hex(r.ECX!)} EDX ${hex(r.EDX!)} ESI ${hex(r.ESI!)} EDI ${hex(r.EDI!)} EBP ${hex(r.EBP!)} ESP ${hex(r.ESP!)}`,
            );
            out.push(`  EIP ${dump.where(r.EIP!)}`);
            out.push("  return addresses on the stack (newest first):");
            out.push(...dump.scan(r.ESP!));
        }
    }
    const list = dump.streams.get(3);
    if (list) {
        const n = b.readUInt32LE(list.rva);
        for (let i = 0; i < n; ++i) {
            const t = list.rva + 4 + i * 48;
            const id = b.readUInt32LE(t);
            if (!threads && id !== faultThread) continue;
            const stackStart = Number(b.readBigUInt64LE(t + 24));
            const ctxSize = b.readUInt32LE(t + 40);
            const ctxRva = b.readUInt32LE(t + 44);
            let esp = stackStart;
            let line = `thread ${id}`;
            if (dump.architecture === 0 && ctxSize >= 200) {
                const r = x86Registers(b.subarray(ctxRva, ctxRva + ctxSize));
                esp = r.ESP!;
                line += `: EIP ${dump.where(r.EIP!)} ESP ${hex(r.ESP!)}`;
            }
            out.push(line);
            if (threads || !exc) out.push(...dump.scan(esp, 0x4000, 16));
        }
    }
    if (modules)
        for (const m of dump.modules) out.push(`  ${hex(m.base)} ${m.size.toString(16).padStart(8)} ${m.name}`);
    return out;
}

function main(): number {
    const { values, positionals } = parseCli(USAGE, {
        options: {
            threads: { type: "boolean" },
            modules: { type: "boolean" },
            image: { type: "string", multiple: true },
        },
        allowPositionals: true,
    });
    const file = positionals[0];
    if (!file || positionals.length > 1)
        pyExit(`${USAGE}
error: one dump file`);
    let dump: Minidump;
    try {
        dump = new Minidump(fs.readFileSync(file));
    } catch (e) {
        pyExit(`${file}: ${(e as Error).message}`);
    }
    // Each module's own file when it still has the dump's timestamp, then the --image files (e.g. a build's
    // dinput8.dll): with the code mapped, the stack scan keeps only values that follow a CALL.
    for (const m of dump.modules) dump.attachImage(m.path, m);
    for (const image of (values.image as string[] | undefined) ?? []) {
        const name = dump.attachImage(image);
        print(name ? `image ${image}: ${name}` : `image ${image}: no module with its name and timestamp`);
    }
    for (const line of report(dump, values.threads === true, values.modules === true)) print(line);
    return 0;
}

if (isMain(import.meta.url)) runMain(main);
