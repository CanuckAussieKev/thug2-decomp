#pragma once

#include <string>

// NOT ORIGINAL - the screenshot key (ours, added at the user's request: "to reference to you things that I have
// noticed"; not the game's and not PartyMod's).
//
// Print Screen (anywhere: the menus, a level, paused, the level editor) saves what's on screen - the frame as it is
// presented, the level editor's panels included - as a PNG in <game folder>\Screenshots\, named
// thug2_YYYY-MM-DD_HH-MM-SS.png (_2, _3 ... when taken), with a sidecar .txt of the same name: when, the exe and the
// DLL's commit, the screen size, where the game is (front end / level: its load script and scene, the game mode,
// paused, loading), the skater's position and heading, the camera, the level editor's state when it runs, and the
// log's last 20 lines. "Screenshot saved: <name>" shows for 2 seconds (the editor's message line in the editor, a
// panel message elsewhere) and the full path is logged.
//
// The pieces: screenshot.cpp (this: the key, the frame's request, what the sidecar says, the worker thread that
// writes), screenshot_files.cpp (pure: names, pixels -> RGB, PNG, the sidecar's text) and
// game/gfx/screenshot_capture.cpp (the renderer backend: the only file of it that includes Direct3D).
//
// thug2-decomp.ini: [features] screenshots=0 turns it off (default 1); [features] screenshot_dir= another folder
// (relative: to the game folder); [Keybinds] Screenshot= the key - an SDL scan code (70 Print Screen, 69 F12) or a
// key's name. Not set: Print Screen, or F12 when Windows' Snipping Tool takes Print Screen (logged at start-up).
namespace Sys::Screenshot {

// [features] screenshots (read once).
bool Enabled();
// The key's SDL scan code (0 while off).
int Key();
// Start listening for the key (Sio::InitSdlInput, after the input settings are read).
void Init();
// Once a frame from the main loop, just before the frame is ended and presented (`rendering`: this frame is drawn).
// Takes a requested picture (the frame's scene open, everything drawn), hands it to the writer and, when the writer
// is done, logs and shows the result.
void BeforePresent(bool rendering);
// The renderer, a viewport's camera as it's drawn (for the sidecar): its position and view direction.
void NoteView(int viewport, const float position[3], const float forward[3]);

// A picture asked for by our own debugging code (works with the key off too; [debug] collision_event_screenshots -
// game/skater/collision_events.h): the frame being drawn, taken as the key's, `note` added to the sidecar and the
// log's "saved" line; no message on screen. Dropped while another is being written.
bool RequestEventShot(const std::string& note); // (false: dropped)
// A short message on screen for 2 s (the "Screenshot saved" one's panel: a newer one replaces it; no sound, the game
// carries on) - our debugging aids' notices (collision_events.h). Works with the key off too.
void ShowNotice(const std::string& text);
// Viewport 0's camera as last drawn (NoteView), if drawn within the last 10 frames.
bool LastView(float position[3], float forward[3]);

// A frame series (the test-automation channel's `capture` - game/front/automation.h; works with the key off too):
// the next `frames` rendered frames (at most kMaxSeriesFrames), every one, as frame_0001.png ... in `folder`
// (relative: to the screenshot folder; "": series_<time> there), with one sidecar series.txt (the screenshot
// sidecar's state at the first frame, the series' count, and a line a frame: the game's frame, its vblank count and
// clock) - no cursor mark, no messages. The pictures are encoded on worker threads; while kSeriesInFlight are waiting,
// the frame waits for one. The game slows down while it captures, but every logic frame is drawn and taken: the
// main loop doesn't count the capture's time (SeriesRunning - no frames skipped to catch up), so the series is the
// game's frames one after another, a vblank (1/60 s of game time) apart. "" if started, else why not (one series at
// a time).
constexpr int kMaxSeriesFrames = 600;
constexpr int kSeriesInFlight = 6;
std::string StartSeries(int frames, const std::string& folder);
// Whether a series is still being captured or written.
bool SeriesRunning();
// The last series' result: "ok: <n> frames in <folder>" or "error: ...".
std::string SeriesResult();

} // namespace Sys::Screenshot
