// NOT ORIGINAL - turns the <module>+0x... offsets of a crash report in thug2-decomp.log into function names and source
// lines. The report of a 32-bit or a 64-bit build (RAX..R15, the "calls (unwound from the context ...)" lines with
// their 16-digit addresses): the offsets are the same kind, and the PDBs of either.
//
//     node tools/symbolize.ts [--pdb-dir out/build/verify] [--module thug2-decomp.exe] [log or offsets...]
//
// With a log file: symbolizes every "<module>+0x..." in its last crash report (EXCEPTION, RUN-TIME CHECK
// FAILURE, ... - src/platform/crash.h; also tools/game/read_dump.ts's output saved to a file) for each module the
// report names that has a file (exe or DLL, with its PDB beside it) in the PDB folder - the game's exe, a test
// program's (thug2_crash_check.exe...), our own DLLs; the system's modules (KERNEL32, ntdll) are skipped. With hex
// offsets: those, in --module (default thug2-decomp.exe); "<module>+0x..." names its own module.
// A line is "0x<offset>  <function>+0x<displacement>  <file>:<line>", with the module's name in front for any module
// but thug2-decomp.exe.
// The PDB must come from the same build as the exe that crashed (compare the deployed exe's timestamp).
// Uses Windows' dbghelp.dll - through Windows PowerShell's Add-Type (C# P/Invoke), as Node has no FFI.

import { spawnSync } from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";
import { exitWith, isMain, parseCli, print, runMain } from "thug2-level-converter-core/lib/cli.ts";
import { parseHexInt } from "thug2-level-converter-core/lib/numbers.ts";
import { readText, splitLines } from "thug2-level-converter-core/lib/text.ts";

const USAGE = "usage: symbolize.ts [--pdb-dir PDB_DIR] [--module NAME] [items ...]";

const BASE = 0x10000000; // any base: offsets are relative

// The game's module in a report: the exe (an older build's thug2_standalone.exe too).
const EXE = "thug2-decomp.exe";
// Every "<module>+0x<offset>" of a report (a module's file name: letters, digits, _ - . and no spaces).
const MODULE_OFFSET = /([A-Za-z0-9_.-]+\.(?:exe|dll))\+0x([0-9A-Fa-f]+)/gi;

export interface ModuleOffset {
    module: string; // as the report names it (case kept)
    offset: number;
}

// The module's file name the PDB folder is searched for: the older exe name is the exe.
export function moduleFile(module: string): string {
    return /^thug2_standalone\.exe$/i.test(module) ? EXE : module;
}

// Every "<module>+0x<offset>" in the text, in order.
export function moduleOffsets(text: string): ModuleOffset[] {
    return [...text.matchAll(MODULE_OFFSET)].map((m) => ({ module: m[1]!, offset: Number.parseInt(m[2]!, 16) }));
}

// A report's first line: EXCEPTION, RUN-TIME CHECK FAILURE, DEBUG CRT REPORT, INVALID PARAMETER, PURE VIRTUAL CALL,
// ABORT, BUFFER OVERRUN DETECTED, GAME CRT MESSAGE (src/platform/crash.h).
export const REPORT_START =
    /^(?:EXCEPTION |RUN-TIME CHECK FAILURE|DEBUG CRT REPORT|INVALID PARAMETER|PURE VIRTUAL CALL|ABORT|BUFFER OVERRUN|GAME CRT MESSAGE)/gm;

// The last crash report in a log's text: its first line and every "<module>+0x<offset>" in it; undefined when the
// text has no report.
export function lastReport(text: string): { first: string; refs: ModuleOffset[] } | undefined {
    // (the last report of any kind - src/platform/crash.h; read_dump.ts's output too)
    const starts = [...text.matchAll(REPORT_START)];
    const start = starts.length ? starts[starts.length - 1]!.index! : -1;
    if (start < 0) return undefined;
    let report = text.slice(start);
    // the report: its first line and the indented lines after it
    const first = report.indexOf("\n") + 1;
    const end = /\n(?! )/.exec(report.slice(first));
    if (end) report = report.slice(0, first + end.index);
    return { first: splitLines(report)[0]!, refs: moduleOffsets(report) };
}

export function offsetsFromLog(file: string): ModuleOffset[] {
    const report = lastReport(readText(file, "replace"));
    if (!report) exitWith(`no EXCEPTION (or other crash) report in ${file}`);
    print(report.first);
    return report.refs;
}

// dbghelp's SymFromAddr / SymGetLineFromAddr64 for each offset (a 64-bit PowerShell: SYMBOL_INFO is 88 bytes, its
// Name at 84; IMAGEHLP_LINE64 40 bytes, LineNumber at 16, FileName at 24). Prints one line per offset, or
// "!<message>" and exit 1 when dbghelp can't load the module.
const HELPER = `
Add-Type -Language CSharp -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class Thug2Symbolize {
    [DllImport("dbghelp.dll", SetLastError = true)] static extern uint SymSetOptions(uint options);
    [DllImport("dbghelp.dll", SetLastError = true, CharSet = CharSet.Ansi)]
    static extern bool SymInitialize(IntPtr process, string searchPath, bool invade);
    [DllImport("dbghelp.dll", SetLastError = true, CharSet = CharSet.Ansi)]
    static extern ulong SymLoadModuleEx(IntPtr process, IntPtr file, string image, string module, ulong baseOfDll,
                                        uint dllSize, IntPtr data, uint flags);
    [DllImport("dbghelp.dll", SetLastError = true)]
    static extern bool SymFromAddr(IntPtr process, ulong address, out ulong displacement, IntPtr symbol);
    [DllImport("dbghelp.dll", SetLastError = true)]
    static extern bool SymGetLineFromAddr64(IntPtr process, ulong address, out uint displacement, IntPtr line);
    [DllImport("dbghelp.dll", SetLastError = true)] static extern bool SymCleanup(IntPtr process);

    static string Utf8At(IntPtr p) {
        int n = 0;
        while (Marshal.ReadByte(p, n) != 0) n++;
        byte[] bytes = new byte[n];
        Marshal.Copy(p, bytes, 0, n);
        return Encoding.UTF8.GetString(bytes);
    }

    public static int Run(string dll, string dir, ulong size, ulong[] offsets, ulong baseOfDll) {
        IntPtr process = new IntPtr(0x7357); // a pseudo handle: dbghelp only uses it as a key
        SymSetOptions(0x2 | 0x10); // SYMOPT_UNDNAME | SYMOPT_LOAD_LINES
        if (!SymInitialize(process, dir, false)) { Console.WriteLine("!SymInitialize failed"); return 1; }
        if (SymLoadModuleEx(process, IntPtr.Zero, dll, null, baseOfDll, (uint)size, IntPtr.Zero, 0) == 0) {
            Console.WriteLine("!SymLoadModuleEx failed for " + dll);
            return 1;
        }
        IntPtr sym = Marshal.AllocHGlobal(88 + 1024);
        IntPtr line = Marshal.AllocHGlobal(40);
        foreach (ulong off in offsets) {
            for (int i = 0; i < 88 + 1024; i++) Marshal.WriteByte(sym, i, 0);
            Marshal.WriteInt32(sym, 0, 88);
            Marshal.WriteInt32(sym, 80, 1024);
            ulong disp;
            string name = "?";
            if (SymFromAddr(process, baseOfDll + off, out disp, sym))
                name = Utf8At(new IntPtr(sym.ToInt64() + 84)) + "+0x" + disp.ToString("X");
            for (int i = 0; i < 40; i++) Marshal.WriteByte(line, i, 0);
            Marshal.WriteInt32(line, 0, 40);
            uint ldisp;
            string where = "";
            if (SymGetLineFromAddr64(process, baseOfDll + off, out ldisp, line))
                where = "  " + Utf8At(Marshal.ReadIntPtr(line, 24)) + ":" + Marshal.ReadInt32(line, 16).ToString();
            Console.WriteLine("0x" + off.ToString("X6") + "  " + name + where);
        }
        SymCleanup(process);
        return 0;
    }
}
'@
[Console]::OutputEncoding = [Text.Encoding]::UTF8
exit [Thug2Symbolize]::Run($args_dll, $args_dir, $args_size, [uint64[]]$args_offsets, $args_base)
`;

const psString = (s: string): string => `'${s.replaceAll("'", "''")}'`;

// dbghelp's answer for one module's offsets: one line each, in order (see HELPER).
function symbolizeModule(dll: string, offsets: number[]): string[] {
    const size = fs.statSync(dll).size;
    const script =
        `$args_dll = ${psString(dll)}; $args_dir = ${psString(path.dirname(dll))}; $args_size = ${size}; ` +
        `$args_base = ${BASE}; $args_offsets = @(${offsets.length ? offsets.join(",") : ""});
${HELPER}`;
    const r = spawnSync(
        "powershell.exe",
        [
            "-NoProfile",
            "-NonInteractive",
            "-ExecutionPolicy",
            "Bypass",
            "-EncodedCommand",
            Buffer.from(script, "utf16le").toString("base64"),
        ],
        { encoding: "utf-8", maxBuffer: 1 << 26 },
    );
    if (r.error) throw r.error;
    const lines = (r.stdout ?? "")
        .replace(/\r\n/g, "\n")
        .split("\n")
        .filter((l) => l !== "");
    for (const l of lines) if (l.startsWith("!")) exitWith(l.slice(1));
    if (r.status !== 0) exitWith(`dbghelp failed: ${(r.stderr ?? "").trim()}`);
    return lines;
}

// The report's offsets, grouped by the module file the PDB folder has (the others - system DLLs - come back in
// `skipped`); `order` is the report's order, as indexes into the group.
export function groupByModule(
    refs: ModuleOffset[],
    fileFor: (module: string) => string | undefined,
): {
    groups: Map<string, { file: string; offsets: number[] }>;
    order: { key: string; index: number }[];
    skipped: string[];
} {
    const groups = new Map<string, { file: string; offsets: number[] }>();
    const order: { key: string; index: number }[] = [];
    const skipped = new Set<string>();
    for (const ref of refs) {
        const key = moduleFile(ref.module).toLowerCase();
        const file = fileFor(moduleFile(ref.module));
        if (file === undefined) {
            skipped.add(ref.module);
            continue;
        }
        let group = groups.get(key);
        if (!group) {
            group = { file, offsets: [] };
            groups.set(key, group);
        }
        order.push({ key, index: group.offsets.length });
        group.offsets.push(ref.offset);
    }
    return { groups, order, skipped: [...skipped] };
}

function main(): number {
    const { values, positionals } = parseCli(USAGE, {
        allowPositionals: true,
        options: {
            "pdb-dir": { type: "string", default: path.join("out", "build", "verify") },
            module: { type: "string", default: EXE },
        },
    });
    const items = positionals.length ? positionals : ["C:\\Games\\THUG2\\Game\\thug2-decomp.log"];
    const refs: ModuleOffset[] = [];
    for (const item of items) {
        if (fs.statSync(item, { throwIfNoEntry: false })?.isFile()) refs.push(...offsetsFromLog(item));
        else {
            const named = moduleOffsets(item); // as read_dump prints it
            refs.push(...(named.length ? named : [{ module: values.module, offset: parseHexInt(item) }]));
        }
    }

    const dir = path.resolve(values["pdb-dir"]);
    const fileFor = (module: string): string | undefined => {
        const file = path.join(dir, module);
        return fs.statSync(file, { throwIfNoEntry: false })?.isFile() ? file : undefined;
    };
    const { groups, order, skipped } = groupByModule(refs, fileFor);
    if (!groups.has(EXE) && refs.some((r) => moduleFile(r.module).toLowerCase() === EXE)) {
        exitWith(`${path.join(dir, EXE)} isn't there (--pdb-dir)`);
    }
    const results = new Map<string, string[]>();
    for (const [key, group] of groups) results.set(key, symbolizeModule(group.file, group.offsets));
    for (const { key, index } of order) {
        const line = results.get(key)![index] ?? "?";
        print(key === EXE ? line : `${path.basename(groups.get(key)!.file)} ${line}`);
    }
    if (skipped.length) process.stderr.write(`(no file in ${dir} for: ${skipped.join(", ")})\n`);
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
