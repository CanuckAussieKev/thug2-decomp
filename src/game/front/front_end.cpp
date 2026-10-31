// Mdl::FrontEnd (0x0054EBE0-0x0055001F): the menus' input - pad and button events, typed keys - and pausing the
// game. See front_end.h.

#include "game/front/front_end.h"
#include "game/net/task_vtable.h"

#include <cstdint>
#include <new>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/task.h"
#include "game/front/automation.h"
#include "game/front/mouse_menus.h"
#include "game/front/screen_element_manager.h"
#include "game/obj/compositemgr.h"
#include "game/obj/tracker.h"
#include "game/pcm/music.h"
#include "game/script/cscript.h"
#include "game/script/game_cfuncs.h"
#include "game/script/struct.h"
#include "game/sfx/sfx.h"
#include "game/sio/device.h"
#include "game/sio/inpman.h"
#include "game/sio/sdl_input.h"
#include "game/skate/skate_module.h"
#include "game/skater/skater.h"
#include "game/sys/game_clock.h"
#include "game/sys/module.h"
#include "game/sys/timer.h"
#include "game/core/code_address.h"
#include "game/obj/movie_manager.h"
#include "game/ass/assman.h"

namespace Mdl {

namespace {
extern void** const kFrontEndVtable;
} // namespace

Mdl::FrontEnd* g_frontEndInstance;

bool g_menusActive;
bool g_pendingScript;

namespace {

constexpr uint32_t kId = 0x40C698AF;              // "id"
constexpr uint32_t kNull = 0xDA3403B0;            // "null"
constexpr uint32_t kText = 0xC4745838;            // "text"
constexpr uint32_t kDeviceNum = 0xC9428A08;       // "device_num"
constexpr uint32_t kController = 0xB30D9965;      // "controller"
constexpr uint32_t kKeyboardAnchor = 0x31631B98;  // "keyboard_anchor"
constexpr uint32_t kCurrentMenuAnchor = 0xF53D1D83; // "current_menu_anchor"
constexpr uint32_t kDialogBoxAnchor = 0x3B56E746; // "dialog_box_anchor"
constexpr uint32_t kBlockMenuInput = 0xDB96E524;  // "block_menu_input"
constexpr uint32_t kUnblockMenuInput = 0x4F653B0B; // "unblock_menu_input"
constexpr uint32_t kInactive = 0x742CF11F;        // "inactive"
constexpr uint32_t kActive = 0xB4E103FD;          // "active"
constexpr uint32_t kUseAsFirst = 0xAD6C73EE;      // "use_as_first"
constexpr uint32_t kHandleKeyboardInput = 0xA6ED4911; // "handle_keyboard_input"
constexpr uint32_t kKeyboardDeviceParam = 0x69E3A1C4; // (the device_num the keys came from; name unknown)
constexpr uint32_t kEnterKey = 0xB292CC42;        // (handle_keyboard_input's flag for Enter; name unknown)
constexpr uint32_t kBackspaceKey = 0x86C0FC77;    // (and for Backspace)
constexpr uint32_t kMenuEvent = 0x36B2EE74;       // the pad events' target and source (name unknown)

// The typed keys' control characters (Sio::GetKeyboardChars).
constexpr char kKeyBackspace = 0x18;
constexpr char kKeyEnter = 0x19;
constexpr char kKeyEscape = 0x1A;

// The button names of SetButtonEventMappings' xbox array, by index (the null ones are unused indexes).
const char* const kXboxButtonNames[18] = {
    "left_trigger_full", "right_trigger_full", "left_trigger_half", "right_trigger_half", "y", "b", "a", "x",
    "back", "left_stick_button", "right_stick_button", "null", "null", "null", "null", "null", "black", "white",
};

uint32_t g_frontEndRefs;
bool g_skateshopOverride;

uint32_t& FrontEndRefs() { return g_frontEndRefs; }
// Set each frame by Update.
bool& MenusActive() { return g_menusActive; }      // paused, in the skateshop, or keyboard up
bool& SkateshopOverride() { return g_skateshopOverride; } // set elsewhere: never counts as the skateshop
bool& KeyboardOnScreen() { return Sio::g_keyboardOnScreen; }
bool& MenuOnScreen() { return Sio::g_inMenuFlag; }     // a menu, dialog box or the quit dialog
bool& PendingScript() { return g_pendingScript; }    // set elsewhere: run 0xFB11E6CD next frame
bool& GamePaused() { return Obj::g_gamePaused; }

uint8_t* SkateBytes() { return reinterpret_cast<uint8_t*>(g_skateModule); } // Mdl::Skate
void* Tracker() { return reinterpret_cast<void*>(Obj::g_tracker); }    // Obj::CTracker
uint8_t* MainLoopManager() { return reinterpret_cast<uint8_t*>(Ass::g_mainLoopManager); }
uint8_t* InputManager() { return reinterpret_cast<uint8_t*>(Inp::g_inputManager); } // Inp::Manager

// The typed arrow keys (Sio::GetKeyboardChars).
constexpr char kKeyLeft = 0x14;
constexpr char kKeyRight = 0x15;
constexpr char kKeyUp = 0x16;
constexpr char kKeyDown = 0x17;

// The held buttons that are directions.
constexpr uint32_t kButtonLeft = 0x8000;
constexpr uint32_t kButtonRight = 0x2000;
constexpr uint32_t kButtonUp = 0x1000;
constexpr uint32_t kButtonDown = 0x4000;

// The CRT's _ftol: truncation, the low 32 bits of the 64-bit result.
int Ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// The debug report (the empty 0x005480A0) after the game is paused or unpaused - compiled out.
inline void ReportPauseState() {}

// Engine pieces without source yet.
bool ObjectExists(Script::CStruct* params) // the script command ObjectExists
{
    return Obj::CFunc_ObjectExists(params, nullptr);
}
// The module base, and the tasks (the logic and input handlers) in the main loop's / input manager's stacks.
void ConstructModule(void* module) { static_cast<Module*>(module)->Construct(); }
void DestroyModule(void* module) { static_cast<Module*>(module)->DestroyBody(); }
void ConstructNode(void* node, int priority) { static_cast<Tsk::BaseTask*>(node)->Construct(priority); }
void AddNode(void* list, void* node) { static_cast<Tsk::Stack*>(list)->AddTask(static_cast<Tsk::BaseTask*>(node)); }
void RemoveNode(void* node) { static_cast<Tsk::BaseTask*>(node)->Remove(); }

// A task list node's scalar deleting destructor (vtable slot 2).
void DeleteNode(void* node)
{
    reinterpret_cast<void(__thiscall*)(void*, unsigned int)>((*static_cast<void***>(node))[2])(node, 1);
}

// A reference to the input manager (Spt::SingletonPtr<Inp::Manager>, 0x0044E7F0 / 0x0044E6B0), not making it.
struct InputManagerRef {
    uint8_t* m_manager;
    InputManagerRef() : m_manager(reinterpret_cast<uint8_t*>(Inp::CreateManager(false))) {}
    ~InputManagerRef() { Inp::ReleaseManager(); }
    InputManagerRef(const InputManagerRef&) = delete;
    InputManagerRef& operator=(const InputManagerRef&) = delete;
};

Script::CStruct* NewParams()
{
    Script::CStruct* params = Script::NewStruct();
    if (params)
        params = params->Init();
    return params;
}
void DeleteParams(Script::CStruct* params)
{
    if (params) {
        params->Clear(); // (the destructor)
        Script::DeleteStruct(params);
    }
}
// ObjectExists id=`id`, on `params` emptied first unless `clear` is false. The original compares the result
// with 1 in some places and with 0 in others; it's a bool either way.
bool AnchorExists(Script::CStruct* params, uint32_t id, bool clear = true)
{
    if (clear)
        params->Clear();
    params->AddChecksum(kId, id);
    return ObjectExists(params);
}

} // namespace

FrontEnd*& FrontEndInstance() { return g_frontEndInstance; }

// 0x0054EBE0
void ReleaseFrontEnd()
{
    if (FrontEndRefs() > 1) {
        --FrontEndRefs();
        return;
    }
    if (FrontEnd* frontEnd = FrontEndInstance())
        reinterpret_cast<void(__thiscall*)(FrontEnd*, unsigned int)>(frontEnd->m_vtable[0])(frontEnd, 1);
    FrontEndInstance() = nullptr;
    FrontEndRefs() = 0;
}

// 0x0054EC20
void FrontEnd::DestroyBody()
{
    m_vtable = kFrontEndVtable;
    if (m_logicTask)
        DeleteNode(m_logicTask);
    // (m_keyboardTask is never deleted)
    for (FrontEndInputHandler*& handler : m_inputHandlers) {
        if (handler)
            DeleteNode(handler);
        handler = nullptr;
    }
    DestroyModule(this);
}

// 0x0054ECA0
bool FrontEnd::PadsPluggedIn()
{
    for (FrontEndInputHandler* handler : m_inputHandlers) {
        if (reinterpret_cast<Sio::Device*>(handler->m_device)->IsPluggedIn())
            return true;
    }
    return false;
}

// 0x0054ECD0
void FrontEnd::RemoveKeyboardHandler()
{
    RemoveNode(m_keyboardTask);
}

// 0x0054ECE0
uint32_t FrontEnd::AutoRepeat(uint32_t held, int deviceIndex)
{
    uint32_t event = 0;
    if (held & kButtonLeft)
        event = kPadLeft;
    else if (held & kButtonRight)
        event = kPadRight;
    else if (held & kButtonUp)
        event = kPadUp;
    else if (held & kButtonDown)
        event = kPadDown;
    if (deviceIndex == 0 && event == 0 && KeyboardOnScreen()) {
        // The PC keyboard's arrows: the first one typed (the keys are left for the others to read).
        char keys[256];
        int count = Sio::GetKeyboardChars(keys);
        for (int i = 0; i < count; ++i) {
            char key = keys[i];
            if (key == kKeyLeft) {
                event = kPadLeft;
                break;
            }
            if (key == kKeyRight) {
                event = kPadRight;
                break;
            }
            if (key == kKeyUp) {
                event = kPadUp;
                break;
            }
            if (key == kKeyDown) {
                event = kPadDown;
                break;
            }
        }
    }

    PadMapping& pad = m_padMappings[deviceIndex];
    if (pad.m_repeatState == 0) {
        if (event) { // first held: sent now, repeated after the delay
            pad.m_repeatTimer = static_cast<uint32_t>(m_autoRepeatDelay);
            pad.m_repeatState = 1;
        }
    } else if (event) {
        uint32_t elapsed = static_cast<uint32_t>(Ftol(Tmr::GetSlomoFrameSeconds() * 1000.0f));
        pad.m_repeatTimer = elapsed < pad.m_repeatTimer ? pad.m_repeatTimer - elapsed : 0;
        if (pad.m_repeatTimer == 0) { // due: sent, then again after the rate
            if (pad.m_repeatState == 1)
                pad.m_repeatState = 2;
            pad.m_repeatTimer = static_cast<uint32_t>(m_autoRepeatRate);
        } else {
            event = 0;
        }
    } else {
        pad.m_repeatState = 0; // let go
    }

    // While the startup script runs, nothing (and it's unpaused).
    if (m_startupScriptOn && deviceIndex == 0) {
        if (Script::CScript* script = Script::FindScriptWithUniqueId(m_startupScriptId))
            Script::UnpauseSpawnedScript(script);
        else
            m_startupScriptOn = false;
        return 0;
    }
    return event;
}

// 0x0054EE70
FrontEnd* FrontEnd::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0054EEA0
void FrontEnd::Start()
{
    Mem::Manager::Get()->PushContext(Mem::Manager::Get()->m_frontEndHeap);
    AddNode(MainLoopManager() + 4, m_logicTask); // the logic tasks
    uint8_t* input = InputManager();
    for (FrontEndInputHandler* handler : m_inputHandlers) {
        // The controller's place in the input manager (see UpdateInputMappings).
        AddNode(input + handler->m_index * 0x84 + 8, handler);
        uint8_t* server = input + handler->m_index * 0x84;
        handler->m_controlData = reinterpret_cast<InputControlData*>(server + 0x44);
        handler->m_device = *reinterpret_cast<uint8_t**>(server + 4);
    }
    Mem::Manager::Get()->PopContext();
}

// 0x0054EF30
void FrontEnd::Stop()
{
    RemoveNode(m_logicTask);
    for (FrontEndInputHandler* handler : m_inputHandlers)
        RemoveNode(handler);
}

// 0x0054EF60
void FrontEnd::AddKeyboardHandler(int device)
{
    m_keyboardDevice = device;
    AddNode(MainLoopManager() + 4, m_keyboardTask); // the logic tasks
}

// 0x0054EF80
void FrontEnd::UpdateButtonEventMappings(Script::CStruct* params)
{
    Script::CArray* mappings;
    if (!params->GetArray("xbox", &mappings, 0))
        return;
    for (uint32_t i = 0; i < mappings->m_size; ++i) {
        auto* pair = reinterpret_cast<Script::CArray*>(mappings->GetElement(i));
        uint32_t buttonName = pair->GetElement(0);
        uint32_t event = pair->GetElement(1);
        uint32_t button = 0;
        for (; button < 18; ++button) {
            if (buttonName == Crc::GenerateCRCFromString(kXboxButtonNames[button]) || buttonName == kNull)
                break;
        }
        if (button == 18)
            continue; // not a button
        // Quirks kept: button null (index 0) removes the event's pairs, then the first free slot still gets
        // (0, event) - the event moves to left_trigger_full; event null removes the button's pairs, then a free
        // slot gets (button, null).
        for (ButtonEvent& entry : m_buttonEvents) {
            if (buttonName == kNull && entry.m_event == event) {
                entry.m_event = 0;
            } else if (event == kNull && entry.m_button == button) {
                entry.m_event = 0;
            } else if (entry.m_event == 0) {
                entry.m_button = button;
                entry.m_event = event;
                break;
            }
        }
    }
}

// 0x0054F0F0
bool __cdecl CFunc_SetMenuAutoRepeatTimes(Script::CStruct* params, Script::CScript* /*script*/)
{
    Script::CPair times;
    times.Init();
    params->GetPair(0, &times, true);
    FrontEnd* frontEnd = FrontEndInstance();
    int rate = Ftol(static_cast<double>(times.y) * 1000.0);
    int delay = Ftol(static_cast<double>(times.x) * 1000.0);
    frontEnd->m_autoRepeatRate = rate;
    frontEnd->m_autoRepeatDelay = delay;
    return true;
}

// 0x0054F150
bool __cdecl CFunc_SetMenuPadMappings(Script::CStruct* params, Script::CScript* /*script*/)
{
    Script::CArray* mappings;
    params->GetArray(0u, &mappings, 1);
    FrontEnd* frontEnd = FrontEndInstance();
    for (int i = 0; i < 2; ++i) {
        uint32_t mapping = mappings->GetElement(i);
        uint32_t value = 0; // inactive, or anything else
        if (mapping != kInactive) {
            if (mapping == kActive)
                value = 1;
            else if (mapping == kUseAsFirst)
                value = 2;
        }
        frontEnd->m_padMappings[i].m_mapping = value;
    }
    return true;
}

// 0x0054F1B0
bool __cdecl CFunc_SetButtonEventMappings(Script::CStruct* params, Script::CScript* /*script*/)
{
    FrontEnd* frontEnd = FrontEndInstance();
    frontEnd->UpdateButtonEventMappings(params);
    if (params->ContainsFlag(kBlockMenuInput))
        frontEnd->m_blockMenuInput = true;
    else if (params->ContainsFlag(kUnblockMenuInput))
        frontEnd->m_blockMenuInput = false;
    return true;
}

// 0x0054F200
void __cdecl FrontEnd::KeyboardTask(FrontEndTask* task)
{
    FrontEnd* frontEnd = task->m_data;
    char keys[256];
    int count = Sio::GetKeyboardChars(keys);
    for (int i = 0; i < count; ++i) {
        char key = keys[i];
        // Printable (and the high half), Enter and Backspace.
        if (!(key >= 0x20 || key < 0 || key == kKeyEnter || key == kKeyBackspace))
            continue;
        Script::CStruct* params = NewParams();
        if (keys[i] == kKeyEnter) {
            params->AddChecksum(0u, kEnterKey);
        } else if (keys[i] == kKeyBackspace) {
            params->AddChecksum(0u, kBackspaceKey);
        } else {
            char text[2] = {keys[i], '\0'};
            params->AddString(kText, text);
        }
        params->AddInteger(kKeyboardDeviceParam, frontEnd->m_keyboardDevice);
        Script::RunScript(kHandleKeyboardInput, params, nullptr, false, nullptr);
        DeleteParams(params);
    }
    Sio::ClearKeyboardChars();
}

// 0x0054F380
void FrontEnd::Update(bool gamePaused)
{
    uint8_t* skate = SkateBytes();
    bool inSkateshop = *reinterpret_cast<uint32_t*>(skate + 0x374) == 0xE92ECAFE; // (the level / game mode)
    if (SkateshopOverride())
        inSkateshop = false;
    bool keyboardUp = false;
    Script::CStruct* params = NewParams();
    if (AnchorExists(params, Crc::GenerateCRCFromString("keyboard_anchor"), false))
        keyboardUp = true;
    params->Clear();
    bool menuUp = AnchorExists(params, Crc::GenerateCRCFromString("current_menu_anchor"), false);
    params->Clear();
    if (AnchorExists(params, Crc::GenerateCRCFromString("dialog_box_anchor"), false))
        menuUp = true;
    params->Clear();
    if (AnchorExists(params, Crc::GenerateCRCFromString("quit_dialog_anchor"), false))
        menuUp = true;
    DeleteParams(params);

    MenusActive() = gamePaused || inSkateshop || keyboardUp;
    KeyboardOnScreen() = keyboardUp;
    MenuOnScreen() = menuUp;

    Pcm::Update();
    // (the sound manager: its voices' states, and its positional sounds; the original calls the first - which
    // takes no manager - as its method)
    auto* objects = Sfx::g_sfxManager;
    Sfx::UpdateVoiceStates();
    Mem::Manager* memory = Mem::Manager::Get();
    if (!gamePaused) {
        memory->PushContext(memory->m_bottomUpHeap);
        objects->UpdatePositionalSounds();
        Mem::Manager::Get()->PopContext();
    }
    Mem::Manager::Get()->PushContext(Mem::Manager::Get()->m_bottomUpHeap);
    Script::UpdateSpawnedScripts();
    reinterpret_cast<Mdl::Skate*>(SkateBytes())->UpdateGameFlow();
    if (PendingScript()) {
        PendingScript() = false;
        auto* object = static_cast<Obj::CObject*>(reinterpret_cast<Mdl::Skate*>(SkateBytes())->GetLocalSkater());
        Script::RunScript(0xFB11E6CD, nullptr, object, false, nullptr);
    }
    Mem::Manager::Get()->PopContext();
}

// 0x0054F600
uint32_t FrontEnd::GrabControlEvent(uint32_t makes, int index, int deviceIndex, uint32_t held)
{
    for (int i = 0; i < kMaxButtonEvents; ++i) {
        ButtonEvent& entry = m_buttonEvents[i];
        if ((makes & (1u << (entry.m_button & 31))) && entry.m_event != 0 && --index < 0)
            return m_buttonEvents[i].m_event;
    }
    // Start is pad_start (unless the global 0xF3E055E1 is set and the 0x100 button is down).
    if (!((held & 0x100) && Script::GetInteger(0xF3E055E1u) != 0)) {
        if ((makes & 0x800) && index == 0)
            return kPadStart;
    }
    if (deviceIndex != 0 || !KeyboardOnScreen())
        return 0;

    // The PC keyboard, while the on-screen keyboard is up.
    char keys[256];
    uint32_t event = 0;
    int count = Sio::GetKeyboardChars(keys);
    for (int i = 0; i < count; ++i) {
        char key = keys[i];
        if (key == kKeyEscape) {
            Script::CStruct* params = NewParams();
            event = AnchorExists(params, kCurrentMenuAnchor, false) ? kPadBack : kPadStart;
            Sio::ClearKeyboardChars();
            DeleteParams(params);
            return event;
        }
        if (key != kKeyEnter && key != ' ')
            continue;
        Script::CStruct* params = NewParams();
        if (!AnchorExists(params, kKeyboardAnchor, false)) {
            if (AnchorExists(params, 0x487FBC86) && keys[i] == ' ')
                return 0; // (the original leaks params here)
            if (AnchorExists(params, kCurrentMenuAnchor) || AnchorExists(params, kDialogBoxAnchor)) {
                event = kPadChoose;
                Sio::ClearKeyboardChars();
            }
        }
        DeleteParams(params);
    }
    return event;
}

// 0x0054F900
void FrontEnd::SetAnalogStickActiveForMenus(bool active)
{
    InputManagerRef input;
    reinterpret_cast<Inp::Manager*>(input.m_manager)->SetOverridePadWithStick(active);
}

// 0x0054F960
void FrontEnd::PauseGame(bool pause)
{
    if (pause == m_paused)
        return;
    uint8_t* skate = SkateBytes();
    auto* devices = Sio::g_manager;
    if (pause) {
        reinterpret_cast<Mdl::Skate*>(skate)->Pause(true);
        devices->PauseDevices();
    } else {
        devices->UnpauseDevices();
    }
    Script::PauseSpawnedScripts(pause);

    // The skaters: their list's nodes (the head's sentinel has -1 at +4).
    uint32_t count = 0;
    for (auto* node = *reinterpret_cast<uint8_t**>(skate + 0x34);
         *reinterpret_cast<int*>(node + 4) != -1 && node; node = *reinterpret_cast<uint8_t**>(node + 0xC))
        ++count;
    for (uint32_t i = 0; i < count; ++i) {
        auto* skater = reinterpret_cast<void***>(reinterpret_cast<Mdl::Skate*>(skate)->GetSkater(static_cast<int>(i)));
        if (!skater)
            continue;
        if (pause)
            reinterpret_cast<void(__thiscall*)(void*)>((*skater)[0x2C / 4])(skater);
        else
            reinterpret_cast<void(__thiscall*)(void*)>((*skater)[0x30 / 4])(skater);
    }
    Obj::CompositeObjectManager()->Pause(pause);
    Tmr::PauseGameClock(pause);
    Front::CScreenElementManager::Instance()->SetPausedAll(pause);
    GamePaused() = pause;
    m_paused = pause;
    // The main loop's logic tasks: while paused those of the 0xC kinds (a task's +0x14 bits) are skipped
    // (0x00406AC0 runs a task only when (mask & its kinds) == 0).
    if (pause) {
        *reinterpret_cast<uint32_t*>(MainLoopManager() + 8) = 0xC;
        ReportPauseState();
    } else {
        *reinterpret_cast<uint32_t*>(MainLoopManager() + 8) = 0;
        reinterpret_cast<Mdl::Skate*>(SkateBytes())->Pause(false);
        ReportPauseState();
    }
    if (m_paused || !reinterpret_cast<Mdl::Skate*>(skate)->GetLocalSkater())
        return;
    // Back in the game with a skater: the camera's view mode back to the global's.
    Script::CStruct* params = NewParams();
    params->AddInteger(0u, Script::GetInteger(0x968F7408u));
    CFuncs::CFunc_SetViewMode(params, nullptr);
    DeleteParams(params);
}

// 0x0054FB40
void __cdecl FrontEnd::InputLogic(FrontEndInputHandler* handler)
{
    FrontEnd* frontEnd = handler->m_data;
    // (not original - the mouse_menus feature: once a frame, with the first controller's handler, the mouse's menu
    // events; nothing when it's off - Update checks; the level editor's input logic runs there too, whatever the
    // setting)
    if (handler->m_index == 0)
        MouseMenus::Update(frontEnd);
    void* tracker = Tracker();
    static_cast<Obj::CTracker*>(tracker)->LogFrame();
    for (int index = 0;; ++index) {
        uint32_t event = 0;
        if (index == 0) {
            if (frontEnd->m_checkStartupScript) {
                frontEnd->m_startupScriptOn = Script::GetInteger(0x756FB186u) != 0;
                if (frontEnd->m_startupScriptOn) {
                    Script::CScript* script =
                        Script::SpawnScript(0xDCE60A74u, nullptr, 0, nullptr, 0, 0, false, false, false, false);
                    frontEnd->m_startupScriptId = *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(script) + 0xB0);
                }
                frontEnd->m_checkStartupScript = false;
            }
            uint32_t repeated = frontEnd->AutoRepeat(handler->m_controlData->m_held, handler->m_index);
            if (repeated)
                event = repeated;
        }
        uint32_t pressed = frontEnd->GrabControlEvent(handler->m_controlData->m_makes, index, handler->m_index,
                                                      handler->m_controlData->m_held);
        if (pressed)
            event = pressed;
        if (!event)
            return;
        if (frontEnd->m_padMappings[handler->m_index].m_mapping == 0 || frontEnd->m_blockMenuInput)
            continue;
        Script::CStruct params;
        params.Init();
        params.AddInteger(kDeviceNum, *reinterpret_cast<int*>(handler->m_device + 0xC));
        params.AddInteger(kController, 0); // (the original loads the mapping, then passes 0)
        static_cast<Obj::CTracker*>(tracker)->LaunchEvent(event, kMenuEvent, kMenuEvent, &params, false, -1.0f);
        params.Clear();
    }
}

// 0x0054FCC0
void __cdecl FrontEnd::LogicTask(FrontEndTask* task)
{
    Mem::Manager::Get()->PushContext(Mem::Manager::Get()->m_frontEndHeap);
    task->m_data->Update(task->m_data->m_paused);
    Front::CScreenElementManager::Instance()->Update();
    Mem::Manager::Get()->PopContext();
}

// 0x0054FD00
void FrontEnd::UpdateInputMappings()
{
    InputManagerRef input;
    for (int i = 0; i < 2; ++i) {
        Dbg::Printf("m_device_server_map[%i] = %i\n", i, m_deviceServerMap[i]); // "m_device_server_map[%i] = %i\n"
        FrontEndInputHandler* handler = m_inputHandlers[i];
        if (!handler)
            continue;
        Dbg::Printf("got a handler\n"); // "got a handler\n"
        RemoveNode(handler);
        // The controller's place in the input manager: its device, its handler list, its control data.
        uint8_t* server = input.m_manager + m_deviceServerMap[i] * 0x84;
        AddNode(server + 8, handler);
        handler->m_controlData = reinterpret_cast<InputControlData*>(server + 0x44);
        handler->m_device = *reinterpret_cast<uint8_t**>(server + 4);
    }
}

// 0x0054FDC0
bool __cdecl CFunc_SetAnalogStickActiveForMenus(Script::CStruct* params, Script::CScript* /*script*/)
{
    int active = 1;
    params->GetInteger(0u, &active, 0);
    FrontEndInstance()->SetAnalogStickActiveForMenus(active != 0);
    return true;
}

// 0x0054FE00
FrontEnd* FrontEnd::Construct()
{
    ConstructModule(this);
    m_vtable = kFrontEndVtable;
    auto* task = static_cast<FrontEndTask*>(Mem::Calloc(sizeof(FrontEndTask)));
    if (task) {
        ConstructNode(task, -2000);
        *reinterpret_cast<uint32_t*>(task) = reinterpret_cast<uint32_t>(GameCode::kTaskVtable); // Tsk::Task<FrontEnd>
        task->m_code = &LogicTask;
        task->m_data = this;
    }
    m_logicTask = task;
    task = static_cast<FrontEndTask*>(Mem::Calloc(sizeof(FrontEndTask)));
    if (task) {
        ConstructNode(task, -1000);
        *reinterpret_cast<uint32_t*>(task) = reinterpret_cast<uint32_t>(GameCode::kTaskVtable);
        task->m_code = &KeyboardTask;
        task->m_data = this;
    }
    m_keyboardTask = task;
    for (int i = 0; i < 2; ++i) {
        auto* handler = static_cast<FrontEndInputHandler*>(Mem::Calloc(sizeof(FrontEndInputHandler)));
        if (handler) {
            ConstructNode(handler, -1000);
            handler->m_index = i;
            *reinterpret_cast<uint32_t*>(handler) = reinterpret_cast<uint32_t>(GameCode::kInputHandlerVtable); // Inp::Handler<FrontEnd>
            handler->m_code = &InputLogic;
            handler->m_data = this;
        }
        m_inputHandlers[i] = handler;
        m_padMappings[i].m_repeatState = 0;
        m_padMappings[i].m_mapping = 1;
    }
    m_paused = false;
    m_autoRepeatDelay = 300;
    m_autoRepeatRate = 50;
    m_blockMenuInput = false;
    m_startupScriptOn = false;
    m_checkStartupScript = true;
    m_startupScriptId = 0;
    for (ButtonEvent& entry : m_buttonEvents)
        entry.m_event = 0;
    m_deviceServerMap[0] = 0;
    m_deviceServerMap[1] = 1;
    return this;
}

// 0x0054FF80
FrontEnd* CreateFrontEnd(bool create)
{
    if (!FrontEndInstance() && create) {
        void* memory = Mem::Calloc(sizeof(FrontEnd), nullptr, 1);
        FrontEndInstance() = memory ? static_cast<FrontEnd*>(memory)->Construct() : nullptr;
    }
    ++FrontEndRefs();
    return FrontEndInstance();
}

namespace {
static VtableSlot kFrontEndVtable_slots[] = {&Mdl::FrontEnd::Destroy, &Mdl::FrontEnd::Start, &Mdl::FrontEnd::Stop};
void** const kFrontEndVtable = reinterpret_cast<void**>(kFrontEndVtable_slots);
} // namespace

} // namespace Mdl
