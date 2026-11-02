// A controller's buttons and sticks (0x0046B7E0-0x0046C470). See control_pad.h.

#include "engine/objects/control_pad.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <array>

#include "engine/core/checksums.h"
#include "game/rider/thaw_bmx.h"
#include "platform/game_clock.h"
#include "platform/stopwatch.h"
#include "engine/core/checked_index.h"

namespace objects {

int g_clock_offset;

namespace {

constexpr int kPressedAbove = 0x40;

// The game clock with its offset (0x00696D60).
int get_clock() { return frame_clock::game_clock_elapsed() + g_clock_offset; }

// frame_clock::GetTime as the original compares it: unsigned, as a float would hold it.
double get_now() { return static_cast<double>(frame_clock::get_time()); }

constexpr float float_from_bits(uint32_t bits) noexcept { return std::bit_cast<float>(bits); }

constexpr float kPi = float_from_bits(0x40490FDB);
constexpr float kHalfPi = float_from_bits(0x3FC90FDB);
constexpr float kQuarterPi = float_from_bits(0x3F490FDB);
constexpr float kThreeQuarterPi = float_from_bits(0x4016CBE4);

// A stick's data byte with 0x80 as the centre, moved 50 towards 0 (crossing it: there's no dead band) and / 77;
// exactly centred is 0. (-0x80 counts as -0x7F.)
void scale_stick(uint8_t byte, float* out)
{
    int value = byte - 0x80;
    if (value == 0) {
        *out = 0.0f;
        return;
    }
    if (value == -0x80)
        value = -0x7F;
    const int moved = value + (value > 0 ? -50 : 50);
    *out = static_cast<float>(moved * static_cast<double>(float_from_bits(0x3C54C77B)));
}

// The button value of a pressure byte: any pressure counts as full.
int get_full(bool down) { return down ? 0xFF : 0; }

// A lock of the second stick stays while it is set, the stick isn't centred, the stick is in the lock's
// quarter and the time hasn't passed.
void update_lock(uint32_t& lock, float length, bool in_quarter)
{
    if (lock == 0)
        return;
    if (length != 0.0f && in_quarter && get_now() <= static_cast<double>(lock))
        return;
    lock = 0;
}

} // namespace

// 0x0046B7E0
void ControlButton::reset_activation() { fired_ = 0; }

// 0x0046B7F0
void ControlButton::forget_let_go() { let_go_ = 0; }

// 0x0046B800
void ControlButton::change_title(const char* name) { label_ = checksums::hash_from_text(name); }

// 0x0046B820
void ControlButton::tick_frame(int pressure)
{
    if (locked_until_ != 0.0f) {
        // (compared as the original: a NaN lock keeps holding presses off)
        if (pressure > kPressedAbove && !(get_now() > static_cast<double>(locked_until_)))
            return;
        locked_until_ = 0.0f;
    }
    press_amount_ = pressure;
    if (pressure > kPressedAbove) {
        if (!held_down_) {
            press_time_ = get_clock();
            fired_ = 1;
        }
        held_down_ = 1;
    } else {
        if (held_down_) {
            release_time_ = get_clock();
            let_go_ = 1;
        }
        held_down_ = 0;
    }
}

// 0x0046B8B0
void ControlButton::set_held_down(uint8_t down)
{
    if (down) {
        if (!held_down_) {
            press_time_ = get_clock();
            fired_ = 1;
        }
    } else if (held_down_) {
        let_go_ = 1;
    }
    held_down_ = down;
}

// 0x0046B900
int ControlButton::read_let_go_time()
{
    const int released = release_time_;
    return frame_clock::game_clock_elapsed() + (g_clock_offset - released);
}

// 0x0046B920
int ControlButton::get_held_millis()
{
    const int pressed = press_time_;
    return frame_clock::game_clock_elapsed() + (g_clock_offset - pressed);
}

namespace {

// Every button of the pad with the name scripts ask for it by (each name's checksum is what read_button matches).
struct PadButtonName {
    ControlButton PadSnapshot::* button;
    const char* name;
};
constexpr auto kPadButtons = std::to_array<PadButtonName>({
    {&PadSnapshot::up_dir_, "UP"},
    {&PadSnapshot::btn_down_, "DOWN"},
    {&PadSnapshot::left_side_, "LEFT"},
    {&PadSnapshot::right_side_, "RIGHT"},
    {&PadSnapshot::button_box_, "SQUARE"},
    {&PadSnapshot::button_round_, "CIRCLE"},
    {&PadSnapshot::button_wedge_, "TRIANGLE"},
    {&PadSnapshot::pos_x_, "X"},
    {&PadSnapshot::shoulder_left_a_, "L1"},
    {&PadSnapshot::shoulder_left_b_, "L2"},
    {&PadSnapshot::stick_click_left_, "L3"},
    {&PadSnapshot::shoulder_right_a_, "R1"},
    {&PadSnapshot::shoulder_right_b_, "R2"},
    {&PadSnapshot::stick_click_right_, "R3"},
    {&PadSnapshot::outset_, "START"},
    {&PadSnapshot::button_choose_, "SELECT"},
});

} // namespace

// 0x0046B940
PadSnapshot::PadSnapshot()
{
    for (const PadButtonName& entry : kPadButtons)
        (this->*entry.button).change_title(entry.name);
    core::at(lock_, 0) = core::at(lock_, 1) = 0;
}

// 0x0046BB50: every button let go (a held one records its release), the sticks centred.
void PadSnapshot::get_reset()
{
    for (const PadButtonName& entry : kPadButtons)
        (this->*entry.button).tick_frame(0);
    for (float& f : stick_)
        f = 0.0f;
    for (float& f : scaled_stick_)
        f = 0.0f;
    core::at(polar_, 0) = {0.0f, 0.0f};
    core::at(polar_, 1) = {0.0f, 0.0f};
    // NOT ORIGINAL - THAW's BMX (the feature on): THAW's stick buttons beside the pad up too.
    if (skater::thaw_bmx::is_switched_on())
        skater::thaw_bmx::update_pad_sticks(this, nullptr);
}

// 0x0046BC60
void PadSnapshot::get_clear()
{
    get_reset();
    auto both = std::to_array<ControlButton*>({&up_dir_,          &btn_down_,        &left_side_,        &right_side_,
                             &shoulder_left_a_, &shoulder_left_b_, &stick_click_left_, &shoulder_right_a_});
    for (ControlButton* button : both) {
        button->reset_activation();
        button->forget_let_go();
    }
    // (the original's slip: R2's press is taken twice and its release kept, R3's release twice and its press kept)
    shoulder_right_b_.reset_activation();
    shoulder_right_b_.reset_activation();
    stick_click_right_.forget_let_go();
    stick_click_right_.forget_let_go();
    auto rest = std::to_array<ControlButton*>({&button_box_, &button_round_, &pos_x_, &button_wedge_, &outset_, &button_choose_});
    for (ControlButton* button : rest) {
        button->reset_activation();
        button->forget_let_go();
    }
}

// 0x0046BDA0
void PadSnapshot::tick_frame(const pad_input::Payload* payload)
{
    const auto& data = payload->incident_; // (the sticks, then a pressure per button)
    up_dir_.tick_frame(get_full(core::at(data, 6) != 0));
    btn_down_.tick_frame(get_full(core::at(data, 7) != 0));
    left_side_.tick_frame(get_full(core::at(data, 5) != 0));
    right_side_.tick_frame(get_full(core::at(data, 4) != 0));
    button_box_.tick_frame(get_full(core::at(data, 0xB) != 0));
    button_round_.tick_frame(get_full(core::at(data, 9) != 0));
    pos_x_.tick_frame(get_full(core::at(data, 0xA) != 0));
    button_wedge_.tick_frame(get_full(core::at(data, 8) != 0));
    shoulder_left_a_.tick_frame(get_full(core::at(data, 0xC) != 0));
    shoulder_right_a_.tick_frame(get_full(core::at(data, 0xD) != 0));
    shoulder_left_b_.tick_frame(get_full(core::at(data, 0xE) != 0));
    shoulder_right_b_.tick_frame(get_full(core::at(data, 0xF) != 0));
    const uint32_t bits = payload->buttons_;
    stick_click_left_.tick_frame(get_full((bits & 0x200) != 0));
    stick_click_right_.tick_frame(get_full((bits & 0x400) != 0));
    button_choose_.tick_frame(get_full((bits & 0x100) != 0));
    // (START isn't read here)

    for (int i = 0; i < 4; ++i)
        scale_stick(core::at(data, i), &core::at(scaled_stick_, i));
    for (int i = 0; i < 4; ++i)
        core::at(stick_, i) = static_cast<float>(core::at(data, 0x12 + i) - 0x80);

    double x = core::at(stick_, 0), y = core::at(stick_, 1);
    core::at(polar_, 0).length = static_cast<float>(std::sqrt(x * x + y * y));
    core::at(polar_, 0).angle = static_cast<float>(std::atan2(static_cast<double>(core::at(stick_, 0)), -static_cast<double>(core::at(stick_, 1))));

    x = core::at(stick_, 2);
    y = core::at(stick_, 3);
    const double length = std::sqrt(y * y + x * x);
    core::at(polar_, 1).length = static_cast<float>(length);
    // (compared unrounded, as the original's)
    if (length > float_from_bits(0x3A83126F)) {
        core::at(polar_, 1).angle =
            static_cast<float>(std::atan2(static_cast<double>(core::at(stick_, 2)), -static_cast<double>(core::at(stick_, 3))));
    } else {
        // the d-pad stands in for a centred stick
        const uint8_t up = core::at(data, 6) != 0;
        const uint8_t down = core::at(data, 7) != 0;
        const uint8_t left = core::at(data, 5) != 0;
        const uint8_t right = core::at(data, 4) != 0;
        if (up || down || left || right) {
            core::at(polar_, 1).angle = d_pad_angle(up, down, left, right);
            core::at(polar_, 1).length = 127.0f;
        } else {
            core::at(polar_, 1).angle = 0.0f;
            core::at(polar_, 1).length = 0.0f;
        }
    }

    const double angle = std::fabs(static_cast<double>(core::at(polar_, 1).angle));
    update_lock(core::at(lock_, 0), core::at(polar_, 1).length, angle < kQuarterPi);
    update_lock(core::at(lock_, 1), core::at(polar_, 1).length, static_cast<double>(kPi) - angle < kQuarterPi);
    // NOT ORIGINAL - THAW's BMX (the feature on): THAW's pad's eight stick buttons (its 0x596CA0), beside the pad -
    // game/rider/thaw_bmx.h. Original behaviour: none.
    if (skater::thaw_bmx::is_switched_on())
        skater::thaw_bmx::update_pad_sticks(this, data.data());
}

// 0x0046C270
ControlButton* PadSnapshot::read_button(uint32_t name)
{
    // (the original's order)
    auto buttons = std::to_array<ControlButton*>({&up_dir_,           &btn_down_,          &left_side_,        &right_side_,
                                &button_box_,       &button_round_,      &button_wedge_,     &pos_x_,
                                &shoulder_left_a_,  &shoulder_left_b_,   &stick_click_left_, &shoulder_right_a_,
                                &shoulder_right_b_, &stick_click_right_, &outset_,           &button_choose_});
    for (ControlButton* button : buttons) {
        if (button->label_ == name)
            return button;
    }
    // NOT ORIGINAL - THAW's BMX (the feature on): THAW's stick buttons by name (leftup ... rightright - its bike
    // scripts' DoBalanceTrick ButtonA=leftright, HeldLongerThan button=leftdown). Original behaviour: null.
    if (skater::thaw_bmx::is_switched_on())
        return static_cast<ControlButton*>(skater::thaw_bmx::pad_stick_button(this, name));
    return nullptr;
}

ControlButton* PadSnapshot::button_at_offset(uint32_t offset)
{
    // (the buttons in their order in the pad, 0x20 bytes apart)
    auto buttons = std::to_array<ControlButton*>({&up_dir_,           &btn_down_,          &left_side_,      &right_side_,
                                                  &shoulder_left_a_,  &shoulder_left_b_,   &stick_click_left_,
                                                  &shoulder_right_a_, &shoulder_right_b_, &stick_click_right_,
                                                  &button_round_,     &button_box_,        &button_wedge_,   &pos_x_,
                                                  &outset_,           &button_choose_});
    constexpr uint32_t kButtonSize = 0x20;
    if (offset % kButtonSize != 0 || offset / kButtonSize >= buttons.size())
        return nullptr;
    return buttons.at(offset / kButtonSize);
}

// 0x0046BA50
int d_pad_direction(uint8_t up, uint8_t down, uint8_t left, uint8_t right)
{
    // (0x0067D8C8)
    static constexpr std::array<int, 16> kDirections = {{0, 1, 2, 2, 3, 5, 7, 7, 4, 6, 8, 8, 4, 6, 8, 8}};
    return core::at(kDirections, up + 2 * (down + 2 * (left + 2 * right)));
}

// 0x0046BA80
float d_pad_angle(uint8_t up, uint8_t down, uint8_t left, uint8_t right)
{
    // (the original's static table 0x006F2270, filled on the first call; up and down together count as down,
    // left and right together as right)
    static const std::array<float, 16> kAngles = {{
        0.0f,
        0.0f,
        kPi,
        kPi,
        -kHalfPi,
        -kQuarterPi,
        -kThreeQuarterPi,
        -kThreeQuarterPi,
        kHalfPi,
        kQuarterPi,
        kThreeQuarterPi,
        kThreeQuarterPi,
        kHalfPi,
        kQuarterPi,
        kThreeQuarterPi,
        kThreeQuarterPi,
    }};
    return core::at(kAngles, up + 2 * (down + 2 * (left + 2 * right)));
}

// 0x0046C370
double stick_length(float x, float y, float angle)
{
    const double nx = x / static_cast<double>(x > 0.0f ? 127.0f : 128.0f);
    const double ny = y / static_cast<double>(y > 0.0f ? 127.0f : 128.0f);
    // the angle brought into the quarter around up / down, then around left / right
    const double a = angle;
    double to_gate = 0.0;
    if (a > -static_cast<double>(kQuarterPi) && a < kQuarterPi)
        to_gate = a;
    else if (a > kQuarterPi && a < kThreeQuarterPi)
        to_gate = a - kHalfPi;
    else if (std::fabs(a) > kThreeQuarterPi)
        to_gate = std::fabs(a) - kPi;
    else
        to_gate = a + kHalfPi;
    const double length =
        std::sqrt(ny * ny + nx * nx) * std::fabs(std::cos(to_gate)) * static_cast<double>(float_from_bits(0x3F969696));
    return length > 1.0 ? 1.0 : length; // (unrounded: the original leaves it in ST0)
}

} // namespace objects
