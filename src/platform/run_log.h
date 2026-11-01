#pragma once

#include <cstdio>
#include <string>
#include <vector>
#include <array>

// Appends to thug2-decomp.log next to the game exe.
void log_open();
// A line, as it is: kept for get_log_tail, passed to the listener, written to the log (or the console) with its
// line end.
void log_line(const char* line);
// A printf-style line (as log_line).
template <typename... Args>
void get_log(const char* fmt, Args... args)
{
    // (a stack buffer for the usual line - no allocation: a crash is logged through here - the heap for a longer one)
    constexpr int kLineSize = 1024;
    std::array<char, kLineSize> line{};
    const int length = std::snprintf(line.data(), sizeof(line), fmt, args...);
    if (length >= kLineSize) {
        std::string longer(static_cast<size_t>(length), ' ');
        std::snprintf(longer.data(), longer.size() + 1, fmt, args...);
        log_line(longer.c_str());
        return;
    }
    log_line(length < 0 ? "" : line.data());
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
