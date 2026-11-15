// NOT ORIGINAL - part of the macOS (arm64) port's Win32 shim (docs/design/macos-port.md). This header is NOT Windows': it gives
// the names of `mmsystem.h` to a build that has no such header, so that the engine and the game compile as they are read on
// Windows. It is only on the include path of a portable build (CMakeLists.txt, thug2_options: -I port/win32_shim); a
// Windows build never sees it.
#ifdef _WIN32
#error "platform/posix/mmsystem.h is the macOS port's stand-in - a Windows build must use the system's real header"
#endif

#pragma once

// NOT ORIGINAL - the macOS port's stand-in for <mmsystem.h>: what the PC rendering layer asks of it (the millisecond
// clock the original timed its loading screens with).

#include "platform/posix/windows.h"

using MMRESULT = UINT;

// The multimedia timer (the loading screen's own tick, on a thread of its own, as Win32 runs it).
#define TIME_ONESHOT 0x0000U
#define TIME_PERIODIC 0x0001U
#define TIME_CALLBACK_FUNCTION 0x0000U
using TIMECALLBACK = void(WIN32_STDCALL*)(UINT timer, UINT message, DWORD_PTR user, DWORD_PTR first, DWORD_PTR second);
using LPTIMECALLBACK = TIMECALLBACK;

MMRESULT timeSetEvent(UINT delay, UINT resolution, LPTIMECALLBACK callback, DWORD_PTR user, UINT event);
MMRESULT timeKillEvent(UINT timer);
