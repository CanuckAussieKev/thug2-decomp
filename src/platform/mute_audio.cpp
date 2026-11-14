// NOT ORIGINAL: [features] mute_audio - see mute_audio.h.

#include "platform/mute_audio.h"

#include <windows.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>

#include <string>

#include "platform/run_log.h"
#include "base/util/com_ptr.h"
#include "base/core/raw_view.h"

namespace sound {

namespace {

// A file named kAlwaysMutedMarker beside the exe: muted whatever the ini says - a test workspace's (tools/game marks
// them), so a test's own ini can't make one heard.
constexpr const wchar_t* kAlwaysMutedMarker = L"thug2-decomp-always-muted";

bool marked_always_muted()
{
    std::wstring path(MAX_PATH, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) {
        return false;
    }
    path.resize(length);
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return false;
    }
    path.resize(slash + 1);
    path += kAlwaysMutedMarker;
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

} // namespace

bool& audio_muted()
{
    static bool muted = marked_always_muted() || config_int("features", "mute_audio", 0) != 0;
    return muted;
}

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
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                       raw::as<void*>(devices.put()))) &&
            SUCCEEDED(devices->GetDefaultAudioEndpoint(eRender, eConsole, device.put())) &&
            SUCCEEDED(device->Activate(__uuidof(IAudioSessionManager), CLSCTX_ALL, nullptr,
                                       raw::as<void*>(sessions.put()))) &&
            SUCCEEDED(sessions->GetSimpleAudioVolume(nullptr, FALSE, volume.put()))) {
            ok = use(volume.get());
        }
    }
    if (SUCCEEDED(init)) {
        CoUninitialize();
    }
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
    return ok && (muted != FALSE);
}

} // namespace sound
