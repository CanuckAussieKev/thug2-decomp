#pragma once

// NOT ORIGINAL - the render-only frames' running state ([Graphics] max_fps, present_mode - engine/sys/frame_pacing.h has
// the design and the schedule's maths): the settings as read, and what the main loop tells the renderer about the
// frame being drawn - whether it is blended, by how much, which tick it shows, whether it is a render-only frame - and
// the cuts the game reports (nothing is blended across one).
#include <cstdint>

#include "engine/sys/frame_pacing.h"

namespace frame_blend {

struct Frame {
    bool on = false;    // this frame is drawn blended (max_fps != 0, the loop not held)
    bool extra = false; // a render-only frame: no tick ran before it
    float alpha = 1.0F; // 0: the tick before, 1: this tick
    uint64_t tick = 0;  // this tick's number (counts the ticks the loop ran)
};

// The frame being drawn (the main loop sets it; render_pc/frame_interp.cpp reads it). Off: drawn as the tick left it.
Frame& frame();

// A cut: a camera snapped, the skater teleported or respawned, a level loaded - the next frames draw the state as it is
// and nothing is blended from before it (until a tick after it has been seen).
void note_cut();
// Whether a cut was reported since the last call (cleared).
bool take_cut();
// (added by the camera effects) The cuts reported so far (whatever takes them): the motion blur blurs nothing across
// one - a frame drawn after the count changed has no motion from before it.
uint64_t cuts();

// [Graphics] max_fps and present_mode, read from the ini (at start-up with the graphics options, and again when the
// Options screen changes one).
void read_settings();
int max_fps();
frame_pacing::PresentMode present_mode();

} // namespace frame_blend
