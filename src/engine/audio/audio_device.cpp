// NOT ORIGINAL - the installed sound output (audio_device.h).

#include "engine/audio/audio_device.h"

namespace audio {

namespace {

// The device app/ installed (a constant-initialised pointer: no guard, nothing to destroy).
Device*& installed()
{
    static Device* device = nullptr;
    return device;
}

} // namespace

Device& device() { return *installed(); }

void install_device(Device& device) { installed() = &device; }

} // namespace audio
