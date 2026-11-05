// The DirectInput input source: see dinput_source.h.

#include "input_dinput/dinput_source.h"

#include <windows.h>
#include <dinput.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "engine/pad_input/input_source.h"
#include "input_dinput/dinput_state.h"
#include "input_dinput/direct_input.h"

namespace input::dinput {

namespace {

constexpr size_t kKeyboardStateBytes = 256;
constexpr int kKeyboardStateSize = 256;

// NOT ORIGINAL grouping - the frame's keyboard state: two 256-byte buffers, and which is current (2 at start: clear
// both first). Made on first use, never destroyed (keyboard_frame()).
struct KeyboardFrame {
    int keyboard_buffer_{2};
    std::array<std::array<uint8_t, kKeyboardStateBytes>, 2> keyboard_state_{};
};

KeyboardFrame& keyboard_frame()
{
    static core::NoDestroy<KeyboardFrame> state;
    return state.get();
}

int& keyboard_buffer() { return keyboard_frame().keyboard_buffer_; }
std::array<std::array<uint8_t, kKeyboardStateBytes>, 2>& keyboard_state() { return keyboard_frame().keyboard_state_; }

class DirectInputSource final : public Source {
public:
    bool devices_made() override { return os_layer::direct_input_state().direct_input() != nullptr; }
    void start() override;
    void read_frame() override;
    bool has_pad(int port) override { return os_layer::read_joystick(port) != nullptr; }
    void set_pad_mode(int mode) override { os_layer::set_pad_mode(mode); }
    void set_default_keys(DefaultKeys keys) override
    {
        os_layer::set_default_enter_control(keys.enter);
        os_layer::set_default_escape_control(keys.escape);
    }
    bool read_pad(int port, sio::PadSnapshot* pad) override
    {
        os_layer::read_pad(port + 1, pad);
        return true;
    }
    bool key_down(int code, bool* down) override
    {
        *down = os_layer::get_key_down(code);
        return true;
    }
};

// 0x005F44F0's part (os_layer::init_input): per port the joysticks (0x007D6318), their device state, the
// force-feedback effects (0x007D6668) and the motors' ranges and kinds cleared.
void DirectInputSource::start()
{
    os_layer::DirectInputState& state = os_layer::direct_input_state();
    core::at(state.joysticks(), 0) = nullptr;
    std::memset(state.joystick_states().data(), 0, sizeof(state.joystick_states()));
    core::at(state.joysticks(), 1) = nullptr;
    std::memset(state.last_joystick_states().data(), 0, sizeof(state.last_joystick_states()));
    core::at(state.effects(), 0) = nullptr;
    core::at(state.force_resolutions(), 0) = 0;
    core::at(state.effects(), 1) = nullptr;
    core::at(state.force_resolutions(), 1) = 0;
    core::at(state.force_resolutions(), 2) = 0;
    core::at(state.force_resolutions(), 3) = 0;
    core::at(state.force_axis_counts(), 0) = 0;
    core::at(state.force_axis_counts(), 1) = 0;
}

// 0x005BDD60's device read (sio::read_keyboard): the keyboard's state into one of the two buffers in turn; lost, it
// is re-acquired and read again, or all up when that fails.
void DirectInputSource::read_frame()
{
    if (keyboard_buffer() == 2) {
        std::memset(keyboard_state().data(), 0, sizeof(keyboard_state()));
        keyboard_buffer() = 1;
    }
    IDirectInputDevice8A* keyboard = os_layer::keyboard_device();
    if (!keyboard) {
        return;
    }
    keyboard_buffer() = keyboard_buffer() == 0 ? 1 : 0;
    const HRESULT result = keyboard->GetDeviceState(kKeyboardStateSize, core::at(keyboard_state(), keyboard_buffer()).data());
    if (result != DIERR_INPUTLOST && result != DIERR_NOTACQUIRED) {
        return;
    }
    if (SUCCEEDED(os_layer::keyboard_device()->Acquire())) {
        os_layer::keyboard_device()->GetDeviceState(kKeyboardStateSize, core::at(keyboard_state(), keyboard_buffer()).data());
    } else {
        std::memset(core::at(keyboard_state(), keyboard_buffer()).data(), 0, kKeyboardStateSize);
    }
}

} // namespace

Source& keyboard()
{
    static core::NoDestroy<DirectInputSource> source;
    return source.get();
}

} // namespace input::dinput
