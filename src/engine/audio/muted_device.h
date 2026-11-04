#pragma once

// NOT ORIGINAL: thug2-decomp.ini [features] mute_audio=1 (platform/mute_audio.h) as a sound output - the null output
// of audio::Device. app/ installs it in place of the real output when the switch is on (the workspaces' test games).
//
// It isn't a device that does nothing: with mute_audio the game must run exactly as with sound - every voice made,
// filled, played and stopped, its status read back (the voices' "still playing" and the sounds' ends that scripts wait
// for), the listener placed, the movies and streams played by Bink through the game's DirectSound object (Bink
// without it would open an output of its own). So it stands over the real output and forwards every call to it but
// the three that could be heard - today's mute paths, unchanged:
//  - open / close: the output opened, then the process's audio session muted; closed, then unmuted (the session
//    mute that used to follow DirectSoundCreate8 / Release in the platform layer);
//  - set_buffer_volume: every voice's volume silent, kSilentAttenuation (what output_attenuation did in the voices'
//    two SetVolume calls);
//  - track_volume: every movie's and stream's Bink track at 0 (what output_bink_volume did in the two
//    video_set_loudness).
// The game's own volume settings, and what the save stores, are left alone.

#include "engine/audio/audio_device.h"

namespace audio {

// The muted output over `output` (made on first use, over the output given then; never destroyed).
Device& muted_device(Device& output);

} // namespace audio
