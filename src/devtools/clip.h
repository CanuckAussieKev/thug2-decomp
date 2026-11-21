#pragma once

#include <string>

// NOT ORIGINAL - the clip key (ours, added so the user can show rendering problems: a recording of what is on screen,
// not the game's and not PartyMod's).
//
// F4 (anywhere a level or a menu is drawn) starts a recording, F4 again stops it: <game folder>\Videos\
// thug2_YYYY-MM-DD_HH-MM-SS.mp4 (H.264 through NVENC or Media Foundation, else MPEG-4 - media/video_writer.h) with a
// sidecar .txt of the same name built like the screenshot's (devtools/screenshot.h): the game's state at the start and
// at the stop, the duration, the frames written and dropped, the codec, and a line per displayed frame - its number,
// the video frame it first shows in, the time, the game's frame, the frame time and the camera - so a moment in the
// clip can be matched with a pose. tools/game/clip_frames.ts takes frames out of a clip as PNGs.
//
// While recording a red dot and the elapsed time (mm:ss) are drawn in the top left corner - after the picture is
// copied, so they are on screen and never in the clip. The picture is the one the screenshot key takes (HUD included),
// copied through a ring of staging buffers and read back two frames later, so no frame waits for the GPU; a worker
// thread scales and encodes it from a short queue (a frame that finds it full is dropped and counted). Frames go
// in at a fixed rate (video_fps): a slower game repeats its last frame, the clip plays in real time. A clip ends on the
// key, on a level change or loading screen, when the device is lost, on exit and after clip_max_seconds.
//
// thug2-decomp.ini: [features] clips=0 turns it off (default 1); clip_dir= another folder (relative: to the game
// folder); clip_scale= the size as a percentage of the game's (default 50); clip_max_seconds= (default 300);
// clip_quality= 1-100 (default: [demo] video_quality); clip_fps= (default: [demo] video_fps, 60); [Keybinds]
// RecordClip= the key (an SDL scan code or a key's name). Automation: `clip start` / `clip stop`.
namespace sys::clip {

// [features] clips (read again after the game writes the settings).
bool is_switched_on();
// Start listening for the key (os_layer::init_input, after the input sources started).
void prepare_all();
// Once a frame from the main loop, just before the frame is presented (`rendering`: this frame is drawn).
void before_present(bool rendering);
// The device is about to be reset: a clip in progress is finished.
void device_lost();
// The game is quitting: a clip in progress is finished and written first.
void shutdown();
// Asks, as the key does, for a start / a stop / a toggle at the next frame (the automation channel's `clip`).
enum class Request { kToggle, kStart, kStop };
void request(Request what);
bool recording();
// The last clip's result as the log says: "ok: <path>" or "error: ..." ("" before any).
std::string last_result();

} // namespace sys::clip
