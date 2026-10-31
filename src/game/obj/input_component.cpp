// The input component (Obj::CInputComponent): see input_component.h. From the disassembly of 0x0041E6F0-0x0041F3FB.

#include "game/obj/input_component.h"
#include "game/net/task_vtable.h"

#include <cstdint>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/front/screen_element.h"
#include "game/front/screen_element_manager.h"
#include "game/net/gamenet_manager.h"
#include "game/obj/vibration_component.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/sio/inpman.h"
#include "game/sys/timer.h"
#include "game/obj/collide_and_die.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "game/skater/skitch.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/object.h"
#include "game/skater/vehicle_sound.h"
#include "hook/game_code.h"
#include "game/obj/camera_lookaround.h"

namespace Obj {

GAME_GLOBAL(uint32_t[90], g_inputComponentMemberFunctions, 0x00684C78,
            0xBF7D6C46, 0x27D7CD28, 0, 0, 0, 0,
            0x6E24F5FD, 0x27D7CD28, 0, 0, 0, 0,
            0x56C6C0CD, 0x27D7CD28, 0, 0, 0, 0,
            0x6DF51774, 0x27D7CD28, 0, 0, 0, 0,
            0x31C6454F, 0x27D7CD28, 0, 0, 0, 0,
            0x0660746A, 0x27D7CD28, 0, 0, 0, 0,
            0x78238598, 0x27D7CD28, 0, 0, 0, 0,
            0x1D604A9D, 0x27D7CD28, 0, 0, 0, 0,
            0x89AA9B5F, 0x27D7CD28, 0, 0, 0, 0,
            0x83028FC5, 0x27D7CD28, 0, 0, 0, 0,
            0xB2A022DE, 0x27D7CD28, 0, 0, 0, 0,
            0x2366469C, 0x27D7CD28, 0, 0, 0, 0,
            0x1090A150, 0x27D7CD28, 0, 0, 0, 0,
            0x28729460, 0x27D7CD28, 0, 0, 0, 0,
            0, 0x27D7CD28, 0, 0, 0, 0);

namespace {

extern void** const kVtable;

constexpr uint32_t kPlayer = 0x67E6859A;      // Player
constexpr uint32_t kController = 0xB30D9965;  // controller
constexpr uint32_t kUnknownB9B1E3B6 = 0xB9B1E3B6;
constexpr uint32_t kVibration = 0x404EB936;   // vibration
constexpr uint32_t kTime = 0x906B67BA;        // time
constexpr uint32_t kClear = 0x1A4E0EF9;       // clear
constexpr uint32_t kButton = 0xC5F953C2;      // button
constexpr uint32_t kSeconds = 0xD029F619, kSecond = 0x49E0EE96; // seconds, second
constexpr uint32_t kFrames = 0x019176C5, kFrame = 0x4A07C332;   // frames, frame
constexpr uint32_t kControllerUnpluggedDialog = 0x2EDB780F; // controller_unplugged_dialog_anchor
constexpr uint32_t kRootWindow = 0x56A1EAE3;                // root_window
constexpr uint32_t kMenuState = 0xCDC00E63;                 // menu_state
constexpr uint32_t kOn = 0xF649D637;                        // on
constexpr uint32_t kKeyboardAnchor = 0x31631B98;            // keyboard_anchor
constexpr float kMilliseconds = 1000.0f;            // 0x006453C0
constexpr float kFrameMilliseconds = 16.6666660f;   // 0x006460DC (0x41855555)
constexpr float kRadiansToDegrees = 57.2957764f;    // 0x00645468 (0x42652EE0)
constexpr double kTwoTo32 = 4294967296.0;           // 0x006453B4

// The pad events: a button's pressure index in the controller data, its event bit, its events down / up and the
// first pad's button (an offset into the pad; -1: none).
struct EventButton {
    int index;
    uint32_t bit;
    uint32_t down;
    uint32_t up;
    int button;
};
const EventButton kEventButtons[] = {
    {6, 0x40, 0x5C8FAA09, 0xB27804A2, 0x000},          {7, 0x80, 0xD26ADAE9, 0xAEF7DFD3, 0x020},
    {5, 0x20, 0xB4F2ADBA, 0xC86FA880, 0x040},          {4, 0x10, 0x0EDBBC2B, 0xC8ABF89C, 0x060},
    {9, 0x200, 0xB069B600, 0xE5AAE06B, 0x140},         {0xB, 0x800, 0xA93DBBD0, 0xFCFEEDBB, 0x160},
    {8, 0x100, 0x7F695A77, 0x7B684919, 0x180},         {0xA, 0x400, 0x42717176, 0xBBBEF674, 0x1A0},
    {0xC, 0x1000, 0xC6547217, 0x28A3DCBC, 0x080},      {0xE, 0x4000, 0x5F5D23AD, 0xB1AA8D06, 0x0A0},
    {0x10, 0x10000, 0x285A133B, 0xC6ADBD90, 0x0C0},    {0xD, 0x2000, 0x12154DC8, 0xFCE2E363, 0x0E0},
    {0xF, 0x8000, 0x8B1C1C72, 0x65EBB2D9, 0x100},      {0x11, 0x20000, 0xFC1B2CE4, 0x12EC824F, 0x120},
    {0x16, 0x400000, 0x33947317, 0xF5E437A0, -1},      {0x17, 0x800000, 0xF8DE049B, 0x3EAE402C, -1},
    {0x18, 0x1000000, 0xAC7F105A, 0x55B09758, -1},
};

// The original's _ftol (0x006236FC): truncated towards zero, the low 32 bits of the 64-bit result.
int Truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// The input manager (0x00696D40): its controller servers (0x84 bytes each from +4: the device, then the handler list
// - a Tsk::Stack - at +4, the data at +0x40), and a player's controller (0x0044E630).
uint8_t* InputManager() { return reinterpret_cast<uint8_t*>(Inp::g_inputManager); }
Tsk::Stack* ServerHandlers(uint8_t* manager, int server)
{
    return reinterpret_cast<Tsk::Stack*>(manager + server * 0x84 + 8);
}
void PlaceHandler(CInputComponent::Handler* handler, uint8_t* manager, int server)
{
    handler->m_controlData = manager + server * 0x84 + 0x44;
    handler->m_device = *reinterpret_cast<void**>(manager + server * 0x84 + 4);
}
int PlayerController(int player)
{
    return reinterpret_cast<Inp::Manager*>(InputManager())->GetPlayerController(player);
}

// A new handler (on the current heap).
CInputComponent::Handler* NewHandler(int index, void(__cdecl* code)(CInputComponent::Handler*), CInputComponent* component)
{
    auto* handler = static_cast<CInputComponent::Handler*>(Mem::Calloc(sizeof(CInputComponent::Handler)));
    if (!handler)
        return nullptr;
    handler->Construct(-1);
    handler->m_index = index;
    handler->m_vtable = GameCode::kInputHandlerVtable;
    handler->m_code = code;
    handler->m_component = component;
    return handler;
}

// The screen element `id`, if there's one (the smart pointer let go at once, as the original does).
Front::CScreenElement* FindElement(uint32_t id)
{
    Front::ElementPtr found;
    Front::CScreenElementManager::Instance()->GetElement(&found, id, 0);
    Front::CScreenElement* element = found.Get();
    found.Release();
    return element;
}

} // namespace

// 0x0041EEC0
CBaseComponent* __cdecl CInputComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CInputComponent));
    return memory ? static_cast<CInputComponent*>(memory)->Construct() : nullptr;
}

// 0x0041E6F0
void __cdecl CInputComponent::RegisterMemberFunctions()
{
    Script::RegisterMemberFunctionTable(reinterpret_cast<void*>(kType), g_inputComponentMemberFunctions, 0x18);
}

// 0x0041E850
CInputComponent* CInputComponent::Construct()
{
    CBaseComponent::Construct();
    m_vtable = kVtable;
    m_pad.Init();
    m_pad2.Init();
    m_type = kType;
    mp_handler = nullptr;
    m_unknown49C = 0;
    return this;
}

// 0x0041E710
void CInputComponent::DestroyBody()
{
    m_vtable = kVtable;
    for (Handler* handler : {mp_handler, mp_handler2}) {
        if (!handler)
            continue;
        if (handler->InList())
            handler->Remove();
        reinterpret_cast<void*(__thiscall*)(Handler*, unsigned int)>(handler->m_vtable[2])(handler, 1);
    }
    CBaseComponent::DestroyBody();
}

// 0x0041E8C0
CInputComponent* CInputComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0041F390
void CInputComponent::InitFromStructure(Script::CStruct* params)
{
    int value;
    if (params->GetInteger(kPlayer, &value, 0))
        BindToController(PlayerController(value));
    else if (params->GetInteger(kController, &value, 0))
        BindToController(value);
    params->GetChecksum(kUnknownB9B1E3B6, &m_unknown49C, 0);
}

// 0x0041F1B0
void CInputComponent::BindToController(int controller)
{
    if (!mp_handler) {
        Mem::Manager* memory = Mem::Manager::Get();
        memory->PushContext(memory->m_skaterInfoHeap);
        mp_handler = NewHandler(0, HandlerCode, this);
        uint8_t* manager = InputManager();
        ServerHandlers(manager, mp_handler->m_index)->AddTask(mp_handler);
        PlaceHandler(mp_handler, manager, mp_handler->m_index);
        mp_handler2 = NewHandler(1, HandlerCode2, this);
        manager = InputManager();
        ServerHandlers(manager, mp_handler2->m_index)->AddTask(mp_handler2);
        PlaceHandler(mp_handler2, manager, mp_handler2->m_index);
        Mem::Manager::Get()->PopContext();
    }
    Handler* handler = mp_handler;
    uint8_t* manager = InputManager();
    handler->Remove();
    ServerHandlers(manager, controller)->AddTask(handler);
    PlaceHandler(handler, manager, controller);
    Handler* handler2 = mp_handler2;
    manager = InputManager();
    handler2->Remove();
    ServerHandlers(manager, 1)->AddTask(handler2);
    PlaceHandler(handler2, manager, 1);
    if (CBaseComponent* vibration = m_object->GetComponent(kVibration))
        static_cast<CVibrationComponent*>(vibration)->SetDevice(reinterpret_cast<Sio::Device*>(mp_handler->m_device));
}

// 0x0041F170
void __cdecl CInputComponent::HandlerCode(Handler* handler) { handler->m_component->UpdatePad(handler->m_controlData); }

// 0x0041F190
void __cdecl CInputComponent::HandlerCode2(Handler* handler) { handler->m_component->UpdatePad2(handler->m_controlData); }

// 0x0041F090
void CInputComponent::UpdatePad(const uint8_t* data)
{
    if (m_object->m_compositeFlags & COMPOSITE_FLAG_PAUSED)
        return;
    UpdateEvents(data);
    bool blocked = IsInputBlocked();
    if (!m_disabled && !m_netDisabled && !blocked) {
        m_pad.Update(data);
        return;
    }
    m_pad.Reset();
    if (blocked)
        m_events = 0;
}

// 0x0041F100
void CInputComponent::UpdatePad2(const uint8_t* data)
{
    if (m_object->m_compositeFlags & COMPOSITE_FLAG_PAUSED)
        return;
    bool blocked = IsInputBlocked();
    if (!m_disabled && !m_netDisabled && !blocked) {
        m_pad2.Update(data);
        return;
    }
    m_pad2.Reset();
    if (blocked)
        m_events = 0;
}

// 0x0041EF20
bool CInputComponent::IsInputBlocked()
{
    if (!g_gameNetManager->InNetGame())
        return false;
    if (FindElement(kControllerUnpluggedDialog))
        return true;
    if (Front::CScreenElement* root = FindElement(kRootWindow)) {
        Script::CStruct* tags = Script::NewStruct();
        if (tags)
            tags = tags->Init();
        root->CopyTagsTo(tags);
        uint32_t state = 0; // (uninitialised in the original: what was on the stack)
        tags->GetChecksum(kMenuState, &state, 0);
        if (tags) {
            tags->Clear(); // (the destructor)
            Script::DeleteStruct(tags);
        }
        if (state == kOn)
            return true;
    }
    return FindElement(kKeyboardAnchor) != nullptr;
}

// 0x0041ECA0
void CInputComponent::UpdateEvents(const uint8_t* data)
{
    m_lastEvents = m_events;
    m_events = 0;
    for (const EventButton& event : kEventButtons) {
        CControlButton* button =
            event.button < 0 ? nullptr
                             : reinterpret_cast<CControlButton*>(reinterpret_cast<uint8_t*>(&m_pad) + event.button);
        CheckButton(data, event.index, event.bit, event.down, event.up, button);
    }
}

// 0x0041EBF0
void CInputComponent::CheckButton(const uint8_t* data, int index, uint32_t bit, uint32_t down, uint32_t up,
                                  CControlButton* button)
{
    if (button) {
        uint32_t lockedUntil = static_cast<uint32_t>(Truncated(button->m_lockedUntil));
        if (Tmr::GetTime() < lockedUntil)
            return;
    }
    bool eventsOn = m_inputEvents && !m_disabled && !m_netDisabled;
    if (data[index]) {
        if (!(m_lastEvents & bit) && eventsOn)
            m_object->SelfEvent(down, nullptr);
        m_events |= bit;
    } else if ((m_lastEvents & bit) && eventsOn) {
        m_object->SelfEvent(up, nullptr);
    }
}

// 0x0041E7E0
void CInputComponent::LockButton(uint32_t name, float seconds, bool release)
{
    CControlButton* button = m_pad.GetButton(name);
    uint32_t now = Tmr::GetTime();
    double time = static_cast<double>(static_cast<int32_t>(now));
    if (static_cast<int32_t>(now) < 0)
        time += kTwoTo32;
    button->m_lockedUntil = static_cast<float>(Truncated(time + static_cast<double>(seconds) * kMilliseconds));
    if (release) {
        button->SetPressed(0);
        button->ClearTrigger();
        button->ClearRelease();
    }
}

// 0x0041E8F0
int CInputComponent::LockButtonFromScript(Script::CStruct* params, Script::CScript*)
{
    uint32_t name = 0;
    if (params->GetChecksum(0u, &name, 0)) {
        float time = 1.0f;
        params->GetFloat(kTime, &time, 0);
        // (the original's file name: its __FILE__ from 12 characters in)
        Dbg::Printf("D:%s:%i: time = %g\n", "omponents\\InputComponent.cpp", 0x94, static_cast<double>(time));
        int clear = 0;
        params->GetInteger(kClear, &clear, 0);
        LockButton(name, time, clear != 0);
    }
    return 1;
}

// 0x0041E9A0
int CInputComponent::DisablePlayerInput(Script::CStruct*, Script::CScript*)
{
    m_disabled = true;
    return 1;
}

// 0x0041E9B0
int CInputComponent::EnablePlayerInput(Script::CStruct*, Script::CScript*)
{
    m_disabled = false;
    return 1;
}

// 0x0041E9C0
int CInputComponent::NetDisablePlayerInput(Script::CStruct*, Script::CScript*)
{
    m_netDisabled = true;
    return 1;
}

// 0x0041E9D0
int CInputComponent::NetEnablePlayerInput(Script::CStruct*, Script::CScript*)
{
    m_netDisabled = false;
    return 1;
}

// 0x0041E7C0
int CInputComponent::EnableInputEvents(Script::CStruct*, Script::CScript*)
{
    m_inputEvents = true;
    return 1;
}

// 0x0041E7D0
int CInputComponent::DisableInputEvents(Script::CStruct*, Script::CScript*)
{
    m_inputEvents = false;
    return 1;
}

// 0x0041E9E0
int CInputComponent::PlayerInputIsDisabled(Script::CStruct*, Script::CScript*)
{
    return m_disabled || m_netDisabled ? 1 : 0;
}

// 0x0041EA20
int CInputComponent::LeftPressed(Script::CStruct*, Script::CScript*) { return m_pad.m_left.m_pressed != 0 ? 1 : 0; }

// 0x0041EA30
int CInputComponent::RightPressed(Script::CStruct*, Script::CScript*) { return m_pad.m_right.m_pressed != 0 ? 1 : 0; }

// 0x0041EA40
int CInputComponent::UpPressed(Script::CStruct*, Script::CScript*) { return m_pad.m_up.m_pressed != 0 ? 1 : 0; }

// 0x0041EA50
int CInputComponent::DownPressed(Script::CStruct*, Script::CScript*) { return m_pad.m_down.m_pressed != 0 ? 1 : 0; }

// 0x0041EA60
int CInputComponent::GetSticks(Script::CStruct*, Script::CScript* script)
{
    script->m_params->AddFloat(0x737F15CF, m_pad.m_polar[1].angle * kRadiansToDegrees);
    script->m_params->AddFloat(0x686AD1C2,
                               static_cast<float>(StickLength(m_pad.m_stick[2], m_pad.m_stick[3], m_pad.m_polar[1].angle)));
    script->m_params->AddFloat(0xD6030CEA, m_pad.m_polar[0].angle * kRadiansToDegrees);
    script->m_params->AddFloat(0xC94777AC,
                               static_cast<float>(StickLength(m_pad.m_stick[0], m_pad.m_stick[1], m_pad.m_polar[0].angle)));
    return 1;
}

// 0x0041EB20
int CInputComponent::HeldLongerThan(Script::CStruct* params, Script::CScript*)
{
    uint32_t name;
    params->GetChecksum(kButton, &name, 1);
    float time;
    std::memcpy(&time, &params, sizeof(time)); // (the original's slot for it: the params argument)
    params->GetFloat(0u, &time, 1);
    double scaled = time;
    if (params->ContainsFlag(kSeconds) || params->ContainsFlag(kSecond))
        scaled = static_cast<double>(time) * kMilliseconds;
    else if (params->ContainsFlag(kFrames) || params->ContainsFlag(kFrame))
        scaled = static_cast<double>(time) * kFrameMilliseconds;
    uint32_t milliseconds = static_cast<uint32_t>(Truncated(scaled));
    CControlButton* button = m_pad.GetButton(name);
    if (button && button->m_pressed && static_cast<uint32_t>(button->GetPressedTime()) > milliseconds)
        return 1;
    return 0;
}

} // namespace Obj

REPLACE(0x0041EEC0, Obj::CInputComponent::Create);
REPLACE(0x0041E6F0, Obj::CInputComponent::RegisterMemberFunctions);
REPLACE(0x0041E850, &Obj::CInputComponent::Construct);
REPLACE(0x0041E710, &Obj::CInputComponent::DestroyBody);
REPLACE(0x0041E8C0, &Obj::CInputComponent::Destroy);
REPLACE(0x0041F390, &Obj::CInputComponent::InitFromStructure);
REPLACE(0x0041F1B0, &Obj::CInputComponent::BindToController);
REPLACE(0x0041F170, Obj::CInputComponent::HandlerCode);
REPLACE(0x0041F190, Obj::CInputComponent::HandlerCode2);
REPLACE(0x0041F090, &Obj::CInputComponent::UpdatePad);
REPLACE(0x0041F100, &Obj::CInputComponent::UpdatePad2);
REPLACE(0x0041EF20, &Obj::CInputComponent::IsInputBlocked);
REPLACE(0x0041ECA0, &Obj::CInputComponent::UpdateEvents);
REPLACE(0x0041EBF0, &Obj::CInputComponent::CheckButton);
REPLACE(0x0041E7E0, &Obj::CInputComponent::LockButton);
REPLACE(0x0041E8F0, &Obj::CInputComponent::LockButtonFromScript);
REPLACE(0x0041E9A0, &Obj::CInputComponent::DisablePlayerInput);
REPLACE(0x0041E9B0, &Obj::CInputComponent::EnablePlayerInput);
REPLACE(0x0041E9C0, &Obj::CInputComponent::NetDisablePlayerInput);
REPLACE(0x0041E9D0, &Obj::CInputComponent::NetEnablePlayerInput);
REPLACE(0x0041E7C0, &Obj::CInputComponent::EnableInputEvents);
REPLACE(0x0041E7D0, &Obj::CInputComponent::DisableInputEvents);
REPLACE(0x0041E9E0, &Obj::CInputComponent::PlayerInputIsDisabled);
REPLACE(0x0041EA20, &Obj::CInputComponent::LeftPressed);
REPLACE(0x0041EA30, &Obj::CInputComponent::RightPressed);
REPLACE(0x0041EA40, &Obj::CInputComponent::UpPressed);
REPLACE(0x0041EA50, &Obj::CInputComponent::DownPressed);
REPLACE(0x0041EA60, &Obj::CInputComponent::GetSticks);
REPLACE(0x0041EB20, &Obj::CInputComponent::HeldLongerThan);

namespace Obj {
namespace {

GAME_VTABLE(kVtable, 0x0064605C,
    &Obj::CInputComponent::Destroy,
    &Obj::ComponentWithoutUpdate::Update,
    &Obj::CInputComponent::InitFromStructure,
    Stubs::DoNothing,
    Stubs::DoNothing,
    Obj::RefreshFromStructure,
    Stubs::IgnoreArguments2,
    &Obj::CBaseComponent::Suspend,
    Stubs::IgnoreArgument,
    Stubs::DoNothing,
    Stubs::IgnoreArgument,
    &Obj::CBaseComponent::RunMemberFunction,
    &Obj::ComponentWithoutMemberFunctions::CallMemberFunction);

} // namespace
} // namespace Obj
