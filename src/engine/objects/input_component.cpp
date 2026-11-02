// The input component (GameObjects::PadInputModule): see input_component.h. From the disassembly of 0x0041E6F0-0x0041F3FB.

#include "engine/objects/input_component.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "engine/objects/control_pad.h"
#include "frontend/screen_element.h"
#include "frontend/screen_element_manager.h"
#include "engine/net/gamenet_manager.h"
#include "engine/objects/vibration_component.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "engine/pad_input/input_hub.h"
#include "platform/stopwatch.h"
#include "engine/objects/collide_and_die.h"
#include "game/rider/skitch.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"
#include <gsl/pointers>
#include <array>
#include <span>
#include "engine/core/ints.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"

namespace objects {

std::array<script::MemberFunctionEntry, 15>& input_component_member_functions()
{
    static std::array<script::MemberFunctionEntry, 15> input_component_member_functions{{
        {.callsign_ = 0xBF7D6C46, .owner_kind_ = 0x27D7CD28}, {.callsign_ = 0x6E24F5FD, .owner_kind_ = 0x27D7CD28}, {.callsign_ = 0x56C6C0CD, .owner_kind_ = 0x27D7CD28}, {.callsign_ = 0x6DF51774, .owner_kind_ = 0x27D7CD28},
        {.callsign_ = 0x31C6454F, .owner_kind_ = 0x27D7CD28}, {.callsign_ = 0x0660746A, .owner_kind_ = 0x27D7CD28}, {.callsign_ = 0x78238598, .owner_kind_ = 0x27D7CD28}, {.callsign_ = 0x1D604A9D, .owner_kind_ = 0x27D7CD28},
        {.callsign_ = 0x89AA9B5F, .owner_kind_ = 0x27D7CD28}, {.callsign_ = 0x83028FC5, .owner_kind_ = 0x27D7CD28}, {.callsign_ = 0xB2A022DE, .owner_kind_ = 0x27D7CD28}, {.callsign_ = 0x2366469C, .owner_kind_ = 0x27D7CD28},
        {.callsign_ = 0x1090A150, .owner_kind_ = 0x27D7CD28}, {.callsign_ = 0x28729460, .owner_kind_ = 0x27D7CD28}, {.callsign_ = 0, .owner_kind_ = 0x27D7CD28}}};
    return input_component_member_functions;
}

namespace {

constexpr uint32_t kPlayer = 0x67E6859A;     // Player
constexpr uint32_t kController = 0xB30D9965; // controller
constexpr uint32_t kUnknownB9b1e3b6 = 0xB9B1E3B6;
constexpr uint32_t kVibration = 0x404EB936;                     // vibration
constexpr uint32_t kTime = 0x906B67BA;                          // time
constexpr uint32_t kClear = 0x1A4E0EF9;                         // clear
constexpr uint32_t kButton = 0xC5F953C2;                        // button
constexpr uint32_t kSeconds = 0xD029F619, kSecond = 0x49E0EE96; // seconds, second
constexpr uint32_t kFrames = 0x019176C5, kFrame = 0x4A07C332;   // frames, frame
constexpr uint32_t kControllerUnpluggedDialog = 0x2EDB780F;     // controller_unplugged_dialog_anchor
constexpr uint32_t kRootWindow = 0x56A1EAE3;                    // root_window
constexpr uint32_t kMenuState = 0xCDC00E63;                     // menu_state
constexpr uint32_t kOn = 0xF649D637;                            // on
constexpr uint32_t kKeyboardAnchor = 0x31631B98;                // keyboard_anchor
constexpr float kMilliseconds = 1000.0f;                        // 0x006453C0
constexpr float kFrameMilliseconds = 16.6666660f;               // 0x006460DC (0x41855555)
constexpr float kDegreesPerRadian = 57.2957764f;                // 0x00645468 (0x42652EE0)
constexpr double kTwoTo32 = 4294967296.0;                       // 0x006453B4

// The pad events: a button's pressure index in the controller data, its event bit, its events down / up and the
// first pad's button (an offset into the pad; -1: none).
struct EventButton {
    int index;
    uint32_t bit;
    uint32_t down;
    uint32_t up;
    int button;
};
constexpr auto kEventButtons = std::to_array<EventButton>({
    {.index = 6, .bit = 0x40, .down = 0x5C8FAA09, .up = 0xB27804A2, .button = 0x000},       {.index = 7, .bit = 0x80, .down = 0xD26ADAE9, .up = 0xAEF7DFD3, .button = 0x020},
    {.index = 5, .bit = 0x20, .down = 0xB4F2ADBA, .up = 0xC86FA880, .button = 0x040},       {.index = 4, .bit = 0x10, .down = 0x0EDBBC2B, .up = 0xC8ABF89C, .button = 0x060},
    {.index = 9, .bit = 0x200, .down = 0xB069B600, .up = 0xE5AAE06B, .button = 0x140},      {.index = 0xB, .bit = 0x800, .down = 0xA93DBBD0, .up = 0xFCFEEDBB, .button = 0x160},
    {.index = 8, .bit = 0x100, .down = 0x7F695A77, .up = 0x7B684919, .button = 0x180},      {.index = 0xA, .bit = 0x400, .down = 0x42717176, .up = 0xBBBEF674, .button = 0x1A0},
    {.index = 0xC, .bit = 0x1000, .down = 0xC6547217, .up = 0x28A3DCBC, .button = 0x080},   {.index = 0xE, .bit = 0x4000, .down = 0x5F5D23AD, .up = 0xB1AA8D06, .button = 0x0A0},
    {.index = 0x10, .bit = 0x10000, .down = 0x285A133B, .up = 0xC6ADBD90, .button = 0x0C0}, {.index = 0xD, .bit = 0x2000, .down = 0x12154DC8, .up = 0xFCE2E363, .button = 0x0E0},
    {.index = 0xF, .bit = 0x8000, .down = 0x8B1C1C72, .up = 0x65EBB2D9, .button = 0x100},   {.index = 0x11, .bit = 0x20000, .down = 0xFC1B2CE4, .up = 0x12EC824F, .button = 0x120},
    {.index = 0x16, .bit = 0x400000, .down = 0x33947317, .up = 0xF5E437A0, .button = -1},   {.index = 0x17, .bit = 0x800000, .down = 0xF8DE049B, .up = 0x3EAE402C, .button = -1},
    {.index = 0x18, .bit = 0x1000000, .down = 0xAC7F105A, .up = 0x55B09758, .button = -1},
});

// The original's _ftol (0x006236FC): truncated towards zero, the low 32 bits of the 64-bit result.
int get_truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// The input manager (0x00696D40): its controller servers (the device, the handler list, the data), and a player's
// controller (0x0044E630).
pad_input::Hub* input_manager() { return pad_input::get_input_manager(); }
void place_handler(PadInputModule::Responder* handler, pad_input::Hub* manager, int server)
{
    handler->control_data_ = &core::at(manager->hosts_, server).content_;
    handler->dev_ = core::at(manager->hosts_, server).dev_;
}
int player_controller(int player) { return input_manager()->get_player_controller(player); }

// A new handler (on the current heap).
gsl::owner<PadInputModule::Responder*> new_handler(int index, PadInputModule::Responder::Code code,
                                                      PadInputModule* component)
{
    return new PadInputModule::Responder(code, component, -1, index);
}

// The screen element `id`, if there's one (the smart pointer let go at once, as the original does).
frontend::UiWidget* find_element(uint32_t id)
{
    frontend::ElementPtr found;
    frontend::UiTree::sole()->fetch_part(&found, id, 0);
    frontend::UiWidget* element = found.get_pull();
    found.get_release();
    return element;
}

} // namespace

// 0x0041EEC0
gsl::owner<ObjectModule*> PadInputModule::fabricate() { return new PadInputModule; }

// 0x0041E6F0
void PadInputModule::publish_method_table()
{
    script::register_member_function_table(input_component_member_functions());
}

// 0x0041E850
PadInputModule::PadInputModule() { kind_tag_ = kType; }

// 0x0041E710 (0x0041E8C0 the deleting one)
PadInputModule::~PadInputModule()
{
    for (Responder* handler : {handler_, handler2_}) {
        if (!handler)
            continue;
        if (handler->is_on_task_list())
            handler->drop_out();
        gsl::owner<decltype(handler)> owned = memory::owned_element(handler); // (the list's)
        delete owned;
    }
}

// 0x0041F390
void PadInputModule::configure_from_data(script::Struct* params)
{
    int value = 0;
    if (params->fetch_int_value(kPlayer, &value, 0))
        link_to_pad(player_controller(value));
    else if (params->fetch_int_value(kController, &value, 0))
        link_to_pad(value);
    params->read_hash_value(kUnknownB9b1e3b6, &unknown49c_, 0);
}

// 0x0041F1B0
void PadInputModule::link_to_pad(int controller)
{
    if (!handler_) {
        memory::Hub* memory = memory::Hub::get_pull();
        memory->enter_heap_scope(memory->skater_info_heap());
        handler_ = new_handler(0, handler_code, this);
        pad_input::Hub* manager = input_manager();
        core::at(manager->hosts_, (handler_->ordinal_)).handlers_.queue_job(handler_);
        place_handler(handler_, manager, handler_->ordinal_);
        handler2_ = new_handler(1, handler_code2, this);
        manager = input_manager();
        core::at(manager->hosts_, (handler2_->ordinal_)).handlers_.queue_job(handler2_);
        place_handler(handler2_, manager, handler2_->ordinal_);
        memory::Hub::get_pull()->leave_heap_scope();
    }
    Responder* handler = handler_;
    pad_input::Hub* manager = input_manager();
    handler->drop_out();
    core::at(manager->hosts_, controller).handlers_.queue_job(handler);
    place_handler(handler, manager, controller);
    Responder* handler2 = handler2_;
    manager = input_manager();
    handler2->drop_out();
    core::at(manager->hosts_, 1).handlers_.queue_job(handler2);
    place_handler(handler2, manager, 1);
    if (ObjectModule* vibration = entity_->fetch_module(kVibration))
        core::down_cast<PadRumbleModule>(vibration)->change_gadget(handler_->dev_);
}

// 0x0041F170
void PadInputModule::handler_code(Responder* handler)
{
    handler->content_->update_pad(handler->control_data_);
}

// 0x0041F190
void PadInputModule::handler_code2(Responder* handler)
{
    handler->content_->update_pad2(handler->control_data_);
}

// 0x0041F090
void PadInputModule::update_pad(const pad_input::Payload* data)
{
    if (entity_->composite_flags_ & kCompositeFlagPaused)
        return;
    update_events(data->incident_);
    const bool blocked = is_input_blocked();
    if (!disabled_ && !net_disabled_ && !blocked) {
        pad_primary_.tick_frame(data);
        return;
    }
    pad_primary_.get_reset();
    if (blocked)
        events_ = 0;
}

// 0x0041F100
void PadInputModule::update_pad2(const pad_input::Payload* data)
{
    if (entity_->composite_flags_ & kCompositeFlagPaused)
        return;
    const bool blocked = is_input_blocked();
    if (!disabled_ && !net_disabled_ && !blocked) {
        pad_secondary_.tick_frame(data);
        return;
    }
    pad_secondary_.get_reset();
    if (blocked)
        events_ = 0;
}

// 0x0041EF20
bool PadInputModule::is_input_blocked()
{
    if (!net_hub()->in_net_game())
        return false;
    if (find_element(kControllerUnpluggedDialog))
        return true;
    if (frontend::UiWidget* root = find_element(kRootWindow)) {
        gsl::owner<script::Struct*> tags = script::spawn_param_bag();
        if (tags)
            tags = new (tags) script::Struct;
        root->copy_tags_to(tags);
        uint32_t state = 0; // (uninitialised in the original: what was on the stack)
        tags->read_hash_value(kMenuState, &state, 0);
        if (tags) {
            tags->get_clear(); // (the destructor)
            script::delete_struct(tags);
        }
        if (state == kOn)
            return true;
    }
    return find_element(kKeyboardAnchor) != nullptr;
}

// 0x0041ECA0
void PadInputModule::update_events(std::span<const uint8_t> data)
{
    last_events_ = events_;
    events_ = 0;
    for (const EventButton& event : kEventButtons) {
        ControlButton* button =
            event.button < 0
                ? nullptr
                : pad_primary_.button_at_offset(static_cast<uint32_t>(event.button));
        read_button_state(data, event.index, event.bit, event.down, event.up, button);
    }
}

// 0x0041EBF0
void PadInputModule::read_button_state(std::span<const uint8_t> data, int index, uint32_t bit, uint32_t down, uint32_t up,
                                       ControlButton* button)
{
    if (button) {
        auto locked_until = static_cast<uint32_t>(get_truncated(button->locked_until_));
        if (frame_clock::get_time() < locked_until)
            return;
    }
    const bool events_on = input_events_ && !disabled_ && !net_disabled_;
    if (core::at(data, index)) {
        if (!(last_events_ & bit) && events_on)
            entity_->post_own_event(down, nullptr);
        events_ |= bit;
    } else if ((last_events_ & bit) && events_on) {
        entity_->post_own_event(up, nullptr);
    }
}

// 0x0041E7E0
void PadInputModule::lock_button(uint32_t name, float seconds, bool release)
{
    ControlButton* button = pad_primary_.read_button(name);
    const uint32_t now = frame_clock::get_time();
    auto time = static_cast<double>(static_cast<int32_t>(now));
    if (static_cast<int32_t>(now) < 0)
        time += kTwoTo32;
    button->locked_until_ = static_cast<float>(get_truncated(time + static_cast<double>(seconds) * kMilliseconds));
    if (release) {
        button->set_held_down(0);
        button->reset_activation();
        button->forget_let_go();
    }
}

// 0x0041E8F0
int PadInputModule::lock_button_from_script(script::Struct* params, script::Script*)
{
    uint32_t name = 0;
    if (params->read_hash_value(0u, &name, 0)) {
        float time = 1.0f;
        params->fetch_float(kTime, &time, 0);
        // (the original's file name: its __FILE__ from 12 characters in)
        debug::printf("D:%s:%i: time = %g\n", "omponents\\InputComponent.cpp", 0x94, static_cast<double>(time));
        int clear = 0;
        params->fetch_int_value(kClear, &clear, 0);
        lock_button(name, time, clear != 0);
    }
    return 1;
}

// 0x0041E9A0
int PadInputModule::disable_player_input(script::Struct*, script::Script*)
{
    disabled_ = true;
    return 1;
}

// 0x0041E9B0
int PadInputModule::enable_player_input(script::Struct*, script::Script*)
{
    disabled_ = false;
    return 1;
}

// 0x0041E9C0
int PadInputModule::net_disable_player_input(script::Struct*, script::Script*)
{
    net_disabled_ = true;
    return 1;
}

// 0x0041E9D0
int PadInputModule::net_enable_player_input(script::Struct*, script::Script*)
{
    net_disabled_ = false;
    return 1;
}

// 0x0041E7C0
int PadInputModule::enable_input_events(script::Struct*, script::Script*)
{
    input_events_ = true;
    return 1;
}

// 0x0041E7D0
int PadInputModule::disable_input_events(script::Struct*, script::Script*)
{
    input_events_ = false;
    return 1;
}

// 0x0041E9E0
int PadInputModule::player_input_is_disabled(script::Struct*, script::Script*)
{
    return disabled_ || net_disabled_ ? 1 : 0;
}

// 0x0041EA20
int PadInputModule::left_pressed(script::Struct*, script::Script*)
{
    return pad_primary_.left_side_.held_down_ != 0 ? 1 : 0;
}

// 0x0041EA30
int PadInputModule::right_pressed(script::Struct*, script::Script*)
{
    return pad_primary_.right_side_.held_down_ != 0 ? 1 : 0;
}

// 0x0041EA40
int PadInputModule::up_pressed(script::Struct*, script::Script*)
{
    return pad_primary_.up_dir_.held_down_ != 0 ? 1 : 0;
}

// 0x0041EA50
int PadInputModule::down_pressed(script::Struct*, script::Script*)
{
    return pad_primary_.btn_down_.held_down_ != 0 ? 1 : 0;
}

// 0x0041EA60
int PadInputModule::get_sticks(script::Struct*, script::Script* script)
{
    script->args()->push_float(0x737F15CF, core::at(pad_primary_.polar_, 1).angle * kDegreesPerRadian);
    script->args()->push_float(
        0x686AD1C2,
        static_cast<float>(stick_length(core::at(pad_primary_.stick_, 2), core::at(pad_primary_.stick_, 3), core::at(pad_primary_.polar_, 1).angle)));
    script->args()->push_float(0xD6030CEA, core::at(pad_primary_.polar_, 0).angle * kDegreesPerRadian);
    script->args()->push_float(
        0xC94777AC,
        static_cast<float>(stick_length(core::at(pad_primary_.stick_, 0), core::at(pad_primary_.stick_, 1), core::at(pad_primary_.polar_, 0).angle)));
    return 1;
}

// 0x0041EB20
int PadInputModule::held_longer_than(script::Struct* params, script::Script*)
{
    uint32_t name = 0;
    params->read_hash_value(kButton, &name, 1);
    float time = 0.0f;
    std::memcpy(&time, static_cast<const void*>(&params),
                sizeof(time)); // (the original's slot for it: the params argument)
    params->fetch_float(0u, &time, 1);
    double scaled = time;
    if (params->has_bare_flag(kSeconds) || params->has_bare_flag(kSecond))
        scaled = static_cast<double>(time) * kMilliseconds;
    else if (params->has_bare_flag(kFrames) || params->has_bare_flag(kFrame))
        scaled = static_cast<double>(time) * kFrameMilliseconds;
    auto milliseconds = static_cast<uint32_t>(get_truncated(scaled));
    ControlButton* button = pad_primary_.read_button(name);
    if (button && button->held_down_ && ints::as_unsigned(button->get_held_millis()) > milliseconds)
        return 1;
    return 0;
}

} // namespace objects
