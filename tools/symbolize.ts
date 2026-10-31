// NOT ORIGINAL - turns the thug2-decomp.exe+0x... offsets of a crash report in thug2-decomp.log into function names and
// source lines.
//
//     node tools/symbolize.ts [--pdb-dir out/build/verify] [log or offsets...]
//
// With a log file: symbolizes every "thug2-decomp.exe+0x..." in its last crash report (EXCEPTION, RUN-TIME CHECK
// FAILURE, ... - src/game/sys/crash.h; also tools/pipeline/game/read_dump.ts's output saved to a file). With hex
// offsets (or "thug2-decomp.exe+0x..."): those.
// The PDB must come from the same build as the exe that crashed (compare the deployed exe's timestamp).
// Uses Windows' dbghelp.dll - through Windows PowerShell's Add-Type (C# P/Invoke), as Node has no FFI.

import { spawnSync } from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";
import { parseCli } from "./pipeline/lib/cli.ts";
import { isMain, print, pyExit, pyIntHex, pySplitlines, readTextPy, runMain } from "./pipeline/lib/py.ts";

const USAGE = "usage: symbolize.ts [--pdb-dir PDB_DIR] [items ...]";

const BASE = 0x10000000; // any base: offsets are relative

// The game's module in a report: the exe (an older build's thug2_standalone.exe too).
const EXE = "thug2-decomp.exe";
const MODULE_OFFSET = /(?:thug2-decomp|thug2_standalone)\.exe\+0x([0-9A-Fa-f]+)/gi;

// A report's first line: EXCEPTION, RUN-TIME CHECK FAILURE, DEBUG CRT REPORT, INVALID PARAMETER, PURE VIRTUAL CALL,
// ABORT, BUFFER OVERRUN DETECTED, GAME CRT MESSAGE (src/game/sys/crash.h).
export const REPORT_START =
    /^(?:EXCEPTION |RUN-TIME CHECK FAILURE|DEBUG CRT REPORT|INVALID PARAMETER|PURE VIRTUAL CALL|ABORT|BUFFER OVERRUN|GAME CRT MESSAGE)/gm;

export function offsetsFromLog(file: string): number[] {
    const text = readTextPy(file, "replace");
    // (the last report of any kind - src/game/sys/crash.h; read_dump.ts's output too)
    const starts = [...text.matchAll(REPORT_START)];
    const start = starts.length ? starts[starts.length - 1]!.index! : -1;
    if (start < 0) pyExit(`no EXCEPTION (or other crash) report in ${file}`);
    let report = text.slice(start);
    // the report: its first line and the indented lines after it
    const first = report.indexOf("\n") + 1;
    const end = /\n(?! )/.exec(report.slice(first));
    if (end) report = report.slice(0, first + end.index);
    print(pySplitlines(report)[0]);
    return [...report.matchAll(MODULE_OFFSET)].map((m) => Number.parseInt(m[1]!, 16));
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

function main(): number {
    const { values, positionals } = parseCli(USAGE, {
        allowPositionals: true,
        options: { "pdb-dir": { type: "string", default: path.join("out", "build", "verify") } },
    });
    const items = positionals.length ? positionals : ["C:\\Games\\THUG2\\Game\\thug2-decomp.log"];
    const offsets: number[] = [];
    for (const item of items) {
        if (fs.statSync(item, { throwIfNoEntry: false })?.isFile()) offsets.push(...offsetsFromLog(item));
        else offsets.push(pyIntHex(item.replace(/^(?:thug2-decomp|thug2_standalone)\.exe\+/i, ""))); // as read_dump prints it
    }

    const dll = path.resolve(values["pdb-dir"], EXE);
    const size = fs.statSync(dll).size;
    const script =
        `$args_dll = ${psString(dll)}; $args_dir = ${psString(path.dirname(dll))}; $args_size = ${size}; ` +
        `$args_base = ${BASE}; $args_offsets = @(${offsets.length ? offsets.join(",") : ""});\n${HELPER}`;
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
    for (const l of lines) {
        if (l.startsWith("!")) pyExit(l.slice(1));
        print(l);
    }
    if (r.status !== 0) pyExit(`dbghelp failed: ${(r.stderr ?? "").trim()}`);
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
