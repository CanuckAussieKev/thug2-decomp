// NOT ORIGINAL - part of the macOS (arm64) port's Win32 shim (docs/design/macos-port.md). This header is NOT Windows': it gives
// the names of `windows.h` to a build that has no such header, so that the engine and the game compile as they are read on
// Windows. It is only on the include path of a portable build (CMakeLists.txt, thug2_options: -I port/win32_shim); a
// Windows build never sees it.
#ifdef _WIN32
#error "platform/posix/windows.h is the macOS port's stand-in - a Windows build must use the system's real header"
#endif

#pragma once

// NOT ORIGINAL - the macOS (arm64) port's stand-in for <windows.h>. The layers above the platform ones - the engine,
// the game, the PC rendering layer, the front end, the dev tools, the script interpreter - reach the operating system
// through Win32 names; this file gives those names the same meaning over POSIX, so their code compiles as it is read
// on Windows.
//
// It is not a Win32 implementation: what the game uses of Win32 is here, and a name the game never calls is not. What
// has no meaning on this platform (the window's cursor, a console's code page) is a stub that says so.
//
// Only a portable build sees this file: its include path puts port/win32 first (CMakeLists.txt, thug2_options), and
// port/win32.cpp is the half of it that needs a body. A Windows build never reads either.

#include <cstdint>
#include <cstddef>

#include "platform/posix/win32_types.h"

// --- the clock ----------------------------------------------------------------------------------------------------
// (taken from the kernel's monotonic clock: the game times its frame with it)
std::uint64_t port_ticks_ns();
void port_sleep_until_ns(std::uint64_t when_ns);

inline DWORD GetTickCount() { return static_cast<DWORD>(port_ticks_ns() / 1000000ULL); }
inline ULONGLONG GetTickCount64() { return port_ticks_ns() / 1000000ULL; }
inline DWORD timeGetTime() { return GetTickCount(); }
inline void Sleep(DWORD milliseconds)
{
    port_sleep_until_ns(port_ticks_ns() + (static_cast<std::uint64_t>(milliseconds) * 1000000ULL));
}
inline void SleepEx(DWORD milliseconds, BOOL /*alertable*/) { Sleep(milliseconds); }

void QueryPerformanceCounter(LARGE_INTEGER* counter);
void QueryPerformanceFrequency(LARGE_INTEGER* frequency);
void GetSystemTimeAsFileTime(FILETIME* time);
BOOL QueryPerformanceCounterImpl(LARGE_INTEGER* counter);

// --- the last error, per thread -----------------------------------------------------------------------------------
DWORD GetLastError();
void SetLastError(DWORD error);

// --- the debug output and the dialogs -----------------------------------------------------------------------------
void OutputDebugStringA(const char* text);
void OutputDebugStringW(const wchar_t* text);
int MessageBoxA(HWND /*window*/, const char* text, const char* caption, UINT type);
int MessageBoxW(HWND /*window*/, const wchar_t* text, const wchar_t* caption, UINT type);
int ShowCursor(BOOL show);
BOOL SystemParametersInfoA(UINT action, UINT param, void* data, UINT flags);

// --- the module and the program -----------------------------------------------------------------------------------
// `name` is the directory the program runs from (Win32's SystemDirectoryA slot: the game asks it for its own).
UINT GetSystemDirectoryA(char* buffer, UINT size);
UINT GetWindowsDirectoryA(char* buffer, UINT size);
HMODULE GetModuleHandleA(const char* name);
HMODULE GetModuleHandleExA(DWORD flags, const char* name, HMODULE* module);
DWORD GetModuleFileNameA(HMODULE module, char* buffer, DWORD size);
DWORD GetModuleFileNameW(HMODULE module, wchar_t* buffer, DWORD size);
HMODULE LoadLibraryA(const char* path);
HMODULE LoadLibraryExA(const char* path, HANDLE file, DWORD flags);
HMODULE LoadLibraryExW(const wchar_t* path, HANDLE file, DWORD flags);
FARPROC GetProcAddress(HMODULE module, const char* name);
BOOL FreeLibrary(HMODULE module);
char* GetCommandLineA();
DWORD GetCurrentThreadId();
DWORD GetCurrentProcessId();
DWORD GetEnvironmentVariableA(const char* name, char* buffer, DWORD size);
BOOL SetEnvironmentVariableA(const char* name, const char* value);
BOOL SetEnvironmentVariableW(const wchar_t* name, const wchar_t* value);
void ExitProcess(UINT code);

HANDLE GetCurrentProcess();
BOOL GetProcessTimes(HANDLE process, FILETIME* creation, FILETIME* exit, FILETIME* kernel, FILETIME* user);

// --- the heap -----------------------------------------------------------------------------------------------------
LPVOID VirtualAlloc(LPVOID at, SIZE_T size, DWORD type, DWORD protect);
BOOL VirtualFree(LPVOID at, SIZE_T size, DWORD type);
void GlobalMemoryStatus(MEMORYSTATUS* status);

// --- the file -----------------------------------------------------------------------------------------------------
HANDLE CreateFileA(const char* path, DWORD access, DWORD share, SECURITY_ATTRIBUTES* attributes, DWORD disposition,
                   DWORD flags, HANDLE template_file);
HANDLE CreateFileW(const wchar_t* path, DWORD access, DWORD share, SECURITY_ATTRIBUTES* attributes, DWORD disposition,
                   DWORD flags, HANDLE template_file);
BOOL ReadFile(HANDLE file, void* buffer, DWORD bytes, DWORD* read, OVERLAPPED* overlapped);
BOOL WriteFile(HANDLE file, const void* buffer, DWORD bytes, DWORD* written, OVERLAPPED* overlapped);
DWORD SetFilePointer(HANDLE file, LONG distance, LONG* high, DWORD method);
BOOL SetFilePointerEx(HANDLE file, LARGE_INTEGER distance, LARGE_INTEGER* where, DWORD method);
DWORD GetFileSize(HANDLE file, DWORD* high);
BOOL GetFileSizeEx(HANDLE file, LARGE_INTEGER* size);
DWORD GetFileType(HANDLE file);
BOOL GetOverlappedResult(HANDLE file, OVERLAPPED* overlapped, DWORD* transferred, BOOL wait);
BOOL FlushFileBuffers(HANDLE file);
DWORD GetFileAttributesA(const char* path);
DWORD GetFileAttributesW(const wchar_t* path);
BOOL CreateDirectoryA(const char* path, SECURITY_ATTRIBUTES* attributes);
BOOL DeleteFileA(const char* path);
BOOL MoveFileExA(const char* from, const char* to, DWORD flags);
DWORD GetFullPathNameA(const char* path, DWORD size, char* buffer, char** file_part);

// the directory walk (FindFirstFileA / FindNextFileA / FindClose: a handle of its own)
HANDLE FindFirstFileA(const char* pattern, WIN32_FIND_DATAA* found);
BOOL FindNextFileA(HANDLE find, WIN32_FIND_DATAA* found);
BOOL FindClose(HANDLE find);

// --- the handles --------------------------------------------------------------------------------------------------
BOOL CloseHandle(HANDLE handle);

// --- the threads and their sync -----------------------------------------------------------------------------------
HANDLE port_create_thread(SECURITY_ATTRIBUTES* attributes, SIZE_T stack, DWORD(WIN32_STDCALL* start)(LPVOID),
                          LPVOID parameter, DWORD flags, DWORD* id);
HANDLE CreateThread(SECURITY_ATTRIBUTES* attributes, SIZE_T stack, DWORD(WIN32_STDCALL* start)(LPVOID),
                    LPVOID parameter, DWORD flags, DWORD* id);
DWORD WaitForSingleObject(HANDLE handle, DWORD milliseconds);
DWORD WaitForMultipleObjects(DWORD count, const HANDLE* handles, BOOL all, DWORD milliseconds);
BOOL GetExitCodeThread(HANDLE thread, DWORD* code);
BOOL SetThreadPriority(HANDLE thread, int priority);
void InitializeCriticalSection(CRITICAL_SECTION* section);
void EnterCriticalSection(CRITICAL_SECTION* section);
void LeaveCriticalSection(CRITICAL_SECTION* section);
void DeleteCriticalSection(CRITICAL_SECTION* section);
BOOL TryEnterCriticalSection(CRITICAL_SECTION* section);
HANDLE CreateMutexA(SECURITY_ATTRIBUTES* attributes, BOOL owned, const char* name);
HANDLE CreateMutexW(SECURITY_ATTRIBUTES* attributes, BOOL owned, const wchar_t* name);
BOOL ReleaseMutex(HANDLE mutex);
HANDLE CreateEventA(SECURITY_ATTRIBUTES* attributes, BOOL manual_reset, BOOL signalled, const char* name);
HANDLE CreateEventW(SECURITY_ATTRIBUTES* attributes, BOOL manual_reset, BOOL signalled, const wchar_t* name);
BOOL SetEvent(HANDLE event);
BOOL ResetEvent(HANDLE event);
HANDLE CreateSemaphoreA(SECURITY_ATTRIBUTES* attributes, LONG initial, LONG maximum, const char* name);
BOOL ReleaseSemaphore(HANDLE semaphore, LONG count, LONG* previous);
DWORD WaitForSingleObjectEx(HANDLE handle, DWORD milliseconds, BOOL alertable);

// the waitable timer (engine/sys/mlp.h: the frame's sleep, in 100 ns units, negative for "from now")
HANDLE CreateWaitableTimerExW(SECURITY_ATTRIBUTES* attributes, const wchar_t* name, DWORD flags, DWORD access);
HANDLE CreateWaitableTimerA(SECURITY_ATTRIBUTES* attributes, BOOL manual_reset, const char* name);
BOOL SetWaitableTimer(HANDLE timer, const LARGE_INTEGER* due, LONG period, void* routine, void* parameter, BOOL resume);
BOOL CancelWaitableTimer(HANDLE timer);

// --- the settings: the registry and the ini file ------------------------------------------------------------------
// (Win32 keeps the game's own settings in HKEY_CURRENT_USER\Software\Activision\Tony Hawk's Underground 2, and the
// rest in thug2-decomp.ini beside the exe; on this platform both are files under the game folder - port/win32.cpp)
HKEY RegOpenKeyExA(HKEY key, const char* sub, DWORD options, DWORD access, HKEY* result);
HKEY RegCreateKeyA(HKEY key, const char* sub, HKEY* result);
HKEY RegCreateKeyExA(HKEY key, const char* sub, DWORD reserved, char* class_name, DWORD options, DWORD access,
                     SECURITY_ATTRIBUTES* attributes, HKEY* result, DWORD* disposition);
LONG RegQueryValueExA(HKEY key, const char* name, DWORD* reserved, DWORD* type, BYTE* data, DWORD* size);
LONG RegSetValueExA(HKEY key, const char* name, DWORD reserved, DWORD type, const BYTE* data, DWORD size);
LONG RegGetValueA(HKEY key, const char* sub, const char* name, DWORD flags, DWORD* type, void* data, DWORD* size);
LONG RegCloseKey(HKEY key);
LONG RegDeleteKeyA(HKEY key, const char* sub);

UINT GetPrivateProfileStringA(const char* section, const char* name, const char* fallback, char* buffer, UINT size,
                              const char* file);
UINT GetPrivateProfileIntA(const char* section, const char* name, INT fallback, const char* file);
UINT GetPrivateProfileSectionA(const char* section, char* buffer, DWORD size, const char* file);
BOOL WritePrivateProfileStringA(const char* section, const char* name, const char* value, const char* file);
BOOL WritePrivateProfileStringW(const wchar_t* section, const wchar_t* name, const wchar_t* value, const wchar_t* file);

// --- the text -----------------------------------------------------------------------------------------------------
int MultiByteToWideChar(UINT code_page, DWORD flags, const char* text, int length, wchar_t* out, int out_count);
int WideCharToMultiByte(UINT code_page, DWORD flags, const wchar_t* text, int length, char* out, int out_count,
                        const char* default_char, BOOL* used_default);
int lstrlenA(const char* text);
int lstrlenW(const wchar_t* text);
LPSTR lstrcpyA(LPSTR to, LPCSTR from);

// The folder the program runs from (a Win32 game's working directory assumptions: the data files are beside the exe,
// whatever directory it was started from - a file manager starts it in "/").
const char* port_game_directory();

// --- the program's start-up (port/win32.cpp takes these from main) -------------------------------------------------
// (app/winmain.cpp's WinMain wants the program's command line and its instance handle; the port's main - which the
// linker starts - hands them over)
void port_set_command_line(int argc, char** argv);

// --- the window ---------------------------------------------------------------------------------------------------
// (the game's window is SDL3's: these answer, or do nothing, so the platform layer's own Win32 window code - which no
// portable build runs - is read as it is on Windows. docs/design/macos-port.md has it.)
HANDLE GetStdHandle(DWORD which);
BOOL SetStdHandle(DWORD which, HANDLE handle);
DWORD GetLogicalDrives();
UINT GetDriveTypeA(const char* root);
BOOL GetVolumeInformationA(const char* root, char* volume, DWORD volume_size, DWORD* serial, DWORD* max_length,
                           DWORD* flags, char* filesystem, DWORD filesystem_size);
int GetSystemMetrics(int index);
BOOL AdjustWindowRect(RECT* rect, DWORD style, BOOL menu);
ATOM RegisterClassExA(const WNDCLASSEXA* window_class);
BOOL UnregisterClassA(const char* class_name, HINSTANCE instance);
HWND CreateWindowExA(DWORD ex_style, const char* class_name, const char* title, DWORD style, int x, int y, int width,
                     int height, HWND parent, HMENU menu, HINSTANCE instance, void* parameter);
BOOL DestroyWindow(HWND window);
LRESULT DefWindowProcA(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
BOOL PeekMessageA(MSG* message, HWND window, UINT first, UINT last, UINT remove);
BOOL GetMessageA(MSG* message, HWND window, UINT first, UINT last);
BOOL TranslateMessage(const MSG* message);
LRESULT DispatchMessageA(const MSG* message);
BOOL PostMessageA(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
LRESULT SendMessageA(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
LRESULT SendMessageW(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
HWND FindWindowExA(HWND parent, HWND after, const char* class_name, const char* title);
int GetWindowTextA(HWND window, char* buffer, int size);
BOOL SetWindowPos(HWND window, HWND after, int x, int y, int width, int height, UINT flags);
BOOL ShowWindow(HWND window, int command);
BOOL UpdateWindow(HWND window);
BOOL GetClientRect(HWND window, RECT* rect);
BOOL ClientToScreen(HWND window, POINT* point);
BOOL GetCursorPos(POINT* point);
BOOL SetCursorPos(int x, int y);
BOOL ClipCursor(const RECT* rect);
HCURSOR LoadCursorA(HINSTANCE instance, const char* name);
HICON LoadIconA(HINSTANCE instance, const char* name);
LPVOID LoadImageW(HINSTANCE instance, const wchar_t* name, UINT type, int width, int height, UINT flags);
#define MAKEINTRESOURCEA(i) ((const char*)(std::uintptr_t)(i))
#define MAKEINTRESOURCEW(i) ((const wchar_t*)(std::uintptr_t)(i))

// --- the display's modes (the window's resolution check) ----------------------------------------------------------
// (a display's mode as Win32 describes it; no mode is enumerated on this platform - the desktop's own size is the
// window's, which is what a resolution beyond every mode falls back to)
struct DEVMODEA {
    char dmDeviceName[32]{};
    WORD dmSpecVersion{};
    WORD dmDriverVersion{};
    WORD dmSize{};
    WORD dmDriverExtra{};
    DWORD dmFields{};
    LONG dmPositionX{};
    LONG dmPositionY{};
    DWORD dmDisplayOrientation{};
    DWORD dmDisplayFixedOutput{};
    SHORT dmColor{};
    SHORT dmDuplex{};
    SHORT dmYResolution{};
    SHORT dmTTOption{};
    SHORT dmCollate{};
    char dmFormName[32]{};
    WORD dmLogPixels{};
    DWORD dmBitsPerPel{};
    DWORD dmPelsWidth{};
    DWORD dmPelsHeight{};
    DWORD dmDisplayFlags{};
    DWORD dmDisplayFrequency{};
};
BOOL EnumDisplaySettingsA(const char* device, DWORD mode, DEVMODEA* settings);
LONG ChangeDisplaySettingsA(DEVMODEA* settings, DWORD flags);
#define HWND_BOTTOM ((HWND)(std::intptr_t)1)
#define SWP_NOACTIVATE 0x0010U

// --- the window, the console and the display's sleep ---------------------------------------------------------------
HWND GetForegroundWindow();
HWND GetActiveWindow();
HWND SetActiveWindow(HWND window);
BOOL PostQuitMessage(int code);
BOOL InvalidateRect(HWND window, const RECT* rect, BOOL erase);
BOOL ValidateRect(HWND window, const RECT* rect);
BOOL BringWindowToTop(HWND window);
BOOL SetForegroundWindow(HWND window);
HWND SetFocus(HWND window);
HMONITOR MonitorFromWindow(HWND window, DWORD flags);
int GetMonitorInfoA(HMONITOR monitor, void* info);
UINT SetThreadExecutionState(UINT flags);
BOOL AllocConsole();
BOOL FreeConsole();
BOOL SetConsoleTitleA(const char* title);
