#pragma once

// NOT ORIGINAL: thug2-decomp.ini [features] mute_audio=1 (default 0: unchanged) silences all of the game's sound
// from the first frame - for test games in the agents' workspaces (tools/game/game_lock.ps1 and the launchers set
// it there), never the user's own install.
//
// app/ installs the muted sound output then (engine/audio/muted_device.h): the process's Windows audio session (the
// default endpoint's GUID_NULL session, the one DirectSound and Bink's DirectSound output play into) muted right
// after the output is made and unmuted when it's released (Windows keeps a session's mute for the exe's next run
// otherwise), every voice's volume silent and every movie's and stream's track at 0. The game's own volume settings
// (and what the save stores) are left alone.

namespace sound {

// The switch (read from the ini once).
bool& audio_muted();

// Mutes (or unmutes) this process's audio session on the default playback device. False if there's no device
// or Core Audio refused.
bool set_audio_session_muted(bool muted);
// The session's mute as Windows has it (false without a device).
bool audio_session_muted();

} // namespace sound
