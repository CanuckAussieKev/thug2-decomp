#pragma once

// NOT ORIGINAL - the macOS (arm64) port's sound output: audio::Device (engine/audio/audio_device.h) over SDL3's audio.
// It stands in for src/audio_dsound's DirectSound device; the voices are mixed in SDL's callback (port/sdl_audio_device.cpp).
namespace audio {
class Device;
} // namespace audio

namespace audio {

Device& sdl_device();

} // namespace audio
