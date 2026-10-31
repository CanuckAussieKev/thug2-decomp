#pragma once

#include <cstddef>
#include <cstdint>

#include "game/core/task_list.h"
#include "game/obj/control_pad.h"
#include "game/obj/entity.h"

namespace Scripts {
class CScript;
class CStruct;
} // namespace Script

namespace GameObjects {

// The input component (type 0x27D7CD28 "input", vtable 0x0064605C, 0x4A0 bytes; made by 0x0041EEC0): an object's
// controller - two control pads fed once a frame by input handlers on the input manager's controller servers (the
// first on the object's controller, the second always on controller 1), and, with input events on, the object's own
// pad events (one event type down and one up for each of 17 buttons).
//
// Vtable: 0 the scalar deleting destructor, 2 ConfigureFromData; the rest the base's / shared (1 0x0051B970, 3 / 4 / 9
// 0x005480A0, 5 0x004E3F90, 6 0x0048DCD0, 7 0x0045D760, 8 / 10 0x005BAB90, 11 0x0042BB20, 12 0x0051ADC0).
class CPadInputModule : public CObjectModule {
public:
    static constexpr uint32_t kType = 0x27D7CD28;

    // Its input handler (PadInput::Responder<CPadInputModule>, 0x2C bytes, vtable 0x0064D7D8; the front end's has the same
    // shape).
    struct Responder : Scheduling::SchedulerTask {
        uint8_t* m_controlData;         // +0x18 its server's data (the button pressures first)
        void* m_dev;                 // +0x1C its server's Sio device
        int m_ordinal;                    // +0x20 which of the two
        void(__cdecl* m_codeValue)(Responder*); // +0x24
        CPadInputModule* m_component;   // +0x28
    };

    // 0x0041EEC0 (registered with the composite object manager by 0x004E4860): a new one (0 if out of memory).
    static CObjectModule* __cdecl Fabricate();
    // 0x0041E6F0 (not a Ghidra function; registered with Create): its member functions' table (0x00684C78; the
    // functions are put in by the static initialiser at 0x006445B0).
    static void __cdecl PublishMethodTable();
    // 0x0041E850: the base's constructor, its vtable, both pads' Init, its type; no handler, 0x49C cleared. Returns
    // this.
    CPadInputModule* MakeNew();
    // 0x0041E710: each handler out of its list and deleted; then the base's body.
    void DestroyBody();
    // 0x0041E8C0 (vtable slot 0)
    CPadInputModule* Teardown(unsigned int flags);
    // 0x0041F390 (vtable slot 2): the controller of Player (the input manager's) or controller (LinkToPad);
    // 0xB9B1E3B6 into +0x49C.
    void ConfigureFromData(Scripts::CStruct* params);
    // 0x0041F1B0: the handlers made the first time (on the skater info heap; priority -1), then the first moved to
    // `controller`'s server and the second to controller 1's; the vibration component's device set to the first's.
    void LinkToPad(int controller);

    // 0x0041F170 / 0x0041F190 (cdecl; not Ghidra functions): the handlers' code - UpdatePad / UpdatePad2 of their
    // component with their data.
    static void __cdecl HandlerCode(Responder* handler);
    static void __cdecl HandlerCode2(Responder* handler);
    // 0x0041F090: unless its object is paused: the pad events (UpdateEvents), then the pad updated from `data` - or,
    // with input disabled (or net disabled) or blocked (IsInputBlocked), reset (and, blocked, no events this frame).
    void UpdatePad(const uint8_t* data);
    // 0x0041F100: the same for the second pad, without the events.
    void UpdatePad2(const uint8_t* data);
    // 0x0041EF20: in a network game, whether something on screen takes the input: the screen element
    // controller_unplugged_dialog_anchor, or root_window with menu_state = on in its tags, or keyboard_anchor. False
    // outside network games.
    bool IsInputBlocked();
    // 0x0041ECA0: the last frame's events kept; this frame's from the 17 buttons (ReadButtonState).
    void UpdateEvents(const uint8_t* data);
    // 0x0041EBF0: a button's event: while it's locked (its lock time not yet reached: FrameClock::GetTime, unsigned) nothing;
    // down (its pressure in `data`) - its `down` event to the object (when it wasn't down last frame, input events are
    // on and input isn't disabled) and its bit set; up - its `up` event (when it was down last frame).
    void ReadButtonState(const uint8_t* data, int index, uint32_t bit, uint32_t down, uint32_t up, CControlButton* button);
    // 0x0041E7E0: `name`'s button (none: a crash, as the original) locked for `seconds` (its lock time: FrameClock::GetTime +
    // seconds * 1000, truncated, as a float); with `release`, also released and its events taken.
    void LockButton(uint32_t name, float seconds, bool release);

    // --- the member functions scripts call (the table at 0x00684C78) ---
    // 0x0041E8F0 [0xBF7D6C46]: the unnamed checksum's button locked for `time` seconds (1), released with `clear`
    // (an integer); its time printed (compiled out). 1.
    int LockButtonFromScript(Scripts::CStruct* params, Scripts::CScript* script);
    // 0x0041E9A0 / 0x0041E9B0 [DisablePlayerInput / enableplayerinput], 0x0041E9C0 / 0x0041E9D0
    // [NetDisablePlayerInput / NetEnablePlayerInput], 0x0041E7C0 / 0x0041E7D0 [EnableInputEvents / 0x28729460]. 1.
    int DisablePlayerInput(Scripts::CStruct* params, Scripts::CScript* script);
    int EnablePlayerInput(Scripts::CStruct* params, Scripts::CScript* script);
    int NetDisablePlayerInput(Scripts::CStruct* params, Scripts::CScript* script);
    int NetEnablePlayerInput(Scripts::CStruct* params, Scripts::CScript* script);
    int EnableInputEvents(Scripts::CStruct* params, Scripts::CScript* script);
    int DisableInputEvents(Scripts::CStruct* params, Scripts::CScript* script);
    // 0x0041E9E0 [PlayerInputIsDisabled]
    int PlayerInputIsDisabled(Scripts::CStruct* params, Scripts::CScript* script);
    // 0x0041EA20 / 0x0041EA30 / 0x0041EA40 / 0x0041EA50 [LeftPressed / RightPressed / UpPressed / DownPressed]: the
    // first pad's button held.
    int LeftPressed(Scripts::CStruct* params, Scripts::CScript* script);
    int RightPressed(Scripts::CStruct* params, Scripts::CScript* script);
    int UpPressed(Scripts::CStruct* params, Scripts::CScript* script);
    int DownPressed(Scripts::CStruct* params, Scripts::CScript* script);
    // 0x0041EA60 [0xB2A022DE]: the sticks into the script's parameters - 0x737F15CF / 0xD6030CEA the angles (degrees),
    // 0x686AD1C2 / 0xC94777AC the lengths (StickLength). 1.
    int GetSticks(Scripts::CStruct* params, Scripts::CScript* script);
    // 0x0041EB20 [HeldLongerThan]: whether button= (asserted) is held and was pressed longer ago than the unnamed time
    // (asserted; milliseconds, or with seconds / second times 1000, with frames / frame times 1000 / 60).
    int HeldLongerThan(Scripts::CStruct* params, Scripts::CScript* script);

    uint32_t m_lastEvents;    // +0x18 the buttons down last frame (their event bits)
    uint32_t m_events;        // +0x1C this frame's
    CPadSnapshot m_padPrimary;        // +0x20
    CPadSnapshot m_padSecondary;       // +0x258
    Responder* m_handler;      // +0x490
    Responder* m_handler2;     // +0x494
    bool m_disabled;          // +0x498 DisablePlayerInput
    bool m_netDisabled;       // +0x499 NetDisablePlayerInput
    bool m_inputEvents;       // +0x49A EnableInputEvents
    uint8_t pad49B;
    uint32_t m_unknown49C;    // +0x49C 0xB9B1E3B6
};
static_assert(offsetof(CPadInputModule, m_padPrimary) == 0x20 && offsetof(CPadInputModule, m_padSecondary) == 0x258 &&
                  offsetof(CPadInputModule, m_handler) == 0x490 && offsetof(CPadInputModule, m_disabled) == 0x498 &&
                  offsetof(CPadInputModule, m_unknown49C) == 0x49C && sizeof(CPadInputModule) == 0x4A0,
              "layout must match the original");
static_assert(offsetof(CPadInputModule::Responder, m_controlData) == 0x18 &&
                  offsetof(CPadInputModule::Responder, m_component) == 0x28 && sizeof(CPadInputModule::Responder) == 0x2C,
              "layout must match the original");

} // namespace Obj
