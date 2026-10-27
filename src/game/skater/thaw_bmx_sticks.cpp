// NOT ORIGINAL - THAW's BMX ridden in THUG2 (game/skater/thaw_bmx.h): THAW's stick buttons - its pad's and its trick
// component's -, BikePress's re-arm rule and the look-around's d-pad inputs. Our addition at the user's request - not
// THUG2's code and not PartyMod's. Read from THAW's reference build (game/skater/thaw_bmx.h): the input
// server's 0x435530 / 0x4355B0, the pad update 0x596CA0, the trick component's 0x471D20 / 0x4723D0 / CheckTrigger
// 0x470EAB, the look-around's update 0x5B08F0. THUG2's own code calls these only while the feature is on.

#include <cstdint>
#include <unordered_map>

#include "game/core/crc.h"
#include "game/obj/control_pad.h"
#include "game/skater/thaw_bmx.h"
#include "game/skater/trick_component.h"

namespace Skater::ThawBmx {

namespace {

// THAW's pad button names (by StickButtonIndex) and its trick component's (by stick, by direction 1-8: up, down,
// left, right, up-left, up-right, down-left, down-right - DPadDirection's numbers).
const char* const kPadNames[8] = {"leftup", "leftdown", "leftleft", "leftright",
                                  "rightup", "rightdown", "rightleft", "rightright"};
const char* const kTrickNames[2][8] = {
    {"leftup", "leftdown", "leftleft", "leftright", "leftupleft", "leftupright", "leftdownleft", "leftdownright"},
    {"rightup", "rightdown", "rightleft", "rightright", "rightupleft", "rightupright", "rightdownleft",
     "rightdownright"},
};

uint32_t Name(const char* text) { return Crc::GenerateCRCFromString(text); }

constexpr uint8_t kMiddle = 0x80;
// THAW 0x435530: the right stick's direction past these (about 60% over; the left stick's: any way off the middle).
constexpr uint8_t kRightOver = 0xCB, kRightUnder = 0x35;

// A pad's eight stick buttons (THAW's pad +0x200..+0x2E0), beside it.
struct PadSticks {
    Obj::CControlButton buttons[8] = {};
};
std::unordered_map<const void*, PadSticks>& Pads()
{
    static std::unordered_map<const void*, PadSticks> pads;
    return pads;
}
PadSticks& SticksOf(const void* pad)
{
    PadSticks& sticks = Pads()[pad];
    for (int i = 0; i < 8; ++i)
        if (!sticks.buttons[i].m_name)
            sticks.buttons[i].m_name = StickButtonName(i);
    return sticks;
}

// The trick component's pad (the input component's, at its +0x20).
const void* TrickPad(const Obj::CTrickComponent* trick) { return trick->m_input + 0x20; }

// The bike state of `trick`'s skater while it rides the bike, else null.
BikeState* RidingState(const Obj::CTrickComponent* trick)
{
    const void* core = trick->m_corePhysics;
    if (!core || !Active(core))
        return nullptr;
    return StateOf(core, false);
}

// A press / release of a stick direction, as CTrickComponent::UpdateButton (0x0051F8F0) makes one for THUG2's
// buttons: not for an ignored button.
void AddStickEvent(Obj::CTrickComponent* trick, uint32_t name, bool pressed)
{
    for (int i = 0; i < trick->m_ignoredCount; ++i)
        if (trick->m_ignored[i] == name)
            return;
    trick->AddEvent(pressed ? 1 : 2)->button = name;
}

} // namespace

uint32_t StickDirections(const uint8_t* data)
{
    uint32_t bits = 0;
    const uint8_t rightX = data[0], rightY = data[1], leftX = data[2], leftY = data[3];
    // the right stick (THAW 0x435530): x past 0xCB right, under 0x35 left; y under 0x35 up, past 0xCB down
    if (rightX > kRightOver)
        bits |= 1u << kRightRight;
    else if (rightX < kRightUnder)
        bits |= 1u << kRightLeft;
    if (rightY < kRightUnder)
        bits |= 1u << kRightUp;
    else if (rightY > kRightOver)
        bits |= 1u << kRightDown;
    // the movement stick (0x4355B0): any way off the middle (the bytes come centred within the dead band)
    if (leftX > kMiddle)
        bits |= 1u << kLeftRight;
    else if (leftX < kMiddle)
        bits |= 1u << kLeftLeft;
    if (leftY < kMiddle)
        bits |= 1u << kLeftUp;
    else if (leftY > kMiddle)
        bits |= 1u << kLeftDown;
    return bits;
}

uint32_t StickButtonName(int index)
{
    static uint32_t names[8] = {};
    if (!names[index])
        names[index] = Name(kPadNames[index]);
    return names[index];
}

void UpdatePadSticks(const void* pad, const uint8_t* data)
{
    if (!data && !Pads().count(pad))
        return;
    PadSticks& sticks = SticksOf(pad);
    uint32_t bits = data ? StickDirections(data) : 0;
    // (THAW 0x596CA0: each a button down while its byte is set - full pressure)
    for (int i = 0; i < 8; ++i)
        sticks.buttons[i].Update((bits >> i) & 1 ? 0xFF : 0);
}

void* PadStickButton(const void* pad, uint32_t name)
{
    for (int i = 0; i < 8; ++i)
        if (StickButtonName(i) == name)
            return &SticksOf(pad).buttons[i];
    return nullptr;
}

int StickDirection(uint32_t directions, bool right)
{
    int base = right ? kRightUp : kLeftUp;
    auto bit = [&](int k) -> uint8_t { return (directions >> (base + k)) & 1; };
    return Obj::DPadDirection(bit(0), bit(1), bit(2), bit(3));
}

uint32_t TrickStickName(bool right, int direction)
{
    static uint32_t names[2][8] = {};
    if (direction < 1 || direction > 8)
        return 0;
    uint32_t& name = names[right ? 1 : 0][direction - 1];
    if (!name)
        name = Name(kTrickNames[right ? 1 : 0][direction - 1]);
    return name;
}

void UpdateTrickSticks(void* trickComponent)
{
    auto* trick = static_cast<Obj::CTrickComponent*>(trickComponent);
    const void* core = trick->m_corePhysics;
    BikeState* state = core ? StateOf(core, false) : nullptr;
    if (!state)
        return;
    bool riding = Active(core);
    uint32_t bits = 0;
    if (riding) {
        PadSticks& sticks = SticksOf(TrickPad(trick));
        for (int i = 0; i < 8; ++i)
            if (sticks.buttons[i].m_pressed)
                bits |= 1u << i;
    }
    for (int s = 0; s < 2; ++s) {
        int was = state->trickStick[s];
        int now = StickDirection(bits, s == 1);
        if (now == was)
            continue;
        state->trickStick[s] = now;
        // (off the bike: forgotten, no events - THUG2's buffer as before)
        if (!riding)
            continue;
        // (THAW 0x471D20 sets the eight in order, as THUG2's d-pad: the lower-numbered change first)
        if (was && (!now || was < now))
            AddStickEvent(trick, TrickStickName(s == 1, was), false);
        if (now)
            AddStickEvent(trick, TrickStickName(s == 1, now), true);
        if (was && now && was > now)
            AddStickEvent(trick, TrickStickName(s == 1, was), false);
    }
}

int TrickStickHeld(const void* trickComponent, uint32_t name)
{
    for (int s = 0; s < 2; ++s) {
        for (int d = 1; d <= 8; ++d) {
            if (TrickStickName(s == 1, d) != name)
                continue;
            const BikeState* state = RidingState(static_cast<const Obj::CTrickComponent*>(trickComponent));
            if (!state)
                return -1;
            return state->trickStick[s] == d ? 1 : 0;
        }
    }
    return -1;
}

void EndTrickUpdate(void* trickComponent)
{
    // THAW 0x4723D0 (the end of the trick component's update): centred while none of the right stick's eight is down
    BikeState* state = RidingState(static_cast<const Obj::CTrickComponent*>(trickComponent));
    if (state)
        state->rightStickCentred = state->trickStick[1] == 0;
}

bool BikePressMayCount(uint32_t lastButton, uint32_t button, bool rightStickCentred)
{
    return lastButton != button || rightStickCentred;
}

bool RightStickCentred(const void* trickComponent)
{
    const BikeState* state = RidingState(static_cast<const Obj::CTrickComponent*>(trickComponent));
    return !state || state->rightStickCentred;
}

LookInputs DPadLookInputs(bool up, bool down, bool left, bool right, bool invert)
{
    // THAW 0x5B08F0 (its constants 0x6DD0A0 1.5, 0x6DB3DC -1.0, 0x6ED5F4 -1.5): up before down, left before right
    LookInputs in{up ? 1.5f : (down ? -1.0f : 0.0f), left ? -1.5f : (right ? 1.5f : 0.0f)};
    if (invert) {
        in.tilt *= -1.0f;
        in.heading *= -1.0f;
    }
    return in;
}

namespace {
bool g_dpadCamera = false;
} // namespace

void NoteDPadCamera(bool on) { g_dpadCamera = on; }
bool DPadCameraNoted() { return g_dpadCamera; }

} // namespace Skater::ThawBmx
