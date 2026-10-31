"""Decode the reference build's code with capstone for tools/gamespy_reach.ts (and any other reachability tool).

    uv run --no-project --python 3.13 --with capstone python -I tools/capstone_refs.py every <lo> <hi> [exe]
        one line per byte offset in [lo, hi): the instruction decoded there -
        "<addr> <size> <flow> <target> <values>" (hex; flow: n normal, c call, j jump, b conditional branch / loop,
        r return, s stop (int3, hlt, ud2), x undecodable; target: a direct branch's destination or "-";
        values: every immediate and memory displacement, comma-separated, or "-"; "<addr> 0 x - -" when nothing
        decodes there)
    uv run ... capstone_refs.py sweep <lo> <hi> [exe] < ranges
        a linear sweep of each "<from> <to>" range read from stdin, printing only the instructions whose branch
        target, immediate or displacement lies in [lo, hi): "<addr> <kind> <value>" (kind: call, jmp, jcc, operand
        (an immediate), memory (a displacement alone: [value]), indexed (a displacement with a register: [value + reg]))
"""
import json
import os
import struct
import sys

import capstone
from capstone import x86

_local = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "local.json")
mode = sys.argv[1]
low, high = int(sys.argv[2], 16), int(sys.argv[3], 16)
exe_path = sys.argv[4] if len(sys.argv) > 4 else json.load(open(_local, encoding="utf-8"))["referenceExe"]
exe = open(exe_path, "rb").read()
pe = struct.unpack_from("<I", exe, 0x3C)[0]
sections = struct.unpack_from("<H", exe, pe + 6)[0]
optional = struct.unpack_from("<H", exe, pe + 20)[0]
text = None
for i in range(sections):
    header = pe + 24 + optional + 40 * i
    if exe[header:header + 8].rstrip(b"\0") == b".text":
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from("<IIII", exe, header + 8)
        text = (0x400000 + virtual_address, raw_offset, min(virtual_size, raw_size))
text_va, text_raw, text_size = text


def code(va, n):
    start = text_raw + va - text_va
    return exe[start:start + max(0, min(n, text_va + text_size - va))]


md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = True
STOPS = {"int3", "hlt", "ud2", "int1"}


def describe(ins):
    groups = set(ins.groups)
    target = None
    values = []
    for op in ins.operands:
        if op.type == x86.X86_OP_IMM:
            values.append(op.imm & 0xFFFFFFFF)
        elif op.type == x86.X86_OP_MEM:
            values.append(op.mem.disp & 0xFFFFFFFF)
    if capstone.CS_GRP_RET in groups or ins.mnemonic.startswith("ret") or ins.mnemonic == "iretd":
        flow = "r"
    elif ins.mnemonic in STOPS:
        flow = "s"
    elif capstone.CS_GRP_CALL in groups:
        flow = "c"
    elif capstone.CS_GRP_JUMP in groups:
        flow = "j" if ins.mnemonic in ("jmp", "ljmp") else "b"
    else:
        flow = "n"
    if flow in "cjb" and ins.operands and ins.operands[0].type == x86.X86_OP_IMM:
        target = ins.operands[0].imm & 0xFFFFFFFF
        values = values[1:]
    return flow, target, values


out = sys.stdout
if mode == "every":
    data = code(low, high - low + 16)
    for a in range(low, high):
        off = a - low
        ins = next(md.disasm(data[off:off + 16], a, 1), None)
        if ins is None:
            out.write(f"{a:x} 0 x - -\n")
            continue
        flow, target, values = describe(ins)
        t = f"{target:x}" if target is not None else "-"
        v = ",".join(f"{x:x}" for x in values) if values else "-"
        out.write(f"{a:x} {ins.size} {flow} {t} {v}\n")
elif mode == "sweep":
    for line in sys.stdin:
        parts = line.split()
        if len(parts) < 2:
            continue
        lo, hi = int(parts[0], 16), int(parts[1], 16)
        a = lo
        while a < hi:
            ins = next(md.disasm(code(a, 16), a, 1), None)
            if ins is None:
                a += 1
                continue
            flow, target, values = describe(ins)
            if target is not None and low <= target < high:
                out.write(f"{ins.address:x} {'call' if flow == 'c' else 'jmp' if flow == 'j' else 'jcc'} {target:x}\n")
            for op in ins.operands:
                if op.type == x86.X86_OP_IMM:
                    v, kind = op.imm & 0xFFFFFFFF, "operand"
                elif op.type == x86.X86_OP_MEM:
                    # memory: [value] alone (that word read / written); indexed: [value + register(s)]
                    v = op.mem.disp & 0xFFFFFFFF
                    kind = "indexed" if op.mem.base or op.mem.index else "memory"
                else:
                    continue
                if low <= v < high and v != target:
                    out.write(f"{ins.address:x} {kind} {v:x}\n")
            a += ins.size
else:
    raise SystemExit(f"unknown mode {mode}")
