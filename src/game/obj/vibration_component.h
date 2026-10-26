#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/object.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script

namespace Sio {
class Device;
}

// The Vibration component (Obj::CVibrationComponent, type 0x404EB936 "vibration", vtable 0x00646864, 0x38 bytes; made
// by 0x0043D670): a controller's two vibration motors driven by script - the member function vibrate runs a motor
// ("actuator") at a percentage, for a duration (stopped by the update when it's over) or until told "off". The
// skater code gives it its device (0x0043D5C0) and turns it on and off (0x0043D560); while off it stays suspended.
//
// Vtable: 0 Destroy, 1 Update; the empty 0x005BAB90 / 0x005480A0 for InitFromStructure and Finalize, 0x004E3F90,
// 0x0048DCD0, the base's Suspend, 11 0x0042BB20 (member function entries by table: vibrate, the table at 0x00684EE8,
// its member function pointer written by the static initialiser 0x006448C0), 12 CallMemberFunction 0x0051ADC0 (not
// mine).
namespace Obj {

class CVibrationComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0x404EB936;
    static constexpr int kActuators = 2;

    // A motor's timed run (0xC bytes from +0x1C).
    struct Timer {
        bool running;        // +0 a duration was given
        uint8_t pad1[3];
        int start;           // +4 the game clock (with its offset) when it started
        uint32_t duration;   // +8 ms
    };

    // 0x0043D670 (registered with the composite object manager): a new one (0 if out of memory): off, no runs.
    static CBaseComponent* __cdecl Create();
    // 0x0043D490 (not a Ghidra function; registered with Create): its script member functions (the table at
    // 0x00684EE8: vibrate).
    static void __cdecl RegisterMemberFunctions();
    // 0x0043D4B0: the destructor body: with a device, both motors stopped (1, then 0); then the base's.
    void DestroyBody();
    // 0x0043D5E0 (vtable slot 0)
    CVibrationComponent* Destroy(unsigned int flags);
    // 0x0043D610 (vtable slot 1): off: suspended (through the vtable); on: the timed runs (0x0043D61F).
    void Update();
    // 0x0043D61F (the rest of 0x0043D610, which Ghidra split off): each motor (1, then 0) whose run's duration has
    // passed on the game clock is stopped.
    void UpdateTimers();

    // 0x0043D520: when on, both motors stopped (1, then 0) and their runs cleared.
    void StopAll();
    // 0x0043D550: the device's motors reset (Sio::Device::ResetActuators).
    void ResetDevice();
    // 0x0043D560: on or off: off stops and disables the device's motors and suspends the component (through the
    // vtable); on unsuspends it and enables them. Nothing when it doesn't change.
    void SetActive(bool active);
    // 0x0043D5C0: the device, its motors enabled or disabled as the component is on or off.
    void SetDevice(Sio::Device* device);

    // --- script member functions (the table at 0x00684EE8) ---
    // 0x0043D6F0 vibrate (not a Ghidra function): unless "off" is given, nothing while the game or the object is
    // paused; nothing while off. actuator (0x114FC131, default 0) runs at percent (0x9E497FC6, truncated; default
    // 0), or with "off" every motor stops (StopActuators) and both runs are cleared. With duration (0x79A07F3F,
    // seconds) the motor's run is timed, else its run is cleared. True.
    bool Vibrate(Script::CStruct* params, Script::CScript* script);

    bool m_active;                 // +0x18
    uint8_t pad19[3];
    Timer m_timers[kActuators];    // +0x1C
    Sio::Device* mp_device;        // +0x34
};
static_assert(offsetof(CVibrationComponent, m_active) == 0x18 && offsetof(CVibrationComponent, m_timers) == 0x1C &&
                  offsetof(CVibrationComponent, mp_device) == 0x34 && sizeof(CVibrationComponent) == 0x38,
              "layout must match the original");

} // namespace Obj
