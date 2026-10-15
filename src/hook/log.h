#pragma once

#include <string>

// Appends to thug2-decomp.log next to the game exe.
void LogOpen();
// Send Log output to the console instead (for tools).
void LogToConsole();
void Log(const char* fmt, ...);

// Full path of `file_name` in the game folder (the folder holding the exe).
std::string GameFolderPath(const char* file_name);

// An integer setting from thug2-decomp.ini in the game folder (`fallback` if absent).
int ConfigInt(const char* section, const char* key, int fallback);
