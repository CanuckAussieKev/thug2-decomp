#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

#include "engine/audio/music_api.h"
#include "base/core/checksums.h"

namespace objects {
class AudioStreamModule;
} // namespace objects

// Streams: sounds streamed from data\streams\pcm\pcm.wad (speech, effects) on the three stream channels; the
// records of what each plays are audio::Stream (music_player.h). Each command does nothing (returns 0 / false) while
// streamed sound is off: no sound, the null sound platform (0x007CEFA8 == 2), or the stream header not loaded.
namespace audio {

// A stream's frame amplitudes (data\fam\<checksum>.fam: one byte a frame, for lip sync - music_api.h's FamData),
// loaded by LoadStreamFrameAmp. One slot (at 0x0069BAC8); a checksum of 0 marks it free.
using StreamAmpCurve = FamData;

// 0x00451010: pause (1) or unpause (0) the streams; -1 unpauses only if they weren't paused when last paused
// with 1. (As PauseMusic, for the streams.)
void freeze_stream(int pause);
// 0x00451620: load the streams' header (PC: nothing to load) - streamed sound is on while it succeeds.
bool load_stream_header(const char* name);
// 0x00451660: whether a stream with this id (or else this name) is playing.
bool is_stream_playing(uint32_t id);

// 0x004516A0: load the frame amplitudes of stream `checksum` into the free slot; the slot, or null (none free,
// no file, or more than 0x1000 bytes).
StreamAmpCurve* load_stream_frame_amp(uint32_t checksum);
// 0x004516E0: the slot holding stream `checksum`'s frame amplitudes (with 0: a free slot), or null.
StreamAmpCurve* fetch_stream_amp_curve(uint32_t checksum);
// 0x00451720: free the slot of stream `checksum`; false if none (or checksum 0).
bool free_stream_frame_amp(uint32_t checksum);

// How a stream starts: its option, its priority and its name.
struct StreamStart {
    int option{};
    int priority{};
    uint32_t name{};
};

// 0x00451770: start stream `checksum` on a free channel or else on the one playing the lowest priority (the
// oldest of those) if `priority` is at least that - stopped first; preloaded only, or played at `volume`
// (an sound::VolumeRef) and `pitch`. Records it (under `name`, with a new id from `name` up); the channel, or -1.
int start_stream(uint32_t checksum, const void* volume, float pitch, StreamStart stream_start,
                 bool preload);
// 0x00451960: the PlayStream command: play stream `checksum` (volume 100, dropoff 1020, one name) under `id` (0:
// the checksum); the id it gets, 0 if it doesn't play. Not while the streams are paused or blocked (0x0069BAAA).
// `option` is the script's integer 0xE46111E0 (default 0), passed on to the platform. (Callers push a seventh
// argument it ignores.)
uint32_t play_stream(uint32_t checksum, const void* volume, float pitch, int option, int priority, uint32_t id);
// 0x00451A50: preload stream `checksum` (silent, under its own checksum); its id, or 0.
uint32_t prime_stream(uint32_t checksum, int priority);
// A stream and the name it is known by.
struct StreamLabel {
    uint32_t stream{};
    uint32_t name{};
};

// 0x00451B80: the AddStream command: give stream `stream` (the one `owner` played, or for no owner the one playing
// with that id or name) another name, with that name's frame amplitudes. False if it isn't playing.
// (Original quirk: no limit on the 8 names a stream has room for.)
bool add_stream(StreamLabel stream_label, void* owner);
// How a stream plays at an object: its dropoff, volume and pitch, option and priority, positional info and falloff curve.
struct StreamPlay {
    float dropoff{};
    float volume{};
    float pitch{};
    int option{};
    int priority{};
    int use_pos_info_{};
    int falloff_curve_{};
};

// 0x00451E20 (Ghidra splits it at 0x00451E7D): play stream `checksum` from an object's stream component (an
// GameObjects::AudioStreamModule: its object at +0xC, the ids it plays at +0x18): with `usePosInfo`, at the volume the
// sound manager gives the nearest emitter's position (`dropoff`, `falloffCurve`) - unless it's under 2 and
// the manager says it isn't audible; else at `volume`. The component becomes the stream's owner (the one before
// lets go). A volume other than 100 scales the stream's and sets its dropoff. The id, or 0.
uint32_t play_stream_at_object(objects::AudioStreamModule* component, checksums::Name stream_name, const StreamPlay& stream_play, uint32_t id);

// The PC's streams are Bink files (sound only) opened from memory.

// Where a stream lies in its file: its offset and its size.
struct FileSpan {
    long offset{};
    size_t size{};
};

// 0x00452A90: stream file `key` in memory - read now (`size` bytes at `offset` in `path`, with the game's C
// runtime) into a free slot (the last one) or else the one unused longest, unless it's there already; the slot
// (12, at 0x0069CAD8; its users counted), or -1 (no slot, no file).
int load_stream_file(uint32_t key, const char* path, FileSpan file_span);

} // namespace audio
