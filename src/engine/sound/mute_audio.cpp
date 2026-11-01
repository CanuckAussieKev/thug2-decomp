// NOT ORIGINAL: [features] mute_audio - see mute_audio.h.

#include "engine/sound/mute_audio.h"

#include <windows.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>

#include "platform/run_log.h"
#include "engine/util/com_ptr.h"

namespace sound {

bool& audio_muted()
{
    static bool muted = config_int("features", "mute_audio", 0) != 0;
    return muted;
}

int output_attenuation(int attenuation) { return audio_muted() ? -10000 : attenuation; }

int output_bink_volume(int volume) { return audio_muted() ? 0 : volume; }

namespace {

// Calls `use` with the default playback device's GUID_NULL session volume of this process (COM set up for the call
// if the thread hadn't). False if any step fails.
template <typename F>
bool with_session_volume(F use)
{
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool ok = false;
    {
        // (released in the reverse order, before COM is shut down)
        util::ComPtr<IMMDeviceEnumerator> devices;
        util::ComPtr<IMMDevice> device;
        util::ComPtr<IAudioSessionManager> sessions;
        util::ComPtr<ISimpleAudioVolume> volume;
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                       __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(devices.put()))) &&
            SUCCEEDED(devices->GetDefaultAudioEndpoint(eRender, eConsole, device.put())) &&
            SUCCEEDED(device->Activate(__uuidof(IAudioSessionManager), CLSCTX_ALL, nullptr,
                                       reinterpret_cast<void**>(sessions.put()))) &&
            SUCCEEDED(sessions->GetSimpleAudioVolume(nullptr, FALSE, volume.put())))
            ok = use(volume.get());
    }
    if (SUCCEEDED(init))
        CoUninitialize();
    return ok;
}

} // namespace

bool set_audio_session_muted(bool muted)
{
    const bool ok = with_session_volume(
        [muted](ISimpleAudioVolume* volume) { return SUCCEEDED(volume->SetMute(muted ? TRUE : FALSE, nullptr)); });
    get_log("mute_audio: audio session %s%s (thug2-decomp.ini [features] mute_audio)", muted ? "muted" : "unmuted",
            ok ? "" : " - FAILED (no playback device?); the game's volume paths are silenced anyway");
    return ok;
}

bool audio_session_muted()
{
    BOOL muted = FALSE;
    const bool ok =
        with_session_volume([&muted](ISimpleAudioVolume* volume) { return SUCCEEDED(volume->GetMute(&muted)); });
    return ok && muted;
}

} // namespace sound
