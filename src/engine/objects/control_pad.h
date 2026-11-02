#pragma once

// A controller's state as the skater (and the editors) read it (0x0046B7E0-0x0046C470): sixteen buttons, each
// knowing when it went down / up and its pressure, both sticks (raw, dead-banded, and as angle + length), and two
// timed holds of the second stick. The names are THPS's (CSkaterPad / CSkaterButton); the
// code here is THUG2's.

#include <cstddef>
#include <cstdint>
#include <array>
#include <span>
#include "engine/pad_input/input_hub.h"

namespace objects {

class ControlButton {
public:
    // 0x0046B7E0 / 0x0046B7F0: the press / release events taken.
    void reset_activation();
    void forget_let_go();
    // 0x0046B800: the button's name (the checksum of `name`).
    void change_title(const char* name);
    // 0x0046B820: the new pressure (0-255; down above 0x40). While the button is locked (+0x1C, a frame_clock::GetTime
    // millisecond time, 0 = not locked), a press isn't taken until that time has passed; any update that isn't a
    // press ends the lock.
    void tick_frame(int pressure);
    // 0x0046B8B0: down or up as `down` (the byte itself is stored), without the pressure; a release here isn't
    // timed.
    void set_held_down(uint8_t down);
    // 0x0046B900 / 0x0046B920: game-clock milliseconds since the button was last released / pressed.
    int read_let_go_time();
    int get_held_millis();

    uint8_t held_down_; // +0x00
    std::array<uint8_t, 3> unknown1_;
    int press_time_;   // +0x04: the game clock (with its offset) at the last press
    int release_time_; // +0x08
    int press_amount_; // +0x0C
    uint8_t fired_;    // +0x10: went down (until reset_activation)
    std::array<uint8_t, 3> unknown11_;
    int let_go_;         // +0x14: went up (until forget_let_go)
    uint32_t label_;     // +0x18
    float locked_until_; // +0x1C
};
static_assert(sizeof(ControlButton) == 0x20, "layout must match the original");

class PadSnapshot {
public:
    // 0x0046B940: the buttons' names; no locks.
    PadSnapshot();
    // 0x0046BB50: every button updated as released (pressure 0), the sticks zeroed.
    void get_reset();
    // 0x0046BC60: Reset, and the buttons' events taken - except, as the original, R2's release and R3's press
    // (R2's press and R3's release are taken twice instead).
    void get_clear();
    // 0x0046BDA0: the buttons and sticks from the controller's data (the PS2 pad layout: pressures 4-0xF, the
    // sticks 0-3 and 0x12-0x15 around 0x80, more buttons in the bits of +0x1C).
    void tick_frame(const pad_input::Payload* payload);
    // 0x0046C270: the button named `name` (null if none).
    ControlButton* read_button(uint32_t name);
    // The button `offset` bytes into the pad (the original's tables and calls name buttons by their offset: a
    // multiple of 0x20 below 0x200); null for another offset.
    ControlButton* button_at_offset(uint32_t offset);

    // A stick as an angle (atan2(x, -y): 0 = up, clockwise positive) and a length.
    struct Polar {
        float angle;
        float length;
    };

    // The fields other code reads and sets.
    ControlButton& up_dir() { return up_dir_; }
    [[nodiscard]] const ControlButton& up_dir() const { return up_dir_; }
    ControlButton& btn_down() { return btn_down_; }
    [[nodiscard]] const ControlButton& btn_down() const { return btn_down_; }
    ControlButton& left_side() { return left_side_; }
    [[nodiscard]] const ControlButton& left_side() const { return left_side_; }
    ControlButton& right_side() { return right_side_; }
    [[nodiscard]] const ControlButton& right_side() const { return right_side_; }
    ControlButton& shoulder_left_a() { return shoulder_left_a_; }
    [[nodiscard]] const ControlButton& shoulder_left_a() const { return shoulder_left_a_; }
    ControlButton& shoulder_left_b() { return shoulder_left_b_; }
    [[nodiscard]] const ControlButton& shoulder_left_b() const { return shoulder_left_b_; }
    ControlButton& stick_click_left() { return stick_click_left_; }
    [[nodiscard]] const ControlButton& stick_click_left() const { return stick_click_left_; }
    ControlButton& shoulder_right_a() { return shoulder_right_a_; }
    [[nodiscard]] const ControlButton& shoulder_right_a() const { return shoulder_right_a_; }
    ControlButton& shoulder_right_b() { return shoulder_right_b_; }
    [[nodiscard]] const ControlButton& shoulder_right_b() const { return shoulder_right_b_; }
    ControlButton& stick_click_right() { return stick_click_right_; }
    [[nodiscard]] const ControlButton& stick_click_right() const { return stick_click_right_; }
    [[nodiscard]] const ControlButton& button_round() const { return button_round_; }
    [[nodiscard]] const ControlButton& button_box() const { return button_box_; }
    ControlButton& button_wedge() { return button_wedge_; }
    [[nodiscard]] const ControlButton& button_wedge() const { return button_wedge_; }
    ControlButton& pos_x() { return pos_x_; }
    [[nodiscard]] const ControlButton& pos_x() const { return pos_x_; }
    ControlButton& button_choose() { return button_choose_; }
    [[nodiscard]] const ControlButton& button_choose() const { return button_choose_; }
    [[nodiscard]] const std::array<float, 4>& stick() const { return stick_; }
    [[nodiscard]] const std::array<float, 4>& scaled_stick() const { return scaled_stick_; }
    std::array<Polar, 2>& polar() { return polar_; }
    [[nodiscard]] const std::array<Polar, 2>& polar() const { return polar_; }
    std::array<uint32_t, 2>& lock() { return lock_; }
    [[nodiscard]] const std::array<uint32_t, 2>& lock() const { return lock_; }

private:
    ControlButton up_dir_{};            // +0x000
    ControlButton btn_down_{};          // +0x020
    ControlButton left_side_{};         // +0x040
    ControlButton right_side_{};        // +0x060
    ControlButton shoulder_left_a_{};   // +0x080
    ControlButton shoulder_left_b_{};   // +0x0A0
    ControlButton stick_click_left_{};  // +0x0C0
    ControlButton shoulder_right_a_{};  // +0x0E0
    ControlButton shoulder_right_b_{};  // +0x100
    ControlButton stick_click_right_{}; // +0x120
    ControlButton button_round_{};      // +0x140
    ControlButton button_box_{};        // +0x160
    ControlButton button_wedge_{};      // +0x180
    ControlButton pos_x_{};             // +0x1A0
    ControlButton outset_{};            // +0x1C0
    ControlButton button_choose_{};     // +0x1E0
    std::array<float, 4> stick_{};                  // +0x200: two sticks' x, y: the data's 0x12-0x15 - 0x80
    std::array<float, 4> scaled_stick_{}; // +0x210: the data's 0-3 - 0x80, moved 50 towards 0 (past it: no dead band), / 77

    // +0x220: each stick as an angle (atan2(x, -y): 0 = up, clockwise positive) and a length; the second is the
    // d-pad's (length 127) while that stick is centred.
    std::array<Polar, 2> polar_{};
    // +0x230: two frame_clock::GetTime times (0 = none; set by other code) for the second stick held up / down: Update
    // clears each once it has passed, or once the stick is centred or leaves that quarter.
    std::array<uint32_t, 2> lock_{};
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(PadSnapshot, stick_) == 0x200 && offsetof(PadSnapshot, polar_) == 0x220 &&
                          offsetof(PadSnapshot, lock_) == 0x230,
                      "layout must match the original");
    }

    // Every button with the name scripts ask for it by (each name's checksum is what read_button matches).
    struct ButtonName {
        ControlButton PadSnapshot::* button;
        const char* name;
    };
    static std::span<const ButtonName> button_names();
};

// 0x0046BA50: the d-pad direction number (0 none, 1 up, 2 down, 3 left, 4 right, 5-8 the diagonals ...) of the
// four directions held.
int d_pad_direction(uint8_t up, uint8_t down, uint8_t left, uint8_t right);
// 0x0046BA80: the d-pad's angle (radians, 0 = up, clockwise positive) for the four directions held.
float d_pad_angle(uint8_t up, uint8_t down, uint8_t left, uint8_t right);
// 0x0046C370: a stick's length 0-1 (x, y around 0, -128..127), squared off towards the corners of the stick's
// square gate by the angle (x 1/0.85, at most 1), unrounded.
double stick_length(float x, float y, float angle);

int clock_offset();
void set_clock_offset(int value);

} // namespace objects
