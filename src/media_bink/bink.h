#pragma once

#include <cstdint>

#include "engine/media/movie.h"

// The Bink SDK (binkw32.dll, the player's own copy - it has no import library: each function is looked up by name
// on first use). The game plays movies and its music / streams through it (media::Movie, engine/media/movie.h:
// bink_movie.cpp's calls these). A movie is binkw32's BINK, whose first fields are the header the game reads
// (media::MovieHeader).
namespace bink {

using Movie = media::MovieHeader;
using FrameJump = media::FrameJump;
using TrackPan = media::TrackPan;
using TrackVolume = media::TrackVolume;
using FrameCopy = media::FrameCopy;

// BinkOpen: a file by path - or, with the flags, an open file handle's address / a file in memory.
Movie* open(const void* source, uint32_t flags);
void close(Movie* movie);                                // BinkClose
int wait(Movie* movie);                                  // BinkWait: non-zero while the next frame isn't due
void do_frame(Movie* movie);                             // BinkDoFrame: decode the frame
void next_frame(Movie* movie);                           // BinkNextFrame
void go_to(Movie* movie, FrameJump jump);                // BinkGoto
void pause(Movie* movie, int pause);                     // BinkPause
void set_pan(Movie* movie, TrackPan track_pan);          // BinkSetPan
void set_volume(Movie* movie, TrackVolume track_volume); // BinkSetVolume
void set_io_size(uint32_t size);                         // BinkSetIOSize
void set_video_on_off(Movie* movie, int on);             // BinkSetVideoOnOff
void set_sound_on_off(Movie* movie, int on);             // BinkSetSoundOnOff
// BinkCopyToBuffer: the decoded frame into a locked surface.
void copy_to_buffer(Movie* movie, void* bits, FrameCopy frame_copy);
// BinkSetSoundSystem(BinkOpenDirectSound, direct_sound): Bink's sound through the game's DirectSound object; false
// if Bink can't use it.
bool use_direct_sound(void* direct_sound);

} // namespace bink
