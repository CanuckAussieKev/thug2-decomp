#pragma once

#include <string>
#include <vector>

// Appends to thug2-decomp.log next to the game exe.
void LogOpen();
// Send Log output to the console instead (for tools).
void LogToConsole();
void Log(const char* fmt, ...);
// The last `lines` lines logged (at most 32, oldest first; each cut at 319 characters) - kept in memory: the log file
// itself can't be opened again while it's written.
std::vector<std::string> LogTail(size_t lines);

// Called after a crash (a fatal exception in the game or this DLL, or any report that ends the process -
// hook/crash.h) has been logged - each of the first few, first
// chance (the code may still handle it). It must not allocate or take locks: the heap may be what broke. One hook:
// the level editor's (its last action and its autosave - game/front/level_editor_layout.h).
void SetCrashHook(void (*hook)());
// The log file's Windows handle (INVALID_HANDLE_VALUE without one): the Debug CRT writes its reports there.
void* LogFileHandle();

// Full path of `file_name` in the game folder (the folder holding the exe).
std::string GameFolderPath(const char* file_name);

// An integer setting from thug2-decomp.ini in the game folder (`fallback` if absent).
int ConfigInt(const char* section, const char* key, int fallback);
// Write an integer setting into thug2-decomp.ini (WritePrivateProfileString). Whether it was written.
bool ConfigSetInt(const char* section, const char* key, int value);
