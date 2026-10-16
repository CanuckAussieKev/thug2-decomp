"""Disassemble a range of THUG2PM.exe with capstone, for code Ghidra never disassembled (blocks only reached
through jump tables, e.g. ParseArray's element loops at 0x471C2B-0x47212D).

    uv run --no-project --python 3.13 --with capstone python tools/capstone_disasm.py 471C2B 47212D [exe]
"""
import struct
import sys

import capstone

exe_path = sys.argv[3] if len(sys.argv) > 3 else r"C:\Games\THUG2\THUG2PM.exe"
exe = open(exe_path, "rb").read()
pe = struct.unpack_from("<I", exe, 0x3C)[0]
sections = struct.unpack_from("<H", exe, pe + 6)[0]
optional = struct.unpack_from("<H", exe, pe + 20)[0]


def file_offset(va):
    rva = va - 0x400000
    for i in range(sections):
        header = pe + 24 + optional + 40 * i
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from("<IIII", exe, header + 8)
        if virtual_address <= rva < virtual_address + max(virtual_size, raw_size):
            return raw_offset + rva - virtual_address
    raise ValueError(hex(va))


low, high = int(sys.argv[1], 16), int(sys.argv[2], 16)
start = file_offset(low)
disassembler = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
for instruction in disassembler.disasm(exe[start:start + (high - low)], low):
    print(f"{instruction.address:08X} {instruction.mnemonic} {instruction.op_str}")
