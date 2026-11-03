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
class MusicApiState {
public:
    int& frame_count() { return frame_count_; }
    [[nodiscard]] int frame_count() const { return frame_count_; }
    int& current_track_index() { return current_track_index_; }
    [[nodiscard]] int current_track_index() const { return current_track_index_; }
    int& current_playlist() { return current_playlist_; }
    [[nodiscard]] int current_playlist() const { return current_playlist_; }
    bool& initialised() { return initialised_; }
    [[nodiscard]] bool initialised() const { return initialised_; }
    bool& custom_playlist_on() { return custom_playlist_on_; }
    [[nodiscard]] bool custom_playlist_on() const { return custom_playlist_on_; }
    int& user_soundtrack() { return user_soundtrack_; }
    [[nodiscard]] int user_soundtrack() const { return user_soundtrack_; }
    uint32_t& user_track() { return user_track_; }
    [[nodiscard]] uint32_t user_track() const { return user_track_; }
    uint32_t& user_position() { return user_position_; }
    [[nodiscard]] uint32_t user_position() const { return user_position_; }

private:
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
class MusicState {
public:
    bool& music_header_loaded() { return music_header_loaded_; }
    [[nodiscard]] bool music_header_loaded() const { return music_header_loaded_; }
    bool& streams_on() { return streams_on_; }
    [[nodiscard]] bool streams_on() const { return streams_on_; }
    float& music_volume() { return music_volume_; }
    [[nodiscard]] float music_volume() const { return music_volume_; }
    float& music_stream_volume() { return music_stream_volume_; }
    [[nodiscard]] float music_stream_volume() const { return music_stream_volume_; }
    int& bgm_stream_kind() { return bgm_stream_kind_; }
    [[nodiscard]] int bgm_stream_kind() const { return bgm_stream_kind_; }
    void set_bgm_stream_kind(int value) { bgm_stream_kind_ = value; }
    bool& shuffle_user() { return shuffle_user_; }
    [[nodiscard]] bool shuffle_user() const { return shuffle_user_; }
    [[nodiscard]] void* sound_device() const { return sound_device_; }
    void set_sound_device(void* value) { sound_device_ = value; }
    std::array<Playlist, 2>& playlists() { return playlists_; }
    [[nodiscard]] const std::array<Playlist, 2>& playlists() const { return playlists_; }
    std::array<std::array<char, 100>, 80>& track_titles() { return track_titles_; }
    [[nodiscard]] const std::array<std::array<char, 100>, 80>& track_titles() const { return track_titles_; }
    std::array<Stream, 3>& streams() { return streams_; }
    [[nodiscard]] const std::array<Stream, 3>& streams() const { return streams_; }
    std::array<objects::AudioStreamModule*, 3>& stream_owners() { return stream_owners_; }
    [[nodiscard]] const std::array<objects::AudioStreamModule*, 3>& stream_owners() const { return stream_owners_; }
    std::array<int, 600>& user_order() { return user_order_; }
    [[nodiscard]] const std::array<int, 600>& user_order() const { return user_order_; }

private:
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
class WadState {
public:
    void*& wad_file() { return wad_file_; }
    [[nodiscard]] void* wad_file() const { return wad_file_; }
    WadEntry*& wad_entries() { return wad_entries_; }
    [[nodiscard]] WadEntry* wad_entries() const { return wad_entries_; }
    [[nodiscard]] void* stream_lock() const { return stream_lock_; }
    void set_stream_lock(void* value) { stream_lock_ = value; }

private:
    void* wad_file_{INVALID_HANDLE_VALUE};
    WadEntry* wad_entries_{};
    void* stream_lock_{}; // a mutex (HANDLE), made at start-up
};
WadState& wad_state();

// NOT ORIGINAL grouping - the stream files read into memory (0x0069CAD8), their use count and whether Bink's sound is
// set. Made on first use, never destroyed (stream_file_state()).
class StreamFileState {
public:
    bool& bink_sound_set() { return bink_sound_set_; }
    [[nodiscard]] bool bink_sound_set() const { return bink_sound_set_; }
    uint32_t& stream_file_uses() { return stream_file_uses_; }
    [[nodiscard]] uint32_t stream_file_uses() const { return stream_file_uses_; }
    std::array<CachedStreamFile, 12>& stream_files() { return stream_files_; }
    [[nodiscard]] const std::array<CachedStreamFile, 12>& stream_files() const { return stream_files_; }

private:
    bool bink_sound_set_{};
    uint32_t stream_file_uses_{};
    std::array<CachedStreamFile, 12> stream_files_{};
};
StreamFileState& stream_file_state();

} // namespace audio
