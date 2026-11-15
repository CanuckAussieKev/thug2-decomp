#pragma once

// NOT ORIGINAL - the macOS (arm64) port's sound output: a silent audio::Device (engine/audio/audio_device.h). It stands
// in for src/audio_dsound's DirectSound device until an SDL3 one is written (port/README.md).
namespace audio {
class Device;
} // namespace audio

namespace audio {

Device& silent_device();

} // namespace audio
