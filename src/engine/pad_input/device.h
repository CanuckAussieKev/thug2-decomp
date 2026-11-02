#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <memory>

#include "engine/core/arena_alloc.h"
#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include <gsl/pointers>
#include <array>

// Serial I/O: the controller ports. A Device per port polls its pad (on PC: a DirectInput joystick, or the
// keyboard bindings) into PS2-format control data, and drives its two vibration motors ("actuators").
// PartyMod replaces the polling (InputPort::work_on) with SDL game controllers and its own keyboard handling.
namespace sio {

class InputPort : public memory::HeapObject {
public:
    // The states Process steps through while a pad is found.
    // NOLINTNEXTLINE(cppcoreguidelines-use-enum-class): held in the original's integer fields (to convert with them)
    enum State : uint8_t { kIdle = 0, kDetect = 1, kReady = 2, kSettle = 5 };
    // Capability bits.
    static constexpr uint32_t kHasActuators = 2;
    static constexpr uint32_t kAnalog = 4;

    // 0x005BD920. Leaves buffer_raw_ unset and reads it (the PS2 version's DMA buffer, aligned to 64 bytes).
    InputPort(int index, int port, int slot);
    // 0x005BCD00: the list node deleted.
    ~InputPort();
    InputPort(const InputPort&) = delete;
    InputPort& operator=(const InputPort&) = delete;

    // 0x005BDCC0: once a frame. PartyMod's (see device.cpp).
    void work_on(int unused);
    // 0x005BCC60
    [[nodiscard]] bool is_attached() const { return is_connected_; }
    // 0x005BCC70: start looking for a pad, if idle and one is there.
    void acquire();
    // 0x005BD8B0: a pad is there: an analogue pad with two motors (type 7); settle, then ready.
    void detect();
    // 0x005BCCC0: one frame's wait, then the state after it.
    void get_settle();

    // 0x005BD200: the original's pad read (Process, state kReady): the platform layer's pad state for this
    // port into PS2-format control data, with the menus deciding which port and keys count. PartyMod's
    // Process does its own reading, so this isn't run in the game.
    void pull_data(int unused);

    // 0x005BDA20: run motor `actuator` at `percent` of its maximum. PartyMod removes the original's first
    // step, reading the pad (see device.cpp).
    void fire_actuator(int actuator, int percent);
    // 0x005BCCB0: allow vibration again (the motors stay as they are).
    void enable_actuators() { rumble_off_ = 0; }
    // 0x005BDAD0: stop the motors and refuse further vibration.
    void stop_rumble();
    // 0x005BDB30: stop the motors. Ends disabled if it was disabled, else enabled (see device.cpp).
    void clear_rumble();
    // 0x005BDB60 / 0x005BDBD0: stop the motors, remembering their levels / restore them (counted: pauses
    // nest).
    void pause_actuators();
    void unpause_actuators();
    // 0x005BDC60: stop the motors and forget the remembered levels.
    void stop_actuators();

    // (The original's vtable held only the destructor: no vtable here.)
    memory::ListNode* list_link_; // in the manager's device list
    int kind_tag_{};
    int port_no_;
    int seat_;
    uint8_t is_usable_;
    std::array<uint8_t, 3> pad15_{};
    uint32_t octets_;          // buffer_raw_ rounded up to 64
    std::array<uint8_t, 32> control_data_{}; // PS2 format: buttons (inverted bits), sticks, pressures
    std::array<uint8_t, 32> align_data_{};
    std::array<uint8_t, 32> rumble_raw_{}; // each motor's level
    std::array<uint8_t, 32> rumble_cap_{};
    std::array<uint8_t, 32> actuator_saved_{}; // levels while paused
    int pause_count_{0};
    uint8_t rumble_off_{};
    uint32_t capabilities_;
    uint32_t unknownc8_{};
    int rumble_motor_count_;
    uint32_t buffer_raw_{};
    int cur_phase_;
    int next_state_; // after kSettle
    int ordinal_;
    bool is_connected_{};
    std::array<uint8_t, 3> pade1_{};
    int disconnected_ticks_{};
    int reconnect_delay_;      // rand() & 63
    uint8_t held_down_{};      // any button down
    uint8_t keyboard_control_{}; // set by the original read (start, the keyboard's char 0x19...)
    std::array<uint8_t, 2> padee_{};
    // Not original: PartyMod's "start or A pressed", written by its pad read (sdl_input.cpp) and never read. (PartyMod
    // wrote it just past the end of the original's 0xF0-byte device.)
    uint32_t start_or_a_ = 0;
};

// The platform layer's pad state for a port (0x005F5BC0 fills it).
struct PadSnapshot {
    uint32_t unknown0;
    uint16_t buttons;    // bits: 0x10 start...; 0x01 / 0x02 / 0x04 / 0x08 the d-pad
    std::array<uint8_t, 8> pressure; // analogue buttons and triggers, 0-255
    std::array<int16_t, 4> sticks;   // -32768..32767
    std::array<uint8_t, 2> unknown16;
};
static_assert(offsetof(PadSnapshot, sticks) == 0x0E && sizeof(PadSnapshot) == 0x18, "layout must match the original");

// The platform layer's motor control (os_layer::set_actuators, 0x005F3A50: PartyMod's): levels 0-65535.
void set_actuators(int port, int left, int right);

// The keyboard (DirectInput): 0x005BDD60 reads its state once a frame into one of two 256-byte buffers;
// typed characters are queued elsewhere (key_input).
void read_keyboard();
// 0x005BDD30 (also reached by a jump at 0x005BCC40): the typed characters, one byte each; returns how many.
int get_keyboard_chars(std::span<char> out);
// 0x005BDE20 (also through 0x005BCC50): forget them.
void clear_keyboard_chars();

// 0x005BDE70: a typed key (a character, or a control key: Enter, Backspace, Escape...) from the window's
// messages, queued for get_keyboard_chars while the keyboard is in use for typing. `flags` is the message's
// l_param (bit 30: auto-repeat; negative: a key release). PartyMod calls it for SDL's text input.
void key_input(int key, int flags);

// 0x005BDF30: a key press from the window's messages (virtual key codes): F5 refreshes the network server
// list while it's shown; otherwise, with typed keys wanted, the arrow keys (in a menu) or F1-F4 are queued
// as 0x14-0x17 / 0x1B-0x1E.
void special_key_input(int key, int flags);
// 0x005BCD50: the name of the key for a pad button (0-29) on port 0, for the menus' hints - from the key
// bindings or fixed keys, depending on whether a menu is up or the keyboard is being typed on. A static
// buffer.
const char* get_button_key_name(int button);

// The device manager (the original's vtable 0x00658AD4 held only its destructor): a device per controller port,
// processed by a task once a frame. One, shared (reference counted).
class Hub : public memory::HeapObject {
public:
    // 0x005BE1F0: the task (not started here) and a device for ports 0 and 1, each acquiring.
    Hub();
    // 0x005BE300 (0x005BE6C0 the deleting one): deletes the devices and the task.
    ~Hub();
    Hub(const Hub&) = delete;
    Hub& operator=(const Hub&) = delete;

    // 0x005BE3F0: process each device (the first is told so).
    void poll_pads();
    // 0x005BE490 / 0x005BE4F0: the device on that port and slot / with that index (null if none).
    InputPort* fetch_gadget(int port, int slot);
    InputPort* get_pad_at_slot(int index);
    // 0x005BE550 / 0x005BE5E0: pause / unpause every device's motors.
    void pause_devices();
    void unpause_devices();

    memory::ListNode devs_{};                             // the list's head
    std::unique_ptr<tasks::Task<memory::ListNode>> task_; // running process_task with the list as its data
};

// (0x005BE110, the scalar deleting destructor of a device's list node - lists::Elem<Device> - is
// memory::ListNode::destroy.)

// 0x005BE140: the manager's task - processes each device in the list it was given.
void process_task(tasks::Task<memory::ListNode>* task);

// 0x005BE670: the manager, made on first use when `create` (counts a reference either way); returns it
// (null if there is none).
Hub* create_manager(bool create);
// 0x005BDCF0: drop a reference; the last one deletes it.
void release_manager();
std::unique_ptr<Hub>& manager_instance();

extern std::unique_ptr<Hub> g_manager;

} // namespace sio
