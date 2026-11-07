// NOT ORIGINAL - thug2-editor.exe, the level editor's launcher (ours): beside thug2-decomp.exe in the game folder, it
// starts the game in the editor mode - `thug2-decomp.exe -editor`, with whatever it was given passed on (`thug2-editor
// -edit BO`: straight into that level) - and ends. The editor mode is the game's (src/engine/sys/editor_mode.h); this
// knows nothing of the engine: plain Win32, so it can go wherever the editor goes.

#include <windows.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::wstring_view kGameExe = L"thug2-decomp.exe";
constexpr std::wstring_view kEditorFlag = L" -editor";

// The command line after the program's own name (quoted or not), spaces before it dropped.
std::wstring arguments_of(std::wstring_view line)
{
    std::size_t at = 0;
    if (line.starts_with(L'"')) {
        const std::size_t close = line.find(L'"', 1);
        at = close == std::wstring_view::npos ? line.size() : close + 1;
    } else {
        at = line.find_first_of(L" \t");
        at = at == std::wstring_view::npos ? line.size() : at;
    }
    const std::size_t first = line.find_first_not_of(L" \t", at);
    return first == std::wstring_view::npos ? std::wstring() : std::wstring(line.substr(first));
}

// This exe's folder, with its last separator ("" if it can't be told).
std::wstring own_folder()
{
    std::vector<wchar_t> path(MAX_PATH);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) {
            return {};
        }
        if (length < path.size()) {
            const std::wstring_view full(path.data(), length);
            const std::size_t slash = full.find_last_of(L"\\/");
            return slash == std::wstring_view::npos ? std::wstring() : std::wstring(full.substr(0, slash + 1));
        }
        path.resize(path.size() * 2);
    }
}

} // namespace

// (its parameters named as the Windows SDK declares them)
int WINAPI WinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/, char* /*lpCmdLine*/, int /*nShowCmd*/)
{
    const std::wstring folder = own_folder();
    const std::wstring game = folder + std::wstring(kGameExe);
    std::wstring command = L"\"" + game + L"\"" + std::wstring(kEditorFlag);
    const std::wstring passed = arguments_of(GetCommandLineW());
    if (!passed.empty()) {
        command += L" " + passed;
    }
    std::vector<wchar_t> line(command.begin(), command.end());
    line.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION started{};
    if (CreateProcessW(game.c_str(), line.data(), nullptr, nullptr, FALSE, 0, nullptr,
                       folder.empty() ? nullptr : folder.c_str(), &startup, &started) == FALSE) {
        const DWORD error = GetLastError();
        const std::wstring text =
            (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? L"thug2-decomp.exe isn't beside the editor:\n"
                                                                             : L"The editor couldn't start:\n") +
            game;
        MessageBoxW(nullptr, text.c_str(), L"THUG2 Editor", MB_OK | MB_ICONERROR);
        return 1;
    }
    // (the editor's window may come to the front: the launcher had the foreground)
    AllowSetForegroundWindow(started.dwProcessId);
    CloseHandle(started.hThread);
    CloseHandle(started.hProcess);
    return 0;
}
