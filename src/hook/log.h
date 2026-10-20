#pragma once

#include <string>

// Appends to thug2-decomp.log next to the game exe.
void LogOpen();
// Send Log output to the console instead (for tools).
void LogToConsole();
void Log(const char* fmt, ...);

// Called after a crash (a fatal exception in the game or this DLL) has been logged - each of the first few, first
// chance (the code may still handle it). It must not allocate or take locks: the heap may be what broke. One hook:
// the level editor's (its last action and its autosave - game/front/level_editor_layout.h).
void SetCrashHook(void (*hook)());

// Full path of `file_name` in the game folder (the folder holding the exe).
std::string GameFolderPath(const char* file_name);

// An integer setting from thug2-decomp.ini in the game folder (`fallback` if absent).
int ConfigInt(const char* section, const char* key, int fallback);
