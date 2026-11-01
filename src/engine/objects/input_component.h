#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/pad_input/input_hub.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/entity.h"
#include <gsl/pointers>

namespace script {
class Script;
class Struct;
} // namespace script

namespace objects {

// The input component (type 0x27D7CD28 "input", vtable 0x0064605C, 0x4A0 bytes; made by 0x0041EEC0): an object's
// controller - two control pads fed once a frame by input handlers on the input manager's controller servers (the
// first on the object's controller, the second always on controller 1), and, with input events on, the object's own
// pad events (one event type down and one up for each of 17 buttons).
//
// Vtable: 0 the scalar deleting destructor, 2 configure_from_data; the rest the base's / shared (1 0x0051B970, 3 / 4 / 9
// 0x005480A0, 5 0x004E3F90, 6 0x0048DCD0, 7 0x0045D760, 8 / 10 0x005BAB90, 11 0x0042BB20, 12 0x0051ADC0).
class PadInputModule final : public ObjectModule {
public:
    PadInputModule(const PadInputModule&) = delete;
    PadInputModule& operator=(const PadInputModule&) = delete;
    static constexpr uint32_t kType = 0x27D7CD28;

    // Its input handlers (the ordinal: which of the two).
    using Responder = pad_input::Responder<PadInputModule>;

    // 0x0041EEC0 (registered with the composite object manager by 0x004E4860): a new one (0 if out of memory).
    static gsl::owner<ObjectModule*> fabricate();
    // 0x0041E6F0 (not a Ghidra function; registered with Create): its member functions' table (0x00684C78; the
    // functions are put in by the static initialiser at 0x006445B0).
    static void publish_method_table();
    // 0x0041E850: both pads' Init, its type; no handler, 0x49C cleared.
    PadInputModule();
    // 0x0041E710: each handler out of its list and deleted; then the base's body.
    ~PadInputModule() override;
    int invoke_method(script::MemberFunctionEntry* entry, script::Struct* params, script::Script* script) override
    {
        return dispatch_script_method(entry, params, script); // (slot 11: 0x0042BB20, shared)
    }
    // 0x0041F390 (vtable slot 2): the controller of Player (the input manager's) or controller (link_to_pad);
    // 0xB9B1E3B6 into +0x49C.
    void configure_from_data(script::Struct* params) override;
    // 0x0041F1B0: the handlers made the first time (on the skater info heap; priority -1), then the first moved to
    // `controller`'s server and the second to controller 1's; the vibration component's device set to the first's.
    void link_to_pad(int controller);

    // 0x0041F170 / 0x0041F190 (cdecl; not Ghidra functions): the handlers' code - update_pad / update_pad2 of their
    // component with their data.
    static void handler_code(Responder* handler);
    static void handler_code2(Responder* handler);
    // 0x0041F090: unless its object is paused: the pad events (update_events), then the pad updated from `data` - or,
    // with input disabled (or net disabled) or blocked (is_input_blocked), reset (and, blocked, no events this frame).
    void update_pad(const uint8_t* data);
    // 0x0041F100: the same for the second pad, without the events.
    void update_pad2(const uint8_t* data);
    // 0x0041EF20: in a network game, whether something on screen takes the input: the screen element
    // controller_unplugged_dialog_anchor, or root_window with menu_state = on in its tags, or keyboard_anchor. False
    // outside network games.
    bool is_input_blocked();
    // 0x0041ECA0: the last frame's events kept; this frame's from the 17 buttons (read_button_state).
    void update_events(const uint8_t* data);
    // 0x0041EBF0: a button's event: while it's locked (its lock time not yet reached: frame_clock::GetTime, unsigned) nothing;
    // down (its pressure in `data`) - its `down` event to the object (when it wasn't down last frame, input events are
    // on and input isn't disabled) and its bit set; up - its `up` event (when it was down last frame).
    void read_button_state(const uint8_t* data, int index, uint32_t bit, uint32_t down, uint32_t up,
                           ControlButton* button);
    // 0x0041E7E0: `name`'s button (none: a crash, as the original) locked for `seconds` (its lock time: frame_clock::GetTime +
    // seconds * 1000, truncated, as a float); with `release`, also released and its events taken.
    void lock_button(uint32_t name, float seconds, bool release);

    // --- the member functions scripts call (the table at 0x00684C78) ---
    // 0x0041E8F0 [0xBF7D6C46]: the unnamed checksum's button locked for `time` seconds (1), released with `clear`
    // (an integer); its time printed (compiled out). 1.
    int lock_button_from_script(script::Struct* params, script::Script* script);
    // 0x0041E9A0 / 0x0041E9B0 [disable_player_input / enableplayerinput], 0x0041E9C0 / 0x0041E9D0
    // [net_disable_player_input / NetEnablePlayerInput], 0x0041E7C0 / 0x0041E7D0 [EnableInputEvents / 0x28729460]. 1.
    int disable_player_input(script::Struct* params, script::Script* script);
    int enable_player_input(script::Struct* params, script::Script* script);
    int net_disable_player_input(script::Struct* params, script::Script* script);
    int net_enable_player_input(script::Struct* params, script::Script* script);
    int enable_input_events(script::Struct* params, script::Script* script);
    int disable_input_events(script::Struct* params, script::Script* script);
    // 0x0041E9E0 [PlayerInputIsDisabled]
    int player_input_is_disabled(script::Struct* params, script::Script* script);
    // 0x0041EA20 / 0x0041EA30 / 0x0041EA40 / 0x0041EA50 [left_pressed / RightPressed / UpPressed / DownPressed]: the
    // first pad's button held.
    int left_pressed(script::Struct* params, script::Script* script);
    int right_pressed(script::Struct* params, script::Script* script);
    int up_pressed(script::Struct* params, script::Script* script);
    int down_pressed(script::Struct* params, script::Script* script);
    // 0x0041EA60 [0xB2A022DE]: the sticks into the script's parameters - 0x737F15CF / 0xD6030CEA the angles (degrees),
    // 0x686AD1C2 / 0xC94777AC the lengths (stick_length). 1.
    int get_sticks(script::Struct* params, script::Script* script);
    // 0x0041EB20 [HeldLongerThan]: whether button= (asserted) is held and was pressed longer ago than the unnamed time
    // (asserted; milliseconds, or with seconds / second times 1000, with frames / frame times 1000 / 60).
    int held_longer_than(script::Struct* params, script::Script* script);

    uint32_t last_events_{};    // +0x18 the buttons down last frame (their event bits)
    uint32_t events_{};         // +0x1C this frame's
    PadSnapshot pad_primary_;   // +0x20
    PadSnapshot pad_secondary_; // +0x258
    Responder* handler_{nullptr}; // +0x490
    Responder* handler2_{};     // +0x494
    bool disabled_{};           // +0x498 disable_player_input
    bool net_disabled_{};       // +0x499 net_disable_player_input
    bool input_events_{};       // +0x49A EnableInputEvents
    uint8_t pad49b_{};
    uint32_t unknown49c_{0}; // +0x49C 0xB9B1E3B6
};
static_assert(offsetof(PadInputModule, pad_primary_) == 0x20 && offsetof(PadInputModule, pad_secondary_) == 0x258 &&
                  offsetof(PadInputModule, handler_) == 0x490 && offsetof(PadInputModule, disabled_) == 0x498 &&
                  offsetof(PadInputModule, unknown49c_) == 0x49C && sizeof(PadInputModule) == 0x4A0,
              "layout must match the original");

} // namespace objects

namespace objects {
// The script member table's records (their code is filled in by engine/script/member_function_code.cpp).
extern uint32_t g_input_component_member_functions[90];
} // namespace objects
