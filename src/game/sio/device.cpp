// Serial I/O: the controller ports' devices, the keyboard's state and the device manager's lifetime.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "game/sio/device.h"
#include "game/net/task_vtable.h"
#include "game/goals/goal.h"

#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "game/core/alloc.h"
#include "game/core/crc.h"
#include "game/front/front_end.h"
#include "game/front/screen_element.h"
#include "game/net/gamenet_game_cfuncs.h"
#include "game/skate/profile_manager.h"
#include "game/sys/platform.h"
#include "game/sys/platform_input.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/stubs.h"
#include "game/core/task.h"
#include "game/lst/search.h"
#include "game/pcm/music.h"
#include "game/pcm/music_api.h"
#include "game/script/cscript.h"
#include "game/sio/sdl_input.h"
#include "game/core/code_address.h"
#include "game/core/game_vtables.h"
#include "game/gfx/movie.h"
#include "game/obj/cutscene.h"
#include "game/ed/park_editor.h"
#include "game/front/screen_element_manager.h"

namespace Sio {

namespace {

extern void** const kDeviceVtable;
void** const& kDeviceNodeVtable = Vt::kDeviceNode; // ListKit::Elem<Device>

// The platform layer: a port's DirectInput joystick read for this frame (none: null), and whether the port has
// any keyboard bindings (the keyboard counts as a pad then).
bool PollJoystick(int port) { return OsLayer::ReadJoystick(port) != nullptr; }
bool HasKeyBindings(int port) { return OsLayer::HasKeyBindings(port); }
bool PadPresent(int port) { return PollJoystick(port) || HasKeyBindings(port); }

} // namespace

void SetActuators(int port, int left, int right) { OsLayer::SetActuators(port, left, right); }

// 0x005BD920
InputPort::InputPort(int index, int port, int slot)
{
    m_vtable = kDeviceVtable;
    m_pauseCount = 0;
    m_capabilities = 0;
    auto* node = static_cast<MemSys::ListNode*>(MemSys::AllocZeroed(sizeof(MemSys::ListNode)));
    if (node) {
        node->vtable = kDeviceNodeVtable;
        node->data = this;
        node->priority = 0;
        node->next = node;
        node->prev = node;
    }
    m_listLink = node;
    m_portNo = port;
    m_ordinal = index;
    m_seat = slot;
    m_curPhase = kIdle;
    m_nextState = kIdle;
    m_capabilities = 0;
    m_numActuators = 0;
    m_isUsable = 0;
    std::memset(m_actuatorDirect, 0, sizeof(m_actuatorDirect));
    std::memset(m_alignData, 0xFF, sizeof(m_alignData));
    m_alignData[0] = 0;
    m_alignData[1] = 1;
    std::memset(m_controlData, 0, sizeof(m_controlData));
    m_octets = (m_bufferRaw + 63) & ~63u; // (m_bufferRaw is never set: as in the original)
    m_unpluggedRetry = std::rand() & 63;
}

// 0x005BCD00
InputPort* InputPort::Teardown(unsigned int flags)
{
    m_vtable = kDeviceVtable;
    if (MemSys::ListNode* node = m_listLink)
        reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(node->vtable[0])(node, 1);
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x005BDCC0. The original steps through the states:
//     m_pluggedIn = 0;
//     switch (m_curPhase) { case kDetect: Detect(); break; case kReady: PullData(0); break;
//                        case kSettle: Settle(); break; }
// where PullData polls the pad into m_controlData. Originally from PartyMod Fixes (its jump from here): SDL game controllers
// and the keyboard (game/sio/sdl_input.h).
void InputPort::WorkOn(int /*unused*/)
{
    ProcessSdlDevice(this);
}

// 0x005BCC70
void InputPort::Acquire()
{
    if (m_curPhase == kIdle && PadPresent(m_portNo)) {
        m_isUsable = 0;
        m_curPhase = kDetect;
    }
}

// 0x005BD8B0
void InputPort::Detect()
{
    if (!PadPresent(m_portNo))
        return;
    m_unpluggedCounter = 0;
    m_kindTag = 7;
    m_capabilities |= kAnalog;
    m_actuatorMax[0] = 0xFF;
    m_actuatorMax[1] = 0xFF;
    m_numActuators = 2;
    m_capabilities |= kHasActuators;
    m_curPhase = kSettle;
    m_nextState = kReady;
}

// 0x005BCCC0 (the pad is polled, the result unused)
void InputPort::Settle()
{
    PadPresent(m_portNo);
    m_curPhase = m_nextState;
    m_nextState = kIdle;
}

// 0x005BDA20. The original first reads the pad (PullData(0)); PartyMod removes that call (its Process does
// the reading).
void InputPort::FireActuator(int actuator, int percent)
{
    if (m_actuatorsDisabled)
        return;
    if (m_curPhase != kReady || !(m_capabilities & kHasActuators))
        return;
    if (actuator < 0 || actuator >= m_numActuators)
        return;
    // (x87: max * percent * 0.01f, truncated for the level; the same value * 256 for the motor)
    double level = static_cast<double>(m_actuatorMax[actuator]) * percent * static_cast<double>(0.01f);
    m_actuatorDirect[actuator] = static_cast<uint8_t>(static_cast<int>(level));
    int motor = static_cast<int>(level * 256.0) & 0xFFFF;
    if (actuator == 0)
        SetActuators(m_portNo, motor, m_actuatorDirect[1] << 8);
    else
        SetActuators(m_portNo, m_actuatorDirect[0] << 8, motor);
}

// 0x005BDAD0
void InputPort::StopRumble()
{
    if (m_actuatorsDisabled)
        return;
    if (m_curPhase == kReady && (m_capabilities & kHasActuators)) {
        for (int i = 0; i < m_numActuators; ++i)
            m_actuatorDirect[i] = 0;
        SetActuators(m_portNo, 0, 0);
    }
    m_actuatorsDisabled = 1;
}

// 0x005BDB30. Either way the motors stop; the flag ends as it was (the first branch clears it so that
// StopRumble acts, which sets it again).
void InputPort::ClearRumble()
{
    if (m_actuatorsDisabled) {
        m_actuatorsDisabled = 0;
        StopRumble();
        return;
    }
    StopRumble();
    m_actuatorsDisabled = 0;
}

// 0x005BDB60
void InputPort::PauseActuators()
{
    if (m_pauseCount == 0 && m_curPhase == kReady && (m_capabilities & kHasActuators)) {
        for (int i = 0; i < m_numActuators; ++i) {
            m_actuatorSaved[i] = m_actuatorDirect[i];
            m_actuatorDirect[i] = 0;
        }
        SetActuators(m_portNo, 0, 0);
    }
    ++m_pauseCount;
}

// 0x005BDBD0
void InputPort::UnpauseActuators()
{
    if (m_pauseCount == 0 || --m_pauseCount != 0)
        return;
    if (m_curPhase == kReady && (m_capabilities & kHasActuators)) {
        for (int i = 0; i < m_numActuators; ++i)
            m_actuatorDirect[i] = m_actuatorSaved[i];
        SetActuators(m_portNo, m_actuatorDirect[0] << 8, m_actuatorDirect[1] << 8);
    }
}

// 0x005BDC60
void InputPort::StopActuators()
{
    if (m_curPhase == kReady && (m_capabilities & kHasActuators)) {
        for (int i = 0; i < m_numActuators; ++i) {
            m_actuatorDirect[i] = 0;
            m_actuatorSaved[i] = 0;
        }
        SetActuators(m_portNo, 0, 0);
    }
}

// --- the keyboard -------------------------------------------------------------------------------------------

namespace {
IDirectInputDevice8A*& KeyboardDevice() { return OsLayer::g_keyboardDevice; }

int g_keyboardBuffer{2};
uint32_t g_managerReferences;
uint32_t g_keyboardCharCount;

// Which of the two state buffers is current (2 at start: clear both first).
int& KeyboardBuffer() { return g_keyboardBuffer; }

char g_keyNameResult[256];
char g_nameBuffer[256];
int g_keyboardChars[16];
uint8_t g_keyboardState[2][256];

uint8_t (&KeyboardState())[2][256] { return g_keyboardState; }
uint32_t& KeyboardCharCount() { return g_keyboardCharCount; }
int (&KeyboardChars())[16] { return g_keyboardChars; }
} // namespace

// 0x005BDD60
void ReadKeyboard()
{
    KeyboardCharCount() = 0;
    if (KeyboardBuffer() == 2) {
        std::memset(KeyboardState(), 0, sizeof(KeyboardState()));
        KeyboardBuffer() = 1;
    }
    IDirectInputDevice8A* keyboard = KeyboardDevice();
    if (!keyboard)
        return;
    KeyboardBuffer() = KeyboardBuffer() == 0 ? 1 : 0;
    HRESULT result = keyboard->GetDeviceState(256, KeyboardState()[KeyboardBuffer()]);
    if (result != DIERR_INPUTLOST && result != DIERR_NOTACQUIRED)
        return;
    if (SUCCEEDED(KeyboardDevice()->Acquire()))
        KeyboardDevice()->GetDeviceState(256, KeyboardState()[KeyboardBuffer()]);
    else
        std::memset(KeyboardState()[KeyboardBuffer()], 0, 256);
}

// 0x005BDD30
int GetKeyboardChars(char* out)
{
    uint32_t i = 0;
    for (; i < KeyboardCharCount(); ++i)
        out[i] = static_cast<char>(KeyboardChars()[i]);
    return static_cast<int>(KeyboardCharCount());
}

// 0x005BDE20
void ClearKeyboardChars() { KeyboardCharCount() = 0; }

// --- the manager's lifetime --------------------------------------------------------------------------------

Hub* g_manager;

Hub*& ManagerInstance() { return g_manager; }

namespace {
uint32_t& ManagerReferences() { return g_managerReferences; }
} // namespace

// 0x005BE110: the device list node's scalar deleting destructor
MemSys::ListNode* DestroyDeviceNode(MemSys::ListNode* node, unsigned int flags)
{
    node->UnlinkAs(kDeviceNodeVtable);
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(node);
    return node;
}

// --- typed keys -------------------------------------------------------------------------------------------

namespace {
// Whether typed keys are wanted: the keyboard is being typed on (0x007CE46E), or text entry is on
// (0x007CCDF8) outside a menu (0x007CE46F). PartyMod sets the last two for its text input.
bool& KeyboardTyping() { return g_keyboardOnScreen; }
bool& TextEntry() { return OnlinePlay::g_textEntry; }
bool& InMenu() { return g_inMenuFlag; }
} // namespace

// 0x005BDE70
void KeyInput(int key, int flags)
{
    if (!KeyboardTyping() && (!TextEntry() || InMenu()))
        return;
    if (flags < 0)
        return;
    bool queue = true;
    switch (key) {
    case '\r': key = 0x19; break; // Enter
    case '\b': key = 0x18; break; // Backspace
    case 0x1B:                    // Escape (not its auto-repeats)
        if (flags & 0x40000000)
            queue = false;
        else
            key = 0x1A;
        break;
    case ';': key = ':'; break;
    // (not typeable in the game's fonts)
    case -1: case '%': case '~': case '\\': case '|': case '[': case ']': case '}': case '"':
        queue = false;
        break;
    default: break;
    }
    if (queue)
        KeyboardChars()[KeyboardCharCount()++] = key;
    if (KeyboardCharCount() == 16)
        KeyboardCharCount() = 15; // (full: the last one is overwritten)
}

// --- special keys and key names ---------------------------------------------------------------------------

namespace {
// The other "in a menu" flag (PartyMod's names) and the quit request.
bool& MenuActive() { return GameModules::g_menusActive; }
bool& QuitRequested() { return Engine3D::g_quitRequested; }
// Port 0's key bindings (DirectInput key codes; -1 = none).
int KeyBinding(int action) { return OsLayer::g_keyBindings[action]; }
// The platform layer's key names.
const wchar_t* KeyNameW(int code) { return OsLayer::KeyName(code); }
char (&NameBuffer())[256] { return g_nameBuffer; }
char (&KeyNameResult())[256] { return g_keyNameResult; }

void ConvertKeyName(int code)
{
#pragma warning(suppress : 4996) // (as the original: the C runtime's conversion, at most 256 bytes)
    std::wcstombs(NameBuffer(), KeyNameW(code), 256);
    NameBuffer()[255] = '\0';
}

// The character a key (by scan code) types, for the menu hints.
char KeyChar(UINT scanCode)
{
    return static_cast<char>(MapVirtualKeyA(MapVirtualKeyA(scanCode, MAPVK_VSC_TO_VK), MAPVK_VK_TO_CHAR));
}

// The key code shown for a button: in a menu (outside the park editor's piece menu, not typing)...
int MenuKey(int button)
{
    switch (button) {
    case 0: return KeyBinding(11);
    case 1: return KeyBinding(10);
    case 2: return KeyBinding(9);
    case 3: return KeyBinding(8);
    case 8: return 1;  // Escape
    case 9: return 0x39; // space
    case 14: return KeyBinding(16);
    case 15: return KeyBinding(17);
    case 16: return KeyBinding(14);
    case 17: return KeyBinding(15);
    case 21: return KeyBinding(18);
    case 22: return 0x1C; // Enter
    case 23: return 1;
    case 24: return 0x12; // E
    case 25: return 0x13; // R
    case 26: return 0x0C; // -
    case 27: return 0x0D; // =
    case 28: return 2;  // 1
    case 29: return 3;  // 2
    default: return 0;
    }
}
// ...while typing...
int TypingKey(int button)
{
    switch (button) {
    case 0: case 1: case 2: case 3: case 8: case 9: case 14: case 15: case 16: case 17: return -1;
    case 22: return 0x1C;
    case 23: return 1;
    default: return 0;
    }
}
// ...and otherwise.
int PlayKey(int button)
{
    switch (button) {
    case 0: return KeyBinding(11);
    case 1: return KeyBinding(10);
    case 2: return KeyBinding(9);
    case 3: return KeyBinding(8);
    case 8: return 1;
    case 9: return KeyBinding(13);
    case 14: return KeyBinding(16);
    case 15: return KeyBinding(17);
    case 16: return KeyBinding(14);
    case 17: return KeyBinding(15);
    case 21: return KeyBinding(18);
    default: return 0;
    }
}
} // namespace

// 0x005BCD50
const char* GetButtonKeyName(int button)
{
    bool menu = MenuActive() || InMenu();
    bool typing = KeyboardTyping();
    bool pieceMenu = false;
    MenuSystem::ElementPtr element;
    MenuSystem::FetchPart(&element, Checksums::HashFromText("select_skater_hmenu"));
    if (element)
        element->IsConcealed(); // (the result isn't used)
    {
        MenuSystem::ElementPtr found;
        MenuSystem::FetchPart(&found, Checksums::HashFromText("piece_menu_hmenu"));
        element = found;
    }
    if (!QuitRequested() && element && !element->IsConcealed())
        pieceMenu = true;
    {
        MenuSystem::ElementPtr found;
        MenuSystem::FetchPart(&found, Checksums::HashFromText("select_skater_hmenu"));
        element = found;
        found.Release();
    }
    if (element)
        element->IsConcealed(); // (the result isn't used)

    bool menuKeys = !pieceMenu && menu && !typing;
    int code = menuKeys ? MenuKey(button) : typing ? TypingKey(button) : PlayKey(button);
    ConvertKeyName(code);
    std::snprintf(KeyNameResult(), sizeof(KeyNameResult()), "%s", NameBuffer());
    if (button == 0x12) {
        // two keys: "first+second"
        ConvertKeyName(KeyBinding(16));
        std::snprintf(KeyNameResult(), sizeof(KeyNameResult()), "%s+", NameBuffer());
        ConvertKeyName(KeyBinding(17));
        size_t used = std::strlen(KeyNameResult()); // (the original's strcat is unbounded)
        size_t length = std::strlen(NameBuffer());
        if (length > sizeof(KeyNameResult()) - 1 - used)
            length = sizeof(KeyNameResult()) - 1 - used;
        std::memcpy(KeyNameResult() + used, NameBuffer(), length);
        KeyNameResult()[used + length] = '\0';
    }
    if (menuKeys && (button == 0x14 || button == 0x13)) {
        // the two keys' characters: "S/X" or "Z/C" on a QWERTY keyboard
        char first = KeyChar(button == 0x14 ? 0x1F : 0x2C);
        char second = KeyChar(button == 0x14 ? 0x2D : 0x2E);
        KeyNameResult()[0] = first;
        KeyNameResult()[1] = '/';
        KeyNameResult()[2] = second;
        KeyNameResult()[3] = '\0';
    }
    return KeyNameResult();
}

// 0x005BDF30
void SpecialKeyInput(int key, int flags)
{
    MenuSystem::ElementPtr element;
    MenuSystem::FetchPart(&element, Checksums::HashFromText("menu_network_select_refresh"));
    bool serverList = false;
    if (element && !element->IsConcealed())
        serverList = true;
    else if (!KeyboardTyping() && (!TextEntry() || InMenu()))
        return;
    if (flags < 0)
        return;
    if (serverList && key == VK_F5) {
        Scripts::LaunchScript("refresh_chosen", nullptr, nullptr, false);
        return;
    }
    int queued = 0;
    if (InMenu()) {
        switch (key) {
        case VK_DOWN: queued = 0x17; break;
        case VK_UP: queued = 0x16; break;
        case VK_LEFT: queued = 0x14; break;
        case VK_RIGHT: queued = 0x15; break;
        default: break;
        }
    } else {
        switch (key) {
        case VK_F1: queued = 0x1B; break;
        case VK_F2: queued = 0x1C; break;
        case VK_F3: queued = 0x1D; break;
        case VK_F4: queued = 0x1E; break;
        default: break;
        }
    }
    if (queued)
        KeyboardChars()[KeyboardCharCount()++] = queued;
    if (KeyboardCharCount() == 16)
        KeyboardCharCount() = 15;
}

// --- the original pad read --------------------------------------------------------------------------------

namespace {
struct LevelInfo {
    uint8_t unknown[0x40];
    uint32_t checksum;
};
} // namespace

namespace {
// Flags the read consults (meaning partly unknown): two that turn the menu handling off, and the level
// (0x007CE5D8; its +0x40 is the level's checksum - 0x7ECA21E5 is the park editor's).
bool& Flag696D95() { return Engine3D::g_moviePlaying; }
bool& Flag6F1158() { return GameObjects::g_cutscenePlaying; }
constexpr uint32_t kParkEditorLevel = 0x7ECA21E5;
// The platform layer: the pad mode and the default keys' Enter / Escape (OsLayer::SetPadMode...), its window
// message pump, the pad read and whether a DirectInput key is down.
void SetPadMode(int mode) { OsLayer::SetPadMode(mode); }
void SetDefaultEnterControl(int control) { OsLayer::SetDefaultEnterControl(control); }
void SetDefaultEscapeControl(int control) { OsLayer::SetDefaultEscapeControl(control); }
void PumpWindowMessages() { OsLayer::ProcessWindowEvents(); }
void ReadPadState(int port, PadState* state) { OsLayer::ReadPad(port + 1, state); }
bool KeyDown(int code) { return OsLayer::KeyDown(code); }
// The port the front end is taking input from (a getter, 0x00553E90, on the object at 0x007CE4B4).
int MenuPort() { return reinterpret_cast<int(__thiscall*)(void*)>(CodeAddress(&GoalSystem::CObjective::FetchArgs))(reinterpret_cast<void*>(GameObjects::g_profileManager)); }

uint8_t IfAbove(uint8_t value, uint8_t threshold, uint8_t bit) { return value > threshold ? bit : 0; }
uint8_t PressureOrZero(uint8_t value) { return value > 0x20 ? value : 0; }
uint8_t AllOrNothing(uint32_t value) { return value ? 0xFF : 0; }
// A stick from -32768..32767 to 0..255 (signed division, rounding toward zero).
uint8_t Stick(int value) { return static_cast<uint8_t>(value / 256); }
} // namespace

// 0x005BD200
void InputPort::PullData(int)
{
    if (QuitRequested())
        return;
    bool menu = MenuActive() || InMenu();
    bool typing = KeyboardTyping();
    bool selectSkaterShown = false;
    bool pieceMenuShown = false;
    bool playing = false;

    // Which menus are up (the skater select's horizontal menu is checked before and after the park editor's).
    MenuSystem::ElementPtr element;
    MenuSystem::FetchPart(&element, Checksums::HashFromText("select_skater_hmenu"));
    if (element && !element->IsConcealed())
        selectSkaterShown = true;
    {
        MenuSystem::ElementPtr found;
        MenuSystem::FetchPart(&found, Checksums::HashFromText("piece_menu_hmenu"));
        element = found;
    }
    if (!QuitRequested() && element && !element->IsConcealed())
        pieceMenuShown = true;
    {
        MenuSystem::ElementPtr found;
        MenuSystem::FetchPart(&found, Checksums::HashFromText("select_skater_hmenu"));
        element = found;
    }
    if (element && !element->IsConcealed())
        selectSkaterShown = true;
    bool menuMode = !pieceMenuShown && menu && !typing;
    if (Flag696D95() || Flag6F1158()) {
        pieceMenuShown = false;
        menuMode = false;
    }

    // In menus only the port the front end listens to (or port 0, or both on the skater select) reads.
    int menuPort = MenuPort();
    SetDefaultEnterControl(0);  // (the pressure button 0)
    SetDefaultEscapeControl(1); // (the pressure button 1)
    if (typing) {
        SetPadMode(2);
    } else if (menuMode) {
        if ((!selectSkaterShown || menuPort == m_portNo) && m_portNo == 0) {
            SetPadMode(1);
        } else if (!selectSkaterShown) {
            SetPadMode(2);
        } else if (menuPort == m_portNo && m_portNo == 1) {
            SetPadMode(3);
        } else {
            m_pluggedIn = true;
            element.Release();
            return;
        }
    } else if (pieceMenuShown) {
        if (m_portNo != 0) {
            m_pluggedIn = true;
            element.Release();
            return;
        }
        SetPadMode(0);
    } else {
        SetPadMode(0);
        playing = true;
    }
    PumpWindowMessages();
    m_pluggedIn = false;
    if (!PadPresent(m_portNo))
        return;

    PadState pad;
    ReadPadState(m_portNo, &pad);
    const uint8_t* p = pad.pressure;
    uint16_t buttons = pad.buttons;
    bool keyboardMenu = m_keyboardControl && menuMode; // (the keyboard drives the menus: some buttons ignored)
    m_isUsable = 1;
    m_pluggedIn = true;
    // Two bytes of button bits, each set while the button is up.
    m_controlData[0] = 0;
    m_controlData[1] = 0x70;
    uint8_t first = keyboardMenu ? 0xFF : static_cast<uint8_t>(~(((buttons & 0xFF) >> 1) & 8));
    first ^= (buttons >> 5) & 7;
    m_controlData[2] = first;
    uint8_t second = static_cast<uint8_t>(~(p[1] >= 0x20 ? 0x20 : 0)) ^ (p[3] >= 0x20 ? 0x10 : 0);
    if (!keyboardMenu)
        second ^= p[0] >= 0x20 ? 0x40 : 0;
    second ^= p[2] >= 0x20 ? 0x80 : 0;
    uint8_t buttonsSoFar = second;
    LevelInfo* level = reinterpret_cast<LevelInfo*>(ParkEditing::g_parkEditor);
    if (level && level->checksum == kParkEditorLevel) {
        // The park editor: the triggers are mapped differently while its element 0x31631B98 is up.
        bool editorElement = false;
        if (MenuSystem::g_screenElementManager) {
            MenuSystem::ElementPtr found;
            MenuSystem::FetchPart(&found, 0x31631B98);
            editorElement = found.Pull() != nullptr;
            found.Release();
        }
        second ^= IfAbove(p[7], 0x20, editorElement ? 2 : 4);
        second ^= IfAbove(p[6], 0x20, 1);
    } else {
        second = buttonsSoFar ^ IfAbove(p[6], 0x80, 1) ^ IfAbove(p[7], 0x80, 2) ^ IfAbove(p[6], 0x20, 4) ^
                 IfAbove(p[7], 0x20, 8);
    }
    m_controlData[3] = second;
    // Pressures (dead zone 0x20).
    m_controlData[12] = PressureOrZero(p[3]);
    m_controlData[13] = PressureOrZero(p[1]);
    m_controlData[14] = keyboardMenu ? 0 : PressureOrZero(p[0]);
    m_controlData[15] = PressureOrZero(p[2]);
    m_controlData[16] = PressureOrZero(p[6]);
    m_controlData[17] = PressureOrZero(p[7]);
    m_controlData[18] = 0;
    m_controlData[19] = 0;
    m_controlData[20] = 0;
    // The d-pad: pressures, and its bits in the first byte.
    m_controlData[10] = AllOrNothing(buttons & 1);
    m_controlData[8] = AllOrNothing(buttons & 8);
    m_controlData[11] = AllOrNothing(buttons & 2);
    uint8_t low = static_cast<uint8_t>(buttons);
    uint8_t dpad = static_cast<uint8_t>((low & 0xFE) << 1);
    dpad = static_cast<uint8_t>((dpad ^ (low & 1)) << 2);
    dpad = static_cast<uint8_t>((dpad ^ (low & 8)) << 2);
    m_controlData[2] ^= dpad;
    m_controlData[9] = AllOrNothing(buttons & 4);
    // The sticks (y inverted).
    m_controlData[4] = Stick(pad.sticks[2] + 0x7FFF);
    m_controlData[5] = Stick(0x7FFF - pad.sticks[3]);
    m_controlData[6] = Stick(pad.sticks[0] + 0x7FFF);
    m_controlData[7] = Stick(0x7FFF - pad.sticks[1]);
    if (p[4] > 0x20)
        m_controlData[20] = 1;
    if (p[5] > 0x20)
        m_controlData[20] |= 2;
    m_heldDown = ((m_controlData[2] << 8 | m_controlData[3]) ^ 0xFFFF) != 0;

    // The keyboard takes over the menus after start, a pressed trigger, a typed Enter, or (playing) Enter.
    bool enterTyped = false;
    if (typing) {
        char typed[256];
        int count = GetKeyboardChars(typed);
        for (int i = 0; i < count; ++i) {
            if (typed[i] == 0x19) {
                enterTyped = true;
                break;
            }
        }
    }
    m_keyboardControl = (buttons & 0x10) || p[0] >= 0x20 || enterTyped || (playing && KeyDown(0x1C));
}

// --- the manager -------------------------------------------------------------------------------------------

namespace {
extern void** const kManagerVtable;

} // namespace

// 0x005BE1F0
Hub::Hub()
{
    m_vtable = kManagerVtable;
    m_devs.data = reinterpret_cast<void*>(-1);
    m_devs.priority = 0;
    m_devs.next = &m_devs;
    m_devs.prev = &m_devs;
    m_devs.vtable = kDeviceNodeVtable;
    auto* task = static_cast<uint32_t*>(MemSys::AllocZeroed(0x20));
    if (task) {
        reinterpret_cast<TaskList::SchedulerTask*>(task)->MakeNew(0);                 // TaskList::SchedulerTask(priority 0)
        task[0] = reinterpret_cast<uint32_t>(GameCode::kTaskVtable);                                             // TaskList::Task<ListKit::Foremost<Device>>
        task[6] = reinterpret_cast<uint32_t>(&ProcessTask);
        task[7] = reinterpret_cast<uint32_t>(&m_devs);
    }
    m_task = task;
    int index = 0;
    for (int port = 0; port < 2; ++port) {
        void* memory = MemSys::AllocZeroed(sizeof(InputPort));
        InputPort* device = memory ? new (memory) InputPort(index, port, 0) : nullptr;
        if (!device)
            continue;
        MemSys::ListNode* node = device->m_listLink; // onto the end of the list
        node->prev = m_devs.prev;
        node->next = &m_devs;
        m_devs.prev->next = node;
        m_devs.prev = node;
        device->Acquire();
        ++index;
    }
    if (!AudioStream::MusicOffRequested()) // (the music system is set up with the controllers)
        AudioStream::PrepareAll();
}

// 0x005BE300
void Hub::DestroyBody()
{
    m_vtable = kManagerVtable;
    ListKit::Hunt<InputPort> search;
    for (InputPort* device = search.HeadElement(&m_devs); device;) {
        InputPort* next = search.FollowingItem();
        reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(device->m_vtable[0])(device, 1);
        device = next;
    }
    if (auto* task = static_cast<void***>(m_task))
        reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>((*task)[2])(task, 1);
    Stubs::ReturnZero(); // (a folded empty function - compiled-out code)
    if (!AudioStream::MusicOffRequested())
        AudioStream::StopUserSoundtrack();
    m_devs.UnlinkAs(kDeviceNodeVtable);
}

// 0x005BE6C0
Hub* Hub::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

namespace {
void ProcessAll(MemSys::ListNode* devices)
{
    ListKit::Hunt<InputPort> search;
    bool first = true;
    for (InputPort* device = search.HeadElement(devices); device; device = search.FollowingItem()) {
        device->WorkOn(first);
        first = false;
    }
}
} // namespace

// 0x005BE140
void __cdecl ProcessTask(void* task) { ProcessAll(static_cast<MemSys::ListNode*>(static_cast<void**>(task)[7])); }

// 0x005BE3F0
void Hub::PollPads() { ProcessAll(&m_devs); }

// 0x005BE490
InputPort* Hub::FetchGadget(int port, int slot)
{
    ListKit::Hunt<InputPort> search;
    for (InputPort* device = search.HeadElement(&m_devs); device; device = search.FollowingItem()) {
        if (device->m_portNo == port && device->m_seat == slot)
            return device;
    }
    return nullptr;
}

// 0x005BE4F0
InputPort* Hub::GetPadAtSlot(int index)
{
    ListKit::Hunt<InputPort> search;
    for (InputPort* device = search.HeadElement(&m_devs); device; device = search.FollowingItem()) {
        if (device->m_ordinal == index)
            return device;
    }
    return nullptr;
}

// 0x005BE550
void Hub::PauseDevices()
{
    ListKit::Hunt<InputPort> search;
    for (InputPort* device = search.HeadElement(&m_devs); device; device = search.FollowingItem())
        device->PauseActuators();
}

// 0x005BE5E0
void Hub::UnpauseDevices()
{
    ListKit::Hunt<InputPort> search;
    for (InputPort* device = search.HeadElement(&m_devs); device; device = search.FollowingItem())
        device->UnpauseActuators();
}

// 0x005BE670
Hub* CreateManager(bool create)
{
    if (!ManagerInstance() && create) {
        if (void* memory = MemSys::AllocZeroed(0x1C, nullptr, 1)) {
            ManagerInstance() = new (memory) Hub();
            ++ManagerReferences();
            return ManagerInstance();
        }
        ManagerInstance() = nullptr;
    }
    ++ManagerReferences();
    return ManagerInstance();
}

// 0x005BDCF0
void ReleaseManager()
{
    if (ManagerReferences() > 1) {
        --ManagerReferences();
        return;
    }
    if (Hub* manager = ManagerInstance()) {
        void** vtable = *reinterpret_cast<void***>(manager);
        reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(vtable[0])(manager, 1);
    }
    ManagerInstance() = nullptr;
    ManagerReferences() = 0;
}

namespace {
static VtableSlot kDeviceVtable_slots[] = {&Sio::InputPort::Teardown};
void** const kDeviceVtable = reinterpret_cast<void**>(kDeviceVtable_slots);
static VtableSlot kManagerVtable_slots[] = {&Sio::Hub::Teardown};
void** const kManagerVtable = reinterpret_cast<void**>(kManagerVtable_slots);
} // namespace

} // namespace Sio

namespace {

struct DeviceAdapter {
    Sio::InputPort* MakeNew(int index, int port, int slot) { return new (this) Sio::InputPort(index, port, slot); }
    Sio::Hub* ConstructManager() { return new (this) Sio::Hub(); }
    MemSys::ListNode* DestroyNode(unsigned int flags) { return Sio::DestroyDeviceNode(reinterpret_cast<MemSys::ListNode*>(this), flags); }
};

} // namespace

namespace Vt {
static VtableSlot kDeviceNode_slots[] = {&DeviceAdapter::DestroyNode};
void** const kDeviceNode = reinterpret_cast<void**>(kDeviceNode_slots);
} // namespace Vt
