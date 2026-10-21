#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "game/front/level_editor.h"
#include "game/front/level_editor_ui.h"

namespace Mdl {
struct FrontEnd;
}
struct ImFont;

// NOT ORIGINAL - play testing from the level editor (ours; see level_editor.h), the UX research's #3, #19, #20 and the
// park editors' "test from this restart" (docs/editor-ux-research.md):
//   - The skater trail (Trackmania's build - drive - look loop): while the level is its edited copy and the editor is
//     closed (any way out: Escape, F5's play from here, a restart's Play from here), each run's path is recorded - the
//     local skater's position each frame, thinned - with what the skater was doing (the motion: ground, air, grind, lip,
//     wall, manual, bail, walking, driving - read from the skater's state components, below), the gaps (got, then landed
//     or lost) and the combos' points as they end. Back in the editor the last runs are drawn as lines coloured by the
//     motion, with markers where gaps were got (green landed, red lost, yellow still pending) and where the skater
//     bailed (red crosses). The Trail window (F7, the toolbar's Trail) lists the runs (show / hide, delete, clear) and
//     keeps them per level for the session; Save writes them beside the layout (thug2-decomp-layouts\<scene>.trails.txt,
//     read again the next time the editor opens in that level with no runs in memory).
//   - Gaps validated by skating (Trackmania's validation): every gap landed during a recorded run is marked landed for
//     the level; the Trail window lists the level's gaps (the gap list's: the level's own and the editor's) with
//     "landed" / "untested", and the Triggers window marks the editor's gap triggers the same way.
//   - Test from this restart: the Inspector of a restart (spawn) node has Play from here - the editor closes as F5's
//     play from here does (level_editor_play.h: back with F5 / Ctrl+P, the camera and selection as they were), then the
//     skater is put at that restart as F8's restart tester puts it (ResetSkaters node_name=<restart>).
//   - Simulate (Unreal's Simulate in Editor; F6, the toolbar's Simulate): the world runs while the editor stays open
//     with its free camera - peds, cars, moving objects, particles, scripts - the skater frozen and hidden (as the
//     editor keeps it). F6 again stops: the game paused as the editor keeps it and, by default, every object put back
//     where it was when the simulation started (position, orientation, velocity - not their scripts' or paths'
//     progress: a ped carries on towards the waypoint it was walking to). Clicking in the view, or any key that edits,
//     stops it first (the edit is made on the world as it was), as does leaving the editor.
//
// How the skater's state is read (THUG2PM.exe; our own reading of the components' script member functions):
//   - SkaterState (component 0x43686585, vtable 0x0064C140; member functions table 0x00684A80, their code
//     0x00519480..0x00519740): +0x18 the physics state - OnGround 0 (0x00525E90), InAir 1, OnWall 2, OnLip 3, OnRail 4,
//     InWallplant 5 (0x00519480..0x005194C0: each compares +0x18); +0x70 InBailState (0x005196D0, a byte); Walking
//     (0x005194D0) = +0x118 clear and +0x1C == 1; Driving (0x00519510) = +0x118 set. The component's reset (0x00519460)
//     sets +0x18 to 1.
//   - SkaterBalanceTrick (0x589E5FB3): +0x18 the balance trick's type (manual.h) - Manual 0xEF24413B, NoseManual
//     0x0AC90769, Grind 0x255ED86F, Slide 0x8D10119D ...; 0 when none (cleared by its stop 0x004F8EE0 and reset
//     0x004F8DE0).
//   - SkaterScore (0xD262802F): +0x18 the score; its +0x14 times its +0x60 is the combo's points pending (as the trick
//     objects goal reads it: trick_goals.cpp, 0x00576520).
//   - The gap checklist (the skate module's, 0x005656C0 - gap_checklist.h): a gap got raises its m_pending (GotGap
//     0x00578C70); landing the combo moves pending into m_count (AwardPendingGaps 0x005796C0); a bail clears pending
//     (ClearPending 0x00578CF0). Read each frame - nothing of the game is hooked.
// Never in a network session or a refused mode (Layout::Refused); [features] level_editor=0 turns it all off.
namespace Front::LevelEditor::PlayTest {

// --- the pure parts (self-tested: src/hook/selftests_editor_ux6.cpp) ----------------------------------------------

// What the skater was doing.
enum class Motion : uint8_t { Ground, Air, Grind, Lip, Wall, Manual, Bail, Walk, Drive };
constexpr int kMotions = 9;
const char* MotionName(Motion motion); // "ground", "air", ...
uint32_t MotionColour(Motion motion);  // ARGB
bool MotionFromName(const std::string& name, Motion* motion);

// The physics states (SkaterState +0x18).
constexpr int kStateGround = 0, kStateAir = 1, kStateWall = 2, kStateLip = 3, kStateRail = 4, kStateWallplant = 5;
// The balance tricks' types (SkaterBalanceTrick +0x18) that make a manual.
constexpr uint32_t kManual = 0xEF24413B, kNoseManual = 0x0AC90769;
// The motion from the skater's state: a bail first, then driving, walking, the physics state (a wall plant is a wall
// ride's), a manual (on the ground with a manual's balance trick). An unknown state counts as the ground.
Motion Classify(int physicsState, bool bailing, uint32_t balanceTrick, bool walking, bool driving);

// A point of a trail; `jump`: the skater was moved there (a restart, a teleport) - no line from the point before.
struct Sample {
    V3 pos = {};
    Motion motion = Motion::Ground;
    bool jump = false;
};

// The thinning: a frame's position is kept when the motion changes (with the frame before it, so each stretch keeps its
// colour), when it moved more than kMaxStep since the last kept point, or more than kMinStep and turned by more than
// kTurnDegrees; a move of more than kJump in a frame is a jump (the frame before kept, the new one marked).
constexpr float kMinStep = 25.0f, kMaxStep = 150.0f, kTurnDegrees = 8.0f, kJump = 600.0f;
constexpr size_t kMaxSamples = 40000; // (a run's; after that it isn't recorded further - `full`)
struct Recorder {
    std::vector<Sample> samples;
    bool haveLast = false, lastKept = false;
    Sample last; // the last frame fed
    bool full = false;
    void Feed(V3 pos, Motion motion);
};

// A marker on a trail.
enum class EventKind : uint8_t { Gap, Bail, Combo };
enum class GapStatus : uint8_t { Pending, Landed, Lost };
struct Event {
    EventKind kind = EventKind::Gap;
    V3 pos = {};
    std::string name;  // a gap's
    int value = 0;     // a gap's score / a combo's points
    GapStatus status = GapStatus::Pending; // (a gap: got, then landed or lost; a combo: Landed or Lost)
};

// The gap checklist as read this frame: each gap's name, pending landings and times landed.
struct GapRead {
    std::string name;
    int pending = 0, count = 0, score = 0;
};
// What changed since the last read: gaps got (pending went up), landed (count went up), lost (pending back to 0 with
// no landing). The first read only learns the list.
struct GapChange {
    enum Kind { Got, Landed, Lost } kind;
    std::string name;
    int score = 0;
};
struct GapTracker {
    bool known = false;
    std::vector<GapRead> last;
    std::vector<GapChange> Update(const std::vector<GapRead>& now);
    void Forget() { known = false, last.clear(); }
};

// A combo's end: its points (pending, as the score reads them) dropped to nothing - landed, unless the skater bails
// within kSettleFrames of it (or was bailing then).
constexpr int kSettleFrames = 12;
struct ComboEnd {
    int points = 0;
    bool landed = false;
};
struct ComboTracker {
    int pending = 0;     // the points pending last frame
    int ending = 0;      // a combo that just ended: its points (0: none) ...
    int settle = 0;      // ... and the frames left to see a bail
    bool Update(int pendingNow, bool bailing, ComboEnd* end); // true when `end` is a combo decided this frame
};

// A recorded run.
struct Run {
    int number = 0;
    std::vector<Sample> samples;
    std::vector<Event> events;
    float seconds = 0.0f;
    float motionSeconds[kMotions] = {};
    bool visible = true;
    bool full = false;
    int GapsLanded() const;
    int Bails() const;
    int BestCombo() const;
};

// A level's trails: its runs (the newest last, at most kMaxRuns) and the gaps landed in them.
constexpr size_t kMaxRuns = 12;
struct LevelTrails {
    std::vector<Run> runs;
    std::set<std::string> landed;
    int nextNumber = 1;
    void Add(Run run); // (the oldest dropped past kMaxRuns)
};

// The side file: "thug2-decomp trails 1", then `gap <name>` lines (landed), and each run: `run <number> <seconds>
// <visible>`, its samples `s x y z <motion> <jump>` and events `e <gap|bail|combo> x y z <value> <pending|landed|lost>
// <name>` (to the end of the line). Unknown lines are skipped; false for another file.
std::string Format(const LevelTrails& trails);
bool Parse(const std::string& text, LevelTrails* trails);
std::string FileName(uint32_t scene); // "<8 hex digits>.trails.txt"

// Simulate: which keys pressed while it runs leave it running (the camera's, the view's, the windows' - not ones that
// edit): with no Ctrl / Alt, F, the digits, the keypad, C, O, T, B, N, P, H, I, /, Tab, `, F1, F5 - F7, and W A S D Q E Space
// when they fly (`flyKeysSafe`: the right button held, or the default scheme where they never nudge a selection -
// level_editor_transform.h's FlyAlways off); with Ctrl the digits (bookmarks) and the comma (preferences); Alt+H; the
// modifiers alone. Anything else (the arrow keys' nudges, Delete, G, Ctrl+S / Z / Y / D, Escape...) stops it first.
bool SimulateKeyKeeps(int scancode, bool ctrl, bool alt, bool flyKeysSafe);

// --- the game side ------------------------------------------------------------------------------------------------

// Once a frame from the front end's input logic while the editor isn't running (MouseMenus::Update): the trail
// recorded, a restart's play from here finished.
void Update(Mdl::FrontEnd* frontEnd);
// The editor starting (LevelEditor_Start: the run under way ends; the level's side file read if it has no runs) and
// stopping (LevelEditor_Stop, first thing: a simulation stopped and the world put back).
void OnEditorStart();
void OnEditorStop();
// The editor's Update, after the keymap's BeginFrame: its actions (Actions::kSimulate - F6, kTrailWindow - F7 by
// default, rebindable) and the UI's requests done here.
void Tick(bool dragging);
// The trails and markers over the view (the UI's overlay; nothing while hidden).
void AddOverlay(V3 camera, const Frame& frame, const Lens& lens, const uint32_t viewport[4], float nearZ,
                std::vector<Ui::Line>* lines, std::vector<Ui::Overlay::Circle>* circles,
                std::vector<Ui::Overlay::Text>* texts);

bool Simulating();
// A gap's state for the UI: landed in a recorded run of this level (`known`: the level has any trail data at all).
bool GapLanded(const std::string& name);
// "landed" / "untested" for a gap's name (the Triggers window's rows).
const char* GapNote(const std::string& name);
// Play from this restart (the Inspector's button): the editor closes into play, the skater put at `restart`.
void PlayFromRestart(uint32_t restart);

// The UI (level_editor_playtest_ui.cpp, ImGui): the Trail window, a restart's Inspector row.
bool& WindowOpen();
void Window(float scale, ImFont* heading, ImFont* small);
void RestartSection(int nodeIndex);
// What the UI asks for (done in the next Tick).
enum class Ask { None, ToggleSimulate, Clear, Save, Load, ToggleKeepSimulation };
void Request(Ask ask);
void DeleteRun(int number);
void SetRunVisible(int number, bool visible);
bool& ShowTrails();
// For the UI: the current level's trails (null: none), its gaps as the checklist has them now, recording on.
const LevelTrails* CurrentTrails();
std::vector<GapRead> LevelGaps();
bool& Recording();
bool& KeepSimulation(); // (Simulate's stop leaves the objects where they got to)
std::string Status();   // a line: recording / runs / simulate

} // namespace Front::LevelEditor::PlayTest
