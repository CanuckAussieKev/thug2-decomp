# Export a function inventory from the Ghidra project to JSON (read-only).
# Run against a snapshot so the MCP server can keep the real project locked:
#   powershell tools/ghidra/snapshot_project.ps1
#   set GHIDRA_INSTALL_DIR=C:\ghidra
#   uv run --no-project --python 3.13 --with pyghidra python tools/ghidra/export_functions.py C:\Games\THUG2-ghidra-snapshot data/derived/functions.json
import json, sys
import pyghidra

pyghidra.start()
from ghidra.util.task import TaskMonitor

project_dir, out_path = sys.argv[1], sys.argv[2]
mon = TaskMonitor.DUMMY


def read_cstring(mem, addr, limit=200):
    """Return the NUL-terminated printable string at addr, or None."""
    out = bytearray()
    try:
        for i in range(limit):
            b = mem.getByte(addr.add(i)) & 0xFF
            if b == 0:
                break
            if not (32 <= b < 127 or b in (9, 10, 13)):
                return None
            out.append(b)
        else:
            return None
    except Exception:
        return None
    return out.decode("latin1") if len(out) >= 4 else None


with pyghidra.open_project(project_dir, "THUG2PM_ghidra_mcp") as project:
    with pyghidra.program_context(project, "/THUG2PM.exe") as prog:
        fm, listing, mem = prog.getFunctionManager(), prog.getListing(), prog.getMemory()
        refman = prog.getReferenceManager()
        text = mem.getBlock(".text")
        funcs = []
        for f in fm.getFunctions(True):
            entry = f.getEntryPoint()
            if not text.contains(entry):
                continue
            callees, imports = set(), set()
            for c in f.getCalledFunctions(mon):
                if c.isExternal():
                    loc = c.getExternalLocation()
                    imports.add(f"{loc.getLibraryName()}!{c.getName()}")
                elif c.isThunk() and c.getThunkedFunction(True).isExternal():
                    t = c.getThunkedFunction(True)
                    imports.add(f"{t.getExternalLocation().getLibraryName()}!{t.getName()}")
                else:
                    callees.add(c.getEntryPoint().getOffset())
            strings, data_refs = [], set()
            for ins in listing.getInstructions(f.getBody(), True):
                for ref in ins.getReferencesFrom():
                    to = ref.getToAddress()
                    if not to.isMemoryAddress() or text.contains(to):
                        continue
                    data_refs.add(to.getOffset())
                    d = listing.getDataAt(to)
                    s = None
                    if d is not None and d.hasStringValue():
                        s = str(d.getValue())
                    elif d is None or not d.isDefined():
                        s = read_cstring(mem, to)
                    if s and s not in strings:
                        strings.append(s[:120])
            # Data locations that hold a pointer to this function (vtables, callback tables).
            ptr_from = sorted({r.getFromAddress().getOffset() for r in refman.getReferencesTo(entry)
                               if not text.contains(r.getFromAddress())})
            funcs.append({
                "addr": entry.getOffset(),
                "end": f.getBody().getMaxAddress().getOffset(),
                "size": f.getBody().getNumAddresses(),
                "name": f.getName(),
                "callers": sorted(c.getEntryPoint().getOffset() for c in f.getCallingFunctions(mon)),
                "callees": sorted(callees),
                "imports": sorted(imports),
                "strings": strings,
                "data_refs": sorted(data_refs),
                "ptr_from": ptr_from,
                "comment": f.getComment(),
            })
        json.dump(funcs, open(out_path, "w"), separators=(",", ":"))
        print(f"exported {len(funcs)} functions to {out_path}")
