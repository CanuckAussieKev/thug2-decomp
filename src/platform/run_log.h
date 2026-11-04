#pragma once

#include <string>
#include <vector>
#include <array>

#include "base/core/format.h"

// Appends to thug2-decomp.log next to the game exe.
void log_open();
// A line, as it is: kept for get_log_tail, passed to the listener, written to the log (or the console) with its
// line end.
void log_line(const char* line);
// A printf-style line (as log_line); the format is checked against the arguments when it's compiled (text::format).
template <typename... Args>
void get_log(text::FormatFor<Args...> format, Args... args)
{
    // (a stack buffer for the usual line - no allocation: a crash is logged through here - the heap for a longer one)
    constexpr size_t kLineSize = 1024;
    std::array<char, kLineSize> line{};
    const size_t length = text::format_to(line, format, args...);
    if (length >= kLineSize) {
        log_line(text::format(format, args...).c_str());
        return;
    }
    log_line(line.data());
}
// The last `lines` lines logged (at most 32, oldest first; each cut at 319 characters) - kept in memory: the log file
// itself can't be opened again while it's written.
std::vector<std::string> get_log_tail(size_t lines);
// Called with each line logged, on the logging thread, after it's kept for get_log_tail (one listener: the loading console -
// engine/gfx/loading_console.h). It must not wait for locks or log.
void set_log_listener(void (*listener)(const char* line));

// Called after a crash (a fatal exception in the game, or any report that ends the process - platform/crash.h)
// has been logged - each of the first few, first
// chance (the code may still handle it). It must not allocate or take locks: the heap may be what broke. One hook:
// the level editor's (its last action and its autosave - editor/level_editor_layout.h).
void set_crash_hook(void (*hook)());
// The log file's Windows handle (INVALID_HANDLE_VALUE without one): the Debug CRT writes its reports there.
void* log_file_handle();

// Full path of `file_name` in the game folder (the folder holding the exe).
std::string game_folder_path(const char* file_name);

// An integer setting from thug2-decomp.ini in the game folder (`fallback` if absent).
int config_int(const char* section, const char* key, int fallback);
// Write an integer setting into thug2-decomp.ini (WritePrivateProfileString). Whether it was written.
bool config_set_int(const char* section, const char* key, int value);
