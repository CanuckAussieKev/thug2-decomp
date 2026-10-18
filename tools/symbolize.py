"""Turn the DINPUT8.dll+0x... offsets of a crash report in thug2-decomp.log into function names and source lines.

    python -I tools/symbolize.py [--pdb-dir out/build/verify] [log or offsets...]

With a log file: symbolizes every "DINPUT8.dll+0x..." in its last EXCEPTION report. With hex offsets: those.
The PDB must come from the same build as the DLL that crashed (compare the deployed DLL's timestamp).
Uses Windows' dbghelp.dll.
"""
import argparse
import ctypes
import ctypes.wintypes as wt
import os
import re
import sys

BASE = 0x10000000  # any base: offsets are relative


class SYMBOL_INFO(ctypes.Structure):
    _fields_ = [("SizeOfStruct", wt.ULONG), ("TypeIndex", wt.ULONG), ("Reserved", ctypes.c_uint64 * 2),
                ("Index", wt.ULONG), ("Size", wt.ULONG), ("ModBase", ctypes.c_uint64), ("Flags", wt.ULONG),
                ("Value", ctypes.c_uint64), ("Address", ctypes.c_uint64), ("Register", wt.ULONG),
                ("Scope", wt.ULONG), ("Tag", wt.ULONG), ("NameLen", wt.ULONG), ("MaxNameLen", wt.ULONG),
                ("Name", ctypes.c_char * 1024)]


class IMAGEHLP_LINE64(ctypes.Structure):
    _fields_ = [("SizeOfStruct", wt.DWORD), ("Key", ctypes.c_void_p), ("LineNumber", wt.DWORD),
                ("FileName", ctypes.c_char_p), ("Address", ctypes.c_uint64)]


def offsets_from_log(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        text = f.read()
    start = text.rfind("EXCEPTION ")
    if start < 0:
        sys.exit("no EXCEPTION report in " + path)
    report = text[start:]
    end = re.search(r"\n(?! )", report[report.find("\n") + 1:])
    if end:
        report = report[:report.find("\n") + 1 + end.start()]
    print(report.splitlines()[0])
    return [int(m, 16) for m in re.findall(r"DINPUT8\.dll\+0x([0-9A-Fa-f]+)", report, re.I)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pdb-dir", default=os.path.join("out", "build", "verify"))
    ap.add_argument("items", nargs="*", default=[r"C:\Games\THUG2\thug2-decomp.log"])
    args = ap.parse_args()
    offsets = []
    for item in args.items:
        offsets += offsets_from_log(item) if os.path.isfile(item) else [int(item, 16)]

    dll = os.path.abspath(os.path.join(args.pdb_dir, "dinput8.dll"))
    dbghelp = ctypes.WinDLL("dbghelp.dll")
    dbghelp.SymLoadModuleEx.restype = ctypes.c_uint64
    dbghelp.SymLoadModuleEx.argtypes = [wt.HANDLE, wt.HANDLE, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64,
                                        wt.DWORD, ctypes.c_void_p, wt.DWORD]
    dbghelp.SymFromAddr.argtypes = [wt.HANDLE, ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint64),
                                    ctypes.POINTER(SYMBOL_INFO)]
    dbghelp.SymGetLineFromAddr64.argtypes = [wt.HANDLE, ctypes.c_uint64, ctypes.POINTER(wt.DWORD),
                                             ctypes.POINTER(IMAGEHLP_LINE64)]
    process = wt.HANDLE(0x7357)  # a pseudo handle: dbghelp only uses it as a key
    dbghelp.SymSetOptions(0x2 | 0x10)  # SYMOPT_UNDNAME | SYMOPT_LOAD_LINES
    if not dbghelp.SymInitialize(process, os.path.dirname(dll).encode(), False):
        sys.exit("SymInitialize failed")
    if not dbghelp.SymLoadModuleEx(process, None, dll.encode(), None, BASE, os.path.getsize(dll), None, 0):
        sys.exit("SymLoadModuleEx failed for " + dll)

    for off in offsets:
        sym = SYMBOL_INFO(SizeOfStruct=88, MaxNameLen=1024)  # 88: sizeof(SYMBOL_INFO) in C
        disp = ctypes.c_uint64()
        name = "?"
        if dbghelp.SymFromAddr(process, BASE + off, ctypes.byref(disp), ctypes.byref(sym)):
            name = "%s+0x%X" % (sym.Name.decode(errors="replace"), disp.value)
        line = IMAGEHLP_LINE64(SizeOfStruct=ctypes.sizeof(IMAGEHLP_LINE64))
        ldisp = wt.DWORD()
        where = ""
        if dbghelp.SymGetLineFromAddr64(process, BASE + off, ctypes.byref(ldisp), ctypes.byref(line)):
            where = "  %s:%d" % (line.FileName.decode(errors="replace"), line.LineNumber)
        print("0x%06X  %s%s" % (off, name, where))
    dbghelp.SymCleanup(process)


if __name__ == "__main__":
    main()
