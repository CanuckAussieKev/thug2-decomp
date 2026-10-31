// NOT ORIGINAL - the part of crash handling that belongs to hooking: the original exe's own C runtime (its /GS handler
// and its message box) pointed at the reports of game/sys/crash.h. The standalone build has its own runtime and none
// of this (crash.cpp's stubs).

#include "game/sys/crash.h"

#include <windows.h>

#include <intrin.h>

#include "game/sys/log.h"
#include "hook/game.h"

namespace {

// The game's CRT (VC7.1, linked into the exe): its __security_error_handler's user handler (set by
// _set_security_error_handler; called as handler(code, data) before the "Buffer overrun detected!" box), and the
// MessageBoxA its __crtMessageBoxA looked up and cached (taken as is when set: the box is then ours).
constexpr uintptr_t kGameSecurityUserHandler = 0x007DB104;
constexpr uintptr_t kGameCachedMessageBox = 0x007DB55C;
constexpr uintptr_t kGameSecurityErrorHandler = 0x006287DD; // __security_error_handler(int code, void* data)
constexpr uintptr_t kGameCrtMessageBox = 0x00634205;        // __crtMessageBoxA(text, caption, type)

// The game's /GS check failed (its __security_error_handler calls this before its "Buffer overrun detected!" box).
void __cdecl GameSecurityError(int code, void*)
{
    Log("BUFFER OVERRUN DETECTED by the game's own /GS check (security error %d%s): one of THUG2's functions found its "
        "stack cookie overwritten as it returned - its caller is among the first addresses below",
        code, code == 1 ? ": a buffer overrun" : "");
    CrashLogStackScan(reinterpret_cast<uintptr_t>(_AddressOfReturnAddress()));
    CrashEndProcess(0xC0000409);
}

// The game's CRT's message box (runtime errors R60xx, and the /GS box if the handler above weren't set): logged, the
// process ends (the CRT would end it after the box anyway).
int WINAPI GameCrtMessageBox(HWND, const char* text, const char* caption, UINT)
{
    char flat[1024];
    size_t n = 0;
    for (const char* p = text ? text : ""; *p && n + 4 < sizeof(flat); ++p) {
        if (*p == '\r')
            continue;
        if (*p == '\n') {
            if (n && flat[n - 1] != ' ')
                flat[n++] = ' ';
            continue;
        }
        flat[n++] = *p;
    }
    flat[n] = '\0';
    Log("GAME CRT MESSAGE: %s: %s", caption ? caption : "", flat);
    CrashLogStackScan(reinterpret_cast<uintptr_t>(_AddressOfReturnAddress()));
    CrashEndProcess(3);
}

} // namespace

void InstallGameCrtHandlers()
{
    GameVariable<void(__cdecl*)(int, void*)>(kGameSecurityUserHandler) = GameSecurityError;
    GameVariable<decltype(&GameCrtMessageBox)>(kGameCachedMessageBox) = GameCrtMessageBox;
}

bool CauseGameCrtFault(TestFault fault)
{
    switch (fault) {
    case TestFault::GameOverrun:
        GameFunction<void(__cdecl*)(int, void*)>(kGameSecurityErrorHandler)(1, nullptr);
        return true;
    case TestFault::GameCrtMessage:
        GameFunction<int(__cdecl*)(const char*, const char*, unsigned)>(kGameCrtMessageBox)(
            "crash test:\n\na message from the game's C runtime", "Microsoft Visual C++ Runtime Library", 0x12010);
        return true;
    default:
        return false;
    }
}
