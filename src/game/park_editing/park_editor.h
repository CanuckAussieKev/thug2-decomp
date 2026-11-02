#pragma once

// The park editor module (park_editing::ParkBuilderMode, 0x7C bytes, the singleton 0x007CE5D8 with its reference count
// 0x007CE5DC; single::LazyInstance's Create 0x00590160 makes it, Release 0x0058D290 lets it go): the Create-A-Park
// mode itself. A module (vtable 0x0064F234: the scalar deleting destructor, start and stop) and an event listener
// (+0x14, vtable 0x0064F22C) for the menus' pad events; a task that runs the editor each frame (0x0058FFC0 ->
// logic_update), an empty display task and an input handler (process_pad_input) on one controller. Its state says what
// the park is doing: off (a normal level), edit (the editor up), test_play (skating the park from the editor)
// or regular_play (a created park loaded as a level).
//
// Here: 0x0058D290-0x0058FBD0 - the module's code, the camera and the controls, and the park editor's script
// commands (park_editor.cpp, park_editor_cfuncs.cpp). The constructor (0x0058FFF0), SetParkEditorState and
// SetParkEditorPauseMode follow at 0x0058FBD0.

#include <memory>
#include <cstddef>
#include <cstdint>
#include <array>

#include "game/park_editing/park.h"
#include "engine/objects/signal.h"
#include "engine/pad_input/input_hub.h"
#include "engine/sys/game_module.h"

namespace pad_input {
struct Payload;
}
namespace maths {
class Vec4;
}

namespace park_editing {

class ParkBuilderMode;

// The editor's tasks and its input handler (on controller 0 at the start).
using ParkEditorTask = tasks::Task<ParkBuilderMode>;
using ParkEditorInputHandler = pad_input::Responder<ParkBuilderMode>;

extern ParkBuilderMode* g_park_editor;
extern uint32_t g_park_editor_references;

// The park editor module (vtable 0x0064F234).
class ParkBuilderMode final : public modules::PartUnit {
public:
    ParkBuilderMode(const ParkBuilderMode&) = delete;
    ParkBuilderMode& operator=(const ParkBuilderMode&) = delete;
    // cur_phase_
    // NOLINTNEXTLINE(cppcoreguidelines-use-enum-class): held in the original's integer fields (to convert with them)
    enum State : uint32_t {
        kStateOff = 0xD443A2BC,         // off
        kStateEdit = 0x7ECA21E5,        // edit
        kStateTestPlay = 0xA40F0B8A,    // test_play
        kStateRegularPlay = 0xCD3682C1, // regular_play
    };
    // flag_bits_
    static constexpr uint32_t kFlagPaused = 0x2;
    // actions_: what the controls asked for this frame (the input handler's buttons, the menu events).
    static constexpr uint32_t kCommandTurnLeft = 0x2;          // Circle: the piece (or the gap) a quarter turn back
    static constexpr uint32_t kCommandTurnRight = 0x4;         // Square
    static constexpr uint32_t kCommandPreviousPiece = 0x8;     // pad_left
    static constexpr uint32_t kCommandNextPiece = 0x10;        // pad_right
    static constexpr uint32_t kCommandPlace = 0x20;            // X
    static constexpr uint32_t kCommandRemove = 0x40;           // Triangle
    static constexpr uint32_t kCommandRaise = 0x80;            // L1
    static constexpr uint32_t kCommandLower = 0x100;           // L2
    static constexpr uint32_t kCommandPreviousSet = 0x200;     // pad_up
    static constexpr uint32_t kCommandNextSet = 0x400;         // pad_down
    static constexpr uint32_t kCommandWidenGapRight = 0x800;   // L1 (with a gap)
    static constexpr uint32_t kCommandNarrowGapRight = 0x1000; // L2
    static constexpr uint32_t kCommandZoomOut = 0x2000;        // button 0x10000 held
    static constexpr uint32_t kCommandZoomIn = 0x4000;         // button 0x20000 held
    static constexpr uint32_t kCommandWidenGapLeft = 0x8000;   // button 0x10000 pressed
    static constexpr uint32_t kCommandNarrowGapLeft = 0x10000; // button 0x20000 pressed

    static ParkBuilderMode*& sole() { return g_park_editor; }
    static uint32_t& ref_tally() { return g_park_editor_references; }

    // --- the module (park_editor.cpp) ---
    // 0x0058D290: single::LazyInstance<ParkBuilderMode>'s release: a reference dropped; the last deletes the instance.
    static void get_release();
    // 0x0058D2D0 / 0x0058DAA0: the park manager deleted, the listener, the module (the tasks and the handler aren't
    // deleted - as in the original).
    ~ParkBuilderMode() override;
    // 0x0058DAD0: the logic task onto the main loop's logic tasks, the input handler onto its controller's
    // handlers, the display task onto the display tasks.
    void start_callback() override;
    // 0x0058D340: the logic task and the input handler off their lists (not the display task).
    void stop_callback() override;
    // 0x0058DB30: back to a normal level: state off; the cursor, the piece menu and the park taken down, the gap
    // manager's copy deleted, the park's node array gone (0x00592C90).
    void get_leave();
    // 0x0058D360: CustomParks\custom<slot>.prk saved, or loaded (the cursor's piece and the clipboards' previews
    // taken away first; then, unless the level is a normal one or `defer_regen`, the park rebuilt - or the level
    // changed to load_sk5ed again if the park's shell size changed).
    void transfer_park_file(bool save, int slot, bool defer_regen);
    // 0x0058D4E0: a whole compressed map taken (loaded or received); while editing or test playing, the park
    // rebuilt from it (or the level changed to load_sk5ed again if the shell size isn't `shell_size`).
    void apply_map(const CompressedMap* map, int shell_size);
    // The park manager's own, for the rest of the game (`this` isn't used):
    void export_to_struct(script::Struct* params);                         // 0x0058D600
    void decode_from_struct(script::Struct* params, bool apply_to_editor); // 0x0058D620
    void get_map_info(const CompressedMap* map, int* num_gaps, int* num_metas, uint16_t* shell_size, uint32_t* theme,
                      int* width, int* length);                           // 0x0058D640
    void write_compressed_map_if_built();                                 // 0x0058D680
    CompressedMap* get_packed_park_data(bool flag_clean);                 // 0x0058D690
    void set_packed_park_data(const CompressedMap* map, bool flag_dirty); // 0x0058D6B0
    // 0x0058D6D0: whether the main heap has room for `count` * `size` floor pieces (park_editing::main_heap_has_room).
    bool main_heap_has_room(int count, int size);
    // 0x0058D6F0: the cursor's position.
    maths::Vec4* get_cursor_pos();
    // 0x0058D700
    void set_daylight_script(uint32_t script);
    static uint32_t get_shell_checksum(); // 0x0058D710
    static const char* get_park_name();   // 0x0058D720
    // 0x0058DCB0: the editor's look on (or off): the generator's pieces (0x00591CF0 with 0), the highlights and
    // the restart flags shown; the trick text paused and the goal points hidden (or the other way round).
    void show_editor_view(bool on);
    // 0x0058DD70: the camera: the rail editor's while placing rails, else parked_cam.
    static void switch_camera();
    // 0x0058DDC0: the park is at its memory limit (the largest figure at 1 or more).
    int is_park_full();
    // 0x0058DDE0: the piece menu shows the cursor's set again and the scripts are told the mode.
    void refresh_piece_menu();
    // 0x0058DEC0: whether `pos` (x and z) lies inside the park more than `margin` from its edges.
    static bool is_inside_park(maths::Vec4 pos, float margin);
    // 0x0058DF80: the generator's restart point search (0x00592A10).
    static bool find_restart(void* a, void* b, void* c);
    // 0x0058DFA0: the park's player count.
    static int get_player_limit();
    // 0x0058EBE0: the menu commands: the previous / next set, the previous / next piece (the piece menu refreshed
    // and its container told).
    void move_selection_cursor();
    // 0x0058ED50: a frame of editing: the camera (parked_cam) placed from the cursor, its angle, tilt and
    // distance; then (not paused) the sticks move the cursor and turn the camera, and the commands are carried out.
    void tick_frame();
    // 0x0058F640: the left stick works the menus unless editing (not paused).
    void update_menu_stick();
    // 0x0058F6C0: the input handler onto controller `controller`'s handlers.
    void link_to_pad(int controller);
    // 0x0058F8A0: the logic task's work each frame: the skaters paused while editing (unpaused in test play);
    // editing with a cursor: the rail editor's scripts, Update (or, placing rails, only move_selection_cursor) and the
    // memory bar; else the commands dropped.
    void logic_update();
    // 0x0058FA50: the state becomes `state` (unless 0): the cursor shown while editing; the editor's look on, or
    // (test play, regular play) off with the gaps listed; the front end's auto-repeat (320 ms; 100 or 50); the menu
    // stick.
    void change_phase(uint32_t state);

    // --- park_editor_state.cpp (0x0058FBD0-0x005901AA) ---
    // 0x0058FFF0: the constructor (state off; the logic task, the empty display task, the input handler, the
    // listener; the park manager made).
    ParkBuilderMode();
    // 0x00590160: single::LazyInstance<ParkBuilderMode>'s Create: the first call with `create` makes the instance; every
    // call takes a reference and returns it (null if there is none).
    static ParkBuilderMode* fabricate(bool create);
    // 0x0058FFC0: the logic task's code: the park editor's clock (0x007CE5EC) moved on, then logic_update.
    static void front_end_tick(ParkEditorTask* task);

    // The editor's event listener (the original's vtable 0x0064F22C; its destructor slot, 0x0058DA90, deletes the
    // editor, which nothing does through the listener).
    struct Listener final : objects::SignalReceiver {
        // The editor it's the member of: the one instance (the original found it 0x14 back from this).
        static ParkBuilderMode* get_editor() { return sole(); }
        // 0x0058DE40: the menus' pad events (to root_window) while editing with a cursor, not paused: up / down the
        // previous / next set, left / right the previous / next piece.
        void receive(objects::Signal* event) override;
    };

    Listener listener_;    // +0x14
    bool action_finished_; // +0x30: a command that changes the park was carried out
    std::array<uint8_t, 3> pad31{};
    std::unique_ptr<ParkEditorTask> editor_update_job_;           // +0x34 (0x0058FFC0, priority 0)
    std::unique_ptr<ParkEditorTask> draw_job_;                    // +0x38 (the empty 0x005480A0, priority -1000)
    std::unique_ptr<ParkEditorInputHandler> editor_pad_listener_; // +0x3C (process_pad_input, priority 0)
    uint32_t cur_phase_;                                          // +0x40
    uint32_t actions_{0};                                         // +0x44
    uint32_t flag_bits_;                                          // +0x48
    float move_speed_;                                            // +0x4C (480)
    float turn_speed_;                                            // +0x50 (120)
    float pad_rx_{};           // +0x54: the sticks (-128..127, then their dead zone taken off)
    float pad_ry_{};           // +0x58
    float pad_lx_{};           // +0x5C
    float pad_ly_{};           // +0x60
    int unknown64_;            // +0x64 (0)
    float camera_angle_;       // +0x68: degrees round y (0-360)
    float camera_tilt_;        // +0x6C: degrees down (10-90; 10 to start with)
    float orbit_radius_;       // +0x70 (500-1500; 1400 to start with)
    int last_time_{};          // +0x74: frame_clock::GetTime at the last Update
    uint32_t lighting_script_; // +0x78 (SetParkEditorTimeOfDay's tod_script)
};
static_assert(offsetof(ParkBuilderMode, listener_) == 0x14 && offsetof(ParkBuilderMode, action_finished_) == 0x30 &&
                  offsetof(ParkBuilderMode, editor_pad_listener_) == 0x3C &&
                  offsetof(ParkBuilderMode, cur_phase_) == 0x40 && offsetof(ParkBuilderMode, move_speed_) == 0x4C &&
                  offsetof(ParkBuilderMode, camera_angle_) == 0x68 &&
                  offsetof(ParkBuilderMode, lighting_script_) == 0x78 && sizeof(ParkBuilderMode) == 0x7C,
              "layout must match the original");

// 0x0058DB70: the input handler's code: the sticks and the buttons as commands (cleared each frame).
void process_pad_input(ParkEditorInputHandler* handler);
// 0x0058E170: the area ResizePark is asked for without a structure: the park's bounds with w= / l= (or inc_w= /
// inc_l=), kept centred. Returns `out`.
CellBox* get_resize_area(CellBox* out, script::Struct* params);
// 0x0058E490: the park resized to `area`: the shell rebuilt round it, the park rebuilt, the rails' points and
// the goals outside it removed, the clipboards too big deleted, the cursor hidden, the collision rebuilt.
void resize_to(CellBox area);

// The park editor's script commands (park_editor_cfuncs.cpp; cdecl, the Function table's).
bool cfunc_free_up_memory_for_playing_park(script::Struct* params, script::Script* script); // 0x0058D730
bool cfunc_is_park_unsaved(script::Struct* params, script::Script* script);                 // 0x0058D760
bool cfunc_set_edited_park_gap_info(script::Struct* params, script::Script* script);        // 0x0058D780
bool cfunc_park_editor_selection_area_too_big_to_copy(script::Struct* params,
                                                      script::Script* script); // 0x0058D820
bool cfunc_copy_park_editor_selection_to_clipboard(script::Struct* params,
                                                   script::Script* script);                // 0x0058D840
bool cfunc_cut_park_editor_area_selection(script::Struct* params, script::Script* script); // 0x0058D870
bool cfunc_park_editor_area_selection_delete_pieces(script::Struct* params,
                                                    script::Script* script);                    // 0x0058D8C0
bool cfunc_continue_park_editor_area_selection(script::Struct* params, script::Script* script); // 0x0058D8E0
bool cfunc_set_editor_theme(script::Struct* params, script::Script* script);                    // 0x0058D900
bool cfunc_set_custom_park_name(script::Struct* params, script::Script* script);                // 0x0058D940
bool cfunc_optimize_park_geometry(script::Struct* params, script::Script* script);              // 0x0058D980
bool cfunc_get_park_ed_mem_stats(script::Struct* params, script::Script* script);               // 0x0058D9A0
bool cfunc_park_ed_mem_snapshot(script::Struct* params, script::Script* script);                // 0x0058D9B0
bool cfunc_enable_park_ed_cursor(script::Struct* params, script::Script* script);               // 0x0058D9E0
bool cfunc_exit_park_ed_test_play(script::Struct* params, script::Script* script);              // 0x0058DA40
bool cfunc_park_update_usage_info(script::Struct* params, script::Script* script);              // 0x0058DA70
bool cfunc_set_park_editor_time_of_day(script::Struct* params, script::Script* script);         // 0x0058DFB0
bool cfunc_get_park_editor_time_of_day_script(script::Struct* params, script::Script* script);  // 0x0058DFE0
bool cfunc_rebuild_park_node_array(script::Struct* params, script::Script* script);             // 0x0058E010
bool cfunc_get_park_editor_cursor_pos(script::Struct* params, script::Script* script);          // 0x0058E020
bool cfunc_custom_park_mode(script::Struct* params, script::Script* script);                    // 0x0058E070
bool cfunc_set_park_editor_max_players(script::Struct* params, script::Script* script);         // 0x0058E0F0
bool cfunc_can_cleanly_resize_park(script::Struct* params, script::Script* script);             // 0x0058E2D0
bool cfunc_resize_park(script::Struct* params, script::Script* script);                         // 0x0058E5E0
bool cfunc_get_current_park_bounds(script::Struct* params, script::Script* script);             // 0x0058E7E0
bool cfunc_can_change_park_dimension(script::Struct* params, script::Script* script);           // 0x0058E870
bool cfunc_save_park_to_disk(script::Struct* params, script::Script* script);                   // 0x0058E9A0
bool cfunc_load_park_from_disk(script::Struct* params, script::Script* script);                 // 0x0058E9E0
bool cfunc_fire_custom_park_gap(script::Struct* params, script::Script* script);                // 0x0058EA40
bool cfunc_get_edited_park_gap_name(script::Struct* params, script::Script* script);            // 0x0058EA70
bool cfunc_get_editor_theme(script::Struct* params, script::Script* script);                    // 0x0058EAA0
bool cfunc_get_editor_max_themes(script::Struct* params, script::Script* script);               // 0x0058EAD0
bool cfunc_get_custom_park_name(script::Struct* params, script::Script* script);                // 0x0058EAF0
bool cfunc_is_custom_park(script::Struct* params, script::Script* script);                      // 0x0058EB90
bool cfunc_is_park_full(script::Struct* params, script::Script* script);                        // 0x0058EBB0
bool cfunc_switch_park_editor_menu_piece_to_most_recent_clipboard(script::Struct* params,
                                                                  script::Script* script); // 0x0058ED00
bool cfunc_switch_to_park_editor_camera(script::Struct* params, script::Script* script);   // 0x0058F750
bool cfunc_bind_park_editor_to_controller(script::Struct* params, script::Script* script); // 0x0058F870
// (park_editor_state.cpp)
// 0x0058FBD0 SetParkEditorState state=<off|edit|test_play|regular_play> [command=...]: the park set up for the state
// and the editor's state changed. False for an unknown state.
bool cfunc_set_park_editor_state(script::Struct* params, script::Script* script);
// 0x0058FED0 SetParkEditorPauseMode pause | unpause: the editor paused (its menu and cursor hidden) or not.
bool cfunc_set_park_editor_pause_mode(script::Struct* params, script::Script* script);

extern bool g_collision_allowed;

} // namespace park_editing
