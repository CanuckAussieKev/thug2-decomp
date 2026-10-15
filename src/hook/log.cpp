#include "hook/log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>

namespace {
FILE* g_log = nullptr;
}

void LogOpen()
{
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (char* slash = strrchr(path, '\\'))
        slash[1] = '\0';
    strcat_s(path, "thug2-decomp.log");
    fopen_s(&g_log, path, "w");
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
