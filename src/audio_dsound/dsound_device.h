#pragma once

#include "engine/audio/audio_device.h"

// NOT ORIGINAL - the DirectSound backend's audio::Device (dsound_device.cpp): each method is the DirectSound calls the
// original made at that place. app/ installs it at start-up.
namespace audio::dsound {

// The DirectSound device (made on first use, never destroyed).
Device& device();

} // namespace audio::dsound
