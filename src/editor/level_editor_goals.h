#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>
#include <array>

#include "editor/level_editor.h"
#include "editor/level_editor_ui.h"
#include "game/park_editing/goal_editor.h"

struct ImFont;

// NOT ORIGINAL - the level editor's goals (ours; see level_editor.h - asked for by the user, 2026-10-21): THUG2's own
// Create-a-Goal goals made, placed and edited in the editor for the edited copy, played there in free skate / single
// session as Create-a-Goal plays them.
//
// How THUG2's Create-a-Goal works (studied in the reference build and the game's scripts - goal_editor.qb, goal_editor_menu.qb,
// levels.qb, memcard.qb, decompiled with NeverScript):
//   - The goal editor is a composite object "GoalEditor" (CreateGoalEditor, startup.qb: camera, input, editorcamera,
//     goaleditor - permanent). Its goaleditor component (type 0x81F01058) keeps every created goal of every level: 50
//     slots of 0x338 bytes from +0x18 (the one being edited at +0xA108), at most 10 complete goals a level
//     (MaxEditedGoalsReached). Its member functions (a table at 0x00684270 filled at start-up by 0x00643960) are the
//     menus' whole interface: NewEditorGoal 0x004EA7C0, SetEditorGoalType 0x004E89A0, EditGoal 0x004EA810 (the cursor:
//     each X press stores the editor camera's cursor - place, height, heading - as the next point, 0x004E77C0),
//     set_goal_specific_params 0x004E8780, AddEditedGoalToGoalManager 0x004EC1D0, write_edited_goal_node_positions
//     0x004E8B90, GetEditedGoalsInfo 0x004EC010, flag_goal_as_won 0x004E87A0...
//   - A created goal (the 0x338-byte record, `CEditGoal` here - GoalImage below): +0x0 its level (the level's load
//     script; Load_Sk5Ed_gameplay counts as Load_Sk5Ed), +0x4 view_goals_text (its name), +0x36 ped_name, +0x4B
//     goal_description, +0xAF win_message, +0x114 its type, from +0x118 thirteen 0x1C-byte points {kind (1 the pro, 2
//     the start, 3 a letter, 4 a checkpoint), x, y, z, w, height, angle}, +0x284 the most points, +0x285 the fewest
//     (complete), +0x286 how many are set, +0x289 won, +0x28A slot used, +0x28C level_goal_index (the goal's number in
//     its level: its id CreatedGoal<n> and its nodes TRG_EditedGoal<n>_Pro / _Restart / _Object_<k>), +0x290
//     time_limit, +0x294 control_type, +0x298 score, +0x29C race_total_laps, +0x2A0 race_per_checkpoint_time, +0x2A4..
//     +0x2C8 the trick goals' acceleration_interval, acceleration_percent, trick_time, time_to_stop_adding_tricks,
//     max_tricks, spin, single_combo, combo_size, tricktris_block_size, tricktris_total_to_win, +0x2CC ten key combo
//     sets {set, require_perfect, spin / 180, num_taps} counted at +0x2F4, +0x2F8 ten gap numbers counted at +0x302,
//     +0x303 required_trick_name. init_edited_goal 0x004E69D0 (game/park_editing/goal_editor_util.h) gives its defaults,
//     change_species 0x004E6DA0 its points' kinds and counts; decode_from_struct 0x004E96D0 / export_to_struct 0x004EB260 turn
//     it into a script structure and back (GetEditedGoalsInfo's entries - and the CreatedGoals save, memcard.qb's own
//     save type, version 5).
//   - Playing one: the level's load script (levels.qb) adds the goal editor's nodes only in create-a-goal mode, net
//     games and split screen (AddGoalEditorNodesToLevelNodeArray 0x005AAA80 -> 0x004E8F50: TRG_EditedGoal0..9 _Pro
//     (EditedGoal_Pro_Node: a Ped_From_Profile pedestrian), _Restart (EditedGoal_Restart_Node: a Player1 restart),
//     _Object_0..10 (EditedGoal_Object_Node: a GameObject)), and InitialiseCreatedGoals (goal_editor.qb) adds the level's
//     goals only there: AddEditedGoalToGoalManager (0x004EB6B0 on the goal: its goal manager parameters made by
//     0x004EA850 - the type's generic parameters, EditedGoal_ExtraParams_<type>, trigger_obj_id / restart_node /
//     letters' obj_id / race_waypoints naming its nodes, edited_goal - added as CreatedGoal<n>, 0x0055BA10), then
//     write_edited_goal_node_positions (0x004E7A80: each point's node gets Pos = place + (0, height, 0), Angles = (0, angle
//     - pi, 0), a letter's Type / model) and GoalManager_InitializeGoal (its pro made). The goal is started by talking
//     to its pro; goal_success (goal_utilities.qb) marks it won in the goal editor (flag_goal_as_won). The goal manager
//     leaves `edited_goal` goals out of the level's saved goal state (0x005542B0) - created goals never reach the
//     career save.
//
// What the editor does with it:
//   - A goal here is the same record: its type, name, ped name, goal text, win message, time limit, score, control,
//     the type's own settings (laps / per-checkpoint time, gaps / required trick, key combo sets, spin, sizes...) and
//     its points. The points are nodes in the editor's overlay (level_editor_node_create.h - owned by this module:
//     GenericNode, type ed_goal, named <goal>_pro, _start, _s.._e / _c.._o2, _cp1.._cp10), so they move, turn, drop,
//     undo and go Home as every point node does; their labels and lines are drawn here. A letter's / checkpoint's
//     height above its node is the goal's (as Create-a-Goal's cursor height; 27 for letters - GoalEditor_ObjectHeight).
//   - Played: while the editor isn't running and the level is its edited copy (free skate / single session - never a
//     refused mode, level_editor_layout.h), each complete goal is put in the goal manager as Create-a-Goal's own
//     InitialiseCreatedGoals does: its play nodes TRG_EditedGoal<n>_Pro / _Restart / _Object_<k> made in the overlay
//     (owned, with EditedGoal_*_Node's fields, placed as write_edited_goal_node_positions places them), a 0x338-byte
//     record of it filled here (GoalImage) and handed to the game's own AddEditedGoalToGoalManager (0x004EB6B0, mark
//     unbeaten), then GoalManager_InitializeGoal. <n> is 20 + its place in the list: never 0..9, Create-a-Goal's own -
//     its goals (and the user's own created goals, in the goal editor's table) are never touched: flag_goal_as_won finds
//     no CreatedGoal2x there and does nothing. The editor starting takes them out of play (deactivated if running,
//     GoalManager_RemoveGoal, the pro killed as edit_goal_menu_exit does, the play nodes removed); leaving it puts them
//     back with their changes.
//   - Saved with the edited copy: `goal` records in the level's layout (thug2-decomp-layouts\<scene>.txt - Ctrl+S,
//     the autosave, Reset level deletes them with the rest), so they load whenever the edited copy loads and never with
//     the level as shipped. Nothing goes in the game's saves: not the career (edited_goal), not the CreatedGoals save
//     (the goal editor's table is untouched) - unless the user exports one (Export below).
//   - Export (the Goals panel's "Copy to Create-a-Goal"): the goal's record copied into a free slot of the goal editor's
//     own table for this level (a free level_goal_index 0..9, as NewEditorGoal picks one) - in memory: CREATE-A-MODES >
//     create goals in this level then lists it, and its SAVE GOALS writes it into the CreatedGoals save.
//
// Layout record (version 2 still; an older reader skips the kind):
//   goal <id> type=<Skate|Combo|HighScore|HighCombo|Skatetris|ComboSkatetris|TrickTris|TrickBeat|Race|Gap>
//        name="..." ped="..." text="..." win="..." time=<s> score=<n> control=<Skate|Walk>
//        p0=x,y,z,heading,height p1=... (the pro, the start, then letters / checkpoints)
//        [laps=<n> cptime=<s>] [gaps="<gap>|<gap>" trick="..."] [sets=<i>,<i> spin=<deg> accel=<n> accelpct=<f>
//        tricktime=<ms> stopat=<s> maxtricks=<n> single=<0|1> combo=<n> block=<n> towin=<n>]
// A text holds no '"', '#' (the layout's comment) or control characters (cleaned when set). Unknown keys skipped; a key
// twice, a bad number or a point missing refuses the file (as the other records).
namespace frontend::level_editor::goal_edit {

// --- the pure parts -------------------------------------------------------------------------------------------------

// Create-a-Goal's goal types, in its SELECT GOAL TYPE order (goal_editor_menu.qb's goals_menu_info).
enum class KindTag : uint8_t {
    kSkate,
    kCombo,
    kHighScore,
    kHighCombo,
    kSkatetris,
    kComboSkatetris,
    kTrickTris,
    kTrickBeat,
    kRace,
    kGap
};
constexpr int kTypes = 10;
const char* current_type_name(KindTag type); // the record's word: the type's checksum name ("Skate"...)
const char* type_title(KindTag type);        // Create-a-Goal's menu name ("SKATE letters"...)
uint32_t get_kind_hash(KindTag type);        // 0x54166ACD...
bool type_of(const std::string& name, KindTag* type);
bool kind_of_key(uint32_t checksum, KindTag* type);
// Whether the type is one of the trick goals (key combo sets, spin...): Skatetris, ComboSkatetris, TrickTris, TrickBeat.
bool get_trick_type(KindTag type);
// How many key combo sets the type offers (cag_key_combos: 8; Trick to the Beat the first 4).
int combo_set_count(KindTag type);
const char* combo_set_name(int set); // cag_key_combos' text

// The points, as change_species (0x004E6DA0) makes them: the fewest (a complete goal) and the most, and point `index`'s kind
// (0 unused, 1 the pro, 2 the start, 3 a letter, 4 a checkpoint) for index 0..12.
constexpr int kMaxPoints = 13;
int min_points(KindTag type);
int max_points(KindTag type);
int point_kind(KindTag type, int index);
// A point's label ("Pro", "Start", "S"... "O", "Checkpoint 1"...) and its node's suffix ("pro", "start", "s"... "o2",
// "cp1"...).
std::string point_label(KindTag type, int index);
std::string point_suffix(KindTag type, int index);
// The default height of a point above its node (letters: GoalEditor_ObjectHeight, 27; others 0).
float starting_height(KindTag type, int index);

struct Point {
    V3 pos = {};          // its node's place (on the ground)
    float heading = 0.0f; // its node's Angles y: the way the pro / the skater faces
    float height = 0.0f;  // above the node (letters float)
};

// A goal: what Create-a-Goal's record holds, by name.
struct Goal {
    std::string id; // ed_goalNN: its points' nodes are <id>_<suffix>
    KindTag type = KindTag::kSkate;
    std::string name, ped, text, win;   // view_goals_text, ped_name, goal_description, win_message
    int time = 120;                     // time_limit (15..3600)
    int score = 100;                    // score (HighScore / HighCombo: 1000..10000000)
    bool walk = false;                  // control_type Walk (else Skate)
    int laps = 1, checkpoint_time_ = 0; // Race: race_total_laps (1..10), race_per_checkpoint_time (0..60, 0 off)
    std::vector<std::string> gaps;      // Gap: the gaps (names - their numbers are the gap list's order when played)
    std::string trick;                  // Gap: required_trick_name
    // The trick goals (init_edited_goal's defaults):
    uint32_t sets = 0;                // key combo sets chosen (bit per cag_key_combos entry)
    int spin = 360;                   // 0..900 (Trick to the Beat: 0)
    int accel_interval_ = 5;          // 1..10
    float accel_percent_ = 0.1f;      // 0.01..1
    int combo_time_ = 3000;           // 5..3000
    int stop_at_ = 5;                 // time_to_stop_adding_tricks
    int max_tricks_ = 11;             // 1..15
    bool single_combo_ = false;       // Combo Skate-Tricks
    int combo_size_ = 2;              // 1..15
    int block_size_ = 3, to_win_ = 8; // Tricktris: 1..15, 1..1000
    std::vector<Point> points;        // min_points..max_points of them
};
bool get_same(const Goal& a, const Goal& b);
// A new goal of `type` with Create-a-Goal's defaults (its menu's name, change_species's score - HighScore 15000, HighCombo
// 10000 - Trick to the Beat's spin 0) and no points.
Goal get_defaults(KindTag type, const std::string& id);
// Whether it has every point it needs - and (Gap) a gap, (trick goals) a key combo set: played only then. `why` says what
// is missing.
bool get_complete(const Goal& goal, std::string* why = nullptr);
// A text as a record keeps it: no '"', '#' or control characters, at most `limit` characters (the record's field:
// name 49, ped 20, text / win 99, trick 49).
std::string clean_text(const std::string& text, size_t limit);
// The record's text (after the word "goal") and one read (false: malformed).
std::string write_record(const Goal& goal);
bool read_record(const std::string& text, Goal* out);

// The 0x338-byte created-goal record (CEditGoal) the game's own functions take: filled from a goal as decode_from_struct
// (0x004E96D0) fills it from the goal's structure - init_edited_goal's defaults, used, its level, its level_goal_index,
// change_species's points, then every field; `gap_numbers` the gaps' numbers in the level's gap list now (those not found
// left out).
constexpr size_t kTexelEdge = 0x338;
struct GoalImage {
    park_editing::EditedGoal goal;
};
void fill_image(const Goal& goal, uint32_t level, int level_goal_index, const std::vector<int>& gap_numbers,
                GoalImage* out);
// The angle the record keeps for a point (the cursor's heading: its node gets angle - pi) and back.
float get_image_angle(float heading);
float get_heading_of(float image_angle);

// The goal's play number (level_goal_index) for its place in the list: 20 + place - never Create-a-Goal's 0..9.
constexpr int kFirstPlayIndex = 20, kMaxGoals = 10;
inline int play_index(int place) { return kFirstPlayIndex + place; }
// Its play nodes' names: TRG_EditedGoal<n>_Pro (which 0), _Restart (1), _Object_<which - 2>; its goal manager id.
std::string play_node_name(int which, int index);
std::string get_goal_id(int index); // CreatedGoal<n>

// --- the game side ------------------------------------------------------------------------------------------------

// The goals now (their points' places read from their nodes).
std::vector<Goal> get_all();
bool is_goal_point(uint32_t node);
// A new goal of `type` laid out where the editor's view looks (the pro facing the camera, the start beside it, letters
// / checkpoints ahead) - one undo step; selected (its pro's node). False: no view, ten goals already.
bool fabricate(KindTag type);
// Changed (one undo step a change; `done` false while a field is dragged - the drag is one step).
bool get_edit(const std::string& id, const Goal& changed, bool done);
// Removed with its points (one undo step).
bool drop_out(const std::string& id);
// A race checkpoint added after the last / the last removed (one undo step).
bool add_checkpoint(const std::string& id);
bool remove_checkpoint(const std::string& id);
// Copied into the goal editor's own table (Create-a-Goal's created goals of this level - in memory; its SAVE GOALS
// saves it). `message` says what happened.
bool export_goal(const std::string& id, std::string* message);

// Layouts: the goals now; a layout's made (how many); every goal removed (and out of play); a new level (forgotten -
// any of ours still in the goal manager removed: their nodes went with the level).
std::vector<Goal> get_capture();
int get_apply(const std::vector<Goal>& goals);
void reset_all();
void forget_level();

// The editor starting / stopping: the goals out of play / put back in play (with their changes).
void kickoff();
void halt();
// Once a frame while the editor runs: what the UI asked for.
void tick_frame();
// Labels and lines over the view (the goal of `selected` - a node name, 0 none - brighter).
void add_overlay(V3 camera_pos, const TickCycle& frame, const Lens& lens, std::span<const uint32_t, 4> viewport, float near_z,
                 uint32_t selected, std::vector<ui::Seg>* lines, std::vector<ui::Overlay::OverlayText>* texts);
// The Inspector: the selected point's goal (`node`: its name, 0 none), and the Goals section (the list, new goals).
void inspector_section(uint32_t node, ImFont* mono, ImFont* small_font);
void list_section(ImFont* mono, ImFont* small_font);
// For the screenshot sidecar: "N goals (M complete)".
std::string describe();
// A point to select (the one just made; -1: none) - a Nodes::All() index (the editor selects it).
int take_select();

// For the UI (level_editor_goals_ui.cpp): an action done in Update (as the other modules' UIs queue theirs); a field
// drag ended; the goal's point nodes deleted (with Delete - Restore points makes them again where they were); the goal
// and point a node is; a point selected; whether goals are in the goal manager now; the level's gap list's names.
void get_queue(std::function<void()> action);
void end_drag();
int get_missing(const std::string& id);
bool restore_points(const std::string& id);
bool point_of_node(uint32_t node, std::string* id, int* point);
void choose_one(const std::string& id, int point);
bool in_play();
std::vector<std::string> get_level_gaps();

} // namespace frontend::level_editor::goal_edit
