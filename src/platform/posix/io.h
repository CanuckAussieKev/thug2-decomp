// NOT ORIGINAL - part of the macOS (arm64) port's Win32 shim (docs/design/macos-port.md). This header is NOT Windows': it gives
// the names of `io.h` to a build that has no such header, so that the engine and the game compile as they are read on
// Windows. It is only on the include path of a portable build (CMakeLists.txt, thug2_options: -I port/win32_shim); a
// Windows build never sees it.
#ifdef _WIN32
#error "platform/posix/io.h is the macOS port's stand-in - a Windows build must use the system's real header"
#endif

#pragma once

// NOT ORIGINAL - the macOS port's stand-in for MSVC's <io.h>: a C runtime file descriptor as the handle the file calls
// of platform/posix/windows.h take (port/win32.cpp keeps one handle per descriptor).

#include <cstdio>

#include "platform/posix/win32_types.h"

HANDLE port_osfhandle_of(int fd);

inline std::intptr_t _get_osfhandle(int fd) { return reinterpret_cast<std::intptr_t>(port_osfhandle_of(fd)); }
inline int _fileno(std::FILE* file) { return fileno(file); }
