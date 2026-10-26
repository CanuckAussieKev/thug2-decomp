#pragma once

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"

// Serial I/O: the controller ports. A Device per port polls its pad (on PC: a DirectInput joystick, or the
// keyboard bindings) into PS2-format control data, and drives its two vibration motors ("actuators").
// PartyMod replaces the polling (Device::Process) with SDL game controllers and its own keyboard handling.
namespace Sio {

class Device {
public:
    // The states Process steps through while a pad is found.
    enum State : int { kIdle = 0, kDetect = 1, kReady = 2, kSettle = 5 };
    // Capability bits.
    enum : uint32_t { kHasActuators = 2, kAnalog = 4 };

    // 0x005BD920. Leaves m_bufferRaw unset and reads it (the PS2 version's DMA buffer, aligned to 64 bytes).
    Device(int index, int port, int slot);
    // 0x005BCD00: the scalar deleting destructor (deletes the list node; frees the device if flags & 1).
    Device* Destroy(unsigned int flags);

    // 0x005BDCC0: once a frame. PartyMod's (see device.cpp).
    void Process(int unused);
    // 0x005BCC60
    bool IsPluggedIn() const { return m_pluggedIn; }
    // 0x005BCC70: start looking for a pad, if idle and one is there.
    void Acquire();
    // 0x005BD8B0: a pad is there: an analogue pad with two motors (type 7); settle, then ready.
    void Detect();
    // 0x005BCCC0: one frame's wait, then the state after it.
    void Settle();

    // 0x005BD200: the original's pad read (Process, state kReady): the platform layer's pad state for this
    // port into PS2-format control data, with the menus deciding which port and keys count. PartyMod's
    // Process does its own reading, so this isn't run in the game.
    void ReadData(int unused);

    // 0x005BDA20: run motor `actuator` at `percent` of its maximum. PartyMod removes the original's first
    // step, reading the pad (see device.cpp).
    void ActivateActuator(int actuator, int percent);
    // 0x005BCCB0: allow vibration again (the motors stay as they are).
    void EnableActuators() { m_actuatorsDisabled = 0; }
    // 0x005BDAD0: stop the motors and refuse further vibration.
    void DisableActuators();
    // 0x005BDB30: stop the motors. Ends disabled if it was disabled, else enabled (see device.cpp).
    void ResetActuators();
    // 0x005BDB60 / 0x005BDBD0: stop the motors, remembering their levels / restore them (counted: pauses
    // nest).
    void PauseActuators();
    void UnpauseActuators();
    // 0x005BDC60: stop the motors and forget the remembered levels.
    void StopActuators();

    void** m_vtable;
    Mem::ListNode* m_node; // in the manager's device list
    int m_type;
    int m_port;
    int m_slot;
    uint8_t m_valid;
    uint8_t m_pad15[3];
    uint32_t m_buffer; // m_bufferRaw rounded up to 64
    uint8_t m_controlData[32]; // PS2 format: buttons (inverted bits), sticks, pressures
    uint8_t m_alignData[32];
    uint8_t m_actuatorDirect[32]; // each motor's level
    uint8_t m_actuatorMax[32];
    uint8_t m_actuatorSaved[32]; // levels while paused
    int m_pauseCount;
    uint8_t m_actuatorsDisabled;
    uint8_t m_padC1[3];
    uint32_t m_capabilities;
    uint32_t m_unknownC8;
    int m_numActuators;
    uint32_t m_bufferRaw;
    int m_state;
    int m_nextState; // after kSettle
    int m_index;
    bool m_pluggedIn;
    uint8_t m_padE1[3];
    int m_unpluggedCounter;
    int m_unpluggedRetry; // rand() & 63
    uint8_t m_pressed; // any button down
    uint8_t m_keyboardControl; // set by the original read (start, the keyboard's char 0x19...)
    uint8_t m_padEE[2];
};
static_assert(offsetof(Device, m_controlData) == 0x1C && offsetof(Device, m_actuatorDirect) == 0x5C &&
                  offsetof(Device, m_pauseCount) == 0xBC && offsetof(Device, m_state) == 0xD4 &&
                  offsetof(Device, m_pluggedIn) == 0xE0 && sizeof(Device) == 0xF0,
              "layout must match the original");

// The platform layer's pad state for a port (0x005F5BC0 fills it).
struct PadState {
    uint32_t unknown0;
    uint16_t buttons;     // bits: 0x10 start...; 0x01 / 0x02 / 0x04 / 0x08 the d-pad
    uint8_t pressure[8];  // analogue buttons and triggers, 0-255
    int16_t sticks[4];    // -32768..32767
    uint8_t unknown16[2];
};
static_assert(offsetof(PadState, sticks) == 0x0E && sizeof(PadState) == 0x18, "layout must match the original");

// The platform layer's motor control (Platform::SetActuators, 0x005F3A50: PartyMod's): levels 0-65535.
void SetActuators(int port, int left, int right);

// The keyboard (DirectInput): 0x005BDD60 reads its state once a frame into one of two 256-byte buffers;
// typed characters are queued elsewhere (KeyInput).
void ReadKeyboard();
// 0x005BDD30 (also reached by a jump at 0x005BCC40): the typed characters, one byte each; returns how many.
int GetKeyboardChars(char* out);
// 0x005BDE20 (also through 0x005BCC50): forget them.
void ClearKeyboardChars();

// 0x005BDE70: a typed key (a character, or a control key: Enter, Backspace, Escape...) from the window's
// messages, queued for GetKeyboardChars while the keyboard is in use for typing. `flags` is the message's
// lParam (bit 30: auto-repeat; negative: a key release). PartyMod calls it for SDL's text input.
void KeyInput(int key, int flags);

// 0x005BDF30: a key press from the window's messages (virtual key codes): F5 refreshes the network server
// list while it's shown; otherwise, with typed keys wanted, the arrow keys (in a menu) or F1-F4 are queued
// as 0x14-0x17 / 0x1B-0x1E.
void SpecialKeyInput(int key, int flags);
// 0x005BCD50: the name of the key for a pad button (0-29) on port 0, for the menus' hints - from the key
// bindings or fixed keys, depending on whether a menu is up or the keyboard is being typed on. A static
// buffer.
const char* GetButtonKeyName(int button);

// The device manager (0x1C bytes, vtable 0x00658AD4): a device per controller port, processed by a task
// once a frame. One, shared (reference counted).
class Manager {
public:
    // 0x005BE1F0: the task (not started here) and a device for ports 0 and 1, each acquiring.
    Manager();
    // 0x005BE300: deletes the devices and the task.
    void DestroyBody();
    // 0x005BE6C0: the scalar deleting destructor.
    Manager* Destroy(unsigned int flags);

    // 0x005BE3F0: process each device (the first is told so).
    void ProcessDevices();
    // 0x005BE490 / 0x005BE4F0: the device on that port and slot / with that index (null if none).
    Device* GetDevice(int port, int slot);
    Device* GetDeviceByIndex(int index);
    // 0x005BE550 / 0x005BE5E0: pause / unpause every device's motors.
    void PauseDevices();
    void UnpauseDevices();

    void** m_vtable;
    Mem::ListNode m_devices; // the list's head
    void* m_task;            // Tsk::Task running ProcessTask with the list as its data
};
static_assert(offsetof(Manager, m_task) == 0x18 && sizeof(Manager) == 0x1C, "layout must match the original");

// 0x005BE110: the scalar deleting destructor of a device's list node (Lst::Node<Device>, vtable 0x00658A70).
Mem::ListNode* DestroyDeviceNode(Mem::ListNode* node, unsigned int flags);

// 0x005BE140: the manager's task - processes each device in the list it was given.
void __cdecl ProcessTask(void* task);

// 0x005BE670: the manager, made on first use when `create` (counts a reference either way); returns it
// (null if there is none).
Manager* CreateManager(bool create);
// 0x005BDCF0: drop a reference; the last one deletes it.
void ReleaseManager();
Manager*& ManagerInstance();

} // namespace Sio
