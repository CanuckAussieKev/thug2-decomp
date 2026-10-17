// Mdl::FrontEnd (0x0054EF60-0x0055001F, with 0x0054EBE0-0x0054EF5F still in the game): the menus' input - pad and
// button events, typed keys - and pausing the game. See front_end.h.

#include "game/front/front_end.h"

#include <cstdint>
#include <new>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/obj/tracker.h"
#include "game/pcm/music.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sio/device.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Mdl {

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

uint32_t& FrontEndRefs() { return GameVariable<uint32_t>(0x007CE468); }
// Set each frame by Update.
bool& MenusActive() { return GameVariable<bool>(0x007CE46C); }      // paused, in the skateshop, or keyboard up
bool& SkateshopOverride() { return GameVariable<bool>(0x007CE46D); } // set elsewhere: never counts as the skateshop
bool& KeyboardOnScreen() { return GameVariable<bool>(0x007CE46E); }
bool& MenuOnScreen() { return GameVariable<bool>(0x007CE46F); }     // a menu, dialog box or the quit dialog
bool& PendingScript() { return GameVariable<bool>(0x007CE4A0); }    // set elsewhere: run 0xFB11E6CD next frame
bool& GamePaused() { return GameVariable<bool>(0x006FA7D4); }

uint8_t* Skate() { return GameVariable<uint8_t*>(0x007CE478); } // Mdl::Skate
void* Tracker() { return GameVariable<void*>(0x006F0FC0); }    // Obj::CTracker
uint8_t* MainLoopManager() { return GameVariable<uint8_t*>(0x00696D68); }

// The CRT's _ftol: truncation, the low 32 bits of the 64-bit result.
int Ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// The debug report (the empty 0x005480A0) after the game is paused or unpaused - compiled out.
inline void ReportPauseState() {}

// Engine pieces without source yet.
bool ObjectExists(Script::CStruct* params) // the script command ObjectExists
{
    return Obj::CFunc_ObjectExists(params, nullptr);
}
void ConstructModule(void* module) { GameFunction<void*(__thiscall*)(void*)>(0x0044F7E0)(module); }
void ConstructNode(void* node, int priority) { GameFunction<void*(__thiscall*)(void*, int)>(0x00406890)(node, priority); }
void AddNode(void* list, void* node) { GameFunction<void(__thiscall*)(void*, void*)>(0x00406C90)(list, node); }
void RemoveNode(void* node) { GameFunction<void(__thiscall*)(void*)>(0x004068F0)(node); }

// A reference to the input manager (Spt::SingletonPtr<Inp::Manager>, 0x0044E7F0 / 0x0044E6B0), not making it.
struct InputManagerRef {
    uint8_t* m_manager;
    InputManagerRef() : m_manager(GameFunction<uint8_t*(__cdecl*)(bool)>(0x0044E7F0)(false)) {}
    ~InputManagerRef() { GameFunction<void(__cdecl*)()>(0x0044E6B0)(); }
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

FrontEnd*& FrontEndInstance() { return GameVariable<FrontEnd*>(0x007CE464); }

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
    uint8_t* skate = Skate();
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
    void* objects = GameVariable<void*>(0x006F85A8); // (the object manager)
    GameFunction<void(__thiscall*)(void*)>(0x0047B000)(objects);
    Mem::Manager* memory = Mem::Manager::Get();
    if (!gamePaused) {
        memory->PushContext(memory->m_bottomUpHeap);
        GameFunction<void(__thiscall*)(void*)>(0x0047C560)(objects);
        Mem::Manager::Get()->PopContext();
    }
    Mem::Manager::Get()->PushContext(Mem::Manager::Get()->m_bottomUpHeap);
    Script::UpdateSpawnedScripts();
    GameFunction<void(__thiscall*)(uint8_t*)>(0x00564D60)(Skate());
    if (PendingScript()) {
        PendingScript() = false;
        auto* object = GameFunction<Obj::CObject*(__thiscall*)(uint8_t*)>(0x00566520)(Skate());
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
    GameFunction<void(__thiscall*)(uint8_t*, bool)>(0x0044E610)(input.m_manager, active);
}

// 0x0054F960
void FrontEnd::PauseGame(bool pause)
{
    if (pause == m_paused)
        return;
    uint8_t* skate = Skate();
    auto* devices = GameVariable<Sio::Manager*>(0x007D1E20);
    if (pause) {
        GameFunction<void(__thiscall*)(uint8_t*, bool)>(0x00564F60)(skate, true);
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
        auto* skater = GameFunction<void***(__thiscall*)(uint8_t*, uint32_t)>(0x005665C0)(skate, i);
        if (!skater)
            continue;
        if (pause)
            reinterpret_cast<void(__thiscall*)(void*)>((*skater)[0x2C / 4])(skater);
        else
            reinterpret_cast<void(__thiscall*)(void*)>((*skater)[0x30 / 4])(skater);
    }
    GameFunction<void(__thiscall*)(void*, bool)>(0x0045F5D0)(GameVariable<void*>(0x006F0FB8), pause);
    GameFunction<void(__cdecl*)(bool)>(0x0044ED10)(pause);
    GameFunction<void(__thiscall*)(void*, bool)>(0x004AAEE0)(GameVariable<void*>(0x00701440), pause);
    GamePaused() = pause;
    m_paused = pause;
    // The main loop's logic tasks: while paused only those of the 0xC kinds run.
    if (pause) {
        *reinterpret_cast<uint32_t*>(MainLoopManager() + 8) = 0xC;
        ReportPauseState();
    } else {
        *reinterpret_cast<uint32_t*>(MainLoopManager() + 8) = 0;
        GameFunction<void(__thiscall*)(uint8_t*, bool)>(0x00564F60)(Skate(), false);
        ReportPauseState();
    }
    if (m_paused || !GameFunction<void*(__thiscall*)(uint8_t*)>(0x00566520)(skate))
        return;
    // Back in the game with a skater: the camera's view mode back to the global's.
    Script::CStruct* params = NewParams();
    params->AddInteger(0u, Script::GetInteger(0x968F7408u));
    GameFunction<bool(__cdecl*)(Script::CStruct*, Script::CScript*)>(0x0059FD90)(params, nullptr); // SetViewMode
    DeleteParams(params);
}

// 0x0054FB40
void __cdecl FrontEnd::InputLogic(FrontEndInputHandler* handler)
{
    FrontEnd* frontEnd = handler->m_data;
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
            // (the auto-repeat of held directions)
            uint32_t repeated = GameFunction<uint32_t(__thiscall*)(FrontEnd*, uint32_t, int)>(0x0054ECE0)(
                frontEnd, handler->m_controlData->m_held, handler->m_index);
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
    GameFunction<void(__thiscall*)(void*)>(0x004ABD00)(GameVariable<void*>(0x00701440));
    Mem::Manager::Get()->PopContext();
}

// 0x0054FD00
void FrontEnd::UpdateInputMappings()
{
    InputManagerRef input;
    for (int i = 0; i < 2; ++i) {
        Dbg::Printf(reinterpret_cast<const char*>(0x0064D82C), i, m_deviceServerMap[i]); // "m_device_server_map[%i] = %i\n"
        FrontEndInputHandler* handler = m_inputHandlers[i];
        if (!handler)
            continue;
        Dbg::Printf(reinterpret_cast<const char*>(0x0064D81C)); // "got a handler\n"
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
    m_vtable = reinterpret_cast<void**>(0x0064D724);
    auto* task = static_cast<FrontEndTask*>(Mem::Calloc(sizeof(FrontEndTask)));
    if (task) {
        ConstructNode(task, -2000);
        *reinterpret_cast<uint32_t*>(task) = 0x006476F0; // Tsk::Task<FrontEnd>
        task->m_code = &LogicTask;
        task->m_data = this;
    }
    m_logicTask = task;
    task = static_cast<FrontEndTask*>(Mem::Calloc(sizeof(FrontEndTask)));
    if (task) {
        ConstructNode(task, -1000);
        *reinterpret_cast<uint32_t*>(task) = 0x006476F0;
        task->m_code = &KeyboardTask;
        task->m_data = this;
    }
    m_keyboardTask = task;
    for (int i = 0; i < 2; ++i) {
        auto* handler = static_cast<FrontEndInputHandler*>(Mem::Calloc(sizeof(FrontEndInputHandler)));
        if (handler) {
            ConstructNode(handler, -1000);
            handler->m_index = i;
            *reinterpret_cast<uint32_t*>(handler) = 0x0064D7D8; // Inp::Handler<FrontEnd>
            handler->m_code = &InputLogic;
            handler->m_data = this;
        }
        m_inputHandlers[i] = handler;
        m_padMappings[i].m_unknown0 = 0;
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

} // namespace Mdl

REPLACE(0x0054EF60, &Mdl::FrontEnd::AddKeyboardHandler);
REPLACE(0x0054EF80, &Mdl::FrontEnd::UpdateButtonEventMappings);
REPLACE(0x0054F0F0, Mdl::CFunc_SetMenuAutoRepeatTimes);
REPLACE(0x0054F150, Mdl::CFunc_SetMenuPadMappings);
REPLACE(0x0054F1B0, Mdl::CFunc_SetButtonEventMappings);
REPLACE(0x0054F200, Mdl::FrontEnd::KeyboardTask);
REPLACE(0x0054F380, &Mdl::FrontEnd::Update);
REPLACE(0x0054F600, &Mdl::FrontEnd::GrabControlEvent);
REPLACE(0x0054F900, &Mdl::FrontEnd::SetAnalogStickActiveForMenus);
REPLACE(0x0054F960, &Mdl::FrontEnd::PauseGame);
REPLACE(0x0054FB40, Mdl::FrontEnd::InputLogic);
REPLACE(0x0054FCC0, Mdl::FrontEnd::LogicTask);
REPLACE(0x0054FD00, &Mdl::FrontEnd::UpdateInputMappings);
REPLACE(0x0054FDC0, Mdl::CFunc_SetAnalogStickActiveForMenus);
REPLACE(0x0054FE00, &Mdl::FrontEnd::Construct);
REPLACE(0x0054FF80, Mdl::CreateFrontEnd);
