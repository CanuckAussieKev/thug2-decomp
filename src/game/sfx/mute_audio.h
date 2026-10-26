#pragma once

// NOT ORIGINAL: thug2-decomp.ini [features] mute_audio=1 (default 0: unchanged) silences all of the game's sound
// from the first frame - for test games in the agents' workspaces (tools/pipeline/game/game_lock.ps1 and the launchers set
// it there), never the user's own install.
//
// Two layers, so nothing leaks even through code that isn't ours yet (or a [replacements] disable):
//  - the process's Windows audio session (the default endpoint's GUID_NULL session, the one DirectSound and Bink's
//    DirectSound output play into) is muted right after DirectSound is made (Platform::InitDirectSound) and
//    unmuted when it's released (Windows keeps a session's mute for the exe's next run otherwise);
//  - the volume paths in our source: a voice's DirectSound attenuation (Sfx SetVoiceParameters) is -100 dB and
//    every BinkSetVolume (music / ambient streams, voice, movies) is 0. The game's own volume settings (and what
//    the save stores) are left alone.

namespace Sfx {

// The switch (read from the ini once; self-tests change it for their run).
bool& AudioMuted();

// What reaches DirectSound / Bink: the level as computed, or silence when muted (-10000 = DSBVOLUME_MIN; Bink 0).
int OutputAttenuation(int attenuation);
int OutputBinkVolume(int volume);

// Mutes (or unmutes) this process's audio session on the default playback device. False if there's no device
// or Core Audio refused.
bool SetAudioSessionMuted(bool muted);
// The session's mute as Windows has it (false without a device).
bool AudioSessionMuted();

} // namespace Sfx
