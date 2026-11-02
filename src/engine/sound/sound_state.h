#pragma once

// NOT ORIGINAL - the sound effects' state shared by sfx.cpp and sfx_voices.cpp (the original's globals; the header is
// the sound subsystem's own: other subsystems use sfx.h's functions).

#include <array>

#include "engine/core/mem.h"
#include "engine/sound/sfx.h"

namespace sound {

// The sound effects' shared state: the loaded waves (permanent 0x006F4268, level 0x006F5E68), the sounds playing
// (0x006F7A68), the positional sounds' list, the manager. Made on first use, never destroyed (sound_state()).
struct SoundState {
    float default_dropoff_{1020.0f};
    std::array<SoundWaveSlot, 256> permanent_waves_{};
    std::array<SoundWaveSlot, 256> level_waves_{};
    std::array<PlayingSound, 48> playing_sounds_{};
    memory::HeapUnique<AudioMixer> audio_mixer_{};
    PositionalSound* positional_head_{};
    int permanent_count_{};
    int level_count_{};
    int positional_count_{};
};
SoundState& sound_state();

} // namespace sound
