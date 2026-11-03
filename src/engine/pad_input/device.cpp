// Serial I/O: the controller ports' devices, the keyboard's state and the device manager's lifetime.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "engine/pad_input/device.h"
#include <windows.h>
#include <dinput.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <span>

#include "engine/core/arena_alloc.h"
#include "engine/core/checksums.h"
#include "frontend/front_end.h"
#include "frontend/screen_element.h"
#include "game/skate/profile_manager.h"
#include "platform/platform.h"
#include "platform/platform_input.h"
#include "engine/core/mem.h"
#include "engine/core/stubs.h"
#include "engine/core/task_list.h"
#include "engine/lists/list_walk.h"
#include "engine/audio/music_player.h"
#include "engine/audio/music_api.h"
#include "engine/script/cscript.h"
#include "engine/pad_input/sdl_input.h"
#include "game/park_editing/park_editor.h"
#include "frontend/screen_element_manager.h"
#include <gsl/pointers>
#include <array>
#include "platform/compiler.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "engine/core/no_destroy.h"
#include "engine/gfx/engine_parts.h"
#include "engine/net/net_game_state.h"
#include "engine/objects/objects_shared.h"

namespace sio {

namespace {

// The pad read and the keyboard's constants.
constexpr int kAlignDataFill = 0xFF;
constexpr int kOctetAlignMask = 63;
constexpr unsigned kOctetAlignMaskUnsigned = 63U;
constexpr int kReconnectDelayMask = 63;
constexpr int kAnalogPadKind = 7;
constexpr int kFullRumble = 0xFF;
constexpr int kRumbleLevelShift = 8;
constexpr size_t kNameBytes = 256;
constexpr size_t kKeyboardCharCapacity = 16;
constexpr size_t kKeyboardStateBytes = 256;
constexpr int kKeyboardStateSize = 256;
constexpr int kTypedEnter = 0x19;
constexpr int kTypedBackspace = 0x18;
constexpr int kTypedEscape = 0x1A;
constexpr int kKeyRepeatFlag = 0x40000000; // the message's "key was already down" bit
constexpr int kKeyboardCharLast = 15;
constexpr int kNameLastIndex = 255;
// The pad buttons the key hints ask for (by number) and the DirectInput scan codes they show.
constexpr int kButtonStart = 8;
constexpr int kButtonSelect = 9;
constexpr int kButtonL2 = 14;
constexpr int kButtonR2 = 15;
constexpr int kButtonL1 = 16;
constexpr int kButtonR1 = 17;
constexpr int kButtonTwoKeys = 0x12;
constexpr int kButtonKeyPairZC = 0x13;
constexpr int kButtonKeyPairSX = 0x14;
constexpr int kButtonFocus = 21;
constexpr int kButtonAccept = 22;
constexpr int kButtonCancel = 23;
constexpr int kButtonKeyE = 24;
constexpr int kButtonKeyR = 25;
constexpr int kButtonKeyMinus = 26;
constexpr int kButtonKeyEquals = 27;
constexpr int kButtonKey1 = 28;
constexpr int kButtonKey2 = 29;
constexpr int kScanSpace = 0x39;
constexpr int kScanEnter = 0x1C;
constexpr int kScanE = 0x12;
constexpr int kScanR = 0x13;
constexpr int kScanMinus = 0x0C;
constexpr int kScanEquals = 0x0D;
// The characters the menu arrows and the chat's F1-F4 queue.
constexpr int kQueuedCursorLeft = 0x14;
constexpr int kQueuedCursorRight = 0x15;
constexpr int kQueuedCursorUp = 0x16;
constexpr int kQueuedCursorDown = 0x17;
constexpr int kQueuedChatF1 = 0x1B;
constexpr int kQueuedChatF2 = 0x1C;
constexpr int kQueuedChatF3 = 0x1D;
constexpr int kQueuedChatF4 = 0x1E;
// The snapshot's button bits and the control data's bits they become.
constexpr int kStickScale = 256;
constexpr int kAllBitsUp = 0xFF;
constexpr int kLowByteMask = 0xFF;
constexpr int kHighBitStart = 8;
constexpr int kSnapshotSelectShift = 5;
constexpr int kHighSelectSticksMask = 7;
constexpr int kSnapshotDpadRight = 8;
constexpr int kSnapshotButtonBits = 0xFE;
constexpr int kSnapshotStart = 0x10;
constexpr int kStickOffset = 0x7FFF; // a signed stick plus this is 0..65535
constexpr int kByteBits = 8;
constexpr int kAllButtonBits = 0xFFFF;
constexpr size_t kTypedBufferBytes = 256;
constexpr int kParkEditorTriggerElement = 0x31631B98; // the park editor's trigger-mapping element (text unknown)

} // namespace

namespace {

// The platform layer: a port's DirectInput joystick read for this frame (none: null), and whether the port has
// any keyboard bindings (the keyboard counts as a pad then).
bool poll_joystick(int port) { return os_layer::read_joystick(port) != nullptr; }
bool has_key_bindings(int port) { return os_layer::has_key_bindings(port); }
bool pad_present(int port) { return poll_joystick(port) || has_key_bindings(port); }

} // namespace

void set_actuators(int port, int left, int right) { os_layer::set_actuators(port, left, right); }

// 0x005BD920
InputPort::InputPort(Place place)
    : list_link_(memory::ListNode::create(this)), reconnect_delay_(std::rand() & kReconnectDelayMask)
{
    const int index = place.index;
    const int port = place.port;
    const int slot = place.slot;
    port_no_ = port;
    ordinal_ = index;
    seat_ = slot;
    std::memset(rumble_raw_.data(), 0, sizeof(rumble_raw_));
    std::memset(align_data_.data(), kAlignDataFill, sizeof(align_data_));
    core::at(align_data_, 0) = 0;
    core::at(align_data_, 1) = 1;
    std::memset(control_data_.data(), 0, sizeof(control_data_));
    octets_ = (buffer_raw_ + kOctetAlignMask) & ~kOctetAlignMaskUnsigned; // (buffer_raw_ is never set: as in the original)
}

// 0x005BCD00
InputPort::~InputPort()
{
    memory::ListNode::destroy(list_link_); // (0x005BE110)
}

// 0x005BDCC0. The original steps through the states:
//     is_connected_ = 0;
//     switch (cur_phase_) { case InputPort::State::kDetect: Detect(); break; case InputPort::State::kReady: pull_data(0); break;
//                        case InputPort::State::kSettle: Settle(); break; }
// where pull_data polls the pad into control_data_. Originally from PartyMod Fixes (its jump from here): SDL game controllers
// and the keyboard (engine/pad_input/sdl_input.h).
void InputPort::work_on(int /*unused*/) { process_sdl_device(this); }

// 0x005BCC70
void InputPort::acquire()
{
    if (cur_phase_ == InputPort::State::kIdle && pad_present(port_no_)) {
        is_usable_ = 0;
        cur_phase_ = InputPort::State::kDetect;
    }
}

// 0x005BD8B0
void InputPort::detect()
{
    if (!pad_present(port_no_)) {
        return;
    }
    disconnected_ticks_ = 0;
    kind_tag_ = kAnalogPadKind;
    capabilities_ |= kAnalog;
    core::at(rumble_cap_, 0) = kFullRumble;
    core::at(rumble_cap_, 1) = kFullRumble;
    rumble_motor_count_ = 2;
    capabilities_ |= kHasActuators;
    cur_phase_ = InputPort::State::kSettle;
    next_state_ = InputPort::State::kReady;
}

// 0x005BCCC0 (the pad is polled, the result unused)
void InputPort::get_settle()
{
    pad_present(port_no_);
    cur_phase_ = next_state_;
    next_state_ = InputPort::State::kIdle;
}

// 0x005BDA20. The original first reads the pad (pull_data(0)); PartyMod removes that call (its Process does
// the reading).
void InputPort::fire_actuator(int actuator, int percent)
{
    if (rumble_off_) {
        return;
    }
    if (cur_phase_ != InputPort::State::kReady || !(capabilities_ & kHasActuators)) {
        return;
    }
    if (actuator < 0 || actuator >= rumble_motor_count_) {
        return;
    }
    // (x87: max * percent * 0.01f, truncated for the level; the same value * 256 for the motor)
    const double level = static_cast<double>(core::at(rumble_cap_, actuator)) * percent * static_cast<double>(0.01F);
    core::at(rumble_raw_, actuator) = static_cast<uint8_t>(static_cast<int>(level));
    const int motor = static_cast<int>(level * 256.0) & 0xFFFF;
    if (actuator == 0) {
        set_actuators(port_no_, motor, core::at(rumble_raw_, 1) << kRumbleLevelShift);
    } else {
        set_actuators(port_no_, core::at(rumble_raw_, 0) << kRumbleLevelShift, motor);
    }
}

// 0x005BDAD0
void InputPort::stop_rumble()
{
    if (rumble_off_) {
        return;
    }
    if (cur_phase_ == InputPort::State::kReady && (capabilities_ & kHasActuators)) {
        for (int i = 0; i < rumble_motor_count_; ++i) {
            core::at(rumble_raw_, i) = 0;
        }
        set_actuators(port_no_, 0, 0);
    }
    rumble_off_ = 1;
}

// 0x005BDB30. Either way the motors stop; the flag ends as it was (the first branch clears it so that
// stop_rumble acts, which sets it again).
void InputPort::clear_rumble()
{
    if (rumble_off_) {
        rumble_off_ = 0;
        stop_rumble();
        return;
    }
    stop_rumble();
    rumble_off_ = 0;
}

// 0x005BDB60
void InputPort::pause_actuators()
{
    if (pause_count_ == 0 && cur_phase_ == InputPort::State::kReady && (capabilities_ & kHasActuators)) {
        for (int i = 0; i < rumble_motor_count_; ++i) {
            core::at(actuator_saved_, i) = core::at(rumble_raw_, i);
            core::at(rumble_raw_, i) = 0;
        }
        set_actuators(port_no_, 0, 0);
    }
    ++pause_count_;
}

// 0x005BDBD0
void InputPort::unpause_actuators()
{
    if (pause_count_ == 0) {
        return;
    }
    --pause_count_;
    if (pause_count_ != 0) {
        return;
    }
    if (cur_phase_ == InputPort::State::kReady && (capabilities_ & kHasActuators)) {
        for (int i = 0; i < rumble_motor_count_; ++i) {
            core::at(rumble_raw_, i) = core::at(actuator_saved_, i);
        }
        set_actuators(port_no_, core::at(rumble_raw_, 0) << kRumbleLevelShift, core::at(rumble_raw_, 1) << kRumbleLevelShift);
    }
}

// 0x005BDC60
void InputPort::stop_actuators()
{
    if (cur_phase_ == InputPort::State::kReady && (capabilities_ & kHasActuators)) {
        for (int i = 0; i < rumble_motor_count_; ++i) {
            core::at(rumble_raw_, i) = 0;
            core::at(actuator_saved_, i) = 0;
        }
        set_actuators(port_no_, 0, 0);
    }
}

// --- the keyboard -------------------------------------------------------------------------------------------

namespace {

// NOT ORIGINAL grouping - the keyboard's buffers, characters and key names, the device manager's references. Made on
// first use, never destroyed (keyboard_device_state()).
struct KeyboardState {
    int keyboard_buffer_{2};
    uint32_t manager_references_{};
    uint32_t keyboard_char_count_{};
    std::array<char, kNameBytes> key_name_result_{};
    std::array<char, kNameBytes> name_buffer_{};
    std::array<int, kKeyboardCharCapacity> keyboard_chars_{};
    std::array<std::array<uint8_t, kKeyboardStateBytes>, 2> keyboard_state_{};
};

KeyboardState& keyboard_device_state()
{
    static core::NoDestroy<KeyboardState> state;
    return state.get();
}

// Which of the two state buffers is current (2 at start: clear both first).
int& keyboard_buffer() { return keyboard_device_state().keyboard_buffer_; }

std::array<std::array<uint8_t, kKeyboardStateBytes>, 2>& keyboard_state() { return keyboard_device_state().keyboard_state_; }
uint32_t& keyboard_char_count() { return keyboard_device_state().keyboard_char_count_; }
std::array<int, kKeyboardCharCapacity>& keyboard_chars() { return keyboard_device_state().keyboard_chars_; }
} // namespace

// 0x005BDD60
void read_keyboard()
{
    keyboard_char_count() = 0;
    if (keyboard_buffer() == 2) {
        std::memset(keyboard_state().data(), 0, sizeof(keyboard_state()));
        keyboard_buffer() = 1;
    }
    IDirectInputDevice8A* keyboard = os_layer::keyboard_device();
    if (!keyboard) {
        return;
    }
    keyboard_buffer() = keyboard_buffer() == 0 ? 1 : 0;
    const HRESULT result = keyboard->GetDeviceState(256, core::at(keyboard_state(), keyboard_buffer()).data());
    if (result != DIERR_INPUTLOST && result != DIERR_NOTACQUIRED) {
        return;
    }
    if (SUCCEEDED(os_layer::keyboard_device()->Acquire())) {
        os_layer::keyboard_device()->GetDeviceState(kKeyboardStateSize, core::at(keyboard_state(), keyboard_buffer()).data());
    } else {
        std::memset(core::at(keyboard_state(), keyboard_buffer()).data(), 0, kKeyboardStateSize);
    }
}

// 0x005BDD30
int get_keyboard_chars(std::span<char> out)
{
    uint32_t i = 0;
    for (; i < keyboard_char_count(); ++i) {
        core::at(out, i) = static_cast<char>(core::at(keyboard_chars(), i));
    }
    return static_cast<int>(keyboard_char_count());
}

// 0x005BDE20
void clear_keyboard_chars() { keyboard_char_count() = 0; }

// --- the manager's lifetime --------------------------------------------------------------------------------

namespace {

// NOT ORIGINAL grouping - the input devices' manager. Made on first use, never destroyed (device_manager_state()).
struct DeviceManagerState {
    std::unique_ptr<Hub> manager_;
};

DeviceManagerState& device_manager_state()
{
    static core::NoDestroy<DeviceManagerState> state;
    return state.get();
}
} // namespace

Hub* get_device_manager() { return device_manager_state().manager_.get(); }

std::unique_ptr<Hub>& manager_instance() { return device_manager_state().manager_; }

namespace {
uint32_t& manager_references() { return keyboard_device_state().manager_references_; }
} // namespace

// --- typed keys -------------------------------------------------------------------------------------------

namespace {
// Whether typed keys are wanted: the keyboard is being typed on (0x007CE46E), or text entry is on
// (0x007CCDF8) outside a menu (0x007CE46F). PartyMod sets the last two for its text input.
bool& keyboard_typing() { return keyboard_on_screen_flag(); }
bool text_entry() { return online::net_game_state().text_entry(); }
bool& in_menu() { return menu_flag(); }
} // namespace

// 0x005BDE70
void key_input(KeyEvent key_event)
{
    auto key = key_event.key;
    const auto flags = key_event.flags;
    if (!keyboard_typing() && (!text_entry() || in_menu())) {
        return;
    }
    if (flags < 0) {
        return;
    }
    bool queue = true;
    switch (key) {
    case '\r':
        key = kTypedEnter;
        break; // Enter
    case '\b':
        key = kTypedBackspace;
        break; // Backspace
    case VK_ESCAPE: // Escape (not its auto-repeats)
        if (flags & kKeyRepeatFlag) {
            queue = false;
        } else {
            key = kTypedEscape;
        }
        break;
    case ';':
        key = ':';
        break;
    // (not typeable in the game's fonts)
    case -1:
    case '%':
    case '~':
    case '\\':
    case '|':
    case '[':
    case ']':
    case '}':
    case '"':
        queue = false;
        break;
    default:
        break;
    }
    if (queue) {
        core::at(keyboard_chars(), keyboard_char_count()++) = key;
    }
    if (keyboard_char_count() == kKeyboardCharCapacity) {
        keyboard_char_count() = kKeyboardCharLast; // (full: the last one is overwritten)
    }
}

// --- special keys and key names ---------------------------------------------------------------------------

namespace {
// The other "in a menu" flag (PartyMod's names) and the quit request.

// Port 0's key bindings (DirectInput key codes; -1 = none).
int key_binding(int action) { return core::at(os_layer::get_controls().front().keys, action); }
// The platform layer's key names.
const wchar_t* key_name_w(int code) { return os_layer::get_key_name(code); }
std::array<char, kNameBytes>& name_buffer() { return keyboard_device_state().name_buffer_; }
std::array<char, kNameBytes>& key_name_result() { return keyboard_device_state().key_name_result_; }

void convert_key_name(int code)
{
    THUG2_MSVC_WARNING(suppress : 4996) // (as the original: the C runtime's conversion, at most 256 bytes)
    std::wcstombs(name_buffer().data(), key_name_w(code), kNameBytes);
    core::at(name_buffer(), kNameLastIndex) = '\0';
}

// The character a key (by scan code) types, for the menu hints.
char key_char(UINT scan_code)
{
    return static_cast<char>(MapVirtualKeyA(MapVirtualKeyA(scan_code, MAPVK_VSC_TO_VK), MAPVK_VK_TO_CHAR));
}

// The key code shown for a button: in a menu (outside the park editor's piece menu, not typing)...
int get_menu_key(int button)
{
    switch (button) {
    case 0:
        return key_binding(os_layer::kBindGrind);
    case 1:
        return key_binding(os_layer::kBindKick);
    case 2:
        return key_binding(os_layer::kBindGrab);
    case 3:
        return key_binding(os_layer::kBindOllie);
    case kButtonStart:
        return 1; // Escape
    case kButtonSelect:
        return kScanSpace; // space
    case kButtonL2:
        return key_binding(os_layer::kBindNollie);
    case kButtonR2:
        return key_binding(os_layer::kBindSwitchRevert);
    case kButtonL1:
        return key_binding(os_layer::kBindLeftSpin);
    case kButtonR1:
        return key_binding(os_layer::kBindRightSpin);
    case kButtonFocus:
        return key_binding(os_layer::kBindFocus);
    case kButtonAccept:
        return kScanEnter; // Enter
    case kButtonCancel:
        return 1;
    case kButtonKeyE:
        return kScanE; // E
    case kButtonKeyR:
        return kScanR; // R
    case kButtonKeyMinus:
        return kScanMinus; // -
    case kButtonKeyEquals:
        return kScanEquals; // =
    case kButtonKey1:
        return 2; // 1
    case kButtonKey2:
        return 3; // 2
    default:
        return 0;
    }
}
// ...while typing...
int typing_key(int button)
{
    switch (button) {
    case 0:
    case 1:
    case 2:
    case 3:
    case kButtonStart:
    case kButtonSelect:
    case kButtonL2:
    case kButtonR2:
    case kButtonL1:
    case kButtonR1:
        return -1;
    case kButtonAccept:
        return kScanEnter;
    case kButtonCancel:
        return 1;
    default:
        return 0;
    }
}
// ...and otherwise.
int play_key(int button)
{
    switch (button) {
    case 0:
        return key_binding(os_layer::kBindGrind);
    case 1:
        return key_binding(os_layer::kBindKick);
    case 2:
        return key_binding(os_layer::kBindGrab);
    case 3:
        return key_binding(os_layer::kBindOllie);
    case kButtonStart:
        return 1;
    case kButtonSelect:
        return key_binding(os_layer::kBindCameraToggle);
    case kButtonL2:
        return key_binding(os_layer::kBindNollie);
    case kButtonR2:
        return key_binding(os_layer::kBindSwitchRevert);
    case kButtonL1:
        return key_binding(os_layer::kBindLeftSpin);
    case kButtonR1:
        return key_binding(os_layer::kBindRightSpin);
    case kButtonFocus:
        return key_binding(os_layer::kBindFocus);
    default:
        return 0;
    }
}
} // namespace

// 0x005BCD50
const char* get_button_key_name(int button)
{
    const bool menu = modules::menus_active() || in_menu();
    const bool typing = keyboard_typing();
    bool piece_menu = false;
    frontend::ElementPtr element;
    frontend::fetch_part(&element, checksums::hash_from_text("select_skater_hmenu"));
    if (element) {
        (void)element->is_concealed(); // (the result isn't used)
    }
    {
        frontend::ElementPtr found;
        frontend::fetch_part(&found, checksums::hash_from_text("piece_menu_hmenu"));
        element = found;
    }
    if (!render::engine_parts().quit_requested() && element && !element->is_concealed()) {
        piece_menu = true;
    }
    {
        frontend::ElementPtr found;
        frontend::fetch_part(&found, checksums::hash_from_text("select_skater_hmenu"));
        element = found;
        found.get_release();
    }
    if (element) {
        (void)element->is_concealed(); // (the result isn't used)
    }

    const bool menu_keys = !piece_menu && menu && !typing;
    const int code = menu_keys ? get_menu_key(button) : typing ? typing_key(button) : play_key(button);
    convert_key_name(code);
    text::format_to(key_name_result(), "%s", name_buffer().data());
    if (button == kButtonTwoKeys) {
        // two keys: "first+second"
        convert_key_name(key_binding(os_layer::kBindNollie));
        text::format_to(key_name_result(), "%s+", name_buffer().data());
        convert_key_name(key_binding(os_layer::kBindSwitchRevert));
        const size_t used = std::strlen(key_name_result().data()); // (the original's strcat is unbounded)
        size_t length = std::strlen(name_buffer().data());
        length = std::min(length, sizeof(key_name_result()) - 1 - used);
        std::memcpy(std::span<char>(key_name_result()).subspan(used).data(), name_buffer().data(), length);
        core::at(key_name_result(), used + length) = '\0';
    }
    if (menu_keys && (button == kButtonKeyPairSX || button == kButtonKeyPairZC)) {
        // the two keys' characters: "S/X" or "Z/C" on a QWERTY keyboard
        const char first = key_char(button == 0x14 ? 0x1F : 0x2C);
        const char second = key_char(button == 0x14 ? 0x2D : 0x2E);
        core::at(key_name_result(), 0) = first;
        core::at(key_name_result(), 1) = '/';
        core::at(key_name_result(), 2) = second;
        core::at(key_name_result(), 3) = '\0';
    }
    return key_name_result().data();
}

// 0x005BDF30
void special_key_input(KeyEvent key_event)
{
    auto key = key_event.key;
    const auto flags = key_event.flags;
    frontend::ElementPtr element;
    frontend::fetch_part(&element, checksums::hash_from_text("menu_network_select_refresh"));
    bool server_list = false;
    if (element && !element->is_concealed()) {
        server_list = true;
    } else if (!keyboard_typing() && (!text_entry() || in_menu())) {
        return;
    }
    if (flags < 0) {
        return;
    }
    if (server_list && key == VK_F5) {
        script::launch_script("refresh_chosen", nullptr, nullptr, false);
        return;
    }
    int queued = 0;
    if (in_menu()) {
        switch (key) {
        case VK_DOWN:
            queued = kQueuedCursorDown;
            break;
        case VK_UP:
            queued = kQueuedCursorUp;
            break;
        case VK_LEFT:
            queued = kQueuedCursorLeft;
            break;
        case VK_RIGHT:
            queued = kQueuedCursorRight;
            break;
        default:
            break;
        }
    } else {
        switch (key) {
        case VK_F1:
            queued = kQueuedChatF1;
            break;
        case VK_F2:
            queued = kQueuedChatF2;
            break;
        case VK_F3:
            queued = kQueuedChatF3;
            break;
        case VK_F4:
            queued = kQueuedChatF4;
            break;
        default:
            break;
        }
    }
    if (queued) {
        core::at(keyboard_chars(), keyboard_char_count()++) = queued;
    }
    if (keyboard_char_count() == kKeyboardCharCapacity) {
        keyboard_char_count() = kKeyboardCharLast;
    }
}

// --- the original pad read --------------------------------------------------------------------------------

namespace {
// Flags the read consults (meaning partly unknown): two that turn the menu handling off; and the park editor
// (0x007CE5D8), editing when its state is "edit" (0x7ECA21E5).

// The platform layer: the pad mode and the default keys' Enter / Escape (os_layer::set_pad_mode...), its window
// message pump, the pad read and whether a DirectInput key is down.
void set_pad_mode(int mode) { os_layer::set_pad_mode(mode); }
void set_default_enter_control(int control) { os_layer::set_default_enter_control(control); }
void set_default_escape_control(int control) { os_layer::set_default_escape_control(control); }
void pump_window_messages() { os_layer::process_window_events(); }
void read_pad_state(int port, PadSnapshot* state) { os_layer::read_pad(port + 1, state); }
bool get_key_down(int code) { return os_layer::get_key_down(code); }
// The port the front end is taking input from (a getter, 0x00553E90, on the object at 0x007CE4B4).
int get_menu_port() { return objects::profile_roster()->cursor_pos(); }

uint8_t if_above(uint8_t value, uint8_t threshold, uint8_t bit) { return value > threshold ? bit : 0; }
uint8_t pressure_or_zero(uint8_t value) { return value > kPressedThreshold ? value : 0; }
uint8_t all_or_nothing(uint32_t value) { return value ? kFullPressure : 0; }
// A stick from -32768..32767 to 0..255 (signed division, rounding toward zero).
uint8_t get_stick(int value) { return static_cast<uint8_t>(value / kStickScale); }
} // namespace

// 0x005BD200
void InputPort::pull_data(int /*unused*/)
{
    if (render::engine_parts().quit_requested()) {
        return;
    }
    const bool menu = modules::menus_active() || in_menu();
    const bool typing = keyboard_typing();
    bool select_skater_shown = false;
    bool piece_menu_shown = false;
    bool playing = false;

    // Which menus are up (the skater select's horizontal menu is checked before and after the park editor's).
    frontend::ElementPtr element;
    frontend::fetch_part(&element, checksums::hash_from_text("select_skater_hmenu"));
    if (element && !element->is_concealed()) {
        select_skater_shown = true;
    }
    {
        frontend::ElementPtr found;
        frontend::fetch_part(&found, checksums::hash_from_text("piece_menu_hmenu"));
        element = found;
    }
    if (!render::engine_parts().quit_requested() && element && !element->is_concealed()) {
        piece_menu_shown = true;
    }
    {
        frontend::ElementPtr found;
        frontend::fetch_part(&found, checksums::hash_from_text("select_skater_hmenu"));
        element = found;
    }
    if (element && !element->is_concealed()) {
        select_skater_shown = true;
    }
    bool menu_mode = !piece_menu_shown && menu && !typing;
    if (render::engine_parts().movie_playing() || objects::objects_shared().cutscene_playing()) {
        piece_menu_shown = false;
        menu_mode = false;
    }

    // In menus only the port the front end listens to (or port 0, or both on the skater select) reads.
    const int menu_port = get_menu_port();
    set_default_enter_control(0);  // (the pressure button 0)
    set_default_escape_control(1); // (the pressure button 1)
    if (typing) {
        set_pad_mode(2);
    } else if (menu_mode) {
        if ((!select_skater_shown || menu_port == port_no_) && port_no_ == 0) {
            set_pad_mode(1);
        } else if (!select_skater_shown) {
            set_pad_mode(2);
        } else if (menu_port == port_no_ && port_no_ == 1) {
            set_pad_mode(3);
        } else {
            is_connected_ = true;
            element.get_release();
            return;
        }
    } else if (piece_menu_shown) {
        if (port_no_ != 0) {
            is_connected_ = true;
            element.get_release();
            return;
        }
        set_pad_mode(0);
    } else {
        set_pad_mode(0);
        playing = true;
    }
    pump_window_messages();
    is_connected_ = false;
    if (!pad_present(port_no_)) {
        return;
    }

    PadSnapshot pad;
    read_pad_state(port_no_, &pad);
    const auto& p = pad.pressure;
    const uint16_t buttons = pad.buttons;
    const bool keyboard_menu = keyboard_control_ && menu_mode; // (the keyboard drives the menus: some buttons ignored)
    is_usable_ = 1;
    is_connected_ = true;
    // Two bytes of button bits, each set while the button is up.
    core::at(control_data_, 0) = 0;
    core::at(control_data_, 1) = kAnalogModeByte;
    uint8_t first = keyboard_menu ? kAllBitsUp : static_cast<uint8_t>(~(((buttons & kLowByteMask) >> 1) & kHighBitStart));
    first ^= (buttons >> kSnapshotSelectShift) & kHighSelectSticksMask;
    core::at(control_data_, kControlButtonsHigh) = first;
    uint8_t second = static_cast<uint8_t>(~(core::at(p, kSnapshotB) >= kPressedThreshold ? kLowBitCircle : 0)) ^ (core::at(p, kSnapshotY) >= kPressedThreshold ? kLowBitTriangle : 0);
    if (!keyboard_menu) {
        second ^= core::at(p, kSnapshotA) >= kPressedThreshold ? kLowBitCross : 0;
    }
    second ^= core::at(p, kSnapshotX) >= kPressedThreshold ? kLowBitSquare : 0;
    const uint8_t buttons_so_far = second;
    const park_editing::ParkBuilderMode* editor = park_editing::ParkBuilderMode::sole();
    if (editor && editor->editor_state() == park_editing::ParkBuilderMode::kStateEdit) {
        // The park editor: the triggers are mapped differently while its element 0x31631B98 is up.
        bool editor_element = false;
        if (frontend::UiTree::sole()) {
            frontend::ElementPtr found;
            frontend::fetch_part(&found, kParkEditorTriggerElement);
            editor_element = found.get_pull() != nullptr;
            found.get_release();
        }
        second ^= if_above(core::at(p, kSnapshotRightTrigger), kPressedThreshold, editor_element ? 2 : 4);
        second ^= if_above(core::at(p, kSnapshotLeftTrigger), kPressedThreshold, 1);
    } else {
        second = buttons_so_far ^ if_above(core::at(p, kSnapshotLeftTrigger), kHardPressThreshold, 1) ^ if_above(core::at(p, kSnapshotRightTrigger), kHardPressThreshold, 2) ^ if_above(core::at(p, kSnapshotLeftTrigger), kPressedThreshold, 4) ^
                 if_above(core::at(p, kSnapshotRightTrigger), kPressedThreshold, kLowBitR1);
    }
    core::at(control_data_, kControlButtonsLow) = second;
    // Pressures (dead zone 0x20).
    core::at(control_data_, kPressureTriangle) = pressure_or_zero(core::at(p, kSnapshotY));
    core::at(control_data_, kPressureCircle) = pressure_or_zero(core::at(p, kSnapshotB));
    core::at(control_data_, kPressureCross) = keyboard_menu ? 0 : pressure_or_zero(core::at(p, kSnapshotA));
    core::at(control_data_, kPressureSquare) = pressure_or_zero(core::at(p, kSnapshotX));
    core::at(control_data_, kPressureL1) = pressure_or_zero(core::at(p, kSnapshotLeftTrigger));
    core::at(control_data_, kPressureR1) = pressure_or_zero(core::at(p, kSnapshotRightTrigger));
    core::at(control_data_, kPressureL2) = 0;
    core::at(control_data_, kPressureR2) = 0;
    core::at(control_data_, kControlExtraButtons) = 0;
    // The d-pad: pressures, and its bits in the first byte.
    core::at(control_data_, kPressureUp) = all_or_nothing(buttons & 1);
    core::at(control_data_, kPressureRight) = all_or_nothing(buttons & kSnapshotDpadRight);
    core::at(control_data_, kPressureDown) = all_or_nothing(buttons & 2);
    auto low = static_cast<uint8_t>(buttons);
    auto dpad = static_cast<uint8_t>((low & kSnapshotButtonBits) << 1);
    dpad = static_cast<uint8_t>((dpad ^ (low & 1)) << 2);
    dpad = static_cast<uint8_t>((dpad ^ (low & kSnapshotDpadRight)) << 2);
    core::at(control_data_, kControlButtonsHigh) ^= dpad;
    core::at(control_data_, kPressureLeft) = all_or_nothing(buttons & 4);
    // The sticks (y inverted).
    core::at(control_data_, kControlRightStickX) = get_stick(core::at(pad.sticks, 2) + kStickOffset);
    core::at(control_data_, kControlRightStickY) = get_stick(kStickOffset - core::at(pad.sticks, 3));
    core::at(control_data_, kControlLeftStickX) = get_stick(core::at(pad.sticks, 0) + kStickOffset);
    core::at(control_data_, kControlLeftStickY) = get_stick(kStickOffset - core::at(pad.sticks, 1));
    if (core::at(p, kSnapshotBlack) > kPressedThreshold) {
        core::at(control_data_, kControlExtraButtons) = 1;
    }
    if (core::at(p, kSnapshotWhite) > kPressedThreshold) {
        core::at(control_data_, kControlExtraButtons) |= 2;
    }
    held_down_ = ((core::at(control_data_, kControlButtonsHigh) << kByteBits | core::at(control_data_, kControlButtonsLow)) ^ kAllButtonBits) != 0;

    // The keyboard takes over the menus after start, a pressed trigger, a typed Enter, or (playing) Enter.
    bool enter_typed = false;
    if (typing) {
        std::array<char, kTypedBufferBytes> typed{};
        const int count = get_keyboard_chars(typed);
        for (int i = 0; i < count; ++i) {
            if (core::at(typed, i) == kTypedEnter) {
                enter_typed = true;
                break;
            }
        }
    }
    keyboard_control_ = (buttons & kSnapshotStart) || core::at(p, kSnapshotA) >= kPressedThreshold || enter_typed || (playing && get_key_down(kScanEnter));
}

// --- the manager -------------------------------------------------------------------------------------------

// 0x005BE1F0
Hub::Hub()
{
    devs_.make_head();
    task_ = std::make_unique<tasks::Task<memory::ListNode>>(&process_task, &devs_, 0);
    int index = 0;
    for (int port = 0; port < 2; ++port) {
        auto* device = memory::make_owned<InputPort>(InputPort::Place{.index = index, .port = port, .slot = 0});
        if (!device) {
            continue;
        }
        memory::ListNode* node = device->list_link(); // onto the end of the list
        node->set_prev(devs_.prev());
        node->set_next(&devs_);
        devs_.prev()->set_next(node);
        devs_.set_prev(node);
        device->acquire();
        ++index;
    }
    if (!audio::music_off_requested()) { // (the music system is set up with the controllers)
        audio::prepare_all();
    }
}

// 0x005BE300
Hub::~Hub()
{
    lists::Hunt<InputPort> search;
    for (const InputPort* device = search.head_element(&devs_); device;) {
        const InputPort* next = search.following_item();
        gsl::owner<const InputPort*> owned = memory::owned_element(device); // (the list's)
        delete owned;
        device = next;
    }
    task_.reset();
    stubs::return_zero(); // (a folded empty function - compiled-out code)
    if (!audio::music_off_requested()) {
        audio::stop_user_soundtrack();
    }
    devs_.uncouple();
}

namespace {
void process_all(memory::ListNode* devices)
{
    lists::Hunt<InputPort> search;
    bool first = true;
    for (InputPort* device = search.head_element(devices); device; device = search.following_item()) {
        device->work_on(first);
        first = false;
    }
}
} // namespace

// 0x005BE140
void process_task(tasks::Task<memory::ListNode>* task) { process_all(task->content()); }

// 0x005BE3F0
void Hub::poll_pads() { process_all(&devs_); }

// 0x005BE490
InputPort* Hub::fetch_gadget(int port, int slot)
{
    lists::Hunt<InputPort> search;
    for (InputPort* device = search.head_element(&devs_); device; device = search.following_item()) {
        if (device->port_no() == port && device->seat() == slot) {
            return device;
        }
    }
    return nullptr;
}

// 0x005BE4F0
InputPort* Hub::get_pad_at_slot(int index)
{
    lists::Hunt<InputPort> search;
    for (InputPort* device = search.head_element(&devs_); device; device = search.following_item()) {
        if (device->ordinal() == index) {
            return device;
        }
    }
    return nullptr;
}

// 0x005BE550
void Hub::pause_devices()
{
    lists::Hunt<InputPort> search;
    for (InputPort* device = search.head_element(&devs_); device; device = search.following_item()) {
        device->pause_actuators();
    }
}

// 0x005BE5E0
void Hub::unpause_devices()
{
    lists::Hunt<InputPort> search;
    for (InputPort* device = search.head_element(&devs_); device; device = search.following_item()) {
        device->unpause_actuators();
    }
}

// 0x005BE670
Hub* create_manager(bool create)
{
    if (!manager_instance() && create) {
        manager_instance() = std::make_unique<Hub>();
    }
    ++manager_references();
    return manager_instance().get();
}

// 0x005BDCF0
void release_manager()
{
    if (manager_references() > 1) {
        --manager_references();
        return;
    }
    memory::destroy_then_clear(manager_instance());
    manager_references() = 0;
}

} // namespace sio
