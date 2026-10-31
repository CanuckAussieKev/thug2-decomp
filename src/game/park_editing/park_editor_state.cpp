// The park editor module's state commands (SetParkEditorState, SetParkEditorPauseMode), its constructor, its
// singleton's Create and its logic task's code (park_editing::ParkBuilderMode, park_editor.h; 0x0058FBD0-0x005901AA).

#include <cstdint>

#include "game/park_editing/park_editor.h"

#include "engine/core/mem.h"
#include "engine/core/stubs.h"
#include "engine/core/task_list.h"
#include "game/park_editing/park.h"
#include "game/park_editing/piece_menu.h"
#include "game/park/park_memory.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "engine/core/code_address.h"
#include "engine/net/task_vtable.h"
#include "engine/core/stubs.h"
#include "engine/sys/model_viewer.h"

namespace game_code {

// pad_input::Responder<T>'s vtable, shared by the park editor's, the viewer's and the observer's input handlers.
static VtableSlot g_input_handler_vtable_slots[] = {&modules::ViewerInputHandler::hdl, stubs::return_zero, &modules::ViewerInputHandler::teardown};
void** const kInputHandlerVtable = reinterpret_cast<void**>(g_input_handler_vtable_slots);

} // namespace game_code

namespace park_editing {

uint32_t g_park_editor_references;
uint32_t g_editor_clock;

namespace {

// tasks::Task<ParkBuilderMode>'s and pad_input::Responder<ParkBuilderMode>'s vtables.

// The park editor's clock (milliseconds).
uint32_t& editor_clock() { return g_editor_clock; }

// A memory marker of the script's (memory_marker): the main heap's free memory as the park is set up.
constexpr uint32_t kParkLoadMarker = 0xCC63F828;

// Play: the park built for playing (the cursor gone, the collision, the node array, the gap checklist).
void build_for_play(ParkBuilderMode* editor, uint32_t state)
{
    ParkLayout* manager = ParkLayout::singleton();
    manager->rebuild_area(nullptr, false);
    mark_progress("Setting Park Play Mode"); // (0x0064F254)
    BuildPointer::teardown();
    manager->builder_->rebuild_collision();
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
bool cursor_shows() { return BuildPointer::singleton()->mode_sel_ != BuildPointer::kModeRailPlacement; }

} // namespace

bool __cdecl cfunc_set_park_editor_state(script::Struct* params, script::Script* /*script*/)
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
        editor->cur_phase_ = ParkBuilderMode::kStateOff;
        BuildPointer::teardown();
        TopBarMenus::discard_occurrence();
        ParkLayout* manager = ParkLayout::singleton();
        manager->teardown(0);
        manager->destroy_gap_manager_copy();
        ParkAssembler::remove_node_array();
        editor->flag_bits_ &= ~ParkBuilderMode::kFlagPaused;
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
        case 0x27F2117D: // the editor started: set up for its level, the menus and the cursor made
            get_marker(kParkLoadMarker);
            ParkLayout::singleton()->commission(ParkBuilderMode::kStateEdit);
            TopBarMenus::fabricate();
            BuildPointer::spawn_occurrence();
            break;
        case 0xE76F5250: // a new, empty park
        case 0x1A4E0EF9: // rebuilt as a new park
        case 0x2A907CBC: { // rebuilt from the saved map
            ParkLayout* manager = ParkLayout::singleton();
            switch (command) {
            case 0x1A4E0EF9:
                BuildPointer::teardown();
                manager->rebuild_park(0x1A4E0EF9);
                break;
            case 0xE76F5250:
                BuildPointer::teardown();
                manager->reset_park();
                break;
            case 0x01625EDA: // (never: the outer switch doesn't let it through)
                manager->rebuild_area(nullptr, false);
                break;
            case 0x2A907CBC:
                BuildPointer::teardown();
                manager->rebuild_park(command);
                break;
            }
            mark_progress("Setting Edit Mode"); // (0x0064F26C)
            ParkLayout::singleton()->builder_->set_player_cap(2);
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

bool __cdecl cfunc_set_park_editor_pause_mode(script::Struct* params, script::Script* /*script*/)
{
    ParkBuilderMode* editor = ParkBuilderMode::sole();
    if (params->has_bare_flag("pause")) {
        editor->flag_bits_ |= ParkBuilderMode::kFlagPaused;
        if (TopBarMenus::singleton()) {
            script::launch_script(0x30251D65, nullptr, nullptr, false, nullptr);
            TopBarMenus::singleton()->turn_off_now();
        }
        if (BuildPointer::singleton())
            show_cursor(false);
        editor->update_menu_stick();
    } else if (params->has_bare_flag("unpause")) {
        editor->flag_bits_ &= ~ParkBuilderMode::kFlagPaused;
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

void __cdecl ParkBuilderMode::front_end_tick(ParkEditorTask* task)
{
    ParkBuilderMode* editor = task->content_;
    const uint32_t before = editor_clock();
    const uint32_t now = frame_clock::get_time();
    editor_clock() += now - before; // (the clock re-read after the call, as the original)
    editor->logic_update();
}

ParkBuilderMode* ParkBuilderMode::make_new()
{
    modules::PartUnit::make_new();
    listener_.make_new();
    vtable_ = kVtable;
    listener_.vtable_ = kListenerVtable;
    actions_ = 0;
    flag_bits_ = 0;
    // The logic task (priority 0), the display task (-1000: nothing to do) and the input handler.
    auto* logic = static_cast<ParkEditorTask*>(memory::alloc_zeroed(sizeof(ParkEditorTask)));
    if (logic) {
        reinterpret_cast<tasks::SchedulerTask*>(logic)->make_new(0);
        *reinterpret_cast<uint32_t*>(logic) = reinterpret_cast<uint32_t>(game_code::kTaskVtable);
        logic->code_value_ = &front_end_tick;
        logic->content_ = this;
    }
    editor_update_job_ = logic;
    auto* display = static_cast<ParkEditorTask*>(memory::alloc_zeroed(sizeof(ParkEditorTask)));
    if (display) {
        reinterpret_cast<tasks::SchedulerTask*>(display)->make_new(-1000);
        *reinterpret_cast<uint32_t*>(display) = reinterpret_cast<uint32_t>(game_code::kTaskVtable);
        // (0x005480A0, the empty function - stubs::do_nothing; a cdecl task code: the argument is the caller's)
        display->code_value_ = reinterpret_cast<void(__cdecl*)(ParkEditorTask*)>(&stubs::do_nothing);
        display->content_ = this;
    }
    draw_job_ = display;
    auto* input = static_cast<ParkEditorInputHandler*>(memory::alloc_zeroed(sizeof(ParkEditorInputHandler)));
    if (input) {
        reinterpret_cast<tasks::SchedulerTask*>(input)->make_new(0);
        input->ordinal_ = 0;
        *reinterpret_cast<uint32_t*>(input) = reinterpret_cast<uint32_t>(game_code::kInputHandlerVtable);
        input->code_value_ = &process_pad_input;
        input->content_ = this;
    }
    editor_pad_listener_ = input;
    move_speed_ = 480.0f;
    turn_speed_ = 120.0f;
    unknown64_ = 0;
    orbit_radius_ = 1400.0f;
    camera_angle_ = 0.0f;
    camera_tilt_ = 10.0f;
    cur_phase_ = kStateOff;
    flag_bits_ = 0;
    lighting_script_ = 0;
    action_finished_ = false;
    ParkLayout::spawn_occurrence();
    listener_.enter_wait_board(nullptr);
    return this;
}

ParkBuilderMode* __cdecl ParkBuilderMode::fabricate(bool create)
{
    if (!sole() && create) {
        void* memory = memory::alloc_zeroed(sizeof(ParkBuilderMode), static_cast<memory::Arena*>(nullptr), 1);
        if (memory) {
            sole() = static_cast<ParkBuilderMode*>(memory)->make_new();
            ++ref_tally();
            return sole();
        }
        sole() = nullptr;
    }
    ++ref_tally();
    return sole();
}

} // namespace Ed
