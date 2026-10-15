// dinput8.dll proxy: Windows loads this from the game folder instead of the system DLL, which gets our
// code into the game process before it starts. We forward the one function the game imports to the
// real dinput8.dll in System32, and install our function replacements on load.
#include <windows.h>
#include <unknwn.h>

#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace {

using DirectInput8CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

DirectInput8CreateFn LoadRealDirectInput8Create()
{
    char path[MAX_PATH];
    GetSystemDirectoryA(path, MAX_PATH);
    lstrcatA(path, "\\dinput8.dll");
    HMODULE real = LoadLibraryA(path);
    if (!real) {
        Log("Could not load the real dinput8.dll from %s", path);
        return nullptr;
    }
    return reinterpret_cast<DirectInput8CreateFn>(GetProcAddress(real, "DirectInput8Create"));
}

} // namespace

// Loaded lazily: LoadLibrary isn't allowed inside DllMain. (PartyMod replaces DirectInput with SDL2 and
// skips this call entirely, but the game still imports it, so it must exist.)
extern "C" HRESULT WINAPI Proxy_DirectInput8Create(HINSTANCE inst, DWORD version, REFIID iid, LPVOID* out,
                                                   LPUNKNOWN outer)
{
    static DirectInput8CreateFn real = LoadRealDirectInput8Create();
    if (!real)
        return E_FAIL;
    return real(inst, version, iid, out, outer);
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        LogOpen();
        Log("thug2-decomp loaded");
        // Self-tests call the original functions, so they must run before those functions are replaced.
        RunSelfTests();
        InstallReplacements();
    }
    return TRUE;
}
