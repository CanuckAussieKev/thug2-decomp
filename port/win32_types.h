#pragma once

// NOT ORIGINAL - the macOS port's Win32 types (included by port/win32_shim/windows.h, which adds the functions). See that
// file: the game's code names these, and they mean here what they mean on Windows - the 32-bit DWORD, the 32-bit
// HRESULT whose sign a caller tests, the LARGE_INTEGER a timer is set with.

#include <cstddef>
#include <cstdint>

// --- the scalar types ---------------------------------------------------------------------------------------------
using BOOL = int;
using BYTE = std::uint8_t;
using WORD = std::uint16_t;
using DWORD = std::uint32_t;
using QWORD = std::uint64_t;
using LONG = std::int32_t;
using ULONG = std::uint32_t;
using LONGLONG = std::int64_t;
using ULONGLONG = std::uint64_t;
using SHORT = std::int16_t;
using USHORT = std::uint16_t;
using INT = int;
using UINT = unsigned int;
using CHAR = char;
using WCHAR = wchar_t;
using TCHAR = char;
using FLOAT = float;
using DOUBLE = double;
using HRESULT = std::int32_t; // (32 bits, as on Windows: its sign is what a caller tests)
using ATOM = std::uint16_t;
using SIZE_T = std::size_t;
using DWORD_PTR = std::uintptr_t;
using UINT_PTR = std::uintptr_t;
using INT_PTR = std::intptr_t;
using ULONG_PTR = std::uintptr_t;
using VOID = void;

using LPVOID = void*;
using LPCVOID = const void*;
using LPSTR = char*;
using LPCSTR = const char*;
using LPWSTR = wchar_t*;
using LPCWSTR = const wchar_t*;
using LPBYTE = BYTE*;

// Win32's integer aliases (the game's own code and GameSpy's use both spellings)
using UINT8 = std::uint8_t;
using UINT16 = std::uint16_t;
using UINT32 = std::uint32_t;
using UINT64 = std::uint64_t;
using INT8 = std::int8_t;
using INT16 = std::int16_t;
using INT32 = std::int32_t;
using INT64 = std::int64_t;

// The one calling convention: on x86 Windows it decided who popped the stack; on arm64 there is only one.
#define WIN32_STDCALL
#define WINAPI
#define APIENTRY
#define CALLBACK
#define CDECL

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef MAX_PATH
#define MAX_PATH 260
#endif

#define INFINITE 0xFFFFFFFFU
#define WAIT_OBJECT_0 0x00000000U
#define WAIT_TIMEOUT 0x00000102U
#define WAIT_FAILED 0xFFFFFFFFU
#define WAIT_ABANDONED 0x00000080U

#define ERROR_SUCCESS 0L
#define ERROR_FILE_NOT_FOUND 2L
#define ERROR_PATH_NOT_FOUND 3L
#define ERROR_ACCESS_DENIED 5L
#define ERROR_INVALID_HANDLE 6L
#define ERROR_NOT_ENOUGH_MEMORY 8L
#define ERROR_INVALID_PARAMETER 87L
#define ERROR_INSUFFICIENT_BUFFER 122L
#define ERROR_PROC_NOT_FOUND 127L
#define ERROR_ALREADY_EXISTS 183L
#define ERROR_MORE_DATA 234L
#define ERROR_NO_MORE_FILES 18L

// file access, sharing, disposition and attributes
#define GENERIC_READ 0x80000000U
#define GENERIC_WRITE 0x40000000U
#define FILE_SHARE_READ 0x00000001U
#define FILE_SHARE_WRITE 0x00000002U
#define CREATE_NEW 1U
#define CREATE_ALWAYS 2U
#define OPEN_EXISTING 3U
#define OPEN_ALWAYS 4U
#define TRUNCATE_EXISTING 5U
#define FILE_ATTRIBUTE_NORMAL 0x00000080U
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010U
#define FILE_ATTRIBUTE_READONLY 0x00000001U
#define FILE_FLAG_OVERLAPPED 0x40000000U
#define FILE_FLAG_SEQUENTIAL_SCAN 0x08000000U
#define INVALID_FILE_SIZE 0xFFFFFFFFU
#define INVALID_SET_FILE_POINTER 0xFFFFFFFFU
#define FILE_BEGIN 0U
#define FILE_CURRENT 1U
#define FILE_END 2U
#define MOVEFILE_REPLACE_EXISTING 0x00000001U
#define MOVEFILE_COPY_ALLOWED 0x00000002U

// the heap
#define MEM_COMMIT 0x00001000U
#define MEM_RESERVE 0x00002000U
#define MEM_RELEASE 0x00008000U
#define MEM_RESET 0x00080000U
#define PAGE_NOACCESS 0x01U
#define PAGE_READONLY 0x02U
#define PAGE_READWRITE 0x04U
#define PAGE_EXECUTE_READWRITE 0x40U

// the registry
#define HKEY_CLASSES_ROOT ((HKEY)0)
#define HKEY_CURRENT_USER ((HKEY)1)
#define HKEY_LOCAL_MACHINE ((HKEY)2)
#define KEY_READ 0x00020019U
#define KEY_WRITE 0x00020006U
#define KEY_ALL_ACCESS 0xF003FU
#define REG_SZ 1U
#define REG_EXPAND_SZ 2U
#define REG_BINARY 3U
#define REG_DWORD 4U
#define RRF_RT_REG_SZ 0x00000002U
#define RRF_RT_REG_DWORD 0x00000010U

// the library loader
#define LOAD_WITH_ALTERED_SEARCH_PATH 0x00000008U
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 0x00000002U
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 0x00000004U

// the console / API's code pages the game names
#define CP_ACP 0U
#define CP_UTF8 65001U
#define MB_ERR_INVALID_CHARS 0x00000008U

// the window (the game's window is SDL3's: these are the numbers its messages and styles use)
#define SPI_GETCLIENTAREAANIMATION 0x1042U
#define WM_USER 0x0400U
#define WM_CLOSE 0x0010U
#define WM_QUIT 0x0012U
#define WM_KEYDOWN 0x0100U
#define WM_KEYUP 0x0101U
#define WS_OVERLAPPEDWINDOW 0x00CF0000U
#define SW_SHOW 5
#define SW_HIDE 0

// --- the handles --------------------------------------------------------------------------------------------------
// A handle is a pointer to one of these (HANDLE, HWND, HINSTANCE, HMODULE, HKEY: Win32's are all one pointer too, and
// the game keeps them apart by what made them).
struct PortHandle;
using HANDLE = PortHandle*;
using HWND = PortHandle*;
using HINSTANCE = PortHandle*;
using HMODULE = PortHandle*;
using HKEY = PortHandle*;
using FARPROC = void (*)();

#define INVALID_HANDLE_VALUE ((HANDLE)(std::intptr_t)-1)
#define INVALID_FILE_ATTRIBUTES ((DWORD)0xFFFFFFFFU)

// --- the structs --------------------------------------------------------------------------------------------------
struct POINT {
    LONG x{};
    LONG y{};
};
struct RECT {
    LONG left{};
    LONG top{};
    LONG right{};
    LONG bottom{};
};
struct GUID {
    std::uint32_t Data1{};
    std::uint16_t Data2{};
    std::uint16_t Data3{};
    std::uint8_t Data4[8]{};
};
using IID = GUID;
using CLSID = GUID;
using REFIID = const GUID&;

union LARGE_INTEGER {
    struct {
        DWORD LowPart;
        LONG HighPart;
    } u;
    LONGLONG QuadPart;
};
using PLARGE_INTEGER = LARGE_INTEGER*;

struct FILETIME {
    DWORD dwLowDateTime{};
    DWORD dwHighDateTime{};
};

struct SYSTEMTIME {
    WORD wYear{};
    WORD wMonth{};
    WORD wDayOfWeek{};
    WORD wDay{};
    WORD wHour{};
    WORD wMinute{};
    WORD wSecond{};
    WORD wMilliseconds{};
};

struct MEMORYSTATUS {
    DWORD dwLength{};
    DWORD dwMemoryLoad{};
    SIZE_T dwTotalPhys{};
    SIZE_T dwAvailPhys{};
    SIZE_T dwTotalPageFile{};
    SIZE_T dwAvailPageFile{};
    SIZE_T dwTotalVirtual{};
    SIZE_T dwAvailVirtual{};
};

struct SECURITY_ATTRIBUTES {
    DWORD nLength{};
    LPVOID lpSecurityDescriptor{};
    BOOL bInheritHandle{};
};

// The asynchronous read's control block (the port reads at the block's offset and reports the bytes at once: no read
// of the game's is left pending, so a poll of its state always finds it done).
struct OVERLAPPED {
    ULONG_PTR Internal{};
    ULONG_PTR InternalHigh{};
    union {
        struct {
            DWORD Offset;
            DWORD OffsetHigh;
        };
        void* Pointer;
    };
    HANDLE hEvent{};
};

struct WIN32_FIND_DATAA {
    DWORD dwFileAttributes{};
    FILETIME ftCreationTime{};
    FILETIME ftLastAccessTime{};
    FILETIME ftLastWriteTime{};
    DWORD nFileSizeHigh{};
    DWORD nFileSizeLow{};
    DWORD dwReserved0{};
    DWORD dwReserved1{};
    char cFileName[MAX_PATH]{};
    char cAlternateFileName[14]{};
};

// The critical section: a recursive mutex (the game's own sections are entered recursively).
struct CRITICAL_SECTION {
    void* impl = nullptr;
};

#define HRESULT_FROM_WIN32(x) ((HRESULT)((x) <= 0 ? (x) : ((((HRESULT)(x)) & 0x0000FFFF) | 0x80070000)))
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#define MAKE_HRESULT(sev, fac, code) ((HRESULT)(((unsigned)(sev) << 31) | ((unsigned)(fac) << 16) | ((unsigned)(code))))
#define LOWORD(v) ((WORD)(((DWORD_PTR)(v)) & 0xFFFF))
#define HIWORD(v) ((WORD)((((DWORD_PTR)(v)) >> 16) & 0xFFFF))
#define MAKEWORD(a, b) ((WORD)(((BYTE)(a)) | ((WORD)((BYTE)(b))) << 8))
#define MAKELONG(a, b) ((LONG)(((WORD)(a)) | ((DWORD)((WORD)(b))) << 16))

// --- the window (the game's window is SDL3's; these are the names the layer's own Win32 window code is read with) ---
using WPARAM = std::uintptr_t;
using LPARAM = std::intptr_t;
using LRESULT = std::intptr_t;
using HICON = PortHandle*;
using HCURSOR = PortHandle*;
using HBRUSH = PortHandle*;
using HFONT = PortHandle*;
using HMENU = PortHandle*;
using HMONITOR = PortHandle*;
using HGDIOBJ = PortHandle*;
using DLGPROC = LRESULT (*)(HWND, UINT, WPARAM, LPARAM);
using WNDPROC = LRESULT (*)(HWND, UINT, WPARAM, LPARAM);

struct MSG {
    HWND hwnd{};
    UINT message{};
    WPARAM wParam{};
    LPARAM lParam{};
    DWORD time{};
    POINT pt{};
};

struct WNDCLASSEXA {
    UINT cbSize{};
    UINT style{};
    WNDPROC lpfnWndProc{};
    int cbClsExtra{};
    int cbWndExtra{};
    HINSTANCE hInstance{};
    HICON hIcon{};
    HCURSOR hCursor{};
    HBRUSH hbrBackground{};
    const char* lpszMenuName{};
    const char* lpszClassName{};
    HICON hIconSm{};
};

#define WS_POPUP 0x80000000U
#define WS_CHILD 0x40000000U
#define WS_MINIMIZE 0x20000000U
#define WS_VISIBLE 0x10000000U
#define WS_DISABLED 0x08000000U
#define WS_CLIPSIBLINGS 0x04000000U
#define WS_CLIPCHILDREN 0x02000000U
#define WS_MAXIMIZE 0x01000000U
#define WS_CAPTION 0x00C00000U
#define WS_BORDER 0x00800000U
#define WS_DLGFRAME 0x00400000U
#define WS_VSCROLL 0x00200000U
#define WS_HSCROLL 0x00100000U
#define WS_SYSMENU 0x00080000U
#define WS_THICKFRAME 0x00040000U
#define WS_MINIMIZEBOX 0x00020000U
#define WS_MAXIMIZEBOX 0x00010000U
#define WS_OVERLAPPED 0x00000000U
#define WS_EX_TOPMOST 0x00000008U
#define WS_EX_APPWINDOW 0x00040000U
#define CS_CLASSDC 0x0040U
#define CS_OWNDC 0x0020U
#define CS_HREDRAW 0x0002U
#define CS_VREDRAW 0x0001U
#define SW_SHOWNORMAL 1
#define SW_MINIMIZE 6
#define SW_RESTORE 9
#define HWND_TOPMOST ((HWND)(std::intptr_t)-1)
#define HWND_NOTOPMOST ((HWND)(std::intptr_t)-2)
#define SWP_NOSIZE 0x0001U
#define SWP_NOMOVE 0x0002U
#define SWP_NOZORDER 0x0004U
#define SWP_SHOWWINDOW 0x0040U

// (the display the window is measured against: the game's own Win32 window code is not what makes one on this
// platform - SDL3 does - so these answer with the desktop's size)
#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define SM_CXVIRTUALSCREEN 78
#define SM_CYVIRTUALSCREEN 79

#define DRIVE_UNKNOWN 0U
#define DRIVE_NO_ROOT_DIR 1U
#define DRIVE_REMOVABLE 2U
#define DRIVE_FIXED 3U
#define DRIVE_REMOTE 4U
#define DRIVE_CDROM 5U
#define DRIVE_RAMDISK 6U

#define STD_INPUT_HANDLE ((DWORD) - 10)
#define STD_OUTPUT_HANDLE ((DWORD) - 11)
#define STD_ERROR_HANDLE ((DWORD) - 12)

#define KEY_QUERY_VALUE 0x0001U
#define KEY_SET_VALUE 0x0002U
#define KEY_ENUMERATE_SUB_KEYS 0x0008U
#define KEY_NOTIFY 0x0010U
#define KEY_CREATE_SUB_KEY 0x0004U

// (Win32's min/max macros, which the fetched third-party C++ - LZHAM - is written against. The game's own code is built
// with NOMINMAX, so it never sees them.)
#ifndef NOMINMAX
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#endif

// --- the rest of the Win32 the engine and the game name -----------------------------------------------------------
// A slim reader/writer lock (engine/gfx/loading_console.h: Console's ring of lines).
struct SRWLOCK {
    void* impl = nullptr;
};
#define SRWLOCK_INIT {nullptr}
void InitializeSRWLock(SRWLOCK* lock);
void AcquireSRWLockExclusive(SRWLOCK* lock);
void ReleaseSRWLockExclusive(SRWLOCK* lock);
void AcquireSRWLockShared(SRWLOCK* lock);
void ReleaseSRWLockShared(SRWLOCK* lock);
BOOL TryAcquireSRWLockExclusive(SRWLOCK* lock);

// The virtual keys (engine/pad_input/device.cpp: the menu's hints, by scan code).
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_PAUSE 0x13
#define VK_CAPITAL 0x14
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F4 0x73
#define VK_F5 0x74
#define VK_F6 0x75
#define VK_F7 0x76
#define VK_F8 0x77
#define VK_F9 0x78
#define VK_F10 0x79
#define VK_F11 0x7A
#define VK_F12 0x7B
#define VK_NUMPAD0 0x60
#define VK_NUMPAD9 0x69
#define VK_OEM_1 0xBA
#define VK_OEM_2 0xBF
#define VK_OEM_3 0xC0
#define VK_OEM_4 0xDB
#define VK_OEM_5 0xDC
#define VK_OEM_6 0xDD
#define VK_OEM_7 0xDE
#define MAPVK_VK_TO_VSC 0
#define MAPVK_VSC_TO_VK 1
#define MAPVK_VK_TO_CHAR 2
#define MAPVK_VSC_TO_VK_EX 3
UINT MapVirtualKeyA(UINT code, UINT map_type);
UINT MapVirtualKeyW(UINT code, UINT map_type);

// The address space's regions (engine/memory/memory_report.cpp: how much of it is free).
#define MEM_COMMIT_FLAG 0x1000U
#define MEM_FREE 0x10000U
#define MEM_RESERVE_FLAG 0x2000U
struct MEMORY_BASIC_INFORMATION {
    void* BaseAddress{};
    void* AllocationBase{};
    DWORD AllocationProtect{};
    WORD PartitionId{};
    SIZE_T RegionSize{};
    DWORD State{};
    DWORD Protect{};
    DWORD Type{};
};
SIZE_T VirtualQuery(const void* address, MEMORY_BASIC_INFORMATION* info, SIZE_T size);

// The OS's version (engine/file/async.cpp asks whether it is NT or later).
#define VER_PLATFORM_WIN32s 0
#define VER_PLATFORM_WIN32_WINDOWS 1
#define VER_PLATFORM_WIN32_NT 2
struct OSVERSIONINFOA {
    DWORD dwOSVersionInfoSize{};
    DWORD dwMajorVersion{};
    DWORD dwMinorVersion{};
    DWORD dwBuildNumber{};
    DWORD dwPlatformId{};
    char szCSDVersion[128]{};
};
BOOL GetVersionExA(OSVERSIONINFOA* info);

// MSVC's "the caller's buffer is big enough to truncate into" for its _s writers.
#ifndef _TRUNCATE
#define _TRUNCATE ((size_t)-1)
#endif

// The screensaver (engine/sys/mlp.cpp keeps the frame from being taken for one).
#define SPI_GETSCREENSAVETIMEOUT 0x000E
#define SPI_SETSCREENSAVETIMEOUT 0x000F
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002U
#define TIMER_ALL_ACCESS 0x1F0003U
#define THREAD_PRIORITY_IDLE (-15)
#define THREAD_PRIORITY_LOWEST (-2)
#define THREAD_PRIORITY_BELOW_NORMAL (-1)
#define THREAD_PRIORITY_NORMAL 0
#define THREAD_PRIORITY_ABOVE_NORMAL 1
#define THREAD_PRIORITY_HIGHEST 2
#define THREAD_PRIORITY_TIME_CRITICAL 15
#define ERROR_IO_PENDING 997L
#define ERROR_IO_INCOMPLETE 996L

// The HRESULTs the game tests (an error is negative: its sign is what a caller reads).
#define S_OK ((HRESULT)0L)
#define S_FALSE ((HRESULT)1L)
#define E_NOTIMPL ((HRESULT)0x80004001L)
#define E_NOINTERFACE ((HRESULT)0x80004002L)
#define E_POINTER ((HRESULT)0x80004003L)
#define E_ABORT ((HRESULT)0x80004004L)
#define E_FAIL ((HRESULT)0x80004005L)
#define E_UNEXPECTED ((HRESULT)0x8000FFFFL)
#define E_ACCESSDENIED ((HRESULT)0x80070005L)
#define E_HANDLE ((HRESULT)0x80070006L)
#define E_OUTOFMEMORY ((HRESULT)0x8007000EL)
#define E_INVALIDARG ((HRESULT)0x80070057L)
#define CLASS_E_CLASSNOTAVAILABLE ((HRESULT)0x80040111L)
#define REGDB_E_CLASSNOTREG ((HRESULT)0x80040154L)

// MessageBox's buttons and icons (the game's own error reports)
#define MB_OK 0x00000000U
#define MB_OKCANCEL 0x00000001U
#define MB_ABORTRETRYIGNORE 0x00000002U
#define MB_YESNOCANCEL 0x00000003U
#define MB_YESNO 0x00000004U
#define MB_RETRYCANCEL 0x00000005U
#define MB_ICONHAND 0x00000010U
#define MB_ICONQUESTION 0x00000020U
#define MB_ICONEXCLAMATION 0x00000030U
#define MB_ICONASTERISK 0x00000040U
#define MB_ICONERROR 0x00000010U
#define MB_ICONWARNING 0x00000030U
#define MB_ICONINFORMATION 0x00000040U
#define IDOK 1
#define IDCANCEL 2
#define IDABORT 3
#define IDRETRY 4
#define IDIGNORE 5
#define IDYES 6
#define IDNO 7

// A thread's start routine (CreateThread's own function type).
using LPTHREAD_START_ROUTINE = unsigned long(WIN32_STDCALL*)(LPVOID);
#define CREATE_SUSPENDED 0x00000004U
#define STACK_SIZE_PARAM_IS_A_RESERVATION 0x00010000U
DWORD ResumeThread(HANDLE thread);
BOOL SetThreadAffinityMask(HANDLE thread, DWORD_PTR mask);

// (a thread whose routine returns `unsigned long` - DWORD's Windows spelling for it - is wrapped: the port's own
// threads return a DWORD, and the two are one type on Windows but not on a platform where `long` is 64 bits)
namespace port_thread_detail {
struct WideStart {
    unsigned long(WIN32_STDCALL* function_)(LPVOID);
    LPVOID parameter_;
};
DWORD WIN32_STDCALL wide_trampoline(LPVOID wide);
} // namespace port_thread_detail
HANDLE port_create_thread(SECURITY_ATTRIBUTES* attributes, SIZE_T stack, DWORD(WIN32_STDCALL* start)(LPVOID),
                          LPVOID parameter, DWORD flags, DWORD* id);

// A routine that returns `unsigned long` (the Windows spelling of a thread's DWORD): wrapped in one that returns the
// port's DWORD, the two being one type on Windows and not here.
inline HANDLE CreateThread(SECURITY_ATTRIBUTES* attributes, SIZE_T stack, unsigned long(WIN32_STDCALL* start)(LPVOID),
                           LPVOID parameter, DWORD flags, DWORD* id)
{
    auto* wide = new port_thread_detail::WideStart{start, parameter};
    return port_create_thread(attributes, stack, &port_thread_detail::wide_trampoline, wide, flags, id);
}

// The window messages and answers the game's own window procedure reads (the window is SDL3's: the procedure is not
// what runs here, but the code that names them is the game's).
#define WM_CREATE 0x0001U
#define WM_DESTROY 0x0002U
#define WM_MOVE 0x0003U
#define WM_SIZE 0x0005U
#define WM_ACTIVATE 0x0006U
#define WM_SETFOCUS 0x0007U
#define WM_KILLFOCUS 0x0008U
#define WM_PAINT 0x000FU
#define WM_ERASEBKGND 0x0014U
#define WM_SHOWWINDOW 0x0018U
#define WM_ACTIVATEAPP 0x001CU
#define WM_SETCURSOR 0x0020U
#define WM_SYSCOMMAND 0x0112U
#define WM_TIMER 0x0113U
#define WM_SYSKEYDOWN 0x0104U
#define WM_SYSKEYUP 0x0105U
#define WM_CHAR 0x0102U
#define WM_SYSCHAR 0x0106U
#define WM_POWERBROADCAST 0x0218U
#define WM_DISPLAYCHANGE 0x007EU
#define WA_INACTIVE 0
#define WA_ACTIVE 1
#define WA_CLICKACTIVE 2
#define SC_KEYMENU 0xF100U
#define SC_MINIMIZE 0xF020U
#define SC_RESTORE 0xF120U
#define SW_MAXIMIZE 3
#define SW_SHOWMINIMIZED 2
#define SW_SHOWMAXIMIZED 3
#define SW_SHOWNOACTIVATE 4
#define SW_SHOWNA 8
#define ES_SYSTEM_REQUIRED 0x00000001U
#define ES_DISPLAY_REQUIRED 0x00000002U
#define ES_CONTINUOUS 0x80000000U
#define PBT_APMQUERYSUSPEND 0x0000U
#define PBT_APMRESUMESUSPEND 0x0007U
#define MONITOR_DEFAULTTONEAREST 2U
#define IDI_APPLICATION ((const char*)(std::uintptr_t)32512)
#define IDC_ARROW ((const char*)(std::uintptr_t)32512)
