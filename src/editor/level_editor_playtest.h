#pragma once

#include <cstdint>
#include <set>
#include <span>
#include <string>
#include <vector>
#include <array>

#include "editor/level_editor.h"
#include "game/skate/play_trackers.h"
#include "editor/level_editor_ui.h"

namespace modules {
struct FrontEnd;
}
struct ImFont;

// NOT ORIGINAL - play testing from the level editor (ours; see level_editor.h), the UX research's #3, #19, #20 and the
// park editors' "test from this restart":
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
// How the skater's state is read (the reference build; our own reading of the components' script member functions):
//   - SkaterState (component 0x43686585, vtable 0x0064C140; member functions table 0x00684A80, their code
//     0x00519480..0x00519740): +0x18 the physics state - OnGround 0 (0x00525E90), InAir 1, OnWall 2, OnLip 3, OnRail 4,
//     in_wallplant 5 (0x00519480..0x005194C0: each compares +0x18); +0x70 in_bail_state (0x005196D0, a byte); Walking
//     (0x005194D0) = +0x118 clear and +0x1C == 1; Driving (0x00519510) = +0x118 set. The component's reset (0x00519460)
//     sets +0x18 to 1.
//   - SkaterBalanceTrick (0x589E5FB3): +0x18 the balance trick's type (manual_balance.h) - Manual 0xEF24413B, NoseManual
//     0x0AC90769, Grind 0x255ED86F, Slide 0x8D10119D ...; 0 when none (cleared by its stop 0x004F8EE0 and reset
//     0x004F8DE0).
//   - SkaterScore (0xD262802F): +0x18 the score; its +0x14 times its +0x60 is the combo's points pending (as the trick
//     objects goal reads it: trick_goals.cpp, 0x00576520).
//   - The gap checklist (the skate module's, 0x005656C0 - gap_checklist.h): a gap got raises its pending_ (register_gap_hit
//     0x00578C70); landing the combo moves pending into num_ (pay_out_queued_gaps 0x005796C0); a bail clears pending
//     (clear_pending 0x00578CF0). Read each frame - nothing of the game is hooked.
// Never in a network session or a refused mode (Layout::Refused); outside the editor mode it is all off.
namespace frontend::level_editor::play_test {

// --- the pure parts -----------------------------------------------------------------------------------------------

// What the skater was doing.
enum class Motion : uint8_t { kGround, kAir, kGrind, kLip, kWall, kManual, kBail, kWalk, kDrive };
constexpr int kMotions = 9;
const char* motion_name(Motion motion); // "ground", "air", ...
uint32_t motion_colour(Motion motion);  // ARGB
bool motion_from_name(const std::string& name, Motion* motion);

// The physics states (SkaterState +0x18).
constexpr int kStateGround = 0, kStateAir = 1, kStateWall = 2, kStateLip = 3, kStateRail = 4, kStateWallplant = 5;
// The balance tricks' types (SkaterBalanceTrick +0x18) that make a manual.
constexpr uint32_t kManual = 0xEF24413B, kNoseManual = 0x0AC90769;
// The motion from the skater's state: a bail first, then driving, walking, the physics state (a wall plant is a wall
// ride's), a manual (on the ground with a manual's balance trick). An unknown state counts as the ground.
Motion get_classify(int physics_state, bool bailing, uint32_t balance_trick, bool walking, bool driving);

// A point of a trail; `jump`: the skater was moved there (a restart, a teleport) - no line from the point before.
struct Sample {
    V3 pos = {};
    Motion motion = Motion::kGround;
    bool jump = false;
};

// The thinning: a frame's position is kept when the motion changes (with the frame before it, so each stretch keeps its
// colour), when it moved more than kMaxStep since the last kept point, or more than kMinStep and turned by more than
// kTurnDegrees; a move of more than kJump in a frame is a jump (the frame before kept, the new one marked).
constexpr float kMinStep = 25.0F, kMaxStep = 150.0F, kTurnDegrees = 8.0F, kJump = 600.0F;
constexpr size_t kMaxSamples = 40000; // (a run's; after that it isn't recorded further - `full`)
struct Recorder {
    // The fields other code reads and sets.
    std::vector<Sample>& samples() { return samples_; }
    [[nodiscard]] const std::vector<Sample>& samples() const { return samples_; }
    [[nodiscard]] bool full() const { return full_; }

private:
    std::vector<Sample> samples_;
    bool have_last_ = false, last_kept_ = false;
    Sample last; // the last frame fed
    bool full_ = false;

public:
    void feed(V3 pos, Motion motion);
};

// A marker on a trail.
enum class EventKind : uint8_t { kGap, kBail, kCombo };
enum class GapStatus : uint8_t { kAwaiting, kLanded, kLost };
struct Occurrence {
    EventKind kind = EventKind::kGap;
    V3 pos = {};
    std::string name;                        // a gap's
    int value = 0;                           // a gap's score / a combo's points
    GapStatus status = GapStatus::kAwaiting; // (a gap: got, then landed or lost; a combo: Landed or Lost)
};

// A recorded run.
struct Run {
    int number = 0;
    std::vector<Sample> samples_;
    std::vector<Occurrence> events;
    float seconds = 0.0F;
    std::array<float, kMotions> motion_seconds_ = {};
    bool visible = true;
    bool full_ = false;
};

[[nodiscard]] int gaps_landed(const Run& run_record);

[[nodiscard]] int bails(const Run& run_record);

[[nodiscard]] int top_combo_value(const Run& run_record);

// A level's trails: its runs (the newest last, at most kMaxRuns) and the gaps landed in them.
constexpr size_t kMaxRuns = 12;
struct LevelTrails {
    // The fields other code reads and sets.
    std::vector<Run>& runs() { return runs_; }
    [[nodiscard]] const std::vector<Run>& runs() const { return runs_; }
    std::set<std::string>& landed() { return landed_; }
    [[nodiscard]] const std::set<std::string>& landed() const { return landed_; }
    [[nodiscard]] int next_number() const { return next_number_; }
    void set_next_number(int value) { next_number_ = value; }

private:
    std::vector<Run> runs_;
    std::set<std::string> landed_;
    int next_number_ = 1;

public:
    void append_one(Run run); // (the oldest dropped past kMaxRuns)
};

// The side file: "thug2-decomp trails 1", then `gap <name>` lines (landed), and each run: `run <number> <seconds>
// <visible>`, its samples `s x y z <motion> <jump>` and events `e <gap|bail|combo> x y z <value> <pending|landed|lost>
// <name>` (to the end of the line). Unknown lines are skipped; false for another file.
std::string get_format(const LevelTrails& trails);
bool parse(const std::string& text, LevelTrails* trails);
std::string get_file_name(uint32_t scene); // "<8 hex digits>.trails.txt"

// Simulate: which keys pressed while it runs leave it running (the camera's, the view's, the windows' - not ones that
// edit): with no Ctrl / Alt, F, the digits, the keypad, C, O, T, B, N, P, H, I, /, Tab, `, F1, F5 - F7, and W A S D Q E Space
// when they fly (`fly_keys_safe`: the right button held, or the default scheme where they never nudge a selection -
// level_editor_transform.h's fly_always off); with Ctrl the digits (bookmarks) and the comma (preferences); Alt+H; the
// modifiers alone. Anything else (the arrow keys' nudges, Delete, G, Ctrl+S / Z / Y / D, Escape...) stops it first.
bool simulate_key_keeps(int scancode, bool ctrl, bool alt, bool fly_keys_safe);

// --- the game side ------------------------------------------------------------------------------------------------

// Once a frame from the front end's input logic while the editor isn't running (its extension): the trail
// recorded, a restart's play from here finished.
void tick_frame(modules::FrontEnd* front_end);
// The editor starting (LevelEditor_Start: the run under way ends; the level's side file read if it has no runs) and
// stopping (LevelEditor_Stop, first thing: a simulation stopped and the world put back).
void on_editor_start();
void on_editor_stop();
// The editor's Update, after the keymap's begin_frame: its actions (Actions::kSimulate - F6, kTrailWindow - F7 by
// default, rebindable) and the UI's requests done here.
void tick(bool dragging);
// The trails and markers over the view (the UI's overlay; nothing while hidden).
void add_overlay(V3 camera, const TickCycle& frame, const Lens& lens, std::span<const uint32_t, 4> viewport, float near_z,
                 std::vector<ui::Seg>* lines, std::vector<ui::Overlay::Circle>* circles,
                 std::vector<ui::Overlay::OverlayText>* texts);

bool get_simulating();
// A gap's state for the UI: landed in a recorded run of this level (`known`: the level has any trail data at all).
bool gap_landed(const std::string& name);
// "landed" / "untested" for a gap's name (the Triggers window's rows).
const char* gap_note(const std::string& name);
// Play from this restart (the Inspector's button): the editor closes into play, the skater put at `restart`
// (kLevelStart: where the level starts the skater - ResetSkaters without a node).
constexpr uint32_t kLevelStart = 0xFFFFFFFFU;
void play_from_restart(uint32_t restart);
// Play (the editor mode's Play menu): from the level's start.
void play_from_start();

// The UI (level_editor_playtest_ui.cpp, ImGui): the Trail window, a restart's Inspector row.
bool& window_open();
void pane(float scale, PaneFonts fonts);
void restart_section(int node_index);
// What the UI asks for (done in the next Tick).
enum class Ask : uint8_t { kNone, kToggleSimulate, kClear, kStoreAway, kReadIn, kToggleKeepSimulation };
void get_request(Ask ask);
void delete_run(int number);
void set_run_visible(int number, bool visible);
bool& show_trails();
// For the UI: the current level's trails (null: none), its gaps as the checklist has them now, recording on.
const LevelTrails* current_trails();
std::vector<GapRead> get_level_gaps();
bool& recording();
bool& keep_simulation();    // (Simulate's stop leaves the objects where they got to)
std::string get_standing(); // a line: recording / runs / simulate

} // namespace frontend::level_editor::play_test
