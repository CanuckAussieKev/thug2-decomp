#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// NOT ORIGINAL - the loading console (ours, added at the user's request: "a console that shows what is currently
// loading, so we can differentiate between a hang and something still loading"; not the game's and not PartyMod's).
//
// While a loading screen is up, a small panel in its bottom-left corner (JetBrains Mono on a dark, half-transparent
// box - clear of the loading bar, which is centred) shows:
//   LOADING 12.3 s                    last activity 0.4 s ago
//   [the last lines of what is being loaded, oldest first, each with its time since the load began]
// The lines: files opened (File::Open - archives, the data overlay, the data folder - by file name and size),
// files loaded whole (the PIP loader: from an archive, the cache or disk, with their size), .prx archives loaded,
// the script commands that load or build things (Load* / Add* scene and collision / Create* / Parse* / Init* /
// Preload*: scenes, collision, node arrays, QB files, assets, objects, the skater), the scripts called meanwhile
// whose names the game knows (the level's load script and what it calls), and the game log's own lines (thug2-decomp.log; not the data
// overlay's - its files are already there as opened). A line repeated (the same file opened again) counts up as
// "(xN)" instead of filling the panel.
//
// Hang or still loading: the loading screen is drawn by a multimedia timer (timeSetEvent, every 29 ms - 0x004B7400,
// its callback 0x004B6FF0 on winmm's timer thread), not by the loader: the loader never yields to it, and the main
// thread's frames don't run while it's up. So the panel keeps being drawn while the loader works - or is stuck:
// when nothing new has been loaded, "last activity N s ago" keeps counting, and after 10 s it turns orange ("no
// activity") - the last line is then what it's stuck on (or still busy with: a big file, a long script). Only if
// the picture itself stops (the timer thread blocked: the Direct3D device held by the main thread, the game
// crashed or deadlocked) is it frozen. A loading screen shown without a time (no loading bar - then the original
// draws it once and never again) gets a timer of ours that draws it with the panel (100 ms); one shown with
// `freeze` (the last frame kept: cutscenes) gets none and no panel, as in the original.
//
// Cost: a line is a few bytes copied into a fixed ring (64 lines, a lock held for the copy), only while a loading
// screen is up; the panel's text is laid out at most 10 times a second and the result drawn with each of the
// loading screen's frames (one draw call).
//
// thug2-decomp.ini [features] loading_console=0 turns it off (default 1): the loading screen exactly as the
// original's (the same timer, the same picture, no extra timer).
namespace Script {
class CStruct;
}

namespace Gfx::LoadingConsole {

// [features] loading_console (read once).
bool Enabled();
// Whether a loading screen is up and the console runs (lines are kept only then).
bool Active();

// DisplayLoadingScreen (0x00490890's callers: the script command, the cutscene loader): a loading screen shown -
// `freeze`: the last frame kept, `blank`: black instead of the picture. Starts the console's clock (the first
// screen of a load; a screen shown again while one is up keeps it) and, unless frozen, its own drawing timer.
void ScreenShown(bool freeze, bool blank);
// HideLoadingScreen (0x004908C0's callers): the screen gone, the console stopped.
void ScreenHidden();

// A line for the panel (printf-style; cut to kLineWidth characters). Nothing while not Active.
void Note(const char* format, ...);
// A file: `what` ("open", "load"...), the file's name without its folder, its size if known (< 0: not known).
void NoteFile(const char* what, const char* path, long long size);
// A script command or script being run (by checksum; only the interesting ones are kept - see above), with what it
// loads or makes if its parameters say (an unnamed string, scene= / file= / name=...).
void NoteScriptCall(uint32_t name, bool cfunction, Script::CStruct* params);
// A line of the game log (thug2-decomp.log), filtered.
void NoteLogLine(const char* line);

// Drawn by the loading screen's frame (game/gfx/loading_bar.cpp, 0x004B6FF0), inside its scene, with the loading
// screen's lock held: the panel over the picture, the device's state put back after.
void DrawPanel();

// --- pure helpers, for the self-test ---
constexpr size_t kLineWidth = 96;
constexpr size_t kRingLines = 64;
// "name.ext" of "folder\\sub/name.ext".
const char* FileName(const char* path);
// "512 B", "12.3 KB", "4.5 MB".
std::string SizeText(long long bytes);
// Whether a script command's name is one the panel shows.
bool InterestingCommand(const char* name);
// Whether a log line is one the panel shows.
bool InterestingLogLine(const char* line);

// The ring of lines (the console's own state, also used by the self-test on a separate instance).
struct Ring {
    struct Line {
        char text[kLineWidth];
        double time;     // seconds since the load began
        unsigned repeat; // the same text again: 1 = once
    };
    Line lines[kRingLines];
    unsigned count = 0; // lines ever added
    // Add `text` at `time`; the same text as the newest line counts that one up instead.
    void Add(const char* text, double time);
    // The newest `n` lines' text ("[  1.2] text (x3)"), oldest first.
    std::vector<std::string> Last(size_t n) const;
};

} // namespace Gfx::LoadingConsole
