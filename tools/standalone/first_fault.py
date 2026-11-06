r"""NOT ORIGINAL - run an exe (32-bit or 64-bit: picked by the file's PE machine) under the Win32 debug API and report
its first fault: the exception, the registers, the calls (a 32-bit target's frame-pointer chain; a 64-bit target's
stack unwound by dbghelp from the modules' unwind data) and a scan of the stack for return addresses (words just after
a call instruction in a loaded module), each resolved offline against the module's PDB (addr2sym.py). Also prints the
program's OutputDebugString text and its exit code. No cdb / WinDbg needed. A 64-bit target needs a 64-bit Python.

    python -I tools/standalone/first_fault.py <exe> <working folder> [--seconds N] [--second-chance] [-- args...]

--seconds N      stop the program (and say so) when nothing went wrong in N seconds (default 120)
--watch <rva>    a hardware breakpoint on writes to the bytes at the exe's base + rva (addr2sym.py name=<symbol>
                 finds it): every write is printed with its caller chain, and the run goes on
--watch-size N   how many bytes --watch watches: 1, 2, 4 (the default) or 8 (a 64-bit target only)
--monitor N      as soon as the program's window exists, centre it on \\.\DISPLAYN without activating it (e.g. 2
                 when the primary monitor is busy)
--second-chance  report only exceptions the program doesn't handle itself (default: the first chance of an access
                 violation, illegal instruction, divide by zero, stack overflow, stack-cookie / fast-fail)
The program is always ended when this returns.
"""
import ctypes
import ctypes.wintypes as W
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from addr2sym import Resolver  # noqa: E402

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
u32 = ctypes.WinDLL("user32", use_last_error=True)


class MONITORINFOEX(ctypes.Structure):
    _fields_ = [("cbSize", W.DWORD), ("rcMonitor", W.RECT), ("rcWork", W.RECT), ("dwFlags", W.DWORD),
                ("szDevice", ctypes.c_wchar * 32)]


def monitor_rect(number):
    """The rectangle of \\\\.\\DISPLAY<number> (left, top, right, bottom), or None."""
    found = []
    proc = ctypes.WINFUNCTYPE(W.BOOL, W.HMONITOR, W.HDC, ctypes.POINTER(W.RECT), W.LPARAM)

    def each(handle, _dc, _rect, _data):
        info = MONITORINFOEX()
        info.cbSize = ctypes.sizeof(info)
        if u32.GetMonitorInfoW(handle, ctypes.byref(info)) and info.szDevice.upper().endswith(f"DISPLAY{number}"):
            r = info.rcMonitor
            found.append((r.left, r.top, r.right, r.bottom))
        return True

    u32.EnumDisplayMonitors(None, None, proc(each), 0)
    return found[0] if found else None


def process_window(pid):
    """The process's visible top-level window, or None."""
    found = []
    proc = ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)

    def each(hwnd, _data):
        owner = W.DWORD()
        u32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and u32.IsWindowVisible(hwnd) and not u32.GetWindow(hwnd, 4):  # GW_OWNER
            found.append(hwnd)
        return True

    u32.EnumWindows(proc(each), 0)
    return found[0] if found else None


def move_to_monitor(hwnd, rect):
    """Centres the window on the monitor without activating it (SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS)."""
    r = W.RECT()
    u32.GetWindowRect(hwnd, ctypes.byref(r))
    width, height = r.right - r.left, r.bottom - r.top
    x = rect[0] + max(0, (rect[2] - rect[0] - width) // 2)
    y = rect[1] + max(0, (rect[3] - rect[1] - height) // 2)
    # (SWP_ASYNCWINDOWPOS: the window's thread may be stopped at a debug event - never wait for it)
    u32.SetWindowPos(hwnd, None, x, y, 0, 0, 0x0001 | 0x0004 | 0x0010 | 0x4000)
    return x, y

DEBUG_ONLY_THIS_PROCESS = 2
EXCEPTION_DEBUG_EVENT, CREATE_THREAD_DEBUG_EVENT, CREATE_PROCESS_DEBUG_EVENT, EXIT_PROCESS_DEBUG_EVENT = 1, 2, 3, 5
LOAD_DLL_DEBUG_EVENT, OUTPUT_DEBUG_STRING_EVENT = 6, 8
DBG_CONTINUE, DBG_EXCEPTION_NOT_HANDLED = 0x10002, 0x80010001
FATAL = {0xC0000005: "access violation", 0xC000001D: "illegal instruction", 0xC0000094: "integer divide by zero",
         0xC00000FD: "stack overflow", 0xC0000409: "stack buffer overrun / fast fail", 0xC0000096: "privileged instruction",
         0xC0000374: "heap corruption", 0xC0000417: "invalid CRT parameter"}
QUIET = {0xE06D7363: "C++ exception", 0x406D1388: "thread name", 0x40010006: "debug print", 0x4001000A: "debug print"}


class STARTUPINFO(ctypes.Structure):
    _fields_ = [("cb", W.DWORD), ("lpReserved", W.LPWSTR), ("lpDesktop", W.LPWSTR), ("lpTitle", W.LPWSTR),
                ("dwX", W.DWORD), ("dwY", W.DWORD), ("dwXSize", W.DWORD), ("dwYSize", W.DWORD),
                ("dwXCountChars", W.DWORD), ("dwYCountChars", W.DWORD), ("dwFillAttribute", W.DWORD),
                ("dwFlags", W.DWORD), ("wShowWindow", W.WORD), ("cbReserved2", W.WORD), ("lpReserved2", ctypes.c_void_p),
                ("hStdInput", W.HANDLE), ("hStdOutput", W.HANDLE), ("hStdError", W.HANDLE)]


class PROCESS_INFORMATION(ctypes.Structure):
    _fields_ = [("hProcess", W.HANDLE), ("hThread", W.HANDLE), ("dwProcessId", W.DWORD), ("dwThreadId", W.DWORD)]


class EXCEPTION_RECORD(ctypes.Structure):
    _fields_ = [("ExceptionCode", W.DWORD), ("ExceptionFlags", W.DWORD), ("ExceptionRecord", ctypes.c_void_p),
                ("ExceptionAddress", ctypes.c_void_p), ("NumberParameters", W.DWORD),
                ("ExceptionInformation", ctypes.c_size_t * 15)]


class EXCEPTION_DEBUG_INFO(ctypes.Structure):
    _fields_ = [("ExceptionRecord", EXCEPTION_RECORD), ("dwFirstChance", W.DWORD)]


class CREATE_PROCESS_DEBUG_INFO(ctypes.Structure):
    _fields_ = [("hFile", W.HANDLE), ("hProcess", W.HANDLE), ("hThread", W.HANDLE), ("lpBaseOfImage", ctypes.c_void_p),
                ("dwDebugInfoFileOffset", W.DWORD), ("nDebugInfoSize", W.DWORD), ("lpThreadLocalBase", ctypes.c_void_p),
                ("lpStartAddress", ctypes.c_void_p), ("lpImageName", ctypes.c_void_p), ("fUnicode", W.WORD)]


class LOAD_DLL_DEBUG_INFO(ctypes.Structure):
    _fields_ = [("hFile", W.HANDLE), ("lpBaseOfDll", ctypes.c_void_p), ("dwDebugInfoFileOffset", W.DWORD),
                ("nDebugInfoSize", W.DWORD), ("lpImageName", ctypes.c_void_p), ("fUnicode", W.WORD)]


class OUTPUT_DEBUG_STRING_INFO(ctypes.Structure):
    _fields_ = [("lpDebugStringData", ctypes.c_void_p), ("fUnicode", W.WORD), ("nDebugStringLength", W.WORD)]


class EXIT_PROCESS_DEBUG_INFO(ctypes.Structure):
    _fields_ = [("dwExitCode", W.DWORD)]


class CREATE_THREAD_DEBUG_INFO(ctypes.Structure):
    _fields_ = [("hThread", W.HANDLE), ("lpThreadLocalBase", ctypes.c_void_p), ("lpStartAddress", ctypes.c_void_p)]


class U(ctypes.Union):
    _fields_ = [("Exception", EXCEPTION_DEBUG_INFO), ("CreateProcessInfo", CREATE_PROCESS_DEBUG_INFO),
                ("CreateThread", CREATE_THREAD_DEBUG_INFO),
                ("LoadDll", LOAD_DLL_DEBUG_INFO), ("DebugString", OUTPUT_DEBUG_STRING_INFO),
                ("ExitProcess", EXIT_PROCESS_DEBUG_INFO), ("pad", ctypes.c_byte * 160)]


class DEBUG_EVENT(ctypes.Structure):
    _fields_ = [("dwDebugEventCode", W.DWORD), ("dwProcessId", W.DWORD), ("dwThreadId", W.DWORD), ("u", U)]


class FSA(ctypes.Structure):
    _fields_ = [("ControlWord", W.DWORD), ("StatusWord", W.DWORD), ("TagWord", W.DWORD), ("ErrorOffset", W.DWORD),
                ("ErrorSelector", W.DWORD), ("DataOffset", W.DWORD), ("DataSelector", W.DWORD),
                ("RegisterArea", ctypes.c_byte * 80), ("Cr0NpxState", W.DWORD)]


class WOW64_CONTEXT(ctypes.Structure):
    _fields_ = [("ContextFlags", W.DWORD)] + [(n, W.DWORD) for n in ("Dr0", "Dr1", "Dr2", "Dr3", "Dr6", "Dr7")] + \
               [("FloatSave", FSA)] + [(n, W.DWORD) for n in ("SegGs", "SegFs", "SegEs", "SegDs", "Edi", "Esi", "Ebx",
                "Edx", "Ecx", "Eax", "Ebp", "Eip", "SegCs", "EFlags", "Esp", "SegSs")] + [("Ext", ctypes.c_byte * 512)]


class CONTEXT64(ctypes.Structure):
    """winnt.h's CONTEXT for AMD64 (1232 bytes; GetThreadContext wants it 16-byte aligned: new_context64())."""
    _fields_ = [("P1Home", ctypes.c_ulonglong), ("P2Home", ctypes.c_ulonglong), ("P3Home", ctypes.c_ulonglong),
                ("P4Home", ctypes.c_ulonglong), ("P5Home", ctypes.c_ulonglong), ("P6Home", ctypes.c_ulonglong),
                ("ContextFlags", W.DWORD), ("MxCsr", W.DWORD),
                ("SegCs", W.WORD), ("SegDs", W.WORD), ("SegEs", W.WORD), ("SegFs", W.WORD), ("SegGs", W.WORD),
                ("SegSs", W.WORD), ("EFlags", W.DWORD)] + \
               [(n, ctypes.c_ulonglong) for n in ("Dr0", "Dr1", "Dr2", "Dr3", "Dr6", "Dr7", "Rax", "Rcx", "Rdx", "Rbx",
                "Rsp", "Rbp", "Rsi", "Rdi", "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15", "Rip")] + \
               [("FltSave", ctypes.c_byte * 512), ("VectorRegister", ctypes.c_byte * 416),
                ("VectorControl", ctypes.c_ulonglong), ("DebugControl", ctypes.c_ulonglong),
                ("LastBranchToRip", ctypes.c_ulonglong), ("LastBranchFromRip", ctypes.c_ulonglong),
                ("LastExceptionToRip", ctypes.c_ulonglong), ("LastExceptionFromRip", ctypes.c_ulonglong)]


CONTEXT_AMD64 = 0x100000
CONTEXT64_CONTROL_INTEGER = CONTEXT_AMD64 | 0x3
CONTEXT64_DEBUG_REGISTERS = CONTEXT_AMD64 | 0x10
assert ctypes.sizeof(CONTEXT64) == 1232 and CONTEXT64.Rax.offset == 120 and CONTEXT64.Rip.offset == 248


def new_context64(flags):
    """A CONTEXT64 at a 16-byte aligned address (its buffer kept alive in .raw)."""
    raw = ctypes.create_string_buffer(ctypes.sizeof(CONTEXT64) + 16)
    ctx = CONTEXT64.from_address((ctypes.addressof(raw) + 15) & ~15)
    ctx.raw = raw
    ctx.ContextFlags = flags
    return ctx


class ADDRESS64(ctypes.Structure):
    _fields_ = [("Offset", ctypes.c_ulonglong), ("Segment", W.WORD), ("Mode", ctypes.c_int)]


class KDHELP64(ctypes.Structure):
    _fields_ = [("Thread", ctypes.c_ulonglong), ("ThCallbackStack", W.DWORD), ("ThCallbackBStore", W.DWORD),
                ("NextCallback", W.DWORD), ("FramePointer", W.DWORD)] + \
               [(n, ctypes.c_ulonglong) for n in ("KiCallUserMode", "KeUserCallbackDispatcher", "SystemRangeStart",
                "KiUserExceptionDispatcher", "StackBase", "StackLimit")] + [("Reserved", ctypes.c_ulonglong * 5)]


class STACKFRAME64(ctypes.Structure):
    _fields_ = [("AddrPC", ADDRESS64), ("AddrReturn", ADDRESS64), ("AddrFrame", ADDRESS64), ("AddrStack", ADDRESS64),
                ("AddrBStore", ADDRESS64), ("FuncTableEntry", ctypes.c_void_p), ("Params", ctypes.c_ulonglong * 4),
                ("Far", W.BOOL), ("Virtual", W.BOOL), ("Reserved", ctypes.c_ulonglong * 3), ("KdHelp", KDHELP64)]


_dbghelp = ctypes.WinDLL("dbghelp", use_last_error=True)
_dbghelp.SymInitialize.argtypes = [W.HANDLE, ctypes.c_char_p, W.BOOL]
_dbghelp.SymCleanup.argtypes = [W.HANDLE]
_dbghelp.StackWalk64.argtypes = [W.DWORD, W.HANDLE, W.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p,
                                 ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
_dbghelp.StackWalk64.restype = W.BOOL
k32.OpenThread.restype = W.HANDLE
k32.GetThreadContext.argtypes = [W.HANDLE, ctypes.c_void_p]
k32.SetThreadContext.argtypes = [W.HANDLE, ctypes.c_void_p]


def pe_machine(path):
    """The PE file's machine field (0x14C x86, 0x8664 x64), or None when it isn't a PE file."""
    with open(path, "rb") as f:
        head = f.read(0x40)
        if len(head) < 0x40 or head[:2] != b"MZ":
            return None
        f.seek(struct.unpack_from("<I", head, 0x3C)[0])
        sig = f.read(6)
    return struct.unpack_from("<H", sig, 4)[0] if sig[:4] == b"PE\0\0" else None


def unwind64(process, thread, ctx, limit=48):
    """A 64-bit thread's calls, newest first (the faulting instruction first): dbghelp's StackWalk64 over the modules'
    unwind data. [(rip, rsp)]. The process must be known to dbghelp (SymInitialize)."""
    frames = []
    frame = STACKFRAME64()
    frame.AddrPC.Offset, frame.AddrFrame.Offset, frame.AddrStack.Offset = ctx.Rip, ctx.Rbp, ctx.Rsp
    frame.AddrPC.Mode = frame.AddrFrame.Mode = frame.AddrStack.Mode = 3  # AddrModeFlat
    walk = new_context64(CONTEXT64_CONTROL_INTEGER)  # (StackWalk64 changes the context it is given)
    ctypes.memmove(ctypes.addressof(walk), ctypes.addressof(ctx), ctypes.sizeof(CONTEXT64))
    table = ctypes.cast(_dbghelp.SymFunctionTableAccess64, ctypes.c_void_p)
    module_base = ctypes.cast(_dbghelp.SymGetModuleBase64, ctypes.c_void_p)
    while len(frames) < limit and _dbghelp.StackWalk64(0x8664, process, thread, ctypes.byref(frame),
                                                       ctypes.addressof(walk), None, table, module_base, None):
        if frame.AddrPC.Offset == 0:
            break
        frames.append((frame.AddrPC.Offset, frame.AddrStack.Offset))
    return frames


class Module:
    def __init__(self, base, path, size):
        self.base, self.path, self.size = base, path, size
        self.name = os.path.basename(path)
        self._resolver = None

    def symbol(self, addr):
        if self._resolver is None:
            self._resolver = Resolver(self.path) if os.path.exists(self.path) else False
        rva = addr - self.base
        found = self._resolver.resolve(rva) if self._resolver and self._resolver.ok else None
        return f"{self.name}+0x{rva:X}" + (f" {found}" if found else "")


def main():
    args = sys.argv[1:]
    program_args = []
    if "--" in args:
        program_args = args[args.index("--") + 1:]
        args = args[:args.index("--")]
    seconds = 120.0
    if "--seconds" in args:
        i = args.index("--seconds")
        seconds = float(args[i + 1])
        del args[i:i + 2]
    watch = None
    if "--watch" in args:
        i = args.index("--watch")
        watch = int(args[i + 1], 16)
        del args[i:i + 2]
    watch_size = 4
    if "--watch-size" in args:
        i = args.index("--watch-size")
        watch_size = int(args[i + 1])
        del args[i:i + 2]
        if watch_size not in (1, 2, 4, 8):
            raise SystemExit("--watch-size 1, 2, 4 or 8")
    monitor = None
    if "--monitor" in args:
        i = args.index("--monitor")
        monitor = monitor_rect(int(args[i + 1]))
        if monitor is None:
            raise SystemExit(f"no monitor DISPLAY{args[i + 1]}")
        del args[i:i + 2]
    second_chance_only = "--second-chance" in args
    args = [a for a in args if a != "--second-chance"]
    if len(args) != 2:
        raise SystemExit(__doc__)
    exe, cwd = os.path.abspath(args[0]), os.path.abspath(args[1])
    machine = pe_machine(exe)
    if machine not in (0x14C, 0x8664):
        raise SystemExit(f"{exe} isn't an x86 or x64 PE file (machine {machine})")
    x64 = machine == 0x8664
    if x64 and ctypes.sizeof(ctypes.c_void_p) != 8:
        raise SystemExit("a 64-bit target needs a 64-bit Python")
    if watch_size == 8 and not x64:
        raise SystemExit("--watch-size 8 is for a 64-bit target")
    hex_width = 16 if x64 else 8
    command = f'"{exe}"' + "".join(f" {a}" for a in program_args)

    si = STARTUPINFO()
    si.cb = ctypes.sizeof(si)
    pi = PROCESS_INFORMATION()
    if not k32.CreateProcessW(exe, command, None, None, False, DEBUG_ONLY_THIS_PROCESS, None, cwd, ctypes.byref(si),
                              ctypes.byref(pi)):
        raise SystemExit(f"CreateProcess failed {ctypes.get_last_error()}")

    def read(addr, n):
        buf = (ctypes.c_char * n)()
        got = ctypes.c_size_t()
        ok = k32.ReadProcessMemory(pi.hProcess, ctypes.c_void_p(addr), buf, n, ctypes.byref(got))
        return bytes(buf[:got.value]) if ok else b""

    def dword(addr):
        b = read(addr, 4)
        return struct.unpack("<I", b)[0] if len(b) == 4 else None

    def qword(addr):
        b = read(addr, 8)
        return struct.unpack("<Q", b)[0] if len(b) == 8 else None

    def image_size(base):
        pe = dword(base + 0x3C)
        return dword(base + pe + 0x50) if pe is not None else None

    modules = []

    def module_of(addr):
        for m in modules:
            if m.base <= addr < m.base + (m.size or 0):
                return m
        return None

    def name(addr):
        m = module_of(addr)
        return m.symbol(addr) if m else "?"

    def after_call(addr):
        # a return address: a call instruction ends just before it (E8 rel32, FF /2 with any operand form)
        m = module_of(addr)
        if not m:
            return False
        b = read(addr - 7, 7)
        if len(b) != 7:
            return False
        if b[2] == 0xE8:
            return True
        for back in (2, 3, 6, 7):
            op = b[7 - back]
            modrm = b[7 - back + 1] if back > 1 else None
            if op == 0xFF and modrm is not None and (modrm >> 3) & 7 == 2:
                return True
        return False

    watched = []  # the address the hardware breakpoint watches (set once the image's base is known)

    def arm(thread):
        # DR0 = the address, DR7: local enable 0, break on write (RW0 = 01), 4 bytes (LEN0 = 11)
        if not watched:
            return
        length = {1: 0, 2: 1, 4: 3, 8: 2}[watch_size]  # DR7's LEN0 field
        if x64:
            ctx = new_context64(CONTEXT64_DEBUG_REGISTERS)
            if k32.GetThreadContext(thread, ctypes.addressof(ctx)):
                ctx.Dr0 = watched[0]
                ctx.Dr7 = (ctx.Dr7 & ~0xF0003) | 0x1 | (0x1 << 16) | (length << 18)
                k32.SetThreadContext(thread, ctypes.addressof(ctx))
            return
        ctx = WOW64_CONTEXT()
        ctx.ContextFlags = 0x10010
        if k32.Wow64GetThreadContext(thread, ctypes.byref(ctx)):
            ctx.Dr0 = watched[0]
            ctx.Dr7 = (ctx.Dr7 & ~0xF0003) | 0x1 | (0x1 << 16) | (length << 18)
            k32.Wow64SetThreadContext(thread, ctypes.byref(ctx))

    symbols_ready = []  # dbghelp knows the program (for StackWalk64) once this has an entry

    def report64(ev, rec, what, scan=True):
        th = k32.OpenThread(0x1FFFFF, False, ev.dwThreadId)
        ctx = new_context64(CONTEXT64_CONTROL_INTEGER)
        k32.GetThreadContext(th, ctypes.addressof(ctx))
        info = rec.ExceptionInformation
        extra = ""
        if rec.ExceptionCode == 0xC0000005 and rec.NumberParameters >= 2:
            extra = f", {['read', 'write', '?', '?', '?', '?', '?', '?', 'execute'][min(info[0], 8)]} of 0x{info[1]:016X}"
        if watched and rec.ExceptionCode == 0x80000004:
            held = int.from_bytes(read(watched[0], watch_size), "little")
            extra = f", 0x{watched[0]:016X} now holds 0x{held:0{watch_size * 2}X}"
        print(f"{what} 0x{rec.ExceptionCode:08X} at RIP 0x{ctx.Rip:016X} {name(ctx.Rip)}{extra}, "
              f"{'first' if ev.u.Exception.dwFirstChance else 'second'} chance, thread {ev.dwThreadId}")
        if scan:
            regs = ("Rax", "Rbx", "Rcx", "Rdx", "Rsi", "Rdi", "Rbp", "Rsp", "R8", "R9", "R10", "R11", "R12", "R13", "R14",
                    "R15")
            for i in range(0, len(regs), 4):
                print("  " + " ".join(f"{n.upper():<3} {getattr(ctx, n):016X}" for n in regs[i:i + 4]))
        if not symbols_ready:
            # (our own search path: no symbol server; deferred loads: only the modules a frame is in)
            _dbghelp.SymSetOptions(0x2 | 0x4 | 0x80000)
            _dbghelp.SymInitialize(pi.hProcess, os.path.dirname(exe).encode(), True)
            symbols_ready.append(True)
        print("  calls (unwound from the context, newest first):")
        for rip, rsp in unwind64(pi.hProcess, th, ctx, 48 if scan else 12):
            print(f"    [rsp={rsp:016X}] {rip:016X} {name(rip)}")
        if not scan:
            return
        print("  stack scan (return addresses):")
        shown = 0
        for i in range(0, 0x4000, 8):
            v = qword(ctx.Rsp + i)
            if v is None or shown >= 40:
                break
            if after_call(v):
                print(f"    rsp+{i:04X}: {v:016X} {name(v)}")
                shown += 1

    def report(ev, rec, what, scan=True):
        if x64:
            report64(ev, rec, what, scan)
            return
        th = k32.OpenThread(0x1FFFFF, False, ev.dwThreadId)
        ctx = WOW64_CONTEXT()
        ctx.ContextFlags = 0x10007
        k32.Wow64GetThreadContext(th, ctypes.byref(ctx))
        info = rec.ExceptionInformation
        extra = ""
        if rec.ExceptionCode == 0xC0000005 and rec.NumberParameters >= 2:
            extra = f", {['read', 'write', '?', '?', '?', '?', '?', '?', 'execute'][min(info[0], 8)]} of 0x{info[1]:08X}"
        if watched and rec.ExceptionCode == 0x80000004:
            extra = f", 0x{watched[0]:08X} now holds 0x{dword(watched[0]) or 0:08X}"
        print(f"{what} 0x{rec.ExceptionCode:08X} at EIP 0x{ctx.Eip:08X} {name(ctx.Eip)}{extra}, "
              f"{'first' if ev.u.Exception.dwFirstChance else 'second'} chance, thread {ev.dwThreadId}")
        if not scan:
            ebp, seen = ctx.Ebp, 0
            while ebp and seen < 12:
                ret, nxt = dword(ebp + 4), dword(ebp)
                if ret is None or nxt is None:
                    break
                print(f"    {ret:08X} {name(ret)}")
                if nxt <= ebp:
                    break
                ebp, seen = nxt, seen + 1
            return
        print(f"  eax {ctx.Eax:08X} ebx {ctx.Ebx:08X} ecx {ctx.Ecx:08X} edx {ctx.Edx:08X} esi {ctx.Esi:08X} "
              f"edi {ctx.Edi:08X} ebp {ctx.Ebp:08X} esp {ctx.Esp:08X}")
        print("  frame chain (ebp):")
        ebp, seen = ctx.Ebp, 0
        while ebp and seen < 48:
            ret, nxt = dword(ebp + 4), dword(ebp)
            if ret is None or nxt is None:
                break
            print(f"    {ret:08X} {name(ret)}")
            if nxt <= ebp:
                break
            ebp, seen = nxt, seen + 1
        print("  stack scan (return addresses):")
        shown = 0
        for i in range(0, 0x4000, 4):
            v = dword(ctx.Esp + i)
            if v is None or shown >= 40:
                break
            if after_call(v):
                print(f"    esp+{i:04X}: {v:08X} {name(v)}")
                shown += 1

    ev = DEBUG_EVENT()
    started = time.time()
    seen_initial_break = False
    done = False
    moved = monitor is None
    while not done:
        left = seconds - (time.time() - started)
        if left <= 0:
            print(f"no fault within {seconds:g} s: stopping the program")
            break
        if not moved:
            hwnd = process_window(pi.dwProcessId)
            if hwnd:
                x, y = move_to_monitor(hwnd, monitor)
                print(f"window moved to ({x}, {y})")
                moved = True
        if not k32.WaitForDebugEvent(ctypes.byref(ev), int(min(left, 0.2 if not moved else left) * 1000)):
            continue
        code, cont = ev.dwDebugEventCode, DBG_CONTINUE
        if code == CREATE_PROCESS_DEBUG_EVENT:
            base = ev.u.CreateProcessInfo.lpBaseOfImage
            modules.append(Module(base, exe, image_size(base)))
            print(f"{os.path.basename(exe)} loaded at 0x{base:0{hex_width}X}")
            if watch is not None:
                watched.append(base + watch)
                print(f"watching writes to 0x{base + watch:0{hex_width}X}")
                arm(ev.u.CreateProcessInfo.hThread)
        elif code == CREATE_THREAD_DEBUG_EVENT:
            arm(ev.u.CreateThread.hThread)
        elif code == LOAD_DLL_DEBUG_EVENT:
            b = ev.u.LoadDll.lpBaseOfDll
            path = ctypes.create_unicode_buffer(1024)
            k32.GetFinalPathNameByHandleW(ev.u.LoadDll.hFile, path, 1024, 0)
            p = path.value[4:] if path.value.startswith("\\\\?\\") else path.value
            modules.append(Module(b, p, image_size(b)))
        elif code == OUTPUT_DEBUG_STRING_EVENT:
            d = ev.u.DebugString
            raw = read(d.lpDebugStringData, d.nDebugStringLength * (2 if d.fUnicode else 1))
            text = raw.decode("utf-16-le" if d.fUnicode else "latin-1", errors="replace").rstrip("\0").rstrip()
            if text:
                print(f"[debug] {text}")
        elif code == EXCEPTION_DEBUG_EVENT:
            rec = ev.u.Exception.ExceptionRecord
            first = ev.u.Exception.dwFirstChance
            if rec.ExceptionCode in (0x80000003, 0x4000001F) and not seen_initial_break:
                seen_initial_break = True
                th = k32.OpenThread(0x1FFFFF, False, ev.dwThreadId)
                arm(th)
            elif rec.ExceptionCode in (0x80000004, 0x4000001E) and watched:
                report(ev, rec, "WRITE", scan=False)
            elif rec.ExceptionCode in QUIET:
                cont = DBG_EXCEPTION_NOT_HANDLED
            elif not first or (rec.ExceptionCode in FATAL and not second_chance_only):
                report(ev, rec, FATAL.get(rec.ExceptionCode, "exception").upper())
                done = True
            else:
                print(f"  (first-chance exception 0x{rec.ExceptionCode:08X} at {name(rec.ExceptionAddress or 0)}: "
                      "passed to the program)")
                cont = DBG_EXCEPTION_NOT_HANDLED
        elif code == EXIT_PROCESS_DEBUG_EVENT:
            print(f"process exited with code 0x{ev.u.ExitProcess.dwExitCode:08X}")
            done = True
        if not done:
            k32.ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, cont)
    if symbols_ready:
        _dbghelp.SymCleanup(pi.hProcess)
    k32.TerminateProcess(pi.hProcess, 1)
    k32.WaitForSingleObject(pi.hProcess, 10000)


if __name__ == "__main__":
    main()
