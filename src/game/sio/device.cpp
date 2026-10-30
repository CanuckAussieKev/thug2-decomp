// Serial I/O: the controller ports' devices, the keyboard's state and the device manager's lifetime.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "game/sio/device.h"

#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>

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
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "game/gfx/movie.h"
#include "game/obj/cutscene.h"
#include "game/ed/park_editor.h"
#include "game/front/screen_element_manager.h"


namespace Sio {

namespace {

void** const kDeviceVtable = reinterpret_cast<void**>(0x00658A6C);
void** const kDeviceNodeVtable = reinterpret_cast<void**>(0x00658A70); // Lst::Node<Device>

// The platform layer: a port's DirectInput joystick read for this frame (none: null), and whether the port has
// any keyboard bindings (the keyboard counts as a pad then).
bool PollJoystick(int port) { return Platform::ReadJoystick(port) != nullptr; }
bool HasKeyBindings(int port) { return Platform::HasKeyBindings(port); }
bool PadPresent(int port) { return PollJoystick(port) || HasKeyBindings(port); }

} // namespace

void SetActuators(int port, int left, int right) { Platform::SetActuators(port, left, right); }

// 0x005BD920
Device::Device(int index, int port, int slot)
{
    m_vtable = kDeviceVtable;
    m_pauseCount = 0;
    m_capabilities = 0;
    auto* node = static_cast<Mem::ListNode*>(Mem::Calloc(sizeof(Mem::ListNode)));
    if (node) {
        node->vtable = kDeviceNodeVtable;
        node->data = this;
        node->priority = 0;
        node->next = node;
        node->prev = node;
    }
    m_node = node;
    m_port = port;
    m_index = index;
    m_slot = slot;
    m_state = kIdle;
    m_nextState = kIdle;
    m_capabilities = 0;
    m_numActuators = 0;
    m_valid = 0;
    std::memset(m_actuatorDirect, 0, sizeof(m_actuatorDirect));
    std::memset(m_alignData, 0xFF, sizeof(m_alignData));
    m_alignData[0] = 0;
    m_alignData[1] = 1;
    std::memset(m_controlData, 0, sizeof(m_controlData));
    m_buffer = (m_bufferRaw + 63) & ~63u; // (m_bufferRaw is never set: as in the original)
    m_unpluggedRetry = std::rand() & 63;
}

// 0x005BCD00
Device* Device::Destroy(unsigned int flags)
{
    m_vtable = kDeviceVtable;
    if (Mem::ListNode* node = m_node)
        reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(node->vtable[0])(node, 1);
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x005BDCC0. The original steps through the states:
//     m_pluggedIn = 0;
//     switch (m_state) { case kDetect: Detect(); break; case kReady: ReadData(0); break;
//                        case kSettle: Settle(); break; }
// where ReadData polls the pad into m_controlData. Originally from PartyMod Fixes (its jump from here): SDL game controllers
// and the keyboard (game/sio/sdl_input.h).
void Device::Process(int /*unused*/)
{
    ProcessSdlDevice(this);
}

// 0x005BCC70
void Device::Acquire()
{
    if (m_state == kIdle && PadPresent(m_port)) {
        m_valid = 0;
        m_state = kDetect;
    }
}

// 0x005BD8B0
void Device::Detect()
{
    if (!PadPresent(m_port))
        return;
    m_unpluggedCounter = 0;
    m_type = 7;
    m_capabilities |= kAnalog;
    m_actuatorMax[0] = 0xFF;
    m_actuatorMax[1] = 0xFF;
    m_numActuators = 2;
    m_capabilities |= kHasActuators;
    m_state = kSettle;
    m_nextState = kReady;
}

// 0x005BCCC0 (the pad is polled, the result unused)
void Device::Settle()
{
    PadPresent(m_port);
    m_state = m_nextState;
    m_nextState = kIdle;
}

// 0x005BDA20. The original first reads the pad (ReadData(0)); PartyMod removes that call (its Process does
// the reading).
void Device::ActivateActuator(int actuator, int percent)
{
    if (m_actuatorsDisabled)
        return;
    if (m_state != kReady || !(m_capabilities & kHasActuators))
        return;
    if (actuator < 0 || actuator >= m_numActuators)
        return;
    // (x87: max * percent * 0.01f, truncated for the level; the same value * 256 for the motor)
    double level = static_cast<double>(m_actuatorMax[actuator]) * percent * static_cast<double>(0.01f);
    m_actuatorDirect[actuator] = static_cast<uint8_t>(static_cast<int>(level));
    int motor = static_cast<int>(level * 256.0) & 0xFFFF;
    if (actuator == 0)
        SetActuators(m_port, motor, m_actuatorDirect[1] << 8);
    else
        SetActuators(m_port, m_actuatorDirect[0] << 8, motor);
}

// 0x005BDAD0
void Device::DisableActuators()
{
    if (m_actuatorsDisabled)
        return;
    if (m_state == kReady && (m_capabilities & kHasActuators)) {
        for (int i = 0; i < m_numActuators; ++i)
            m_actuatorDirect[i] = 0;
        SetActuators(m_port, 0, 0);
    }
    m_actuatorsDisabled = 1;
}

// 0x005BDB30. Either way the motors stop; the flag ends as it was (the first branch clears it so that
// DisableActuators acts, which sets it again).
void Device::ResetActuators()
{
    if (m_actuatorsDisabled) {
        m_actuatorsDisabled = 0;
        DisableActuators();
        return;
    }
    DisableActuators();
    m_actuatorsDisabled = 0;
}

// 0x005BDB60
void Device::PauseActuators()
{
    if (m_pauseCount == 0 && m_state == kReady && (m_capabilities & kHasActuators)) {
        for (int i = 0; i < m_numActuators; ++i) {
            m_actuatorSaved[i] = m_actuatorDirect[i];
            m_actuatorDirect[i] = 0;
        }
        SetActuators(m_port, 0, 0);
    }
    ++m_pauseCount;
}

// 0x005BDBD0
void Device::UnpauseActuators()
{
    if (m_pauseCount == 0 || --m_pauseCount != 0)
        return;
    if (m_state == kReady && (m_capabilities & kHasActuators)) {
        for (int i = 0; i < m_numActuators; ++i)
            m_actuatorDirect[i] = m_actuatorSaved[i];
        SetActuators(m_port, m_actuatorDirect[0] << 8, m_actuatorDirect[1] << 8);
    }
}

// 0x005BDC60
void Device::StopActuators()
{
    if (m_state == kReady && (m_capabilities & kHasActuators)) {
        for (int i = 0; i < m_numActuators; ++i) {
            m_actuatorDirect[i] = 0;
            m_actuatorSaved[i] = 0;
        }
        SetActuators(m_port, 0, 0);
    }
}

// --- the keyboard -------------------------------------------------------------------------------------------

namespace {
IDirectInputDevice8A*& KeyboardDevice() { return Platform::g_keyboardDevice; }

GAME_GLOBAL(int, g_keyboardBuffer, 0x00684FC4, 2);
GAME_GLOBAL(uint32_t, g_managerReferences, 0x007D1E24);
GAME_GLOBAL(uint32_t, g_keyboardCharCount, 0x007D1E28);

// Which of the two state buffers is current (2 at start: clear both first).
int& KeyboardBuffer() { return g_keyboardBuffer; }

GAME_GLOBAL(char[256], g_keyNameResult, 0x007D19D8);
GAME_GLOBAL(char[256], g_nameBuffer, 0x007D1AD8);
GAME_GLOBAL(int[16], g_keyboardChars, 0x007D1BE0);
GAME_GLOBAL(uint8_t[2][256], g_keyboardState, 0x007D1C20);

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

GAME_GLOBAL(Manager*, g_manager, 0x007D1E20);

Manager*& ManagerInstance() { return g_manager; }

namespace {
uint32_t& ManagerReferences() { return g_managerReferences; }
} // namespace

// 0x005BE110: the device list node's scalar deleting destructor
Mem::ListNode* DestroyDeviceNode(Mem::ListNode* node, unsigned int flags)
{
    node->UnlinkAs(reinterpret_cast<void**>(0x00658A70));
    if (flags & 1)
        Mem::Manager::Get()->Free(node);
    return node;
}

// --- typed keys -------------------------------------------------------------------------------------------

namespace {
// Whether typed keys are wanted: the keyboard is being typed on (0x007CE46E), or text entry is on
// (0x007CCDF8) outside a menu (0x007CE46F). PartyMod sets the last two for its text input.
bool& KeyboardTyping() { return g_keyboardOnScreen; }
bool& TextEntry() { return GameNet::g_textEntry; }
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
bool& MenuActive() { return Mdl::g_menusActive; }
bool& QuitRequested() { return Nx::g_quitRequested; }
// Port 0's key bindings (DirectInput key codes; -1 = none).
int KeyBinding(int action) { return Platform::g_keyBindings[action]; }
// The platform layer's key names.
const wchar_t* KeyNameW(int code) { return Platform::KeyName(code); }
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
    Front::ElementPtr element;
    Front::GetElement(&element, Crc::GenerateCRCFromString("select_skater_hmenu"));
    if (element)
        element->IsHidden(); // (the result isn't used)
    {
        Front::ElementPtr found;
        Front::GetElement(&found, Crc::GenerateCRCFromString("piece_menu_hmenu"));
        element = found;
    }
    if (!QuitRequested() && element && !element->IsHidden())
        pieceMenu = true;
    {
        Front::ElementPtr found;
        Front::GetElement(&found, Crc::GenerateCRCFromString("select_skater_hmenu"));
        element = found;
        found.Release();
    }
    if (element)
        element->IsHidden(); // (the result isn't used)

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
    Front::ElementPtr element;
    Front::GetElement(&element, Crc::GenerateCRCFromString("menu_network_select_refresh"));
    bool serverList = false;
    if (element && !element->IsHidden())
        serverList = true;
    else if (!KeyboardTyping() && (!TextEntry() || InMenu()))
        return;
    if (flags < 0)
        return;
    if (serverList && key == VK_F5) {
        Script::RunScript("refresh_chosen", nullptr, nullptr, false);
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
bool& Flag696D95() { return Nx::g_moviePlaying; }
bool& Flag6F1158() { return Obj::g_cutscenePlaying; }
constexpr uint32_t kParkEditorLevel = 0x7ECA21E5;
// The platform layer: the pad mode and the default keys' Enter / Escape (Platform::SetPadMode...), its window
// message pump, the pad read and whether a DirectInput key is down.
void SetPadMode(int mode) { Platform::SetPadMode(mode); }
void SetDefaultEnterControl(int control) { Platform::SetDefaultEnterControl(control); }
void SetDefaultEscapeControl(int control) { Platform::SetDefaultEscapeControl(control); }
void PumpWindowMessages() { Platform::ProcessWindowEvents(); }
void ReadPadState(int port, PadState* state) { Platform::ReadPad(port + 1, state); }
bool KeyDown(int code) { return Platform::KeyDown(code); }
// The port the front end is taking input from (a getter, 0x00553E90, on the object at 0x007CE4B4).
int MenuPort() { return GameFunction<int(__thiscall*)(void*)>(0x00553E90)(reinterpret_cast<void*>(Obj::g_profileManager)); }

uint8_t IfAbove(uint8_t value, uint8_t threshold, uint8_t bit) { return value > threshold ? bit : 0; }
uint8_t PressureOrZero(uint8_t value) { return value > 0x20 ? value : 0; }
uint8_t AllOrNothing(uint32_t value) { return value ? 0xFF : 0; }
// A stick from -32768..32767 to 0..255 (signed division, rounding toward zero).
uint8_t Stick(int value) { return static_cast<uint8_t>(value / 256); }
} // namespace

// 0x005BD200
void Device::ReadData(int)
{
    if (QuitRequested())
        return;
    bool menu = MenuActive() || InMenu();
    bool typing = KeyboardTyping();
    bool selectSkaterShown = false;
    bool pieceMenuShown = false;
    bool playing = false;

    // Which menus are up (the skater select's horizontal menu is checked before and after the park editor's).
    Front::ElementPtr element;
    Front::GetElement(&element, Crc::GenerateCRCFromString("select_skater_hmenu"));
    if (element && !element->IsHidden())
        selectSkaterShown = true;
    {
        Front::ElementPtr found;
        Front::GetElement(&found, Crc::GenerateCRCFromString("piece_menu_hmenu"));
        element = found;
    }
    if (!QuitRequested() && element && !element->IsHidden())
        pieceMenuShown = true;
    {
        Front::ElementPtr found;
        Front::GetElement(&found, Crc::GenerateCRCFromString("select_skater_hmenu"));
        element = found;
    }
    if (element && !element->IsHidden())
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
        if ((!selectSkaterShown || menuPort == m_port) && m_port == 0) {
            SetPadMode(1);
        } else if (!selectSkaterShown) {
            SetPadMode(2);
        } else if (menuPort == m_port && m_port == 1) {
            SetPadMode(3);
        } else {
            m_pluggedIn = true;
            element.Release();
            return;
        }
    } else if (pieceMenuShown) {
        if (m_port != 0) {
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
    if (!PadPresent(m_port))
        return;

    PadState pad;
    ReadPadState(m_port, &pad);
    const uint8_t* p = pad.pressure;
    uint16_t buttons = pad.buttons;
    bool keyboardMenu = m_keyboardControl && menuMode; // (the keyboard drives the menus: some buttons ignored)
    m_valid = 1;
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
    LevelInfo* level = reinterpret_cast<LevelInfo*>(Ed::g_parkEditor);
    if (level && level->checksum == kParkEditorLevel) {
        // The park editor: the triggers are mapped differently while its element 0x31631B98 is up.
        bool editorElement = false;
        if (Front::g_screenElementManager) {
            Front::ElementPtr found;
            Front::GetElement(&found, 0x31631B98);
            editorElement = found.Get() != nullptr;
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
    m_pressed = ((m_controlData[2] << 8 | m_controlData[3]) ^ 0xFFFF) != 0;

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
void** const kManagerVtable = reinterpret_cast<void**>(0x00658AD4);

} // namespace

// 0x005BE1F0
Manager::Manager()
{
    m_vtable = kManagerVtable;
    m_devices.data = reinterpret_cast<void*>(-1);
    m_devices.priority = 0;
    m_devices.next = &m_devices;
    m_devices.prev = &m_devices;
    m_devices.vtable = reinterpret_cast<void**>(0x00658A70);
    auto* task = static_cast<uint32_t*>(Mem::Calloc(0x20));
    if (task) {
        reinterpret_cast<Tsk::BaseTask*>(task)->Construct(0);                 // Tsk::BaseTask(priority 0)
        task[0] = 0x006476F0;                                             // Tsk::Task<Lst::Head<Device>>
        task[6] = reinterpret_cast<uint32_t>(&ProcessTask);
        task[7] = reinterpret_cast<uint32_t>(&m_devices);
    }
    m_task = task;
    int index = 0;
    for (int port = 0; port < 2; ++port) {
        void* memory = Mem::Calloc(sizeof(Device));
        Device* device = memory ? new (memory) Device(index, port, 0) : nullptr;
        if (!device)
            continue;
        Mem::ListNode* node = device->m_node; // onto the end of the list
        node->prev = m_devices.prev;
        node->next = &m_devices;
        m_devices.prev->next = node;
        m_devices.prev = node;
        device->Acquire();
        ++index;
    }
    if (!Pcm::NoMusicPlease()) // (the music system is set up with the controllers)
        Pcm::Init();
}

// 0x005BE300
void Manager::DestroyBody()
{
    m_vtable = kManagerVtable;
    Lst::Search<Device> search;
    for (Device* device = search.FirstItem(&m_devices); device;) {
        Device* next = search.NextItem();
        reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(device->m_vtable[0])(device, 1);
        device = next;
    }
    if (auto* task = static_cast<void***>(m_task))
        reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>((*task)[2])(task, 1);
    Stubs::ReturnZero(); // (a folded empty function - compiled-out code)
    if (!Pcm::NoMusicPlease())
        Pcm::StopUserSoundtrack();
    m_devices.UnlinkAs(reinterpret_cast<void**>(0x00658A70));
}

// 0x005BE6C0
Manager* Manager::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

namespace {
void ProcessAll(Mem::ListNode* devices)
{
    Lst::Search<Device> search;
    bool first = true;
    for (Device* device = search.FirstItem(devices); device; device = search.NextItem()) {
        device->Process(first);
        first = false;
    }
}
} // namespace

// 0x005BE140
void __cdecl ProcessTask(void* task) { ProcessAll(static_cast<Mem::ListNode*>(static_cast<void**>(task)[7])); }

// 0x005BE3F0
void Manager::ProcessDevices() { ProcessAll(&m_devices); }

// 0x005BE490
Device* Manager::GetDevice(int port, int slot)
{
    Lst::Search<Device> search;
    for (Device* device = search.FirstItem(&m_devices); device; device = search.NextItem()) {
        if (device->m_port == port && device->m_slot == slot)
            return device;
    }
    return nullptr;
}

// 0x005BE4F0
Device* Manager::GetDeviceByIndex(int index)
{
    Lst::Search<Device> search;
    for (Device* device = search.FirstItem(&m_devices); device; device = search.NextItem()) {
        if (device->m_index == index)
            return device;
    }
    return nullptr;
}

// 0x005BE550
void Manager::PauseDevices()
{
    Lst::Search<Device> search;
    for (Device* device = search.FirstItem(&m_devices); device; device = search.NextItem())
        device->PauseActuators();
}

// 0x005BE5E0
void Manager::UnpauseDevices()
{
    Lst::Search<Device> search;
    for (Device* device = search.FirstItem(&m_devices); device; device = search.NextItem())
        device->UnpauseActuators();
}

// 0x005BE670
Manager* CreateManager(bool create)
{
    if (!ManagerInstance() && create) {
        if (void* memory = Mem::Calloc(0x1C, nullptr, 1)) {
            ManagerInstance() = new (memory) Manager();
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
    if (Manager* manager = ManagerInstance()) {
        void** vtable = *reinterpret_cast<void***>(manager);
        reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(vtable[0])(manager, 1);
    }
    ManagerInstance() = nullptr;
    ManagerReferences() = 0;
}

} // namespace Sio

namespace {

struct DeviceAdapter {
    Sio::Device* Construct(int index, int port, int slot) { return new (this) Sio::Device(index, port, slot); }
    Sio::Manager* ConstructManager() { return new (this) Sio::Manager(); }
    Mem::ListNode* DestroyNode(unsigned int flags) { return Sio::DestroyDeviceNode(reinterpret_cast<Mem::ListNode*>(this), flags); }
};

} // namespace

REPLACE(0x005BD920, &DeviceAdapter::Construct);
REPLACE(0x005BCD00, &Sio::Device::Destroy);
REPLACE(0x005BDCC0, &Sio::Device::Process);
REPLACE(0x005BCC60, &Sio::Device::IsPluggedIn);
REPLACE(0x005BCC70, &Sio::Device::Acquire);
REPLACE(0x005BD8B0, &Sio::Device::Detect);
REPLACE(0x005BCCC0, &Sio::Device::Settle);
REPLACE(0x005BDA20, &Sio::Device::ActivateActuator);
REPLACE(0x005BD200, &Sio::Device::ReadData); // (not run with PartyMod) // (PartyMod's patch inside it is past our jump)
REPLACE(0x005BCCB0, &Sio::Device::EnableActuators);
REPLACE(0x005BDAD0, &Sio::Device::DisableActuators);
REPLACE(0x005BDB30, &Sio::Device::ResetActuators);
REPLACE(0x005BDB60, &Sio::Device::PauseActuators);
REPLACE(0x005BDBD0, &Sio::Device::UnpauseActuators);
REPLACE(0x005BDC60, &Sio::Device::StopActuators);
REPLACE(0x005BDD60, Sio::ReadKeyboard);
REPLACE(0x005BDD30, Sio::GetKeyboardChars);
REPLACE_SOURCE_ONLY(0x005BCC40, Sio::GetKeyboardChars); // a jump to 0x005BDD30
REPLACE(0x005BDE20, Sio::ClearKeyboardChars);
REPLACE_SOURCE_ONLY(0x005BCC50, Sio::ClearKeyboardChars); // a jump to 0x005BDE20
REPLACE(0x005BE670, Sio::CreateManager);
REPLACE(0x005BDCF0, Sio::ReleaseManager);
REPLACE(0x005BDE70, Sio::KeyInput);
REPLACE(0x005BDF30, Sio::SpecialKeyInput);
REPLACE(0x005BCD50, Sio::GetButtonKeyName);
REPLACE(0x005BE1F0, &DeviceAdapter::ConstructManager);
REPLACE(0x005BE300, &Sio::Manager::DestroyBody);
REPLACE(0x005BE6C0, &Sio::Manager::Destroy);
REPLACE(0x005BE140, Sio::ProcessTask);
REPLACE(0x005BE3F0, &Sio::Manager::ProcessDevices);
REPLACE(0x005BE490, &Sio::Manager::GetDevice);
REPLACE(0x005BE4F0, &Sio::Manager::GetDeviceByIndex);
REPLACE(0x005BE550, &Sio::Manager::PauseDevices);
REPLACE(0x005BE5E0, &Sio::Manager::UnpauseDevices);
REPLACE(0x005BE110, &DeviceAdapter::DestroyNode);
