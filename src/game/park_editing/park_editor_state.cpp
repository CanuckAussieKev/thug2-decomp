// The park editor module's state commands (SetParkEditorState, SetParkEditorPauseMode), its constructor, its
// singleton's Create and its logic task's code (park_editing::ParkBuilderMode, park_editor.h; 0x0058FBD0-0x005901AA).

#include <memory>
#include <cstdint>
#include <new>

#include "game/park_editing/park_editor.h"

#include "game/park_editing/park.h"
#include "game/park_editing/piece_menu.h"
#include "game/park/park_memory.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "engine/sys/model_viewer.h"
#include "game/park_editing/park_editing_state.h"

namespace park_editing {

uint32_t& ParkBuilderMode::ref_tally()
{
    static uint32_t park_editor_references{};
    return park_editor_references;
}
namespace {

constexpr std::uint32_t kBuildFloor = 0x01625EDAu; // build_floor
constexpr std::uint32_t kClear = 0x1A4E0EF9u; // Clear
constexpr std::uint32_t kInitialize = 0x27F2117Du; // initialize
constexpr std::uint32_t kNewParkCommand = 0xE76F5250u;       // a new, empty park (text unknown)
constexpr std::uint32_t kRebuildFromMapCommand = 0x2A907CBCu; // rebuilt from the saved map (text unknown)
constexpr std::uint32_t kPausedMenuScript = 0x30251D65u;     // run when the editor pauses with the top bar up (text unknown)
constexpr int kDrawTaskPriority = 1000;                      // the display task runs at its negative
constexpr float kStartMoveSpeed = 480.0f;
constexpr float kStartTurnSpeed = 120.0f;
constexpr float kStartOrbitRadius = 1400.0f;
constexpr float kStartCameraTilt = 10.0f;

// The display task's code: 0x005480A0, the empty function.
void draw_nothing(ParkEditorTask* /*task*/) {}

// The park editor's clock (milliseconds).
uint32_t& editor_clock()
{
    static uint32_t editor_clock{};
    return editor_clock;
}

// A memory marker of the script's (memory_marker): the main heap's free memory as the park is set up.
constexpr uint32_t kParkLoadMarker = 0xCC63F828;

// Play: the park built for playing (the cursor gone, the collision, the node array, the gap checklist).
void build_for_play(ParkBuilderMode* editor, uint32_t state)
{
    ParkLayout* manager = ParkLayout::singleton();
    manager->rebuild_area(nullptr, false);
    mark_progress("Setting Park Play Mode"); // (0x0064F254)
    BuildPointer::teardown();
    manager->builder()->rebuild_collision();
    manager->generate_node_array();
    GapRegistry::singleton()->update_checklist();
    editor->change_phase(state);
}

// The cursor (if there is one) shown or hidden, and the scripts told its mode.
void show_cursor(bool visible)
{
    BuildPointer::singleton()->toggle_shown(visible);
    BuildPointer::singleton()->on_mode_changed();
}
// Shown unless it's placing rails.
bool cursor_shows() { return BuildPointer::singleton()->mode_sel() != BuildPointer::kModeRailPlacement; }

} // namespace

bool cfunc_set_park_editor_state(script::Struct* params, script::Script* /*script*/)
{
    ParkBuilderMode* editor = ParkBuilderMode::sole();
    uint32_t state = 0;
    params->read_hash_value("state", &state, 0);
    uint32_t command = 0;
    params->read_hash_value("command", &command, 0);
    switch (state) {
    case ParkBuilderMode::kStateRegularPlay: // a created park played as a level: set up from scratch first
        get_marker(kParkLoadMarker);
        ParkLayout::singleton()->commission(ParkBuilderMode::kStateRegularPlay);
        build_for_play(editor, ParkBuilderMode::kStateRegularPlay);
        return true;
    case ParkBuilderMode::kStateTestPlay:
        build_for_play(editor, ParkBuilderMode::kStateTestPlay);
        return true;
    case ParkBuilderMode::kStateOff: {
        editor->change_phase(ParkBuilderMode::kStateOff);
        editor->set_editor_state(ParkBuilderMode::kStateOff);
        BuildPointer::teardown();
        TopBarMenus::discard_occurrence();
        ParkLayout* manager = ParkLayout::singleton();
        manager->teardown(0);
        manager->destroy_gap_manager_copy();
        ParkAssembler::remove_node_array();
        editor->flag_bits() &= ~ParkBuilderMode::kFlagPaused;
        if (TopBarMenus::singleton()) {
            TopBarMenus::singleton()->turn_on();
            editor->refresh_piece_menu();
        }
        if (BuildPointer::singleton())
            show_cursor(cursor_shows());
        editor->update_menu_stick();
        return true;
    }
    case ParkBuilderMode::kStateEdit:
        switch (command) {
        case kInitialize: // the editor started: set up for its level, the menus and the cursor made
            get_marker(kParkLoadMarker);
            ParkLayout::singleton()->commission(ParkBuilderMode::kStateEdit);
            TopBarMenus::fabricate();
            BuildPointer::spawn_occurrence();
            break;
        case kNewParkCommand:   // a new, empty park
        case kClear:   // rebuilt as a new park
        case kRebuildFromMapCommand: { // rebuilt from the saved map
            ParkLayout* manager = ParkLayout::singleton();
            switch (command) {
            case kClear:
                BuildPointer::teardown();
                manager->rebuild_park(kClear);
                break;
            case kNewParkCommand:
                BuildPointer::teardown();
                manager->reset_park();
                break;
            case kBuildFloor: // (never: the outer switch doesn't let it through)
                manager->rebuild_area(nullptr, false);
                break;
            case kRebuildFromMapCommand:
                BuildPointer::teardown();
                manager->rebuild_park(command);
                break;
            default:
                break;
            }
            mark_progress("Setting Edit Mode"); // (0x0064F26C)
            ParkLayout::singleton()->builder()->set_player_cap(2);
            break;
        }
        default:
            break;
        }
        BuildPointer::spawn_occurrence();
        if (BuildPointer::singleton())
            BuildPointer::singleton()->clamp_to_park();
        editor->change_phase(ParkBuilderMode::kStateEdit);
        return true;
    default:
        return false;
    }
}

bool cfunc_set_park_editor_pause_mode(script::Struct* params, script::Script* /*script*/)
{
    ParkBuilderMode* editor = ParkBuilderMode::sole();
    if (params->has_bare_flag("pause")) {
        editor->flag_bits() |= ParkBuilderMode::kFlagPaused;
        if (TopBarMenus::singleton()) {
            script::launch_script(kPausedMenuScript, nullptr, nullptr, false, nullptr);
            TopBarMenus::singleton()->turn_off_now();
        }
        if (BuildPointer::singleton())
            show_cursor(false);
        editor->update_menu_stick();
    } else if (params->has_bare_flag("unpause")) {
        editor->flag_bits() &= ~ParkBuilderMode::kFlagPaused;
        if (TopBarMenus::singleton()) {
            TopBarMenus::singleton()->turn_on();
            editor->refresh_piece_menu();
        }
        if (BuildPointer::singleton())
            show_cursor(cursor_shows());
        editor->update_menu_stick();
    }
    return true;
}

void ParkBuilderMode::front_end_tick(ParkEditorTask* task)
{
    ParkBuilderMode* editor = task->content();
    const uint32_t before = editor_clock();
    const uint32_t now = frame_clock::get_time();
    editor_clock() += now - before; // (the clock re-read after the call, as the original)
    editor->logic_update();
}

ParkBuilderMode::ParkBuilderMode()
{
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): set twice, as the original does
    flag_bits_ = 0;
    // The logic task (priority 0), the display task (-1000: nothing to do) and the input handler.
    editor_update_job_ = std::make_unique<ParkEditorTask>(&front_end_tick, this, 0);
    draw_job_ = std::make_unique<ParkEditorTask>(&draw_nothing, this, -kDrawTaskPriority);
    editor_pad_listener_ = std::make_unique<ParkEditorInputHandler>(&process_pad_input, this, 0, 0);
    move_speed_ = kStartMoveSpeed;
    turn_speed_ = kStartTurnSpeed;
    unknown64_ = 0;
    orbit_radius_ = kStartOrbitRadius;
    camera_angle_ = 0.0f;
    camera_tilt_ = kStartCameraTilt;
    cur_phase_ = kStateOff;
    flag_bits_ = 0;
    lighting_script_ = 0;
    action_finished_ = false;
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
    ParkLayout::spawn_occurrence();
    listener_.enter_wait_board(nullptr);
}

ParkBuilderMode* ParkBuilderMode::fabricate(bool create)
{
    if (!sole() && create) {
        park_editing_state().park_editor() = std::make_unique<ParkBuilderMode>(); // (null when the heap is full)
        if (sole()) {
            ++ref_tally();
            return sole();
        }
    }
    ++ref_tally();
    return sole();
}

} // namespace park_editing
