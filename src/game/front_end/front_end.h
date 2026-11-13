#pragma once

// The front end module (modules::FrontEnd, 0x164 bytes, vtable 0x0064D724): turns the controllers' presses into the
// menus' pad events (pad_up, pad_choose, pad_back...), sent through the event tracker to the screen elements, and
// the typed keys into the script handle_keyboard_input. It also pauses and unpauses the game, and once a frame
// updates the spawned scripts and decides whether a menu is up.
//
// Menu input mapping, as the scripts set it up:
// - SetMenuPadMappings [m0 m1]: per controller, inactive (0: its events reach nothing), active (1) or
//   use_as_first (2). Only zero / non-zero matters to the code here.
// - SetButtonEventMappings xbox=[[button event] ...]: up to 32 (button, event) pairs, a button being an index into
//   the Xbox button names (left_trigger_full ... white). Each frame the n-th event whose button was pressed is
//   sent; button null removes every pair of that event, event null every pair of that button (each then still
//   takes a free slot, as (0, event) / (button, null) - a quirk of the original). Flags
//   block_menu_input / unblock_menu_input stop / restart sending any pad event.
// - SetMenuAutoRepeatTimes (delay rate): seconds, kept in milliseconds (default 300 / 50).
// - SetAnalogStickActiveForMenus: whether the left stick moves through menus too (the input manager's flag).
// PartyMod's UIControls setting (src/engine/pad_input/sdl_input.*) is read but never used; nothing here reads it
// either - these mappings come from the scripts only.

#include <cstddef>
#include <cstdint>
#include <memory>

#include "base/core/api.h"
#include "engine/pad_input/input_hub.h"
#include "engine/sys/game_module.h"
#include <gsl/pointers>
#include <array>

namespace script {
class Context;
class Struct;
} // namespace script

namespace modules {

struct FrontEnd;

// The front end's tasks (in the main loop's task lists) and its input handlers (one per controller).
using FrontEndTask = tasks::Task<FrontEnd>;
using FrontEndInputHandler = pad_input::Responder<FrontEnd>;

struct FrontEnd final : public PartUnit {
    FrontEnd(const FrontEnd&) = delete;
    FrontEnd& operator=(const FrontEnd&) = delete;
    // The menu's pad events.
    static constexpr uint32_t kPadStart = 0x2E6EF8E7;  // "pad_start"
    static constexpr uint32_t kPadBack = 0x7EE0FD2A;   // "pad_back"
    static constexpr uint32_t kPadChoose = 0x1E3E253B; // "pad_choose"

    // The menu's direction events (the d-pad, or the PC keyboard's arrows while the on-screen keyboard is up).
    static constexpr uint32_t kPadLeft = 0x6949DB75;  // "pad_left"
    static constexpr uint32_t kPadRight = 0x05DDD87C; // "pad_right"
    static constexpr uint32_t kPadUp = 0x30A4F836;    // "pad_up"
    static constexpr uint32_t kPadDown = 0x0FD1AC26;  // "pad_down"

    struct PadMapping {
        uint32_t repeat_state_; // a direction held: 0 no, 1 waiting for the auto-repeat delay, 2 repeating
        uint32_t repeat_timer_; // ms left until the next repeat
        uint32_t mapping_;      // 0 inactive, 1 active (the default), 2 use_as_first
    };
    struct PadPressRecord {
        uint32_t button_;   // the index of the button's name (ButtonEventMappings)
        uint32_t incident_; // 0: a free slot
    };
    static constexpr int kMaxButtonEvents = 32;

    // The fields other code reads and sets.
    std::array<std::unique_ptr<FrontEndInputHandler>, 2>& pad_listeners() { return pad_listeners_; }
    [[nodiscard]] const std::array<std::unique_ptr<FrontEndInputHandler>, 2>& pad_listeners() const { return pad_listeners_; }
    std::array<int, 2>& device_server_map() { return device_server_map_; }
    [[nodiscard]] const std::array<int, 2>& device_server_map() const { return device_server_map_; }
    // The world frozen: the original's pause (get_pause_game).
    [[nodiscard]] bool is_halted() const { return is_halted_; }
    // NOT ORIGINAL - the game paused, frozen or live (the live pause - game/front_end/live_pause.h: the pause menu up
    // with the world running): what the tools that wait for or refuse the pause menu ask.
    [[nodiscard]] bool is_paused() const { return is_halted_ || live_paused_; }
    [[nodiscard]] bool is_live_paused() const { return live_paused_; }
    void set_hold_repeat_ms(int value) { hold_repeat_ms_ = value; }
    void set_auto_repeat_rate(int value) { auto_repeat_rate_ = value; }
    std::array<PadMapping, 2>& pad_mappings() { return pad_mappings_; }
    [[nodiscard]] const std::array<PadMapping, 2>& pad_mappings() const { return pad_mappings_; }
    [[nodiscard]] bool block_menu_input() const { return block_menu_input_; }
    void set_block_menu_input(bool value) { block_menu_input_ = value; }

private:
    std::unique_ptr<FrontEndTask> update_job_; // +0x14: front_end_tick, priority -2000
    std::unique_ptr<FrontEndTask>
        keyboard_task_; // +0x18: keyboard_task, priority -1000 (added by register_key_listener)
    uint32_t unknown1c_{};
    std::array<std::unique_ptr<FrontEndInputHandler>, 2> pad_listeners_; // +0x20: input_logic, priority -1000
    std::array<int, 2> device_server_map_{};                             // +0x28: which controller each handler listens to (0, 1)
    bool is_halted_;                                         // +0x30
    int hold_repeat_ms_;                                     // +0x34: ms (300)
    int auto_repeat_rate_;                                   // +0x38: ms (50)
    int keyboard_device_{};                                  // +0x3C: the device_num the typed keys are reported with
    std::array<PadMapping, 2> pad_mappings_{};                           // +0x40
    bool block_menu_input_;                                  // +0x58
    std::array<PadPressRecord, kMaxButtonEvents> pad_event_queue_{};     // +0x5C
    bool check_startup_script_;  // +0x15C: the first input frame looks at the global below once
    bool startup_script_on_;     // +0x15D
    uint32_t startup_script_id_; // +0x160: the spawned script's unique id
    bool live_paused_ = false;   // NOT ORIGINAL: the live pause is on (pause_live)

public:

    // 0x0054FE00: the constructor (the module, its tasks and handlers; mappings active, 300/50 ms).
    FrontEnd();
    // 0x0054EC20 / 0x0054EE70: the logic task and the input handlers deleted (the keyboard task isn't - an
    // original leak), then the module's.
    ~FrontEnd() override;
    // 0x0054EEA0: the logic task joins the main loop's logic tasks and each input handler the handler list of the
    // controller its own index names (not device_server_map_'s).
    void start_callback() override;
    // 0x0054EF30: the logic task and the input handlers out of their lists.
    void stop_callback() override;
    // 0x0054ECA0: whether either handler's controller is plugged in (the script command PadsPluggedIn).
    bool pads_plugged_in();
    // 0x0054ECD0: no more typed keys (the keyboard task out of the logic tasks; StopKeyboardHandler).
    void unregister_key_listener();
    // 0x0054ECE0: the auto-repeat of a held direction for controller `device_index` (input_logic's first event): the
    // direction's event when it's first held, then after hold_repeat_ms_ ms, then every auto_repeat_rate_ ms; 0
    // in between. Controller 0 with the on-screen keyboard up also takes the PC keyboard's arrows. Always 0 while
    // the startup script runs (it's unpaused each time until it ends).
    // A pad as the front end reads it: the buttons pressed this frame, those held, and its device.
    struct PadRead {
        uint32_t makes;
        uint32_t held;
        int device;
    };
    uint32_t auto_repeat(const PadRead& read);
    // 0x0054EF60: typed keys are reported for `device` from now on (the keyboard task joins the logic tasks).
    void register_key_listener(int device);
    // 0x0054EF80: the button-event pairs from the `xbox` array of `params` (see the top of this file).
    void update_button_event_mappings(script::Struct* params);
    // 0x0054F380: once a frame (front_end_tick): whether a menu / the keyboard is up, the music, the spawned scripts.
    static void tick_frame(bool game_paused);
    // 0x0054F600: the `index`-th pad event of the pressed buttons, else pad_start for Start, else (controller 0,
    // the on-screen keyboard up) Escape / Enter / Space as pad_back / pad_start / pad_choose. 0 if none.
    uint32_t grab_control_event(const PadRead& read, int index);
    // 0x0054F900
    static void set_analog_stick_active_for_menus(bool active);
    // 0x0054F960: pauses or unpauses the game (controllers' motors, scripts, skaters, the object updates...).
    THUG2_API void get_pause_game(bool pause);
    // NOT ORIGINAL - the live pause (game/front_end/live_pause.h) on or off: on, the local skaters frozen and the menus
    // taking the pad, the world left running; off, the skaters unfrozen. get_pause_game ends it first.
    void pause_live(bool pause);
    // 0x0054FD00: each handler moved to the handler list of the controller device_server_map_ names.
    void update_input_mappings();

    // 0x0054F200 (the keyboard task): each typed key (printable, Enter or Backspace) to handle_keyboard_input.
    static void keyboard_task(FrontEndTask* task);
    // 0x0054FCC0 (the logic task)
    static void front_end_tick(FrontEndTask* task);
    // 0x0054FB40 (a controller's handler): its pad events, each sent to the menus (unless blocked or inactive).
    static void input_logic(FrontEndInputHandler* handler);
};

// The singleton (0x007CE464, references 0x007CE468).
THUG2_API std::unique_ptr<FrontEnd>& front_end_instance();
// 0x0054FF80: the first call with `create` makes it; every call takes a reference and returns it.
FrontEnd* create_front_end(bool create);
// 0x0054EBE0: gives back a reference; the last one destroys it.
void release_front_end();

// The script commands.
// 0x0054F0F0: SetMenuAutoRepeatTimes (delay rate) - seconds.
bool cfunc_set_menu_auto_repeat_times(script::Struct* params, script::Context context);
// 0x0054F150: SetMenuPadMappings [inactive|active|use_as_first x2].
bool cfunc_set_menu_pad_mappings(script::Struct* params, script::Context context);
// 0x0054F1B0: SetButtonEventMappings xbox=[...] block_menu_input / unblock_menu_input.
bool cfunc_set_button_event_mappings(script::Struct* params, script::Context context);
// 0x0054FDC0: SetAnalogStickActiveForMenus (an integer, default 1).
bool cfunc_set_analog_stick_active_for_menus(script::Struct* params, script::Context context);

bool menus_active();
void set_menus_active(bool value);

} // namespace modules
