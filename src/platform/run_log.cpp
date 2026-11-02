#include "platform/run_log.h"

#include <windows.h>
#include <string_view>

#include <atomic>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <algorithm>
#include <string>
#include <vector>

#include <io.h>
#include <array>

#include "platform/crash.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"

namespace {
FILE* g_log = nullptr;
bool g_console = false;

// The last lines logged, kept for get_log_tail (the screenshot sidecar - platform/screenshot.h): fixed slots, no
// allocation or lock (Log runs in the crash logger too); a line written while another thread reads may come out torn.
constexpr unsigned kTailLines = 32;
constexpr size_t kTailWidth = 320;
std::array<std::array<char, kTailWidth>, kTailLines> g_tail;
std::atomic<unsigned> g_tail_count{0};

const char* keep_tail(const char* line)
{
    const unsigned slot = g_tail_count.fetch_add(1) % kTailLines;
    text::format_to(core::at(g_tail, slot), "%s", line);
    return core::at(g_tail, slot).data();
}

// Not original - who else wants each line (the loading console - engine/gfx/loading_console.h).
std::atomic<void (*)(const char*)> g_listener{nullptr};
} // namespace

std::string game_folder_path(const char* file_name)
{
    std::array<char, MAX_PATH> path{};
    GetModuleFileNameA(nullptr, path.data(), MAX_PATH);
    if (const size_t slash = std::string_view(path.data()).rfind('\\'); slash != std::string_view::npos)
        core::at(path, slash + 1) = '\0';
    return std::string(path.data()) + file_name;
}

int config_int(const char* section, const char* key, int fallback)
{
    return static_cast<int>(
        GetPrivateProfileIntA(section, key, fallback, game_folder_path("thug2-decomp.ini").c_str()));
}

bool config_set_int(const char* section, const char* key, int value)
{
    std::array<char, 16> text{};
    text::format_to(text, "%d", value);
    return WritePrivateProfileStringA(section, key, text.data(), game_folder_path("thug2-decomp.ini").c_str()) != 0;
}

void log_open()
{
    // (fopen_s doesn't share a file open for writing: when another copy of the game has the log - [features]
    // allow_multiple_instances - this one writes thug2-decomp-2.log, -3, ...)
    if (fopen_s(&g_log, game_folder_path("thug2-decomp.log").c_str(), "w") != 0) {
        for (int copy = 2; copy <= 9 && !g_log; ++copy) {
            const std::string name = "thug2-decomp-" + std::to_string(copy) + ".log";
            if (fopen_s(&g_log, game_folder_path(name.c_str()).c_str(), "w") != 0)
                g_log = nullptr;
        }
    }
    // (crashes, CRT reports, run-time check failures... logged, the process ended, never a dialog - platform/crash.h)
    install_crash_handling(false);
}

void* log_file_handle()
{
    return g_log ? cell::pointer<void>(_get_osfhandle(_fileno(g_log))) : INVALID_HANDLE_VALUE;
}

void log_line(const char* line)
{
    {
        const char* kept = keep_tail(line);
        if (void (*listener)(const char*) = g_listener.load())
            listener(kept);
    }
    if (g_console) {
        std::fputs(line, stdout);
        std::putchar('\n');
        return;
    }
    if (!g_log)
        return;
    std::fputs(line, g_log);
    fputc('\n', g_log);
    fflush(g_log);
}

void set_log_listener(void (*listener)(const char* line)) { g_listener = listener; }

std::vector<std::string> get_log_tail(size_t lines)
{
    const unsigned count = g_tail_count.load();
    auto n = std::min<size_t>({lines, count, kTailLines});
    std::vector<std::string> out;
    out.reserve(n);
    for (size_t i = n; i > 0; --i) {
        const char* line = core::at(g_tail, (count - i) % kTailLines).data();
        out.emplace_back(line, strnlen(line, kTailWidth));
    }
    return out;
}
