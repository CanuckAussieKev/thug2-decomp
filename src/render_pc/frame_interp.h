#pragma once

// NOT ORIGINAL - the world drawn between two simulation ticks ([Graphics] max_fps: engine/sys/frame_pacing.h). While the
// main loop says a frame is blended (frame_blend::frame()), render::draw_world calls begin_frame first and end_frame
// last:
//   - begin_frame: on the first frame drawn after a tick, every instance's transform and bone palette and every
//     viewport's camera are noted as that tick's (the tick before's kept); then each instance seen in both ticks - the
//     same instance, the same mesh list and bone count, no jump between them (maths/transform_blend.h kObjectJump) - gets
//     its transform blended, and its bones (a blended copy of the palette the instance points at; the palette itself is
//     never written: it is the game's) unless a bone jumped (an animation snapped). Anything new, gone, cut or jumped is
//     drawn as the tick left it.
//   - camera (draw_viewport, after the extensions placed the camera): the viewport's camera blended the same way, unless
//     it is another camera than the tick before's, it jumped (kCameraJump: a cut), an extension placed it (the level
//     editor's flying camera, the automation channel's held pose - drawn where they put it), or a cut was reported.
//   - end_frame: every transform, bone pointer and camera put back exactly as the tick left them, so the game reads
//     nothing of the blend.
// The instances' identity is the instance itself (its address, with its mesh list): the renderer's motion vectors key
// their draws by mesh and instance too, so a blended frame's motion is the move since the frame drawn before it.
#include <cstdint>

namespace gfx {
class ViewCam;
}

namespace render::frame_interp {

// draw_world's first step: note the tick's state on its first frame, then blend the instances (frame_blend off: nothing).
void begin_frame();
// The camera of viewport `index` blended (placed_by_extension: the extensions moved it this frame - drawn as placed).
void camera(gfx::ViewCam* camera, int index, bool placed_by_extension);
// draw_world's last step: everything put back as the tick left it.
void end_frame();
// The texture animations' time for this frame: the clock's (frame_clock::get_time) less the part of a tick the frame is
// behind (the clock's own when not blending).
uint32_t shown_time_ms();
// The game clock's seconds a frame shows (the sky's and the water's time), the same way.
double shown_game_seconds(double seconds);
// A level or the loading screen came up: what was noted is dropped (nothing blended until two ticks are seen again).
void forget();

// The counts of the last blended frame (for the log): instances blended, instances drawn as they are, cameras blended.
struct Counts {
    int blended = 0;
    int as_is = 0;
    int cameras = 0;
};
Counts last_counts();

} // namespace render::frame_interp
