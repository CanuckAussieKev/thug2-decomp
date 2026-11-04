#pragma once

// NOT ORIGINAL - the sound effects' state shared by sfx.cpp and sfx_voices.cpp (the original's globals; the header is
// the sound subsystem's own: other subsystems use sfx.h's functions).

#include <array>

#include "engine/memory/mem.h"
#include "engine/sound/sfx.h"

namespace sound {
constexpr float kDefaultDropoff = 1020.0F; // the range a sound is heard from when its script gives none

// The sound effects' shared state: the loaded waves (permanent 0x006F4268, level 0x006F5E68), the sounds playing
// (0x006F7A68), the positional sounds' list, the manager. Made on first use, never destroyed (sound_state()).
class SoundState {
public:
    float& default_dropoff() { return default_dropoff_; }
    [[nodiscard]] float default_dropoff() const { return default_dropoff_; }
    std::array<SoundWaveSlot, kWaveSlotCount>& permanent_waves() { return permanent_waves_; }
    [[nodiscard]] const std::array<SoundWaveSlot, kWaveSlotCount>& permanent_waves() const { return permanent_waves_; }
    std::array<SoundWaveSlot, kWaveSlotCount>& level_waves() { return level_waves_; }
    [[nodiscard]] const std::array<SoundWaveSlot, kWaveSlotCount>& level_waves() const { return level_waves_; }
    std::array<PlayingSound, kPlayingSoundCount>& playing_sounds() { return playing_sounds_; }
    [[nodiscard]] const std::array<PlayingSound, kPlayingSoundCount>& playing_sounds() const { return playing_sounds_; }
    memory::HeapUnique<AudioMixer>& audio_mixer() { return audio_mixer_; }
    [[nodiscard]] const memory::HeapUnique<AudioMixer>& audio_mixer() const { return audio_mixer_; }
    PositionalSound*& positional_head() { return positional_head_; }
    [[nodiscard]] PositionalSound* positional_head() const { return positional_head_; }
    int& permanent_count() { return permanent_count_; }
    [[nodiscard]] int permanent_count() const { return permanent_count_; }
    int& level_count() { return level_count_; }
    [[nodiscard]] int level_count() const { return level_count_; }
    int& positional_count() { return positional_count_; }
    [[nodiscard]] int positional_count() const { return positional_count_; }

private:
    float default_dropoff_{kDefaultDropoff};
    std::array<SoundWaveSlot, kWaveSlotCount> permanent_waves_{};
    std::array<SoundWaveSlot, kWaveSlotCount> level_waves_{};
    std::array<PlayingSound, kPlayingSoundCount> playing_sounds_{};
    memory::HeapUnique<AudioMixer> audio_mixer_;
    PositionalSound* positional_head_{};
    int permanent_count_{};
    int level_count_{};
    int positional_count_{};
};
SoundState& sound_state();

} // namespace sound
