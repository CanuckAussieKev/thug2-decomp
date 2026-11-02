#pragma once

// NOT ORIGINAL - the audio subsystem's state shared between its files (the original's globals): the music player's,
// the music commands', pcm.wad's and the stream files'. The header is the subsystem's own: other subsystems use
// music_player.h's, music_api.h's and stream.h's functions.

#include <array>
#include <cstdint>

#include <windows.h>

#include "engine/audio/music_player.h"
#include "engine/audio/stream.h"
#include "engine/audio/stream_files.h"

namespace audio {

// NOT ORIGINAL grouping - the music's frame count, track and playlist playing, the user soundtrack's track and
// position. Made on first use, never destroyed (music_api_state()).
struct MusicApiState {
    int frame_count_{1};
    int current_track_index_{};
    int current_playlist_{};
    bool initialised_{};
    bool custom_playlist_on_{};
    int user_soundtrack_{};
    uint32_t user_track_{};
    uint32_t user_position_{};
};
MusicApiState& music_api_state();

// NOT ORIGINAL grouping - the music player's switches and volumes, the DirectSound device, the playlists and their
// titles, the three streams and their owners, the user soundtrack's order. Made on first use, never destroyed
// (music_state()).
struct MusicState {
    bool music_header_loaded_{true};
    bool streams_on_{true};
    float music_volume_{50.0f};
    float music_stream_volume_{100.0f};
    int bgm_stream_kind_{};
    bool shuffle_user_{};
    void* sound_device_{};
    std::array<Playlist, 2> playlists_{}; // (the music's two playlists; 0 is the game's, 1 the script's)
    std::array<std::array<char, 100>, 80> track_titles_{};
    std::array<Stream, 3> streams_{};
    std::array<objects::AudioStreamModule*, 3> stream_owners_{};
    std::array<int, 600> user_order_{};
};
MusicState& music_state();

// NOT ORIGINAL grouping - pcm.wad (opened for streaming), pcm.dat's index of it and the streams' lock (a mutex made at
// start-up). Made on first use, never destroyed (wad_state()).
struct WadState {
    void* wad_file_{INVALID_HANDLE_VALUE};
    WadEntry* wad_entries_{};
    void* stream_lock_{}; // a mutex (HANDLE), made at start-up
};
WadState& wad_state();

// NOT ORIGINAL grouping - the stream files read into memory (0x0069CAD8), their use count and whether Bink's sound is
// set. Made on first use, never destroyed (stream_file_state()).
struct StreamFileState {
    bool bink_sound_set_{};
    uint32_t stream_file_uses_{};
    std::array<CachedStreamFile, 12> stream_files_{};
};
StreamFileState& stream_file_state();

} // namespace audio
