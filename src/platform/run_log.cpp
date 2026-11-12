#include "platform/run_log.h"

#include <windows.h>
#include <string_view>

#include <atomic>

#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <algorithm>
#include <string>
#include <vector>

#include <io.h>
#include <array>

#include "platform/crash.h"
#include "base/core/address.h"
#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "base/core/no_destroy.h"

namespace {

constexpr std::size_t kNumberTextSize = 16;
constexpr int kLastLogCopy = 9; // a second copy of the game writes thug2-decomp-2.log, ... up to this number

// NOT ORIGINAL grouping - run_log.cpp's state (the original's globals). Made on first use, never destroyed
// (run_log_state()).
struct RunLogState {
    FILE* log_ = nullptr;
    bool console_ = false;
};

RunLogState& run_log_state()
{
    static core::NoDestroy<RunLogState> state;
    return state.get();
}

// The last lines logged, kept for get_log_tail (the screenshot sidecar - devtools/screenshot.h): fixed slots, no
// allocation or lock (Log runs in the crash logger too); a line written while another thread reads may come out torn.
constexpr unsigned kTailLines = 32;
constexpr size_t kTailWidth = 320;
// NOT ORIGINAL grouping - the log's last lines and how many there are (the original's globals). Made on first use,
// never destroyed (log_tail()).
struct LogTail {
    std::array<std::array<char, kTailWidth>, kTailLines> tail_{};
    std::atomic<unsigned> tail_count_{0};
};

LogTail& log_tail()
{
    static core::NoDestroy<LogTail> state;
    return state.get();
}

const char* keep_tail(const char* line)
{
    const unsigned slot = log_tail().tail_count_.fetch_add(1) % kTailLines;
    text::format_to(core::at(log_tail().tail_, slot), "%s", line);
    return core::at(log_tail().tail_, slot).data();
}

// Not original - who else wants each line (the loading console - engine/gfx/loading_console.h).
std::atomic<void (*)(const char*)>& stored_listener()
{
    static std::atomic<void (*)(const char*)> listener{nullptr};
    return listener;
}
} // namespace

std::string game_folder_path(const char* file_name)
{
    std::array<char, MAX_PATH> path{};
    GetModuleFileNameA(nullptr, path.data(), MAX_PATH);
    if (const size_t slash = std::string_view(path.data()).rfind('\\'); slash != std::string_view::npos) {
        core::at(path, slash + 1) = '\0';
    }
    return std::string(path.data()) + file_name;
}

int config_int(const char* section, const char* key, int fallback)
{
    return static_cast<int>(
        GetPrivateProfileIntA(section, key, fallback, game_folder_path("thug2-decomp.ini").c_str()));
}

std::string config_text(const char* section, const char* key, const char* fallback)
{
    std::array<char, 256> text{};
    GetPrivateProfileStringA(section, key, fallback, text.data(), static_cast<DWORD>(text.size()),
                             game_folder_path("thug2-decomp.ini").c_str());
    return text.data();
}

bool config_set_int(const char* section, const char* key, int value)
{
    std::array<char, kNumberTextSize> text{};
    text::format_to(text, "%d", value);
    return WritePrivateProfileStringA(section, key, text.data(), game_folder_path("thug2-decomp.ini").c_str()) != 0;
}

void log_open()
{
    // (fopen_s doesn't share a file open for writing: when another copy of the game has the log - [features]
    // allow_multiple_instances - this one writes thug2-decomp-2.log, -3, ...)
    if (fopen_s(&run_log_state().log_, game_folder_path("thug2-decomp.log").c_str(), "w") != 0) {
        for (int copy = 2; copy <= kLastLogCopy && !run_log_state().log_; ++copy) {
            const std::string name = "thug2-decomp-" + std::to_string(copy) + ".log";
            if (fopen_s(&run_log_state().log_, game_folder_path(name.c_str()).c_str(), "w") != 0) {
                run_log_state().log_ = nullptr;
            }
        }
    }
    // (crashes, CRT reports, run-time check failures... logged, the process ended, never a dialog - platform/crash.h)
    install_crash_handling(false);
}

void* log_file_handle()
{
    return run_log_state().log_ ? core::at_address<void>(_get_osfhandle(_fileno(run_log_state().log_)))
                                : INVALID_HANDLE_VALUE;
}

void log_line(const char* line)
{
    {
        const char* kept = keep_tail(line);
        if (void (*listener)(const char*) = stored_listener().load()) {
            listener(kept);
        }
    }
    if (run_log_state().console_) {
        std::fputs(line, stdout);
        std::putchar('\n');
        return;
    }
    if (!run_log_state().log_) {
        return;
    }
    std::fputs(line, run_log_state().log_);
    fputc('\n', run_log_state().log_);
    fflush(run_log_state().log_);
}

void set_log_listener(void (*listener)(const char* line)) { stored_listener() = listener; }

std::vector<std::string> get_log_tail(size_t lines)
{
    const unsigned count = log_tail().tail_count_.load();
    auto tail_count = std::min<size_t>({lines, count, kTailLines});
    std::vector<std::string> out;
    out.reserve(tail_count);
    for (size_t i = tail_count; i > 0; --i) {
        const char* line = core::at(log_tail().tail_, (count - i) % kTailLines).data();
        out.emplace_back(line, strnlen(line, kTailWidth));
    }
    return out;
}
