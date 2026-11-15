// NOT ORIGINAL - see test_workspace.h.

#include "platform/test_workspace.h"

#include <windows.h>

#include <cstddef>
#include <string>

#include "platform/process_flags.h"

namespace platform {

namespace {

constexpr const wchar_t* kWorkspaceMarker = L"thug2-decomp-always-muted";

bool marker_beside_exe()
{
    std::wstring path(MAX_PATH, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) {
        return false;
    }
    path.resize(length);
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return false;
    }
    path.resize(slash + 1);
    path += kWorkspaceMarker;
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

} // namespace

bool test_workspace()
{
    static const bool marked = marker_beside_exe() || batch_run();
    return marked;
}

bool batch_run()
{
    // (the command line as it started: -relit-bake <level>, a word of its own)
    static const bool batch = [] {
        const std::string& line = sys::startup_command_line();
        const std::size_t at = line.find("-relit-bake ");
        return at != std::string::npos && (at == 0 || line.at(at - 1) == ' ' || line.at(at - 1) == '"');
    }();
    return batch;
}

} // namespace platform
