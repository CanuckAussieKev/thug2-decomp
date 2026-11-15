// NOT ORIGINAL - the macOS (arm64) port's Win32: the bodies of platform/posix/windows.h. See that file's comment.

#include "platform/posix/windows.h"
#include "platform/posix/psapi.h"
#include "platform/posix/mmsystem.h"

#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <map>

#include <dlfcn.h>
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <pthread.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <wchar.h>

// --- a handle -----------------------------------------------------------------------------------------------------
enum class HandleKind : std::uint8_t { kFile, kThread, kMutex, kEvent, kSemaphore, kTimer, kLibrary, kKey, kFind };

struct PortHandle {
    HandleKind kind_{};
    int fd_ = -1;          // a file's or a find's descriptor
    void* user_ = nullptr; // a library's, a thread's, a directory's
    pthread_t thread_{};
    pthread_mutex_t mutex_{};
    pthread_cond_t cond_{};
    bool signalled_ = false;
    bool manual_reset_ = false;
    bool suspended_ = false;
    long count_ = 0; // a semaphore's
    DWORD exit_code_ = 0;
    DWORD thread_id_ = 0;
    std::uint64_t deadline_ns_ = 0;
    bool has_deadline_ = false;
    std::string name_;   // a registry key's path
    bool owned_ = false; // a mutex's: held by this thread
};

namespace {

HANDLE invalid_handle() { return reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(-1)); }
thread_local DWORD t_last_error = 0;

HANDLE make_handle(HandleKind kind)
{
    auto* handle = new PortHandle();
    handle->kind_ = kind;
    return handle;
}

PortHandle* as_handle(HANDLE handle) { return handle == nullptr || handle == invalid_handle() ? nullptr : handle; }

std::uint64_t now_ns()
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    return (static_cast<std::uint64_t>(now.tv_sec) * 1000000000ULL) + static_cast<std::uint64_t>(now.tv_nsec);
}

void sleep_until(std::uint64_t when_ns)
{
    // (macOS has no clock_nanosleep: the wait is a relative nanosleep, asked again for whatever is left)
    for (;;) {
        const std::uint64_t now = now_ns();
        if (now >= when_ns) {
            return;
        }
        struct timespec relative;
        relative.tv_sec = static_cast<time_t>((when_ns - now) / 1000000000ULL);
        relative.tv_nsec = static_cast<long>((when_ns - now) % 1000000000ULL);
        nanosleep(&relative, nullptr);
    }
}

// The program's own paths: where its executable is (Win32's EXE path, and the game folder is its folder).
std::string executable_path()
{
    char buffer[4096];
    std::uint32_t size = sizeof(buffer);
    if (_NSGetExecutablePath(buffer, &size) != 0) {
        return ".";
    }
    char resolved[4096];
    if (realpath(buffer, resolved) == nullptr) {
        return ".";
    }
    return resolved;
}

std::string directory_of(const std::string& path)
{
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

std::string game_directory()
{
    static const std::string directory = directory_of(executable_path());
    return directory;
}

// The program's command line, as Win32's GetCommandLineA gives it (port_set_command_line fills it).
std::string g_command_line;
std::string command_line() { return g_command_line; }

// --- the ini file (Win32's profile API: a text file of [section] and name=value lines) ---------------------------
std::string read_file(const std::string& path)
{
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return {};
    }
    std::string text;
    char buffer[4096];
    std::size_t read = 0;
    while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        text.append(buffer, read);
    }
    std::fclose(file);
    return text;
}

void write_text_file(const std::string& path, const std::string& text)
{
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        return;
    }
    std::fwrite(text.data(), 1, text.size(), file);
    std::fclose(file);
}

std::string ini_path(const char* file)
{
    if (file == nullptr || file[0] == '\0') {
        return {};
    }
    if (file[0] == '/') {
        return file;
    }
    return game_directory() + "/" + file;
}

bool ini_find(const std::string& text, const char* section, const char* name, std::string* value)
{
    const std::string wanted = std::string("[") + section + "]";
    std::size_t at = 0;
    bool inside = false;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos) {
            end = text.size();
        }
        std::string line = text.substr(at, end - at);
        at = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        std::size_t begin = line.find_first_not_of(" \t");
        if (begin == std::string::npos) {
            continue;
        }
        line.erase(0, begin);
        if (line.front() == '[') {
            inside = line == wanted;
            continue;
        }
        if (!inside || line.front() == ';' || line.front() == '#') {
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        std::string key = line.substr(0, equals);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) {
            key.pop_back();
        }
        if (key == name) {
            *value = line.substr(equals + 1);
            return true;
        }
    }
    return false;
}

// --- the registry (Win32's: the game's settings under HKCU\Software\Activision\Tony Hawk's Underground 2) ---------
// The port keeps the key\name\type\data lines in one file beside the exe: same values, the settings survive a run.
std::string registry_path() { return game_directory() + "/thug2-decomp-registry.txt"; }

std::string registry_key_path(HKEY key)
{
    if (key == HKEY_CURRENT_USER) {
        return "HKEY_CURRENT_USER";
    }
    if (key == HKEY_LOCAL_MACHINE) {
        return "HKEY_LOCAL_MACHINE";
    }
    if (key == HKEY_CLASSES_ROOT) {
        return "HKEY_CLASSES_ROOT";
    }
    const PortHandle* handle = as_handle(key);
    return handle == nullptr ? std::string() : handle->name_;
}

} // namespace

// --- the clock ----------------------------------------------------------------------------------------------------
std::uint64_t port_ticks_ns() { return now_ns(); }
void port_sleep_until_ns(std::uint64_t when_ns) { sleep_until(when_ns); }

void QueryPerformanceCounter(LARGE_INTEGER* counter)
{
    if (counter != nullptr) {
        counter->QuadPart = static_cast<LONGLONG>(now_ns());
    }
}

void QueryPerformanceFrequency(LARGE_INTEGER* frequency)
{
    if (frequency != nullptr) {
        frequency->QuadPart = 1000000000LL; // (a nanosecond count: the unit the counter above is in)
    }
}

void GetSystemTimeAsFileTime(FILETIME* time)
{
    if (time == nullptr) {
        return;
    }
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    const std::uint64_t ticks = (static_cast<std::uint64_t>(now.tv_sec) + 11644473600ULL) * 10000000ULL +
                                (static_cast<std::uint64_t>(now.tv_nsec) / 100ULL);
    time->dwLowDateTime = static_cast<DWORD>(ticks & 0xFFFFFFFFU);
    time->dwHighDateTime = static_cast<DWORD>(ticks >> 32);
}

// --- the last error -----------------------------------------------------------------------------------------------
DWORD GetLastError() { return t_last_error; }
void SetLastError(DWORD error) { t_last_error = error; }

// --- the debug output and the dialogs -----------------------------------------------------------------------------
void OutputDebugStringA(const char* text)
{
    if (text != nullptr) {
        std::fputs(text, stderr);
        std::fflush(stderr);
    }
}

void OutputDebugStringW(const wchar_t* text)
{
    if (text != nullptr) {
        std::fputws(text, stderr);
        std::fflush(stderr);
    }
}

int MessageBoxA(HWND /*window*/, const char* text, const char* caption, UINT /*type*/)
{
    std::fprintf(stderr, "[message box] %s: %s\n", caption != nullptr ? caption : "", text != nullptr ? text : "");
    std::fflush(stderr);
    return 1; // IDOK
}

int MessageBoxW(HWND window, const wchar_t* text, const wchar_t* caption, UINT type)
{
    std::string narrow_text;
    std::string narrow_caption;
    if (text != nullptr) {
        narrow_text.assign(text, text + std::wcslen(text));
    }
    if (caption != nullptr) {
        narrow_caption.assign(caption, caption + std::wcslen(caption));
    }
    return MessageBoxA(window, narrow_text.c_str(), narrow_caption.c_str(), type);
}

int ShowCursor(BOOL /*show*/) { return 0; } // (the window's cursor is SDL3's)
BOOL SystemParametersInfoA(UINT /*action*/, UINT /*param*/, void* /*data*/, UINT /*flags*/) { return TRUE; }

// --- the module and the program -----------------------------------------------------------------------------------
UINT GetSystemDirectoryA(char* buffer, UINT size)
{
    const std::string directory = game_directory();
    if (buffer == nullptr || size == 0) {
        return static_cast<UINT>(directory.size());
    }
    const std::size_t count = directory.size() < size - 1 ? directory.size() : size - 1;
    std::memcpy(buffer, directory.data(), count);
    buffer[count] = '\0';
    return static_cast<UINT>(count);
}

UINT GetWindowsDirectoryA(char* buffer, UINT size)
{
    return GetSystemDirectoryA(buffer, size); // (the game asks it for a folder to write to: its own is that)
}

HMODULE GetModuleHandleA(const char* name)
{
    // (only the program itself is known here: a library's handle comes from LoadLibraryA. Answering a handle for a
    // library that was never loaded would hide the load - and its failure - from the caller.)
    if (name != nullptr && name[0] != '\0') {
        return nullptr;
    }
    static PortHandle module{.kind_ = HandleKind::kLibrary, .user_ = nullptr};
    return &module;
}

DWORD GetModuleFileNameA(HMODULE /*module*/, char* buffer, DWORD size)
{
    const std::string path = executable_path();
    if (buffer == nullptr || size == 0) {
        return 0;
    }
    const std::size_t count = path.size() < size - 1 ? path.size() : size - 1;
    std::memcpy(buffer, path.data(), count);
    buffer[count] = '\0';
    return static_cast<DWORD>(count);
}

DWORD GetModuleFileNameW(HMODULE module, wchar_t* buffer, DWORD size)
{
    char path[MAX_PATH * 4];
    GetModuleFileNameA(module, path, sizeof(path));
    const int wide = MultiByteToWideChar(CP_UTF8, 0, path, -1, buffer, static_cast<int>(size));
    return wide > 0 ? static_cast<DWORD>(wide - 1) : 0;
}

HMODULE GetModuleHandleExA(DWORD /*flags*/, const char* /*name*/, HMODULE* module)
{
    if (module != nullptr) {
        *module = GetModuleHandleA(nullptr);
    }
    return GetModuleHandleA(nullptr);
}

HMODULE LoadLibraryA(const char* path)
{
    if (path == nullptr) {
        return nullptr;
    }
    void* library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (library == nullptr) {
        // (the game names a DLL without a path: try it beside the exe, then as a dylib)
        const std::string beside = game_directory() + "/" + path;
        library = dlopen(beside.c_str(), RTLD_NOW | RTLD_LOCAL);
    }
    if (library == nullptr) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return nullptr;
    }
    HANDLE handle = make_handle(HandleKind::kLibrary);
    handle->user_ = library;
    return handle;
}

HMODULE LoadLibraryExA(const char* path, HANDLE /*file*/, DWORD /*flags*/) { return LoadLibraryA(path); }

HMODULE LoadLibraryExW(const wchar_t* path, HANDLE file, DWORD flags)
{
    char narrow[1024];
    if (WideCharToMultiByte(CP_UTF8, 0, path, -1, narrow, sizeof(narrow), nullptr, nullptr) <= 0) {
        return nullptr;
    }
    return LoadLibraryExA(narrow, file, flags);
}

FARPROC GetProcAddress(HMODULE module, const char* name)
{
    const PortHandle* handle = as_handle(module);
    if (handle == nullptr || handle->user_ == nullptr || name == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<FARPROC>(dlsym(handle->user_, name));
}

BOOL FreeLibrary(HMODULE module)
{
    PortHandle* handle = as_handle(module);
    if (handle == nullptr || handle->kind_ != HandleKind::kLibrary) {
        return FALSE;
    }
    if (handle->user_ != nullptr) {
        dlclose(handle->user_);
    }
    delete handle;
    return TRUE;
}

char* GetCommandLineA() { return g_command_line.data(); }

DWORD GetCurrentThreadId()
{
    thread_local const DWORD id = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(&t_last_error) >> 4);
    return id;
}

DWORD GetCurrentProcessId() { return static_cast<DWORD>(getpid()); }

DWORD GetEnvironmentVariableA(const char* name, char* buffer, DWORD size)
{
    const char* value = name != nullptr ? std::getenv(name) : nullptr;
    const std::size_t length = value != nullptr ? std::strlen(value) : 0;
    if (buffer != nullptr && size > length) {
        std::memcpy(buffer, value != nullptr ? value : "", length + 1);
    }
    return static_cast<DWORD>(length);
}

BOOL SetEnvironmentVariableA(const char* name, const char* value)
{
    if (name == nullptr) {
        return FALSE;
    }
    return value != nullptr ? (setenv(name, value, 1) == 0) : (unsetenv(name) == 0);
}

BOOL SetEnvironmentVariableW(const wchar_t* name, const wchar_t* value)
{
    char narrow_name[512];
    char narrow_value[4096];
    if (WideCharToMultiByte(CP_UTF8, 0, name, -1, narrow_name, sizeof(narrow_name), nullptr, nullptr) <= 0) {
        return FALSE;
    }
    if (value == nullptr) {
        return SetEnvironmentVariableA(narrow_name, nullptr);
    }
    if (WideCharToMultiByte(CP_UTF8, 0, value, -1, narrow_value, sizeof(narrow_value), nullptr, nullptr) <= 0) {
        return FALSE;
    }
    return SetEnvironmentVariableA(narrow_name, narrow_value);
}

void ExitProcess(UINT code) { std::exit(static_cast<int>(code)); }

// --- the heap -----------------------------------------------------------------------------------------------------
LPVOID VirtualAlloc(LPVOID at, SIZE_T size, DWORD /*type*/, DWORD /*protect*/)
{
    void* memory = mmap(at, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return memory == MAP_FAILED ? nullptr : memory;
}

BOOL VirtualFree(LPVOID at, SIZE_T size, DWORD /*type*/) { return munmap(at, size) == 0; }

void GlobalMemoryStatus(MEMORYSTATUS* status)
{
    if (status == nullptr) {
        return;
    }
    std::uint64_t total = 0;
    mach_port_t host = mach_host_self();
    host_basic_info_data_t info;
    mach_msg_type_number_t count = HOST_BASIC_INFO_COUNT;
    if (host_info(host, HOST_BASIC_INFO, reinterpret_cast<host_info_t>(&info), &count) == KERN_SUCCESS) {
        total = info.memory_size;
    }
    status->dwLength = sizeof(MEMORYSTATUS);
    status->dwTotalPhys = static_cast<SIZE_T>(total);
    status->dwAvailPhys = static_cast<SIZE_T>(total); // (the game looks for a rough figure: the whole of it)
    status->dwMemoryLoad = 0;
    status->dwTotalPageFile = static_cast<SIZE_T>(total);
    status->dwAvailPageFile = static_cast<SIZE_T>(total);
    status->dwTotalVirtual = static_cast<SIZE_T>(total);
    status->dwAvailVirtual = static_cast<SIZE_T>(total);
}

// --- the file -----------------------------------------------------------------------------------------------------
namespace {

HANDLE open_path(const std::string& path, DWORD access, DWORD disposition)
{
    int flags = 0;
    if (disposition == CREATE_ALWAYS || disposition == CREATE_NEW) {
        flags = O_CREAT | O_TRUNC | ((access & GENERIC_READ) != 0 ? O_RDWR : O_WRONLY);
    } else if (disposition == OPEN_ALWAYS) {
        flags = O_CREAT | ((access & GENERIC_WRITE) != 0 ? O_RDWR : O_RDONLY);
    } else if (disposition == TRUNCATE_EXISTING) {
        flags = O_TRUNC | O_RDWR;
    } else {
        flags = ((access & GENERIC_WRITE) != 0) ? ((access & GENERIC_READ) != 0 ? O_RDWR : O_WRONLY) : O_RDONLY;
    }
    const int fd = open(path.c_str(), flags, 0644);
    if (fd < 0) {
        SetLastError(errno == ENOENT ? ERROR_FILE_NOT_FOUND : ERROR_ACCESS_DENIED);
        return INVALID_HANDLE_VALUE;
    }
    HANDLE handle = make_handle(HandleKind::kFile);
    handle->fd_ = fd;
    return handle;
}

} // namespace

HANDLE CreateFileA(const char* path, DWORD access, DWORD /*share*/, SECURITY_ATTRIBUTES* /*attributes*/,
                   DWORD disposition, DWORD /*flags*/, HANDLE /*template_file*/)
{
    if (path == nullptr) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    return open_path(path, access, disposition);
}

HANDLE CreateFileW(const wchar_t* path, DWORD access, DWORD share, SECURITY_ATTRIBUTES* attributes, DWORD disposition,
                   DWORD flags, HANDLE template_file)
{
    char narrow[4096];
    if (WideCharToMultiByte(CP_UTF8, 0, path, -1, narrow, sizeof(narrow), nullptr, nullptr) <= 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    return CreateFileA(narrow, access, share, attributes, disposition, flags, template_file);
}

BOOL ReadFile(HANDLE file, void* buffer, DWORD bytes, DWORD* read_bytes, OVERLAPPED* overlapped)
{
    const PortHandle* handle = as_handle(file);
    if (handle == nullptr || handle->kind_ != HandleKind::kFile) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    const off_t offset = overlapped != nullptr
                             ? static_cast<off_t>(static_cast<std::uint64_t>(overlapped->Offset) |
                                                  (static_cast<std::uint64_t>(overlapped->OffsetHigh) << 32))
                             : static_cast<off_t>(-1);
    const ssize_t got = offset >= 0 ? pread(handle->fd_, buffer, bytes, offset) : read(handle->fd_, buffer, bytes);
    if (got < 0) {
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    if (read_bytes != nullptr) {
        *read_bytes = static_cast<DWORD>(got);
    }
    if (overlapped != nullptr) {
        overlapped->Internal = 0; // STATUS_SUCCESS (the read is done)
        overlapped->InternalHigh = static_cast<ULONG_PTR>(got);
    }
    return TRUE;
}

BOOL WriteFile(HANDLE file, const void* buffer, DWORD bytes, DWORD* written, OVERLAPPED* overlapped)
{
    const PortHandle* handle = as_handle(file);
    if (handle == nullptr || handle->kind_ != HandleKind::kFile) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    const off_t offset = overlapped != nullptr
                             ? static_cast<off_t>(static_cast<std::uint64_t>(overlapped->Offset) |
                                                  (static_cast<std::uint64_t>(overlapped->OffsetHigh) << 32))
                             : static_cast<off_t>(-1);
    const ssize_t put = offset >= 0 ? pwrite(handle->fd_, buffer, bytes, offset) : write(handle->fd_, buffer, bytes);
    if (put < 0) {
        return FALSE;
    }
    if (written != nullptr) {
        *written = static_cast<DWORD>(put);
    }
    if (overlapped != nullptr) {
        overlapped->Internal = 0;
        overlapped->InternalHigh = static_cast<ULONG_PTR>(put);
    }
    return TRUE;
}

DWORD SetFilePointer(HANDLE file, LONG distance, LONG* high, DWORD method)
{
    const PortHandle* handle = as_handle(file);
    if (handle == nullptr || handle->kind_ != HandleKind::kFile) {
        return INVALID_SET_FILE_POINTER;
    }
    const int whence = method == FILE_BEGIN ? SEEK_SET : (method == FILE_CURRENT ? SEEK_CUR : SEEK_END);
    const off_t offset = static_cast<off_t>(distance) + (high != nullptr ? (static_cast<off_t>(*high) << 32) : 0);
    const off_t where = lseek(handle->fd_, offset, whence);
    if (where < 0) {
        return INVALID_SET_FILE_POINTER;
    }
    if (high != nullptr) {
        *high = static_cast<LONG>(static_cast<std::uint64_t>(where) >> 32);
    }
    return static_cast<DWORD>(where & 0xFFFFFFFF);
}

BOOL SetFilePointerEx(HANDLE file, LARGE_INTEGER distance, LARGE_INTEGER* where, DWORD method)
{
    const PortHandle* handle = as_handle(file);
    if (handle == nullptr || handle->kind_ != HandleKind::kFile) {
        return FALSE;
    }
    const int whence = method == FILE_BEGIN ? SEEK_SET : (method == FILE_CURRENT ? SEEK_CUR : SEEK_END);
    const off_t at = lseek(handle->fd_, static_cast<off_t>(distance.QuadPart), whence);
    if (at < 0) {
        return FALSE;
    }
    if (where != nullptr) {
        where->QuadPart = static_cast<LONGLONG>(at);
    }
    return TRUE;
}

DWORD GetFileSize(HANDLE file, DWORD* high)
{
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size)) {
        return INVALID_FILE_SIZE;
    }
    if (high != nullptr) {
        *high = static_cast<DWORD>(static_cast<std::uint64_t>(size.QuadPart) >> 32);
    }
    return static_cast<DWORD>(static_cast<std::uint64_t>(size.QuadPart) & 0xFFFFFFFFU);
}

BOOL GetFileSizeEx(HANDLE file, LARGE_INTEGER* size)
{
    const PortHandle* handle = as_handle(file);
    if (handle == nullptr || handle->kind_ != HandleKind::kFile || size == nullptr) {
        return FALSE;
    }
    struct stat info;
    if (fstat(handle->fd_, &info) != 0) {
        return FALSE;
    }
    size->QuadPart = static_cast<LONGLONG>(info.st_size);
    return TRUE;
}

DWORD GetFileType(HANDLE file) { return as_handle(file) != nullptr ? 1U : 0U; } // FILE_TYPE_DISK

BOOL GetOverlappedResult(HANDLE /*file*/, OVERLAPPED* overlapped, DWORD* transferred, BOOL /*wait*/)
{
    if (overlapped == nullptr) {
        return FALSE;
    }
    if (transferred != nullptr) {
        *transferred = static_cast<DWORD>(overlapped->InternalHigh);
    }
    return TRUE;
}

BOOL FlushFileBuffers(HANDLE file)
{
    const PortHandle* handle = as_handle(file);
    return handle != nullptr && handle->kind_ == HandleKind::kFile ? fsync(handle->fd_) == 0 : FALSE;
}

DWORD GetFileAttributesA(const char* path)
{
    struct stat info;
    if (path == nullptr || stat(path, &info) != 0) {
        return INVALID_FILE_ATTRIBUTES;
    }
    return S_ISDIR(info.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
}

DWORD GetFileAttributesW(const wchar_t* path)
{
    char narrow[4096];
    if (WideCharToMultiByte(CP_UTF8, 0, path, -1, narrow, sizeof(narrow), nullptr, nullptr) <= 0) {
        return INVALID_FILE_ATTRIBUTES;
    }
    return GetFileAttributesA(narrow);
}

BOOL CreateDirectoryA(const char* path, SECURITY_ATTRIBUTES* /*attributes*/)
{
    return path != nullptr && mkdir(path, 0755) == 0;
}

BOOL DeleteFileA(const char* path) { return path != nullptr && unlink(path) == 0; }

BOOL MoveFileExA(const char* from, const char* to, DWORD /*flags*/)
{
    return from != nullptr && to != nullptr && rename(from, to) == 0;
}

DWORD GetFullPathNameA(const char* path, DWORD size, char* buffer, char** file_part)
{
    char resolved[4096];
    if (path == nullptr || realpath(path, resolved) == nullptr) {
        if (buffer != nullptr && size > 0) {
            buffer[0] = '\0';
        }
        return 0;
    }
    const std::size_t length = std::strlen(resolved);
    if (buffer != nullptr && size > length) {
        std::memcpy(buffer, resolved, length + 1);
    }
    if (file_part != nullptr) {
        *file_part = nullptr;
    }
    return static_cast<DWORD>(length);
}

HANDLE FindFirstFileA(const char* pattern, WIN32_FIND_DATAA* found)
{
    if (pattern == nullptr) {
        return INVALID_HANDLE_VALUE;
    }
    const std::string text = pattern;
    const std::size_t slash = text.find_last_of('/');
    const std::string directory = slash == std::string::npos ? std::string(".") : text.substr(0, slash);
    std::string mask = slash == std::string::npos ? text : text.substr(slash + 1);
    if (mask.empty()) {
        mask = "*";
    }
    DIR* dir = opendir(directory.c_str());
    if (dir == nullptr) {
        SetLastError(ERROR_PATH_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    HANDLE handle = make_handle(HandleKind::kFind);
    handle->user_ = dir;
    handle->name_ = mask;
    handle->fd_ = dirfd(dir);
    if (!FindNextFileA(handle, found)) {
        closedir(dir);
        delete handle;
        return INVALID_HANDLE_VALUE;
    }
    return handle;
}

int port_match(const char* mask, const char* name)
{
    // (a Win32 wildcard match: '*' any run, '?' one character, the rest literal, case-insensitive)
    const char* m = mask;
    const char* n = name;
    const char* star = nullptr;
    const char* retry = nullptr;
    while (*n != '\0') {
        if (*m == '?' || (*m != '\0' && *m != '*' &&
                          tolower(static_cast<unsigned char>(*m)) == tolower(static_cast<unsigned char>(*n)))) {
            ++m;
            ++n;
        } else if (*m == '*') {
            star = m++;
            retry = n;
        } else if (star != nullptr) {
            m = star + 1;
            n = ++retry;
        } else {
            return 0;
        }
    }
    while (*m == '*') {
        ++m;
    }
    return *m == '\0';
}

BOOL FindNextFileA(HANDLE find, WIN32_FIND_DATAA* found)
{
    PortHandle* handle = as_handle(find);
    if (handle == nullptr || handle->kind_ != HandleKind::kFind || found == nullptr) {
        return FALSE;
    }
    DIR* dir = static_cast<DIR*>(handle->user_);
    struct dirent* entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        if (!port_match(handle->name_.c_str(), entry->d_name)) {
            continue;
        }
        std::memset(found, 0, sizeof(*found));
        std::snprintf(found->cFileName, sizeof(found->cFileName), "%s", entry->d_name);
        found->dwFileAttributes = entry->d_type == DT_DIR ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        struct stat info;
        const std::string path = std::string() + "." + "/" + entry->d_name;
        if (stat(path.c_str(), &info) == 0) {
            found->nFileSizeLow = static_cast<DWORD>(info.st_size & 0xFFFFFFFF);
            found->nFileSizeHigh = static_cast<DWORD>(static_cast<std::uint64_t>(info.st_size) >> 32);
        }
        return TRUE;
    }
    SetLastError(ERROR_NO_MORE_FILES);
    return FALSE;
}

BOOL FindClose(HANDLE find)
{
    PortHandle* handle = as_handle(find);
    if (handle == nullptr || handle->kind_ != HandleKind::kFind) {
        return FALSE;
    }
    closedir(static_cast<DIR*>(handle->user_));
    delete handle;
    return TRUE;
}

// --- the handles --------------------------------------------------------------------------------------------------
BOOL CloseHandle(HANDLE handle)
{
    PortHandle* port = as_handle(handle);
    if (port == nullptr) {
        return FALSE;
    }
    switch (port->kind_) {
    case HandleKind::kFile:
        if (port->fd_ >= 0) {
            close(port->fd_);
        }
        break;
    case HandleKind::kThread:
        if (port->user_ != nullptr) {
            pthread_detach(port->thread_);
        }
        break;
    case HandleKind::kMutex:
        pthread_mutex_destroy(&port->mutex_);
        break;
    case HandleKind::kEvent:
    case HandleKind::kSemaphore:
    case HandleKind::kTimer:
        pthread_mutex_destroy(&port->mutex_);
        pthread_cond_destroy(&port->cond_);
        break;
    case HandleKind::kLibrary:
        if (port->user_ != nullptr) {
            dlclose(port->user_);
        }
        break;
    case HandleKind::kFind:
        if (port->user_ != nullptr) {
            closedir(static_cast<DIR*>(port->user_));
        }
        break;
    case HandleKind::kKey:
    default:
        break;
    }
    delete port;
    return TRUE;
}

// --- the threads and their sync -----------------------------------------------------------------------------------
namespace {

struct ThreadStart {
    DWORD(WIN32_STDCALL* function_)(LPVOID);
    LPVOID parameter_;
    PortHandle* handle_;
};

void* thread_body(void* argument)
{
    auto* start = static_cast<ThreadStart*>(argument);
    if (start->handle_->suspended_) {
        pthread_mutex_lock(&start->handle_->mutex_);
        while (start->handle_->suspended_) {
            pthread_cond_wait(&start->handle_->cond_, &start->handle_->mutex_);
        }
        pthread_mutex_unlock(&start->handle_->mutex_);
    }
    const DWORD code = start->function_ != nullptr ? start->function_(start->parameter_) : 0;
    PortHandle* handle = start->handle_;
    delete start;
    handle->exit_code_ = code;
    handle->signalled_ = true;
    pthread_mutex_lock(&handle->mutex_);
    pthread_cond_broadcast(&handle->cond_);
    pthread_mutex_unlock(&handle->mutex_);
    return nullptr;
}

} // namespace

HANDLE port_create_thread(SECURITY_ATTRIBUTES* /*attributes*/, SIZE_T /*stack*/, DWORD(WIN32_STDCALL* start)(LPVOID),
                          LPVOID parameter, DWORD flags, DWORD* id)
{
    HANDLE handle = make_handle(HandleKind::kThread);
    pthread_mutex_init(&handle->mutex_, nullptr);
    pthread_cond_init(&handle->cond_, nullptr);
    auto* body = new ThreadStart{start, parameter, handle};
    if ((flags & CREATE_SUSPENDED) != 0) {
        // (created suspended: it starts when ResumeThread is called - the game starts its threads that way to give
        // each a stack size and a priority before its first instruction)
        handle->suspended_ = true;
    }
    if (pthread_create(&handle->thread_, nullptr, thread_body, body) != 0) {
        delete body;
        delete handle;
        return nullptr;
    }
    if (id != nullptr) {
        *id = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(handle->thread_) >> 4);
    }
    return handle;
}

HANDLE CreateThread(SECURITY_ATTRIBUTES* attributes, SIZE_T stack, DWORD(WIN32_STDCALL* start)(LPVOID),
                    LPVOID parameter, DWORD flags, DWORD* id)
{
    return port_create_thread(attributes, stack, start, parameter, flags, id);
}

DWORD WIN32_STDCALL port_thread_detail::wide_trampoline(LPVOID wide)
{
    auto* start = static_cast<port_thread_detail::WideStart*>(wide);
    const unsigned long code = start->function_(start->parameter_);
    delete start;
    return static_cast<DWORD>(code);
}

DWORD ResumeThread(HANDLE thread)
{
    PortHandle* handle = as_handle(thread);
    if (handle == nullptr || handle->kind_ != HandleKind::kThread) {
        return static_cast<DWORD>(-1);
    }
    pthread_mutex_lock(&handle->mutex_);
    handle->suspended_ = false;
    pthread_cond_broadcast(&handle->cond_);
    pthread_mutex_unlock(&handle->mutex_);
    return 1;
}

BOOL SetThreadAffinityMask(HANDLE /*thread*/, DWORD_PTR /*mask*/) { return TRUE; }

DWORD WaitForSingleObject(HANDLE handle, DWORD milliseconds)
{
    PortHandle* port = as_handle(handle);
    if (port == nullptr) {
        return WAIT_FAILED;
    }
    const std::uint64_t deadline =
        milliseconds == INFINITE ? 0 : now_ns() + (static_cast<std::uint64_t>(milliseconds) * 1000000ULL);
    switch (port->kind_) {
    case HandleKind::kThread:
    case HandleKind::kEvent: {
        pthread_mutex_lock(&port->mutex_);
        while (!port->signalled_) {
            if (port->kind_ == HandleKind::kThread && milliseconds != INFINITE) {
                // (a thread's end is waited for with a condvar too)
            }
            if (deadline == 0) {
                pthread_cond_wait(&port->cond_, &port->mutex_);
                continue;
            }
            struct timespec when;
            when.tv_sec = static_cast<time_t>(deadline / 1000000000ULL);
            when.tv_nsec = static_cast<long>(deadline % 1000000000ULL);
            if (pthread_cond_timedwait(&port->cond_, &port->mutex_, &when) == ETIMEDOUT) {
                const bool done = port->signalled_;
                pthread_mutex_unlock(&port->mutex_);
                return done ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
            }
        }
        const bool manual = port->manual_reset_;
        if (!manual) {
            port->signalled_ = false;
        }
        pthread_mutex_unlock(&port->mutex_);
        return WAIT_OBJECT_0;
    }
    case HandleKind::kMutex: {
        pthread_mutex_lock(&port->mutex_);
        return WAIT_OBJECT_0;
    }
    case HandleKind::kTimer: {
        if (!port->has_deadline_) {
            return WAIT_TIMEOUT;
        }
        if (port->deadline_ns_ > now_ns()) {
            sleep_until(port->deadline_ns_);
        }
        port->has_deadline_ = false;
        return WAIT_OBJECT_0;
    }
    case HandleKind::kSemaphore: {
        pthread_mutex_lock(&port->mutex_);
        while (port->count_ <= 0) {
            if (deadline == 0) {
                pthread_cond_wait(&port->cond_, &port->mutex_);
                continue;
            }
            struct timespec when;
            when.tv_sec = static_cast<time_t>(deadline / 1000000000ULL);
            when.tv_nsec = static_cast<long>(deadline % 1000000000ULL);
            if (pthread_cond_timedwait(&port->cond_, &port->mutex_, &when) == ETIMEDOUT) {
                pthread_mutex_unlock(&port->mutex_);
                return WAIT_TIMEOUT;
            }
        }
        --port->count_;
        pthread_mutex_unlock(&port->mutex_);
        return WAIT_OBJECT_0;
    }
    case HandleKind::kFile:
    default:
        return WAIT_OBJECT_0;
    }
}

DWORD WaitForSingleObjectEx(HANDLE handle, DWORD milliseconds, BOOL /*alertable*/)
{
    return WaitForSingleObject(handle, milliseconds);
}

DWORD WaitForMultipleObjects(DWORD count, const HANDLE* handles, BOOL all, DWORD milliseconds)
{
    if (count == 0 || handles == nullptr) {
        return WAIT_FAILED;
    }
    const DWORD step = 1;
    const std::uint64_t deadline =
        milliseconds == INFINITE ? 0 : now_ns() + (static_cast<std::uint64_t>(milliseconds) * 1000000ULL);
    for (;;) {
        DWORD first = WAIT_FAILED;
        DWORD ready = 0;
        for (DWORD i = 0; i < count; ++i) {
            const DWORD result = WaitForSingleObject(handles[i], 0);
            if (result == WAIT_OBJECT_0) {
                ++ready;
                if (first == WAIT_FAILED) {
                    first = i;
                }
                if (!all) {
                    return i;
                }
            } else if (first == WAIT_FAILED) {
                first = i;
            }
        }
        if (all && ready == count) {
            return WAIT_OBJECT_0;
        }
        if (deadline != 0 && now_ns() >= deadline) {
            return WAIT_TIMEOUT;
        }
        Sleep(step);
        if (all) {
            for (DWORD i = 0; i < count; ++i) {
                PortHandle* port = as_handle(handles[i]);
                if (port != nullptr && port->kind_ == HandleKind::kEvent && !port->manual_reset_) {
                    port->signalled_ = false;
                }
            }
        }
    }
}

BOOL GetExitCodeThread(HANDLE thread, DWORD* code)
{
    const PortHandle* handle = as_handle(thread);
    if (handle == nullptr || code == nullptr) {
        return FALSE;
    }
    *code = handle->exit_code_;
    return TRUE;
}

BOOL SetThreadPriority(HANDLE /*thread*/, int /*priority*/) { return TRUE; }

void InitializeCriticalSection(CRITICAL_SECTION* section)
{
    if (section == nullptr || section->impl != nullptr) {
        return;
    }
    auto* mutex = new pthread_mutex_t();
    pthread_mutexattr_t attributes;
    pthread_mutexattr_init(&attributes);
    pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(mutex, &attributes);
    pthread_mutexattr_destroy(&attributes);
    section->impl = mutex;
}

void EnterCriticalSection(CRITICAL_SECTION* section)
{
    if (section != nullptr && section->impl != nullptr) {
        pthread_mutex_lock(static_cast<pthread_mutex_t*>(section->impl));
    }
}

void LeaveCriticalSection(CRITICAL_SECTION* section)
{
    if (section != nullptr && section->impl != nullptr) {
        pthread_mutex_unlock(static_cast<pthread_mutex_t*>(section->impl));
    }
}

BOOL TryEnterCriticalSection(CRITICAL_SECTION* section)
{
    return section != nullptr && section->impl != nullptr &&
                   pthread_mutex_trylock(static_cast<pthread_mutex_t*>(section->impl)) == 0
               ? TRUE
               : FALSE;
}

void DeleteCriticalSection(CRITICAL_SECTION* section)
{
    if (section != nullptr && section->impl != nullptr) {
        pthread_mutex_destroy(static_cast<pthread_mutex_t*>(section->impl));
        delete static_cast<pthread_mutex_t*>(section->impl);
        section->impl = nullptr;
    }
}

HANDLE CreateMutexA(SECURITY_ATTRIBUTES* /*attributes*/, BOOL owned, const char* /*name*/)
{
    HANDLE handle = make_handle(HandleKind::kMutex);
    // (a Win32 mutex is recursive for the thread that owns it: waiting on it again succeeds and counts, which
    // render_pc's loading screen relies on - it holds the screen's mutex while drawing the picture, which takes it
    // again. A plain pthread mutex would deadlock the main thread there.)
    pthread_mutexattr_t attributes;
    pthread_mutexattr_init(&attributes);
    pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&handle->mutex_, &attributes);
    pthread_mutexattr_destroy(&attributes);
    if (owned) {
        pthread_mutex_lock(&handle->mutex_);
        handle->owned_ = true;
    }
    return handle;
}

HANDLE CreateMutexW(SECURITY_ATTRIBUTES* attributes, BOOL owned, const wchar_t* name)
{
    char narrow[512];
    if (WideCharToMultiByte(CP_UTF8, 0, name, -1, narrow, sizeof(narrow), nullptr, nullptr) <= 0) {
        return CreateMutexA(attributes, owned, nullptr);
    }
    return CreateMutexA(attributes, owned, narrow);
}

BOOL ReleaseMutex(HANDLE mutex)
{
    PortHandle* handle = as_handle(mutex);
    if (handle == nullptr || handle->kind_ != HandleKind::kMutex) {
        return FALSE;
    }
    handle->owned_ = false;
    return pthread_mutex_unlock(&handle->mutex_) == 0;
}

HANDLE CreateEventA(SECURITY_ATTRIBUTES* /*attributes*/, BOOL manual_reset, BOOL signalled, const char* /*name*/)
{
    HANDLE handle = make_handle(HandleKind::kEvent);
    pthread_mutex_init(&handle->mutex_, nullptr);
    pthread_cond_init(&handle->cond_, nullptr);
    handle->manual_reset_ = manual_reset != 0;
    handle->signalled_ = signalled != 0;
    return handle;
}

HANDLE CreateEventW(SECURITY_ATTRIBUTES* attributes, BOOL manual_reset, BOOL signalled, const wchar_t* name)
{
    char narrow[512];
    if (WideCharToMultiByte(CP_UTF8, 0, name, -1, narrow, sizeof(narrow), nullptr, nullptr) <= 0) {
        return CreateEventA(attributes, manual_reset, signalled, nullptr);
    }
    return CreateEventA(attributes, manual_reset, signalled, narrow);
}

BOOL SetEvent(HANDLE event)
{
    PortHandle* handle = as_handle(event);
    if (handle == nullptr || handle->kind_ != HandleKind::kEvent) {
        return FALSE;
    }
    pthread_mutex_lock(&handle->mutex_);
    handle->signalled_ = true;
    pthread_cond_broadcast(&handle->cond_);
    pthread_mutex_unlock(&handle->mutex_);
    return TRUE;
}

BOOL ResetEvent(HANDLE event)
{
    PortHandle* handle = as_handle(event);
    if (handle == nullptr || handle->kind_ != HandleKind::kEvent) {
        return FALSE;
    }
    handle->signalled_ = false;
    return TRUE;
}

HANDLE CreateSemaphoreA(SECURITY_ATTRIBUTES* /*attributes*/, LONG initial, LONG /*maximum*/, const char* /*name*/)
{
    HANDLE handle = make_handle(HandleKind::kSemaphore);
    pthread_mutex_init(&handle->mutex_, nullptr);
    pthread_cond_init(&handle->cond_, nullptr);
    handle->count_ = initial;
    return handle;
}

BOOL ReleaseSemaphore(HANDLE semaphore, LONG count, LONG* previous)
{
    PortHandle* handle = as_handle(semaphore);
    if (handle == nullptr || handle->kind_ != HandleKind::kSemaphore) {
        return FALSE;
    }
    pthread_mutex_lock(&handle->mutex_);
    if (previous != nullptr) {
        *previous = handle->count_;
    }
    handle->count_ += count;
    pthread_cond_broadcast(&handle->cond_);
    pthread_mutex_unlock(&handle->mutex_);
    return TRUE;
}

HANDLE CreateWaitableTimerExW(SECURITY_ATTRIBUTES* /*attributes*/, const wchar_t* /*name*/, DWORD /*flags*/,
                              DWORD /*access*/)
{
    HANDLE handle = make_handle(HandleKind::kTimer);
    pthread_mutex_init(&handle->mutex_, nullptr);
    pthread_cond_init(&handle->cond_, nullptr);
    return handle;
}

HANDLE CreateWaitableTimerA(SECURITY_ATTRIBUTES* /*attributes*/, BOOL /*manual_reset*/, const char* /*name*/)
{
    HANDLE handle = make_handle(HandleKind::kTimer);
    pthread_mutex_init(&handle->mutex_, nullptr);
    pthread_cond_init(&handle->cond_, nullptr);
    return handle;
}

BOOL SetWaitableTimer(HANDLE timer, const LARGE_INTEGER* due, LONG /*period*/, void* /*routine*/, void* /*parameter*/,
                      BOOL /*resume*/)
{
    PortHandle* handle = as_handle(timer);
    if (handle == nullptr || handle->kind_ != HandleKind::kTimer || due == nullptr) {
        return FALSE;
    }
    // (100 ns units, as Win32: negative - from now, positive - since 1601)
    const std::int64_t value = due->QuadPart;
    if (value < 0) {
        handle->deadline_ns_ = now_ns() + static_cast<std::uint64_t>(-value) * 100ULL;
    } else {
        handle->deadline_ns_ = static_cast<std::uint64_t>(value) * 100ULL;
    }
    handle->has_deadline_ = true;
    return TRUE;
}

BOOL CancelWaitableTimer(HANDLE timer)
{
    PortHandle* handle = as_handle(timer);
    if (handle == nullptr || handle->kind_ != HandleKind::kTimer) {
        return FALSE;
    }
    handle->has_deadline_ = false;
    return TRUE;
}

// --- the settings: the registry and the ini file ------------------------------------------------------------------
namespace {

// The registry's file: one line per value, `key<TAB>name_<TAB>type_<TAB>data_` (data in hex for anything but text).
struct RegistryEntry {
    std::string key_;
    std::string name_;
    DWORD type_ = REG_DWORD;
    std::string data_;
};

std::vector<RegistryEntry> load_registry()
{
    std::vector<RegistryEntry> entries;
    const std::string text = read_file(registry_path());
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string line = text.substr(at, end - at);
        at = end + 1;
        std::size_t tab1 = line.find('\t');
        if (tab1 == std::string::npos) {
            continue;
        }
        std::size_t tab2 = line.find('\t', tab1 + 1);
        if (tab2 == std::string::npos) {
            continue;
        }
        std::size_t tab3 = line.find('\t', tab2 + 1);
        if (tab3 == std::string::npos) {
            continue;
        }
        RegistryEntry entry;
        entry.key_ = line.substr(0, tab1);
        entry.name_ = line.substr(tab1 + 1, tab2 - tab1 - 1);
        entry.type_ = static_cast<DWORD>(std::strtoul(line.substr(tab2 + 1, tab3 - tab2 - 1).c_str(), nullptr, 10));
        entry.data_ = line.substr(tab3 + 1);
        entries.push_back(entry);
    }
    return entries;
}

void save_registry(const std::vector<RegistryEntry>& entries)
{
    std::string text;
    for (const RegistryEntry& entry : entries) {
        text += entry.key_ + "\t" + entry.name_ + "\t" + std::to_string(entry.type_) + "\t" + entry.data_ + "\n";
    }
    write_text_file(registry_path(), text);
}

} // namespace

HKEY RegOpenKeyExA(HKEY key, const char* sub, DWORD /*options*/, DWORD /*access*/, HKEY* result)
{
    if (result == nullptr) {
        return nullptr;
    }
    const std::string path = registry_key_path(key) + (sub != nullptr ? std::string("\\") + sub : std::string());
    HANDLE handle = make_handle(HandleKind::kKey);
    handle->name_ = path;
    *result = handle;
    return handle;
}

HKEY RegCreateKeyA(HKEY key, const char* sub, HKEY* result) { return RegOpenKeyExA(key, sub, 0, 0, result); }

HKEY RegCreateKeyExA(HKEY key, const char* sub, DWORD /*reserved*/, char* /*class_name*/, DWORD /*options*/,
                     DWORD /*access*/, SECURITY_ATTRIBUTES* /*attributes*/, HKEY* result, DWORD* disposition)
{
    if (disposition != nullptr) {
        *disposition = 0; // REG_CREATED_NEW_KEY (the port's keys always "exist")
    }
    return RegOpenKeyExA(key, sub, 0, 0, result);
}

LONG RegQueryValueExA(HKEY key, const char* name, DWORD* /*reserved*/, DWORD* type, BYTE* data, DWORD* size)
{
    const std::string path = registry_key_path(key);
    for (const RegistryEntry& entry : load_registry()) {
        if (entry.key_ != path || entry.name_ != (name != nullptr ? name : "")) {
            continue;
        }
        if (type != nullptr) {
            *type = entry.type_;
        }
        if (entry.type_ == REG_SZ) {
            const std::size_t wanted = entry.data_.size() + 1;
            if (data == nullptr || size == nullptr) {
                return ERROR_SUCCESS;
            }
            if (*size < wanted) {
                *size = static_cast<DWORD>(wanted);
                return ERROR_MORE_DATA;
            }
            std::memcpy(data, entry.data_.data(), entry.data_.size());
            data[entry.data_.size()] = 0;
            *size = static_cast<DWORD>(wanted);
        } else {
            if (data == nullptr || size == nullptr) {
                return ERROR_SUCCESS;
            }
            if (*size < sizeof(DWORD)) {
                *size = sizeof(DWORD);
                return ERROR_MORE_DATA;
            }
            const DWORD value = static_cast<DWORD>(std::strtoul(entry.data_.c_str(), nullptr, 10));
            std::memcpy(data, &value, sizeof(value));
            *size = sizeof(DWORD);
        }
        return ERROR_SUCCESS;
    }
    return ERROR_FILE_NOT_FOUND;
}

LONG RegSetValueExA(HKEY key, const char* name, DWORD /*reserved*/, DWORD type, const BYTE* data, DWORD size)
{
    const std::string path = registry_key_path(key);
    std::vector<RegistryEntry> entries = load_registry();
    RegistryEntry wanted;
    wanted.key_ = path;
    wanted.name_ = name != nullptr ? name : "";
    wanted.type_ = type;
    if (type == REG_SZ) {
        wanted.data_ = std::string(reinterpret_cast<const char*>(data), size > 0 ? size - 1 : 0);
    } else if (data != nullptr && size >= sizeof(DWORD)) {
        DWORD value = 0;
        std::memcpy(&value, data, sizeof(value));
        wanted.data_ = std::to_string(value);
    }
    bool replaced = false;
    for (RegistryEntry& entry : entries) {
        if (entry.key_ == wanted.key_ && entry.name_ == wanted.name_) {
            entry = wanted;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        entries.push_back(wanted);
    }
    save_registry(entries);
    return ERROR_SUCCESS;
}

LONG RegGetValueA(HKEY key, const char* sub, const char* name, DWORD /*flags*/, DWORD* type, void* data, DWORD* size)
{
    HKEY opened = nullptr;
    if (sub != nullptr && sub[0] != '\0' && RegOpenKeyExA(key, sub, 0, 0, &opened) == nullptr) {
        return ERROR_ACCESS_DENIED;
    }
    const LONG result =
        RegQueryValueExA(opened != nullptr ? opened : key, name, nullptr, type, static_cast<BYTE*>(data), size);
    if (opened != nullptr) {
        RegCloseKey(opened);
    }
    return result;
}

LONG RegCloseKey(HKEY key)
{
    const PortHandle* handle = as_handle(key);
    if (handle == nullptr || handle->kind_ != HandleKind::kKey) {
        return ERROR_SUCCESS;
    }
    CloseHandle(key);
    return ERROR_SUCCESS;
}

LONG RegDeleteKeyA(HKEY /*key*/, const char* /*sub*/) { return ERROR_SUCCESS; }

UINT GetPrivateProfileStringA(const char* section, const char* name, const char* fallback, char* buffer, UINT size,
                              const char* file)
{
    std::string value;
    const std::string path = ini_path(file);
    const bool found = !path.empty() && ini_find(read_file(path), section != nullptr ? section : "",
                                                 name != nullptr ? name : "", &value);
    const std::string text = found ? value : (fallback != nullptr ? fallback : "");
    if (buffer == nullptr || size == 0) {
        return static_cast<UINT>(text.size());
    }
    const std::size_t count = text.size() < size - 1 ? text.size() : size - 1;
    std::memcpy(buffer, text.data(), count);
    buffer[count] = '\0';
    return static_cast<UINT>(count);
}

UINT GetPrivateProfileIntA(const char* section, const char* name, INT fallback, const char* file)
{
    char text[64];
    GetPrivateProfileStringA(section, name, "", text, sizeof(text), file);
    if (text[0] == '\0') {
        return static_cast<UINT>(fallback);
    }
    return static_cast<UINT>(std::strtol(text, nullptr, 10));
}

UINT GetPrivateProfileSectionA(const char* section, char* buffer, DWORD size, const char* file)
{
    // (the section's lines, each NUL-terminated, as Win32 gives them)
    const std::string path = ini_path(file);
    std::string text;
    if (!path.empty()) {
        const std::string all = read_file(path);
        const std::string wanted = std::string("[") + (section != nullptr ? section : "") + "]";
        std::size_t at = 0;
        bool inside = false;
        while (at < all.size()) {
            std::size_t end = all.find('\n', at);
            if (end == std::string::npos) {
                end = all.size();
            }
            std::string line = all.substr(at, end - at);
            at = end + 1;
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
                line.pop_back();
            }
            if (line.empty()) {
                continue;
            }
            if (line.front() == '[') {
                inside = line == wanted;
                continue;
            }
            if (inside) {
                text += line;
                text += '\0';
            }
        }
        text += '\0';
    }
    if (buffer == nullptr || size == 0) {
        return static_cast<UINT>(text.size());
    }
    const std::size_t count = text.size() < size ? text.size() : size;
    std::memcpy(buffer, text.data(), count);
    return static_cast<UINT>(count);
}

BOOL WritePrivateProfileStringA(const char* section, const char* name, const char* value, const char* file)
{
    const std::string path = ini_path(file);
    if (path.empty()) {
        return FALSE;
    }
    std::string text = read_file(path);
    const std::string wanted = std::string("[") + (section != nullptr ? section : "") + "]";
    // (rewrite the file: the section's line replaced or added, the rest as it was)
    std::vector<std::string> lines;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos) {
            end = text.size();
        }
        lines.push_back(text.substr(at, end - at));
        at = end + 1;
    }
    bool inside = false;
    bool wrote = false;
    std::size_t section_end = std::string::npos;
    for (std::string& line : lines) {
        std::string trimmed = line;
        while (!trimmed.empty() && (trimmed.back() == '\r' || trimmed.back() == ' ' || trimmed.back() == '\t')) {
            trimmed.pop_back();
        }
        if (!trimmed.empty() && trimmed.front() == '[') {
            if (inside && section_end == std::string::npos) {
                section_end = &line - lines.data(); // (the next section starts here: a new name goes before it)
            }
            inside = trimmed == wanted;
            continue;
        }
        if (inside && !trimmed.empty()) {
            const std::size_t equals = trimmed.find('=');
            std::string key = equals == std::string::npos ? trimmed : trimmed.substr(0, equals);
            while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) {
                key.pop_back();
            }
            if (key == (name != nullptr ? name : "")) {
                line = std::string(name != nullptr ? name : "") + "=" + (value != nullptr ? value : "");
                wrote = true;
            }
        }
    }
    if (!wrote) {
        if (section_end != std::string::npos) {
            lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(section_end),
                         std::string(name != nullptr ? name : "") + "=" + (value != nullptr ? value : ""));
        } else {
            // the section itself is added at the end
            bool has_section = false;
            for (const std::string& line : lines) {
                if (!line.empty() && line.front() == '[' && line == wanted) {
                    has_section = true;
                }
            }
            if (!has_section) {
                lines.push_back(wanted);
            }
            lines.push_back(std::string(name != nullptr ? name : "") + "=" + (value != nullptr ? value : ""));
        }
    }
    std::string out;
    for (const std::string& line : lines) {
        out += line;
        out += "\n";
    }
    write_text_file(path, out);
    return TRUE;
}

BOOL WritePrivateProfileStringW(const wchar_t* section, const wchar_t* name, const wchar_t* value, const wchar_t* file)
{
    char narrow_section[512];
    char narrow_name[512];
    char narrow_value[4096];
    char narrow_file[4096];
    WideCharToMultiByte(CP_UTF8, 0, section, -1, narrow_section, sizeof(narrow_section), nullptr, nullptr);
    WideCharToMultiByte(CP_UTF8, 0, name, -1, narrow_name, sizeof(narrow_name), nullptr, nullptr);
    WideCharToMultiByte(CP_UTF8, 0, value, -1, narrow_value, sizeof(narrow_value), nullptr, nullptr);
    WideCharToMultiByte(CP_UTF8, 0, file, -1, narrow_file, sizeof(narrow_file), nullptr, nullptr);
    return WritePrivateProfileStringA(narrow_section, narrow_name, narrow_value, narrow_file);
}

// --- the text -----------------------------------------------------------------------------------------------------
int MultiByteToWideChar(UINT /*code_page*/, DWORD /*flags*/, const char* text, int length, wchar_t* out, int out_count)
{
    if (text == nullptr) {
        return 0;
    }
    // (length < 0: the whole NUL-terminated text, the count then including its NUL, as Win32's)
    const bool whole = length < 0;
    const std::size_t bytes = whole ? std::strlen(text) + 1 : static_cast<std::size_t>(length);
    static thread_local std::vector<wchar_t> wide;
    wide.assign(bytes + 1, L'\0');
    const std::size_t converted = std::mbstowcs(wide.data(), text, bytes);
    if (converted == static_cast<std::size_t>(-1)) {
        return 0;
    }
    const std::size_t total = whole ? converted + 1 : converted;
    if (out != nullptr && out_count > 0) {
        const std::size_t copy =
            total < static_cast<std::size_t>(out_count) ? total : static_cast<std::size_t>(out_count);
        std::wmemcpy(out, wide.data(), copy);
    }
    return static_cast<int>(total);
}

int WideCharToMultiByte(UINT /*code_page*/, DWORD /*flags*/, const wchar_t* text, int length, char* out, int out_count,
                        const char* /*default_char*/, BOOL* used_default)
{
    if (text == nullptr) {
        return 0;
    }
    if (used_default != nullptr) {
        *used_default = FALSE;
    }
    const bool whole = length < 0;
    const std::size_t chars = whole ? std::wcslen(text) + 1 : static_cast<std::size_t>(length);
    static thread_local std::vector<char> narrow;
    narrow.assign((chars * 4) + 1, '\0');
    const std::size_t converted = std::wcstombs(narrow.data(), text, narrow.size());
    if (converted == static_cast<std::size_t>(-1)) {
        return 0;
    }
    const std::size_t total = whole ? converted + 1 : converted;
    if (out != nullptr && out_count > 0) {
        const std::size_t copy =
            total < static_cast<std::size_t>(out_count) ? total : static_cast<std::size_t>(out_count);
        std::memcpy(out, narrow.data(), copy);
    }
    return static_cast<int>(total);
}

int lstrlenA(const char* text) { return text != nullptr ? static_cast<int>(std::strlen(text)) : 0; }
int lstrlenW(const wchar_t* text) { return text != nullptr ? static_cast<int>(std::wcslen(text)) : 0; }

LPSTR lstrcpyA(LPSTR to, LPCSTR from)
{
    if (to != nullptr && from != nullptr) {
        std::strcpy(to, from);
    }
    return to;
}

// --- the program's start-up ---------------------------------------------------------------------------------------
void port_set_command_line(int argc, char** argv)
{
    // (the arguments joined the way Win32 gives them: a quoted argument keeps its quotes)
    g_command_line.clear();
    for (int i = 0; i < argc; ++i) {
        if (i != 0) {
            g_command_line += ' ';
        }
        const bool quoted = std::strchr(argv[i], ' ') != nullptr;
        if (quoted) {
            g_command_line += '"';
        }
        g_command_line += argv[i];
        if (quoted) {
            g_command_line += '"';
        }
    }
}

// --- the window ---------------------------------------------------------------------------------------------------
// (nothing of the game's Win32 window code runs on this platform - SDL3 makes the window - so these answer with what
// a caller can act on and do nothing else)
HANDLE GetStdHandle(DWORD which)
{
    static PortHandle standard[3] = {{.kind_ = HandleKind::kFile, .fd_ = 0},
                                     {.kind_ = HandleKind::kFile, .fd_ = 1},
                                     {.kind_ = HandleKind::kFile, .fd_ = 2}};
    if (which == static_cast<DWORD>(-10)) {
        return &standard[0];
    }
    if (which == static_cast<DWORD>(-11)) {
        return &standard[1];
    }
    if (which == static_cast<DWORD>(-12)) {
        return &standard[2];
    }
    return INVALID_HANDLE_VALUE;
}

BOOL SetStdHandle(DWORD which, HANDLE handle)
{
    PortHandle* standard = as_handle(GetStdHandle(which));
    if (standard == nullptr) {
        return FALSE;
    }
    const PortHandle* given = as_handle(handle);
    standard->fd_ = given != nullptr ? given->fd_ : -1;
    return TRUE;
}

DWORD GetLogicalDrives() { return 0; }
UINT GetDriveTypeA(const char* /*root*/) { return DRIVE_FIXED; }

BOOL GetVolumeInformationA(const char* /*root*/, char* volume, DWORD volume_size, DWORD* serial, DWORD* max_length,
                           DWORD* flags, char* filesystem, DWORD filesystem_size)
{
    if (volume != nullptr && volume_size > 0) {
        volume[0] = '\0';
    }
    if (filesystem != nullptr && filesystem_size > 0) {
        std::snprintf(filesystem, filesystem_size, "APFS");
    }
    if (serial != nullptr) {
        *serial = 0;
    }
    if (max_length != nullptr) {
        *max_length = 255;
    }
    if (flags != nullptr) {
        *flags = 0;
    }
    return TRUE;
}

int GetSystemMetrics(int index)
{
    if (index == SM_CXSCREEN || index == SM_CXVIRTUALSCREEN) {
        return 1920;
    }
    if (index == SM_CYSCREEN || index == SM_CYVIRTUALSCREEN) {
        return 1080;
    }
    return 0;
}

BOOL AdjustWindowRect(RECT* rect, DWORD /*style*/, BOOL /*menu*/)
{
    if (rect == nullptr) {
        return FALSE;
    }
    // (a window's frame: the caller only wants the inside's size back)
    rect->left -= 0;
    rect->top -= 0;
    rect->right += 0;
    rect->bottom += 0;
    return TRUE;
}

ATOM RegisterClassExA(const WNDCLASSEXA* /*window_class*/) { return 1; }
BOOL UnregisterClassA(const char* /*class_name*/, HINSTANCE /*instance*/) { return TRUE; }

HWND CreateWindowExA(DWORD /*ex_style*/, const char* /*class_name*/, const char* /*title*/, DWORD /*style*/, int /*x*/,
                     int /*y*/, int /*width*/, int /*height*/, HWND /*parent*/, HMENU /*menu*/, HINSTANCE /*instance*/,
                     void* /*parameter*/)
{
    return nullptr; // (SDL3's window is the game's: the platform's own Win32 one is not made here)
}

BOOL DestroyWindow(HWND /*window*/) { return TRUE; }
LRESULT DefWindowProcA(HWND /*window*/, UINT /*message*/, WPARAM /*wparam*/, LPARAM /*lparam*/) { return 0; }
BOOL PeekMessageA(MSG* /*message*/, HWND /*window*/, UINT /*first*/, UINT /*last*/, UINT /*remove*/) { return FALSE; }
BOOL GetMessageA(MSG* /*message*/, HWND /*window*/, UINT /*first*/, UINT /*last*/) { return FALSE; }
BOOL TranslateMessage(const MSG* /*message*/) { return FALSE; }
LRESULT DispatchMessageA(const MSG* /*message*/) { return 0; }
BOOL PostMessageA(HWND /*window*/, UINT /*message*/, WPARAM /*wparam*/, LPARAM /*lparam*/) { return TRUE; }
LRESULT SendMessageA(HWND /*window*/, UINT /*message*/, WPARAM /*wparam*/, LPARAM /*lparam*/) { return 0; }
LRESULT SendMessageW(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    return SendMessageA(window, message, wparam, lparam);
}
HWND FindWindowExA(HWND /*parent*/, HWND /*after*/, const char* /*class_name*/, const char* /*title*/)
{
    return nullptr;
}
int GetWindowTextA(HWND /*window*/, char* buffer, int size)
{
    if (buffer != nullptr && size > 0) {
        buffer[0] = '\0';
    }
    return 0;
}
BOOL SetWindowPos(HWND /*window*/, HWND /*after*/, int /*x*/, int /*y*/, int /*width*/, int /*height*/, UINT /*flags*/)
{
    return TRUE;
}
BOOL ShowWindow(HWND /*window*/, int /*command*/) { return TRUE; }
BOOL UpdateWindow(HWND /*window*/) { return TRUE; }
BOOL GetClientRect(HWND /*window*/, RECT* rect)
{
    if (rect == nullptr) {
        return FALSE;
    }
    rect->left = 0;
    rect->top = 0;
    rect->right = GetSystemMetrics(SM_CXSCREEN);
    rect->bottom = GetSystemMetrics(SM_CYSCREEN);
    return TRUE;
}
BOOL ClientToScreen(HWND /*window*/, POINT* /*point*/) { return TRUE; }
BOOL GetCursorPos(POINT* point)
{
    if (point != nullptr) {
        point->x = 0;
        point->y = 0;
    }
    return TRUE;
}
BOOL SetCursorPos(int /*x*/, int /*y*/) { return TRUE; }
BOOL ClipCursor(const RECT* /*rect*/) { return TRUE; } // (SDL3's relative mouse mode is the game's cursor)
HCURSOR LoadCursorA(HINSTANCE /*instance*/, const char* /*name*/) { return nullptr; }
HICON LoadIconA(HINSTANCE /*instance*/, const char* /*name*/) { return nullptr; }
LPVOID LoadImageW(HINSTANCE /*instance*/, const wchar_t* /*name*/, UINT /*type*/, int /*width*/, int /*height*/,
                  UINT /*flags*/)
{
    return nullptr;
}

// The C runtime's file descriptor as a handle (run_log.cpp: the log's stdio file, written with WriteFile).
HANDLE port_osfhandle_of(int fd)
{
    static std::map<int, HANDLE>* handles = new std::map<int, HANDLE>();
    const auto found = handles->find(fd);
    if (found != handles->end()) {
        return found->second;
    }
    HANDLE handle = make_handle(HandleKind::kFile);
    handle->fd_ = fd;
    (*handles)[fd] = handle;
    return handle;
}

// --- the display's modes ------------------------------------------------------------------------------------------
// (no mode is enumerated: the loop that fits a resolution to the display finds none and falls back to the desktop's,
// which is what this platform wants)
BOOL EnumDisplaySettingsA(const char* /*device*/, DWORD /*mode*/, DEVMODEA* settings)
{
    if (settings != nullptr) {
        std::memset(static_cast<void*>(settings), 0, sizeof(*settings));
    }
    return FALSE;
}

LONG ChangeDisplaySettingsA(DEVMODEA* /*settings*/, DWORD /*flags*/) { return 0; } // DISP_CHANGE_SUCCESSFUL

// --- the process's memory -----------------------------------------------------------------------------------------
HANDLE GetCurrentProcess()
{
    static PortHandle process{.kind_ = HandleKind::kFile, .fd_ = -1};
    return &process;
}

BOOL K32GetProcessMemoryInfo(HANDLE /*process*/, PROCESS_MEMORY_COUNTERS* counters, DWORD size)
{
    if (counters == nullptr || size < sizeof(PROCESS_MEMORY_COUNTERS)) {
        return FALSE;
    }
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    const kern_return_t result =
        task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count);
    if (result != KERN_SUCCESS) {
        return FALSE;
    }
    std::memset(counters, 0, sizeof(*counters));
    counters->cb = sizeof(*counters);
    counters->WorkingSetSize = static_cast<SIZE_T>(info.resident_size);
    counters->PeakWorkingSetSize = static_cast<SIZE_T>(info.resident_size_max);
    counters->PagefileUsage = static_cast<SIZE_T>(info.virtual_size);
    counters->PeakPagefileUsage = static_cast<SIZE_T>(info.virtual_size);
    return TRUE;
}

// --- the reader/writer lock, the virtual keys, the address space and the version -----------------------------------
namespace {

struct RwLock {
    pthread_rwlock_t lock_{};
    bool made_ = false;
};

RwLock* rw_lock(SRWLOCK* lock)
{
    auto* state = static_cast<RwLock*>(lock->impl);
    if (state == nullptr) {
        state = new RwLock();
        pthread_rwlock_init(&state->lock_, nullptr);
        state->made_ = true;
        lock->impl = state;
    }
    return state;
}

} // namespace

void InitializeSRWLock(SRWLOCK* lock) { (void)rw_lock(lock); }
void AcquireSRWLockExclusive(SRWLOCK* lock) { pthread_rwlock_wrlock(&rw_lock(lock)->lock_); }
void ReleaseSRWLockExclusive(SRWLOCK* lock) { pthread_rwlock_unlock(&rw_lock(lock)->lock_); }
void AcquireSRWLockShared(SRWLOCK* lock) { pthread_rwlock_rdlock(&rw_lock(lock)->lock_); }
void ReleaseSRWLockShared(SRWLOCK* lock) { pthread_rwlock_unlock(&rw_lock(lock)->lock_); }
BOOL TryAcquireSRWLockExclusive(SRWLOCK* lock) { return pthread_rwlock_trywrlock(&rw_lock(lock)->lock_) == 0; }

UINT MapVirtualKeyA(UINT code, UINT map_type)
{
    // (the scan code and the character of a key: the game only makes the menu's hints from these)
    switch (map_type) {
    case MAPVK_VK_TO_CHAR:
        return code >= 0x20 && code < 0x7F ? code : 0;
    case MAPVK_VSC_TO_VK_EX:
    case MAPVK_VSC_TO_VK:
        return code & 0xFF;
    case MAPVK_VK_TO_VSC:
    default:
        return code & 0xFF;
    }
}

UINT MapVirtualKeyW(UINT code, UINT map_type) { return MapVirtualKeyA(code, map_type); }

SIZE_T VirtualQuery(const void* /*address*/, MEMORY_BASIC_INFORMATION* info, SIZE_T size)
{
    // (no walk of the address space's regions: the game's report finds none and says nothing)
    if (info != nullptr && size >= sizeof(*info)) {
        std::memset(info, 0, sizeof(*info));
    }
    return 0;
}

BOOL GetVersionExA(OSVERSIONINFOA* info)
{
    if (info == nullptr || info->dwOSVersionInfoSize != sizeof(OSVERSIONINFOA)) {
        return FALSE;
    }
    // (the game asks whether it is NT or later, for overlapped reads: the answer is yes, and the port reads the way
    // the later path does)
    info->dwMajorVersion = 10;
    info->dwMinorVersion = 0;
    info->dwBuildNumber = 0;
    info->dwPlatformId = VER_PLATFORM_WIN32_NT;
    info->szCSDVersion[0] = '\0';
    return TRUE;
}

// --- the multimedia timer -----------------------------------------------------------------------------------------
namespace {

struct MediaTimer {
    UINT id_ = 0;
    UINT delay_ms_ = 0;
    bool periodic_ = false;
    LPTIMECALLBACK callback_ = nullptr;
    DWORD_PTR user_ = 0;
    bool stop_ = false;
    pthread_t thread_{};
    pthread_mutex_t mutex_{};
};

std::map<UINT, MediaTimer*>& media_timers()
{
    static std::map<UINT, MediaTimer*>* timers = new std::map<UINT, MediaTimer*>();
    return *timers;
}

UINT g_next_timer_id = 1;

void* media_timer_body(void* argument)
{
    auto* timer = static_cast<MediaTimer*>(argument);
    do {
        sleep_until(now_ns() + (static_cast<std::uint64_t>(timer->delay_ms_) * 1000000ULL));
        pthread_mutex_lock(&timer->mutex_);
        const bool stop = timer->stop_;
        pthread_mutex_unlock(&timer->mutex_);
        if (stop) {
            break;
        }
        timer->callback_(timer->id_, 0, timer->user_, 0, 0);
    } while (timer->periodic_);
    return nullptr;
}

} // namespace

MMRESULT timeSetEvent(UINT delay, UINT /*resolution*/, LPTIMECALLBACK callback, DWORD_PTR user, UINT event)
{
    if (callback == nullptr) {
        return 0;
    }
    auto* timer = new MediaTimer();
    timer->id_ = g_next_timer_id++;
    timer->delay_ms_ = delay;
    timer->periodic_ = event == TIME_PERIODIC;
    timer->callback_ = callback;
    timer->user_ = user;
    pthread_mutex_init(&timer->mutex_, nullptr);
    pthread_create(&timer->thread_, nullptr, media_timer_body, timer);
    media_timers()[timer->id_] = timer;
    return timer->id_;
}

MMRESULT timeKillEvent(UINT timer)
{
    const auto found = media_timers().find(timer);
    if (found == media_timers().end()) {
        return 0;
    }
    MediaTimer* entry = found->second;
    pthread_mutex_lock(&entry->mutex_);
    entry->stop_ = true;
    pthread_mutex_unlock(&entry->mutex_);
    pthread_join(entry->thread_, nullptr);
    media_timers().erase(found);
    pthread_mutex_destroy(&entry->mutex_);
    delete entry;
    return 0;
}

BOOL GetProcessTimes(HANDLE /*process*/, FILETIME* creation, FILETIME* exit, FILETIME* kernel, FILETIME* user)
{
    for (FILETIME* each : {creation, exit, kernel, user}) {
        if (each != nullptr) {
            each->dwLowDateTime = 0;
            each->dwHighDateTime = 0;
        }
    }
    return TRUE;
}

// --- the window, the console and the display's sleep ---------------------------------------------------------------
HWND GetForegroundWindow() { return nullptr; }
HWND GetActiveWindow() { return nullptr; }
HWND SetActiveWindow(HWND /*window*/) { return nullptr; }
BOOL PostQuitMessage(int /*code*/) { return TRUE; }
BOOL InvalidateRect(HWND /*window*/, const RECT* /*rect*/, BOOL /*erase*/) { return TRUE; }
BOOL ValidateRect(HWND /*window*/, const RECT* /*rect*/) { return TRUE; }
BOOL BringWindowToTop(HWND /*window*/) { return TRUE; }
BOOL SetForegroundWindow(HWND /*window*/) { return TRUE; }
HWND SetFocus(HWND /*window*/) { return nullptr; }
HMONITOR MonitorFromWindow(HWND /*window*/, DWORD /*flags*/) { return nullptr; }
int GetMonitorInfoA(HMONITOR /*monitor*/, void* /*info*/) { return FALSE; }
BOOL AllocConsole() { return FALSE; } // (the program already has its streams: pc_setup's console is Windows')
BOOL FreeConsole() { return TRUE; }
BOOL SetConsoleTitleA(const char* /*title*/) { return TRUE; }

UINT SetThreadExecutionState(UINT /*flags*/)
{
    // (the display is asked not to sleep while the game runs: this platform has no such call from a program, and macOS
    // leaves a running full-screen game's display on by itself)
    return 0;
}

// --- the program's folder -----------------------------------------------------------------------------------------
const char* port_game_directory()
{
    // (kept: game_directory() hands back a copy, whose text would be gone before the caller read it)
    static const std::string directory = game_directory();
    return directory.c_str();
}
