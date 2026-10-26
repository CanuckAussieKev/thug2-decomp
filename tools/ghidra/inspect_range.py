"""Read the Ghidra snapshot of the reference build from the command line (no GUI, no MCP server needed).

    GHIDRA_INSTALL_DIR=C:/ghidra uv run --no-project --python 3.13 --with pyghidra \
        python tools/ghidra/inspect_range.py <snapshot> <command> <args...>

<snapshot> is the folder of a copy of the Ghidra project (tools/ghidra/snapshot_project.ps1 makes one, e.g.
C:/Games/THUG2-ghidra-snapshot). Addresses are hex without 0x. Commands:

    survey <from> <to>          functions in the range: size, callers, REPLACED (if src/ replaces it), name,
                                strings it refers to
    disasm <address>...         each function's instructions (Ghidra's function bodies; for code Ghidra never
                                disassembled use tools/capstone_disasm.py)
    decompile <address>...      Ghidra's C for each function (a starting point only: never trust its return
                                types or parameter counts - read RET n and EAX/AL at each RET)
    table <dwords>:<n>:<bytes>:<m>:<bias>...
                                decode a switch jump table: n dword targets at <dwords>, and m byte indexes at
                                <bytes> (0 when the switch indexes the dwords directly) for case values
                                bias, bias+1, ...; prints each target with its case values
    strings <address>...        the C strings at those addresses
"""
import pathlib
import re
import sys

import pyghidra
from local_config import GHIDRA_PROGRAM, GHIDRA_PROJECT

pyghidra.start()

REPO = pathlib.Path(__file__).resolve().parents[2]


def replaced_addresses():
    found = set()
    for path in (REPO / "src").rglob("*.cpp"):
        for match in re.finditer(r"REPLACE\w*\(0x([0-9A-Fa-f]+)", path.read_text(encoding="utf-8")):
            found.add(int(match.group(1), 16))
    return found


def main():
    snapshot, command, args = sys.argv[1], sys.argv[2], sys.argv[3:]
    with pyghidra.open_project(snapshot, GHIDRA_PROJECT) as project, \
            pyghidra.program_context(project, GHIDRA_PROGRAM) as program:
        functions = program.getFunctionManager()
        listing = program.getListing()
        memory = program.getMemory()
        space = program.getAddressFactory().getDefaultAddressSpace()
        address = lambda value: space.getAddress(value)
        byte = lambda value: memory.getByte(address(value)) & 0xFF

        if command == "survey":
            low, high = int(args[0], 16), int(args[1], 16)
            replaced = replaced_addresses()
            references = program.getReferenceManager()
            for function in functions.getFunctions(address(low), True):
                start = function.getEntryPoint().getOffset()
                if start >= high:
                    break
                callers = sum(1 for r in references.getReferencesTo(function.getEntryPoint())
                              if r.getReferenceType().isCall())
                strings = []
                for instruction in listing.getInstructions(function.getBody(), True):
                    for reference in instruction.getReferencesFrom():
                        data = listing.getDataAt(reference.getToAddress())
                        if data is not None and data.hasStringValue():
                            strings.append(str(data.getValue())[:40])
                print(f"{start:08X} {function.getBody().getNumAddresses():5d} callers={callers:4d} "
                      f"{'REPLACED ' if start in replaced else ''}{function.getName()} {' | '.join(strings[:3])}")
        elif command == "disasm":
            for value in args:
                function = functions.getFunctionAt(address(int(value, 16)))
                print(f"== {value}")
                for instruction in listing.getInstructions(function.getBody(), True):
                    print(f"{instruction.getAddress().getOffset():08X} {instruction}")
        elif command == "decompile":
            from ghidra.app.decompiler import DecompInterface
            from ghidra.util.task import ConsoleTaskMonitor
            decompiler = DecompInterface()
            decompiler.openProgram(program)
            for value in args:
                function = functions.getFunctionAt(address(int(value, 16)))
                result = decompiler.decompileFunction(function, 60, ConsoleTaskMonitor())
                print(f"==== {value} {function.getName()}")
                print(result.getDecompiledFunction().getC() if result.decompileCompleted() else "FAILED")
        elif command == "table":
            for spec in args:
                dwords, count, bytes_at, index_count, bias = [int(part, 16) for part in spec.split(":")]
                dword = lambda at: byte(at) | byte(at + 1) << 8 | byte(at + 2) << 16 | byte(at + 3) << 24
                targets = [dword(dwords + 4 * i) for i in range(count)]
                cases = {}
                for i in range(index_count):
                    target = targets[byte(bytes_at + i) if bytes_at else i]
                    cases.setdefault(target, []).append(i + bias)
                print(f"== {dwords:X}")
                for target, values in cases.items():
                    print(f"  {target:08X}: " + " ".join(f"{v:X}" for v in values))
        elif command == "strings":
            for value in args:
                at, text = int(value, 16), bytearray()
                while byte(at + len(text)):
                    text.append(byte(at + len(text)))
                print(f"{at:08X} {text.decode('latin-1')}")
        else:
            sys.exit(__doc__)


main()
