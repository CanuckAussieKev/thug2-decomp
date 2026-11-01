#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"
#include <gsl/pointers>

namespace script {
class Array;
class Script;
class Struct;
} // namespace script
namespace memory {
class Arena;
} // namespace memory

// Create-a-goal: a goal the player made in the goal editor (EditedGoal, a 0x338-byte record) - its level, texts,
// type, points (the pro, the start, the letters / cone checkpoints) and the type's own settings (time, score, laps,
// trick settings, key combo sets, gaps) - and how it's read from / written into script structures and placed.
namespace park_editing {

// A point of an edited goal (0x1C bytes).
struct EditedGoalPoint {
    // 1 the pro, 2 the start (restart), 3 an object (a letter), 4 a checkpoint (the cone goal's).
    int kind;        // +0x00
    maths::Vec4 pos; // +0x04
    float height;    // +0x14 above pos
    float angle;     // +0x18 heading (radians)
};
static_assert(sizeof(EditedGoalPoint) == 0x1C, "layout must match the original");

// A key combo set of a trick goal (num_taps etc.).
struct KeyComboSet {
    uint8_t set;               // +0x0 (key_combo_set)
    uint8_t must_be_flawless_; // +0x1 (the flag 0x687E2C25)
    uint8_t spin;              // +0x2 in half turns (spin / 180)
    uint8_t tap_total_;        // +0x3
};

// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class): held in the original's integer fields (to convert with them)
enum EditedGoalPointKind : uint8_t { kPointPro = 1, kPointStart = 2, kPointObject = 3, kPointCheckpoint = 4 };

// The goal types (their names' checksums).
constexpr uint32_t kGoalTypeSkate = 0x54166ACD, kGoalTypeCombo = 0x4EC3CFB5, kGoalTypeRace = 0x25904450,
                   kGoalTypeComboTricks = 0x0FC6F698, kGoalTypeGap = 0x61C5D092, kGoalTypeScore = 0x6FE44C6D,
                   kGoalTypeTrickAttack = 0x7FE2238A, kGoalTypeKeyCombo = 0xD0DD047B, kGoalTypeTricktris = 0xEA7BA666,
                   kGoalTypeHighCombo = 0xEC414B76;
// The park editor's level (Load_Sk5Ed): its goals go with the park.
constexpr uint32_t kParkEditorLevel = 0xE8B4B836;

// An edited goal (0x338 bytes; constructed by 0x004E93C0, its defaults by init_edited_goal 0x004E69D0).
class EditedGoal {
public:
    // 0x004E93C0 (not a Ghidra function: the goal editor's array constructor's element function): its defaults
    // (init_edited_goal).
    EditedGoal();

    // 0x004E6B80: `gap` taken out of the gap list (the rest moved down), if it's there.
    void drop_gap_entry(uint8_t gap);
    // 0x004E6BF0: `gap` taken out, and every later gap number one less (a gap removed from the park).
    void remove_gap_and_renumber(uint8_t gap);
    // 0x004E7390: `gap` added to the gap list unless it's there ("Warning! MAX_GAPS limit reached!" with ten).
    void register_gap_entry(uint8_t gap);
    // 0x004E6C40: the key combo set `set` given (replaced if there, else added - no limit checked); spin in degrees.
    void add_key_combo_set(int set, int must_be_flawless, int spin, int tap_total);
    // 0x004E6CD0: the key combo set `set` taken out (the rest moved down), if it's there.
    void remove_key_combo_set(int set);
    // 0x004E6D50: the key combo set `set`, or null.
    KeyComboSet* find_key_combo_set(int set);

    // 0x004E6DA0: the goal's type: its points' kinds and how many it takes, then the type's defaults structure's
    // time limit, score and text (goal_type_structure).
    void change_species(uint32_t type);
    // 0x004E6F70: the time limit (a trick goal's "stop adding tricks" time kept 5 below it).
    void set_countdown_seconds(int seconds);

    // 0x004E7050: the marker object of point `index` made (the script 0xEFC08BAE, with its kind's model, place,
    // height and heading).
    void place_goal_marker(int index);
    // 0x004E7780: the markers of every set point but the pro made again; the first point current, not won.
    void create_markers();
    // 0x004E7210: whether `pos` is too near another set point (one radius between a letter and the start, another
    // otherwise; the current point isn't counted).
    bool near_other_point(const maths::Vec4& pos);
    // 0x004E7300: whether a point can go at `pos` raised by `height` (inside the park in the park editor; not too
    // near another point).
    bool can_place_point(const maths::Vec4& pos, float height);
    // 0x004E77C0: the current point placed at `pos` (false if it can't go there): its marker made, the next point
    // current (starting where this one is).
    bool place_point(const maths::Vec4& pos, float height, float angle);
    // 0x004E7960: the current point's place, height and heading. True.
    bool get_current_point(maths::Vec4* pos, float* height, float* angle);
    // 0x004E79C0: point `index`'s node (`nodeName`) given the point's Pos and Angles (and a letter's type / model).
    void write_node_position(int index, uint32_t node_name);
    // 0x004E7A80: every set point's node (TRG_EditedGoal<n>_Pro / _Restart / _Object_<k>) given its position.
    void emit_node_coords();

    // 0x004E73F0: the key combo sets from the parameters' array 0x0490F171.
    void read_key_combo_sets(script::Struct* params);
    // 0x004E74F0: a trick goal's settings (acceleration, trick time, max tricks, spin, key combo sets).
    void read_trick_settings(script::Struct* params);
    // 0x004E7590: the type's own settings from the parameters (set_goal_specific_params).
    void read_specific_params(script::Struct* params);

    // 0x004E93D0: point `index` dropped onto what's below it (a line from 10000 above to 10000 below; missed, from
    // 20000 above), if anything.
    void drop_point_to_ground(int index);
    // 0x004E9590: every set point dropped to the ground and its object made there (the script 0x79582240 with its
    // node's Name - none for the start - and Pos).
    void place_points_on_ground();
    // 0x004EB550: the current point (not the pro's) placed again at `pos` (false if it can't go there), and the
    // point before it current; back at the first, the script 0xD066A889 run with 0x7F1538E0.
    bool place_point_and_step_back(const maths::Vec4& pos, float height, float angle);

    // 0x004E96D0: the goal from a structure (GetEditedGoalsInfo's entries, the CreatedGoals save): its defaults, then
    // level, number, type, score, time, control, texts, points and the type's settings. Marked used, not won.
    void decode_from_struct(script::Struct* params);
    // 0x004EB260: the goal into a structure (decode_from_struct's names).
    void export_to_struct(script::Struct* out);
    // 0x004E9BC0: the key combo sets as the array 0x0490F171 (none: nothing).
    void write_key_combo_sets(script::Struct* out);
    // 0x004E98F0: the key combo sets as the goal manager wants them: every key combo of each set (from the global
    // array 0xDF1451EB) with the set's taps, perfect flag and the goal's spin - no sets: emergency_key_combos.
    void write_goal_key_combos(script::Struct* out);
    // 0x004E9D60: a trick goal's settings; `for_goal_manager` (1): the key combos for the goal manager.
    void write_trick_settings(script::Struct* out, int for_goal_manager);
    // 0x004E9E00: the type's own settings; `for_goal_manager` (1): as the goal manager wants them.
    void write_specific_params(script::Struct* out, int for_goal_manager);
    // 0x004EA850: the goal manager's parameters for the goal: the type's structures, its nodes (pro, restart,
    // letters / checkpoints), its settings, texts, win message, id CreatedGoal<n>, time and control.
    void write_goal_manager_params(script::Struct* out);
    // 0x004EB6B0: the goal added to the goal manager as CreatedGoal<n> (seen; won again unless `unwin`).
    void add_to_goal_manager(bool unwin);

    uint32_t level_{};                    // +0x000 the level's load script
    char label_[0x32]{};                  // +0x004 view_goals_text
    char ped_name_[0x15]{};               // +0x036
    char summary_text_[0x64]{};           // +0x04B goal_description
    char win_message_[0x65]{};            // +0x0AF
    uint32_t kind_tag_{};                 // +0x114
    EditedGoalPoint path_vertices_[13]{}; // +0x118
    uint8_t max_points_{};                // +0x284
    uint8_t min_points_{};                // +0x285 (complete with this many)
    uint8_t vertex_total_{};              // +0x286 set
    uint8_t current_point_{};             // +0x287
    bool last_point_placed_{};            // +0x288
    bool won_{};                          // +0x289
    bool taken_count_{};                  // +0x28A
    uint8_t pad28b_{};
    int level_goal_number_{};          // +0x28C its number in its level (CreatedGoal<n>)
    int time_limit_{};                 // +0x290
    uint32_t control_type_{};          // +0x294
    int points_held_{};                // +0x298
    int race_total_laps_{};            // +0x29C
    int race_per_checkpoint_time_{};   // +0x2A0
    int acceleration_interval_{};      // +0x2A4
    float acceleration_percent_{};     // +0x2A8
    int trick_time_{};                 // +0x2AC
    int time_to_stop_adding_tricks_{}; // +0x2B0
    int max_tricks_{};                 // +0x2B4
    int twist_rate_{};                 // +0x2B8
    bool single_combo_{};              // +0x2BC
    uint8_t pad2bd_[3]{};
    int chain_length_{};               // +0x2C0
    int stack_block_span_{};           // +0x2C4
    int tricktris_total_to_win_{};     // +0x2C8
    KeyComboSet key_combo_sets_[10]{}; // +0x2CC
    int num_key_combo_sets_{};         // +0x2F4
    uint8_t gaps_[10]{};               // +0x2F8
    uint8_t num_gaps_{};               // +0x302
    char required_trick_[0x35]{};      // +0x303
};
static_assert(offsetof(EditedGoal, kind_tag_) == 0x114 && offsetof(EditedGoal, path_vertices_) == 0x118 &&
                  offsetof(EditedGoal, max_points_) == 0x284 && offsetof(EditedGoal, level_goal_number_) == 0x28C &&
                  offsetof(EditedGoal, single_combo_) == 0x2BC && offsetof(EditedGoal, key_combo_sets_) == 0x2CC &&
                  offsetof(EditedGoal, num_key_combo_sets_) == 0x2F4 && offsetof(EditedGoal, gaps_) == 0x2F8 &&
                  offsetof(EditedGoal, required_trick_) == 0x303 && sizeof(EditedGoal) == 0x338,
              "layout must match the original");

// 0x004E8F50: the goal editor's nodes (ten goals' TRG_EditedGoal<n>_Pro / _Restart / _Object_0..10, from the
// structures 0x8F0F67D7 / 0xC9119673 / 0x000B52B3, numbered from *index) on `heap`: into `nodes` from *index
// (the structures already there) - or, without `nodes`, added to the node manager's last node array (grown by 0x82)
// and hashed. ("Inserting Goal Editor Nodes")
void add_goal_editor_nodes(script::Array* nodes, int* index, memory::Arena* heap);

// The goal editor component ("goaleditor", type 0x81F01058, vtable 0x0064B534, 0xA114 bytes; made by 0x004EB7F0) of
// the permanent object GoalEditor: every created goal of every level (50 slots), the one being edited, and the
// object's input and editor camera components (the cursor). Its script member functions are the table at 0x00684270
// (filled by 0x00643960).
//
// Vtable: 0 Destroy 0x004E7B20; 1 Update 0x004EBD30; 2 the empty 0x005BAB90; 3 Finalize 0x004E8210; 8 Hide
// 0x004EBC40; the rest shared.
class ObjectiveAuthoring final : public objects::ObjectModule {
public:
    ObjectiveAuthoring(const ObjectiveAuthoring&) = delete;
    ObjectiveAuthoring& operator=(const ObjectiveAuthoring&) = delete;
    // 0x004EB7F0: a new one (0 if out of memory).
    static gsl::owner<objects::ObjectModule*> fabricate();
    // 0x004E6FB0 (not a Ghidra function): its script member functions (the table at 0x00684270) registered.
    static void publish_method_table();
    // 0x004EA230: its type, every goal's defaults (the array constructor with
    // 0x004E93C0), the goals cleared; no input or camera yet.
    ObjectiveAuthoring();
    // 0x004E6FD0: the destructor body: the script 0xFCC60D8B run, then the base's.
    ~ObjectiveAuthoring() override;
    int invoke_method(script::MemberFunctionEntry* entry, script::Struct* params, script::Script* script) override
    {
        return dispatch_script_method(entry, params, script); // (slot 11: 0x0042BB20, shared)
    }
    // 0x004E8210 (vtable slot 3): the object's input (0x27D7CD28) and editor camera components found.
    void seal() override;
    // 0x004EBD30 (vtable slot 1): with a goal being edited, the input's two buttons (+0x1C0: place the current point
    // at the cursor and go on; +0x1A0: place it and step back) - refused near another goal's pro / start (the
    // scripts 0xAEBC7020 / 0xADE100D6), else placed (0x718B071B / 0xFA275CC5, the cursor refreshed and moved;
    // the last point: 0xC05E7E20); then the cursor script.
    void tick_frame() override;
    // 0x004EBC40 (vtable slot 8, not a Ghidra function): hidden - the script 0xFCC60D8B; shown - the input
    // component given 0x67E6859A = 0 and the cursor refreshed.
    void conceal(bool hide) override;
    // 0x004EC010 [GetEditedGoalsInfo]: EditedGoalsInfo = the level's (0x651533EC, any without) complete goals.
    bool get_edited_goals_info(script::Struct* params, script::Script* script);

    // 0x004E8470: the goal editor (the object GoalEditor's component).
    static ObjectiveAuthoring* get_pull();

    // 0x004E7BE0: the current level's goal named `name` (view_goals_text, any case), or null.
    EditedGoal* find_goal_by_name(const char* name);
    // 0x004E7C60: the current level's goal whose CreatedGoal<n> is `id`, or null.
    EditedGoal* find_goal_by_id(uint32_t id);
    // 0x004E7D00: the goal the parameters name (goal_id, a goal name 0xBFECC45B, a number 0x474A6A7F), or the one
    // being edited.
    EditedGoal* get_goal_from_params(script::Struct* params);
    // 0x004E7E00: how many slots are used.
    int count_used_goals();
    // 0x004E7E50: how many goals of `level` (any with 0) have all their points.
    int count_complete_goals(uint32_t level);
    // 0x004E8130: whether point `index` of any goal of `level` is within the radius 0x71B20276 of `pos` raised by
    // `height`.
    bool point_near_any_goal(const maths::Vec4& pos, float height, uint32_t level, int index);
    // 0x004E8240: every goal outside the park editor cleared; none current.
    void clear_level_goals();
    // 0x004E8280: every goal of the park editor cleared; none current.
    void clear_park_goals();
    // 0x004E82C0: every goal without all its points cleared.
    void clear_incomplete_goals();
    // 0x004E8300: whether any park goal's start lies outside x0..x1, z0..z1.
    bool any_park_goal_outside(float x0, float z0, float x1, float z1);
    // 0x004E8390: the park goals whose start lies outside x0..x1, z0..z1 removed.
    void remove_park_goals_outside(float x0, float z0, float x1, float z1);
    // 0x004E8420: gap `gap` removed from the park's gap goals (later gaps renumbered).
    void remove_gap_from_park_goals(int gap);
    // 0x004E8090: `goal` taken out of the goal manager (if it's there) and cleared; no longer current.
    void drop_objective(EditedGoal* goal);
    // 0x004E7B50: the editor camera's simple collision on for a letter / checkpoint point, else in the park editor.
    void update_camera_collision();
    // 0x004E7F30: the editor camera's cursor put on the current point.
    void move_cursor_to_current_point();
    // 0x004E7FC0: the script 0xDEA7FE56 run with the cursor's place (raised by its height) and heading.
    void run_cursor_script();
    // 0x004EA580: the cursor's model for the current point (the script 0x36791E3C on the object).
    void refresh_cursor();
    // 0x004EA6C0: a new goal in the first free slot (the level's lowest free number), current; incomplete goals
    // cleared first. Nothing without a free slot.
    void begin_fresh_objective();
    // 0x004EA780: the park's goals' points dropped to the ground.
    void place_park_goals_on_ground();
    // 0x004EA470: the goals from the structure's array 0x38DBE1D0 (`park_only`: the park editor's, else the others'),
    // the matching goals cleared first.
    void read_goals(script::Struct* params, int park_only);
    // 0x004EA2C0: `level`'s goals from a written structure of `size` bytes (the park's, from a buffer). Returns the
    // buffer's end. (Not original: the size, and a structure without the goals' array leaves the goals cleared.)
    const uint8_t* read_level_goals_from_buffer(uint32_t level, const uint8_t* buffer, size_t size);
    // 0x004EB850: every goal outside the park editor into the array 0x38DBE1D0 (none: nothing).
    void write_goals(script::Struct* out);
    // 0x004EBA10: `level`'s goals into the array 0x38DBE1D0 (sized by the complete ones).
    void write_level_goals(uint32_t level, script::Struct* out);
    // 0x004EBB70: `level`'s goals written into `buffer` (emit_into_buffer's size).
    uint32_t write_level_goals_to_buffer(uint32_t level, uint8_t* buffer, uint32_t size);

    // --- script member functions (the table at 0x00684270; most aren't Ghidra functions). "The goal": the one the
    // parameters name (get_goal_from_params). ---
    // 0x004EA7C0 [NewEditorGoal]: a new goal; its goal_id returned in the script's parameters. True.
    bool new_editor_goal(script::Struct* params, script::Script* script);
    // 0x004E8490 [EditedGoalAddGap]: the goal's gap 0x3AC9AB5E added. True.
    bool edited_goal_add_gap(script::Struct* params, script::Script* script);
    // 0x004E84E0 [EditedGoalRemoveGap]: the goal's gap 0x3AC9AB5E taken out. True.
    bool edited_goal_remove_gap(script::Struct* params, script::Script* script);
    // 0x004E8530 [EditedGoalGotGap]: whether the goal has the gap 0x3AC9AB5E (no goal: false).
    bool edited_goal_got_gap(script::Struct* params, script::Script* script);
    // 0x004E8590 [AddKeyComboSet]: the goal's key combo set (key_combo_set, spin, num_taps, the perfect flag). True.
    bool add_key_combo_set(script::Struct* params, script::Script* script);
    // 0x004E8630 [RemoveKeyComboSet]. True.
    bool remove_key_combo_set(script::Struct* params, script::Script* script);
    // 0x004E8680 [GetKeyComboSet]: the set's spin, num_taps and perfect flag into the script's parameters (false
    // without the set; true without the goal).
    bool get_key_combo_set(script::Struct* params, script::Script* script);
    // 0x004E8720 [RemoveGoalSpecificFlag]: the combo goal's single-combo flag cleared if it's the flag named. True.
    bool remove_goal_specific_flag(script::Struct* params, script::Script* script);
    // 0x004E8780 [set_goal_specific_params]: the goal's type settings from the parameters. True.
    bool set_goal_specific_params(script::Struct* params, script::Script* script);
    // 0x004E87A0 [flag_goal_as_won]. True.
    bool flag_goal_as_won(script::Struct* params, script::Script* script);
    // 0x004E87C0 [FindUnfinishedGoal]: the last incomplete goal of the type made current; whether there is one.
    bool find_unfinished_goal(script::Struct* params, script::Script* script);
    // 0x004E8900 [GoalExists]
    bool goal_exists(script::Struct* params, script::Script* script);
    // 0x004E8920 [RemovedCreatedGoal]: the goal removed. True.
    bool removed_created_goal(script::Struct* params, script::Script* script);
    // 0x004E8950 [GoalHasAllPositionsSet] (no goal: false)
    bool goal_has_all_positions_set(script::Struct* params, script::Script* script);
    // 0x004E8980 [SetCurrentEditorGoal]. True.
    bool set_current_editor_goal(script::Struct* params, script::Script* script);
    // 0x004E89A0 [SetEditorGoalType]: the goal's type. True.
    bool set_editor_goal_type(script::Struct* params, script::Script* script);
    // 0x004E89F0 [set_editor_goal_description]: the goal's text. True.
    bool set_editor_goal_description(script::Struct* params, script::Script* script);
    // 0x004E8A40 [set_editor_goal_win_message]. True.
    bool set_editor_goal_win_message(script::Struct* params, script::Script* script);
    // 0x004E8AA0 [GetNumEditedGoals]: NumGoals - the level's complete goals (any level's; the flag 0xB99F6330: but
    // the park's). True.
    bool get_num_edited_goals(script::Struct* params, script::Script* script);
    // 0x004E8B30 [GetCurrentEditedGoalId]: goal_id = the current goal's CreatedGoal<n>, removed without one. True.
    bool get_current_edited_goal_id(script::Struct* params, script::Script* script);
    // 0x004E8B90 [write_edited_goal_node_positions]. True.
    bool write_edited_goal_node_positions(script::Struct* params, script::Script* script);
    // 0x004E8BB0 [set_editor_goal_name]: with a current goal and Name: the goal named made current and given the name.
    bool set_editor_goal_name(script::Struct* params, script::Script* script);
    // 0x004E8C20 [set_editor_ped_name]: the same for the ped's name.
    bool set_editor_ped_name(script::Struct* params, script::Script* script);
    // 0x004E8C90 [MaxEditedGoalsReached]: every slot used, or ten complete goals in the level.
    bool max_edited_goals_reached(script::Struct* params, script::Script* script);
    // 0x004E8CF0 [NukeAllGoals]: the park's goals (the flag 0x98439808) or the others cleared. True.
    bool nuke_all_goals(script::Struct* params, script::Script* script);
    // 0x004E8D30 [GetMaxGoalsPerLevel]: 0x8BC2E0DB = 10. True.
    bool get_max_goals_per_level(script::Struct* params, script::Script* script);
    // 0x004E8D50 [SetGoalScore]: the goal made current, its score (the unnamed integer). True.
    bool set_goal_score(script::Struct* params, script::Script* script);
    // 0x004E8DB0 [SetGoalTimeLimit]: the goal made current, its time limit (the unnamed integer). True.
    bool set_goal_time_limit(script::Struct* params, script::Script* script);
    // 0x004E8E00 [SetGoalControlType]: the goal made current, its control type (the unnamed checksum). True.
    bool set_goal_control_type(script::Struct* params, script::Script* script);
    // 0x004E8E60 [RefreshGoalCursorPosition]: the cursor script run at the current point. True.
    bool refresh_goal_cursor_position(script::Struct* params, script::Script* script);
    // 0x004EA810 [EditGoal]: the goal made current, its markers made, the cursor on its first point. True.
    bool edit_goal(script::Struct* params, script::Script* script);
    // 0x004EBFF0 [get_current_edited_goal_info]: the current goal into the script's parameters. True.
    bool get_current_edited_goal_info(script::Struct* params, script::Script* script);
    // 0x004EC1D0 [AddEditedGoalToGoalManager]: the goal added to the goal manager (won again unless 0xC6322A25).
    bool add_edited_goal_to_goal_manager(script::Struct* params, script::Script* script);
    // 0x004EC200 [0xD5F6EA3B]: every goal outside the park editor into the script's parameters. True.
    bool get_created_goals(script::Struct* params, script::Script* script);

    EditedGoal goals_[50];               // +0x0018
    EditedGoal* active_node_{};          // +0xA108 the one being edited
    objects::ObjectModule* input_ptr_;   // +0xA10C the object's input component
    objects::ObjectModule* view_camera_; // +0xA110 its editor camera
};
static_assert(offsetof(ObjectiveAuthoring, goals_) == 0x18 && offsetof(ObjectiveAuthoring, active_node_) == 0xA108 &&
                  sizeof(ObjectiveAuthoring) == 0xA114,
              "layout must match the original");

} // namespace park_editing

namespace park_editing {
// The script member table's records (their code is filled in by engine/script/member_function_code.cpp).
extern uint32_t g_goal_editor_member_functions[216];
} // namespace park_editing
