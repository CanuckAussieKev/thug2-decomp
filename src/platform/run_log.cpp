#include "platform/run_log.h"

#include <windows.h>
#include <string_view>

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <io.h>
#include <share.h>
#include <array>

#include "platform/crash.h"
#include "platform/platform_config.h"
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
    // NOT ORIGINAL - the log file's full path and its name without ".log" (thug2-decomp, thug2-decomp-2...): kept at
    // log_open for log_keep_crash_copy, which mustn't allocate.
    std::array<char, MAX_PATH> path_{};
    std::array<char, MAX_PATH> stem_{};
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

// NOT ORIGINAL - the lines waiting for the log file. The drawing thread never waits on the disk (a busy disk held a frame
// in the log's flush for seconds): log_line copies the line into a slot of this ring (no allocation: a crash is logged
// through here) and a thread of its own writes the slots and flushes. A full ring drops the line and counts it (the
// writer says how many). log_flush writes what is waiting at once: the end of the process and every crash.
constexpr unsigned kQueueSlots = 1024;
constexpr size_t kSlotWidth = 2048; // (a longer line is cut)
struct LogQueue {
    std::mutex mutex_; // the ring's: head_, tail_, dropped_
    std::condition_variable wake_;
    std::mutex file_mutex_; // the file's: whoever writes the slots holds it
    std::array<std::array<char, kSlotWidth>, kQueueSlots> slots_{};
    std::array<unsigned short, kQueueSlots> lengths_{};
    unsigned long long head_ = 0; // the next slot to fill
    unsigned long long tail_ = 0; // the next to write
    unsigned dropped_ = 0;
    bool started_ = false;
};

LogQueue& log_queue()
{
    static core::NoDestroy<LogQueue> state;
    return state.get();
}

// The waiting lines written to the file and flushed (the file's lock held).
void write_waiting(LogQueue& queue)
{
    FILE* const log = run_log_state().log_;
    unsigned long long end = 0;
    unsigned long long begin = 0;
    {
        const std::scoped_lock lock(queue.mutex_);
        begin = queue.tail_;
        end = queue.head_;
    }
    // (a slot between tail_ and head_ is only read here: the producers fill the ones past head_)
    for (unsigned long long at = begin; at < end; ++at) {
        const size_t slot = static_cast<size_t>(at % kQueueSlots);
        std::fwrite(core::at(queue.slots_, slot).data(), 1, core::at(queue.lengths_, slot), log);
        std::fputc('\n', log);
    }
    unsigned dropped = 0;
    {
        const std::scoped_lock lock(queue.mutex_);
        queue.tail_ = end;
        dropped = std::exchange(queue.dropped_, 0U);
    }
    if (dropped != 0) {
        std::fprintf(log, "log: %u lines dropped (the disk was too slow)\n", dropped);
    }
    if (end != begin || dropped != 0) {
        std::fflush(log);
    }
}

void log_writer()
{
    LogQueue& queue = log_queue();
    for (;;) {
        {
            std::unique_lock lock(queue.mutex_);
            queue.wake_.wait(lock, [&queue] { return queue.head_ != queue.tail_ || queue.dropped_ != 0; });
        }
        const std::scoped_lock file_lock(queue.file_mutex_);
        write_waiting(queue);
    }
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
    return build_info::cached_profile_int(game_folder_path("thug2-decomp.ini").c_str(), section, key, fallback);
}

std::string config_text(const char* section, const char* key, const char* fallback)
{
    std::array<char, 256> text{};
    build_info::cached_profile_text(game_folder_path("thug2-decomp.ini").c_str(), section, key, fallback, text.data(),
                                    text.size());
    return text.data();
}

bool config_set_int(const char* section, const char* key, int value)
{
    std::array<char, kNumberTextSize> text{};
    text::format_to(text, "%d", value);
    const bool written =
        WritePrivateProfileStringA(section, key, text.data(), game_folder_path("thug2-decomp.ini").c_str()) != 0;
    build_info::forget_cached_reads();
    return written;
}

namespace {

// NOT ORIGINAL - the earlier runs' logs are kept: a run starting moves <stem>.log to <stem>.prev.log, that one to
// <stem>.prev2.log and so on up to this many (the oldest dropped). A crash that ended the run before is still there
// to read after the game has been started again.
constexpr int kKeptLogs = 3;

// <stem>.log (age 0), <stem>.prev.log (1), <stem>.prev2.log (2)...
std::string kept_log_path(const std::string& stem, int age)
{
    if (age == 0) {
        return stem + ".log";
    }
    return stem + ".prev" + (age == 1 ? std::string() : std::to_string(age)) + ".log";
}

// Whether another process (another copy of the game - allow_multiple_instances) has the file open for writing.
bool log_in_use(const std::string& path)
{
    HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return GetLastError() == ERROR_SHARING_VIOLATION;
    }
    CloseHandle(file);
    return false;
}

// The log `stem` names opened for this run, the earlier ones moved down a place first; false when another copy of the
// game writes it.
bool open_log(const std::string& stem)
{
    const std::string path = kept_log_path(stem, 0);
    if (log_in_use(path)) {
        return false;
    }
    for (int age = kKeptLogs; age > 0; --age) {
        MoveFileExA(kept_log_path(stem, age - 1).c_str(), kept_log_path(stem, age).c_str(), MOVEFILE_REPLACE_EXISTING);
    }
    // (others may read it while it's written - not write it: a second copy of the game takes the next name)
    FILE* log = _fsopen(path.c_str(), "w", _SH_DENYWR);
    if (!log) {
        return false;
    }
    run_log_state().log_ = log;
    text::format_to(run_log_state().path_, "%s", path.c_str());
    text::format_to(run_log_state().stem_, "%s", stem.c_str());
    return true;
}

} // namespace

void log_open()
{
    // (when another copy of the game has the log - [features] allow_multiple_instances - this one writes
    // thug2-decomp-2.log, -3, ...)
    if (!open_log(game_folder_path("thug2-decomp"))) {
        for (int copy = 2; copy <= kLastLogCopy && !run_log_state().log_; ++copy) {
            const std::string stem = "thug2-decomp-" + std::to_string(copy);
            open_log(game_folder_path(stem.c_str()));
        }
    }
    if (run_log_state().log_) {
        std::thread(log_writer).detach();
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
    LogQueue& queue = log_queue();
    {
        const std::scoped_lock lock(queue.mutex_);
        if (queue.head_ - queue.tail_ >= kQueueSlots) {
            ++queue.dropped_;
        } else {
            const size_t slot = static_cast<size_t>(queue.head_ % kQueueSlots);
            const size_t length = std::min(std::strlen(line), kSlotWidth);
            std::memcpy(core::at(queue.slots_, slot).data(), line, length);
            core::at(queue.lengths_, slot) = static_cast<unsigned short>(length);
            ++queue.head_;
        }
    }
    queue.wake_.notify_one();
}

void log_flush()
{
    LogQueue& queue = log_queue();
    if (!run_log_state().log_ || run_log_state().console_) {
        return;
    }
    // (the writer may be stuck in the disk: wait for it a while, not for ever)
    constexpr int kTries = 200;
    for (int tries = 0; tries < kTries; ++tries) {
        if (queue.file_mutex_.try_lock()) {
            write_waiting(queue);
            queue.file_mutex_.unlock();
            return;
        }
        Sleep(10);
    }
}

void log_flush_to_disk()
{
    log_flush();
    if (void* const handle = log_file_handle(); handle != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(static_cast<HANDLE>(handle));
    }
}

void log_keep_crash_copy()
{
    RunLogState& state = run_log_state();
    if (!state.log_ || state.console_) {
        return;
    }
    // <stem>-crash-<date>_<time>.txt beside the log, a new file (another crash in the same second: -2, -3...)
    SYSTEMTIME now{};
    GetLocalTime(&now);
    std::array<char, MAX_PATH> name{};
    HANDLE copy = INVALID_HANDLE_VALUE;
    constexpr int kTries = 9;
    for (int attempt = 1; attempt <= kTries && copy == INVALID_HANDLE_VALUE; ++attempt) {
        const unsigned year = now.wYear;
        const unsigned month = now.wMonth;
        const unsigned day = now.wDay;
        const unsigned hour = now.wHour;
        const unsigned minute = now.wMinute;
        const unsigned second = now.wSecond;
        if (attempt == 1) {
            text::format_to(name, "%s-crash-%04u-%02u-%02u_%02u-%02u-%02u.txt", state.stem_.data(), year, month, day,
                            hour, minute, second);
        } else {
            text::format_to(name, "%s-crash-%04u-%02u-%02u_%02u-%02u-%02u-%d.txt", state.stem_.data(), year, month,
                            day, hour, minute, second, attempt);
        }
        copy = CreateFileA(name.data(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (copy == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_EXISTS) {
            return;
        }
    }
    if (copy == INVALID_HANDLE_VALUE) {
        return;
    }
    const char* file_name = name.data();
    if (const char* slash = std::strrchr(file_name, '\\')) {
        file_name = slash + 1;
    }
    get_log("  this log is kept as %s", file_name);
    log_flush();
    // (the log read through a handle of its own: it's open here for writing, shared for reading - open_log)
    HANDLE log = CreateFileA(state.path_.data(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log != INVALID_HANDLE_VALUE) {
        // (static: no allocation, and not on a stack that may be what overflowed)
        constexpr DWORD kChunk = 64 * 1024;
        static std::array<char, kChunk> buffer;
        DWORD read = 0;
        while (ReadFile(log, buffer.data(), kChunk, &read, nullptr) && read != 0) {
            DWORD written = 0;
            if (!WriteFile(copy, buffer.data(), read, &written, nullptr) || written != read) {
                break;
            }
        }
        CloseHandle(log);
    }
    CloseHandle(copy);
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
