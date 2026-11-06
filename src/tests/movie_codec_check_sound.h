#pragma once

// NOT ORIGINAL - the sound outputs thug2_movie_codec_check plays both codecs' sound into (movie_codec_check_sound.cpp):
// for Bink, a stand-in for the game's DirectSound object handed to binkw32 (BinkSetSoundSystem); for the FFmpeg codec,
// a stand-in audio::Device. Neither makes a sound: each buffer is played by the clock - its play position moves at its
// frequency from Play on, as a sound card's would - and what passes the play position is kept ("heard"), with what was
// done to the buffer (its format and flags, the volumes, pans and frequencies set, the plays and stops). Both codecs'
// sound goes through the same buffer model, so what each one hands its output can be compared sample by sample.

#include <cstdint>
#include <string>
#include <vector>

#include "engine/audio/audio_device.h"
#include "engine/audio/audio_handles.h"

namespace tests::sound {

// One buffer an output made, as far as it was played.
struct BufferRecord {
    uint32_t flags = 0;   // DSBCAPS_*
    uint32_t bytes = 0;   // the buffer's size
    uint16_t channels = 0;
    uint32_t rate = 0;    // samples a second (the format's)
    uint16_t bits = 0;
    bool primary = false; // (DirectSound's primary buffer: only binkw32 could ask for it)
    std::vector<int> volumes;          // each SetVolume (hundredths of a decibel)
    std::vector<int> pans;             // each SetPan
    std::vector<uint32_t> frequencies; // each SetFrequency
    std::vector<int16_t> heard;        // the samples that passed the play position (interleaved)
    double first_play_ms = -1.0;       // since the record was started (-1: never played)
    int plays = 0;
    int stops = 0;
    uint64_t written_bytes = 0;        // through Lock / Unlock or write_buffer
    double lead_max_ms = 0.0;          // how far past the play position the writes reached: the most, and the sum
    double lead_sum_ms = 0.0;
    uint32_t leads = 0;
    std::string other_calls;           // the methods called that the model doesn't play (named once each)
    std::string timeline;              // each play and stop: when (since the record was started), how much was heard
};

// The stand-in DirectSound object for binkw32 (its handle, as the game passes the real one).
audio::SoundOutput* bink_output();
// The stand-in audio::Device and the output handle it is opened with.
audio::Device& recording_device();
audio::SoundOutput* recording_output();

// The listener started (a thread moving every playing buffer's samples into its record every 2 ms).
void start_listening();
// The records begun again: the buffers made from now on are recorded, the times counted from now.
void begin_records();
// The last volume and pan set on the last secondary buffer made (cheaper than its whole record).
struct Settings {
    int volume = 0;
    int pan = 0;
};
Settings last_settings();
// The buffers made since begin_records, in the order made (the buffers still open listened to once more first).
std::vector<BufferRecord> records();

} // namespace tests::sound
