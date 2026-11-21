#pragma once

#include <cstdint>
#include <string>

#include "platform/screenshot_files.h"

// NOT ORIGINAL - the screenshot key's renderer backend (ours; see devtools/screenshot.h). Renderer-independence (as
// the level editor's UI backend): screenshot_capture.cpp is the only file of the feature that includes Direct3D; an
// OpenGL / Vulkan renderer replaces only it.
namespace gfx::screenshot_capture {

// The frame as it will be shown - the back buffer, everything drawn (the 2D, the level editor's panels), just before
// it's presented - copied into `out` (rows as the renderer has them, its format mapped to PixelFormat). Called inside
// the frame's scene, from the main loop; the scene is ended for the copy and begun again. A multisampled back buffer
// is resolved first. False (with why in `error`) when it can't be read now: no device, the device lost, an
// unsupported format, a call refused. Blocks until the GPU has drawn the frame (one hitch, at most a frame's time).
bool capture_back_buffer(sys::screenshot::TickCycle* out, std::string* error);

// The clip recorder's ring (render_device.h capture_ring_*): kRingSlots staging surfaces; a frame is read back two
// submits later, so the copy has finished and the read never waits.
bool ring_submit(uint64_t id, int marker_seconds, bool marker_lit, sys::screenshot::TickCycle* ready, uint64_t* ready_id,
                 bool* have, std::string* error);
bool ring_drain(sys::screenshot::TickCycle* ready, uint64_t* ready_id);
void ring_release();

} // namespace gfx::screenshot_capture
