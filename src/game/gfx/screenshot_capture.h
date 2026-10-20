#pragma once

#include <string>

#include "game/sys/screenshot_files.h"

// NOT ORIGINAL - the screenshot key's renderer backend (ours; see game/sys/screenshot.h). Renderer-independence (as
// the level editor's UI backend): screenshot_capture.cpp is the only file of the feature that includes Direct3D; an
// OpenGL / Vulkan renderer replaces only it.
namespace Gfx::ScreenshotCapture {

// The frame as it will be shown - the back buffer, everything drawn (the 2D, the level editor's panels), just before
// it's presented - copied into `out` (rows as the renderer has them, its format mapped to PixelFormat). Called inside
// the frame's scene, from the main loop; the scene is ended for the copy and begun again. A multisampled back buffer
// is resolved first. False (with why in `error`) when it can't be read now: no device, the device lost, an
// unsupported format, a call refused. Blocks until the GPU has drawn the frame (one hitch, at most a frame's time).
bool CaptureBackBuffer(Sys::Screenshot::Frame* out, std::string* error);

} // namespace Gfx::ScreenshotCapture
