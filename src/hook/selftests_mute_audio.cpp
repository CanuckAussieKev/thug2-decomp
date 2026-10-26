// Self-tests for [features] mute_audio (src/game/sfx/mute_audio.h; not original): with the switch on, the voices'
// DirectSound attenuation is -100 dB and every Bink volume 0; off, the levels pass unchanged (the voice and Bink
// functions themselves are compared with the originals, switch off, by their own tests). The audio session's mute
// is set and read back through Core Audio, and put back as it was (passes without a playback device).

#include "game/sfx/mute_audio.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace {

void TestMuteAudioLevels()
{
    using namespace Sfx;
    bool& muted = AudioMuted();
    const bool was = muted;
    muted = false;
    Check(OutputAttenuation(0) == 0 && OutputAttenuation(-2500) == -2500 && OutputAttenuation(-10000) == -10000 &&
              OutputBinkVolume(32768) == 32768 && OutputBinkVolume(0) == 0,
          "mute_audio off: levels unchanged");
    muted = true;
    Check(OutputAttenuation(0) == -10000 && OutputAttenuation(-2500) == -10000 && OutputBinkVolume(32768) == 0 &&
              OutputBinkVolume(16384) == 0 && OutputBinkVolume(0) == 0,
          "mute_audio on: silence");
    muted = was;
}

void TestMuteAudioSession()
{
    using namespace Sfx;
    const bool was = AudioSessionMuted();
    if (!SetAudioSessionMuted(true)) {
        Log("mute_audio: no playback device / Core Audio refused - session test skipped");
        return;
    }
    Check(AudioSessionMuted(), "mute_audio: session muted");
    Check(SetAudioSessionMuted(false) && !AudioSessionMuted(), "mute_audio: session unmuted");
    SetAudioSessionMuted(was);
}

} // namespace

SELFTEST(Early, TestMuteAudioLevels);
SELFTEST(Early, TestMuteAudioSession);
