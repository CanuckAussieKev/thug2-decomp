#pragma once

#include <cstdint>

// The Bink SDK (binkw32.dll, the player's own copy - it has no import library: each function is looked up by name
// on first use). The game plays movies and its music / streams through it.
namespace bink {

// A Bink movie's header (binkw32's BINK: the fields the game reads).
struct Movie {
    uint32_t width;
    uint32_t height;
    uint32_t frames;
    uint32_t frame_num_; // the frame decoded last, from 1
    uint32_t last_frame_num_;
    uint32_t frame_rate_; // frames per frame_rate_div_ seconds
    uint32_t frame_rate_div_;
};

// BinkOpen: a file by path - or, with the flags, an open file handle's address / a file in memory.
Movie* open(const void* source, uint32_t flags);
// A frame to go to and the flags.
struct FrameJump {
    uint32_t frame{};
    int flags{};
};

// A sound track and its pan.
struct TrackPan {
    uint32_t track{};
    int pan{};
};

// A sound track and its volume.
struct TrackVolume {
    uint32_t track{};
    int volume{};
};

void close(Movie* movie);           // BinkClose
int wait(Movie* movie);             // BinkWait: non-zero while the next frame isn't due
void do_frame(Movie* movie);        // BinkDoFrame: decode the frame
void next_frame(Movie* movie);      // BinkNextFrame
void go_to(Movie* movie, FrameJump jump); // BinkGoto
void pause(Movie* movie, int pause);                  // BinkPause
void set_pan(Movie* movie, TrackPan track_pan);  // BinkSetPan
void set_volume(Movie* movie, TrackVolume track_volume); // BinkSetVolume
void set_io_size(uint32_t size);                      // BinkSetIOSize
void set_video_on_off(Movie* movie, int on);          // BinkSetVideoOnOff
void set_sound_on_off(Movie* movie, int on);          // BinkSetSoundOnOff
// Where a frame is copied: the buffer's pitch and height, the corner and the flags.
struct FrameCopy {
    int pitch{};
    uint32_t height{};
    uint32_t x{};
    uint32_t y{};
    uint32_t flags{};
};

// BinkCopyToBuffer: the decoded frame into a locked surface.
void copy_to_buffer(Movie* movie, void* bits, FrameCopy frame_copy);
// BinkSetSoundSystem(BinkOpenDirectSound, direct_sound): Bink's sound through the game's DirectSound object; false
// if Bink can't use it.
bool use_direct_sound(void* direct_sound);

} // namespace bink
