// NOT ORIGINAL - the loading console's lines (see loading_console.h); the panel is drawn by the graphics backend.

#include "engine/gfx/loading_console.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>
#include <array>

#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "platform/run_log.h"
#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "base/core/no_destroy.h"

namespace gfx::loading_console {

constexpr std::size_t kSizeTextChars = 32;
constexpr int kKilobyteBytes = 1024;
constexpr long long kKilobyteBytesLL = 1024LL;
constexpr double kKilobyteBytesD = 1024.0;
constexpr std::size_t kLineSlack = 32;

// --- pure helpers ---

const char* get_file_name(const char* path)
{
    const std::string_view full(path);
    const std::size_t slash = full.find_last_of("\\/");
    return slash == std::string_view::npos ? path : full.substr(slash + 1).data();
}

std::string size_text(long long bytes)
{
    std::array<char, kSizeTextChars> text{};
    if (bytes < kKilobyteBytes) {
        text::format_to(text, "%lld B", bytes);
    } else if (bytes < kKilobyteBytesLL * kKilobyteBytes) {
        text::format_to(text, "%.1f KB", static_cast<double>(bytes) / kKilobyteBytesD);
    } else {
        text::format_to(text, "%.1f MB", static_cast<double>(bytes) / (kKilobyteBytesD * kKilobyteBytesD));
    }
    return text.data();
}

bool interesting_command(const char* name)
{
    if (!name) {
        return false;
    }
    for (const char* prefix : {"Load", "Create", "Parse", "Init", "Preload", "Spawn"}) {
        if (_strnicmp(name, prefix, std::strlen(prefix)) == 0) {
            return true;
        }
    }
    // (Add* is mostly AddParams / AddFlag-style bookkeeping: only the loading ones)
    return std::ranges::any_of(std::array{"AddScene", "AddCollision", "AddSoundBanks", "AddMusicTrack", "AddStream"},
                               [&](const char* exact) { return _stricmp(name, exact) == 0; });
}

bool interesting_log_line(const char* line)
{
    if (!line || (*line == '\0')) {
        return false;
    }
    // (the data overlay's files are already there as opened; the rest is bookkeeping)
    return std::ranges::none_of(
        std::array{"data overlay:", "replaced 0x", "shadow checks:", "self-test", "  "},
        [&](const char* prefix) { return std::strncmp(line, prefix, std::strlen(prefix)) == 0; });
}

void Ring::append_one(const char* text, double time)
{
    if (count != 0) {
        Seg& newest = core::at(lines, (count - 1) % kRingLines);
        if (std::strncmp(newest.text.data(), text, kLineWidth - 1) == 0) {
            ++newest.repeat;
            newest.time = time;
            return;
        }
    }
    Seg& line = core::at(lines, count % kRingLines);
    strncpy_s(line.text.data(), line.text.size(), text, _TRUNCATE);
    line.time = time;
    line.repeat = 1;
    ++count;
}

std::vector<std::string> Ring::final_one(size_t line_count) const
{
    auto shown = std::min<size_t>({line_count, count, kRingLines});
    std::vector<std::string> out;
    out.reserve(shown);
    for (size_t i = shown; i > 0; --i) {
        const Seg& line = core::at(lines, (count - i) % kRingLines);
        std::array<char, kLineWidth + kLineSlack> text{};
        if (line.repeat > 1) {
            text::format_to(text, "[%6.1f] %s (x%u)", line.time, line.text.data(), line.repeat);
        } else {
            text::format_to(text, "[%6.1f] %s", line.time, line.text.data());
        }
        out.emplace_back(text.data());
    }
    return out;
}

namespace {

// --- the console's state ---

// NOT ORIGINAL grouping - loading_console.cpp's state (were globals). Made on first use, never destroyed
// (loading_console_state()).
struct LoadingConsoleState {
    SRWLOCK lock_ = SRWLOCK_INIT; // the ring and the activity time
    Ring ring_{};
    std::atomic<bool> active_{false};
    double start_ = 0.0; // when the load began (Now())
    double last_activity_ = 0.0; // the last line's time (Now())
};

LoadingConsoleState& loading_console_state()
{
    static core::NoDestroy<LoadingConsoleState> state;
    return state.get();
}

void add_line(const char* text)
{
    const double now = get_now();
    AcquireSRWLockExclusive(&loading_console_state().lock_);
    loading_console_state().ring_.append_one(text, now - loading_console_state().start_);
    loading_console_state().last_activity_ = now;
    ReleaseSRWLockExclusive(&loading_console_state().lock_);
}


void on_log_line(const char* line)
{
    if (loading_console_state().active_) {
        note_log_line(line);
    }
}


// Until app/ installs the backend's: nothing drawn.
void no_panel() {}
void no_panel_start(bool /*freeze*/, bool /*blank*/) {}

PanelHooks& panel_hooks()
{
    static PanelHooks hooks{.prepare = &no_panel, .restart_layout = &no_panel, .start = &no_panel_start, .stop = &no_panel};
    return hooks;
}

} // namespace

void install_panel(const PanelHooks& hooks) { panel_hooks() = hooks; }

double get_now()
{
    static const double kFrequency = [] {
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        return static_cast<double>(frequency.QuadPart);
    }();
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) / kFrequency;
}

Snapshot take_snapshot(size_t line_count)
{
    Snapshot shot;
    AcquireSRWLockShared(&loading_console_state().lock_);
    shot.lines = loading_console_state().ring_.final_one(line_count);
    shot.started = loading_console_state().start_;
    shot.last_activity = loading_console_state().last_activity_;
    ReleaseSRWLockShared(&loading_console_state().lock_);
    return shot;
}

bool is_switched_on()
{
    static const bool kEnabled = config_int("features", "loading_console", 1) != 0;
    return kEnabled;
}

bool live_now() { return loading_console_state().active_; }

void screen_shown(bool freeze, bool blank)
{
    if (!is_switched_on()) {
        return;
    }
    panel_hooks().prepare(); // (the font, made once)
    if (!loading_console_state().active_) {
        const double now = get_now();
        AcquireSRWLockExclusive(&loading_console_state().lock_);
        loading_console_state().ring_.set_count(0);
        loading_console_state().start_ = now;
        loading_console_state().last_activity_ = now;
        ReleaseSRWLockExclusive(&loading_console_state().lock_);
        panel_hooks().restart_layout();
        set_log_listener(&on_log_line);
        loading_console_state().active_ = true;
    }
    if (freeze) {
        get_note("loading screen (frozen frame)");
    } else if (blank) {
        get_note("loading screen (blank)");
    } else {
        get_note("loading screen");
    }
    panel_hooks().start(freeze, blank);
}

void screen_hidden()
{
    if (!loading_console_state().active_) {
        return;
    }
    loading_console_state().active_ = false;
    panel_hooks().stop();
    const double now = get_now();
    get_log("loading console: the loading screen was up %.1f s", now - loading_console_state().start_);
}

void note_text(const char* text)
{
    if (!loading_console_state().active_) {
        return;
    }
    add_line(text);
}

void note_file(const char* what, const char* path, long long size)
{
    if (!loading_console_state().active_ || !path) {
        return;
    }
    if (size >= 0) {
        get_note("%s %s  %s", what, get_file_name(path), size_text(size).c_str());
    } else {
        get_note("%s %s", what, get_file_name(path));
    }
}

void note_script_call(uint32_t name, bool cfunction, script::Struct* params)
{
    if (!loading_console_state().active_) {
        return;
    }
    const char* text = script::look_up_hash_name(name);
    if (cfunction) {
        if (!interesting_command(text)) {
            return;
        }
        // what it's given: a file or scene name, else a name checksum
        const char* what = nullptr;
        if (params) {
            if (!params->fetch_text(0U, &what, 0)) {
                for (const char* key : {"scene", "file", "name", "filename", "level", "type"}) {
                    if (params->fetch_text(key, &what, 0)) {
                        break;
                    }
                }
            }
            uint32_t checksum = 0;
            if (!what && (params->read_hash_value(0U, &checksum, 0) || params->read_hash_value("name", &checksum, 0) ||
                          params->read_hash_value("type", &checksum, 0))) {
                what = script::look_up_hash_name(checksum);
            }
        }
        if (what && (*what != '\0')) {
            get_note("%s %s", text, get_file_name(what));
        } else {
            get_note("%s", text);
        }
    } else if (text) {
        get_note("script %s", text);
    }
    // (a script whose name the game doesn't know - most of them, the retail scripts carry few names - isn't shown:
    // a checksum says nothing)
}

void note_log_line(const char* line)
{
    if (!loading_console_state().active_ || !interesting_log_line(line)) {
        return;
    }
    std::array<char, kLineWidth> text{};
    strncpy_s(text.data(), text.size(), line, _TRUNCATE);
    // (a log line can come from any thread, also while a crash is being logged: never wait for the lock)
    const double now = get_now();
    if (TryAcquireSRWLockExclusive(&loading_console_state().lock_) == FALSE) {
        return;
    }
    loading_console_state().ring_.append_one(text.data(), now - loading_console_state().start_);
    loading_console_state().last_activity_ = now;
    ReleaseSRWLockExclusive(&loading_console_state().lock_);
}

} // namespace gfx::loading_console
