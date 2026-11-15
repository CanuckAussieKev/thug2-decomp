// NOT ORIGINAL - part of the macOS (arm64) port's Win32 shim (docs/design/macos-port.md). This header is NOT Windows': it gives
// the names of `process.h` to a build that has no such header, so that the engine and the game compile as they are read on
// Windows. It is only on the include path of a portable build (CMakeLists.txt, thug2_options: -I port/win32_shim); a
// Windows build never sees it.
#ifdef _WIN32
#error "platform/posix/process.h is the macOS port's stand-in - a Windows build must use the system's real header"
#endif

#pragma once

// NOT ORIGINAL - the macOS port's stand-in for MSVC's <process.h>: the few C runtime calls of it the source names
// (platform/posix/windows.h has the rest).

#include <unistd.h>

#include "platform/posix/win32_types.h"

using intptr_t = std::intptr_t;

inline int _getpid() { return static_cast<int>(getpid()); }
inline void _exit_now(int code) { _exit(code); }
