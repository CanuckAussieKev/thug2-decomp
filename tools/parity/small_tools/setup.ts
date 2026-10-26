// NOT ORIGINAL - inputs for the small tools' parity cases (tools/parity/small_tools/cases.ts), written into a case's
// shared input folder:
//   node tools/parity/small_tools/setup.ts <in> bps      - BPS patches (every command, varints of several bytes,
//                                                           negative copy offsets, odd metadata) and a non-BPS file
//   node tools/parity/small_tools/setup.ts <in> symbols  - the verify build's dinput8.dll + .pdb (a snapshot: a
//                                                           rebuild during the run can't change one side) and a log
//                                                           with two crash reports
//   node tools/parity/small_tools/setup.ts <in> mutate   - a stand-in checker (thug2_check.exe, compiled with the
//                                                           .NET Framework's csc), a stand-in build and source files
//   node tools/parity/small_tools/setup.ts <in> pending  - pending records to fold (one with CRLF line ends)
//   node tools/parity/small_tools/setup.ts <in> codewrites - online_hosts.cpp variants: a write into a rewritten
//                                                           function (overlap.cpp), one write fewer (short.cpp)

import { spawnSync } from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import * as zlib from "node:zlib";

const REPO = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..", "..");

function bpsNumber(n: number): number[] {
    const out: number[] = [];
    for (;;) {
        const x = n % 128;
        n = Math.floor(n / 128);
        if (n === 0) {
            out.push(0x80 | x);
            return out;
        }
        out.push(x);
        n -= 1;
    }
}

function bps(meta: Buffer, commands: (number[] | Buffer)[]): Buffer {
    const body = Buffer.concat([
        Buffer.from("BPS1"),
        Buffer.from(bpsNumber(70000)),
        Buffer.from(bpsNumber(70123)),
        Buffer.from(bpsNumber(meta.length)),
        meta,
        ...commands.map((c) => Buffer.from(c)),
    ]);
    const crcs = Buffer.alloc(12);
    crcs.writeUInt32LE(zlib.crc32(Buffer.from("source")), 0);
    crcs.writeUInt32LE(zlib.crc32(Buffer.from("target")), 4);
    crcs.writeUInt32LE(zlib.crc32(body), 8);
    return Buffer.concat([body, crcs]);
}

const command = (kind: number, length: number): number[] => bpsNumber((length - 1) * 4 + kind);
const offset = (relative: number): number[] => bpsNumber(Math.abs(relative) * 2 + (relative < 0 ? 1 : 0));

function makeBps(dir: string): void {
    fs.writeFileSync(
        path.join(dir, "patch.bps"),
        bps(Buffer.from("<patch name='x' \"q\">\n\t\\\x00\xff\x7f", "latin1"), [
            command(0, 100), // SourceRead
            command(1, 5),
            Buffer.from([1, 2, 0xab, 0x20, 0xff]), // TargetRead
            command(2, 300),
            offset(0x4000), // SourceCopy forwards
            command(2, 16),
            offset(-0x4100), // ... and back
            command(3, 40),
            offset(7), // TargetCopy
            command(3, 1),
            offset(-30),
            command(0, 69000), // a long SourceRead (a 3-byte varint)
            command(1, 1),
            Buffer.from([0x41]),
        ]),
    );
    fs.writeFileSync(path.join(dir, "empty.bps"), bps(Buffer.alloc(0), []));
    fs.writeFileSync(path.join(dir, "quote.bps"), bps(Buffer.from("it's"), [command(0, 1)]));
    fs.writeFileSync(path.join(dir, "notbps.bin"), Buffer.from("IPS!not a bps patch at all"));
}

function makeSymbols(dir: string): void {
    const build = "C:\\Dev\\thug2-decomp\\out\\build\\verify";
    fs.mkdirSync(path.join(dir, "pdb"), { recursive: true });
    for (const f of ["dinput8.dll", "dinput8.pdb"]) fs.copyFileSync(path.join(build, f), path.join(dir, "pdb", f));
    const report = (title: string, offsets: string[]): string =>
        `EXCEPTION ${title}\n${offsets.map((o, i) => `  #${i} DINPUT8.dll+0x${o} (frame)\n`).join("")}`;
    fs.writeFileSync(
        path.join(dir, "crash.log"),
        "[00:01] start\r\n" +
            report("0xC0000005 at 0x00401234 (old)", ["11234"]) +
            "[00:02] more log\n" +
            report("0xC0000005 at 0x00563210 (reading 0x00000000)", ["77F6C", "891a0", "180000", "100000", "1000"]) +
            "[00:03] after the report: dinput8.dll+0x55B04 not part of it\n",
    );
    fs.writeFileSync(path.join(dir, "nocrash.log"), "[00:01] start\n[00:02] quit\n");
}

const CHECKER = `using System;
using System.IO;
using System.Threading;
// A stand-in for out/build/verify/thug2_check.exe: reads victim.cpp; BUG fails a test, CRASH ends like an access
// violation, HANG never ends.
static class Check {
    static int Main() {
        string text = File.ReadAllText("victim.cpp") + File.ReadAllText("victim2.cpp");
        string tests = Environment.GetEnvironmentVariable("THUG2_SELFTESTS") ?? "(all)";
        Console.WriteLine("checking " + tests);
        if (text.Contains("HANG")) { Thread.Sleep(Timeout.Infinite); }
        if (text.Contains("CRASH")) {
            Console.WriteLine("SELFTEST CRASH TestVictim\\x85 caf\\u00e9");
            Console.Out.Flush();
            Environment.Exit(unchecked((int)0xC0000005));
        }
        if (text.Contains("BUG")) {
            for (int i = 0; i < 6; i++) Console.WriteLine("  SELFTEST FAIL TestVictim" + i + " [" + tests + "]  ");
            return 3;
        }
        Console.WriteLine("SELFTEST PASS all");
        return 0;
    }
}
`;

function makeMutate(dir: string): void {
    fs.writeFileSync(path.join(dir, "check.cs"), CHECKER);
    const csc = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\csc.exe";
    const r = spawnSync(csc, ["/nologo", `/out:${path.join(dir, "thug2_check.exe")}`, path.join(dir, "check.cs")], {
        encoding: "utf-8",
    });
    if (r.status !== 0) throw new Error(`csc failed: ${r.stdout}${r.stderr}`);
    // the build: fails when a source has NOCOMPILE; counts its runs
    fs.writeFileSync(
        path.join(dir, "build.cmd"),
        '@echo off\r\necho build>>build_runs.txt\r\nfindstr /c:"NOCOMPILE" victim.cpp victim2.cpp >nul && exit /b 1\r\nexit /b 0\r\n',
    );
    fs.writeFileSync(
        path.join(dir, "victim.cpp"),
        "int Add(int a, int b)\r\n{\r\n    return a + b;\r\n}\r\n\r\nint Twice(int x) { return x * 2; }\r\n",
    );
    fs.writeFileSync(path.join(dir, "victim2.cpp"), "// second file, LF line ends\nint Three() { return 3; }\n");
}

function makePending(dir: string): void {
    fs.writeFileSync(
        path.join(dir, "front.md"),
        "# Front-end agent\n\nnotes\n\n## Batches\n\n" +
            "- [x] Batch FE-1 (NOT run in the game yet; 12 functions: menus)\n  details line\n" +
            "- [x] Batch FE-2 (NOT run in the game yet;   3 functions, $1 and \\1 kept)\n" +
            "- [x] Batch FX-9 (another tag)\n\n\n## Game runs\n\n- run 1\n",
    );
    fs.writeFileSync(
        path.join(dir, "crlf.md"),
        "- [x] Batch A.B-7 (NOT run in the game yet; dotted tag)\r\n- [x] Batch A.B-8 (already run)  \r\n",
    );
}

function makeCodeWrites(dir: string): void {
    const hosts = fs.readFileSync(path.join(REPO, "src", "game", "net", "online_hosts.cpp"), "utf-8");
    const entry = "{0x005F86DD, 1}";
    if (!hosts.includes(entry)) throw new Error(`online_hosts.cpp has no ${entry} any more: update setup.ts`);
    // 0x00401000 + 4 is inside the rewritten Mem::Free at 0x00401000
    fs.writeFileSync(path.join(dir, "overlap.cpp"), hosts.replace(entry, "{0x00401000, 4}"));
    fs.writeFileSync(path.join(dir, "short.cpp"), hosts.replace(entry, ""));
}

const [dir, what] = process.argv.slice(2);
if (dir === undefined) throw new Error("usage: setup.ts <in> bps|symbols|mutate|pending");
fs.mkdirSync(dir, { recursive: true });
if (what === "bps") makeBps(dir);
else if (what === "symbols") makeSymbols(dir);
else if (what === "mutate") makeMutate(dir);
else if (what === "pending") makePending(dir);
else if (what === "codewrites") makeCodeWrites(dir);
else throw new Error(`unknown input set ${what}`);
