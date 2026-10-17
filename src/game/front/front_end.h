#pragma once

// The front end module (Mdl::FrontEnd, 0x164 bytes, vtable 0x0064D724): turns the controllers' presses into the
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
// The PC port's UIControls setting (src/game/sio/sdl_input.*) is read but never used; nothing here reads it
// either - these mappings come from the scripts only.

#include <cstddef>
#include <cstdint>

namespace Script {
class CScript;
class CStruct;
} // namespace Script

namespace Mdl {

struct FrontEnd;

// Tsk::Task<FrontEnd> (0x20 bytes, vtable 0x006476F0): a node of a main loop task list.
struct FrontEndTask {
    uint8_t m_node[0x18];   // the task list node (0x00406890 constructs it, with the priority)
    void(__cdecl* m_code)(FrontEndTask*);
    FrontEnd* m_data;
};
static_assert(sizeof(FrontEndTask) == 0x20 && offsetof(FrontEndTask, m_code) == 0x18);

// One controller's place in the input manager (0x84 bytes each, from +4).
struct InputServer;

// What the input manager keeps of a controller's buttons for this frame.
struct InputControlData {
    uint8_t m_unknown0[0x1C];
    uint32_t m_held;  // +0x1C: the buttons down
    uint32_t m_makes; // +0x20: the buttons pressed (with the manager's auto-repeat)
};

// Inp::Handler<FrontEnd> (0x2C bytes, vtable 0x0064D7D8): gets one controller's presses once a frame.
struct FrontEndInputHandler {
    uint8_t m_node[0x18];               // the handler list node (0x00406890)
    InputControlData* m_controlData;    // +0x18: set when it's added to a controller's handler list
    uint8_t* m_device;                  // +0x1C: the Sio device (its number at +0x0C)
    int m_index;                        // +0x20: which of the front end's two handlers
    void(__cdecl* m_code)(FrontEndInputHandler*);
    FrontEnd* m_data;
};
static_assert(sizeof(FrontEndInputHandler) == 0x2C && offsetof(FrontEndInputHandler, m_code) == 0x24);

struct FrontEnd {
    // The menu's pad events.
    static constexpr uint32_t kPadStart = 0x2E6EF8E7;  // "pad_start"
    static constexpr uint32_t kPadBack = 0x7EE0FD2A;   // "pad_back"
    static constexpr uint32_t kPadChoose = 0x1E3E253B; // "pad_choose"

    struct PadMapping {
        uint32_t m_unknown0; // (zeroed by the constructor)
        uint32_t m_unknown4;
        uint32_t m_mapping;  // 0 inactive, 1 active (the default), 2 use_as_first
    };
    struct ButtonEvent {
        uint32_t m_button; // the index of the button's name (ButtonEventMappings)
        uint32_t m_event;  // 0: a free slot
    };
    static constexpr int kMaxButtonEvents = 32;

    void** m_vtable;
    uint8_t m_module[0x14 - 4];          // Mdl::Module's fields
    FrontEndTask* m_logicTask;           // +0x14: LogicTask, priority -2000
    FrontEndTask* m_keyboardTask;        // +0x18: KeyboardTask, priority -1000 (added by AddKeyboardHandler)
    uint32_t m_unknown1C;
    FrontEndInputHandler* m_inputHandlers[2]; // +0x20: InputLogic, priority -1000
    int m_deviceServerMap[2];            // +0x28: which controller each handler listens to (0, 1)
    bool m_paused;                       // +0x30
    int m_autoRepeatDelay;               // +0x34: ms (300)
    int m_autoRepeatRate;                // +0x38: ms (50)
    int m_keyboardDevice;                // +0x3C: the device_num the typed keys are reported with
    PadMapping m_padMappings[2];         // +0x40
    bool m_blockMenuInput;               // +0x58
    ButtonEvent m_buttonEvents[kMaxButtonEvents]; // +0x5C
    bool m_checkStartupScript;           // +0x15C: the first input frame looks at the global below once
    bool m_startupScriptOn;              // +0x15D
    uint32_t m_startupScriptId;          // +0x160: the spawned script's unique id

    // 0x0054FE00: the constructor (the module, its tasks and handlers; mappings active, 300/50 ms). Returns this.
    FrontEnd* Construct();

    // 0x0054EF60: typed keys are reported for `device` from now on (the keyboard task joins the logic tasks).
    void AddKeyboardHandler(int device);
    // 0x0054EF80: the button-event pairs from the `xbox` array of `params` (see the top of this file).
    void UpdateButtonEventMappings(Script::CStruct* params);
    // 0x0054F380: once a frame (LogicTask): whether a menu / the keyboard is up, the music, the spawned scripts.
    void Update(bool gamePaused);
    // 0x0054F600: the `index`-th pad event of the pressed buttons, else pad_start for Start, else (controller 0,
    // the on-screen keyboard up) Escape / Enter / Space as pad_back / pad_start / pad_choose. 0 if none.
    uint32_t GrabControlEvent(uint32_t makes, int index, int deviceIndex, uint32_t held);
    // 0x0054F900
    void SetAnalogStickActiveForMenus(bool active);
    // 0x0054F960: pauses or unpauses the game (controllers' motors, scripts, skaters, the object updates...).
    void PauseGame(bool pause);
    // 0x0054FD00: each handler moved to the handler list of the controller m_deviceServerMap names.
    void UpdateInputMappings();

    // 0x0054F200 (the keyboard task): each typed key (printable, Enter or Backspace) to handle_keyboard_input.
    static void __cdecl KeyboardTask(FrontEndTask* task);
    // 0x0054FCC0 (the logic task)
    static void __cdecl LogicTask(FrontEndTask* task);
    // 0x0054FB40 (a controller's handler): its pad events, each sent to the menus (unless blocked or inactive).
    static void __cdecl InputLogic(FrontEndInputHandler* handler);
};
static_assert(offsetof(FrontEnd, m_logicTask) == 0x14 && offsetof(FrontEnd, m_inputHandlers) == 0x20 &&
                  offsetof(FrontEnd, m_paused) == 0x30 && offsetof(FrontEnd, m_keyboardDevice) == 0x3C &&
                  offsetof(FrontEnd, m_padMappings) == 0x40 && offsetof(FrontEnd, m_blockMenuInput) == 0x58 &&
                  offsetof(FrontEnd, m_buttonEvents) == 0x5C && offsetof(FrontEnd, m_checkStartupScript) == 0x15C &&
                  offsetof(FrontEnd, m_startupScriptId) == 0x160 && sizeof(FrontEnd) == 0x164,
              "layout must match the original");

// The singleton (0x007CE464, references 0x007CE468).
FrontEnd*& FrontEndInstance();
// 0x0054FF80: the first call with `create` makes it; every call takes a reference and returns it.
FrontEnd* CreateFrontEnd(bool create);

// The script commands.
// 0x0054F0F0: SetMenuAutoRepeatTimes (delay rate) - seconds.
bool __cdecl CFunc_SetMenuAutoRepeatTimes(Script::CStruct* params, Script::CScript* script);
// 0x0054F150: SetMenuPadMappings [inactive|active|use_as_first x2].
bool __cdecl CFunc_SetMenuPadMappings(Script::CStruct* params, Script::CScript* script);
// 0x0054F1B0: SetButtonEventMappings xbox=[...] block_menu_input / unblock_menu_input.
bool __cdecl CFunc_SetButtonEventMappings(Script::CStruct* params, Script::CScript* script);
// 0x0054FDC0: SetAnalogStickActiveForMenus (an integer, default 1).
bool __cdecl CFunc_SetAnalogStickActiveForMenus(Script::CStruct* params, Script::CScript* script);

} // namespace Mdl
