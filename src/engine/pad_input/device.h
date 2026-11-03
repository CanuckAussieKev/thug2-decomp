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

// The PS2-format control data (InputPort::control_data(); every per-port buffer is kPortBufferBytes long): the status bytes, the two bytes of button bits
// (each set while the button is up), the sticks, a pressure per button, and the Xbox pad's black and white buttons.
constexpr size_t kPortBufferBytes = 32;
constexpr int kControlModeByte = 1;
constexpr int kControlButtonsHigh = 2;
constexpr int kControlButtonsLow = 3;
constexpr int kControlRightStickX = 4;
constexpr int kControlRightStickY = 5;
constexpr int kControlLeftStickX = 6;
constexpr int kControlLeftStickY = 7;
constexpr int kPressureRight = 8;
constexpr int kPressureLeft = 9;
constexpr int kPressureUp = 10;
constexpr int kPressureDown = 11;
constexpr int kPressureTriangle = 12;
constexpr int kPressureCircle = 13;
constexpr int kPressureCross = 14;
constexpr int kPressureSquare = 15;
constexpr int kPressureL1 = 16;
constexpr int kPressureR1 = 17;
constexpr int kPressureL2 = 18;
constexpr int kPressureR2 = 19;
constexpr int kControlExtraButtons = 20; // bit 0 black, bit 1 white
constexpr int kAnalogModeByte = 0x70;
constexpr int kFullPressure = 0xFF;
// The bits of the low button byte (the face buttons and the shoulders) and of the high byte's d-pad.
constexpr int kLowBitL2 = 1;
constexpr int kLowBitR2 = 2;
constexpr int kLowBitL1 = 4;
constexpr int kLowBitR1 = 8;
constexpr int kLowBitTriangle = 0x10;
constexpr int kLowBitCircle = 0x20;
constexpr int kLowBitCross = 0x40;
constexpr int kLowBitSquare = 0x80;
constexpr int kCircleBit = 5; // the same as shift counts: circle, cross, square in the low byte; right, down, left in the high one
constexpr int kCrossBit = 6;
constexpr int kSquareBit = 7;
constexpr int kRightBit = 5;
constexpr int kDownBit = 6;
constexpr int kLeftBit = 7;
// The platform layer's pad snapshot: its pressures (the Xbox pad's) and its button bits.
constexpr int kSnapshotA = 0; // cross
constexpr int kSnapshotB = 1; // circle
constexpr int kSnapshotX = 2; // square
constexpr int kSnapshotY = 3; // triangle
constexpr int kSnapshotBlack = 4;
constexpr int kSnapshotWhite = 5;
constexpr int kSnapshotLeftTrigger = 6;
constexpr int kSnapshotRightTrigger = 7;
constexpr size_t kSnapshotPressureCount = 8;
constexpr int kPressedThreshold = 0x20;
constexpr int kHardPressThreshold = 0x80;

class InputPort : public memory::HeapObject {
public:
    // The states Process steps through while a pad is found.
    enum class State : uint8_t {
        kIdle = 0,
        kDetect = 1,
        kReady = 2,
        kSettle = 5,
    };
    // Capability bits.
    static constexpr uint32_t kHasActuators = 2;
    static constexpr uint32_t kAnalog = 4;

    // 0x005BD920. Leaves buffer_raw_ unset and reads it (the PS2 version's DMA buffer, aligned to 64 bytes).
    // (the port's place: its index in the list, its port and its slot)
    struct Place {
        int index{};
        int port{};
        int slot{};
    };
    explicit InputPort(Place place);
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

    // 0x005BD200: the original's pad read (Process, state State::kReady): the platform layer's pad state for this
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

    // The fields other code reads and sets.
    memory::ListNode*& list_link() { return list_link_; }
    [[nodiscard]] memory::ListNode* list_link() const { return list_link_; }
    [[nodiscard]] int port_no() const { return port_no_; }
    [[nodiscard]] int seat() const { return seat_; }
    [[nodiscard]] uint8_t is_usable() const { return is_usable_; }
    void set_is_usable(uint8_t value) { is_usable_ = value; }
    std::array<uint8_t, 3>& pad15() { return pad15_; }
    [[nodiscard]] const std::array<uint8_t, 3>& pad15() const { return pad15_; }
    std::array<uint8_t, kPortBufferBytes>& control_data() { return control_data_; }
    [[nodiscard]] const std::array<uint8_t, kPortBufferBytes>& control_data() const { return control_data_; }
    std::array<uint8_t, kPortBufferBytes>& rumble_cap() { return rumble_cap_; }
    [[nodiscard]] const std::array<uint8_t, kPortBufferBytes>& rumble_cap() const { return rumble_cap_; }
    void set_capabilities(uint32_t value) { capabilities_ = value; }
    [[nodiscard]] uint32_t unknownc8() const { return unknownc8_; }
    void set_rumble_motor_count(int value) { rumble_motor_count_ = value; }
    void set_cur_phase(State value) { cur_phase_ = value; }
    [[nodiscard]] int ordinal() const { return ordinal_; }
    void set_is_connected(bool value) { is_connected_ = value; }
    std::array<uint8_t, 3>& pade1() { return pade1_; }
    [[nodiscard]] const std::array<uint8_t, 3>& pade1() const { return pade1_; }
    void set_held_down(uint8_t value) { held_down_ = value; }
    void set_keyboard_control(uint8_t value) { keyboard_control_ = value; }
    std::array<uint8_t, 2>& padee() { return padee_; }
    [[nodiscard]] const std::array<uint8_t, 2>& padee() const { return padee_; }
    void set_start_or_a(uint32_t value) { start_or_a_ = value; }

private:
    // (The original's vtable held only the destructor: no vtable here.)
    memory::ListNode* list_link_; // in the manager's device list
    int kind_tag_{};
    int port_no_;
    int seat_;
    uint8_t is_usable_{0};
    std::array<uint8_t, 3> pad15_{};
    uint32_t octets_;          // buffer_raw_ rounded up to 64
    std::array<uint8_t, kPortBufferBytes> control_data_{}; // PS2 format: buttons (inverted bits), sticks, pressures
    std::array<uint8_t, kPortBufferBytes> align_data_{};
    std::array<uint8_t, kPortBufferBytes> rumble_raw_{}; // each motor's level
    std::array<uint8_t, kPortBufferBytes> rumble_cap_{};
    std::array<uint8_t, kPortBufferBytes> actuator_saved_{}; // levels while paused
    int pause_count_{0};
    uint8_t rumble_off_{};
    uint32_t capabilities_{0};
    uint32_t unknownc8_{};
    int rumble_motor_count_{0};
    uint32_t buffer_raw_{};
    State cur_phase_{State::kIdle}; // (the original's int: the rest is zero)
    std::array<uint8_t, 3> unknown_phase_pad_{};
    State next_state_{State::kIdle}; // after kSettle (the original's int)
    std::array<uint8_t, 3> unknown_next_pad_{};
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
constexpr size_t kPadSnapshotSticksOffset = 0x0E;
constexpr size_t kPadSnapshotBytes = 0x18;
struct PadSnapshot {
    uint32_t unknown0;
    uint16_t buttons;    // bits: 0x10 start...; 0x01 / 0x02 / 0x04 / 0x08 the d-pad
    std::array<uint8_t, kSnapshotPressureCount> pressure; // analogue buttons and triggers, 0-255
    std::array<int16_t, 4> sticks;   // -32768..32767
    std::array<uint8_t, 2> unknown16;
};
static_assert(offsetof(PadSnapshot, sticks) == kPadSnapshotSticksOffset && sizeof(PadSnapshot) == kPadSnapshotBytes, "layout must match the original");

// The platform layer's motor control (os_layer::set_actuators, 0x005F3A50: PartyMod's): levels 0-65535.
void set_actuators(int port, int left, int right);

// The keyboard (DirectInput): 0x005BDD60 reads its state once a frame into one of two 256-byte buffers;
// typed characters are queued elsewhere (key_input).
void read_keyboard();
// 0x005BDD30 (also reached by a jump at 0x005BCC40): the typed characters, one byte each; returns how many.
int get_keyboard_chars(std::span<char> out);
// 0x005BDE20 (also through 0x005BCC50): forget them.
void clear_keyboard_chars();

// A key and its flags.
struct KeyEvent {
    int key{};
    int flags{};
};

// 0x005BDE70: a typed key (a character, or a control key: Enter, Backspace, Escape...) from the window's
// messages, queued for get_keyboard_chars while the keyboard is in use for typing. `flags` is the message's
// l_param (bit 30: auto-repeat; negative: a key release). PartyMod calls it for SDL's text input.
void key_input(KeyEvent key_event);

// 0x005BDF30: a key press from the window's messages (virtual key codes): F5 refreshes the network server
// list while it's shown; otherwise, with typed keys wanted, the arrow keys (in a menu) or F1-F4 are queued
// as 0x14-0x17 / 0x1B-0x1E.
void special_key_input(KeyEvent key_event);
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

    // The fields other code reads and sets.
    std::unique_ptr<tasks::Task<memory::ListNode>>& task() { return task_; }
    [[nodiscard]] const std::unique_ptr<tasks::Task<memory::ListNode>>& task() const { return task_; }

private:
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

// NOT ORIGINAL - the input devices' manager (null until made).
Hub* get_device_manager();

} // namespace sio
