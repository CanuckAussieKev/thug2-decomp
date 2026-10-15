#include "hook/log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>

namespace {
FILE* g_log = nullptr;
}

std::string GameFolderPath(const char* file_name)
{
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (char* slash = strrchr(path, '\\'))
        slash[1] = '\0';
    return std::string(path) + file_name;
}

int ConfigInt(const char* section, const char* key, int fallback)
{
    return static_cast<int>(
        GetPrivateProfileIntA(section, key, fallback, GameFolderPath("thug2-decomp.ini").c_str()));
}

void LogOpen()
{
    fopen_s(&g_log, GameFolderPath("thug2-decomp.log").c_str(), "w");
}

void Log(const char* fmt, ...)
{
    if (!g_log)
        return;
    va_list args;
    va_start(args, fmt);
    vfprintf(g_log, fmt, args);
    va_end(args);
    fputc('\n', g_log);
    fflush(g_log);
}
