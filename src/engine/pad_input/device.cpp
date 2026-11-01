// Serial I/O: the controller ports' devices, the keyboard's state and the device manager's lifetime.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "engine/pad_input/device.h"
#include "game/goals/objective.h"
#include "engine/net/buddy_status.h"

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): a library's configuration / the compiler-specific macro
#define DIRECTINPUT_VERSION 0x0800
#include <memory>
#include <windows.h>
#include <dinput.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "engine/core/arena_alloc.h"
#include "engine/core/checksums.h"
#include "frontend/front_end.h"
#include "frontend/screen_element.h"
#include "engine/net/gamenet_game_cfuncs.h"
#include "game/skate/profile_manager.h"
#include "platform/platform.h"
#include "platform/platform_input.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/stubs.h"
#include "engine/core/task_list.h"
#include "engine/lists/list_walk.h"
#include "engine/audio/music_player.h"
#include "engine/audio/music_api.h"
#include "engine/script/cscript.h"
#include "engine/pad_input/sdl_input.h"
#include "engine/core/code_address.h"
#include "engine/gfx/movie.h"
#include "engine/objects/cutscene.h"
#include "game/park_editing/park_editor.h"
#include "frontend/screen_element_manager.h"
#include "engine/core/owner.h"
#include "platform/compiler.h"

namespace sio {

namespace {

// The platform layer: a port's DirectInput joystick read for this frame (none: null), and whether the port has
// any keyboard bindings (the keyboard counts as a pad then).
bool poll_joystick(int port) { return os_layer::read_joystick(port) != nullptr; }
bool has_key_bindings(int port) { return os_layer::has_key_bindings(port); }
bool pad_present(int port) { return poll_joystick(port) || has_key_bindings(port); }

} // namespace

void set_actuators(int port, int left, int right) { os_layer::set_actuators(port, left, right); }

// 0x005BD920
InputPort::InputPort(int index, int port, int slot)
{
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): set twice, as the original does
    capabilities_ = 0;
    list_link_ = memory::ListNode::create(this);
    port_no_ = port;
    ordinal_ = index;
    seat_ = slot;
    cur_phase_ = kIdle;
    next_state_ = kIdle;
    capabilities_ = 0;
    rumble_motor_count_ = 0;
    is_usable_ = 0;
    std::memset(rumble_raw_, 0, sizeof(rumble_raw_));
    std::memset(align_data_, 0xFF, sizeof(align_data_));
    align_data_[0] = 0;
    align_data_[1] = 1;
    std::memset(control_data_, 0, sizeof(control_data_));
    octets_ = (buffer_raw_ + 63) & ~63u; // (buffer_raw_ is never set: as in the original)
    // NOLINTNEXTLINE(misc-predictable-rand): the game's own rand(), as the original
    reconnect_delay_ = std::rand() & 63;
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
}

// 0x005BCD00
InputPort::~InputPort()
{
    memory::ListNode::destroy(list_link_); // (0x005BE110)
}

// 0x005BDCC0. The original steps through the states:
//     is_connected_ = 0;
//     switch (cur_phase_) { case kDetect: Detect(); break; case kReady: pull_data(0); break;
//                        case kSettle: Settle(); break; }
// where pull_data polls the pad into control_data_. Originally from PartyMod Fixes (its jump from here): SDL game controllers
// and the keyboard (engine/pad_input/sdl_input.h).
void InputPort::work_on(int /*unused*/) { process_sdl_device(this); }

// 0x005BCC70
void InputPort::acquire()
{
    if (cur_phase_ == kIdle && pad_present(port_no_)) {
        is_usable_ = 0;
        cur_phase_ = kDetect;
    }
}

// 0x005BD8B0
void InputPort::detect()
{
    if (!pad_present(port_no_))
        return;
    disconnected_ticks_ = 0;
    kind_tag_ = 7;
    capabilities_ |= kAnalog;
    rumble_cap_[0] = 0xFF;
    rumble_cap_[1] = 0xFF;
    rumble_motor_count_ = 2;
    capabilities_ |= kHasActuators;
    cur_phase_ = kSettle;
    next_state_ = kReady;
}

// 0x005BCCC0 (the pad is polled, the result unused)
void InputPort::get_settle()
{
    pad_present(port_no_);
    cur_phase_ = next_state_;
    next_state_ = kIdle;
}

// 0x005BDA20. The original first reads the pad (pull_data(0)); PartyMod removes that call (its Process does
// the reading).
void InputPort::fire_actuator(int actuator, int percent)
{
    if (rumble_off_)
        return;
    if (cur_phase_ != kReady || !(capabilities_ & kHasActuators))
        return;
    if (actuator < 0 || actuator >= rumble_motor_count_)
        return;
    // (x87: max * percent * 0.01f, truncated for the level; the same value * 256 for the motor)
    const double level = static_cast<double>(rumble_cap_[actuator]) * percent * static_cast<double>(0.01f);
    rumble_raw_[actuator] = static_cast<uint8_t>(static_cast<int>(level));
    const int motor = static_cast<int>(level * 256.0) & 0xFFFF;
    if (actuator == 0)
        set_actuators(port_no_, motor, rumble_raw_[1] << 8);
    else
        set_actuators(port_no_, rumble_raw_[0] << 8, motor);
}

// 0x005BDAD0
void InputPort::stop_rumble()
{
    if (rumble_off_)
        return;
    if (cur_phase_ == kReady && (capabilities_ & kHasActuators)) {
        for (int i = 0; i < rumble_motor_count_; ++i)
            rumble_raw_[i] = 0;
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
    if (pause_count_ == 0 && cur_phase_ == kReady && (capabilities_ & kHasActuators)) {
        for (int i = 0; i < rumble_motor_count_; ++i) {
            actuator_saved_[i] = rumble_raw_[i];
            rumble_raw_[i] = 0;
        }
        set_actuators(port_no_, 0, 0);
    }
    ++pause_count_;
}

// 0x005BDBD0
void InputPort::unpause_actuators()
{
    // NOLINTNEXTLINE(bugprone-inc-dec-in-conditions): the original's, as written
    if (pause_count_ == 0 || --pause_count_ != 0)
        return;
    if (cur_phase_ == kReady && (capabilities_ & kHasActuators)) {
        for (int i = 0; i < rumble_motor_count_; ++i)
            rumble_raw_[i] = actuator_saved_[i];
        set_actuators(port_no_, rumble_raw_[0] << 8, rumble_raw_[1] << 8);
    }
}

// 0x005BDC60
void InputPort::stop_actuators()
{
    if (cur_phase_ == kReady && (capabilities_ & kHasActuators)) {
        for (int i = 0; i < rumble_motor_count_; ++i) {
            rumble_raw_[i] = 0;
            actuator_saved_[i] = 0;
        }
        set_actuators(port_no_, 0, 0);
    }
}

// --- the keyboard -------------------------------------------------------------------------------------------

namespace {
IDirectInputDevice8A*& keyboard_device() { return os_layer::g_keyboard_device; }

int g_keyboard_buffer{2};
uint32_t g_manager_references;
uint32_t g_keyboard_char_count;

// Which of the two state buffers is current (2 at start: clear both first).
int& keyboard_buffer() { return g_keyboard_buffer; }

char g_key_name_result[256];
char g_name_buffer[256];
int g_keyboard_chars[16];
uint8_t g_keyboard_state[2][256];

uint8_t (&keyboard_state())[2][256] { return g_keyboard_state; }
uint32_t& keyboard_char_count() { return g_keyboard_char_count; }
int (&keyboard_chars())[16] { return g_keyboard_chars; }
} // namespace

// 0x005BDD60
void read_keyboard()
{
    keyboard_char_count() = 0;
    if (keyboard_buffer() == 2) {
        std::memset(keyboard_state(), 0, sizeof(keyboard_state()));
        keyboard_buffer() = 1;
    }
    IDirectInputDevice8A* keyboard = keyboard_device();
    if (!keyboard)
        return;
    keyboard_buffer() = keyboard_buffer() == 0 ? 1 : 0;
    const HRESULT result = keyboard->GetDeviceState(256, keyboard_state()[keyboard_buffer()]);
    if (result != DIERR_INPUTLOST && result != DIERR_NOTACQUIRED)
        return;
    if (SUCCEEDED(keyboard_device()->Acquire()))
        keyboard_device()->GetDeviceState(256, keyboard_state()[keyboard_buffer()]);
    else
        std::memset(keyboard_state()[keyboard_buffer()], 0, 256);
}

// 0x005BDD30
int get_keyboard_chars(char* out)
{
    uint32_t i = 0;
    for (; i < keyboard_char_count(); ++i)
        out[i] = static_cast<char>(keyboard_chars()[i]);
    return static_cast<int>(keyboard_char_count());
}

// 0x005BDE20
void clear_keyboard_chars() { keyboard_char_count() = 0; }

// --- the manager's lifetime --------------------------------------------------------------------------------

memory::owner<Hub*> g_manager;

Hub*& manager_instance() { return g_manager; }

namespace {
uint32_t& manager_references() { return g_manager_references; }
} // namespace

// --- typed keys -------------------------------------------------------------------------------------------

namespace {
// Whether typed keys are wanted: the keyboard is being typed on (0x007CE46E), or text entry is on
// (0x007CCDF8) outside a menu (0x007CE46F). PartyMod sets the last two for its text input.
bool& keyboard_typing() { return g_keyboard_on_screen; }
bool& text_entry() { return online::g_text_entry; }
bool& in_menu() { return g_in_menu_flag; }
} // namespace

// 0x005BDE70
void key_input(int key, int flags)
{
    if (!keyboard_typing() && (!text_entry() || in_menu()))
        return;
    if (flags < 0)
        return;
    bool queue = true;
    switch (key) {
    case '\r':
        key = 0x19;
        break; // Enter
    case '\b':
        key = 0x18;
        break; // Backspace
    case 0x1B: // Escape (not its auto-repeats)
        if (flags & 0x40000000)
            queue = false;
        else
            key = 0x1A;
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
    if (queue)
        keyboard_chars()[keyboard_char_count()++] = key;
    if (keyboard_char_count() == 16)
        keyboard_char_count() = 15; // (full: the last one is overwritten)
}

// --- special keys and key names ---------------------------------------------------------------------------

namespace {
// The other "in a menu" flag (PartyMod's names) and the quit request.
bool& menu_active() { return modules::g_menus_active; }
bool& quit_requested() { return render::g_quit_requested; }
// Port 0's key bindings (DirectInput key codes; -1 = none).
int key_binding(int action) { return os_layer::g_input_map[action]; }
// The platform layer's key names.
const wchar_t* key_name_w(int code) { return os_layer::get_key_name(code); }
char (&name_buffer())[256] { return g_name_buffer; }
char (&key_name_result())[256] { return g_key_name_result; }

void convert_key_name(int code)
{
    THUG2_MSVC_WARNING(suppress : 4996) // (as the original: the C runtime's conversion, at most 256 bytes)
    std::wcstombs(name_buffer(), key_name_w(code), 256);
    name_buffer()[255] = '\0';
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
        return key_binding(11);
    case 1:
        return key_binding(10);
    case 2:
        return key_binding(9);
    case 3:
        return key_binding(8);
    case 8:
        return 1; // Escape
    case 9:
        return 0x39; // space
    case 14:
        return key_binding(16);
    case 15:
        return key_binding(17);
    case 16:
        return key_binding(14);
    case 17:
        return key_binding(15);
    case 21:
        return key_binding(18);
    case 22:
        return 0x1C; // Enter
    case 23:
        return 1;
    case 24:
        return 0x12; // E
    case 25:
        return 0x13; // R
    case 26:
        return 0x0C; // -
    case 27:
        return 0x0D; // =
    case 28:
        return 2; // 1
    case 29:
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
    case 8:
    case 9:
    case 14:
    case 15:
    case 16:
    case 17:
        return -1;
    case 22:
        return 0x1C;
    case 23:
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
        return key_binding(11);
    case 1:
        return key_binding(10);
    case 2:
        return key_binding(9);
    case 3:
        return key_binding(8);
    case 8:
        return 1;
    case 9:
        return key_binding(13);
    case 14:
        return key_binding(16);
    case 15:
        return key_binding(17);
    case 16:
        return key_binding(14);
    case 17:
        return key_binding(15);
    case 21:
        return key_binding(18);
    default:
        return 0;
    }
}
} // namespace

// 0x005BCD50
const char* get_button_key_name(int button)
{
    const bool menu = menu_active() || in_menu();
    const bool typing = keyboard_typing();
    bool piece_menu = false;
    frontend::ElementPtr element;
    frontend::fetch_part(&element, checksums::hash_from_text("select_skater_hmenu"));
    if (element)
        element->is_concealed(); // (the result isn't used)
    {
        frontend::ElementPtr found;
        frontend::fetch_part(&found, checksums::hash_from_text("piece_menu_hmenu"));
        element = found;
    }
    if (!quit_requested() && element && !element->is_concealed())
        piece_menu = true;
    {
        frontend::ElementPtr found;
        frontend::fetch_part(&found, checksums::hash_from_text("select_skater_hmenu"));
        element = found;
        found.get_release();
    }
    if (element)
        element->is_concealed(); // (the result isn't used)

    const bool menu_keys = !piece_menu && menu && !typing;
    const int code = menu_keys ? get_menu_key(button) : typing ? typing_key(button) : play_key(button);
    convert_key_name(code);
    std::snprintf(key_name_result(), sizeof(key_name_result()), "%s", name_buffer());
    if (button == 0x12) {
        // two keys: "first+second"
        convert_key_name(key_binding(16));
        std::snprintf(key_name_result(), sizeof(key_name_result()), "%s+", name_buffer());
        convert_key_name(key_binding(17));
        const size_t used = std::strlen(key_name_result()); // (the original's strcat is unbounded)
        size_t length = std::strlen(name_buffer());
        if (length > sizeof(key_name_result()) - 1 - used)
            length = sizeof(key_name_result()) - 1 - used;
        std::memcpy(key_name_result() + used, name_buffer(), length);
        key_name_result()[used + length] = '\0';
    }
    if (menu_keys && (button == 0x14 || button == 0x13)) {
        // the two keys' characters: "S/X" or "Z/C" on a QWERTY keyboard
        const char first = key_char(button == 0x14 ? 0x1F : 0x2C);
        const char second = key_char(button == 0x14 ? 0x2D : 0x2E);
        key_name_result()[0] = first;
        key_name_result()[1] = '/';
        key_name_result()[2] = second;
        key_name_result()[3] = '\0';
    }
    return key_name_result();
}

// 0x005BDF30
void special_key_input(int key, int flags)
{
    frontend::ElementPtr element;
    frontend::fetch_part(&element, checksums::hash_from_text("menu_network_select_refresh"));
    bool server_list = false;
    if (element && !element->is_concealed())
        server_list = true;
    else if (!keyboard_typing() && (!text_entry() || in_menu()))
        return;
    if (flags < 0)
        return;
    if (server_list && key == VK_F5) {
        script::launch_script("refresh_chosen", nullptr, nullptr, false);
        return;
    }
    int queued = 0;
    if (in_menu()) {
        switch (key) {
        case VK_DOWN:
            queued = 0x17;
            break;
        case VK_UP:
            queued = 0x16;
            break;
        case VK_LEFT:
            queued = 0x14;
            break;
        case VK_RIGHT:
            queued = 0x15;
            break;
        default:
            break;
        }
    } else {
        switch (key) {
        case VK_F1:
            queued = 0x1B;
            break;
        case VK_F2:
            queued = 0x1C;
            break;
        case VK_F3:
            queued = 0x1D;
            break;
        case VK_F4:
            queued = 0x1E;
            break;
        default:
            break;
        }
    }
    if (queued)
        keyboard_chars()[keyboard_char_count()++] = queued;
    if (keyboard_char_count() == 16)
        keyboard_char_count() = 15;
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
bool& flag_696d_95() { return render::g_movie_playing; }
bool& flag_6f_1158() { return objects::g_cutscene_playing; }
constexpr uint32_t kParkEditorLevel = 0x7ECA21E5;
// The platform layer: the pad mode and the default keys' Enter / Escape (os_layer::set_pad_mode...), its window
// message pump, the pad read and whether a DirectInput key is down.
void set_pad_mode(int mode) { os_layer::set_pad_mode(mode); }
void set_default_enter_control(int control) { os_layer::set_default_enter_control(control); }
void set_default_escape_control(int control) { os_layer::set_default_escape_control(control); }
void pump_window_messages() { os_layer::process_window_events(); }
void read_pad_state(int port, PadSnapshot* state) { os_layer::read_pad(port + 1, state); }
bool get_key_down(int code) { return os_layer::get_key_down(code); }
// The port the front end is taking input from (a getter, 0x00553E90, on the object at 0x007CE4B4).
int get_menu_port() { return objects::g_profile_roster->cursor_pos_; }

uint8_t if_above(uint8_t value, uint8_t threshold, uint8_t bit) { return value > threshold ? bit : 0; }
uint8_t pressure_or_zero(uint8_t value) { return value > 0x20 ? value : 0; }
uint8_t all_or_nothing(uint32_t value) { return value ? 0xFF : 0; }
// A stick from -32768..32767 to 0..255 (signed division, rounding toward zero).
uint8_t get_stick(int value) { return static_cast<uint8_t>(value / 256); }
} // namespace

// 0x005BD200
void InputPort::pull_data(int)
{
    if (quit_requested())
        return;
    const bool menu = menu_active() || in_menu();
    const bool typing = keyboard_typing();
    bool select_skater_shown = false;
    bool piece_menu_shown = false;
    bool playing = false;

    // Which menus are up (the skater select's horizontal menu is checked before and after the park editor's).
    frontend::ElementPtr element;
    frontend::fetch_part(&element, checksums::hash_from_text("select_skater_hmenu"));
    if (element && !element->is_concealed())
        select_skater_shown = true;
    {
        frontend::ElementPtr found;
        frontend::fetch_part(&found, checksums::hash_from_text("piece_menu_hmenu"));
        element = found;
    }
    if (!quit_requested() && element && !element->is_concealed())
        piece_menu_shown = true;
    {
        frontend::ElementPtr found;
        frontend::fetch_part(&found, checksums::hash_from_text("select_skater_hmenu"));
        element = found;
    }
    if (element && !element->is_concealed())
        select_skater_shown = true;
    bool menu_mode = !piece_menu_shown && menu && !typing;
    if (flag_696d_95() || flag_6f_1158()) {
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
    if (!pad_present(port_no_))
        return;

    PadSnapshot pad;
    read_pad_state(port_no_, &pad);
    const uint8_t* p = pad.pressure;
    const uint16_t buttons = pad.buttons;
    const bool keyboard_menu = keyboard_control_ && menu_mode; // (the keyboard drives the menus: some buttons ignored)
    is_usable_ = 1;
    is_connected_ = true;
    // Two bytes of button bits, each set while the button is up.
    control_data_[0] = 0;
    control_data_[1] = 0x70;
    uint8_t first = keyboard_menu ? 0xFF : static_cast<uint8_t>(~(((buttons & 0xFF) >> 1) & 8));
    first ^= (buttons >> 5) & 7;
    control_data_[2] = first;
    uint8_t second = static_cast<uint8_t>(~(p[1] >= 0x20 ? 0x20 : 0)) ^ (p[3] >= 0x20 ? 0x10 : 0);
    if (!keyboard_menu)
        second ^= p[0] >= 0x20 ? 0x40 : 0;
    second ^= p[2] >= 0x20 ? 0x80 : 0;
    const uint8_t buttons_so_far = second;
    auto* level = reinterpret_cast<LevelInfo*>(park_editing::g_park_editor);
    if (level && level->checksum == kParkEditorLevel) {
        // The park editor: the triggers are mapped differently while its element 0x31631B98 is up.
        bool editor_element = false;
        if (frontend::g_ui_element_hub) {
            frontend::ElementPtr found;
            frontend::fetch_part(&found, 0x31631B98);
            editor_element = found.get_pull() != nullptr;
            found.get_release();
        }
        second ^= if_above(p[7], 0x20, editor_element ? 2 : 4);
        second ^= if_above(p[6], 0x20, 1);
    } else {
        second = buttons_so_far ^ if_above(p[6], 0x80, 1) ^ if_above(p[7], 0x80, 2) ^ if_above(p[6], 0x20, 4) ^
                 if_above(p[7], 0x20, 8);
    }
    control_data_[3] = second;
    // Pressures (dead zone 0x20).
    control_data_[12] = pressure_or_zero(p[3]);
    control_data_[13] = pressure_or_zero(p[1]);
    control_data_[14] = keyboard_menu ? 0 : pressure_or_zero(p[0]);
    control_data_[15] = pressure_or_zero(p[2]);
    control_data_[16] = pressure_or_zero(p[6]);
    control_data_[17] = pressure_or_zero(p[7]);
    control_data_[18] = 0;
    control_data_[19] = 0;
    control_data_[20] = 0;
    // The d-pad: pressures, and its bits in the first byte.
    control_data_[10] = all_or_nothing(buttons & 1);
    control_data_[8] = all_or_nothing(buttons & 8);
    control_data_[11] = all_or_nothing(buttons & 2);
    auto low = static_cast<uint8_t>(buttons);
    auto dpad = static_cast<uint8_t>((low & 0xFE) << 1);
    dpad = static_cast<uint8_t>((dpad ^ (low & 1)) << 2);
    dpad = static_cast<uint8_t>((dpad ^ (low & 8)) << 2);
    control_data_[2] ^= dpad;
    control_data_[9] = all_or_nothing(buttons & 4);
    // The sticks (y inverted).
    control_data_[4] = get_stick(pad.sticks[2] + 0x7FFF);
    control_data_[5] = get_stick(0x7FFF - pad.sticks[3]);
    control_data_[6] = get_stick(pad.sticks[0] + 0x7FFF);
    control_data_[7] = get_stick(0x7FFF - pad.sticks[1]);
    if (p[4] > 0x20)
        control_data_[20] = 1;
    if (p[5] > 0x20)
        control_data_[20] |= 2;
    held_down_ = ((control_data_[2] << 8 | control_data_[3]) ^ 0xFFFF) != 0;

    // The keyboard takes over the menus after start, a pressed trigger, a typed Enter, or (playing) Enter.
    bool enter_typed = false;
    if (typing) {
        char typed[256];
        const int count = get_keyboard_chars(typed);
        for (int i = 0; i < count; ++i) {
            if (typed[i] == 0x19) {
                enter_typed = true;
                break;
            }
        }
    }
    keyboard_control_ = (buttons & 0x10) || p[0] >= 0x20 || enter_typed || (playing && get_key_down(0x1C));
}

// --- the manager -------------------------------------------------------------------------------------------

// 0x005BE1F0
Hub::Hub()
{
    devs_.make_head();
    task_ = std::make_unique<tasks::Task<memory::ListNode>>(&process_task, &devs_, 0);
    int index = 0;
    for (int port = 0; port < 2; ++port) {
        auto* device = new InputPort(index, port, 0);
        if (!device)
            continue;
        memory::ListNode* node = device->list_link_; // onto the end of the list
        node->prev = devs_.prev;
        node->next = &devs_;
        devs_.prev->next = node;
        devs_.prev = node;
        device->acquire();
        ++index;
    }
    if (!audio::music_off_requested()) // (the music system is set up with the controllers)
        audio::prepare_all();
}

// 0x005BE300
Hub::~Hub()
{
    lists::Hunt<InputPort> search;
    for (const InputPort* device = search.head_element(&devs_); device;) {
        const InputPort* next = search.following_item();
        delete device;
        device = next;
    }
    task_.reset();
    stubs::return_zero(); // (a folded empty function - compiled-out code)
    if (!audio::music_off_requested())
        audio::stop_user_soundtrack();
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
void process_task(tasks::Task<memory::ListNode>* task) { process_all(task->content_); }

// 0x005BE3F0
void Hub::poll_pads() { process_all(&devs_); }

// 0x005BE490
InputPort* Hub::fetch_gadget(int port, int slot)
{
    lists::Hunt<InputPort> search;
    for (InputPort* device = search.head_element(&devs_); device; device = search.following_item()) {
        if (device->port_no_ == port && device->seat_ == slot)
            return device;
    }
    return nullptr;
}

// 0x005BE4F0
InputPort* Hub::get_pad_at_slot(int index)
{
    lists::Hunt<InputPort> search;
    for (InputPort* device = search.head_element(&devs_); device; device = search.following_item()) {
        if (device->ordinal_ == index)
            return device;
    }
    return nullptr;
}

// 0x005BE550
void Hub::pause_devices()
{
    lists::Hunt<InputPort> search;
    for (InputPort* device = search.head_element(&devs_); device; device = search.following_item())
        device->pause_actuators();
}

// 0x005BE5E0
void Hub::unpause_devices()
{
    lists::Hunt<InputPort> search;
    for (InputPort* device = search.head_element(&devs_); device; device = search.following_item())
        device->unpause_actuators();
}

// 0x005BE670
Hub* create_manager(bool create)
{
    if (!manager_instance() && create)
        manager_instance() = new Hub;
    ++manager_references();
    return manager_instance();
}

// 0x005BDCF0
void release_manager()
{
    if (manager_references() > 1) {
        --manager_references();
        return;
    }
    delete manager_instance();
    manager_instance() = nullptr;
    manager_references() = 0;
}

} // namespace sio
