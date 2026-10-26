// thug2_check: the start-up checks and the early self-tests, without starting the game.
//
//   thug2_check [<path to the reference build>]   (default: THUG2_REFERENCE_EXE, else local.json's referenceExe;
//                                                  the alternate THUG2.exe build works too)
//
// The game's code only runs at its own addresses (0x400000 up), and Windows puts its own data there in any
// process before main() runs. So this starts a second copy of itself suspended, reserves the game's address
// range in it before the Windows loader gets going, and lets it continue. That copy loads the reference build's image
// there (sections copied, imports resolved from System32 only - not the game folder, which has our own
// dinput8.dll), then:
//   - checks every replacement's argument size against the original's RET n, as the game does at start-up;
//   - runs the early self-tests, which compare our code with the original code. The game's own start-up
//     never runs, which is exactly the situation the early stage is written for (inside DllMain).
//
// Then the crash handling (hook/crash.h): each deliberate fault in a process of its own must be reported and end it.
//
// Exit code 0 when everything passes, 1 on a mismatch, a failed self-test or crash-handling check, 2 if the exe can't be loaded, 3 if
// a test corrupts memory (CRT report, run-time check failure, or the game's own buffer overrun check) - printed,
// never a dialog.
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "hook/crash.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

// Sys::FastExit (src/game/sys/pc_setup.cpp) calls the DLL's pre-exit hook; nothing to do here (it never runs
// offline).
extern "C" void thug2_decomp_before_exit() {}

namespace {

constexpr uintptr_t kImageBase = 0x00400000;

bool ReadFileBytes(const char* path, std::vector<unsigned char>& bytes)
{
    FILE* file = nullptr;
    if (fopen_s(&file, path, "rb") != 0 || !file)
        return false;
    unsigned char chunk[65536];
    size_t n;
    while ((n = std::fread(chunk, 1, sizeof(chunk), file)) > 0)
        bytes.insert(bytes.end(), chunk, chunk + n);
    std::fclose(file);
    return true;
}

std::string Trimmed(const std::vector<unsigned char>& bytes)
{
    std::string text(bytes.begin(), bytes.end());
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' '))
        text.pop_back();
    return text;
}

// The main checkout of a linked git worktree (its .git is a file "gitdir: <main>/.git/worktrees/<name>", whose
// commondir names <main>/.git), or empty: local.json is gitignored, so a worktree has none of its own.
std::string MainCheckout(const std::string& dir)
{
    std::vector<unsigned char> bytes;
    if (!ReadFileBytes((dir + "/.git").c_str(), bytes))
        return {};
    std::string gitdir = Trimmed(bytes);
    if (gitdir.rfind("gitdir: ", 0) != 0)
        return {};
    gitdir = gitdir.substr(8);
    bytes.clear();
    if (!ReadFileBytes((gitdir + "/commondir").c_str(), bytes))
        return {};
    std::string common = Trimmed(bytes);
    bool relative = !(common.size() > 1 && common[1] == ':') && common[0] != '/' && common[0] != '\\';
    char full[MAX_PATH];
    if (!GetFullPathNameA((relative ? gitdir + "/" + common : common).c_str(), sizeof(full), full, nullptr))
        return {};
    std::string main = full; // <main>\.git
    size_t slash = main.find_last_of("\\/");
    return slash == std::string::npos ? std::string() : main.substr(0, slash);
}

// The reference build's path when none is given: THUG2_REFERENCE_EXE, else referenceExe in the checkout's local.json
// (gitignored - this machine's settings; README "Contributing"; in a linked worktree, the main checkout's). Empty when
// neither has one.
std::string DefaultExePath()
{
    char env[MAX_PATH];
    DWORD length = GetEnvironmentVariableA("THUG2_REFERENCE_EXE", env, sizeof(env));
    if (length > 0 && length < sizeof(env))
        return env;
    std::vector<unsigned char> bytes;
    if (!ReadFileBytes(THUG2_SOURCE_DIR "/local.json", bytes)) {
        std::string main = MainCheckout(THUG2_SOURCE_DIR);
        if (main.empty() || !ReadFileBytes((main + "/local.json").c_str(), bytes))
            return {};
    }
    std::string text(bytes.begin(), bytes.end());
    // "referenceExe": "<path>" - a JSON string (\\ and \" escapes)
    size_t key = text.find("\"referenceExe\"");
    size_t colon = key == std::string::npos ? key : text.find(':', key);
    size_t quote = colon == std::string::npos ? colon : text.find('"', colon);
    if (quote == std::string::npos)
        return {};
    std::string path;
    for (size_t i = quote + 1; i < text.size() && text[i] != '"'; ++i) {
        if (text[i] == '\\' && i + 1 < text.size())
            ++i;
        path += text[i];
    }
    return path;
}

const IMAGE_NT_HEADERS32* GameHeaders(const std::vector<unsigned char>& bytes)
{
    if (bytes.size() < sizeof(IMAGE_DOS_HEADER))
        return nullptr;
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return nullptr;
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.ImageBase != kImageBase)
        return nullptr;
    return nt;
}

// --- the parent: start the child with the game's address range reserved ---------------------------------

// `mode`: "--child" (the checks) or "--crash <kind>" (one deliberate fault). `output`: the child's stdout and stderr
// collected there instead of shown (nullptr: shown).
int RunChild(const char* exe_path, DWORD image_size, const std::string& mode = "--child", std::string* output = nullptr)
{
    char self[MAX_PATH];
    GetModuleFileNameA(nullptr, self, MAX_PATH);
    std::string command = std::string("\"") + self + "\" " + mode + " \"" + exe_path + "\"";
    STARTUPINFOA si = {sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    HANDLE readEnd = nullptr, writeEnd = nullptr;
    if (output) {
        SECURITY_ATTRIBUTES inherit = {sizeof(inherit), nullptr, TRUE};
        if (!CreatePipe(&readEnd, &writeEnd, &inherit, 0))
            return 2;
        SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
        si.hStdOutput = si.hStdError = writeEnd;
    }
    PROCESS_INFORMATION pi = {};
    BOOL created = CreateProcessA(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED, nullptr, nullptr,
                                  &si, &pi);
    if (writeEnd)
        CloseHandle(writeEnd); // (the child's copy is the only one left: reading ends when it exits)
    if (!created) {
        std::printf("couldn't start the checking process (error %lu)\n", GetLastError());
        if (readEnd)
            CloseHandle(readEnd);
        return 2;
    }
    void* reserved = VirtualAllocEx(pi.hProcess, reinterpret_cast<void*>(kImageBase), image_size, MEM_RESERVE,
                                    PAGE_EXECUTE_READWRITE);
    if (reserved != reinterpret_cast<void*>(kImageBase)) {
        std::printf("couldn't reserve 0x%08zX in the checking process (error %lu)\n", kImageBase, GetLastError());
        TerminateProcess(pi.hProcess, 2);
        return 2;
    }
    ResumeThread(pi.hThread);
    if (output) {
        // (at most 60 s: a fault that shows a dialog instead of ending the process mustn't hang the check - it's
        // killed, exit code 6)
        ULONGLONG deadline = GetTickCount64() + 60000;
        char chunk[4096];
        for (;;) {
            bool ended = WaitForSingleObject(pi.hProcess, 20) == WAIT_OBJECT_0;
            DWORD available = 0, n = 0;
            while (PeekNamedPipe(readEnd, nullptr, 0, nullptr, &available, nullptr) && available &&
                   ReadFile(readEnd, chunk, sizeof(chunk), &n, nullptr) && n)
                output->append(chunk, n);
            if (ended)
                break;
            if (GetTickCount64() > deadline) {
                output->append("\n(still running after 60 s - a dialog? killed)\n");
                TerminateProcess(pi.hProcess, 6);
                WaitForSingleObject(pi.hProcess, INFINITE);
                break;
            }
        }
        CloseHandle(readEnd);
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 2;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<int>(code);
}

// --- the child: load the image, run the checks ----------------------------------------------------------

bool LoadGameImage(const std::vector<unsigned char>& bytes, const IMAGE_NT_HEADERS32* nt)
{
    auto* image = static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(kImageBase),
                                                           nt->OptionalHeader.SizeOfImage, MEM_COMMIT,
                                                           PAGE_EXECUTE_READWRITE));
    if (image != reinterpret_cast<unsigned char*>(kImageBase)) {
        std::printf("couldn't commit the game's address range (error %lu)\n", GetLastError());
        return false;
    }
    std::memcpy(image, bytes.data(), nt->OptionalHeader.SizeOfHeaders);
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        DWORD size = section->SizeOfRawData;
        if (section->Misc.VirtualSize && section->Misc.VirtualSize < size)
            size = section->Misc.VirtualSize;
        if (section->PointerToRawData + size > bytes.size())
            size = static_cast<DWORD>(bytes.size() - section->PointerToRawData);
        std::memcpy(image + section->VirtualAddress, bytes.data() + section->PointerToRawData, size);
    }

    // Imports, from System32 only. Libraries that aren't there (binkw32) stay unresolved; nothing the early
    // self-tests call uses them.
    const IMAGE_DATA_DIRECTORY& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(image + directory.VirtualAddress);
    int unresolved = 0;
    for (; directory.VirtualAddress && descriptor->Name; ++descriptor) {
        const char* library = reinterpret_cast<const char*>(image + descriptor->Name);
        HMODULE module = LoadLibraryExA(library, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA32*>(
            image + (descriptor->OriginalFirstThunk ? descriptor->OriginalFirstThunk : descriptor->FirstThunk));
        auto* addresses = reinterpret_cast<IMAGE_THUNK_DATA32*>(image + descriptor->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++addresses) {
            FARPROC function = nullptr;
            if (module) {
                if (IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal))
                    function = GetProcAddress(module, MAKEINTRESOURCEA(IMAGE_ORDINAL32(names->u1.Ordinal)));
                else
                    function = GetProcAddress(
                        module, reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(image + names->u1.AddressOfData)->Name);
            }
            addresses->u1.Function = reinterpret_cast<DWORD>(function);
            unresolved += function == nullptr;
        }
    }
    if (unresolved)
        std::printf("(%d imports not resolved - not needed by the early self-tests)\n", unresolved);
    return true;
}

// The game's own /GS check (VC7.1 CRT __security_error_handler, called from report_failure 0x622F9E when an
// original function's stack cookie is overwritten) shows a "Buffer overrun detected!" message box and waits.
// Offline that means a test corrupted an original's stack: print it and end the run instead.
void __cdecl GameSecurityError(int code, void*)
{
    std::printf("BUFFER OVERRUN in the game's code (its /GS check, code %d): a self-test corrupted the stack of "
                "an original function\n",
                code);
    std::fflush(stdout);
    _exit(3);
}

void RedirectGameSecurityErrorHandler()
{
    auto* entry = reinterpret_cast<unsigned char*>(0x006287DD);
    entry[0] = 0xE9; // JMP rel32 (the image is committed read/write/execute)
    *reinterpret_cast<int32_t*>(entry + 1) =
        static_cast<int32_t>(reinterpret_cast<uintptr_t>(&GameSecurityError) - (0x006287DD + 5));
}

int Child(const char* exe_path)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0); // (nothing is lost if a test crashes)
    LogToConsole();
    std::vector<unsigned char> bytes;
    const IMAGE_NT_HEADERS32* nt = ReadFileBytes(exe_path, bytes) ? GameHeaders(bytes) : nullptr;
    if (!nt || !LoadGameImage(bytes, nt))
        return 2;
    RedirectGameSecurityErrorHandler();
    // The file self-tests open data\pre\... relative to the working directory.
    std::string folder(exe_path);
    folder.resize(folder.find_last_of("\\/") == std::string::npos ? 0 : folder.find_last_of("\\/"));
    if (!folder.empty())
        SetCurrentDirectoryA(folder.c_str());

    int checked = 0, unchecked = 0, mismatched = 0;
    for (const ArgumentSizeCheck& check : CheckArgumentSizes()) {
        const Replacement& r = check.replacement;
        if (check.Mismatch()) {
            ++mismatched;
            std::printf("MISMATCH 0x%08X %s: the original pops %d bytes of arguments, ours %d\n", r.address, r.name,
                        check.original_pop, r.callee_pop);
        } else if (r.callee_pop >= 0 && check.original_pop >= 0) {
            ++checked;
        } else {
            ++unchecked;
            std::printf("not checked 0x%08X %s (%s)\n", r.address, r.name,
                        r.callee_pop < 0 ? "REPLACE_UNCHECKED stub" : "original's RET not found");
        }
    }
    std::printf("argument sizes: %d replacements, %d match the original, %d not checked, %d MISMATCHED\n",
                checked + unchecked + mismatched, checked, unchecked, mismatched);

    RunSelfTests(TestStage::Early);
    int failures = SelfTestFailures();
    std::fflush(stdout);
    return mismatched || failures ? 1 : 0;
}

// One deliberate fault (hook/crash.h) in a process of its own, with the game's image loaded and the handlers set as
// in the game: it must be reported and end the process - never a dialog, never a hang. Exit code 4: this build
// can't cause it.
int CrashChild(const char* kind, const char* exe_path)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    LogToConsole();
    TestFault fault;
    if (!ParseTestFault(kind, &fault))
        return 2;
    std::vector<unsigned char> bytes;
    const IMAGE_NT_HEADERS32* nt = ReadFileBytes(exe_path, bytes) ? GameHeaders(bytes) : nullptr;
    if (!nt || !LoadGameImage(bytes, nt))
        return 2;
    InstallGameCrtHandlers();
    if (!CauseTestFault(fault))
        return 4;
    return 5; // (still running: a handler returned)
}

// The crash handling, fault by fault: the exit code and the report each must give.
int CheckCrashHandling(const char* exe_path, DWORD image_size)
{
    struct Expected {
        const char* kind;
        DWORD exitCode;
        const char* report;
    };
    const Expected expected[] = {
        {"access", 0xC0000005, "unhandled: nothing caught exception 0xC0000005"},
        {"rtc", 3, "RUN-TIME CHECK FAILURE"},
        {"crterror", 3, "DEBUG CRT REPORT (type 1)"},
        {"invalidparam", 3, "INVALID PARAMETER"},
        {"purecall", 3, "PURE VIRTUAL CALL"},
        {"abort", 3, "ABORT"},
        {"overrun", 0xC0000409, "BUFFER OVERRUN DETECTED"},
        {"crtbox", 3, "GAME CRT MESSAGE: Microsoft Visual C++ Runtime Library: crash test: a message"},
    };
    const int total = static_cast<int>(sizeof(expected) / sizeof(expected[0]));
    int failed = 0, skipped = 0;
    for (const Expected& e : expected) {
        std::string output;
        DWORD code = static_cast<DWORD>(RunChild(exe_path, image_size, std::string("--crash ") + e.kind, &output));
        if (code == 4) {
            ++skipped;
            continue;
        }
        char ends[64];
        std::snprintf(ends, sizeof(ends), "the process ends (exit code %lu = 0x%08lX)", code, code);
        if (code != e.exitCode || output.find(e.report) == std::string::npos || output.find(ends) == std::string::npos) {
            ++failed;
            std::printf("CRASH HANDLING FAIL: %s - exit code 0x%08lX (wanted 0x%08lX), its output:\n%s\n", e.kind, code,
                        e.exitCode, output.c_str());
        }
    }
    std::printf("crash handling: %d faults, %d reported and ended as they should, %d not in this build, %d FAILED\n",
                total, total - failed - skipped, skipped, failed);
    return failed ? 1 : 0;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc > 2 && std::strcmp(argv[1], "--child") == 0)
        return Child(argv[2]);
    if (argc > 3 && std::strcmp(argv[1], "--crash") == 0)
        return CrashChild(argv[2], argv[3]);

    std::string default_path = argc > 1 ? std::string() : DefaultExePath();
    const char* exe_path = argc > 1 ? argv[1] : default_path.c_str();
    if (!*exe_path) {
        std::printf("usage: thug2_check <path to the reference build of THUG2.exe> (or THUG2_REFERENCE_EXE, or "
                    "referenceExe in local.json)\n");
        return 2;
    }
    std::vector<unsigned char> bytes;
    if (!ReadFileBytes(exe_path, bytes)) {
        std::printf("can't open %s\n", exe_path);
        return 2;
    }
    const IMAGE_NT_HEADERS32* nt = GameHeaders(bytes);
    if (!nt) {
        std::printf("%s isn't the 32-bit game exe (image base 0x400000)\n", exe_path);
        return 2;
    }
    int result = RunChild(exe_path, nt->OptionalHeader.SizeOfImage);
    int crashes = CheckCrashHandling(exe_path, nt->OptionalHeader.SizeOfImage);
    return result ? result : crashes;
}
