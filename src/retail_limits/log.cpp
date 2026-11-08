// NOT ORIGINAL - the retail-limits DLL's log (log.h).

#include "retail_limits/log.h"

#include <windows.h>

#include <string>
#include <string_view>

namespace retail_limits {

namespace {

std::string& log_path()
{
    static std::string path;
    return path;
}

// The text appended to the file (opened and closed for each line: nothing is lost if the game ends abruptly).
void append(const std::string& path, std::string_view text)
{
    HANDLE file = CreateFileA(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    CloseHandle(file);
}

} // namespace

void log_open(const std::string& path)
{
    log_path() = path;
    if (path.empty()) {
        return;
    }
    HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        CloseHandle(file);
    }
}

void log_line(std::string_view line)
{
    if (log_path().empty()) {
        return;
    }
    std::string text(line);
    text += "\r\n";
    append(log_path(), text);
}

} // namespace retail_limits
