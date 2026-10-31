"""NOT ORIGINAL - resolve addresses in a 32-bit exe / DLL to function+offset and file:line with its PDB (DbgHelp),
offline: the module is loaded at a made-up base, so give RVAs (a run's address minus the module's base) - or
run-time addresses with --base <the run's base>.

    python -I tools/standalone/addr2sym.py <exe> <rva> [<rva> ...] [--base <hex>]
    python -I tools/standalone/addr2sym.py <exe> name=<symbol>    its RVA, size and the symbols next to it

Also imported by first_fault.py (resolve()).
"""
import ctypes
import ctypes.wintypes as W
import os
import sys

_dbghelp = ctypes.WinDLL("dbghelp", use_last_error=True)


class SYMBOL_INFO(ctypes.Structure):
    _fields_ = [("SizeOfStruct", W.ULONG), ("TypeIndex", W.ULONG), ("Reserved", ctypes.c_ulonglong * 2),
                ("Index", W.ULONG), ("Size", W.ULONG), ("ModBase", ctypes.c_ulonglong), ("Flags", W.ULONG),
                ("Value", ctypes.c_ulonglong), ("Address", ctypes.c_ulonglong), ("Register", W.ULONG),
                ("Scope", W.ULONG), ("Tag", W.ULONG), ("NameLen", W.ULONG), ("MaxNameLen", W.ULONG),
                ("Name", ctypes.c_char * 1024)]


class IMAGEHLP_LINE64(ctypes.Structure):
    _fields_ = [("SizeOfStruct", W.DWORD), ("Key", ctypes.c_void_p), ("LineNumber", W.DWORD),
                ("FileName", ctypes.c_char_p), ("Address", ctypes.c_ulonglong)]


_OFFLINE_BASE = 0x10000000


class Resolver:
    """One module loaded offline at a fixed base; resolve(rva) -> "name+0xoff file:line" or None."""

    _next_handle = 0x5000

    def __init__(self, path):
        Resolver._next_handle += 1
        self.handle = ctypes.c_void_p(Resolver._next_handle)
        _dbghelp.SymSetOptions(0x2 | 0x10 | 0x40000)  # undecorate, load lines, no prompts
        _dbghelp.SymInitialize(self.handle, os.path.dirname(os.path.abspath(path)).encode(), False)
        self.ok = bool(_dbghelp.SymLoadModuleEx(self.handle, None, os.path.abspath(path).encode(), None,
                                                ctypes.c_ulonglong(_OFFLINE_BASE), 0, None, 0))

    def resolve(self, rva):
        addr = _OFFLINE_BASE + rva
        sym = SYMBOL_INFO()
        sym.SizeOfStruct = 88
        sym.MaxNameLen = 1000
        disp = ctypes.c_ulonglong()
        if not _dbghelp.SymFromAddr(self.handle, ctypes.c_ulonglong(addr), ctypes.byref(disp), ctypes.byref(sym)):
            return None
        text = f"{sym.Name.decode(errors='replace')}+0x{disp.value:X}"
        line = IMAGEHLP_LINE64()
        line.SizeOfStruct = ctypes.sizeof(IMAGEHLP_LINE64)
        d2 = W.DWORD()
        if _dbghelp.SymGetLineFromAddr64(self.handle, ctypes.c_ulonglong(addr), ctypes.byref(d2), ctypes.byref(line)):
            text += f" {line.FileName.decode(errors='replace')}:{line.LineNumber}"
        return text

    def find(self, name):
        """A symbol's RVA and size by name ("Namespace::name" or a mask with *), or None."""
        sym = SYMBOL_INFO()
        sym.SizeOfStruct = 88
        sym.MaxNameLen = 1000
        if not _dbghelp.SymFromName(self.handle, name.encode(), ctypes.byref(sym)):
            return None
        return sym.Address - _OFFLINE_BASE, sym.Size

    def all_symbols(self, mask="*"):
        """Every symbol matching the mask: [(rva, size, name)]."""
        found = []
        callback_type = ctypes.WINFUNCTYPE(W.BOOL, ctypes.POINTER(SYMBOL_INFO), W.ULONG, ctypes.c_void_p)

        def collect(info, size, _context):
            s = info.contents
            name = ctypes.string_at(ctypes.addressof(s) + SYMBOL_INFO.Name.offset, s.NameLen)
            found.append((s.Address - _OFFLINE_BASE, s.Size, name.decode(errors="replace")))
            return True

        callback = callback_type(collect)
        _dbghelp.SymEnumSymbols(self.handle, ctypes.c_ulonglong(_OFFLINE_BASE), mask.encode(), callback, None)
        return found

    def around(self, rva, before=0x40, after=0x40):
        """The symbols in [rva - before, rva + after): [(rva, size, name)]."""
        found = {}
        a = rva - before
        while a < rva + after:
            sym = SYMBOL_INFO()
            sym.SizeOfStruct = 88
            sym.MaxNameLen = 1000
            disp = ctypes.c_ulonglong()
            if _dbghelp.SymFromAddr(self.handle, ctypes.c_ulonglong(_OFFLINE_BASE + a), ctypes.byref(disp),
                                    ctypes.byref(sym)):
                start = sym.Address - _OFFLINE_BASE
                found[start] = (start, sym.Size, sym.Name.decode(errors="replace"))
                a = max(a + 1, start + max(sym.Size, 1))
            else:
                a += 4
        return sorted(found.values())


def main():
    args = sys.argv[1:]
    base = 0
    if "--base" in args:
        i = args.index("--base")
        base = int(args[i + 1], 16)
        del args[i:i + 2]
    if len(args) < 2:
        raise SystemExit(__doc__)
    r = Resolver(args[0])
    if not r.ok:
        raise SystemExit(f"couldn't load {args[0]} (error {ctypes.get_last_error()})")
    for a in args[1:]:
        if a.startswith("name="):
            found = r.find(a[5:])
            if not found:
                print(f"{a[5:]}: not found")
                continue
            print(f"{a[5:]}: rva 0x{found[0]:X}, size 0x{found[1]:X}; neighbours:")
            for start, size, nm in r.around(found[0]):
                print(f"  0x{start:X} +0x{size:X} {nm}")
            continue
        rva = int(a, 16) - base
        print(f"{a} {r.resolve(rva) or '?'}")


if __name__ == "__main__":
    main()
