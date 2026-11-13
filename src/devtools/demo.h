#pragma once

#include <string>

namespace ext {
class Extension;
}
namespace input {
class Source;
}

// NOT ORIGINAL - demos (ours, added at the user's request: "a player can record their movements, we can save it as a
// demo, then I can record that demo with various different settings for before/after visualization"; not the game's
// and not PartyMod's). docs/demos.md has the design; the file format is formats/demo_format.h.
//
// Recording:
//   F6 in a level (free skate, not paused, not online; [Keybinds] DemoRecord= another key) starts the level again and
//   records from its load; F6 again stops and saves thug2-decomp-demos\demo_<level>_<date>_<time>.demo (+ .json).
//   THUG2.exe -level <name> -record <demo>      records from the start-up's level load; F6 (or quitting) saves.
//   The developer console: demo record [name] / demo stop / demo play <name> / demo status (devtools/console.h).
// Playing back:
//   THUG2.exe -demo <demo>                      the demo's level loaded in free skate (its skater put back if the
//                                               profile differs), the run played from the recorded input; at its end
//                                               the controls are the player's again.
//   -demo-capture <folder>                      also every frame from the level's first as <folder>\frame_0001.png...
//                                               (relative: to the game folder), then the game quits;
//   -demo-every <n>                             with it: one frame in n.
//   -demo-video <file.mp4>                      saves it as a video ([demo] video_fps / video_quality / video_codec /
//                                               video_scale: engine/media/video_writer.h), then the game quits;
//   -demo-quit                                  quits at the end (without -demo-capture).
// In the game: the pause menu's DEMOS (the data overlay's scripts, tools/pipeline/menu_level/demo_menu.ts, call the
// script commands Demo_Item, Demo_GetEntry, Demo_Record, Demo_Stop and Demo_Play): RECORD DEMO (as F6; STOP RECORDING
// saves, the frames in the pause menu left out), PLAY DEMO and SAVE AS VIDEO (thug2-decomp-videos\<demo>.mp4) from a
// list of the saved demos. A playback started there or with -demo is the player's to take over: a button pressed or a
// stick pushed stops it and the controls are theirs (that frame's input too: Start pauses). REC / REPLAY / SAVING
// VIDEO in the top-right corner (devtools/demo_overlay.h; never in a capture or a video).
// What makes the run the same each time: the load starts from the same random numbers (both streams and the C
// runtime's rand seeded from the demo), the main loop's logic is a fixed 1/60 s a frame, every frame is drawn
// (no frame skipped to catch up, also in the background) and port 0's control data each frame is the recorded one -
// the input after the keyboard, the controller and the automation channel are added up; once the level is up the
// random numbers and the clocks are put back as the recording had them. A check track (the skater and the camera
// every frame from the level's first) is recorded with the input; a playback compares its own with it and logs the
// first frame that differs and the largest drift ("demo: ... drift"), and writes its track to <capture
// folder>\track.txt and run.json (or thug2-decomp-demos\<demo>.play.track.txt / .play.run.json).
//
// Refused in network games and online. Keys our tools read directly (F7, F8, F9, the console) aren't recorded: using
// them while recording makes the playback differ.
namespace devtools::demo {

// The extension (app/ registers it after the start level's) and the input source (after the automation channel's).
ext::Extension& extension();
input::Source& input_source();

// The console's commands: "" or what went wrong / what happened.
std::string record_now(const std::string& name);
std::string stop_now();
std::string play_now(const std::string& name);
// The demo played and saved as a video as it plays: thug2-decomp-videos\<name>.mp4.
std::string save_video_now(const std::string& name);
std::string status_text();

} // namespace devtools::demo
