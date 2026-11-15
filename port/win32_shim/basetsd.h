// NOT ORIGINAL - part of the macOS (arm64) port's Win32 shim (port/README.md). This header is NOT Windows': it gives
// the names of `basetsd.h` to a build that has no such header, so that the engine and the game compile as they are read on
// Windows. It is only on the include path of a portable build (CMakeLists.txt, thug2_options: -I port/win32_shim); a
// Windows build never sees it.
#ifdef _WIN32
#error "port/win32_shim/basetsd.h is the macOS port's stand-in - a Windows build must use the system's real header"
#endif

#pragma once

// NOT ORIGINAL - the macOS port's stand-in for <basetsd.h> (Win32's pointer-sized integer types, all of which
// port/win32_types.h has).

#include "port/win32_types.h"
