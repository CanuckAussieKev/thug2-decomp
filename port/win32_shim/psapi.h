// NOT ORIGINAL - part of the macOS (arm64) port's Win32 shim (port/README.md). This header is NOT Windows': it gives
// the names of `psapi.h` to a build that has no such header, so that the engine and the game compile as they are read on
// Windows. It is only on the include path of a portable build (CMakeLists.txt, thug2_options: -I port/win32_shim); a
// Windows build never sees it.
#ifdef _WIN32
#error "port/win32_shim/psapi.h is the macOS port's stand-in - a Windows build must use the system's real header"
#endif

#pragma once

// NOT ORIGINAL - the macOS port's stand-in for <psapi.h>: what the game asks of it (its own process's memory figures,
// engine/memory/memory_report.cpp), answered from the Mach task's basic info.

#include "port/win32_types.h"

struct PROCESS_MEMORY_COUNTERS {
    DWORD cb{};
    DWORD PageFaultCount{};
    SIZE_T PeakWorkingSetSize{};
    SIZE_T WorkingSetSize{};
    SIZE_T QuotaPeakPagedPoolUsage{};
    SIZE_T QuotaPagedPoolUsage{};
    SIZE_T QuotaPeakNonPagedPoolUsage{};
    SIZE_T QuotaNonPagedPoolUsage{};
    SIZE_T PagefileUsage{};
    SIZE_T PeakPagefileUsage{};
};

HANDLE GetCurrentProcess();
BOOL K32GetProcessMemoryInfo(HANDLE process, PROCESS_MEMORY_COUNTERS* counters, DWORD size);
inline BOOL GetProcessMemoryInfo(HANDLE process, PROCESS_MEMORY_COUNTERS* counters, DWORD size)
{
    return K32GetProcessMemoryInfo(process, counters, size);
}
