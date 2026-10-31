// The input component (GameObjects::CPadInputModule): see input_component.h. From the disassembly of 0x0041E6F0-0x0041F3FB.

#include "game/obj/input_component.h"
#include "game/net/task_vtable.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>

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
#include "game/core/code_address.h"
#include "game/skater/skitch.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/object.h"
#include "game/skater/vehicle_sound.h"
#include "game/obj/camera_lookaround.h"

namespace GameObjects {

uint32_t g_inputComponentMemberFunctions[90]{
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
            0, 0x27D7CD28, 0, 0, 0, 0};

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
// - a TaskList::PileUp - at +4, the data at +0x40), and a player's controller (0x0044E630).
uint8_t* InputManager() { return reinterpret_cast<uint8_t*>(PadInput::g_inputManager); }
TaskList::PileUp* ServerHandlers(uint8_t* manager, int server)
{
    return reinterpret_cast<TaskList::PileUp*>(manager + server * 0x84 + 8);
}
void PlaceHandler(CPadInputModule::Responder* handler, uint8_t* manager, int server)
{
    handler->m_controlData = manager + server * 0x84 + 0x44;
    handler->m_dev = *reinterpret_cast<void**>(manager + server * 0x84 + 4);
}
int PlayerController(int player)
{
    return reinterpret_cast<PadInput::Hub*>(InputManager())->GetPlayerController(player);
}

// A new handler (on the current heap).
CPadInputModule::Responder* NewHandler(int index, void(__cdecl* code)(CPadInputModule::Responder*), CPadInputModule* component)
{
    auto* handler = static_cast<CPadInputModule::Responder*>(MemSys::AllocZeroed(sizeof(CPadInputModule::Responder)));
    if (!handler)
        return nullptr;
    handler->MakeNew(-1);
    handler->m_ordinal = index;
    handler->m_vtable = GameCode::kInputHandlerVtable;
    handler->m_codeValue = code;
    handler->m_component = component;
    return handler;
}

// The screen element `id`, if there's one (the smart pointer let go at once, as the original does).
MenuSystem::CUiWidget* FindElement(uint32_t id)
{
    MenuSystem::ElementPtr found;
    MenuSystem::CUiTree::Sole()->FetchPart(&found, id, 0);
    MenuSystem::CUiWidget* element = found.Pull();
    found.Release();
    return element;
}

} // namespace

// 0x0041EEC0
CObjectModule* __cdecl CPadInputModule::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CPadInputModule));
    return memory ? static_cast<CPadInputModule*>(memory)->MakeNew() : nullptr;
}

// 0x0041E6F0
void __cdecl CPadInputModule::PublishMethodTable()
{
    Scripts::RegisterMemberFunctionTable(reinterpret_cast<void*>(kType), g_inputComponentMemberFunctions, 0x18);
}

// 0x0041E850
CPadInputModule* CPadInputModule::MakeNew()
{
    CObjectModule::MakeNew();
    m_vtable = kVtable;
    m_padPrimary.PrepareAll();
    m_padSecondary.PrepareAll();
    m_kindTag = kType;
    mp_handler = nullptr;
    m_unknown49C = 0;
    return this;
}

// 0x0041E710
void CPadInputModule::DestroyBody()
{
    m_vtable = kVtable;
    for (Responder* handler : {mp_handler, mp_handler2}) {
        if (!handler)
            continue;
        if (handler->IsOnTaskList())
            handler->DropOut();
        reinterpret_cast<void*(__thiscall*)(Responder*, unsigned int)>(handler->m_vtable[2])(handler, 1);
    }
    CObjectModule::DestroyBody();
}

// 0x0041E8C0
CPadInputModule* CPadInputModule::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x0041F390
void CPadInputModule::ConfigureFromData(Scripts::CStruct* params)
{
    int value;
    if (params->FetchIntValue(kPlayer, &value, 0))
        LinkToPad(PlayerController(value));
    else if (params->FetchIntValue(kController, &value, 0))
        LinkToPad(value);
    params->ReadHashValue(kUnknownB9B1E3B6, &m_unknown49C, 0);
}

// 0x0041F1B0
void CPadInputModule::LinkToPad(int controller)
{
    if (!mp_handler) {
        MemSys::Hub* memory = MemSys::Hub::Pull();
        memory->EnterHeapScope(memory->m_skaterInfoHeap);
        mp_handler = NewHandler(0, HandlerCode, this);
        uint8_t* manager = InputManager();
        ServerHandlers(manager, mp_handler->m_ordinal)->QueueJob(mp_handler);
        PlaceHandler(mp_handler, manager, mp_handler->m_ordinal);
        mp_handler2 = NewHandler(1, HandlerCode2, this);
        manager = InputManager();
        ServerHandlers(manager, mp_handler2->m_ordinal)->QueueJob(mp_handler2);
        PlaceHandler(mp_handler2, manager, mp_handler2->m_ordinal);
        MemSys::Hub::Pull()->LeaveHeapScope();
    }
    Responder* handler = mp_handler;
    uint8_t* manager = InputManager();
    handler->DropOut();
    ServerHandlers(manager, controller)->QueueJob(handler);
    PlaceHandler(handler, manager, controller);
    Responder* handler2 = mp_handler2;
    manager = InputManager();
    handler2->DropOut();
    ServerHandlers(manager, 1)->QueueJob(handler2);
    PlaceHandler(handler2, manager, 1);
    if (CObjectModule* vibration = m_entity->FetchModule(kVibration))
        static_cast<CPadRumbleModule*>(vibration)->ChangeGadget(reinterpret_cast<Sio::InputPort*>(mp_handler->m_dev));
}

// 0x0041F170
void __cdecl CPadInputModule::HandlerCode(Responder* handler) { handler->m_component->UpdatePad(handler->m_controlData); }

// 0x0041F190
void __cdecl CPadInputModule::HandlerCode2(Responder* handler) { handler->m_component->UpdatePad2(handler->m_controlData); }

// 0x0041F090
void CPadInputModule::UpdatePad(const uint8_t* data)
{
    if (m_entity->m_compositeFlags & COMPOSITE_FLAG_PAUSED)
        return;
    UpdateEvents(data);
    bool blocked = IsInputBlocked();
    if (!m_disabled && !m_netDisabled && !blocked) {
        m_padPrimary.TickFrame(data);
        return;
    }
    m_padPrimary.Reset();
    if (blocked)
        m_events = 0;
}

// 0x0041F100
void CPadInputModule::UpdatePad2(const uint8_t* data)
{
    if (m_entity->m_compositeFlags & COMPOSITE_FLAG_PAUSED)
        return;
    bool blocked = IsInputBlocked();
    if (!m_disabled && !m_netDisabled && !blocked) {
        m_padSecondary.TickFrame(data);
        return;
    }
    m_padSecondary.Reset();
    if (blocked)
        m_events = 0;
}

// 0x0041EF20
bool CPadInputModule::IsInputBlocked()
{
    if (!g_gameNetManager->InNetGame())
        return false;
    if (FindElement(kControllerUnpluggedDialog))
        return true;
    if (MenuSystem::CUiWidget* root = FindElement(kRootWindow)) {
        Scripts::CStruct* tags = Scripts::NewStruct();
        if (tags)
            tags = tags->PrepareAll();
        root->CopyTagsTo(tags);
        uint32_t state = 0; // (uninitialised in the original: what was on the stack)
        tags->ReadHashValue(kMenuState, &state, 0);
        if (tags) {
            tags->Clear(); // (the destructor)
            Scripts::DeleteStruct(tags);
        }
        if (state == kOn)
            return true;
    }
    return FindElement(kKeyboardAnchor) != nullptr;
}

// 0x0041ECA0
void CPadInputModule::UpdateEvents(const uint8_t* data)
{
    m_lastEvents = m_events;
    m_events = 0;
    for (const EventButton& event : kEventButtons) {
        CControlButton* button =
            event.button < 0 ? nullptr
                             : reinterpret_cast<CControlButton*>(reinterpret_cast<uint8_t*>(&m_padPrimary) + event.button);
        ReadButtonState(data, event.index, event.bit, event.down, event.up, button);
    }
}

// 0x0041EBF0
void CPadInputModule::ReadButtonState(const uint8_t* data, int index, uint32_t bit, uint32_t down, uint32_t up,
                                  CControlButton* button)
{
    if (button) {
        uint32_t lockedUntil = static_cast<uint32_t>(Truncated(button->m_lockedUntil));
        if (FrameClock::GetTime() < lockedUntil)
            return;
    }
    bool eventsOn = m_inputEvents && !m_disabled && !m_netDisabled;
    if (data[index]) {
        if (!(m_lastEvents & bit) && eventsOn)
            m_entity->PostOwnEvent(down, nullptr);
        m_events |= bit;
    } else if ((m_lastEvents & bit) && eventsOn) {
        m_entity->PostOwnEvent(up, nullptr);
    }
}

// 0x0041E7E0
void CPadInputModule::LockButton(uint32_t name, float seconds, bool release)
{
    CControlButton* button = m_padPrimary.ReadButton(name);
    uint32_t now = FrameClock::GetTime();
    double time = static_cast<double>(static_cast<int32_t>(now));
    if (static_cast<int32_t>(now) < 0)
        time += kTwoTo32;
    button->m_lockedUntil = static_cast<float>(Truncated(time + static_cast<double>(seconds) * kMilliseconds));
    if (release) {
        button->SetHeldDown(0);
        button->ResetActivation();
        button->ForgetLetGo();
    }
}

// 0x0041E8F0
int CPadInputModule::LockButtonFromScript(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t name = 0;
    if (params->ReadHashValue(0u, &name, 0)) {
        float time = 1.0f;
        params->FetchFloat(kTime, &time, 0);
        // (the original's file name: its __FILE__ from 12 characters in)
        DebugOut::Printf("D:%s:%i: time = %g\n", "omponents\\InputComponent.cpp", 0x94, static_cast<double>(time));
        int clear = 0;
        params->FetchIntValue(kClear, &clear, 0);
        LockButton(name, time, clear != 0);
    }
    return 1;
}

// 0x0041E9A0
int CPadInputModule::DisablePlayerInput(Scripts::CStruct*, Scripts::CScript*)
{
    m_disabled = true;
    return 1;
}

// 0x0041E9B0
int CPadInputModule::EnablePlayerInput(Scripts::CStruct*, Scripts::CScript*)
{
    m_disabled = false;
    return 1;
}

// 0x0041E9C0
int CPadInputModule::NetDisablePlayerInput(Scripts::CStruct*, Scripts::CScript*)
{
    m_netDisabled = true;
    return 1;
}

// 0x0041E9D0
int CPadInputModule::NetEnablePlayerInput(Scripts::CStruct*, Scripts::CScript*)
{
    m_netDisabled = false;
    return 1;
}

// 0x0041E7C0
int CPadInputModule::EnableInputEvents(Scripts::CStruct*, Scripts::CScript*)
{
    m_inputEvents = true;
    return 1;
}

// 0x0041E7D0
int CPadInputModule::DisableInputEvents(Scripts::CStruct*, Scripts::CScript*)
{
    m_inputEvents = false;
    return 1;
}

// 0x0041E9E0
int CPadInputModule::PlayerInputIsDisabled(Scripts::CStruct*, Scripts::CScript*)
{
    return m_disabled || m_netDisabled ? 1 : 0;
}

// 0x0041EA20
int CPadInputModule::LeftPressed(Scripts::CStruct*, Scripts::CScript*) { return m_padPrimary.m_leftSide.m_heldDown != 0 ? 1 : 0; }

// 0x0041EA30
int CPadInputModule::RightPressed(Scripts::CStruct*, Scripts::CScript*) { return m_padPrimary.m_rightSide.m_heldDown != 0 ? 1 : 0; }

// 0x0041EA40
int CPadInputModule::UpPressed(Scripts::CStruct*, Scripts::CScript*) { return m_padPrimary.m_upDir.m_heldDown != 0 ? 1 : 0; }

// 0x0041EA50
int CPadInputModule::DownPressed(Scripts::CStruct*, Scripts::CScript*) { return m_padPrimary.m_btnDown.m_heldDown != 0 ? 1 : 0; }

// 0x0041EA60
int CPadInputModule::GetSticks(Scripts::CStruct*, Scripts::CScript* script)
{
    script->m_args->PushFloat(0x737F15CF, m_padPrimary.m_polar[1].angle * kRadiansToDegrees);
    script->m_args->PushFloat(0x686AD1C2,
                               static_cast<float>(StickLength(m_padPrimary.m_stick[2], m_padPrimary.m_stick[3], m_padPrimary.m_polar[1].angle)));
    script->m_args->PushFloat(0xD6030CEA, m_padPrimary.m_polar[0].angle * kRadiansToDegrees);
    script->m_args->PushFloat(0xC94777AC,
                               static_cast<float>(StickLength(m_padPrimary.m_stick[0], m_padPrimary.m_stick[1], m_padPrimary.m_polar[0].angle)));
    return 1;
}

// 0x0041EB20
int CPadInputModule::HeldLongerThan(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t name;
    params->ReadHashValue(kButton, &name, 1);
    float time;
    std::memcpy(&time, &params, sizeof(time)); // (the original's slot for it: the params argument)
    params->FetchFloat(0u, &time, 1);
    double scaled = time;
    if (params->HasBareFlag(kSeconds) || params->HasBareFlag(kSecond))
        scaled = static_cast<double>(time) * kMilliseconds;
    else if (params->HasBareFlag(kFrames) || params->HasBareFlag(kFrame))
        scaled = static_cast<double>(time) * kFrameMilliseconds;
    uint32_t milliseconds = static_cast<uint32_t>(Truncated(scaled));
    CControlButton* button = m_padPrimary.ReadButton(name);
    if (button && button->m_heldDown && static_cast<uint32_t>(button->GetHeldMillis()) > milliseconds)
        return 1;
    return 0;
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CPadInputModule::Teardown,
         &GameObjects::ComponentWithoutUpdate::TickFrame,
         &GameObjects::CPadInputModule::ConfigureFromData,
         Stubs::DoNothing,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         &GameObjects::CObjectModule::RunMemberFunction,
         &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
