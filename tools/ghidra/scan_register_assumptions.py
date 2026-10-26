# Safety check for replaced functions: find original callers that read ECX or EDX after calling one of them
# without writing the register first. Normally ECX/EDX don't survive a call, but this exe was built with
# whole-program optimisation, so a caller may rely on a particular callee leaving them alone - and our
# compiled replacements don't promise that.
#
# Read-only. Run against a snapshot so the MCP server can keep the real project:
#   powershell tools/ghidra/snapshot_project.ps1
#   set GHIDRA_INSTALL_DIR=C:\ghidra
#   uv run --no-project --python 3.13 --with pyghidra python tools/ghidra/scan_register_assumptions.py C:\Games\THUG2-ghidra-snapshot
import glob, re, sys
import pyghidra
from local_config import GHIDRA_PROGRAM, GHIDRA_PROJECT

pyghidra.start()

TRACKED = ("ECX", "EDX")
MAX_STEPS = 64

project_dir = sys.argv[1]
replaced = {}
for path in glob.glob("src/game/**/*.cpp", recursive=True):
    for m in re.finditer(r"REPLACE(?:_SHADOW|_UNCHECKED)?\(0x([0-9A-Fa-f]{8}),\s*([^;]+)\);", open(path, encoding="utf-8").read()):
        replaced[int(m.group(1), 16)] = m.group(2).strip()


def base_names(objects):
    names = set()
    for o in objects:
        if hasattr(o, "getBaseRegister"):
            names.add(str(o.getBaseRegister().getName()))
    return names


def operands(ins):
    return [str(ins.getDefaultOperandRepresentation(i)) for i in range(ins.getNumOperands())]


def ignores_old_value(ins, listing):
    """Instructions that mention ECX/EDX as an input without depending on its value."""
    mnemonic, ops = str(ins.getMnemonicString()), operands(ins)
    # xor ecx, ecx / sub ecx, ecx: zeroing.  or ecx, -1: sets -1.  lea ecx, [ecx]: alignment padding.
    if mnemonic in ("XOR", "SUB") and len(ops) == 2 and ops[0] == ops[1]:
        return True
    if mnemonic == "OR" and len(ops) == 2 and ops[1].lower() in ("0xffffffff", "-0x1"):
        return True
    if mnemonic == "LEA" and len(ops) == 2 and ops[1] == f"[{ops[0]}]":
        return True
    # push ecx used to reserve stack space: the slot is overwritten before anything reads it.
    if mnemonic == "PUSH" and len(ops) == 1 and ops[0] in TRACKED + ("EAX",):
        nxt = ins
        for _ in range(6):
            nxt = listing.getInstructionAt(nxt.getFallThrough()) if nxt.getFallThrough() else None
            if nxt is None or nxt.getFlowType().isCall() or nxt.getFlowType().isJump():
                break
            mnemonic_next = str(nxt.getMnemonicString())
            ops_next = operands(nxt)
            first = ops_next[0] if ops_next else ""
            # The pushed slot is [ESP] until the stack pointer moves.
            if mnemonic_next in ("PUSH", "POP") or first == "ESP":
                break
            if "[ESP]" in first and mnemonic_next in ("FSTP", "FST", "MOV", "FISTP"):
                return True  # overwritten before anything reads it
            if any("[ESP]" in o for o in ops_next):
                break  # the slot itself is read
            # Accesses to other slots ([ESP + n]) and LEA don't touch the pushed value.
    return False


with pyghidra.open_project(project_dir, GHIDRA_PROJECT) as project:
    with pyghidra.program_context(project, GHIDRA_PROGRAM) as prog:
        listing = prog.getListing()
        refs = prog.getReferenceManager()
        fm = prog.getFunctionManager()
        space = prog.getAddressFactory().getDefaultAddressSpace()

        def original_writes(address):
            f = fm.getFunctionAt(space.getAddress(address))
            written = set()
            if f is not None:
                for ins in listing.getInstructions(f.getBody(), True):
                    written |= base_names(ins.getResultObjects()) & set(TRACKED)
            return written

        findings = []
        checked_sites = 0
        for target, name in sorted(replaced.items()):
            clobbers = original_writes(target)
            for ref in refs.getReferencesTo(space.getAddress(target)):
                kind = ref.getReferenceType()
                if not (kind.isCall() or kind.isJump()):
                    continue
                site = listing.getInstructionAt(ref.getFromAddress())
                if site is None or not kind.isCall():
                    continue  # a tail jump never returns to the caller
                checked_sites += 1
                caller = fm.getFunctionContaining(site.getAddress())

                # Walk every path from the instruction after the call.
                work = [(site.getFallThrough(), set(TRACKED), 0)]
                seen = set()
                while work:
                    addr, live, steps = work.pop()
                    if addr is None or steps > MAX_STEPS or not live:
                        continue
                    key = (addr.getOffset(), tuple(sorted(live)))
                    if key in seen:
                        continue
                    seen.add(key)
                    ins = listing.getInstructionAt(addr)
                    if ins is None:
                        continue
                    reads = set() if ignores_old_value(ins, listing) else base_names(ins.getInputObjects())
                    for reg in live & reads:
                        findings.append((target, name, reg, site.getAddress().getOffset(),
                                         caller.getName() if caller else "?", ins.getAddress().getOffset(),
                                         str(ins), reg in clobbers))
                    live = live - reads - base_names(ins.getResultObjects())
                    flow = ins.getFlowType()
                    if flow.isCall():
                        live = set()  # another call: assume it changes them
                    if flow.isTerminal():
                        continue
                    for nxt in list(ins.getFlows()) + ([ins.getFallThrough()] if ins.getFallThrough() else []):
                        work.append((nxt, live, steps + 1))

        print(f"{len(replaced)} replaced functions, {checked_sites} call sites checked")
        risks = 0
        for target, name, reg, site, caller, at, text, clobbered in findings:
            caller_func = fm.getFunctionContaining(space.getAddress(site))
            caller_replaced = caller_func is not None and caller_func.getEntryPoint().getOffset() in replaced
            if caller_replaced:
                note = "caller is replaced too (its original code no longer runs) - safe"
            elif clobbered:
                note = "original also writes it (probably a false alarm)"
            else:
                note = "ORIGINAL PRESERVES IT - RISK"
                risks += 1
            print(f"0x{target:08X} {name}: {reg} read at 0x{at:08X} '{text}' after call at 0x{site:08X} "
                  f"in {caller} - {note}")
        print(f"{risks} call site(s) need attention." if risks else "No risky call sites.")

        # Second pass: which callers use the return value (EAX), and how much of it. Compare this with each
        # replacement's C++ return type: a function we declared void must not have callers reading EAX
        # (Lzss::Decode was exactly that bug), and one returning bool (AL only) must not have callers reading
        # all of EAX.
        def reg_names(objects):
            return {str(o.getName()) for o in objects if hasattr(o, "getBaseRegister")
                    and str(o.getBaseRegister().getName()) == "EAX"}

        print()
        print("Return-value use (EAX read after the call, before being written):")
        for target, name in sorted(replaced.items()):
            widths, sites = set(), 0
            for ref in refs.getReferencesTo(space.getAddress(target)):
                if not ref.getReferenceType().isCall():
                    continue
                site = listing.getInstructionAt(ref.getFromAddress())
                if site is None:
                    continue
                caller_func = fm.getFunctionContaining(site.getAddress())
                if caller_func is not None and caller_func.getEntryPoint().getOffset() in replaced:
                    continue  # its original code no longer runs
                work, seen, used = [(site.getFallThrough(), 0)], set(), False
                while work:
                    addr, steps = work.pop()
                    if addr is None or steps > MAX_STEPS or addr.getOffset() in seen:
                        continue
                    seen.add(addr.getOffset())
                    ins = listing.getInstructionAt(addr)
                    if ins is None:
                        continue
                    read = set() if ignores_old_value(ins, listing) else reg_names(ins.getInputObjects())
                    if read:
                        widths |= read
                        used = True
                        continue
                    if reg_names(ins.getResultObjects()) or ins.getFlowType().isCall() or ins.getFlowType().isTerminal():
                        continue  # EAX overwritten, or the path ends
                    for nxt in list(ins.getFlows()) + ([ins.getFallThrough()] if ins.getFallThrough() else []):
                        work.append((nxt, steps + 1))
                sites += used
            if sites:
                print(f"  0x{target:08X} {name}: {sites} site(s) read {', '.join(sorted(widths))}")
