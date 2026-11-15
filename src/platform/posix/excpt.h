// NOT ORIGINAL - part of the macOS (arm64) port's Win32 shim (docs/design/macos-port.md). This header is NOT Windows': it gives
// the names of `excpt.h` to a build that has no such header, so that the engine and the game compile as they are read on
// Windows. It is only on the include path of a portable build (CMakeLists.txt, thug2_options: -I port/win32_shim); a
// Windows build never sees it.
#ifdef _WIN32
#error "platform/posix/excpt.h is the macOS port's stand-in - a Windows build must use the system's real header"
#endif

#pragma once

// NOT ORIGINAL - the macOS port's stand-in for MSVC's <excpt.h> (structured exception handling's declarations). The
// portable build has no SEH: the files that used it are left out of it (port/platform_stubs.cpp).
