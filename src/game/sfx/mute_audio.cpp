// NOT ORIGINAL: [features] mute_audio - see mute_audio.h.

#include "game/sfx/mute_audio.h"

#include <windows.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>

#include "hook/log.h"

namespace Sfx {

bool& AudioMuted()
{
    static bool muted = ConfigInt("features", "mute_audio", 0) != 0;
    return muted;
}

int OutputAttenuation(int attenuation) { return AudioMuted() ? -10000 : attenuation; }

int OutputBinkVolume(int volume) { return AudioMuted() ? 0 : volume; }

namespace {

// Calls `use` with the default playback device's GUID_NULL session volume of this process (COM set up for the call
// if the thread hadn't). False if any step fails.
template <typename F>
bool WithSessionVolume(F use)
{
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool ok = false;
    IMMDeviceEnumerator* devices = nullptr;
    IMMDevice* device = nullptr;
    IAudioSessionManager* sessions = nullptr;
    ISimpleAudioVolume* volume = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                   reinterpret_cast<void**>(&devices))) &&
        SUCCEEDED(devices->GetDefaultAudioEndpoint(eRender, eConsole, &device)) &&
        SUCCEEDED(device->Activate(__uuidof(IAudioSessionManager), CLSCTX_ALL, nullptr,
                                   reinterpret_cast<void**>(&sessions))) &&
        SUCCEEDED(sessions->GetSimpleAudioVolume(nullptr, FALSE, &volume)))
        ok = use(volume);
    if (volume)
        volume->Release();
    if (sessions)
        sessions->Release();
    if (device)
        device->Release();
    if (devices)
        devices->Release();
    if (SUCCEEDED(init))
        CoUninitialize();
    return ok;
}

} // namespace

bool SetAudioSessionMuted(bool muted)
{
    bool ok = WithSessionVolume([muted](ISimpleAudioVolume* volume) {
        return SUCCEEDED(volume->SetMute(muted ? TRUE : FALSE, nullptr));
    });
    Log("mute_audio: audio session %s%s (thug2-decomp.ini [features] mute_audio)", muted ? "muted" : "unmuted",
        ok ? "" : " - FAILED (no playback device?); the game's volume paths are silenced anyway");
    return ok;
}

bool AudioSessionMuted()
{
    BOOL muted = FALSE;
    bool ok = WithSessionVolume([&muted](ISimpleAudioVolume* volume) { return SUCCEEDED(volume->GetMute(&muted)); });
    return ok && muted;
}

} // namespace Sfx
