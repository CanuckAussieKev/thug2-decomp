// NOT ORIGINAL - THAW's BMX ridden in THUG2 (game/rider/thaw_bmx.h): THAW's stick buttons - its pad's and its trick
// component's -, BikePress's re-arm rule and the look-around's d-pad inputs. Our addition at the user's request - not
// THUG2's code and not PartyMod's. Read from THAW's reference build (game/rider/thaw_bmx.h): the input
// server's 0x435530 / 0x4355B0, the pad update 0x596CA0, the trick component's 0x471D20 / 0x4723D0 / check_trigger
// 0x470EAB, the look-around's update 0x5B08F0. THUG2's own code calls these only while the feature is on.

#include <cstdint>
#include <span>
#include <unordered_map>
#include <array>

#include "engine/core/checksums.h"
#include "engine/objects/control_pad.h"
#include "game/rider/thaw_bmx.h"
#include "game/rider/trick_component.h"
#include "engine/objects/input_component.h"
#include "engine/core/checked_index.h"

namespace skater::thaw_bmx {

namespace {
constexpr int kStickButtonCount = 8;      // the stick's buttons: eight directions
constexpr int kPressedValue = 0xFF;
constexpr int kDirectionCount = 8;
constexpr float kLookStrength = 1.5F;

// THAW's pad button names (by StickButtonIndex) and its trick component's (by stick, by direction 1-8: up, down,
// left, right, up-left, up-right, down-left, down-right - d_pad_direction's numbers).
const std::array<const char*, 8> kPadNames = {{"leftup",  "leftdown",  "leftleft",  "leftright",
                                  "rightup", "rightdown", "rightleft", "rightright"}};
const std::array<std::array<const char*, 8>, 2> kStickTrickTable = {{
    {"leftup", "leftdown", "leftleft", "leftright", "leftupleft", "leftupright", "leftdownleft", "leftdownright"},
    {"rightup", "rightdown", "rightleft", "rightright", "rightupleft", "rightupright", "rightdownleft",
     "rightdownright"},
}};

uint32_t get_name(const char* text) { return checksums::hash_from_text(text); }

constexpr uint8_t kMiddle = 0x80;
// THAW 0x435530: the right stick's direction past these (about 60% over; the left stick's: any way off the middle).
constexpr uint8_t kRightOver = 0xCB, kRightUnder = 0x35;

// A pad's eight stick buttons (THAW's pad +0x200..+0x2E0), beside it.
struct PadSticks {
    std::array<objects::ControlButton, kStickButtonCount> buttons = {};
};
std::unordered_map<const void*, PadSticks>& get_pads()
{
    static std::unordered_map<const void*, PadSticks> pads;
    return pads;
}
PadSticks& sticks_of(const void* pad)
{
    PadSticks& sticks = get_pads()[pad];
    for (int i = 0; i < kStickButtonCount; ++i) {
        if (core::at(sticks.buttons, i).label() == 0U) {
            core::at(sticks.buttons, i).set_label(stick_button_name(i));
        }
    }
    return sticks;
}

// The trick component's pad (the input component's).
const void* trick_pad(const objects::MoveExecutor* trick) { return &trick->input()->pad_primary(); }

// The bike state of `trick`'s skater while it rides the bike, else null.
BikeState* riding_state(const objects::MoveExecutor* trick)
{
    const SkaterPhysicsModule* core = trick->core_physics();
    if (!core || !live_now(core)) {
        return nullptr;
    }
    return state_of(core, false);
}

// A press / release of a stick direction, as MoveExecutor::update_button (0x0051F8F0) makes one for THUG2's
// buttons: not for an ignored button.
void add_stick_event(objects::MoveExecutor* trick, uint32_t name, bool pressed)
{
    for (int i = 0; i < trick->ignored_count(); ++i) {
        if (core::at(trick->ignored(), i) == name) {
            return;
        }
    }
    trick->append_incident(pressed ? 1 : 2)->button = name;
}

} // namespace

uint32_t stick_directions(std::span<const uint8_t> data)
{
    uint32_t bits = 0;
    const uint8_t right_x = core::at(data, 0);
    const uint8_t right_y = core::at(data, 1);
    const uint8_t left_x = core::at(data, 2);
    const uint8_t left_y = core::at(data, 3);
    // the right stick (THAW 0x435530): x past 0xCB right, under 0x35 left; y under 0x35 up, past 0xCB down
    if (right_x > kRightOver) {
        bits |= 1U << kRightRight;
    } else if (right_x < kRightUnder) {
        bits |= 1U << kRightLeft;
    }
    if (right_y < kRightUnder) {
        bits |= 1U << kRightUp;
    } else if (right_y > kRightOver) {
        bits |= 1U << kRightDown;
    }
    // the movement stick (0x4355B0): any way off the middle (the bytes come centred within the dead band)
    if (left_x > kMiddle) {
        bits |= 1U << kLeftRight;
    } else if (left_x < kMiddle) {
        bits |= 1U << kLeftLeft;
    }
    if (left_y < kMiddle) {
        bits |= 1U << kLeftUp;
    } else if (left_y > kMiddle) {
        bits |= 1U << kLeftDown;
    }
    return bits;
}

uint32_t stick_button_name(int index)
{
    static std::array<uint32_t, kStickButtonCount> names = {};
    if (core::at(names, index) == 0U) {
        core::at(names, index) = get_name(core::at(kPadNames, index));
    }
    return core::at(names, index);
}

void update_pad_sticks(const void* pad, std::span<const uint8_t> data)
{
    if (data.empty() && !get_pads().contains(pad)) {
        return;
    }
    PadSticks& sticks = sticks_of(pad);
    const uint32_t bits = !data.empty() ? stick_directions(data) : 0;
    // (THAW 0x596CA0: each a button down while its byte is set - full pressure)
    for (int i = 0; i < kStickButtonCount; ++i) {
        core::at(sticks.buttons, i).tick_frame((((bits >> i) & 1) != 0U) ? kPressedValue : 0);
    }
}

void* pad_stick_button(const void* pad, uint32_t name)
{
    for (int i = 0; i < kStickButtonCount; ++i) {
        if (stick_button_name(i) == name) {
            return &core::at(sticks_of(pad).buttons, i);
        }
    }
    return nullptr;
}

int stick_direction(uint32_t directions, bool right)
{
    int base = right ? kRightUp : kLeftUp;
    auto bit = [&](int direction_bit) -> uint8_t { return (directions >> (base + direction_bit)) & 1; };
    return objects::d_pad_direction(bit(0), bit(1), bit(2), bit(3));
}

uint32_t trick_stick_name(bool right, int direction)
{
    static std::array<std::array<uint32_t, kDirectionCount>, 2> names = {};
    if (direction < 1 || direction > kDirectionCount) {
        return 0;
    }
    uint32_t& name = core::at(core::at(names, right ? 1 : 0), direction - 1);
    if (name == 0U) {
        name = get_name(core::at(core::at(kStickTrickTable, right ? 1 : 0), direction - 1));
    }
    return name;
}

void update_trick_sticks(void* trick_component)
{
    auto* trick = static_cast<objects::MoveExecutor*>(trick_component);
    const SkaterPhysicsModule* core = trick->core_physics();
    BikeState* state = core ? state_of(core, false) : nullptr;
    if (!state) {
        return;
    }
    const bool riding = live_now(core);
    uint32_t bits = 0;
    if (riding) {
        const PadSticks& sticks = sticks_of(trick_pad(trick));
        for (int i = 0; i < kStickButtonCount; ++i) {
            if (core::at(sticks.buttons, i).held_down() != 0U) {
                bits |= 1U << i;
            }
        }
    }
    for (int stick = 0; stick < 2; ++stick) {
        const int was = core::at(state->trick_stick_, stick);
        const int now = stick_direction(bits, stick == 1);
        if (now == was) {
            continue;
        }
        core::at(state->trick_stick_, stick) = now;
        // (off the bike: forgotten, no events - THUG2's buffer as before)
        if (!riding) {
            continue;
        }
        // (THAW 0x471D20 sets the eight in order, as THUG2's d-pad: the lower-numbered change first)
        if ((was != 0) && ((now == 0) || was < now)) {
            add_stick_event(trick, trick_stick_name(stick == 1, was), false);
        }
        if (now != 0) {
            add_stick_event(trick, trick_stick_name(stick == 1, now), true);
        }
        if ((was != 0) && (now != 0) && was > now) {
            add_stick_event(trick, trick_stick_name(stick == 1, was), false);
        }
    }
}

int trick_stick_held(const void* trick_component, uint32_t name)
{
    for (int stick = 0; stick < 2; ++stick) {
        for (int direction = 1; direction <= kDirectionCount; ++direction) {
            if (trick_stick_name(stick == 1, direction) != name) {
                continue;
            }
            const BikeState* state = riding_state(static_cast<const objects::MoveExecutor*>(trick_component));
            if (!state) {
                return -1;
            }
            return core::at(state->trick_stick_, stick) == direction ? 1 : 0;
        }
    }
    return -1;
}

void end_trick_update(void* trick_component)
{
    // THAW 0x4723D0 (the end of the trick component's update): centred while none of the right stick's eight is down
    BikeState* state = riding_state(static_cast<const objects::MoveExecutor*>(trick_component));
    if (state) {
        state->right_stick_centred_ = core::at(state->trick_stick_, 1) == 0;
    }
}

bool bike_press_may_count(uint32_t last_button, uint32_t button, bool right_stick_centred)
{
    return last_button != button || right_stick_centred;
}

bool get_right_stick_centred(const void* trick_component)
{
    const BikeState* state = riding_state(static_cast<const objects::MoveExecutor*>(trick_component));
    return (state == nullptr) || state->right_stick_centred_;
}

LookInputs d_pad_look_inputs(bool up, bool down, bool left, bool right, bool invert)
{
    // THAW 0x5B08F0 (its constants 0x6DD0A0 1.5, 0x6DB3DC -1.0, 0x6ED5F4 -1.5): up before down, left before right
    const float down_tilt = down ? -1.0F : 0.0F;
    const float right_heading = right ? kLookStrength : 0.0F;
    LookInputs in{.tilt = up ? kLookStrength : down_tilt, .heading = left ? -kLookStrength : right_heading};
    if (invert) {
        in.tilt *= -1.0F;
        in.heading *= -1.0F;
    }
    return in;
}

namespace {
bool& dpad_camera()
{
    static bool dpad_camera{false};
    return dpad_camera;
}
} // namespace

void note_d_pad_camera(bool on) { dpad_camera() = on; }
bool d_pad_camera_noted() { return dpad_camera(); }

} // namespace skater::thaw_bmx
