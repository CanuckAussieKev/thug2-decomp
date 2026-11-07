// NOT ORIGINAL - tests of tools/symbolize.ts and of tools/standalone/first_fault.py's x64 structures: the crash reports
// of a 32-bit and a 64-bit build (thug2_crash_check's "access" crash, src/tests/crash_check.cpp; recorded, shortened)
// are read the same way, and any module a report names is looked up in the PDB folder.

import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import * as path from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";
import { groupByModule, lastReport, moduleFile, moduleOffsets } from "./symbolize.ts";

const X86_REPORT = `crash test: access
EXCEPTION 0xC0000005 at thug2_crash_check.exe+0x10D33 (001F0D33) (first chance: if the code handles it, nothing more happens)
  writing address 00D90000
  EAX 00D90000 EBX 0093FB74 ECX 38540000 EDX 00D90000 ESI 0093FB2C EDI 0093FB68 EBP 0093FB68 ESP 0093FB2C
  [esp+0x00] 0093FC20 001E3143 CCCCCCCC 00000000 00345B5C CCCCCCCC CCCCCCCC CCCCCCCC
  return addresses on the stack (newest first):
    [esp+0x40] thug2_crash_check.exe+0xDF5E (001EDF5E)
    [esp+0x60] thug2_crash_check.exe+0xDFCD (001EDFCD)
    [esp+0x190] KERNEL32.DLL+0x15D49 (76025D49)
    [esp+0x1A0] ntdll.dll+0x6C75B (7762C75B)
  unhandled: nothing caught exception 0xC0000005
  the process ends (exit code 3221225477 = 0xC0000005)
`;

const X64_REPORT = `crash test: access
EXCEPTION 0xC0000005 at thug2_crash_check.exe+0x110C9 (00007FF7DE9610C9) (first chance: if the code handles it, nothing more happens)
  writing address 000001FC51970000
  RAX 000001FC51970000 RBX 0000000000000000 RCX 00007FFF0A1C2054 RDX 0000000000000000
  R12 0000000000000000 R13 0000000000000000 R14 0000000000000000 R15 0000000000000000
  [rsp+0x00] 000001FC51970000 0000000000001000 0000000000000000 0000000000000000
  calls (unwound from the context, newest first):
    [rsp=000000A5550FF8B0] thug2_crash_check.exe+0x110C9 (00007FF7DE9610C9)
    [rsp=000000A5550FF990] thug2_crash_check.exe+0xE083 (00007FF7DE95E083)
    [rsp=000000A5550FFC80] KERNEL32.DLL+0x2DDA7 (00007FFF08B0DDA7)
    [rsp=000000A5550FFCB0] ntdll.dll+0x7EE4C (00007FFF0A0DEE4C)
  return addresses on the stack (newest first):
    [rsp+0xD8] thug2_crash_check.exe+0xE083 (00007FF7DE95E083)
  unhandled: nothing caught exception 0xC0000005
  the process ends (exit code 3221225477 = 0xC0000005)
`;

test("moduleOffsets: every <module>+0x<offset>, 32-bit and 64-bit reports alike", () => {
    assert.deepEqual(moduleOffsets(X86_REPORT), [
        { module: "thug2_crash_check.exe", offset: 0x10d33 },
        { module: "thug2_crash_check.exe", offset: 0xdf5e },
        { module: "thug2_crash_check.exe", offset: 0xdfcd },
        { module: "KERNEL32.DLL", offset: 0x15d49 },
        { module: "ntdll.dll", offset: 0x6c75b },
    ]);
    assert.deepEqual(moduleOffsets(X64_REPORT), [
        { module: "thug2_crash_check.exe", offset: 0x110c9 },
        { module: "thug2_crash_check.exe", offset: 0x110c9 },
        { module: "thug2_crash_check.exe", offset: 0xe083 },
        { module: "KERNEL32.DLL", offset: 0x2dda7 },
        { module: "ntdll.dll", offset: 0x7ee4c },
        { module: "thug2_crash_check.exe", offset: 0xe083 },
    ]);
});

test("moduleOffsets: the game's exe, an older name of it, a DLL of ours", () => {
    const refs = moduleOffsets(
        "a thug2-decomp.exe+0x1F40 b thug2_standalone.exe+0x20 c thug2-vulkan.dll+0xFF d D3DX9_43.dll+0x1",
    );
    assert.deepEqual(
        refs.map((r) => moduleFile(r.module)),
        ["thug2-decomp.exe", "thug2-decomp.exe", "thug2-vulkan.dll", "D3DX9_43.dll"],
    );
});

test("lastReport: the first line and the offsets of the last report only", () => {
    const log = `EXCEPTION 0xC0000005 at thug2-decomp.exe+0x1 (00401001)\n  [esp+0x00] thug2-decomp.exe+0x2 (00401002)\nloading...\n${X64_REPORT}`;
    const report = lastReport(log);
    assert.ok(report);
    assert.match(report.first, /^EXCEPTION 0xC0000005 at thug2_crash_check\.exe\+0x110C9 \(00007FF7DE9610C9\)/);
    assert.equal(report.refs.length, 6);
    assert.equal(lastReport("nothing here\n"), undefined);
});

test("lastReport: an x64 report's lines end the report at the first unindented line", () => {
    const report = lastReport(`${X64_REPORT}the game goes on thug2-decomp.exe+0x9\n`);
    assert.ok(report);
    assert.equal(report.refs.length, 6);
});

test("groupByModule: modules with a file are grouped in report order, the system's skipped", () => {
    const refs = moduleOffsets(X64_REPORT);
    const { groups, order, skipped } = groupByModule(refs, (m) =>
        m === "thug2_crash_check.exe" ? `dir/${m}` : undefined,
    );
    assert.deepEqual([...groups.keys()], ["thug2_crash_check.exe"]);
    assert.deepEqual(groups.get("thug2_crash_check.exe")?.offsets, [0x110c9, 0x110c9, 0xe083, 0xe083]);
    assert.deepEqual(
        order.map((o) => o.index),
        [0, 1, 2, 3],
    );
    assert.deepEqual(skipped, ["KERNEL32.DLL", "ntdll.dll"]);
});

test("groupByModule: two modules keep their own offsets and the report's order", () => {
    const refs = moduleOffsets("a thug2-decomp.exe+0x10 b thug2-vulkan.dll+0x20 c thug2-decomp.exe+0x30");
    const { groups, order } = groupByModule(refs, (m) => `dir/${m}`);
    assert.deepEqual(groups.get("thug2-decomp.exe")?.offsets, [0x10, 0x30]);
    assert.deepEqual(groups.get("thug2-vulkan.dll")?.offsets, [0x20]);
    assert.deepEqual(
        order.map((o) => `${o.key}#${o.index}`),
        ["thug2-decomp.exe#0", "thug2-vulkan.dll#0", "thug2-decomp.exe#1"],
    );
});

// first_fault.py: its x64 CONTEXT layout is asserted when the file is imported, and pe_machine reads the PE header:
// node's own exe is the 64-bit program at hand.
test("first_fault.py: imports (the x64 CONTEXT layout holds) and reads a PE machine", {
    skip: process.platform !== "win32",
}, () => {
    const dir = path.join(path.dirname(fileURLToPath(import.meta.url)), "standalone");
    const code =
        "import sys; sys.path.insert(0, sys.argv[1]); import first_fault; print(hex(first_fault.pe_machine(sys.argv[2])))";
    const r = spawnSync("python", ["-I", "-c", code, dir, process.execPath], { encoding: "utf-8" });
    if (r.error) return; // (no Python on the machine: the tool is optional)
    assert.equal(r.status, 0, r.stderr);
    assert.equal(r.stdout.trim(), process.arch === "x64" ? "0x8664" : r.stdout.trim());
});
