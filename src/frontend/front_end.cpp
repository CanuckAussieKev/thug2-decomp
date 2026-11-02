// modules::FrontEnd (0x0054EBE0-0x0055001F): the menus' input - pad and button events, typed keys - and pausing the
// game. See front_end.h.

#include "frontend/front_end.h"

#include <cstdint>
#include <memory>
#include <new>

#include "engine/core/arena_alloc.h"
#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "frontend/automation.h"
#include "frontend/mouse_menus.h"
#include "frontend/screen_element_manager.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/tracker.h"
#include "engine/audio/music_player.h"
#include "engine/script/cscript.h"
#include "engine/script/game_cfuncs.h"
#include "engine/script/param_struct.h"
#include "engine/sound/sfx.h"
#include "engine/pad_input/device.h"
#include "engine/pad_input/input_hub.h"
#include "engine/pad_input/sdl_input.h"
#include "game/skate/skate_module.h"
#include "game/rider/rider.h"
#include "platform/game_clock.h"
#include "platform/stopwatch.h"
#include "engine/objects/movie_manager.h"
#include "engine/assets/resource_registry.h"
#include "engine/sys/mlp.h"
#include <gsl/pointers>
#include <array>
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/core/no_destroy.h"

namespace modules {

namespace {

bool& stored_menus_active()
{
    static bool menus_active{};
    return menus_active;
}

} // namespace

bool menus_active() { return stored_menus_active(); }
void set_menus_active(bool value) { stored_menus_active() = value; }

namespace {

bool& stored_pending_script()
{
    static bool pending_script{};
    return pending_script;
}

} // namespace

bool pending_script() { return stored_pending_script(); }
void set_pending_script(bool value) { stored_pending_script() = value; }

namespace {

constexpr uint32_t kId = 0x40C698AF;                  // "id"
constexpr uint32_t kNull = 0xDA3403B0;                // "null"
constexpr uint32_t kText = 0xC4745838;                // "text"
constexpr uint32_t kDeviceNum = 0xC9428A08;           // "device_num"
constexpr uint32_t kController = 0xB30D9965;          // "controller"
constexpr uint32_t kKeyboardAnchor = 0x31631B98;      // "keyboard_anchor"
constexpr uint32_t kCurrentMenuAnchor = 0xF53D1D83;   // "current_menu_anchor"
constexpr uint32_t kDialogBoxAnchor = 0x3B56E746;     // "dialog_box_anchor"
constexpr uint32_t kBlockMenuInput = 0xDB96E524;      // "block_menu_input"
constexpr uint32_t kUnblockMenuInput = 0x4F653B0B;    // "unblock_menu_input"
constexpr uint32_t kInactive = 0x742CF11F;            // "inactive"
constexpr uint32_t kActive = 0xB4E103FD;              // "active"
constexpr uint32_t kUseAsFirst = 0xAD6C73EE;          // "use_as_first"
constexpr uint32_t kHandleKeyboardInput = 0xA6ED4911; // "handle_keyboard_input"
constexpr uint32_t kKeyboardDeviceParam = 0x69E3A1C4; // (the device_num the keys came from; name unknown)
constexpr uint32_t kEnterKey = 0xB292CC42;            // (handle_keyboard_input's flag for Enter; name unknown)
constexpr uint32_t kBackspaceKey = 0x86C0FC77;        // (and for Backspace)
constexpr uint32_t kMenuEvent = 0x36B2EE74;           // the pad events' target and source (name unknown)

// The typed keys' control characters (sio::get_keyboard_chars).
constexpr char kKeyBackspace = 0x18;
constexpr char kKeyEnter = 0x19;
constexpr char kKeyEscape = 0x1A;

// The button names of SetButtonEventMappings' xbox array, by index (the null ones are unused indexes).
const std::array<const char*, 18> kXboxButtonNames = {{
    "left_trigger_full",
    "right_trigger_full",
    "left_trigger_half",
    "right_trigger_half",
    "y",
    "b",
    "a",
    "x",
    "back",
    "left_stick_button",
    "right_stick_button",
    "null",
    "null",
    "null",
    "null",
    "null",
    "black",
    "white",
}};

// NOT ORIGINAL grouping - front_end.cpp's state (the original's globals). Made on first use, never destroyed
// (front_end_state()).
struct FrontEndState {
    uint32_t front_end_refs_{};
    bool skateshop_override_{};
};

FrontEndState& front_end_state()
{
    static core::NoDestroy<FrontEndState> state;
    return state.get();
}

uint32_t& front_end_refs() { return front_end_state().front_end_refs_; }
// Set each frame by Update.

bool& skateshop_override() { return front_end_state().skateshop_override_; } // set elsewhere: never counts as the skateshop
bool& get_keyboard_on_screen() { return sio::keyboard_on_screen_flag(); }
bool& menu_on_screen() { return sio::menu_flag(); } // a menu, dialog box or the quit dialog

frame_loop::Hub* main_loop_manager() { return assets::get_main_loop_manager(); }
pad_input::Hub* input_manager() { return pad_input::get_input_manager(); }

// The typed arrow keys (sio::get_keyboard_chars).
constexpr char kKeyLeft = 0x14;
constexpr char kKeyRight = 0x15;
constexpr char kKeyUp = 0x16;
constexpr char kKeyDown = 0x17;

// The held buttons that are directions.
constexpr uint32_t kButtonLeft = 0x8000;
constexpr uint32_t kButtonRight = 0x2000;
constexpr uint32_t kReleaseEdge = 0x1000;
constexpr uint32_t kPressEdge = 0x4000;

// The CRT's _ftol: truncation, the low 32 bits of the 64-bit result.
int ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// The debug report (the empty 0x005480A0) after the game is paused or unpaused - compiled out.
inline void report_pause_state() {}

// Engine pieces without source yet.
bool object_exists(script::Struct* params) // the script command ObjectExists
{
    return objects::cfunc_object_exists(params, nullptr);
}
// The module base, and the tasks (the logic and input handlers) in the main loop's / input manager's stacks.

void append_vertex(tasks::PileUp& list, tasks::SchedulerTask* node) { list.queue_job(node); }
void remove_node(void* node) { static_cast<tasks::SchedulerTask*>(node)->drop_out(); }

// A reference to the input manager (single::LazyInstance<pad_input::Hub>, 0x0044E7F0 / 0x0044E6B0), not making it.
struct InputManagerRef {
    pad_input::Hub* supervisor_;
    InputManagerRef() : supervisor_(pad_input::create_manager(false)) {}
    ~InputManagerRef() { pad_input::release_manager(); }
    InputManagerRef(const InputManagerRef&) = delete;
    InputManagerRef& operator=(const InputManagerRef&) = delete;
};

gsl::owner<script::Struct*> new_params() { return script::new_struct(); }
void delete_params(script::Struct* params)
{
    if (params) {
        params->get_clear(); // (the destructor)
        script::delete_struct(params);
    }
}
// ObjectExists id=`id`, on `params` emptied first unless `clear` is false. The original compares the result
// with 1 in some places and with 0 in others; it's a bool either way.
bool anchor_exists(script::Struct* params, uint32_t id, bool clear = true)
{
    if (clear)
        params->get_clear();
    params->put_name_key(kId, id);
    return object_exists(params);
}

} // namespace

std::unique_ptr<modules::FrontEnd>& front_end_instance()
{
    static core::NoDestroy<std::unique_ptr<modules::FrontEnd>> front_end_instance;
    return front_end_instance.get();
}

// 0x0054EBE0
void release_front_end()
{
    if (front_end_refs() > 1) {
        --front_end_refs();
        return;
    }
    memory::destroy_then_clear(front_end_instance());
    front_end_refs() = 0;
}

// 0x0054EC20
FrontEnd::~FrontEnd()
{
    update_job_.reset();
    for (std::unique_ptr<FrontEndInputHandler>& handler : pad_listeners_)
        handler.reset();
    // (keyboard_task_ goes with the object, after these - the original never deleted it)
}

// 0x0054ECA0
bool FrontEnd::pads_plugged_in()
{
    for (const std::unique_ptr<FrontEndInputHandler>& handler : pad_listeners_) {
        if (handler->dev_->is_attached())
            return true;
    }
    return false;
}

// 0x0054ECD0
void FrontEnd::unregister_key_listener() { remove_node(keyboard_task_.get()); }

// 0x0054ECE0
uint32_t FrontEnd::auto_repeat(uint32_t held, int device_index)
{
    uint32_t event = 0;
    if (held & kButtonLeft)
        event = kPadLeft;
    else if (held & kButtonRight)
        event = kPadRight;
    else if (held & kReleaseEdge)
        event = kPadUp;
    else if (held & kPressEdge)
        event = kPadDown;
    if (device_index == 0 && event == 0 && get_keyboard_on_screen()) {
        // The PC keyboard's arrows: the first one typed (the keys are left for the others to read).
        std::array<char, 256> keys{};
        const int count = sio::get_keyboard_chars(keys);
        for (int i = 0; i < count; ++i) {
            const char key = core::at(keys, i);
            if (key == kKeyLeft) {
                event = kPadLeft;
                break;
            }
            if (key == kKeyRight) {
                event = kPadRight;
                break;
            }
            if (key == kKeyUp) {
                event = kPadUp;
                break;
            }
            if (key == kKeyDown) {
                event = kPadDown;
                break;
            }
        }
    }

    PadMapping& pad = core::at(pad_mappings_, device_index);
    if (pad.repeat_state_ == 0) {
        if (event) { // first held: sent now, repeated after the delay
            pad.repeat_timer_ = static_cast<uint32_t>(hold_repeat_ms_);
            pad.repeat_state_ = 1;
        }
    } else if (event) {
        auto elapsed = static_cast<uint32_t>(ftol(frame_clock::get_slomo_frame_seconds() * 1000.0f));
        pad.repeat_timer_ = elapsed < pad.repeat_timer_ ? pad.repeat_timer_ - elapsed : 0;
        if (pad.repeat_timer_ == 0) { // due: sent, then again after the rate
            if (pad.repeat_state_ == 1)
                pad.repeat_state_ = 2;
            pad.repeat_timer_ = static_cast<uint32_t>(auto_repeat_rate_);
        } else {
            event = 0;
        }
    } else {
        pad.repeat_state_ = 0; // let go
    }

    // While the startup script runs, nothing (and it's unpaused).
    if (startup_script_on_ && device_index == 0) {
        if (script::Script* script = script::find_script_with_unique_id(startup_script_id_))
            script::resume_child_script(script);
        else
            startup_script_on_ = false;
        return 0;
    }
    return event;
}

// 0x0054EEA0
void FrontEnd::start_callback()
{
    memory::Hub::get_pull()->enter_heap_scope(memory::Hub::get_pull()->front_end_heap_);
    append_vertex(main_loop_manager()->logic_tasks_, update_job_.get());
    pad_input::Hub* input = input_manager();
    for (const std::unique_ptr<FrontEndInputHandler>& handler : pad_listeners_) {
        // The controller's place in the input manager (see update_input_mappings).
        pad_input::HostEnd& server = core::at(input->hosts_, handler->ordinal_);
        append_vertex(server.handlers_, handler.get());
        handler->control_data_ = &server.content_;
        handler->dev_ = server.dev_;
    }
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x0054EF30
void FrontEnd::stop_callback()
{
    remove_node(update_job_.get());
    for (const std::unique_ptr<FrontEndInputHandler>& handler : pad_listeners_)
        remove_node(handler.get());
}

// 0x0054EF60
void FrontEnd::register_key_listener(int device)
{
    keyboard_device_ = device;
    append_vertex(main_loop_manager()->logic_tasks_, keyboard_task_.get());
}

// 0x0054EF80
void FrontEnd::update_button_event_mappings(script::Struct* params)
{
    script::Array* mappings = nullptr;
    if (!params->fetch_list("xbox", &mappings, 0))
        return;
    for (uint32_t i = 0; i < mappings->extent_; ++i) {
        auto* pair = mappings->array_at(i);
        const uint32_t pad_button_label = pair->fetch_part(0);
        const uint32_t event = pair->fetch_part(1);
        uint32_t button = 0;
        for (; button < 18; ++button) {
            if (pad_button_label == checksums::hash_from_text(core::at(kXboxButtonNames, button)) || pad_button_label == kNull)
                break;
        }
        if (button == 18)
            continue; // not a button
        // Quirks kept: button null (index 0) removes the event's pairs, then the first free slot still gets
        // (0, event) - the event moves to left_trigger_full; event null removes the button's pairs, then a free
        // slot gets (button, null).
        for (PadPressRecord& entry : pad_event_queue_) {
            // NOLINTNEXTLINE(bugprone-branch-clone): the original's, as written
            if (pad_button_label == kNull && entry.incident_ == event) {
                entry.incident_ = 0;
            } else if (event == kNull && entry.button_ == button) {
                entry.incident_ = 0;
            } else if (entry.incident_ == 0) {
                entry.button_ = button;
                entry.incident_ = event;
                break;
            }
        }
    }
}

// 0x0054F0F0
bool cfunc_set_menu_auto_repeat_times(script::Struct* params, script::Script* /*script*/)
{
    script::Pair times;
    params->fetch_pair(0, &times, true);
    FrontEnd* front_end = front_end_instance().get();
    const int rate = ftol(static_cast<double>(times.y) * 1000.0);
    const int delay = ftol(static_cast<double>(times.x) * 1000.0);
    front_end->auto_repeat_rate_ = rate;
    front_end->hold_repeat_ms_ = delay;
    return true;
}

// 0x0054F150
bool cfunc_set_menu_pad_mappings(script::Struct* params, script::Script* /*script*/)
{
    script::Array* mappings = nullptr;
    params->fetch_list(0u, &mappings, 1);
    FrontEnd* front_end = front_end_instance().get();
    for (int i = 0; i < 2; ++i) {
        const uint32_t mapping = mappings->fetch_part(i);
        uint32_t value = 0; // inactive, or anything else
        if (mapping != kInactive) {
            if (mapping == kActive)
                value = 1;
            else if (mapping == kUseAsFirst)
                value = 2;
        }
        core::at(front_end->pad_mappings_, i).mapping_ = value;
    }
    return true;
}

// 0x0054F1B0
bool cfunc_set_button_event_mappings(script::Struct* params, script::Script* /*script*/)
{
    FrontEnd* front_end = front_end_instance().get();
    front_end->update_button_event_mappings(params);
    if (params->has_bare_flag(kBlockMenuInput))
        front_end->block_menu_input_ = true;
    else if (params->has_bare_flag(kUnblockMenuInput))
        front_end->block_menu_input_ = false;
    return true;
}

// 0x0054F200
void FrontEnd::keyboard_task(FrontEndTask* task)
{
    const FrontEnd* front_end = task->content_;
    std::array<char, 256> keys{};
    const int count = sio::get_keyboard_chars(keys);
    for (int i = 0; i < count; ++i) {
        const char key = core::at(keys, i);
        // Printable (and the high half), Enter and Backspace.
        if (!(key >= 0x20 || key < 0 || key == kKeyEnter || key == kKeyBackspace))
            continue;
        gsl::owner<script::Struct*> params = new_params();
        if (core::at(keys, i) == kKeyEnter) {
            params->put_name_key(0u, kEnterKey);
        } else if (core::at(keys, i) == kKeyBackspace) {
            params->put_name_key(0u, kBackspaceKey);
        } else {
            std::array<char, 2> text = {{core::at(keys, i), '\0'}};
            params->append_word(kText, text.data());
        }
        params->put_int(kKeyboardDeviceParam, front_end->keyboard_device_);
        script::launch_script(kHandleKeyboardInput, params, nullptr, false, nullptr);
        delete_params(params);
    }
    sio::clear_keyboard_chars();
}

// 0x0054F380
void FrontEnd::tick_frame(bool game_paused)
{
    bool in_skateshop = modules::Skate::sole()->pending_level_ == 0xE92ECAFE; // (the level / game mode)
    if (skateshop_override())
        in_skateshop = false;
    bool keyboard_up = false;
    gsl::owner<script::Struct*> params = new_params();
    if (anchor_exists(params, checksums::hash_from_text("keyboard_anchor"), false))
        keyboard_up = true;
    params->get_clear();
    bool menu_up = anchor_exists(params, checksums::hash_from_text("current_menu_anchor"), false);
    params->get_clear();
    if (anchor_exists(params, checksums::hash_from_text("dialog_box_anchor"), false))
        menu_up = true;
    params->get_clear();
    if (anchor_exists(params, checksums::hash_from_text("quit_dialog_anchor"), false))
        menu_up = true;
    delete_params(params);

    stored_menus_active() = game_paused || in_skateshop || keyboard_up;
    get_keyboard_on_screen() = keyboard_up;
    menu_on_screen() = menu_up;

    audio::tick_frame();
    // (the sound manager: its voices' states, and its positional sounds; the original calls the first - which
    // takes no manager - as its method)
    auto* objects = sound::get_audio_mixer();
    sound::update_voice_states();
    memory::Hub* memory = memory::Hub::get_pull();
    if (!game_paused) {
        memory->enter_heap_scope(memory->bottom_up_heap_);
        objects->tick_positional_sounds();
        memory::Hub::get_pull()->leave_heap_scope();
    }
    memory::Hub::get_pull()->enter_heap_scope(memory::Hub::get_pull()->bottom_up_heap_);
    script::tick_child_scripts();
    modules::Skate::sole()->advance_match_flow();
    if (pending_script()) {
        stored_pending_script() = false;
        auto* object =
            static_cast<objects::Entity*>(modules::Skate::sole()->get_home_rider());
        script::launch_script(0xFB11E6CD, nullptr, object, false, nullptr);
    }
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x0054F600
uint32_t FrontEnd::grab_control_event(uint32_t makes, int index, int device_index, uint32_t held)
{
    for (const PadPressRecord& entry : pad_event_queue_) {
        if ((makes & (1u << (entry.button_ & 31))) && entry.incident_ != 0 && --index < 0)
            return entry.incident_;
    }
    // Start is pad_start (unless the global 0xF3E055E1 is set and the 0x100 button is down).
    if (!((held & 0x100) && script::fetch_int_value(0xF3E055E1u) != 0)) {
        if ((makes & 0x800) && index == 0)
            return kPadStart;
    }
    if (device_index != 0 || !get_keyboard_on_screen())
        return 0;

    // The PC keyboard, while the on-screen keyboard is up.
    std::array<char, 256> keys{};
    uint32_t event = 0;
    const int count = sio::get_keyboard_chars(keys);
    for (int i = 0; i < count; ++i) {
        const char key = core::at(keys, i);
        if (key == kKeyEscape) {
            gsl::owner<script::Struct*> params = new_params();
            event = anchor_exists(params, kCurrentMenuAnchor, false) ? kPadBack : kPadStart;
            sio::clear_keyboard_chars();
            delete_params(params);
            return event;
        }
        if (key != kKeyEnter && key != ' ')
            continue;
        gsl::owner<script::Struct*> params = new_params();
        if (!anchor_exists(params, kKeyboardAnchor, false)) {
            if (anchor_exists(params, 0x487FBC86) && core::at(keys, i) == ' ')
                return 0; // (the original leaks params here)
            if (anchor_exists(params, kCurrentMenuAnchor) || anchor_exists(params, kDialogBoxAnchor)) {
                event = kPadChoose;
                sio::clear_keyboard_chars();
            }
        }
        delete_params(params);
    }
    return event;
}

// 0x0054F900
void FrontEnd::set_analog_stick_active_for_menus(bool active)
{
    InputManagerRef input;
    input.supervisor_->change_pad_follows_stick(active);
}

// 0x0054F960
void FrontEnd::get_pause_game(bool pause)
{
    if (pause == is_halted_)
        return;
    modules::Skate* skate = modules::Skate::sole();
    auto* devices = sio::get_device_manager();
    if (pause) {
        skate->get_freeze(true);
        devices->pause_devices();
    } else {
        devices->unpause_devices();
    }
    script::pause_spawned_scripts(pause);

    // The skaters: their list's nodes (the head's data is -1; its data read before the null check, as the original).
    uint32_t count = 0;
    for (const memory::ListNode* node = skate->riders_.list_link_.next;
         !node->is_head() && node; node = node->next)
        ++count;
    for (uint32_t i = 0; i < count; ++i) {
        auto* skater =
            core::down_cast<objects::Rider>(skate->get_rider_at(static_cast<int>(i)));
        if (!skater)
            continue;
        if (pause)
            skater->get_freeze();
        else
            skater->unpause();
    }
    objects::object_hub_slot()->get_freeze(pause);
    frame_clock::pause_game_clock(pause);
    frontend::UiTree::sole()->set_paused_all(pause);
    objects::set_simulation_halted(pause);
    is_halted_ = pause;
    // The main loop's logic tasks: while paused those of the 0xC kinds (a task's +0x14 bits) are skipped
    // (0x00406AC0 runs a task only when (mask & its kinds) == 0).
    if (pause) {
        main_loop_manager()->logic_tasks_.skip_bits_ = 0xC;
        report_pause_state();
    } else {
        main_loop_manager()->logic_tasks_.skip_bits_ = 0;
        modules::Skate::sole()->get_freeze(false);
        report_pause_state();
    }
    if (is_halted_ || !skate->get_home_rider())
        return;
    // Back in the game with a skater: the camera's view mode back to the global's.
    gsl::owner<script::Struct*> params = new_params();
    params->put_int(0u, script::fetch_int_value(0x968F7408u));
    commands::cfunc_set_view_mode(params, nullptr);
    delete_params(params);
}

// 0x0054FB40
void FrontEnd::input_logic(FrontEndInputHandler* handler)
{
    FrontEnd* front_end = handler->content_;
    // (not original - the mouse_menus feature: once a frame, with the first controller's handler, the mouse's menu
    // events; nothing when it's off - Update checks; the level editor's input logic runs there too, whatever the
    // setting)
    if (handler->ordinal_ == 0)
        mouse_menus::tick_frame(front_end);
    objects::EventWaitBoard* tracker = objects::get_tracker();
    tracker->log_frame();
    for (int index = 0;; ++index) {
        uint32_t event = 0;
        if (index == 0) {
            if (front_end->check_startup_script_) {
                front_end->startup_script_on_ = script::fetch_int_value(0x756FB186u) != 0;
                if (front_end->startup_script_on_) {
                    const script::Script* script =
                        script::spawn_script(0xDCE60A74u, nullptr, 0, nullptr, 0, 0, false, false, false, false);
                    front_end->startup_script_id_ = script->unique_id_;
                }
                front_end->check_startup_script_ = false;
            }
            const uint32_t repeated = front_end->auto_repeat(handler->control_data_->buttons_, handler->ordinal_);
            if (repeated)
                event = repeated;
        }
        const uint32_t pressed = front_end->grab_control_event(handler->control_data_->makes_, index, handler->ordinal_,
                                                               handler->control_data_->buttons_);
        if (pressed)
            event = pressed;
        if (!event)
            return;
        if (core::at(front_end->pad_mappings_, handler->ordinal_).mapping_ == 0 || front_end->block_menu_input_)
            continue;
        script::Struct params;
        params.put_int(kDeviceNum, handler->dev_->port_no_);
        params.put_int(kController, 0); // (the original loads the mapping, then passes 0)
        tracker->launch_event(event, kMenuEvent, kMenuEvent, &params, false,
                                                                     -1.0f);
        params.get_clear();
    }
}

// 0x0054FCC0
void FrontEnd::front_end_tick(FrontEndTask* task)
{
    memory::Hub::get_pull()->enter_heap_scope(memory::Hub::get_pull()->front_end_heap_);
    task->content_->tick_frame(task->content_->is_halted_);
    frontend::UiTree::sole()->tick_frame();
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x0054FD00
void FrontEnd::update_input_mappings()
{
    const InputManagerRef input;
    for (int i = 0; i < 2; ++i) {
        debug::printf("m_device_server_map[%i] = %i\n", i, core::at(device_server_map_, i)); // "m_device_server_map[%i] = %i\n"
        FrontEndInputHandler* handler = core::at(pad_listeners_, i).get();
        if (!handler)
            continue;
        debug::printf("got a handler\n"); // "got a handler\n"
        remove_node(handler);
        // The controller's place in the input manager: its device, its handler list, its control data.
        pad_input::HostEnd& server = core::at(input.supervisor_->hosts_, core::at(device_server_map_, i));
        append_vertex(server.handlers_, handler);
        handler->control_data_ = &server.content_;
        handler->dev_ = server.dev_;
    }
}

// 0x0054FDC0
bool cfunc_set_analog_stick_active_for_menus(script::Struct* params, script::Script* /*script*/)
{
    int active = 1;
    params->fetch_int_value(0u, &active, 0);
    front_end_instance()->set_analog_stick_active_for_menus(active != 0);
    return true;
}

// 0x0054FE00
FrontEnd::FrontEnd()
{
    update_job_ = std::make_unique<FrontEndTask>(&front_end_tick, this, -2000);
    keyboard_task_ = std::make_unique<FrontEndTask>(&keyboard_task, this, -1000);
    for (int i = 0; i < 2; ++i) {
        core::at(pad_listeners_, i) = std::make_unique<FrontEndInputHandler>(&input_logic, this, -1000, i);
        core::at(pad_mappings_, i).repeat_state_ = 0;
        core::at(pad_mappings_, i).mapping_ = 1;
    }
    is_halted_ = false;
    hold_repeat_ms_ = 300;
    auto_repeat_rate_ = 50;
    block_menu_input_ = false;
    startup_script_on_ = false;
    check_startup_script_ = true;
    startup_script_id_ = 0;
    for (PadPressRecord& entry : pad_event_queue_)
        entry.incident_ = 0;
    core::at(device_server_map_, 0) = 0;
    core::at(device_server_map_, 1) = 1;
}

// 0x0054FF80
FrontEnd* create_front_end(bool create)
{
    if (!front_end_instance() && create) {
        front_end_instance() = std::make_unique<FrontEnd>();
    }
    ++front_end_refs();
    return front_end_instance().get();
}

} // namespace modules
