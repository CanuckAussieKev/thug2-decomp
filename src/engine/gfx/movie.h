#pragma once

#include <cstdint>

namespace render {

// 0x0044FA80: draw a movie frame's texture at (x, y), (width, height) in screen units, faded by alpha (0-1),
// with PartyMod's 4:3 fit inside the back buffer (contains code from PartyMod
// (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md).
void draw_movie_frame(void* texture, float x, float y, float width, float height, float alpha);

// PartyMod's two calls in draw_movie_frame. set_movie_texture (cdecl): the renderer's texture set (0x004DA730),
// noting the texture's size first. draw_movie_quad (stdcall, as the device's DrawPrimitiveUP it stands in for):
// the quad's corners moved to the largest 4:3 area centred in the back buffer, then drawn.
void set_movie_texture(int stage, void* texture, int unknown);
void draw_movie_quad(void* device, int type, uint32_t count, void* vertices, uint32_t stride);

} // namespace render

namespace script {
class Struct;
}

namespace render {

// 0x0059F0C0: the PlayMovie script command. With thug2-decomp.ini [features] skip_intro_movies=1 the
// start-up logo and intro movies (ATVI, Beenox_Shift, Dolby_Digital, Intel_intro, NSlogo, intro, introf) are
// skipped, and so is any movie the start-up script startup_loading_screen plays (Anteara Mod's Shorty's movie);
// other movies play as usual.
bool play_movie(script::Struct* params, void* script);
// The skip_intro_movies flag (read from the ini once - also on for a run started with -edit / edit_level,
// frontend/start_level.h).
bool& skip_intro_movies();

// A Bink movie's header (binkw32's BINK: the fields the game reads).
struct Bink {
    uint32_t width;
    uint32_t height;
    uint32_t frames;
    uint32_t frame_num_; // the frame decoded last, from 1
    uint32_t last_frame_num_;
    uint32_t frame_rate_; // frames per frame_rate_div_ seconds
    uint32_t frame_rate_div_;
};

// 0x0044FDC0 (also reached by a jump at 0x0044F830): play data\movies\bik\<name>.bik (a path before the last
// backslash in `name` is dropped) to its end, until the quit request, or until start or X is pressed on a pad
// (pressed after being seen up). The music is paused and the streams stopped first. Originally from PartyMod Fixes: the
// pads are read every frame, not every fifth.
void play_movie_file(const char* name);

// 0x0044F8C0: the render states for movies (the old ones saved for end_movie) and a width x height texture for
// the frames (null if it can't be made). Also notes the movie's and the back buffer's sizes as floats.
void* begin_movie(uint32_t width, uint32_t height);
// 0x0044F9F0: the render states back; the texture released.
void end_movie(void* texture);
// 0x0044FCC0: one frame: clear, draw the texture over the screen, present; then Bink on to the next frame.
void draw_movie(Bink* bink, void* texture);

// 0x0044F840: whether a button of `mask` is pressed now after having been seen up (control data bits are set while up).
bool button_pressed_again(const uint8_t* control_data, uint16_t* seen_up, uint32_t mask);

extern Bink* g_current_movie; // a Bink movie playing

extern float g_one_percent;
extern bool g_quit_requested;
extern bool g_movie_playing;
extern bool g_device_lost;
extern int g_movie_dump_count; // (also read by the music's shuffle: pcm/music.cpp)
} // namespace render
