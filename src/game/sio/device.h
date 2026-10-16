#pragma once

#include <cstddef>
#include <cstdint>

namespace Mem {
struct ListNode;
}

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

// The platform layer's motor control (0x005F3A50; PartyMod replaces it): levels 0-65535.
void SetActuators(int port, int left, int right);

// The keyboard (DirectInput): 0x005BDD60 reads its state once a frame into one of two 256-byte buffers;
// typed characters are queued elsewhere (KeyInput).
void ReadKeyboard();
// 0x005BDD30 (also reached by a jump at 0x005BCC40): the typed characters, one byte each; returns how many.
int GetKeyboardChars(char* out);
// 0x005BDE20 (also through 0x005BCC50): forget them.
void ClearKeyboardChars();

// The device manager: one, shared (reference counted).
class Manager;
// 0x005BE670: the manager, made on first use when `create` (counts a reference either way); returns it
// (null if there is none).
Manager* CreateManager(bool create);
// 0x005BDCF0: drop a reference; the last one deletes it.
void ReleaseManager();
Manager*& ManagerInstance();

} // namespace Sio

namespace Spt {

// The base of the engine's singletons and other classes with a virtual destructor (vtable 0x00658AA4).
// Assumed to be Neversoft's Spt::Class.
class Class {
public:
    // 0x005BDE30: the destructor (only resets the vtable).
    void DestroyBody();
    // 0x005BDE40: the scalar deleting destructor.
    Class* Destroy(unsigned int flags);

    void** m_vtable;
};

} // namespace Spt
